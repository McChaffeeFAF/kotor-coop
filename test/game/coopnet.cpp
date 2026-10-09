/* Copyright (c) 2026 The reone project contributors */

#include <filesystem>
#include <functional>
#include <fstream>
#include <random>
#include <set>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/action.h"
#include "reone/game/coopcommand.h"
#include "reone/game/coopflow.h"
#include "reone/game/coopidmap.h"
#include "reone/game/cooploot.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/combat.h"
#include "reone/game/effect.h"
#include "reone/game/coopreplication.h"
#include "reone/game/coopsession.h"
#include "reone/game/coopprogress.h"
#include "reone/game/coopstandin.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/net/protocol.h"
#include "reone/resource/gff.h"
#include "reone/net/transport.h"
#include "reone/scene/collision.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace net = reone::net;

// ID mapping

TEST(CoopIdMap, binds_both_ways_and_rebinding_replaces) {
    NetIdMap ids;
    ids.bind(10, 100);
    ids.bind(11, 101);
    EXPECT_EQ(100u, *ids.local(10));
    EXPECT_EQ(11u, *ids.host(101));
    EXPECT_FALSE(ids.local(12).has_value());

    // A host id moves to a new local copy: the old local is released.
    ids.bind(10, 102);
    EXPECT_EQ(102u, *ids.local(10));
    EXPECT_FALSE(ids.host(100).has_value());

    // A local copy taken by another host id loses its old binding.
    ids.bind(12, 101);
    EXPECT_FALSE(ids.local(11).has_value());
    EXPECT_EQ(12u, *ids.host(101));
    EXPECT_EQ(2u, ids.size());

    ids.unbindHost(12);
    EXPECT_FALSE(ids.host(101).has_value());
    ids.clear();
    EXPECT_EQ(0u, ids.size());
}

TEST(CoopIdMap, load_keys_ignore_ids_and_match_across_installs) {
    // The same GIT loaded by two engines whose registries were at different
    // counters: different ids, the same keys in the same order.
    std::vector<LoadEntry> host {
        {40, 5, "end_trask", "end_trask", 12.34f, 56.78f},
        {41, 10, "end_door01", "end_door01", 1.0f, 2.0f},
        {42, 5, "end_sith", "end_sith01", 3.0f, 4.0f},
        {43, 5, "end_sith", "end_sith01", 3.0f, 4.0f},
    };
    auto client = host;
    for (auto &entry : client) {
        entry.id += 1000;
    }
    client[0].tag = "END_TRASK"; // tags compare case-insensitively
    auto hostKeys = assignLoadKeys(host);
    auto clientKeys = assignLoadKeys(client);
    ASSERT_EQ(hostKeys.size(), clientKeys.size());
    for (size_t i = 0; i < hostKeys.size(); ++i) {
        EXPECT_EQ(hostKeys[i].second, clientKeys[i].second);
        EXPECT_EQ(hostKeys[i].first + 1000, clientKeys[i].first);
    }
    EXPECT_EQ("5:end_trask:end_trask:123:568", hostKeys[0].second);
    // Identical twins are told apart by load order.
    EXPECT_EQ(hostKeys[2].second + "#1", hostKeys[3].second);
}

TEST(CoopIdMap, load_keys_separate_objects_by_position_and_type) {
    std::vector<LoadEntry> entries {
        {1, 5, "a", "t", 0.0f, 0.0f},
        {2, 5, "a", "t", 0.2f, 0.0f},
        {3, 9, "a", "t", 0.0f, 0.0f},
    };
    auto keys = assignLoadKeys(entries);
    EXPECT_NE(keys[0].second, keys[1].second);
    EXPECT_NE(keys[0].second, keys[2].second);
    EXPECT_EQ(std::string::npos, keys[1].second.find('#'));
}

TEST(CoopRateLimiter, allows_a_burst_per_second_then_slides) {
    CoopRateLimiter limiter;
    for (int i = 0; i < 15; ++i) {
        EXPECT_TRUE(limiter.allow(100, 30));
    }
    EXPECT_FALSE(limiter.allow(100, 30));
    EXPECT_FALSE(limiter.allow(129, 30));
    EXPECT_TRUE(limiter.allow(130, 30));
}

// Command validation and the host session

class CoopNetTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        ON_CALL(_sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                collision.intersection = glm::vec3(position.x, position.y, 0.0f);
                collision.user = nullptr;
                return true;
            }));
        EXPECT_CALL(_engine.resourceModule().scripts(), get(_))
            .Times(AnyNumber())
            .WillRepeatedly(Return(std::shared_ptr<script::ScriptProgram>()));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
        TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));

        _player = addCreature("player", glm::vec3(0.0f), Faction::Player);
        _game->party().addMember(kNpcPlayer, _player);
        _game->party().setPlayer(_player);
        _trask = addCreature("end_trask", glm::vec3(2.0f, 0.0f, 0.0f), Faction::Friendly1);
        _sith = addCreature("end_sith", glm::vec3(10.0f, 0.0f, 0.0f), Faction::Hostile1);
        _farSith = addCreature("end_sith_far", glm::vec3(500.0f, 0.0f, 0.0f), Faction::Hostile1);
        _door = _game->newDoor();
        _door->setTag("end_door02");
        _door->setPosition(glm::vec3(5.0f, 5.0f, 0.0f));
        _area->add(_door);
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, glm::vec3 position, Faction faction) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setPosition(position);
        creature->setFaction(faction);
        creature->setCurrentHitPoints(10);
        _area->add(creature);
        return creature;
    }

    void traskJoins() {
        _game->party().addAvailableMember(0, _trask);
        ASSERT_TRUE(_game->party().addMember(0, _trask));
    }

    net::CommandReject check(net::CommandType type, uint32_t target = 0, uint32_t controlled = 0, uint32_t actor = 0) {
        net::Command command;
        command.type = type;
        command.target = target;
        command.actor = actor ? actor : _trask->id();
        command.point = {3.0f, 1.0f, 0.0f};
        CoopCommandTargets targets;
        return validateCoopCommand(*_game, command, controlled ? controlled : _trask->id(), targets);
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _player;
    std::shared_ptr<Creature> _trask;
    std::shared_ptr<Creature> _sith;
    std::shared_ptr<Creature> _farSith;
    std::shared_ptr<Door> _door;
};

TEST_F(CoopNetTest, a_client_drives_only_its_own_live_party_creature) {
    traskJoins();
    net::Command command;
    command.type = net::CommandType::Cancel;
    command.actor = _trask->id();
    CoopCommandTargets targets;
    EXPECT_EQ(net::CommandReject::NoSlot, validateCoopCommand(*_game, command, 0, targets));
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::Cancel));
    // Claiming the host's own character.
    EXPECT_EQ(net::CommandReject::NotControlled, check(net::CommandType::Cancel, 0, _trask->id(), _player->id()));
    // The slot names a creature that is not in the party.
    EXPECT_EQ(net::CommandReject::ActorUnavailable, check(net::CommandType::Cancel, 0, _sith->id(), _sith->id()));
    _trask->setCurrentHitPoints(0);
    EXPECT_EQ(net::CommandReject::ActorUnavailable, check(net::CommandType::Cancel));
}

TEST_F(CoopNetTest, story_state_the_host_owns_blocks_input) {
    traskJoins();
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::MoveTo));
    // Menus and containers do not pause co-op, so they do not block input.
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGameMenu));
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::MoveTo));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    _trask->setCommandable(false);
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::MoveTo));
}

TEST_F(CoopNetTest, targets_must_exist_in_the_area_within_reach) {
    traskJoins();
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::Attack, _sith->id()));
    EXPECT_EQ(net::CommandReject::TargetTooFar, check(net::CommandType::Attack, _farSith->id()));
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, 0));
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, 999999));
    // No friendly fire on the party, and not on oneself.
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, _player->id()));
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, _trask->id()));
    _sith->setCurrentHitPoints(0);
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, _sith->id()));
    // An object the area does not hold.
    auto elsewhere = _game->newCreature();
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::Attack, elsewhere->id()));
}

TEST_F(CoopNetTest, doors_points_items_feats_and_powers_are_checked) {
    traskJoins();
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::OpenDoor, _door->id()));
    EXPECT_EQ(net::CommandReject::TargetInvalid, check(net::CommandType::OpenDoor, _sith->id()));
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::UseObject, _door->id()));

    net::Command far;
    far.type = net::CommandType::MoveTo;
    far.actor = _trask->id();
    far.point = {400.0f, 0.0f, 0.0f};
    CoopCommandTargets targets;
    EXPECT_EQ(net::CommandReject::PointInvalid, validateCoopCommand(*_game, far, _trask->id(), targets));

    net::Command item;
    item.type = net::CommandType::UseItem;
    item.actor = _trask->id();
    item.item = "g_i_medeqpmnt01";
    EXPECT_EQ(net::CommandReject::ItemUnavailable, validateCoopCommand(*_game, item, _trask->id(), targets));

    net::Command feat;
    feat.type = net::CommandType::UseFeat;
    feat.actor = _trask->id();
    feat.target = _sith->id();
    feat.param = 8;
    EXPECT_EQ(net::CommandReject::FeatUnavailable, validateCoopCommand(*_game, feat, _trask->id(), targets));

    net::Command power;
    power.type = net::CommandType::UsePower;
    power.actor = _trask->id();
    power.param = 5;
    EXPECT_EQ(net::CommandReject::PowerUnavailable, validateCoopCommand(*_game, power, _trask->id(), targets));
}

TEST_F(CoopNetTest, skills_go_on_objects_and_only_the_object_skills) {
    traskJoins();
    net::Command skill;
    skill.type = net::CommandType::UseSkill;
    skill.actor = _trask->id();
    skill.target = _door->id();
    skill.param = static_cast<int>(SkillType::Security);
    CoopCommandTargets targets;
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, skill, _trask->id(), targets));
    executeCoopCommand(*_game, skill, targets);
    ASSERT_EQ(1u, _trask->actions().size());
    EXPECT_EQ(ActionType::UseSkill, _trask->actions().front()->type());

    skill.param = static_cast<int>(SkillType::Persuade);
    EXPECT_EQ(net::CommandReject::SkillUnavailable, validateCoopCommand(*_game, skill, _trask->id(), targets));
    skill.param = 99;
    EXPECT_EQ(net::CommandReject::SkillUnavailable, validateCoopCommand(*_game, skill, _trask->id(), targets));
    skill.param = static_cast<int>(SkillType::Security);
    skill.target = _sith->id();
    EXPECT_EQ(net::CommandReject::TargetInvalid, validateCoopCommand(*_game, skill, _trask->id(), targets));
    skill.target = 0;
    EXPECT_EQ(net::CommandReject::TargetInvalid, validateCoopCommand(*_game, skill, _trask->id(), targets));
}

TEST_F(CoopNetTest, disarm_trap_requires_control_detection_and_reach) {
    traskJoins();
    auto trap = _game->newTrigger();
    trap->setTag("trap01");
    trap->configureMine(3, glm::vec3(3.0f, 1.0f, 0.0f), Faction::Hostile1, _sith->id(), 20, 20);
    _area->add(trap);

    net::Command command;
    command.type = net::CommandType::DisarmTrap;
    command.target = trap->id();
    command.actor = _trask->id();
    CoopCommandTargets targets;

    // Not detected yet: the target menu would not even offer it.
    EXPECT_EQ(net::CommandReject::TargetInvalid, validateCoopCommand(*_game, command, _trask->id(), targets));

    trap->setTrapDetectedBy(_trask->id(), true);
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, command, _trask->id(), targets));
    executeCoopCommand(*_game, command, targets);
    ASSERT_EQ(1u, _trask->actions().size());
    EXPECT_EQ(ActionType::DisableTrap, _trask->actions().front()->type());

    // A trigger that is not a trap at all.
    auto plain = _game->newTrigger();
    plain->setTag("plain01");
    plain->setPosition(glm::vec3(3.0f, 1.0f, 0.0f));
    _area->add(plain);
    command.target = plain->id();
    EXPECT_EQ(net::CommandReject::TargetInvalid, validateCoopCommand(*_game, command, _trask->id(), targets));

    // Too far to reach.
    auto farTrap = _game->newTrigger();
    farTrap->setTag("trap02");
    farTrap->configureMine(3, glm::vec3(500.0f, 0.0f, 0.0f), Faction::Hostile1, _sith->id(), 20, 20);
    farTrap->setTrapDetectedBy(_trask->id(), true);
    _area->add(farTrap);
    command.target = farTrap->id();
    EXPECT_EQ(net::CommandReject::TargetTooFar, validateCoopCommand(*_game, command, _trask->id(), targets));

    // A client may only drive its own creature.
    command.target = trap->id();
    EXPECT_EQ(net::CommandReject::NotControlled, check(net::CommandType::DisarmTrap, trap->id(), _trask->id(), _player->id()));
}

TEST_F(CoopNetTest, validated_commands_queue_the_actions_a_click_queues) {
    traskJoins();
    net::Command attack;
    attack.type = net::CommandType::Attack;
    attack.actor = _trask->id();
    attack.target = _sith->id();
    CoopCommandTargets targets;
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, attack, _trask->id(), targets));
    executeCoopCommand(*_game, attack, targets);
    ASSERT_EQ(1u, _trask->actions().size());
    EXPECT_EQ(ActionType::AttackObject, _trask->actions().front()->type());
    EXPECT_TRUE(_trask->actions().front()->isUserAction());

    net::Command move;
    move.type = net::CommandType::MoveTo;
    move.actor = _trask->id();
    move.point = {4.0f, 3.0f, 0.0f};
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, move, _trask->id(), targets));
    executeCoopCommand(*_game, move, targets);
    ASSERT_EQ(1u, _trask->actions().size()) << "a new command replaces the old one";
    EXPECT_EQ(ActionType::MoveToLocation, _trask->actions().front()->type());

    net::Command cancel;
    cancel.type = net::CommandType::Cancel;
    cancel.actor = _trask->id();
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, cancel, _trask->id(), targets));
    executeCoopCommand(*_game, cancel, targets);
    EXPECT_TRUE(_trask->actions().empty());
}

namespace {

/** The test plays the client end of a loopback link with raw messages. */
struct RawClient {
    std::unique_ptr<net::ITransport> transport;
    net::ConnectionId connection {0};
    std::vector<std::vector<uint8_t>> inbox;
    bool autoAck {true};

    void poll() {
        std::vector<net::TransportEvent> events;
        transport->poll(events);
        for (auto &event : events) {
            if (event.type == net::TransportEventType::Connected) {
                connection = event.connection;
            } else if (event.type == net::TransportEventType::Message) {
                if (autoAck && !event.data.empty() && event.data[0] == static_cast<uint8_t>(net::MessageType::Snapshot)) {
                    // As a client does: acknowledge every snapshot decoded.
                    net::Snapshot snapshot;
                    if (net::decode(event.data, snapshot)) {
                        net::SnapshotAck ack {snapshot.generation, snapshot.tick};
                        transport->send(connection, net::encode(ack), net::SendMode::Unreliable);
                    }
                }
                inbox.push_back(std::move(event.data));
            } else {
                inbox.push_back({0});
            }
        }
    }

    template <class T>
    void send(const T &message) { transport->send(connection, net::encode(message), net::SendMode::Reliable); }

    template <class T>
    std::vector<T> take(net::MessageType type) {
        std::vector<T> result;
        for (auto &bytes : inbox) {
            if (!bytes.empty() && bytes[0] == static_cast<uint8_t>(type)) {
                T message;
                if (net::decode(bytes, message)) {
                    result.push_back(message);
                }
            }
        }
        return result;
    }
};

} // namespace

class CoopSessionTest : public CoopNetTest {
protected:
    void SetUp() override {
        CoopNetTest::SetUp();
        auto [host, client] = net::createLoopbackPair();
        _client.transport = std::move(client);
        CoopConfig config;
        config.role = CoopRole::Host;
        config.build = "build-A";
        config.k1 = {true, 1, 2, 3};
        _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
        _session->pollNetwork();
        _client.poll();
    }

    net::Hello hello() {
        net::Hello message;
        message.build = "build-A";
        message.mode = "story";
        message.playerName = "Guest";
        message.k1 = {true, 1, 2, 3};
        return message;
    }

    void exchange() {
        _session->pollNetwork();
        _client.poll();
    }

    void tick() {
        _session->pollNetwork(); // the client's acks of the last tick
        TestGameModule::advanceTick(*_game);
        _session->beforeTick();
        _session->afterTick();
        _client.poll();
    }

    RawClient _client;
    std::shared_ptr<CoopSession> _session;
};

// The generations of the ModuleInfo messages the client received. Read raw:
// the test modules have no name, which a client's decoder refuses.
static std::vector<uint32_t> moduleGenerations(const RawClient &client) {
    std::vector<uint32_t> result;
    for (auto &bytes : client.inbox) {
        if (bytes.size() >= 5 && bytes[0] == static_cast<uint8_t>(net::MessageType::ModuleInfo)) {
            result.push_back(static_cast<uint32_t>(bytes[1]) | (static_cast<uint32_t>(bytes[2]) << 8) |
                             (static_cast<uint32_t>(bytes[3]) << 16) | (static_cast<uint32_t>(bytes[4]) << 24));
        }
    }
    return result;
}

TEST_F(CoopSessionTest, a_new_module_is_announced_even_where_the_old_one_was_freed) {
    _client.send(hello());
    exchange();
    tick();
    auto first = moduleGenerations(_client);
    ASSERT_EQ(1u, first.size());

    // A module load frees the old module before making the new one, and the
    // allocator is free to put the new one at the same address (it did in
    // the two-player network probe at the end_m01aa exit: the client was never told).
    // Loads in a row; each must reach the client as a new generation,
    // including the ones that land at an address a freed module had.
    std::set<const void *> seen {TestGameModule::activeModuleAddress(*_game)};
    int reused = 0;
    uint32_t generation = first[0];
    for (int i = 0; i < 32; ++i) {
        TestGameModule::replaceActiveModule(*_game, _game->newArea());
        reused += seen.insert(TestGameModule::activeModuleAddress(*_game)).second ? 0 : 1;
        _client.inbox.clear();
        tick();
        auto next = moduleGenerations(_client);
        ASSERT_EQ(1u, next.size()) << "load " << i;
        EXPECT_EQ(++generation, next[0]);
    }
    RecordProperty("reused_addresses", reused);
}

TEST_F(CoopSessionTest, handshake_rejects_other_builds_files_and_modes) {
    auto message = hello();
    message.k1.key = 99;
    _client.send(message);
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::FilesMismatchK1, rejects[0].reason);
    EXPECT_NE(std::string::npos, rejects[0].detail.find("key"));
}

TEST_F(CoopSessionTest, handshake_rejects_other_modules) {
    auto message = hello();
    message.k1.modules = 99;
    _client.send(message);
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::FilesMismatchK1, rejects[0].reason);
    EXPECT_NE(std::string::npos, rejects[0].detail.find("modules"));
}

// A French player and a German one hold the same modules and scripts; only dialog.tlk (the
// strings) differs. That is a warning, as it is for K2, not a refusal.
TEST_F(CoopSessionTest, handshake_lets_installs_in_other_languages_join) {
    auto message = hello();
    message.k1.tlk = 99;
    _client.send(message);
    exchange();
    EXPECT_TRUE(_client.take<net::Reject>(net::MessageType::Reject).empty());
    EXPECT_EQ(1u, _client.take<net::Welcome>(net::MessageType::Welcome).size());
}

TEST_F(CoopSessionTest, handshake_rejects_a_different_build) {
    auto message = hello();
    message.build = "build-B";
    _client.send(message);
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::BuildMismatch, rejects[0].reason);
}

TEST_F(CoopSessionTest, handshake_rejects_free_mode_and_k2_mismatch) {
    auto message = hello();
    message.mode = "free";
    _client.send(message);
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::ModeMismatch, rejects[0].reason);
}

TEST_F(CoopSessionTest, a_slot_waits_for_a_companion_then_commands_reach_the_host) {
    _client.send(hello());
    exchange();
    ASSERT_EQ(1u, _client.take<net::Welcome>(net::MessageType::Welcome).size());
    net::SlotRequest request;
    _client.send(request);
    exchange();
    auto slots = _client.take<net::SlotAssign>(net::MessageType::SlotAssign);
    ASSERT_EQ(1u, slots.size());
    EXPECT_EQ(net::SlotStatus::Waiting, slots[0].status);

    // Trask joins the party: the next tick hands him to the client.
    traskJoins();
    _client.inbox.clear();
    tick();
    slots = _client.take<net::SlotAssign>(net::MessageType::SlotAssign);
    ASSERT_EQ(1u, slots.size());
    EXPECT_EQ(net::SlotStatus::Assigned, slots[0].status);
    EXPECT_EQ(_trask->id(), slots[0].creatureId);
    EXPECT_EQ("end_trask", slots[0].tag);

    net::Command attack;
    attack.seq = 1;
    attack.type = net::CommandType::Attack;
    attack.actor = _trask->id();
    attack.target = _sith->id();
    _client.send(attack);
    net::Command impostor;
    impostor.seq = 2;
    impostor.type = net::CommandType::MoveTo;
    impostor.actor = _player->id();
    _client.send(impostor);
    _session->pollNetwork();
    EXPECT_TRUE(_trask->actions().empty()) << "commands wait for the next tick";
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_EQ(2u, results.size());
    EXPECT_TRUE(results[0].accepted);
    EXPECT_EQ(1u, results[0].seq);
    EXPECT_FALSE(results[1].accepted);
    EXPECT_EQ(net::CommandReject::NotControlled, results[1].reason);
    ASSERT_EQ(1u, _trask->actions().size());
    EXPECT_EQ(ActionType::AttackObject, _trask->actions().front()->type());
    EXPECT_TRUE(_player->actions().empty());
}

TEST_F(CoopSessionTest, snapshots_start_full_then_carry_only_changes) {
    _client.send(hello());
    exchange();
    _client.inbox.clear();
    tick();
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    EXPECT_TRUE(snapshots[0].full());
    EXPECT_EQ(net::kWorldAllMask, snapshots[0].worldFields);
    std::set<uint32_t> ids;
    for (auto &record : snapshots[0].entities) {
        ids.insert(record.state.id);
        EXPECT_TRUE(record.fields & net::kFieldIdentity);
    }
    for (auto &object : {std::static_pointer_cast<Object>(_player), std::static_pointer_cast<Object>(_trask),
                         std::static_pointer_cast<Object>(_sith), std::static_pointer_cast<Object>(_door)}) {
        EXPECT_TRUE(ids.count(object->id())) << object->tag();
    }
    for (auto &record : snapshots[0].entities) {
        if (record.state.id == _player->id()) {
            EXPECT_EQ(kPlayerLoadKey, record.state.key);
        }
    }

    // Nothing changed: a snapshot still goes out to carry the tick.
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    EXPECT_FALSE(snapshots[0].full());
    EXPECT_TRUE(snapshots[0].entities.empty());
    EXPECT_EQ(0, snapshots[0].worldFields);

    // One creature moves and is hurt: exactly those fields travel.
    _sith->setPosition(glm::vec3(11.0f, 0.0f, 0.0f));
    _sith->setCurrentHitPoints(4);
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    ASSERT_EQ(1u, snapshots[0].entities.size());
    EXPECT_EQ(_sith->id(), snapshots[0].entities[0].state.id);
    EXPECT_EQ(net::kFieldPosition | net::kFieldVitals, snapshots[0].entities[0].fields);
    EXPECT_EQ(11.0f, snapshots[0].entities[0].state.position.x);
    EXPECT_EQ(4, snapshots[0].entities[0].state.hp);

    // It leaves the area: the client is told to drop it.
    _area->destroyObject(*_sith);
    CoopSession::flushDestroyedObjects(*_area); // what Area::update does on the host
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    bool removed = false;
    for (auto &record : snapshots[0].entities) {
        removed |= record.state.id == _sith->id() && record.fields == net::kFieldRemoved;
    }
    EXPECT_TRUE(removed);
}

TEST_F(CoopSessionTest, placeables_built_in_code_travel_by_appearance) {
    // Built in code, as a body bag is: no template.
    auto bag = _game->newPlaceable();
    bag->setTag("Body Bag");
    ASSERT_TRUE(bag->blueprintResRef().empty());
    _area->add(bag);
    _client.send(hello());
    exchange();
    _client.inbox.clear();
    tick();
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    bool found = false;
    for (auto &record : snapshots[0].entities) {
        if (record.state.id == bag->id()) {
            found = true;
            EXPECT_EQ(std::string(kAppearancePrefix) + std::to_string(bag->appearance()), record.state.resref);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(CoopSessionTest, malformed_traffic_gets_a_client_kicked) {
    _client.send(hello());
    exchange();
    for (int i = 0; i < 8; ++i) {
        _client.transport->send(_client.connection, {static_cast<uint8_t>(net::MessageType::Snapshot), 1, 2}, net::SendMode::Reliable);
    }
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::Kicked, rejects[0].reason);
    _session->pollNetwork(); // the close lands on the next poll
    auto status = _session->statusJson();
    EXPECT_EQ(0u, status.at("clients").as_array().size());
}

TEST_F(CoopSessionTest, a_client_session_suppresses_scripts) {
    auto [host, client] = net::createLoopbackPair();
    CoopConfig config;
    config.role = CoopRole::Client;
    auto game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
    game->initLocalServices();
    ASSERT_FALSE(game->scriptRunner().isSuppressed());
    CoopSession session(*game, config, std::move(client));
    EXPECT_TRUE(game->scriptRunner().isSuppressed());
    EXPECT_EQ(-1, game->scriptRunner().run("k_any_script"));
}

void reone::game::TestGameModule::advanceTick(Game &game) {
    ++game._tickCount;
}

void reone::game::TestGameModule::setDoorLink(Door &door, std::string module, std::string entry) {
    door._linkedToModule = std::move(module);
    door._linkedTo = std::move(entry);
}

// N2: ready check, enemy scaling, pause cooldown

TEST(CoopReadyCheck, everyone_ready_passes_at_once) {
    CoopReadyCheck check;
    check.start(1, 0, "end_m01ab", 10.0f, {{0, "Host"}, {1, "Guest"}});
    EXPECT_TRUE(check.active());
    EXPECT_EQ("Host", check.initiatorName());
    EXPECT_EQ(CoopReadyCheck::Outcome::Pending, check.evaluate(11.0f));
    EXPECT_EQ(net::ReadyAnswer::Ready, check.players()[0].answer) << "the initiator is ready";
    EXPECT_TRUE(check.answer(1, true));
    EXPECT_EQ(CoopReadyCheck::Outcome::Passed, check.evaluate(12.0f));
    EXPECT_FALSE(check.active());
    EXPECT_EQ(net::ReadyState::Passed, check.message(12.0f).state);
    EXPECT_FALSE(check.answer(1, false)) << "an ended check takes no answers";
}

TEST(CoopReadyCheck, a_majority_carries_after_the_timeout_and_a_tie_does_not) {
    CoopReadyCheck three;
    three.start(2, 1, "end_m01ab", 0.0f, {{0, "Host"}, {1, "A"}, {2, "B"}});
    three.answer(0, true);
    EXPECT_EQ(CoopReadyCheck::Outcome::Pending, three.evaluate(29.9f));
    EXPECT_EQ(30, three.secondsLeft(0.0f));
    EXPECT_EQ(CoopReadyCheck::Outcome::Passed, three.evaluate(30.0f)) << "2 of 3 ready";

    CoopReadyCheck two;
    two.start(3, 1, "end_m01ab", 0.0f, {{0, "Host"}, {1, "A"}});
    two.answer(0, false);
    EXPECT_EQ(CoopReadyCheck::Outcome::Cancelled, two.evaluate(31.0f)) << "1 of 2 is no majority";
    EXPECT_EQ(net::ReadyState::Cancelled, two.message(31.0f).state);
}

TEST(CoopReadyCheck, the_host_can_force_and_leavers_stop_counting) {
    CoopReadyCheck forced;
    forced.start(4, 1, "end_m01ab", 0.0f, {{0, "Host"}, {1, "A"}, {2, "B"}});
    forced.answer(2, false);
    forced.force();
    EXPECT_EQ(CoopReadyCheck::Outcome::Passed, forced.evaluate(1.0f));

    CoopReadyCheck left;
    left.start(5, 0, "end_m01ab", 0.0f, {{0, "Host"}, {1, "A"}});
    left.removePlayer(1);
    EXPECT_EQ(CoopReadyCheck::Outcome::Passed, left.evaluate(1.0f)) << "only ready players remain";

    CoopReadyCheck busy;
    busy.start(6, 0, "end_m01ab", 0.0f, {{0, "Host"}, {1, "A"}});
    busy.setBusy(1, true);
    auto message = busy.message(0.0f);
    ASSERT_EQ(2u, message.players.size());
    EXPECT_TRUE(message.players[1].busy);
    EXPECT_EQ(net::ReadyState::Active, message.state);
    busy.addPlayer({2, "Late"});
    EXPECT_EQ(3u, busy.players().size());
}

TEST(CoopEnemyScaling, follows_the_design_steps_per_player) {
    auto near = [](float a, float b) { return std::fabs(a - b) < 1e-4f; };
    EXPECT_TRUE(near(1.0f, coopEnemyScale(1).hp));
    EXPECT_TRUE(near(1.0f, coopEnemyScale(1).damage));
    EXPECT_TRUE(near(1.5f, coopEnemyScale(2).hp));
    EXPECT_TRUE(near(1.05f, coopEnemyScale(2).damage));
    EXPECT_TRUE(near(2.0f, coopEnemyScale(3).hp));
    EXPECT_TRUE(near(1.10f, coopEnemyScale(3).damage));
    EXPECT_TRUE(near(2.05f, coopEnemyScale(4).hp));
    EXPECT_TRUE(near(1.125f, coopEnemyScale(4).damage));
    EXPECT_TRUE(near(2.25f, coopEnemyScale(8).hp));
    EXPECT_TRUE(near(1.225f, coopEnemyScale(8).damage));
    EXPECT_TRUE(near(2.25f, coopEnemyScale(12).hp)) << "capped at 8 players";
    EXPECT_TRUE(near(1.0f, coopEnemyScale(3, 0.0f).hp)) << "the host turned it off";
    EXPECT_TRUE(near(1.5f, coopEnemyScale(3, 0.5f).hp));
}

TEST(CoopPauseCooldown, holds_each_player_back_on_their_own_clock) {
    CoopPauseCooldown none;
    none.recordPause(1, 0.0f);
    EXPECT_TRUE(none.mayPause(1, 0.1f));
    CoopPauseCooldown cooldown(10.0f);
    EXPECT_TRUE(cooldown.mayPause(1, 0.0f));
    cooldown.recordPause(1, 0.0f);
    EXPECT_FALSE(cooldown.mayPause(1, 5.0f));
    EXPECT_TRUE(cooldown.mayPause(2, 5.0f)) << "another player is not held";
    EXPECT_TRUE(cooldown.mayPause(1, 10.0f));
}

TEST_F(CoopNetTest, uses_are_classified_containers_exits_and_conversations) {
    traskJoins();
    net::Command use;
    use.type = net::CommandType::UseObject;
    use.actor = _trask->id();
    CoopCommandTargets targets;

    // A dead body with loot opens the client's own container screen.
    auto body = addCreature("end_sith_dead", glm::vec3(4.0f, 0.0f, 0.0f), Faction::Hostile1);
    body->addItem(_game->newItem());
    body->items().back()->setDropable(true);
    body->setCurrentHitPoints(0);
    use.target = body->id();
    EXPECT_EQ(net::CommandReject::None, validateCoopCommand(*_game, use, _trask->id(), targets));
    EXPECT_EQ(CoopUse::Container, targets.use);
    executeCoopCommand(*_game, use, targets);
    EXPECT_TRUE(_trask->actions().empty()) << "the session carries containers out, not executeCoopCommand";

    // A friendly with a conversation: the host leads conversations.
    auto talker = addCreature("end_talker", glm::vec3(3.0f, 3.0f, 0.0f), Faction::Friendly1);
    talker->setConversation("end_talk01");
    use.target = talker->id();
    EXPECT_EQ(net::CommandReject::HostLeadsConversation, validateCoopCommand(*_game, use, _trask->id(), targets));

    // A transition door is an exit: the ready check.
    TestGameModule::setDoorLink(*_door, "end_m01ab", "end_wp01");
    use.target = _door->id();
    EXPECT_EQ(net::CommandReject::None, validateCoopCommand(*_game, use, _trask->id(), targets));
    EXPECT_EQ(CoopUse::Transition, targets.use);
}

TEST_F(CoopSessionTest, a_controlled_companion_leaves_the_ai_until_its_player_drops_and_rejoins) {
    traskJoins();
    _client.send(hello());
    exchange();
    net::SlotRequest request;
    _client.send(request);
    exchange();
    tick();
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_FALSE(_session->isPlayerControlled(_player->id()));

    // The player drops: the AI takes Trask back, and his slot is kept.
    _client.transport->close(_client.connection, "gone");
    exchange();
    tick();
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id()));
    auto status = _session->statusJson();
    EXPECT_EQ("end_trask", std::string(status.at("reservations").as_object().at("Guest").as_string()));
}

// Co-op companion combat: a creature a client controls is a player's hand,
// like the host's leader. It takes no party-AI orders, so the engine itself
// must keep it fighting.
class CoopCompanionCombatTest : public CoopSessionTest {
protected:
    void SetUp() override {
        CoopSessionTest::SetUp();
        ON_CALL(_engine.gameModule().reputes(), getIsEnemy(An<const Creature &>(), An<const Creature &>()))
            .WillByDefault(Invoke([](const Creature &source, const Creature &target) {
                return (source.faction() == Faction::Hostile1) != (target.faction() == Faction::Hostile1);
            }));
        _game->setCoopSession(_session);
    }

    void TearDown() override {
        _game->setCoopSession(nullptr);
        CoopSessionTest::TearDown();
    }

    void traskTaken() {
        traskJoins();
        _client.send(hello());
        exchange();
        net::SlotRequest request;
        _client.send(request);
        exchange();
        tick();
        ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    }

    // A round the Sith fights against the PC: the party is under attack.
    void sithAttacksPlayer() {
        auto attack = _game->newAction<AttackObjectAction>(_player);
        _sith->addAction(attack);
        _game->combat().addAction(attack, *_sith);
        ASSERT_EQ(_player, _sith->getAttackTarget());
    }

    void finishRounds() {
        _game->combat().update(0.0f);
        _game->combat().update(1.5f);
        _game->combat().update(1.5f);
    }

    void idle(float seconds) {
        for (float t = 0.0f; t < seconds; t += 0.25f) {
            _trask->updateCombatHandover(0.25f);
        }
    }

    std::shared_ptr<Object> traskTarget() const {
        auto action = _trask->getCurrentAction();
        auto *attack = action ? dyn_cast<AttackObjectAction>(action.get()) : nullptr;
        return attack ? attack->target() : nullptr;
    }
};

TEST_F(CoopCompanionCombatTest, a_client_s_companion_keeps_attacking_at_the_end_of_a_round_as_the_leader_does) {
    traskTaken();
    auto attack = _game->newAction<AttackObjectAction>(_sith);
    _trask->addAction(attack);
    _game->combat().addAction(attack, *_trask);
    finishRounds();
    // The round's action is still running: the end of the round queues the
    // next attack on the same target (no OnEndRound script for this hand).
    ASSERT_EQ(2u, _trask->actions().size());
    auto *next = dyn_cast<AttackObjectAction>(_trask->actions().back().get());
    ASSERT_TRUE(next);
    EXPECT_EQ(_sith, next->target());

    // Cancel Combat holds the continuation back.
    _trask->clearAllActions();
    auto again = _game->newAction<AttackObjectAction>(_sith);
    _trask->addAction(again);
    _game->combat().addAction(again, *_trask);
    _trask->cancelCombatNow();
    _trask->addAction(again); // as if the swing were still locked in
    EXPECT_TRUE(_trask->isCombatHeld());
    finishRounds();
    EXPECT_LE(_trask->actions().size(), 1u);
}

TEST_F(CoopCompanionCombatTest, a_companion_without_a_player_gets_no_continuation) {
    traskJoins(); // the AI's companion: OnEndRound decides its next round
    auto attack = _game->newAction<AttackObjectAction>(_sith);
    _trask->addAction(attack);
    _game->combat().addAction(attack, *_trask);
    finishRounds();
    EXPECT_EQ(1u, _trask->actions().size());
}

TEST_F(CoopCompanionCombatTest, a_client_s_auto_combat_companion_retries_the_ai_when_idle) {
    traskTaken();
    _trask->setOnEndRound("k_ai_test_round");
    _trask->setObjectSeen(_sith, true);
    // Auto-combat off: the player's hand, no AI round.
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_ai_test_round")).Times(0);
    idle(3.5f);
    Mock::VerifyAndClearExpectations(&_engine.resourceModule().scripts());
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<script::ScriptProgram>()));
    // Auto-combat on: with nothing queued and an enemy in sight, the AI is
    // asked for a round, as it is for the host's leader.
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_ai_test_round"))
        .Times(AtLeast(1))
        .WillRepeatedly(Return(std::shared_ptr<script::ScriptProgram>()));
    _game->setAutoCombat(*_trask, true);
    idle(3.5f);
    Mock::VerifyAndClearExpectations(&_engine.resourceModule().scripts());
    EXPECT_CALL(_engine.resourceModule().scripts(), get(_))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<script::ScriptProgram>()));
    EXPECT_FALSE(_trask->isCombatHandoverPending());
    _game->setAutoCombat(*_trask, false);
    _trask->requestCombatHandover();
    idle(0.25f);
    EXPECT_FALSE(_trask->isCombatHandoverPending()) << "under the player's hand: nothing to hand over";
}

TEST_F(CoopCompanionCombatTest, an_idle_client_s_companion_joins_a_fight_against_the_party) {
    traskTaken();
    _trask->setObjectSeen(_sith, true);
    _trask->setObjectSeen(_farSith, true);

    // An enemy in sight that fights nobody: no fight to join.
    idle(2.0f);
    EXPECT_TRUE(_trask->actions().empty());

    sithAttacksPlayer();
    idle(0.5f);
    EXPECT_TRUE(_trask->actions().empty()) << "a moment's idle first";
    idle(0.75f);
    EXPECT_EQ(_sith, traskTarget()) << "the nearest hostile, not the far one";
    EXPECT_FALSE(_trask->getCurrentAction()->isUserAction());
}

TEST_F(CoopCompanionCombatTest, idle_engage_stays_out_of_the_player_s_way) {
    traskTaken();
    _trask->setObjectSeen(_sith, true);
    sithAttacksPlayer();

    // An order of the player's own runs untouched.
    auto order = _game->newAction<AttackObjectAction>(_farSith);
    order->setUserAction(true);
    _trask->addAction(order);
    idle(2.0f);
    ASSERT_EQ(1u, _trask->actions().size());
    EXPECT_EQ(_farSith, traskTarget());
    _trask->clearAllActions();

    // Cancel Combat: held while the enemy is in sight and no new order came.
    _trask->cancelCombatNow();
    idle(2.0f);
    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_TRUE(_trask->isCombatHeld());
    // A new order from the player releases the hold.
    order = _game->newAction<AttackObjectAction>(_sith);
    order->setUserAction(true);
    _trask->addAction(order);
    idle(0.25f);
    EXPECT_FALSE(_trask->isCombatHeld());
    _trask->clearAllActions();

    // Solo mode keeps the other characters still.
    _game->party().setSoloMode(true);
    idle(2.0f);
    EXPECT_TRUE(_trask->actions().empty());
    _game->party().setSoloMode(false);

    // So does stealth.
    auto stealth = std::make_shared<Effect>(EffectType::Invisibility);
    _trask->applyEffect(stealth, DurationType::Permanent);
    idle(2.0f);
    EXPECT_TRUE(_trask->actions().empty());
}

// A companion under the party AI whose round ended with nothing queued joins
// the fight too (tar_m09aa: Bastila idle 4 m behind the PC for a whole fight).
// With no round script it falls back on a plain attack on the nearest enemy.
TEST_F(CoopCompanionCombatTest, an_idle_party_ai_companion_joins_a_fight_against_the_party) {
    traskJoins(); // no client: the party AI's companion
    _trask->setObjectSeen(_sith, true);
    _trask->setObjectSeen(_farSith, true);

    idle(2.0f);
    EXPECT_TRUE(_trask->actions().empty()) << "no fight to join";

    sithAttacksPlayer();
    idle(1.25f);
    EXPECT_EQ(_sith, traskTarget());
}

TEST_F(CoopCompanionCombatTest, the_leader_does_not_idle_engage) {
    traskJoins();
    auto leader = _game->party().getLeader();
    ASSERT_TRUE(leader);
    ASSERT_NE(_trask, leader);
    leader->setObjectSeen(_sith, true);
    sithAttacksPlayer();
    idle(2.0f);
    EXPECT_TRUE(leader->actions().empty());
}

TEST_F(CoopSessionTest, a_client_pauses_for_everyone_and_the_host_sees_who) {
    _client.send(hello());
    exchange();
    net::Command pause;
    pause.seq = 1;
    pause.type = net::CommandType::Pause;
    pause.param = 1;
    _client.send(pause);
    exchange();
    _client.inbox.clear();
    tick();
    EXPECT_TRUE(_game->isPaused());
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_FALSE(snapshots.empty());
    EXPECT_TRUE(snapshots.back().world.paused);
    EXPECT_EQ("Guest", snapshots.back().world.pausedBy);

    pause.seq = 2;
    pause.param = 0;
    _client.send(pause);
    exchange();
    tick();
    EXPECT_FALSE(_game->isPaused()) << "any player resumes";
}

// N3: unreliable snapshots, deltas against the last acknowledged one

TEST_F(CoopSessionTest, snapshots_are_deltas_against_the_last_acknowledged_one) {
    _client.autoAck = false;
    _client.send(hello());
    exchange();
    _client.inbox.clear();
    tick();
    tick();
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(2u, snapshots.size());
    // Nothing acknowledged yet: each one stands alone.
    EXPECT_TRUE(snapshots[0].full());
    EXPECT_TRUE(snapshots[1].full());

    // The client acknowledges the first; the host now sends against it.
    net::SnapshotAck ack {snapshots[0].generation, snapshots[0].tick};
    _client.transport->send(_client.connection, net::encode(ack), net::SendMode::Unreliable);
    _sith->setPosition(glm::vec3(12.0f, 0.0f, 0.0f));
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    EXPECT_EQ(ack.tick, snapshots[0].baseTick);
    ASSERT_EQ(1u, snapshots[0].entities.size());
    EXPECT_EQ(_sith->id(), snapshots[0].entities[0].state.id);

    // Its ack is lost: the next snapshot is still against the same base, so
    // it carries the move again (nothing is lost with a lost packet).
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    EXPECT_EQ(ack.tick, snapshots[0].baseTick);
    ASSERT_EQ(1u, snapshots[0].entities.size());
    EXPECT_EQ(12.0f, snapshots[0].entities[0].state.position.x);

    // An ack for a tick never sent counts against the client.
    net::SnapshotAck bogus {snapshots[0].generation, snapshots[0].tick + 1000};
    _client.transport->send(_client.connection, net::encode(bogus), net::SendMode::Unreliable);
    exchange();
    auto status = _session->statusJson();
    EXPECT_EQ(1, status.at("clients").as_array()[0].as_object().at("violations").to_number<int64_t>());
}

TEST_F(CoopSessionTest, events_go_reliably_beside_the_snapshot_of_their_tick) {
    _client.send(hello());
    exchange();
    tick();
    _session->onProjectileFired(*_sith, *_player, false, true);
    _client.inbox.clear();
    tick();
    auto batches = _client.take<net::EventBatch>(net::MessageType::Events);
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, batches.size());
    ASSERT_EQ(1u, snapshots.size());
    EXPECT_EQ(snapshots[0].tick, batches[0].tick);
    ASSERT_EQ(1u, batches[0].events.size());
    EXPECT_EQ(net::EventKind::Projectile, batches[0].events[0].kind);
    EXPECT_EQ(2, batches[0].events[0].flags);
}

TEST_F(CoopSessionTest, a_host_that_asks_holds_a_new_player_until_accepted) {
    traskJoins();
    _session.reset();
    auto [host, client] = net::createLoopbackPair();
    _client = RawClient();
    _client.transport = std::move(client);
    CoopConfig config;
    config.role = CoopRole::Host;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    config.askToJoin = true;
    _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
    exchange();
    _client.send(hello());
    exchange();
    net::SlotRequest request;
    _client.send(request);
    exchange();
    tick();
    auto players = _session->players();
    ASSERT_EQ(1u, players.size());
    EXPECT_TRUE(players[0].waiting);
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id())) << "no companion before the host accepts";
    ASSERT_TRUE(_session->answerJoin(players[0].clientId, true));
    tick();
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_FALSE(_session->answerJoin(players[0].clientId, true)) << "answered already";

    // A welcome carries the token a reconnect presents to skip the question.
    auto welcomes = _client.take<net::Welcome>(net::MessageType::Welcome);
    ASSERT_EQ(1u, welcomes.size());
    EXPECT_NE(0u, welcomes[0].rejoinToken);
}

TEST_F(CoopSessionTest, a_declined_player_is_told_and_let_go) {
    traskJoins();
    _session->setJoinApprover([](const CoopJoinRequest &) { return false; });
    _client.send(hello());
    exchange();
    net::SlotRequest request;
    _client.send(request);
    exchange();
    tick();
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::Declined, rejects[0].reason);
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id()));
}

// Stand-ins (design item 12, design note coop_standins)

TEST(CoopStandIn, rules_come_from_the_section_table) {
    EXPECT_EQ(CoopStandInRule::Play, coopStandInRule("end_m01ab"));
    EXPECT_EQ(CoopStandInRule::Play, coopStandInRule("lev_m40ac"));
    EXPECT_EQ(CoopStandInRule::Watch, coopStandInRule("sta_m45ac"));
    EXPECT_EQ(CoopStandInRule::Watch, coopStandInRule("UNK_M44AC"));
    EXPECT_EQ(CoopStandInRule::Watch, coopStandInRule("stunt_34"));
    EXPECT_EQ(CoopStandInRule::Play, coopStandInRule("stunt"));
    EXPECT_EQ(CoopStandInRule::Watch, coopStandInRule("tar_m02ae"));
}

TEST(CoopStandIn, the_template_field_round_trips_and_refuses_junk) {
    auto resref = coopStandInResRef(91, 1, 7);
    int appearance = 0;
    int gender = 0;
    uint32_t source = 0;
    ASSERT_TRUE(parseCoopStandInResRef(resref, appearance, gender, source));
    EXPECT_EQ(91, appearance);
    EXPECT_EQ(1, gender);
    EXPECT_EQ(7u, source);
    for (const char *junk : {"@standin:", "@standin:1:2", "@standin:1:2:3:4", "@standin:-1:0:3", "@standin:1:9:3",
                             "@standin:1:0:0", "@standin:x:0:3", "@appearance:4", "p_carth"}) {
        EXPECT_FALSE(parseCoopStandInResRef(junk, appearance, gender, source)) << junk;
    }
}

TEST(CoopStandIn, places_behind_and_beside_never_ahead) {
    for (int index = 0; index < 4; ++index) {
        auto offsets = coopStandInOffsets(0.0f, index); // facing +x
        ASSERT_FALSE(offsets.empty());
        EXPECT_LT(offsets.front().x, 0.0f) << "the first choice is behind";
        for (const auto &offset : offsets) {
            EXPECT_LE(offset.x, 0.01f);
            EXPECT_GT(glm::length(offset), 1.0f);
        }
    }
    // Two players stand on opposite sides.
    EXPECT_LT(coopStandInOffsets(0.0f, 0).front().y * coopStandInOffsets(0.0f, 1).front().y, 0.0f);
}

class CoopStandInSessionTest : public CoopSessionTest {
protected:
    void join() {
        _client.send(hello());
        exchange();
        net::SlotRequest request;
        _client.send(request);
        exchange();
    }

    uint32_t standInId() const {
        auto status = _session->statusJson();
        const auto &clients = status.at("clients").as_array();
        if (clients.empty() || clients[0].as_object().at("standIn").is_null()) {
            return 0;
        }
        return static_cast<uint32_t>(clients[0].as_object().at("standIn").as_object().at("id").to_number<int64_t>());
    }

    std::vector<net::SlotAssign> slots() { return _client.take<net::SlotAssign>(net::MessageType::SlotAssign); }
};

TEST_F(CoopStandInSessionTest, a_player_with_no_companion_plays_a_stand_in_outside_the_party) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    _client.inbox.clear();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id) << boost::json::serialize(_session->statusJson());
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    EXPECT_FALSE(_game->party().isMember(*standIn)) << "party counts still see the solo character";
    EXPECT_EQ(1, _game->party().getSize());
    EXPECT_TRUE(standIn->isPuppet());
    EXPECT_TRUE(_session->isStandIn(id));
    EXPECT_TRUE(_session->isPlayerControlled(id));
    EXPECT_EQ(30, standIn->maxHitPoints());
    EXPECT_LT(glm::distance(standIn->position(), _player->position()), 3.0f);
    EXPECT_GT(glm::distance(standIn->position(), _player->position()), 0.5f);
    auto sent = slots();
    ASSERT_EQ(1u, sent.size());
    EXPECT_EQ(net::SlotStatus::StandIn, sent[0].status);
    EXPECT_EQ(id, sent[0].creatureId);

    // Its player drives it with ordinary, validated commands.
    net::Command move;
    move.seq = 1;
    move.type = net::CommandType::MoveTo;
    move.actor = id;
    move.point = {-3.0f, 2.0f, 0.0f};
    _client.send(move);
    _session->pollNetwork();
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_EQ(1u, results.size());
    EXPECT_TRUE(results[0].accepted);
    ASSERT_FALSE(standIn->actions().empty());
    EXPECT_EQ(ActionType::MoveToLocation, standIn->actions().front()->type());
}

TEST_F(CoopStandInSessionTest, a_stand_in_leaves_for_conversations_and_comes_back_with_its_wounds) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t first = standInId();
    ASSERT_NE(0u, first);
    _game->getObjectById<Creature>(first)->setCurrentHitPoints(12);

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    _client.inbox.clear();
    tick();
    EXPECT_EQ(0u, standInId());
    EXPECT_FALSE(_game->getObjectById(first)) << "gone from the world, not hidden";
    auto sent = slots();
    ASSERT_EQ(1u, sent.size());
    EXPECT_NE(net::SlotStatus::StandIn, sent[0].status);
    EXPECT_EQ("conversation", std::string(_session->statusJson().at("standIns").as_object().at("pause").as_string()));

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    uint32_t second = standInId();
    ASSERT_NE(0u, second);
    auto standIn = _game->getObjectById<Creature>(second);
    ASSERT_TRUE(standIn);
    EXPECT_EQ(12, standIn->currentHitPoints());
    EXPECT_LT(glm::distance(standIn->position(), _player->position()), 3.0f);
}

TEST_F(CoopStandInSessionTest, a_free_companion_replaces_the_stand_in) {
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    traskJoins();
    _client.inbox.clear();
    tick();
    EXPECT_EQ(0u, standInId());
    EXPECT_FALSE(_game->getObjectById(id));
    auto sent = slots();
    ASSERT_EQ(1u, sent.size());
    EXPECT_EQ(net::SlotStatus::Assigned, sent[0].status);
    EXPECT_EQ(_trask->id(), sent[0].creatureId);
}

TEST_F(CoopStandInSessionTest, solo_mode_and_a_dropped_player_leave_no_stand_in) {
    _game->party().setSoloMode(true);
    join();
    tick();
    EXPECT_EQ(0u, standInId());
    EXPECT_EQ("solo_mode", std::string(_session->statusJson().at("standIns").as_object().at("pause").as_string()));
    _game->party().setSoloMode(false);
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    _client.transport->close(_client.connection, "gone");
    exchange();
    tick();
    EXPECT_FALSE(_game->getObjectById(id));
    EXPECT_FALSE(_session->isStandIn(id));
}

TEST_F(CoopStandInSessionTest, module_snapshots_see_no_stand_in_and_base_enemy_hit_points) {
    _sith->setMaxHitPoints(20);
    _sith->setCurrentHitPoints(20);
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    int scaledMax = _sith->maxHitPoints();
    _sith->setCurrentHitPoints(scaledMax - 3);

    _session->beginModuleSnapshot();
    EXPECT_FALSE(_game->getObjectById(id)) << "a save never holds a stand-in";
    EXPECT_EQ(20, _sith->maxHitPoints());
    if (scaledMax != 20) {
        EXPECT_LT(_sith->currentHitPoints(), 20);
    }
    _session->endModuleSnapshot();
    EXPECT_EQ(scaledMax, _sith->maxHitPoints());
    EXPECT_EQ(scaledMax - 3, _sith->currentHitPoints());
    tick();
    EXPECT_NE(0u, standInId()) << "back beside the solo character after the save";
}

TEST_F(CoopStandInSessionTest, enemy_damage_to_a_stand_in_is_scaled_like_the_partys) {
    _sith->setMaxHitPoints(20);
    _sith->setCurrentHitPoints(20);
    join();
    tick();
    auto standIn = _game->getObjectById<Creature>(standInId());
    ASSERT_TRUE(standIn);
    int toStandIn = _session->scaleEnemyDamage(_sith.get(), *standIn, 20);
    int toPlayer = _session->scaleEnemyDamage(_sith.get(), *_player, 20);
    EXPECT_EQ(toPlayer, toStandIn);
}

// Co-op completeness: shared inventory, character sheet, barks, level-up, equipment

TEST(CoopCompleteness, inventory_sheet_and_bark_round_trip) {
    net::SharedInventory inventory;
    inventory.owner = 1;
    inventory.credits = 250;
    inventory.items.push_back({42, "g_i_medeqpmnt01", 3, net::kInventoryUsable});
    inventory.items.push_back({43, "g_a_clothes01", 1, 0});
    net::SharedInventory inventoryBack;
    ASSERT_TRUE(net::decode(net::encode(inventory), inventoryBack));
    ASSERT_EQ(2u, inventoryBack.items.size());
    EXPECT_EQ(42u, inventoryBack.items[0].id);
    EXPECT_EQ(3, inventoryBack.items[0].stack);
    EXPECT_EQ(net::kInventoryUsable, inventoryBack.items[0].flags);
    EXPECT_EQ(250, inventoryBack.credits);

    net::CharacterSheet sheet;
    sheet.creature = 342;
    sheet.xp = 1500;
    sheet.levelUp = true;
    sheet.classes.push_back({2, 3});
    sheet.abilities[0] = 14;
    sheet.skills[6] = 4;
    sheet.feats = {1, 2, 3};
    sheet.spells = {7};
    sheet.baseHp = 30;
    sheet.equipment.push_back({4, 99, "g_w_blstrpstl001"});
    net::CharacterSheet sheetBack;
    ASSERT_TRUE(net::decode(net::encode(sheet), sheetBack));
    EXPECT_EQ(342u, sheetBack.creature);
    EXPECT_TRUE(sheetBack.levelUp);
    ASSERT_EQ(1u, sheetBack.classes.size());
    EXPECT_EQ(3, sheetBack.classes[0].level);
    EXPECT_EQ(14, sheetBack.abilities[0]);
    EXPECT_EQ(4, sheetBack.skills[6]);
    EXPECT_EQ(3u, sheetBack.feats.size());
    ASSERT_EQ(1u, sheetBack.equipment.size());
    EXPECT_EQ("g_w_blstrpstl001", sheetBack.equipment[0].resref);

    net::Bark bark {"Die, Republic dog!", 4.0f};
    net::Bark barkBack;
    ASSERT_TRUE(net::decode(net::encode(bark), barkBack));
    EXPECT_EQ(bark.text, barkBack.text);
    EXPECT_FLOAT_EQ(4.0f, barkBack.duration);
    EXPECT_EQ(net::MessageType::Bark, *net::peekType(net::encode(bark)));
    EXPECT_TRUE(net::isHostMessage(net::MessageType::SharedInventory));
    EXPECT_FALSE(net::isClientMessage(net::MessageType::CharacterSheet));
}

TEST(CoopCompleteness, decoders_refuse_what_a_host_never_sends) {
    net::SharedInventory inventory;
    inventory.items.push_back({42, "bad resref!", 1, 0});
    net::SharedInventory inventoryBack;
    EXPECT_FALSE(net::decode(net::encode(inventory), inventoryBack));
    inventory.items[0] = {42, "g_i_medeqpmnt01", 1, 0x80};
    EXPECT_FALSE(net::decode(net::encode(inventory), inventoryBack)) << "unknown flags";
    inventory.items[0] = {0, "g_i_medeqpmnt01", 1, 0};
    EXPECT_FALSE(net::decode(net::encode(inventory), inventoryBack)) << "no id";

    net::CharacterSheet sheet;
    sheet.creature = 1;
    sheet.xp = -5;
    net::CharacterSheet sheetBack;
    EXPECT_FALSE(net::decode(net::encode(sheet), sheetBack));
    sheet.xp = 0;
    sheet.classes = {{1, 1}, {2, 1}, {3, 1}, {4, 1}};
    EXPECT_FALSE(net::decode(net::encode(sheet), sheetBack)) << "four classes";

    net::Bark bark {"ok", 1.0e9f};
    net::Bark barkBack;
    EXPECT_FALSE(net::decode(net::encode(bark), barkBack));
    auto truncated = net::encode(net::Bark {"line", 2.0f});
    truncated.pop_back();
    EXPECT_FALSE(net::decode(truncated, barkBack));
}

TEST(CoopCompleteness, level_up_choices_round_trip_and_refuse_junk) {
    CoopLevelUpChoice choice;
    choice.clazz = 2;
    choice.ability = 3;
    choice.skills = {{0, 1}, {6, 2}};
    choice.feats = {12, 40};
    choice.powers = {5};
    auto text = encodeCoopLevelUp(choice);
    CoopLevelUpChoice back;
    ASSERT_TRUE(decodeCoopLevelUp(text, back)) << text;
    EXPECT_TRUE(back == choice);
    CoopLevelUpChoice none;
    none.clazz = 1;
    ASSERT_TRUE(decodeCoopLevelUp(encodeCoopLevelUp(none), back));
    EXPECT_TRUE(back == none);
    for (const char *junk : {"", "c2", "c2|a0|s|f|", "c2|a7|s|f|p", "c2|a0|s9:1|f|p", "c2|a0|s1:0|f|p", "c2|a0|s1:1,1:1|f|p",
                             "c2|a0|s|fx|p", "c2|a0|s|f1,2,3,4,5,6,7,8,9,10,11,12,13|p", "c-1|a0|s|f|p"}) {
        EXPECT_FALSE(decodeCoopLevelUp(junk, back)) << junk;
    }
}

TEST_F(CoopStandInSessionTest, the_client_s_companion_orders_are_checked_on_the_host) {
    traskJoins();
    join();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    uint32_t seq = 1;
    auto send = [&](net::CommandType type, int32_t param, const std::string &item) {
        net::Command command;
        command.seq = seq++;
        command.type = type;
        command.actor = _trask->id();
        command.param = param;
        command.item = item;
        _client.send(command);
    };
    send(net::CommandType::Equip, 0, "99");          // no such slot
    send(net::CommandType::Equip, 123456, "4");      // not in the shared inventory
    send(net::CommandType::Equip, 0, "4");           // nothing to clear
    send(net::CommandType::LevelUp, 0, "c1|a-1|s|f|p"); // no XP for a level
    send(net::CommandType::LevelUp, 0, "not a level");
    send(net::CommandType::UseItem, 123456, "g_i_medeqpmnt01"); // not the party's
    _session->pollNetwork();
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_EQ(6u, results.size());
    EXPECT_EQ(net::CommandReject::TargetInvalid, results[0].reason);
    EXPECT_EQ(net::CommandReject::ItemUnavailable, results[1].reason);
    EXPECT_EQ(net::CommandReject::ItemUnavailable, results[2].reason);
    EXPECT_EQ(net::CommandReject::LevelUpUnavailable, results[3].reason);
    EXPECT_EQ(net::CommandReject::LevelUpInvalid, results[4].reason);
    EXPECT_EQ(net::CommandReject::ItemUnavailable, results[5].reason);
}

TEST_F(CoopStandInSessionTest, a_stand_in_has_no_equipment_or_level_of_its_own_to_change) {
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    net::Command command;
    command.seq = 1;
    command.type = net::CommandType::LevelUp;
    command.actor = id;
    command.item = "c1|a-1|s|f|p";
    _client.send(command);
    _session->pollNetwork();
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_EQ(1u, results.size());
    EXPECT_EQ(net::CommandReject::NotSupported, results[0].reason);
}

TEST_F(CoopStandInSessionTest, the_shared_inventory_and_the_companion_sheet_reach_the_client) {
    traskJoins();
    _player->addItem("g_i_medeqpmnt01", 2);
    join();
    _client.inbox.clear();
    tick();
    auto inventories = _client.take<net::SharedInventory>(net::MessageType::SharedInventory);
    auto sheets = _client.take<net::CharacterSheet>(net::MessageType::CharacterSheet);
    // The fixture's items have no template file; what matters is that the
    // host tells the client its companion's sheet, and says it only again
    // when something changes.
    ASSERT_EQ(1u, sheets.size());
    EXPECT_EQ(_trask->id(), sheets[0].creature);
    EXPECT_FALSE(sheets[0].levelUp);
    EXPECT_LE(inventories.size(), 1u);
    _client.inbox.clear();
    tick();
    EXPECT_TRUE(_client.take<net::CharacterSheet>(net::MessageType::CharacterSheet).empty());
    EXPECT_TRUE(_client.take<net::SharedInventory>(net::MessageType::SharedInventory).empty());
}

TEST_F(CoopStandInSessionTest, host_barks_go_to_every_client) {
    join();
    _client.inbox.clear();
    _session->onBark("Halt!", 3.0f);
    exchange();
    auto barks = _client.take<net::Bark>(net::MessageType::Bark);
    ASSERT_EQ(1u, barks.size());
    EXPECT_EQ("Halt!", barks[0].text);
}

TEST_F(CoopNetTest, a_console_script_that_cannot_be_read_counts_as_story) {
    EXPECT_TRUE(coopScriptChangesStory(*_game, "k_no_such_script"));
    EXPECT_FALSE(coopScriptChangesStory(*_game, ""));
}

// Free Mode

static net::GuestCharacter sampleGuest() {
    net::GuestCharacter guest;
    guest.name = "Alice";
    guest.appearance = 91;
    guest.gender = 1;
    guest.classes.push_back({0, 2});
    for (auto &value : guest.abilities) {
        value = 12;
    }
    guest.skills[6] = 3;
    guest.xp = 1200;
    guest.credits = 40;
    guest.baseHp = 20;
    guest.baseFp = 0;
    guest.items.push_back({"g_a_clothes01", 1, 1});
    guest.items.push_back({"g_i_medeqpmnt01", 3, net::kGuestInventory});
    return guest;
}

TEST(CoopFree, guest_characters_and_store_views_round_trip_and_refuse_junk) {
    auto guest = sampleGuest();
    net::GuestCharacter back;
    ASSERT_TRUE(net::decode(net::encode(guest), back));
    EXPECT_EQ("Alice", back.name);
    EXPECT_EQ(2, back.classes[0].level);
    EXPECT_EQ(1200, back.xp);
    ASSERT_EQ(2u, back.items.size());
    EXPECT_EQ(net::kGuestInventory, back.items[1].slot);
    EXPECT_TRUE(net::isClientMessage(net::MessageType::GuestCharacter));
    EXPECT_TRUE(net::isHostMessage(net::MessageType::GuestCharacter));

    auto bad = guest;
    bad.classes.clear();
    EXPECT_FALSE(net::decode(net::encode(bad), back)) << "no class";
    bad = guest;
    bad.classes[0].level = 51;
    EXPECT_FALSE(net::decode(net::encode(bad), back)) << "over the level cap";
    bad = guest;
    bad.items[0].resref = "not a resref!";
    EXPECT_FALSE(net::decode(net::encode(bad), back));
    bad = guest;
    bad.credits = -1;
    EXPECT_FALSE(net::decode(net::encode(bad), back));

    net::StoreView view;
    view.store = 9;
    view.credits = 100;
    view.stock.push_back({11, "g_w_blstrpstl001", 1, 60});
    view.sellable.push_back({12, "g_i_medeqpmnt01", 2, 5});
    net::StoreView viewBack;
    ASSERT_TRUE(net::decode(net::encode(view), viewBack));
    EXPECT_EQ(60, viewBack.stock[0].price);
    EXPECT_EQ(12u, viewBack.sellable[0].id);
    view.stock[0].price = -1;
    EXPECT_FALSE(net::decode(net::encode(view), viewBack));
}

class CoopFreeSessionTest : public CoopNetTest {
protected:
    void SetUp() override {
        CoopNetTest::SetUp();
        auto [host, client] = net::createLoopbackPair();
        _client.transport = std::move(client);
        CoopConfig config;
        config.role = CoopRole::Host;
        config.mode = "free";
        config.maxClients = 7;
        config.build = "build-A";
        config.k1 = {true, 1, 2, 3};
        // Any class the guest names is a plain class of that type.
        _classes = std::make_unique<Classes>(_strings, _twoDas);
        ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([this](ClassType type) {
            auto &made = _made[type];
            if (!made) {
                made = std::make_shared<CreatureClass>(type, *_classes, _strings, _twoDas);
            }
            return made;
        }));
        _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
        _game->setCoopSession(_session);
        _session->pollNetwork();
        _client.poll();
    }

    void TearDown() override {
        _game->setCoopSession(nullptr);
    }

    void exchange() {
        _session->pollNetwork();
        _client.poll();
    }

    void tick() {
        _session->pollNetwork();
        TestGameModule::advanceTick(*_game);
        _session->beforeTick();
        _session->afterTick();
        _client.poll();
    }

    void join(const net::GuestCharacter &guest) {
        net::Hello hello;
        hello.build = "build-A";
        hello.mode = "free";
        hello.playerName = "Alice";
        hello.k1 = {true, 1, 2, 3};
        _client.send(hello);
        exchange();
        net::SlotRequest request;
        _client.send(request);
        _client.send(guest);
        exchange();
    }

    uint32_t guestId() const {
        auto status = _session->statusJson();
        const auto &clients = status.at("clients").as_array();
        if (clients.empty() || !clients[0].as_object().contains("guest")) {
            return 0;
        }
        return static_cast<uint32_t>(clients[0].as_object().at("guest").as_object().at("id").to_number<int64_t>());
    }

    NiceMock<MockStrings> _strings;
    NiceMock<MockTwoDAs> _twoDas;
    std::unique_ptr<Classes> _classes;
    std::map<ClassType, std::shared_ptr<CreatureClass>> _made;
    RawClient _client;
    std::shared_ptr<CoopSession> _session;
};

TEST_F(CoopFreeSessionTest, a_guest_plays_its_own_character_outside_the_party_and_never_a_companion) {
    traskJoins();
    join(sampleGuest());
    _client.inbox.clear();
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id) << boost::json::serialize(_session->statusJson());
    auto guest = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(guest);
    EXPECT_EQ("Alice", guest->name());
    EXPECT_TRUE(guest->isPuppet());
    EXPECT_FALSE(_game->party().isMember(*guest));
    EXPECT_EQ(2, _game->party().getSize()) << "the host keeps its AI party";
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id())) << "guests never take companions";
    EXPECT_TRUE(_session->isPlayerControlled(id));
    EXPECT_EQ(1200, static_cast<int>(guest->xp()));
    EXPECT_EQ(2, guest->attributes().getAggregateLevel());
    auto slots = _client.take<net::SlotAssign>(net::MessageType::SlotAssign);
    ASSERT_FALSE(slots.empty());
    EXPECT_EQ(net::SlotStatus::Guest, slots.back().status);
    EXPECT_EQ(id, slots.back().creatureId);
}

TEST_F(CoopFreeSessionTest, xp_the_host_gets_goes_to_each_guest) {
    join(sampleGuest());
    tick();
    auto guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    _game->party().awardXP(300, XPSource::Plot);
    EXPECT_EQ(1500, static_cast<int>(guest->xp()));
}

TEST_F(CoopFreeSessionTest, an_impossible_character_is_refused) {
    auto guest = sampleGuest();
    guest.skills[0] = 30; // far above level + 3
    join(guest);
    tick();
    exchange();
    EXPECT_EQ(0u, guestId());
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::Malformed, rejects[0].reason);
}

TEST_F(CoopFreeSessionTest, a_guest_leaves_for_conversations_and_comes_back_with_its_state) {
    join(sampleGuest());
    tick();
    uint32_t first = guestId();
    ASSERT_NE(0u, first);
    _game->getObjectById<Creature>(first)->setXP(5000);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    _client.inbox.clear();
    tick();
    EXPECT_EQ(0u, guestId());
    EXPECT_FALSE(_game->getObjectById(first));
    auto saved = _client.take<net::GuestCharacter>(net::MessageType::GuestCharacter);
    ASSERT_FALSE(saved.empty()) << "the guest's machine keeps the character";
    EXPECT_EQ(5000, saved.back().xp);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    auto back = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(back);
    EXPECT_EQ(5000, static_cast<int>(back->xp()));
}

TEST_F(CoopFreeSessionTest, a_host_that_asks_holds_a_guest_until_accepted) {
    _session.reset();
    _game->setCoopSession(nullptr);
    auto [host, client] = net::createLoopbackPair();
    _client = RawClient();
    _client.transport = std::move(client);
    CoopConfig config;
    config.role = CoopRole::Host;
    config.mode = "free";
    config.maxClients = 7;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    config.askToJoin = true;
    _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
    _game->setCoopSession(_session);
    exchange();
    join(sampleGuest());
    tick();
    EXPECT_EQ(0u, guestId()) << "no character in the world before the host accepts";
    auto players = _session->players();
    ASSERT_EQ(1u, players.size());
    EXPECT_TRUE(players[0].waiting);
    ASSERT_TRUE(_session->answerJoin(players[0].clientId, true));
    tick();
    EXPECT_NE(0u, guestId());
}

namespace {

// A guest's level-up command, answered by the host's class rules.
net::CommandReject guestLevelUpAnswer(CoopSession &session, RawClient &client, uint32_t actor, const std::string &item,
                                      const std::function<void()> &tick) {
    static uint32_t seq = 0;
    if (item.empty()) {
        seq = 0; // a new test starts a new session
        return net::CommandReject::None;
    }
    net::Command command;
    command.seq = ++seq;
    command.type = net::CommandType::LevelUp;
    command.actor = actor;
    command.item = item;
    client.send(command);
    session.pollNetwork();
    client.inbox.clear();
    tick();
    auto results = client.take<net::CommandResult>(net::MessageType::CommandResult);
    return results.empty() ? net::CommandReject::None : results.back().reason;
}

} // namespace

TEST_F(CoopFreeSessionTest, a_guest_s_new_class_choice_is_checked_by_the_host) {
    auto characterWith = [](std::vector<std::pair<int16_t, int16_t>> classes) {
        auto guest = sampleGuest();
        guest.classes.clear();
        for (auto [type, level] : classes) {
            guest.classes.push_back({type, level});
        }
        return guest;
    };
    join(characterWith({{0, 2}, {1, 1}, {3, 1}}));
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id);
    auto guest = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(guest);
    guest->setXP(1000000);
    guestLevelUpAnswer(*_session, _client, id, "", {});
    auto answer = [&](const std::string &item) {
        return guestLevelUpAnswer(*_session, _client, id, item, [this]() { tick(); });
    };
    ASSERT_TRUE(guest->isLevelUpPending());
    // Three classes already: no slot for a fourth (a base class or a prestige one).
    EXPECT_EQ(net::CommandReject::ClassSlotsFull, answer("c2|a-1|s|f|p"));
    EXPECT_EQ(net::CommandReject::ClassLocked, answer("c12|a-1|s|f|p")) << "locked first: the K1 game here has no merge";
    // Malformed class ids are plain invalid level-ups.
    for (const char *junk : {"c99|a-1|s|f|p", "c17|a-1|s|f|p", "c255|a-1|s|f|p", "c6|a-1|s|f|p", "c9|a-1|s|f|p"}) {
        EXPECT_EQ(net::CommandReject::LevelUpInvalid, answer(junk)) << junk;
    }
    EXPECT_EQ(net::CommandReject::LevelUpInvalid, answer("c-1|a-1|s|f|p"));
    EXPECT_EQ(3u, guest->attributes().classLevels().size()) << "refusals change nothing";
}

TEST_F(CoopFreeSessionTest, a_locked_prestige_class_is_refused_as_locked_and_a_second_base_class_is_taken) {
    join(sampleGuest()); // one class: Soldier
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id);
    auto guest = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(guest);
    guest->setXP(1000000);
    guestLevelUpAnswer(*_session, _client, id, "", {});
    auto answer = [&](const std::string &item) {
        return guestLevelUpAnswer(*_session, _client, id, item, [this]() { tick(); });
    };
    EXPECT_EQ(net::CommandReject::ClassLocked, answer("c12|a-1|s|f|p")) << "no Jedi class, no reveal";
    EXPECT_EQ(1u, guest->attributes().classLevels().size());
    EXPECT_EQ(net::CommandReject::None, answer("c1|a-1|s|f|p")) << "a second base class";
    ASSERT_EQ(2u, guest->attributes().classLevels().size());
    EXPECT_EQ(ClassType::Scout, guest->attributes().getEffectiveClass());
    EXPECT_EQ(1, guest->attributes().getClassLevel(ClassType::Scout));
}

TEST_F(CoopFreeSessionTest, a_declined_guest_is_told_and_let_go) {
    _session->setJoinApprover([](const CoopJoinRequest &) { return false; });
    join(sampleGuest());
    tick();
    exchange();
    EXPECT_EQ(0u, guestId());
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::Declined, rejects[0].reason);
}

TEST_F(CoopStandInSessionTest, a_player_s_creature_counts_as_the_pc_for_door_and_trigger_scripts) {
    traskJoins();
    join();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_EQ(_player->id(), _session->scriptActor(_trask->id()));
    EXPECT_EQ(_sith->id(), _session->scriptActor(_sith->id())) << "anyone else is themselves";
    EXPECT_EQ(_player->id(), _session->scriptActor(_player->id()));

    // Every event that hands a script a clicker or user goes through the same
    // mapping: door and trigger OnClick, placeable OnUsed and OnOpen. The
    // scripts do not exist here, so the runs are no-ops that must not throw.
    auto door = _game->newDoor();
    door->setOnClick("k_missing_click");
    EXPECT_NO_THROW(door->runClickScript(_trask->id()));
    auto trigger = _game->newTrigger();
    trigger->setOnClick("k_missing_click");
    EXPECT_NO_THROW(trigger->runClickScript(_trask->id()));
    auto box = _game->newPlaceable();
    box->setOnUsed("k_missing_used");
    EXPECT_NO_THROW(box->runOnUsed(_trask));
    EXPECT_NO_THROW(box->onOpen(_trask->id()));
    EXPECT_EQ(_player->id(), _session->scriptActor(_trask->id())) << "clicker, user and opener all read as the PC";
    EXPECT_NE(_trask->id(), _session->scriptActor(_trask->id())) << "traps pass the real creature and skip the mapping";
}

TEST(CoopLootLedgerTest, what_a_guest_took_survives_leaving_and_coming_back) {
    CoopLootLedger ledger;
    std::vector<CoopLootLedger::Entry> baseline {{"g_w_blstrpstl01", "", 1, false, false}, {"geno_datapad", "", 1, true, false}};
    auto first = CoopLootLedger::key("end_locker01", glm::vec3(10.2f, -3.1f, 0.0f));
    ledger.capture("end_m01aa", first, baseline);
    ledger.markTaken("end_m01aa", first, "Alice", 0);

    // The revisit: new object ids, the same tag and place.
    auto again = CoopLootLedger::key("end_locker01", glm::vec3(10.2f, -3.1f, 0.0f));
    EXPECT_EQ(first, again);
    EXPECT_TRUE(ledger.isTaken("end_m01aa", again, "Alice", 0));
    EXPECT_FALSE(ledger.isTaken("end_m01aa", again, "Alice", 1));
    EXPECT_FALSE(ledger.isTaken("end_m01aa", again, "Bob", 0)) << "each guest has its own copy";
    EXPECT_FALSE(ledger.isTaken("tar_m02aa", again, "Alice", 0)) << "per module";
    EXPECT_NE(first, CoopLootLedger::key("end_locker01", glm::vec3(40.0f, 7.0f, 0.0f))) << "same tag elsewhere is another container";

    // Opening it again keeps the first copy, even if the real contents changed.
    ledger.capture("end_m01aa", again, {});
    ASSERT_NE(nullptr, ledger.find("end_m01aa", again));
    EXPECT_EQ(2u, ledger.find("end_m01aa", again)->baseline.size());
    ledger.clear();
    EXPECT_EQ(nullptr, ledger.find("end_m01aa", again));
}

namespace {

Dialog::EntryReplyLink dialogLink(int index, const std::string &active = std::string()) {
    Dialog::EntryReplyLink link;
    link.index = index;
    link.active = active;
    return link;
}

} // namespace

TEST(CoopDialogReaches, store_node_only_through_an_active_condition) {
    // Entry 0 greets; reply 0 (gated by c_false) leads to entry 1, which opens
    // the store; reply 1 (gated by c_true) leads to entry 2, which also does.
    Dialog dialog;
    dialog.startEntries.push_back(dialogLink(0));
    dialog.entries.resize(3);
    dialog.replies.resize(2);
    dialog.entries[0].replies = {dialogLink(0, "c_false"), dialogLink(1, "c_true")};
    dialog.replies[0].entries = {dialogLink(1)};
    dialog.replies[1].entries = {dialogLink(2)};
    dialog.entries[1].script = "a_openstore_false";
    dialog.entries[2].script = "a_openstore_true";

    std::vector<std::string> evaluated;
    auto condition = [&](const std::string &script, const Dialog::EntryReplyLink::ConditionParams &) {
        evaluated.push_back(script);
        return script == "c_true";
    };
    auto opens = [](const std::string &script) {
        return [script](const Dialog::EntryReply &node) { return node.script == script; };
    };

    // The true-gated path reaches its store node; the false-gated one does not.
    EXPECT_TRUE(coopDialogReaches(dialog, condition, opens("a_openstore_true")));
    EXPECT_FALSE(coopDialogReaches(dialog, condition, opens("a_openstore_false")));
    EXPECT_THAT(evaluated, Contains(std::string("c_false")));

    // Not flags and Logic as Conversation::isLinkActive: NOT c_false is active.
    dialog.entries[0].replies[0].notActive = true;
    EXPECT_TRUE(coopDialogReaches(dialog, condition, opens("a_openstore_false")));
    dialog.entries[0].replies[0].notActive = false;
    dialog.entries[0].replies[0].active2 = "c_true";
    dialog.entries[0].replies[0].logic = 1; // OR
    EXPECT_TRUE(coopDialogReaches(dialog, condition, opens("a_openstore_false")));
    dialog.entries[0].replies[0].logic = 0; // AND
    EXPECT_FALSE(coopDialogReaches(dialog, condition, opens("a_openstore_false")));
}

TEST(CoopDialogReaches, only_the_first_active_entry_is_taken) {
    // Start entries: a refusal (active) before the store entry: the
    // conversation shows the refusal, so the store is out of reach. A cycle
    // back to the refusal must not loop.
    Dialog dialog;
    dialog.startEntries = {dialogLink(0, "c_true"), dialogLink(1)};
    dialog.entries.resize(2);
    dialog.replies.resize(1);
    dialog.entries[0].replies = {dialogLink(0)};
    dialog.replies[0].entries = {dialogLink(0)};
    dialog.entries[1].script = "a_openstore";
    auto condition = [](const std::string &script, const Dialog::EntryReplyLink::ConditionParams &) { return script == "c_true"; };
    auto opens = [](const Dialog::EntryReply &node) { return node.script == "a_openstore"; };
    EXPECT_FALSE(coopDialogReaches(dialog, condition, opens));

    // Once the refusal's condition is false, the store entry is the first active one.
    dialog.startEntries[0].active = "c_false";
    EXPECT_TRUE(coopDialogReaches(dialog, condition, opens));

    // A store opened by a reply's own action script (Script2) counts too.
    dialog.entries[1].script.clear();
    dialog.startEntries = {dialogLink(0)};
    EXPECT_FALSE(coopDialogReaches(dialog, condition, opens));
    dialog.replies[0].script2 = "a_openstore";
    auto opensEither = [](const Dialog::EntryReply &node) { return node.script == "a_openstore" || node.script2 == "a_openstore"; };
    EXPECT_TRUE(coopDialogReaches(dialog, condition, opensEither));
}

// Hostile peers
//
// A host transport the test drives by hand: any number of connections, and the
// test decides what each delivers and when, so it can duplicate, reorder, delay
// and drop packets, replay old ones, and open as many connections as it likes.
// No sockets are involved.

TEST_F(CoopNetTest, same_tag_containers_in_one_cell_get_their_own_loot_records) {
    auto place = [&](const glm::vec3 &position) {
        auto locker = _game->newPlaceable();
        locker->setTag("end_locker01");
        locker->setPosition(position);
        _area->add(locker);
        return locker;
    };
    auto first = place(glm::vec3(10.2f, -3.1f, 0.0f));
    auto second = place(glm::vec3(10.3f, -3.3f, 0.0f)); // the same half-metre cell
    auto elsewhere = place(glm::vec3(40.0f, 7.0f, 0.0f));
    ASSERT_EQ(CoopLootLedger::key("end_locker01", first->position()), CoopLootLedger::key("end_locker01", second->position()))
        << "the cell alone aliases them";
    auto a = coopLootKey(*_area, *first);
    auto b = coopLootKey(*_area, *second);
    EXPECT_NE(a, b);
    EXPECT_EQ(CoopLootLedger::key("end_locker01", first->position()), a) << "the first keeps the key older saves wrote";
    EXPECT_EQ(CoopLootLedger::key("end_locker01", elsewhere->position()), coopLootKey(*_area, *elsewhere));
    EXPECT_EQ(b, coopLootKey(*_area, *second)) << "stable";

    CoopLootLedger ledger;
    ledger.capture("end_m01aa", a, {{"g_w_blstrpstl01", "", 1, false, false}});
    ledger.capture("end_m01aa", b, {{"g_i_medeqpmnt01", "", 1, false, false}});
    ledger.markTaken("end_m01aa", a, "Alice", 0);
    EXPECT_FALSE(ledger.isTaken("end_m01aa", b, "Alice", 0)) << "taking from one leaves the other";
    EXPECT_EQ("g_i_medeqpmnt01", ledger.find("end_m01aa", b)->baseline[0].resref);
}

namespace {

class FakeHostTransport : public net::ITransport {
public:
    struct Sent {
        net::ConnectionId connection;
        std::vector<uint8_t> data;
    };

    bool isHost() const override { return true; }

    void poll(std::vector<net::TransportEvent> &events) override {
        for (auto &event : _incoming) {
            events.push_back(std::move(event));
        }
        _incoming.clear();
    }

    bool send(net::ConnectionId connection, const std::vector<uint8_t> &data, net::SendMode) override {
        if (!_open.count(connection)) {
            return false;
        }
        _sent.push_back({connection, data});
        return true;
    }

    void close(net::ConnectionId connection, const std::string &reason) override {
        if (_open.erase(connection)) {
            _closed.insert(connection);
            queue(net::TransportEventType::Disconnected, connection, {}, reason);
        }
    }

    net::TransportStats stats() const override { return {}; }
    std::string describe() const override { return "fake host"; }

    // Test controls

    void connect(net::ConnectionId connection) {
        _open.insert(connection);
        _closed.erase(connection);
        queue(net::TransportEventType::Connected, connection, {}, "");
    }

    void deliver(net::ConnectionId connection, std::vector<uint8_t> data) {
        queue(net::TransportEventType::Message, connection, std::move(data), "");
    }

    /** A message the transport refused for its size before copying it (R3). */
    void deliverRefused(net::ConnectionId connection, size_t size) {
        queue(net::TransportEventType::Message, connection, {}, "");
        _incoming.back().oversizedBytes = size;
    }

    void drop(net::ConnectionId connection, const std::string &reason = "dropped") {
        if (_open.erase(connection)) {
            queue(net::TransportEventType::Disconnected, connection, {}, reason);
        }
    }

    bool wasClosedByHost(net::ConnectionId connection) const { return _closed.count(connection) > 0; }

    template <class T>
    std::vector<T> received(net::ConnectionId connection, net::MessageType type) const {
        std::vector<T> result;
        for (const auto &sent : _sent) {
            if (sent.connection == connection && !sent.data.empty() && sent.data[0] == static_cast<uint8_t>(type)) {
                T message;
                if (net::decode(sent.data, message)) {
                    result.push_back(message);
                }
            }
        }
        return result;
    }

    void forgetSent() { _sent.clear(); }

private:
    void queue(net::TransportEventType type, net::ConnectionId connection, std::vector<uint8_t> data, const std::string &reason) {
        net::TransportEvent event;
        event.type = type;
        event.connection = connection;
        event.data = std::move(data);
        event.reason = reason;
        _incoming.push_back(std::move(event));
    }

    std::set<net::ConnectionId> _open;
    std::set<net::ConnectionId> _closed;
    std::vector<net::TransportEvent> _incoming;
    std::vector<Sent> _sent;
};

} // namespace

class CoopHostileTest : public CoopNetTest {
protected:
    void SetUp() override {
        CoopNetTest::SetUp();
        _mission = addCreature("end_mission", glm::vec3(3.0f, 0.0f, 0.0f), Faction::Friendly1);
        auto fake = std::make_unique<FakeHostTransport>();
        _fake = fake.get();
        CoopConfig config;
        config.role = CoopRole::Host;
        config.build = "build-A";
        config.k1 = {true, 1, 2, 3};
        config.maxClients = 8;
        _session = std::make_shared<CoopSession>(*_game, config, std::move(fake));
        _session->pollNetwork();
    }

    void bothJoinTheParty() {
        traskJoins();
        _game->party().addAvailableMember(1, _mission);
        ASSERT_TRUE(_game->party().addMember(1, _mission));
    }

    net::Hello hello(const std::string &name, uint64_t token = 0) {
        net::Hello message;
        message.build = "build-A";
        message.mode = "story";
        message.playerName = name;
        message.k1 = {true, 1, 2, 3};
        message.rejoinToken = token;
        return message;
    }

    template <class T>
    void say(net::ConnectionId connection, const T &message) {
        _fake->deliver(connection, net::encode(message));
    }

    void exchange() { _session->pollNetwork(); }

    void tick() {
        _session->pollNetwork();
        TestGameModule::advanceTick(*_game);
        _session->beforeTick();
        _session->afterTick();
    }

    /** Connect, say hello and return the token the welcome carried (0: no welcome). */
    uint64_t join(net::ConnectionId connection, const std::string &name, uint64_t token = 0) {
        _fake->connect(connection);
        say(connection, hello(name, token));
        exchange();
        auto welcomes = _fake->received<net::Welcome>(connection, net::MessageType::Welcome);
        return welcomes.empty() ? 0 : welcomes.back().rejoinToken;
    }

    std::optional<net::SlotAssign> lastSlot(net::ConnectionId connection) {
        auto slots = _fake->received<net::SlotAssign>(connection, net::MessageType::SlotAssign);
        if (slots.empty()) {
            return std::nullopt;
        }
        return slots.back();
    }

    int64_t stat(const std::string &name, const char *key) {
        auto status = clientStatus(name);
        return status.empty() ? -1 : status.at(key).to_number<int64_t>();
    }

    boost::json::object clientStatus(const std::string &name) {
        auto status = _session->statusJson();
        for (const auto &entry : status.at("clients").as_array()) {
            if (entry.as_object().at("name").as_string() == name) {
                return entry.as_object();
            }
        }
        return {};
    }

    FakeHostTransport *_fake {nullptr};
    std::shared_ptr<CoopSession> _session;
    std::shared_ptr<Creature> _mission;
};

TEST_F(CoopHostileTest, nothing_a_client_sends_before_hello_gets_it_anything) {
    traskJoins();
    _fake->connect(1);
    net::SlotRequest slot;
    say(1, slot);
    net::Command move;
    move.seq = 1;
    move.type = net::CommandType::MoveTo;
    move.actor = _trask->id();
    move.point = {4.0f, 3.0f, 0.0f};
    say(1, move);
    say(1, net::SnapshotAck {1, 1});
    exchange();
    tick();

    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_TRUE(_fake->received<net::Welcome>(1, net::MessageType::Welcome).empty());
    EXPECT_TRUE(_fake->received<net::SlotAssign>(1, net::MessageType::SlotAssign).empty());
    EXPECT_TRUE(_fake->received<net::CommandResult>(1, net::MessageType::CommandResult).empty());
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id()));
}

TEST_F(CoopHostileTest, a_slot_request_that_overtakes_the_hello_is_ignored) {
    traskJoins();
    _fake->connect(1);
    net::SlotRequest slot;
    say(1, slot);
    say(1, hello("Guest"));
    exchange();
    tick();

    ASSERT_FALSE(_fake->received<net::Welcome>(1, net::MessageType::Welcome).empty());
    EXPECT_FALSE(lastSlot(1).has_value()) << "the early request did not count";
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id()));
}

TEST_F(CoopHostileTest, a_second_hello_is_a_violation_and_changes_nothing) {
    uint64_t token = join(1, "Guest");
    ASSERT_NE(0u, token);
    say(1, hello("Guest", token));
    say(1, hello("Somebody Else"));
    exchange();

    EXPECT_EQ(1u, _fake->received<net::Welcome>(1, net::MessageType::Welcome).size());
    EXPECT_EQ(2u, stat("Guest", "violations"));
    EXPECT_EQ(1u, _session->players().size());
}

TEST_F(CoopHostileTest, a_mismatched_protocol_or_build_ends_the_join_and_later_traffic_is_ignored) {
    traskJoins();
    auto wrongProtocol = hello("Guest");
    wrongProtocol.protocol = net::kProtocolVersion + 1;
    _fake->connect(1);
    say(1, wrongProtocol);
    exchange();
    auto rejects = _fake->received<net::Reject>(1, net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::ProtocolMismatch, rejects[0].reason);
    EXPECT_TRUE(_fake->wasClosedByHost(1));
    exchange();

    // Whatever a rejected peer still manages to deliver goes nowhere.
    net::SlotRequest slot;
    say(1, slot);
    net::Command move;
    move.type = net::CommandType::MoveTo;
    move.actor = _trask->id();
    say(1, move);
    exchange();
    tick();
    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_TRUE(_fake->received<net::Welcome>(1, net::MessageType::Welcome).empty());
    EXPECT_EQ(0u, _session->players().size());
}

TEST_F(CoopHostileTest, every_command_type_from_a_player_with_no_companion_changes_nothing) {
    traskJoins();
    join(1, "Guest");
    for (int type = static_cast<int>(net::CommandType::MoveTo); type <= static_cast<int>(net::CommandType::DisarmTrap); ++type) {
        net::Command command;
        command.seq = static_cast<uint32_t>(type);
        command.type = static_cast<net::CommandType>(type);
        command.actor = type % 2 ? _trask->id() : _player->id();
        command.target = _sith->id();
        command.point = {4.0f, 3.0f, 0.0f};
        command.param = 1;
        command.item = "g_i_medeqpmnt01";
        say(1, command);
    }
    exchange();
    tick();

    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_TRUE(_player->actions().empty());
    EXPECT_TRUE(_sith->actions().empty());
    for (const auto &result : _fake->received<net::CommandResult>(1, net::MessageType::CommandResult)) {
        // The only orders open without a companion are requests to the session.
        bool sessionRequest = result.seq == static_cast<uint32_t>(net::CommandType::Pause) ||
                              result.seq == static_cast<uint32_t>(net::CommandType::Ready) ||
                              result.seq == static_cast<uint32_t>(net::CommandType::PickSlot);
        EXPECT_TRUE(!result.accepted || sessionRequest) << "command type " << result.seq << " was accepted";
    }
}

TEST_F(CoopHostileTest, a_client_cannot_command_a_creature_it_does_not_control) {
    bothJoinTheParty();
    join(1, "Alice");
    join(2, "Bob");
    net::SlotRequest slot;
    say(1, slot);
    say(2, slot);
    exchange();
    tick();
    auto alice = lastSlot(1);
    auto bob = lastSlot(2);
    ASSERT_TRUE(alice && bob);
    ASSERT_EQ(net::SlotStatus::Assigned, alice->status);
    ASSERT_EQ(net::SlotStatus::Assigned, bob->status);
    ASSERT_NE(alice->creatureId, bob->creatureId);

    // Alice tries Bob's companion, the host's character, a hostile, a far creature,
    // an id that does not exist, and the largest id there is.
    uint32_t seq = 100;
    for (uint32_t actor : {bob->creatureId, _player->id(), _sith->id(), _farSith->id(), 0u, 424242u, UINT32_MAX}) {
        net::Command command;
        command.seq = ++seq;
        command.type = net::CommandType::MoveTo;
        command.actor = actor;
        command.point = {4.0f, 3.0f, 0.0f};
        say(1, command);
    }
    exchange();
    tick();

    for (auto creature : {_trask, _mission, _player, _sith, _farSith}) {
        EXPECT_TRUE(creature->actions().empty()) << creature->tag();
    }
    auto results = _fake->received<net::CommandResult>(1, net::MessageType::CommandResult);
    int forged = 0;
    for (const auto &result : results) {
        if (result.seq > 100) {
            ++forged;
            EXPECT_FALSE(result.accepted) << result.seq;
        }
    }
    EXPECT_EQ(7, forged);
}

TEST_F(CoopHostileTest, a_slot_request_cannot_take_a_companion_another_player_holds) {
    bothJoinTheParty();
    join(1, "Alice");
    net::SlotRequest wantTrask;
    wantTrask.preferredTag = "end_trask";
    say(1, wantTrask);
    exchange();
    tick();
    auto alice = lastSlot(1);
    ASSERT_TRUE(alice);
    ASSERT_EQ(_trask->id(), alice->creatureId);

    join(2, "Bob");
    say(2, wantTrask);
    exchange();
    tick();
    auto bob = lastSlot(2);
    ASSERT_TRUE(bob);
    ASSERT_EQ(net::SlotStatus::Assigned, bob->status);
    EXPECT_EQ(_mission->id(), bob->creatureId) << "Bob must get the other companion";
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_TRUE(_session->isPlayerControlled(_mission->id()));
}

TEST_F(CoopHostileTest, oversized_empty_and_wrong_direction_messages_are_violations_not_crashes) {
    join(1, "Guest");
    std::vector<std::vector<uint8_t>> junk;
    junk.push_back({});                                                                 // empty
    junk.push_back({0});                                                                // unknown type
    junk.push_back({255, 1, 2, 3});                                                     // unknown type
    junk.push_back(std::vector<uint8_t>(net::kMaxClientMessageBytes + 1, 8));           // one byte too big
    junk.push_back(std::vector<uint8_t>(1024 * 1024, 8));                               // far too big
    junk.push_back(net::encode(net::Bye {"x"}));                                        // legal, but it ends the session
    for (auto &message : junk) {
        _fake->deliver(1, message);
    }
    // Messages only a host sends.
    _fake->deliver(1, net::encode(net::Welcome {1, 1, 30, "story", 0}));
    _fake->deliver(1, net::encode(net::Notice {net::NoticeKind::Info, "hi"}));
    exchange();

    // Eight violations get a client kicked; the exact count does not matter,
    // only that nothing was accepted and the host is still standing.
    EXPECT_GE(stat("Guest", "violations"), 0);
    EXPECT_EQ(0, stat("Guest", "commandsAccepted"));
    tick();
}

// Review R3: a flood is taken off the network a bounded amount per poll.
TEST(CoopTransportBudget, a_flood_is_drained_within_the_per_poll_budget_and_nothing_is_lost) {
    // A fake network queue: 3000 small messages, with oversized ones mixed in.
    std::deque<std::vector<uint8_t>> queue;
    for (int i = 0; i < 3000; ++i) {
        std::vector<uint8_t> message(40 + i % 200, static_cast<uint8_t>(net::MessageType::Command));
        message[1] = static_cast<uint8_t>(i & 0xff);
        message[2] = static_cast<uint8_t>(i >> 8);
        queue.push_back(std::move(message));
        if (i % 500 == 0) {
            queue.push_back(std::vector<uint8_t>(net::kMaxClientMessageBytes + 1, static_cast<uint8_t>(net::MessageType::Command)));
        }
    }
    size_t queued = queue.size();
    net::ReceiveBudget budget;
    budget.maxMessages = 100;
    budget.maxBytes = 16 * 1024;
    budget.maxMessageBytes = net::kMaxClientMessageBytes;

    std::vector<std::vector<uint8_t>> taken; // what the network library handed out this batch
    std::vector<int> order;
    size_t refused = 0;
    size_t copiedTotal = 0;
    size_t released = 0;
    int polls = 0;
    while (!queue.empty() && polls < 10000) {
        ++polls;
        auto stats = net::drainInbound(
            budget,
            [&](int max, std::vector<net::InboundMessage> &batch) {
                for (int i = 0; i < max && !queue.empty(); ++i) {
                    taken.push_back(std::move(queue.front()));
                    queue.pop_front();
                }
                for (const auto &message : taken) {
                    batch.push_back({1, message.data(), message.size()});
                }
            },
            [&](const net::InboundMessage &message) -> size_t {
                if (message.size > net::clientMessageLimit(message.data[0])) {
                    ++refused; // not copied
                    return 0;
                }
                std::vector<uint8_t> copy(message.data, message.data + message.size);
                order.push_back(copy[1] | (copy[2] << 8));
                copiedTotal += copy.size();
                return copy.size();
            },
            [&]() {
                released += taken.size();
                taken.clear();
            });
        EXPECT_LE(stats.messages, budget.maxMessages) << "poll " << polls;
        EXPECT_LE(stats.bytes, budget.maxBytes) << "poll " << polls;
        if (!queue.empty()) {
            EXPECT_TRUE(stats.budgetSpent) << "a poll stops only on the budget or an empty queue";
        }
    }
    EXPECT_TRUE(queue.empty());
    EXPECT_GT(polls, 30) << "the flood took many polls, not one";
    EXPECT_EQ(queued, released) << "every message handed out is released";
    EXPECT_EQ(6u, refused);
    ASSERT_EQ(3000u, order.size()) << "leftovers stayed queued for the next poll";
    for (int i = 0; i < 3000; ++i) {
        ASSERT_EQ(i, order[i]) << "in order";
    }
    EXPECT_GT(copiedTotal, 0u);
}

TEST_F(CoopHostileTest, a_message_the_transport_refused_for_its_size_is_a_violation) {
    traskJoins();
    join(1, "Guest");
    _fake->deliverRefused(1, 1024 * 1024);
    exchange();
    EXPECT_EQ(1, stat("Guest", "violations"));
    // Legitimate traffic still flows after the budget and refusals.
    say(1, net::SlotRequest());
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(1));
    EXPECT_EQ(net::SlotStatus::Assigned, lastSlot(1)->status);
    net::Command move;
    move.seq = 1;
    move.type = net::CommandType::MoveTo;
    move.actor = _trask->id();
    move.point = {3.0f, 1.0f, 0.0f};
    say(1, move);
    exchange();
    tick();
    EXPECT_EQ(1, stat("Guest", "commandsAccepted"));
}

TEST_F(CoopHostileTest, packets_from_a_connection_the_host_never_saw_are_ignored) {
    traskJoins();
    net::Command move;
    move.type = net::CommandType::MoveTo;
    move.actor = _trask->id();
    _fake->deliver(77, net::encode(hello("Ghost")));
    _fake->deliver(77, net::encode(move));
    exchange();
    tick();
    EXPECT_TRUE(_fake->received<net::Welcome>(77, net::MessageType::Welcome).empty());
    EXPECT_EQ(0u, _session->players().size());

    // A hello delayed past its connection's close finds nobody there.
    join(1, "Guest");
    _fake->drop(1);
    exchange();
    _fake->deliver(1, net::encode(hello("Guest")));
    exchange();
    EXPECT_EQ(0u, _session->players().size());
}

TEST_F(CoopHostileTest, acks_that_arrive_late_twice_or_for_snapshots_never_sent_do_not_move_the_base) {
    traskJoins();
    join(1, "Guest");
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    ASSERT_TRUE(_session->statusJson().at("clients").as_array().size() == 1);
    tick();
    tick();
    tick();
    auto snapshots = _fake->received<net::Snapshot>(1, net::MessageType::Snapshot);
    ASSERT_GE(snapshots.size(), 3u);
    uint32_t generation = snapshots[0].generation;
    uint32_t newest = snapshots.back().tick;
    uint32_t older = snapshots[snapshots.size() - 2].tick;

    say(1, net::SnapshotAck {generation, newest});
    exchange();
    EXPECT_EQ(newest, stat("Guest", "ackedTick"));

    // The same ack again, an older one arriving late, and one from another module visit.
    say(1, net::SnapshotAck {generation, newest});
    say(1, net::SnapshotAck {generation, older});
    say(1, net::SnapshotAck {generation + 7, newest + 1});
    exchange();
    EXPECT_EQ(newest, stat("Guest", "ackedTick"));
    EXPECT_EQ(0, stat("Guest", "violations"));

    // An ack for a tick the host never sent is a violation, and also moves nothing.
    say(1, net::SnapshotAck {generation, newest + 1000});
    exchange();
    EXPECT_EQ(newest, stat("Guest", "ackedTick"));
    EXPECT_EQ(1, stat("Guest", "violations"));
}

TEST_F(CoopHostileTest, a_replayed_or_reordered_command_is_not_run_a_second_time) {
    traskJoins();
    join(1, "Guest");
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(1) && lastSlot(1)->status == net::SlotStatus::Assigned);

    auto moveTo = [&](uint32_t seq, float x) {
        net::Command command;
        command.seq = seq;
        command.type = net::CommandType::MoveTo;
        command.actor = _trask->id();
        command.point = {x, 1.0f, 0.0f};
        return command;
    };
    say(1, moveTo(5, 4.0f));
    exchange();
    tick();
    ASSERT_EQ(1u, _trask->actions().size());
    _trask->clearAllActions();

    // The same command delivered again, and an older one that was held back.
    say(1, moveTo(5, 4.0f));
    say(1, moveTo(3, 9.0f));
    exchange();
    tick();
    EXPECT_TRUE(_trask->actions().empty()) << "a replay must not run again";
    EXPECT_EQ(2, stat("Guest", "violations"));

    // The next in order still works.
    say(1, moveTo(6, 5.0f));
    exchange();
    tick();
    EXPECT_EQ(1u, _trask->actions().size());
}

// Rejoining

TEST_F(CoopHostileTest, a_rejoin_with_the_players_token_replaces_the_old_connection_and_returns_the_companion) {
    traskJoins();
    uint64_t token = join(1, "Alice");
    ASSERT_NE(0u, token);
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));

    // The old connection is still open when the same player comes back.
    uint64_t again = join(2, "Alice", token);
    EXPECT_EQ(token, again) << "the same player keeps the same token";
    EXPECT_TRUE(_fake->wasClosedByHost(1));
    say(2, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(2));
    EXPECT_EQ(_trask->id(), lastSlot(2)->creatureId);
    EXPECT_TRUE(clientStatus("Alice").at("trusted").as_bool());
}

// Review R1: a token proves who a player is, not that the host let them in.
TEST_F(CoopHostileTest, a_pending_join_reconnecting_with_its_own_token_stays_pending) {
    auto fake = std::make_unique<FakeHostTransport>();
    _fake = fake.get();
    CoopConfig config;
    config.role = CoopRole::Host;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    config.maxClients = 8;
    config.askToJoin = true;
    _session = std::make_shared<CoopSession>(*_game, config, std::move(fake));
    _session->pollNetwork();
    traskJoins();

    uint64_t token = join(1, "Alice");
    ASSERT_NE(0u, token);
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_FALSE(_session->isPlayerControlled(_trask->id())) << "waiting for the host";
    ASSERT_EQ(1u, _session->players().size());
    ASSERT_TRUE(_session->players()[0].waiting);

    // A second connection with the token from Alice's Welcome, before the first goes.
    uint64_t again = join(2, "Alice", token);
    EXPECT_EQ(token, again) << "the token still identifies Alice";
    EXPECT_TRUE(_fake->wasClosedByHost(1));
    say(2, slot);
    exchange();
    tick();
    tick();
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id())) << "no companion without the host's Accept";
    EXPECT_FALSE(clientStatus("Alice").at("trusted").as_bool());
    if (auto assigned = lastSlot(2)) {
        EXPECT_NE(net::SlotStatus::Assigned, assigned->status);
        EXPECT_NE(net::SlotStatus::StandIn, assigned->status);
    }
    auto players = _session->players();
    ASSERT_EQ(1u, players.size());
    EXPECT_TRUE(players[0].waiting) << "the reconnect is asked about like any pending join";

    ASSERT_TRUE(_session->answerJoin(players[0].clientId, true));
    tick();
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    ASSERT_TRUE(lastSlot(2));
    EXPECT_EQ(_trask->id(), lastSlot(2)->creatureId);

    // Once accepted, a reconnect with the token is trusted and keeps Trask.
    EXPECT_EQ(token, join(3, "Alice", token));
    EXPECT_TRUE(clientStatus("Alice").at("trusted").as_bool());
    say(3, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(3));
    EXPECT_EQ(_trask->id(), lastSlot(3)->creatureId);
}

TEST_F(CoopHostileTest, a_join_waiting_for_the_host_cannot_pause_or_answer_a_ready_check) {
    auto fake = std::make_unique<FakeHostTransport>();
    _fake = fake.get();
    CoopConfig config;
    config.role = CoopRole::Host;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    config.maxClients = 8;
    config.askToJoin = true;
    _session = std::make_shared<CoopSession>(*_game, config, std::move(fake));
    _session->pollNetwork();
    traskJoins();
    join(1, "Alice");
    say(1, net::SlotRequest());
    exchange();
    tick();
    ASSERT_TRUE(_session->players()[0].waiting);

    net::Command pause;
    pause.seq = 1;
    pause.type = net::CommandType::Pause;
    pause.param = 1;
    say(1, pause);
    exchange();
    tick();
    EXPECT_FALSE(_game->isPaused()) << "not before the host accepts";
    auto results = _fake->received<net::CommandResult>(1, net::MessageType::CommandResult);
    ASSERT_FALSE(results.empty());
    EXPECT_FALSE(results.back().accepted);
    EXPECT_EQ(net::CommandReject::NoSlot, results.back().reason);

    ASSERT_TRUE(_session->answerJoin(_session->players()[0].clientId, true));
    tick();
    pause.seq = 2;
    say(1, pause);
    exchange();
    tick();
    EXPECT_TRUE(_game->isPaused()) << "an accepted player pauses as before";
}

TEST_F(CoopHostileTest, a_stolen_token_with_another_name_does_not_throw_the_owner_off) {
    traskJoins();
    uint64_t token = join(1, "Alice");
    ASSERT_NE(0u, token);

    uint64_t attacker = join(2, "Mallory", token);
    EXPECT_FALSE(_fake->wasClosedByHost(1)) << "the token belongs to Alice, not to a client calling itself Mallory";
    EXPECT_NE(token, attacker) << "a token is not handed to a different name";
    EXPECT_FALSE(clientStatus("Mallory").at("trusted").as_bool());
}

TEST_F(CoopHostileTest, forged_and_stale_tokens_are_just_new_players) {
    traskJoins();
    uint64_t alice = join(1, "Alice");
    ASSERT_NE(0u, alice);

    for (uint64_t forged : {uint64_t(1), alice + 1, ~alice, UINT64_MAX}) {
        net::ConnectionId connection = 10 + static_cast<net::ConnectionId>(forged & 0xff);
        uint64_t token = join(connection, "Eve", forged);
        EXPECT_NE(0u, token);
        EXPECT_NE(forged, token) << "a token the host never issued is not honoured";
        EXPECT_FALSE(_fake->wasClosedByHost(1));
        EXPECT_FALSE(clientStatus("Eve").empty() ? clientStatus("Eve").at("trusted").as_bool() : false);
        _fake->drop(connection);
        exchange();
    }
}

TEST_F(CoopHostileTest, a_stranger_using_a_dropped_players_name_does_not_inherit_their_companion) {
    traskJoins();
    uint64_t token = join(1, "Alice");
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    _fake->drop(1);
    exchange();
    tick();
    ASSERT_FALSE(_session->isPlayerControlled(_trask->id())) << "the AI holds Trask for Alice";

    // Somebody else arrives calling themselves Alice, without her token.
    join(2, "Alice");
    say(2, slot);
    exchange();
    tick();
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id())) << "Trask is reserved for the holder of Alice's token";

    // Alice herself comes back with it.
    join(3, "Alice", token);
    say(3, slot);
    exchange();
    tick();
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    ASSERT_TRUE(lastSlot(3));
    EXPECT_EQ(_trask->id(), lastSlot(3)->creatureId);
}

TEST_F(CoopHostileTest, joining_and_leaving_over_and_over_cannot_use_up_the_rejoin_tokens) {
    traskJoins();
    uint64_t token = join(1, "Alice");
    ASSERT_NE(0u, token);
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    _fake->drop(1);
    exchange();

    // While Alice is away, others churn through far more connections than the table holds.
    for (net::ConnectionId i = 100; i < 100 + 400; ++i) {
        join(i, "Churn" + std::to_string(i));
        _fake->drop(i);
        exchange();
    }
    uint64_t again = join(2, "Alice", token);
    EXPECT_EQ(token, again) << "a real player can still rejoin after other connections churned";
    EXPECT_TRUE(clientStatus("Alice").at("trusted").as_bool());
}

TEST_F(CoopHostileTest, a_storm_of_mutated_messages_never_gets_a_client_anything_it_does_not_control) {
    bothJoinTheParty();
    join(1, "Alice");
    join(2, "Bob");
    net::SlotRequest slot;
    say(1, slot);
    say(2, slot);
    exchange();
    tick();
    auto alice = lastSlot(1);
    auto bob = lastSlot(2);
    ASSERT_TRUE(alice && bob);
    ASSERT_EQ(net::SlotStatus::Assigned, alice->status);
    ASSERT_EQ(net::SlotStatus::Assigned, bob->status);

    // One valid sample of every kind of message, then thousands of corrupted copies.
    std::vector<std::vector<uint8_t>> seeds;
    net::Command command;
    command.seq = 1;
    command.type = net::CommandType::Attack;
    command.actor = _player->id();
    command.target = _sith->id();
    seeds.push_back(net::encode(command));
    command.type = net::CommandType::TakeItem;
    seeds.push_back(net::encode(command));
    command.type = net::CommandType::Equip;
    command.item = "g_w_blstrpstl001";
    seeds.push_back(net::encode(command));
    seeds.push_back(net::encode(hello("Mallory")));
    seeds.push_back(net::encode(net::SlotRequest {"end_trask"}));
    seeds.push_back(net::encode(net::SnapshotAck {1, 2}));
    seeds.push_back(net::encode(net::Bye {"x"}));
    seeds.push_back(net::encode(net::Welcome {9, 9, 30, "story", 1}));
    seeds.push_back(net::encode(net::Notice {net::NoticeKind::Info, "x"}));
    net::GuestCharacter guest;
    guest.name = "G";
    guest.classes = {{0, 1}};
    guest.baseHp = 5;
    seeds.push_back(net::encode(guest));

    std::mt19937 random(0xC0FFEE);
    auto pick = [&](size_t bound) { return static_cast<size_t>(random() % bound); };
    for (int round = 0; round < 3000; ++round) {
        auto message = seeds[pick(seeds.size())];
        int edits = 1 + static_cast<int>(pick(4));
        for (int e = 0; e < edits; ++e) {
            switch (pick(5)) {
            case 0:
                if (!message.empty()) message[pick(message.size())] ^= static_cast<uint8_t>(1u << pick(8));
                break;
            case 1:
                if (!message.empty()) message[pick(message.size())] = static_cast<uint8_t>(random());
                break;
            case 2:
                message.resize(pick(message.size() + 1));
                break;
            case 3:
                message.insert(message.begin() + pick(message.size() + 1), static_cast<uint8_t>(random()));
                break;
            default:
                message.insert(message.end(), pick(40), static_cast<uint8_t>(random()));
                break;
            }
        }
        // A corrupted message can legitimately end the connection (too many
        // violations); open a fresh one when it does.
        net::ConnectionId connection = 1;
        if (_fake->wasClosedByHost(1)) {
            break; // Alice was kicked: nothing more to attack from her end
        }
        _fake->deliver(connection, message);
        if (round % 50 == 49) {
            exchange();
            tick();
        }
    }
    exchange();
    tick();

    // Whatever got through, the host's characters and the hostiles were never
    // ordered about by Alice's connection, and the host is still standing.
    EXPECT_TRUE(_player->actions().empty());
    EXPECT_TRUE(_sith->actions().empty());
    EXPECT_TRUE(_farSith->actions().empty());
}

// The client end: a host that replays, or sends what a host never sends.

// Review R2: Free Mode guest records follow the token, not the display name.
class CoopFreeHostileTest : public CoopHostileTest {
protected:
    void startFree(bool askToJoin) {
        _classes = std::make_unique<Classes>(_strings, _twoDas);
        ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([this](ClassType type) {
            auto &made = _made[type];
            if (!made) {
                made = std::make_shared<CreatureClass>(type, *_classes, _strings, _twoDas);
            }
            return made;
        }));
        auto fake = std::make_unique<FakeHostTransport>();
        _fake = fake.get();
        CoopConfig config;
        config.role = CoopRole::Host;
        config.mode = "free";
        config.build = "build-A";
        config.k1 = {true, 1, 2, 3};
        config.maxClients = 7;
        config.askToJoin = askToJoin;
        _session = std::make_shared<CoopSession>(*_game, config, std::move(fake));
        _session->pollNetwork();
    }

    uint64_t joinFree(net::ConnectionId connection, const std::string &name, const std::string &character, uint64_t token = 0) {
        _fake->connect(connection);
        auto message = hello(name, token);
        message.mode = "free";
        say(connection, message);
        exchange();
        auto guest = sampleGuest();
        guest.name = character;
        say(connection, net::SlotRequest());
        say(connection, guest);
        exchange();
        auto welcomes = _fake->received<net::Welcome>(connection, net::MessageType::Welcome);
        return welcomes.empty() ? 0 : welcomes.back().rejoinToken;
    }

    std::string characterOf(const std::string &name) {
        auto status = clientStatus(name);
        if (status.empty() || !status.contains("guest") || !status.at("guest").as_object().contains("name")) {
            return std::string();
        }
        return std::string(status.at("guest").as_object().at("name").as_string());
    }

    int64_t guestRecords() { return _session->statusJson().at("guestRecords").to_number<int64_t>(); }

    NiceMock<MockStrings> _strings;
    NiceMock<MockTwoDAs> _twoDas;
    std::unique_ptr<Classes> _classes;
    std::map<ClassType, std::shared_ptr<CreatureClass>> _made;
};

TEST_F(CoopFreeHostileTest, characters_of_joins_the_host_never_let_in_are_not_kept) {
    startFree(true);
    for (net::ConnectionId i = 100; i < 160; ++i) {
        joinFree(i, "Pending" + std::to_string(i), "Char" + std::to_string(i));
        tick();
        ASSERT_EQ(1u, _session->players().size());
        ASSERT_TRUE(_session->players()[0].waiting);
        if (i % 2) {
            _fake->drop(i); // leaves while waiting
        } else {
            ASSERT_TRUE(_session->answerJoin(_session->players()[0].clientId, false)); // declined
            tick();
        }
        exchange();
        EXPECT_EQ(0, guestRecords()) << "connection " << i;
    }
    EXPECT_EQ(0, _session->statusJson().at("rejoinTokens").to_number<int64_t>());
}

TEST_F(CoopFreeHostileTest, absent_characters_are_bounded_and_a_reused_name_brings_its_own_character) {
    startFree(false);
    std::vector<uint64_t> tokens;
    for (net::ConnectionId i = 0; i < 40; ++i) {
        tokens.push_back(joinFree(100 + i, "Guest" + std::to_string(i), "Char" + std::to_string(i)));
        tick();
        ASSERT_EQ("Char" + std::to_string(i), characterOf("Guest" + std::to_string(i)));
        _fake->drop(100 + i);
        exchange();
        EXPECT_LE(guestRecords(), 32) << "absent approved records are bounded";
    }
    EXPECT_EQ(32, guestRecords());

    // The newest absent player still comes back to the character the host kept.
    joinFree(200, "Guest39", "Impostor", tokens[39]);
    tick();
    EXPECT_EQ("Char39", characterOf("Guest39"));
    _fake->drop(200);
    exchange();

    // The oldest was retired with its token: a new player taking the name
    // plays the character they submitted, not the old one.
    joinFree(201, "Guest0", "Newcomer");
    tick();
    EXPECT_EQ("Newcomer", characterOf("Guest0"));
    // Nor does the retired token bring the old character back.
    _fake->drop(201);
    exchange();
    joinFree(202, "Guest1", "Fresh", tokens[1]);
    tick();
    EXPECT_EQ("Fresh", characterOf("Guest1"));
}

class CoopClientHostileTest : public CoopNetTest {
protected:
    void SetUp() override {
        CoopNetTest::SetUp();
        auto [host, client] = net::createLoopbackPair();
        _host = std::move(host);
        CoopConfig config;
        config.role = CoopRole::Client;
        config.build = "build-A";
        _clientGame = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _clientGame->initLocalServices();
        _session = std::make_unique<CoopSession>(*_clientGame, config, std::move(client));
        _session->pollNetwork();
    }

    void hostSends(const std::vector<uint8_t> &bytes) {
        std::vector<net::TransportEvent> events;
        _host->poll(events);
        _host->send(1, bytes, net::SendMode::Reliable);
        _session->pollNetwork();
    }

    boost::json::object status() {
        return _session->statusJson();
    }

    std::unique_ptr<net::ITransport> _host;
    std::unique_ptr<Game> _clientGame;
    std::unique_ptr<CoopSession> _session;
};

TEST_F(CoopClientHostileTest, snapshots_for_a_module_visit_the_client_is_not_in_are_dropped_and_never_acknowledged) {
    for (uint32_t tick : {10u, 11u, 10u, 9u}) { // new, newer, a replay, an older one
        net::Snapshot snapshot;
        snapshot.tick = tick;
        snapshot.generation = 4;
        snapshot.worldFields = 0;
        hostSends(net::encode(snapshot));
    }

    auto root = status();
    EXPECT_EQ(4, root.at("snapshotsDropped").to_number<int64_t>());
    EXPECT_EQ(0, root.at("snapshotsApplied").to_number<int64_t>());
    EXPECT_EQ(0, root.at("acksSent").to_number<int64_t>()) << "a snapshot the client cannot use is not acknowledged";
    EXPECT_EQ(0, root.at("appliedTick").to_number<int64_t>());
}

TEST_F(CoopClientHostileTest, messages_a_host_never_sends_are_counted_and_change_nothing) {
    hostSends(net::encode(net::Hello {}));
    hostSends(net::encode(net::SlotRequest {"end_trask"}));
    hostSends(net::encode(net::SnapshotAck {1, 1}));
    net::Command command;
    command.type = net::CommandType::MoveTo;
    hostSends(net::encode(command));
    hostSends({});
    hostSends({0});
    hostSends(std::vector<uint8_t>(net::kMaxHostMessageBytes + 1, 7));

    auto root = status();
    EXPECT_EQ(0, root.at("snapshotsApplied").to_number<int64_t>());
    EXPECT_EQ(0, root.at("acksSent").to_number<int64_t>());
    EXPECT_FALSE(root.at("loaded").as_bool());
}

// A restarted game and its saved rejoin token

class CoopRejoinStoreTest : public CoopNetTest {
protected:
    void SetUp() override {
        CoopNetTest::SetUp();
        _dir = std::filesystem::temp_directory_path() / ("reone_rejoin_test_" + std::to_string(std::random_device {}()));
        std::filesystem::create_directories(_dir);
        _store = _dir / "coop_rejoin.json";
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(_dir, ec);
        CoopNetTest::TearDown();
    }

    /** A client game "launched" from scratch: its own session over a fresh loopback pair. */
    void launchClient(const std::string &name = "Alice", const std::string &key = "addr:127.0.0.1:27015") {
        _session.reset();
        auto [host, client] = net::createLoopbackPair();
        _host = std::move(host);
        CoopConfig config;
        config.role = CoopRole::Client;
        config.build = "build-A";
        config.playerName = name;
        config.rejoinStore = _store;
        config.sessionKey = key;
        _clientGame = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _clientGame->initLocalServices();
        _session = std::make_unique<CoopSession>(*_clientGame, config, std::move(client));
        _session->pollNetwork();
    }

    /** The token in the Hello the host side received. */
    uint64_t helloToken() {
        std::vector<net::TransportEvent> events;
        _host->poll(events);
        for (const auto &event : events) {
            net::Hello hello;
            if (event.type == net::TransportEventType::Message && !event.data.empty() &&
                event.data[0] == static_cast<uint8_t>(net::MessageType::Hello) && net::decode(event.data, hello)) {
                return hello.rejoinToken;
            }
        }
        return UINT64_MAX;
    }

    void hostSends(const std::vector<uint8_t> &bytes) {
        _host->send(1, bytes, net::SendMode::Reliable);
        _session->pollNetwork();
    }

    std::filesystem::path _dir;
    std::filesystem::path _store;
    std::unique_ptr<net::ITransport> _host;
    std::unique_ptr<Game> _clientGame;
    std::unique_ptr<CoopSession> _session;
};

TEST_F(CoopRejoinStoreTest, a_saved_token_is_found_only_for_its_session_and_name_and_expires) {
    saveRejoinToken(_store, "code:K7QM4TXR", "Alice", 0xFEDCBA9876543210ull, 1000);
    EXPECT_EQ(0xFEDCBA9876543210ull, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1001)) << "a 64-bit token survives the file";
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:K7QM4TXR", "Bob", 1001));
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:OTHER", "Alice", 1001));
    EXPECT_EQ(0xFEDCBA9876543210ull, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1000 + kRejoinTokenLifetimeSeconds - 1));
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1000 + kRejoinTokenLifetimeSeconds)) << "it expires";
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1001)) << "and an expired token is gone";

    saveRejoinToken(_store, "code:K7QM4TXR", "Alice", 7, 1000);
    forgetRejoinToken(_store, "code:K7QM4TXR", "Alice");
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1001));

    std::ofstream(_store, std::ios::binary | std::ios::trunc) << "{ not json";
    EXPECT_EQ(0u, loadRejoinToken(_store, "code:K7QM4TXR", "Alice", 1001)) << "a damaged file is just no token";
}

TEST_F(CoopRejoinStoreTest, a_restarted_client_presents_the_token_it_was_given) {
    launchClient();
    EXPECT_EQ(0u, helloToken()) << "a first join has no token";
    net::Welcome welcome;
    welcome.rejoinToken = 0x1122334455667788ull;
    welcome.mode = "story";
    hostSends(net::encode(welcome));

    launchClient(); // the game was restarted
    EXPECT_EQ(0x1122334455667788ull, helloToken());

    launchClient("Mallory");
    EXPECT_EQ(0u, helloToken()) << "another name does not pick it up";
    launchClient("Alice", "addr:10.0.0.9:27015");
    EXPECT_EQ(0u, helloToken()) << "nor does another session";
}

TEST_F(CoopRejoinStoreTest, the_token_is_cleared_when_the_host_rejects_or_ends_the_session) {
    for (int ending = 0; ending < 2; ++ending) {
        launchClient();
        helloToken();
        net::Welcome welcome;
        welcome.rejoinToken = 99;
        welcome.mode = "story";
        hostSends(net::encode(welcome));
        ASSERT_EQ(99u, loadRejoinToken(_store, "addr:127.0.0.1:27015", "Alice", 0));
        if (ending == 0) {
            net::Reject reject;
            reject.reason = net::RejectReason::Declined;
            hostSends(net::encode(reject));
        } else {
            hostSends(net::encode(net::Bye {"the host quit"}));
        }
        EXPECT_EQ(0u, loadRejoinToken(_store, "addr:127.0.0.1:27015", "Alice", 0)) << (ending == 0 ? "rejected" : "ended");
        launchClient();
        EXPECT_EQ(0u, helloToken());
    }
}

TEST_F(CoopHostileTest, a_restarted_client_with_its_saved_token_reclaims_its_slot_but_a_namesake_without_it_gets_a_stand_in) {
    auto dir = std::filesystem::temp_directory_path() / ("reone_rejoin_host_" + std::to_string(std::random_device {}()));
    std::filesystem::create_directories(dir);
    auto store = dir / "coop_rejoin.json";
    traskJoins();
    uint64_t token = join(1, "Alice");
    ASSERT_NE(0u, token);
    saveRejoinToken(store, "addr:h", "Alice", token, 0); // what the client does on Welcome
    net::SlotRequest slot;
    say(1, slot);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));

    // The game crashes: the connection goes and the AI holds Trask.
    _fake->drop(1);
    exchange();
    tick();
    ASSERT_FALSE(_session->isPlayerControlled(_trask->id()));

    // Somebody else, same name, no token; then one with a wrong token.
    join(2, "Alice");
    say(2, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(2));
    EXPECT_NE(_trask->id(), lastSlot(2)->creatureId);
    EXPECT_EQ(net::SlotStatus::StandIn, lastSlot(2)->status);
    _fake->drop(2);
    exchange();
    join(4, "Alice", token ^ 1);
    say(4, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(4));
    EXPECT_NE(_trask->id(), lastSlot(4)->creatureId);
    EXPECT_FALSE(_session->isPlayerControlled(_trask->id()));
    _fake->drop(4);
    exchange();

    // Alice's restarted game reads the saved token and sends it.
    uint64_t saved = loadRejoinToken(store, "addr:h", "Alice", 1);
    ASSERT_EQ(token, saved);
    EXPECT_EQ(token, join(3, "Alice", saved));
    say(3, slot);
    exchange();
    tick();
    ASSERT_TRUE(lastSlot(3));
    EXPECT_EQ(_trask->id(), lastSlot(3)->creatureId);
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
