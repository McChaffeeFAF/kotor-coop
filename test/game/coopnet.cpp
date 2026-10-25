/* Copyright (c) 2026 The reone project contributors */

#include <filesystem>
#include <functional>
#include <fstream>
#include <random>
#include <cstdlib>
#include <set>
#include <sstream>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <glm/gtc/constants.hpp>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"
#include "../fixtures/scene.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/action.h"
#include "reone/game/coopcommand.h"
#include "reone/script/program.h"
#include "reone/game/coopflow.h"
#include "reone/game/coopidmap.h"
#include "reone/game/cooploot.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/wait.h"
#include "reone/game/location.h"
#include "reone/game/combat.h"
#include "reone/game/effect.h"
#include "reone/game/coopreplication.h"
#include "reone/game/companionbrain.h"
#include "reone/game/coopsession.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/guestbenchscreen.h"
#include "reone/resource/dialog.h"
#include "reone/game/coopprogress.h"
#include "reone/game/autolevel.h"
#include "reone/game/cameo.h"
#include "reone/game/coopcast.h"
#include "reone/game/coopstandin.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/visualeffects.h"
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

// Owner report on alpha-0.4.5 (Story Mode, Smarter Companions on): AI
// movement seemed to fight the guest's character. The brain must never
// decide for a creature a player controls, the host's leader or a guest's.
TEST_F(CoopCompanionCombatTest, smarter_companions_never_drive_a_creature_a_player_controls) {
    _engine.options().game.smartCompanions = true;
    traskJoins();
    _trask->setOnEndRound("k_hen_combend01");
    _player->setOnEndRound("k_hen_combend01");
    EXPECT_TRUE(companion::usesBrain(*_game, *_trask)) << "an AI companion on the generic script";
    EXPECT_FALSE(companion::usesBrain(*_game, *_player)) << "the host's leader";
    _client.send(hello());
    exchange();
    net::SlotRequest request;
    _client.send(request);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_FALSE(companion::usesBrain(*_game, *_trask)) << "the guest's character";
    // Its end of round runs neither the brain nor the script.
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_hen_combend01")).Times(0);
    _trask->setObjectSeen(_sith, true);
    sithAttacksPlayer();
    finishRounds();
    EXPECT_TRUE(_trask->actions().empty()) << "no order the guest did not give";
    _engine.options().game.smartCompanions = false;
}

// Owner report (single player too): companions "following inefficiently"
// some of the time. FollowLeader ended on arrival and nothing followed again
// until the next heartbeat re-issued it (up to 6 s), so a companion stood
// while the leader walked off and then ran to catch up.
TEST_F(CoopCompanionCombatTest, an_arrived_follower_goes_after_the_leader_again_without_waiting_for_its_heartbeat) {
    traskJoins();
    _player->setPosition(glm::vec3(0.0f));
    _trask->setPosition(glm::vec3(3.0f, 0.0f, 0.0f));
    _trask->clearAllActions();
    _trask->setFollowingLeader(true); // as a FollowLeader that arrived leaves it
    _trask->updateIdleFollow(0.25f);
    EXPECT_TRUE(_trask->actions().empty()) << "close enough: stays";

    _trask->setPosition(glm::vec3(6.0f, 0.0f, 0.0f)); // the leader walked off
    _trask->updateIdleFollow(0.25f);
    ASSERT_FALSE(_trask->actions().empty());
    EXPECT_EQ(ActionType::FollowLeader, _trask->actions().front()->type());

    // Ordered to do something else: the follow is over until a script issues one.
    _trask->clearAllActions();
    EXPECT_FALSE(_trask->isFollowingLeader());
    _trask->updateIdleFollow(0.25f);
    EXPECT_TRUE(_trask->actions().empty());

    // A guest's character is never sent after the host's leader.
    _trask->setFollowingLeader(true);
    _client.send(hello());
    exchange();
    net::SlotRequest request;
    _client.send(request);
    exchange();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    _trask->clearAllActions();
    _trask->setFollowingLeader(true);
    _trask->updateIdleFollow(0.25f);
    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_FALSE(_trask->isFollowingLeader());
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

TEST_F(CoopCompanionCombatTest, a_creature_its_player_steers_is_not_idle) {
    // Playtest 2026-10-22: a guest walking with its movement keys through a
    // fight stalled every second: steering leaves the queue empty, so idle
    // engage queued an attack and the swing held the creature in place.
    traskTaken();
    _trask->setObjectSeen(_sith, true);
    sithAttacksPlayer();
    for (int i = 0; i < 12; ++i) {
        _trask->noteDirectControl(); // what hostApplySteering does each tick it moves it
        idle(0.25f);
    }
    EXPECT_TRUE(_trask->actions().empty()) << "three seconds of steering, no attack of its own";
    idle(1.25f);
    EXPECT_EQ(_sith, traskTarget()) << "let go, it joins the fight again";
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

TEST_F(CoopSessionTest, a_beam_event_names_its_source_so_guests_can_draw_it) {
    // Design note v14_programmed_effects, section 6.
    _client.send(hello());
    exchange();
    tick();
    VisualEffectDesc desc;
    desc.id = 2038;
    auto request = std::make_shared<VisualEffectPresenter::Request>();
    request->desc = &desc;
    request->targetId = _sith->id();
    request->sourceId = _player->id();
    request->sourceBodyNode = static_cast<int>(BodyNode::HandRight);
    _session->onVisualEffectBegin(request);
    _client.inbox.clear();
    tick();
    auto batches = _client.take<net::EventBatch>(net::MessageType::Events);
    ASSERT_EQ(1u, batches.size());
    ASSERT_EQ(1u, batches[0].events.size());
    const auto &event = batches[0].events[0];
    EXPECT_EQ(net::EventKind::VfxBegin, event.kind);
    EXPECT_EQ(2038u, event.row);
    EXPECT_NE(0u, event.a);
    EXPECT_NE(0u, event.b) << "the beam's source travels";
    EXPECT_NE(event.a, event.b);
    EXPECT_EQ(static_cast<int>(BodyNode::HandRight), event.flags);
}

namespace reone {

namespace game {

// Drives a client session's event handling directly: the host side of these
// tests has no client game to replicate into (design note v15_vfx_leftovers).
class TestCoopSession {
public:
    static void bindId(CoopSession &session, uint32_t hostId, uint32_t localId) {
        session._ids.bind(hostId, localId);
    }

    static void applyEvent(CoopSession &session, const net::NetEvent &event) {
        session.clientApplyEvent(event);
    }

    /** The guest's copy localId stands for the host's hostId, as a bound snapshot leaves it. */
    static void bindReplica(CoopSession &session, uint32_t hostId, uint32_t localId) {
        session._ids.bind(hostId, localId);
        session._replicas[hostId].localId = localId;
    }

    static void applyEntity(CoopSession &session, const net::EntityRecord &record) {
        session.clientApplyEntity(record);
    }

    /** The guest was welcomed as clientId and told it plays a stand-in under hostId. */
    static void setStandInSlot(CoopSession &session, uint32_t clientId, uint32_t hostId) {
        session._welcome.clientId = clientId;
        session._slot.status = net::SlotStatus::StandIn;
        session._slot.creatureId = hostId;
        session._slot.tag = "coop_standin_" + std::to_string(clientId);
    }

    static void setReplicaTag(CoopSession &session, uint32_t hostId, const std::string &tag) {
        session._replicas[hostId].state.tag = tag;
        session._replicas[hostId].state.id = hostId;
    }

    static void resolveControlled(CoopSession &session) {
        session.clientResolveControlled();
    }

    /** The client's keys are held with nothing to drive: what clientSteer does. */
    static void steerWithNothing(CoopSession &session, float dt) {
        session._clientState = CoopSession::ClientState::Joined;
        session._moduleLoaded = true;
        session.clientSteer(dt);
    }

    static void receiveReputation(CoopSession &session, net::ReputationTable table) {
        session._hostReputation = std::move(table);
        session.clientApplyReputation();
    }
};

} // namespace game

} // namespace reone

/**
 * A guest's session over the fixture's game: its creatures stand in for the
 * guest's own copies of the host's, under host ids that differ from theirs.
 */
class CoopClientSessionTest : public CoopNetTest {
protected:
    static constexpr uint32_t kHostSith = 9001;
    static constexpr uint32_t kHostPlayer = 9002;

    void SetUp() override {
        CoopNetTest::SetUp();
        auto [host, client] = net::createLoopbackPair();
        _host = std::move(host);
        CoopConfig config;
        config.role = CoopRole::Client;
        config.build = "build-A";
        _session = std::make_unique<CoopSession>(*_game, config, std::move(client));
        TestCoopSession::bindId(*_session, kHostSith, _sith->id());
        TestCoopSession::bindId(*_session, kHostPlayer, _player->id());
    }

    void TearDown() override {
        _session.reset();
        CoopNetTest::TearDown();
    }

    std::unique_ptr<net::ITransport> _host;
    std::unique_ptr<CoopSession> _session;
};

TEST_F(CoopClientSessionTest, a_guest_draws_a_beam_from_its_own_copy_of_the_source) {
    // Design note v14_programmed_effects, section 6: the guest side.
    VisualEffectDesc desc;
    desc.id = 2038;
    desc.kind = VisualEffectDesc::Kind::Duration;
    ON_CALL(_engine.gameModule().visualEffects(), get(2038u))
        .WillByDefault(Return(std::optional<const VisualEffectDesc *>(&desc)));

    net::NetEvent begin;
    begin.kind = net::EventKind::VfxBegin;
    begin.handle = 7;
    begin.row = 2038;
    begin.a = kHostSith;
    begin.b = kHostPlayer;
    begin.flags = static_cast<uint8_t>(BodyNode::HandRight);
    TestCoopSession::applyEvent(*_session, begin);

    const auto &pending = _game->visualEffectPresenter().pending();
    ASSERT_EQ(1u, pending.size());
    EXPECT_EQ(&desc, pending[0]->desc);
    EXPECT_EQ(_sith->id(), pending[0]->targetId) << "the target, by the guest's own id";
    EXPECT_EQ(_player->id(), pending[0]->sourceId) << "the beam leaves the guest's copy of the source";
    EXPECT_EQ(static_cast<int>(BodyNode::HandRight), pending[0]->sourceBodyNode);

    // A source the guest does not know: the row still shows on the target,
    // with no beam.
    net::NetEvent unknown = begin;
    unknown.handle = 8;
    unknown.b = 4242;
    TestCoopSession::applyEvent(*_session, unknown);
    ASSERT_EQ(2u, pending.size());
    EXPECT_EQ(_sith->id(), pending[1]->targetId);
    EXPECT_EQ(0u, pending[1]->sourceId);

    // The host's end reaches the right request.
    net::NetEvent end;
    end.kind = net::EventKind::VfxEnd;
    end.handle = 7;
    TestCoopSession::applyEvent(*_session, end);
    EXPECT_TRUE(pending[0]->ended);
    EXPECT_FALSE(pending[1]->ended);
}

TEST_F(CoopClientSessionTest, a_respawned_stand_in_is_found_by_its_tag_when_the_slot_lags) {
    // Playtest 2026-10-22 row 12: a conversation takes the stand-in away and
    // it comes back under a new host id. Whatever the slot last said, the
    // guest's own stand-in is the one tagged with its client id.
    constexpr uint32_t kOld = 611;
    constexpr uint32_t kNew = 617;
    TestCoopSession::setStandInSlot(*_session, 3, kOld);
    TestCoopSession::bindReplica(*_session, kNew, _trask->id());
    TestCoopSession::setReplicaTag(*_session, kNew, "coop_standin_3");
    TestCoopSession::bindReplica(*_session, 700, _sith->id());
    TestCoopSession::setReplicaTag(*_session, 700, "coop_standin_4"); // another player's
    EXPECT_EQ(kOld, _session->controlledHostId());
    TestCoopSession::resolveControlled(*_session);
    EXPECT_EQ(kNew, _session->controlledHostId());
    EXPECT_EQ(_trask, _session->localObjectForHost(_session->controlledHostId()));
}

TEST_F(CoopClientSessionTest, held_keys_with_nothing_to_drive_say_so) {
    // Row 13: no silent input loss. With no module loaded the keys are not
    // read; the notice itself is what the player sees once they are.
    EXPECT_EQ(nullptr, coopNoControlNotice(611, true));
    EXPECT_STREQ("You have no character to control yet", coopNoControlNotice(0, false));
    EXPECT_STREQ("Your character is on its way", coopNoControlNotice(611, false));
    TestCoopSession::steerWithNothing(*_session, 0.1f);
    EXPECT_EQ(0, _session->statusJson().at("inputDropped").to_number<int64_t>()) << "no keys held, nothing dropped";
}

TEST_F(CoopClientSessionTest, a_creature_the_host_turns_hostile_turns_hostile_on_the_guest) {
    // The guest's client runs no scripts: ChangeToStandardFaction on the host
    // reaches its copy only as the replicated faction.
    _trask->setFaction(Faction::Friendly1);
    TestCoopSession::bindReplica(*_session, 9003, _trask->id());
    net::EntityRecord record;
    record.fields = net::kFieldFaction;
    record.state.id = 9003;
    record.state.faction = static_cast<uint8_t>(Faction::Hostile1);
    TestCoopSession::applyEntity(*_session, record);
    EXPECT_EQ(Faction::Hostile1, _trask->faction());

    // A delta without the field leaves it as it is.
    record.fields = net::kFieldPosition;
    record.state.position = {3.0f, 0.0f, 0.0f};
    TestCoopSession::applyEntity(*_session, record);
    EXPECT_EQ(Faction::Hostile1, _trask->faction());

    // No faction at all on the host.
    record.fields = net::kFieldFaction;
    record.state.faction = net::kNoFaction;
    TestCoopSession::applyEntity(*_session, record);
    EXPECT_EQ(Faction::Invalid, _trask->faction());
}

TEST_F(CoopClientSessionTest, the_host_s_reputation_table_replaces_the_guest_s_own) {
    auto &reputes = _engine.gameModule().reputes();
    IReputes::State local;
    local.factions.resize(2);
    local.labels.resize(2);
    local.values = {{100, 100}, {100, 100}};
    ON_CALL(reputes, state()).WillByDefault(Return(local));
    ON_CALL(reputes, getReputation(_, _)).WillByDefault(Return(100));
    IReputes::State replaced;
    EXPECT_CALL(reputes, replace(_)).Times(1).WillOnce(Invoke([&](IReputes::State state) { replaced = std::move(state); }));

    net::ReputationTable table;
    table.values = {{100, 0, 50}, {0, 100, 50}, {50, 50, 100}};
    TestCoopSession::receiveReputation(*_session, table);
    ASSERT_EQ(3u, replaced.values.size());
    EXPECT_EQ(0, replaced.values[0][1]) << "the player faction is hostile to Hostile_1 because the host says so";
    EXPECT_EQ(0, replaced.values[1][0]);
    EXPECT_EQ(50, replaced.values[2][0]);
    EXPECT_EQ(3u, replaced.factions.size()) << "the table grows to the host's faction count";
    EXPECT_EQ(1u, _session->reputationTablesApplied());
}

TEST_F(CoopClientSessionTest, a_table_the_guest_already_has_is_not_applied_again) {
    auto &reputes = _engine.gameModule().reputes();
    ON_CALL(reputes, getReputation(_, _)).WillByDefault(Invoke([](Faction source, Faction target) {
        return source == target ? 100 : 0;
    }));
    EXPECT_CALL(reputes, replace(_)).Times(0);
    net::ReputationTable table;
    table.values = {{100, 0}, {0, 100}};
    TestCoopSession::receiveReputation(*_session, table);
    EXPECT_EQ(0u, _session->reputationTablesApplied());
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
    {
        // The request is in the host's join queue (headless: no prompt up).
        auto status = _session->statusJson();
        ASSERT_TRUE(status.contains("joinQueue"));
        auto &queue = status.at("joinQueue").as_array();
        ASSERT_EQ(1u, queue.size());
        EXPECT_EQ(players[0].clientId, queue[0].to_number<uint32_t>());
        EXPECT_EQ(0, status.at("joinPrompt").to_number<int>());
    }
    EXPECT_FALSE(_session->answerJoin(players[0].clientId + 1, true)) << "no request from another client";
    ASSERT_TRUE(_session->answerJoin(players[0].clientId, true));
    tick();
    EXPECT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_FALSE(_session->answerJoin(players[0].clientId, true)) << "answered already";
    EXPECT_TRUE(_session->statusJson().at("joinQueue").as_array().empty());

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
    for (const char *junk : {"@standin:", "@standin:1:2", "@standin:1:2:3:4:5", "@standin:1:2:3:T", "@standin:-1:0:3", "@standin:1:9:3",
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

TEST_F(CoopStandInSessionTest, the_stand_in_carries_its_players_name_and_the_whole_look) {
    join();
    _client.inbox.clear();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    EXPECT_FALSE(standIn->name().empty());
    EXPECT_NE(_player->name(), standIn->name()) << "the guest's name, not the host's";
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_FALSE(snapshots.empty());
    const net::EntityState *standInState = nullptr;
    const net::EntityState *hostState = nullptr;
    for (const auto &snapshot : snapshots) {
        for (const auto &record : snapshot.entities) {
            if ((record.fields & net::kFieldLook) == 0) {
                continue;
            }
            if (record.state.id == id) {
                standInState = &record.state;
            } else if (record.state.id == _player->id()) {
                hostState = &record.state;
            }
        }
    }
    ASSERT_TRUE(standInState) << "the stand-in's look travels";
    EXPECT_EQ(standIn->name(), standInState->look.playerName);
    EXPECT_GE(standInState->look.bodyVariation, 0) << "marks a whole look";
    EXPECT_GE(standInState->look.textureVariation, 0);
    EXPECT_EQ(standIn->headRow(), standInState->look.headRow);
    ASSERT_TRUE(hostState) << "the host's own character is named too";
    EXPECT_EQ(0, hostState->look.colour);
    EXPECT_EQ(-1, hostState->look.bodyVariation) << "only a stand-in sends the whole look";

    // The same on both ends of the session's own presence list.
    bool found = false;
    for (const auto &presence : _session->presences()) {
        if (presence.object == standIn) {
            found = true;
            EXPECT_TRUE(presence.outsideParty);
            EXPECT_FALSE(presence.own);
            EXPECT_EQ(standIn->name(), presence.name);
        }
    }
    EXPECT_TRUE(found);
}

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

// Playtest 2026-10-22, rows 1, 12 and 14 (design note coop_playtest_1022).

static net::Steer steerMessage(uint32_t seq, uint32_t actor, bool moving, float facing = 0.0f, bool run = true) {
    net::Steer steer;
    steer.seq = seq;
    steer.actor = actor;
    steer.moving = moving;
    steer.facing = facing;
    steer.run = run;
    return steer;
}

TEST_F(CoopStandInSessionTest, the_host_reports_the_stand_in_as_the_guest_s_creature) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    auto status = _session->statusJson();
    const auto &guest = status.at("clients").as_array()[0].as_object();
    EXPECT_TRUE(guest.at("controlled").as_bool()) << boost::json::serialize(guest);
    EXPECT_EQ(static_cast<int64_t>(id), guest.at("creature").to_number<int64_t>());
    EXPECT_EQ("standin", std::string(guest.at("slot").as_object().at("status").as_string()));
    EXPECT_EQ(static_cast<int64_t>(id), guest.at("slot").as_object().at("id").to_number<int64_t>());
    EXPECT_EQ(1u, status.at("controlled").as_object().size()) << "the stand-in is in the controlled list";
}

TEST_F(CoopStandInSessionTest, movement_keys_steer_the_stand_in_directly_each_tick_until_a_stop) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    auto start = standIn->position();
    // Facing pi/2: direction (-1, 0), as the host's own keys read it.
    _client.send(steerMessage(1, id, true, glm::half_pi<float>(), true));
    for (int i = 0; i < 5; ++i) {
        tick();
    }
    EXPECT_TRUE(standIn->actions().empty()) << "no MoveTo is queued: it moves directly";
    EXPECT_EQ(Creature::MovementType::Run, standIn->movementType());
    EXPECT_NEAR(glm::half_pi<float>(), standIn->getFacing(), 1e-3f);
    float expected = standIn->runSpeed() * 5.0f * _game->tickSeconds();
    EXPECT_NEAR(expected, glm::distance(start, standIn->position()), 1e-3f) << "the creature's own run speed, every tick";
    if (expected > 0.0f) {
        EXPECT_LT(standIn->position().x, start.x);
    }
    auto status = _session->statusJson().at("clients").as_array()[0].as_object().at("steer").as_object();
    EXPECT_EQ(5, status.at("ticks").to_number<int64_t>());

    // A stop (reliable) ends it at once; an older update arriving late is spent.
    _client.send(steerMessage(3, id, false));
    _client.transport->send(_client.connection, net::encode(steerMessage(2, id, true, 0.0f)), net::SendMode::Unreliable);
    tick();
    auto stopped = standIn->position();
    tick();
    tick();
    EXPECT_EQ(stopped, standIn->position());
    EXPECT_EQ(Creature::MovementType::None, standIn->movementType());

    // Walk, then the updates stop coming: it stops on its own.
    _client.send(steerMessage(4, id, true, 0.0f, false));
    tick();
    EXPECT_EQ(Creature::MovementType::Walk, standIn->movementType());
    int timeout = static_cast<int>(std::ceil(net::kSteerTimeoutSeconds * _game->tickRate()));
    for (int i = 0; i < timeout + 2; ++i) {
        tick();
    }
    EXPECT_EQ(Creature::MovementType::None, standIn->movementType());
    EXPECT_FALSE(_session->statusJson().at("clients").as_array()[0].as_object().at("steer").as_object().at("moving").as_bool());
}

TEST_F(CoopStandInSessionTest, steering_names_only_the_guest_s_own_creature_and_waits_out_conversations) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    auto start = standIn->position();
    _client.inbox.clear();
    // Someone else's creature: refused, and the host repeats which one is theirs.
    _client.send(steerMessage(1, _player->id(), true));
    tick();
    tick();
    EXPECT_EQ(start, standIn->position());
    EXPECT_EQ(start, standIn->position());
    auto sent = slots();
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(id, sent.back().creatureId);

    // During a conversation the host owns the story: no steering.
    _client.send(steerMessage(2, id, true));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    EXPECT_EQ(0u, standInId());
}

TEST_F(CoopStandInSessionTest, the_guest_keeps_control_of_its_stand_in_across_a_conversation) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t first = standInId();
    ASSERT_NE(0u, first);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    _client.inbox.clear();
    tick();
    uint32_t second = standInId();
    ASSERT_NE(0u, second);
    ASSERT_NE(first, second) << "a new object, as before";
    // The new id goes out reliably as the slot.
    auto sent = slots();
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(net::SlotStatus::StandIn, sent.back().status);
    EXPECT_EQ(second, sent.back().creatureId);

    // Input still naming the old stand-in (in flight across the respawn)
    // drives the new one, and the host repeats the slot.
    auto standIn = _game->getObjectById<Creature>(second);
    auto start = standIn->position();
    _client.inbox.clear();
    _client.send(steerMessage(1, first, true, glm::half_pi<float>()));
    tick();
    tick();
    EXPECT_EQ(Creature::MovementType::Run, standIn->movementType());
    EXPECT_NEAR(standIn->runSpeed() * 2.0f * _game->tickSeconds(), glm::distance(start, standIn->position()), 1e-3f);
    sent = slots();
    ASSERT_FALSE(sent.empty());
    EXPECT_EQ(second, sent.back().creatureId);

    net::Command move;
    move.seq = 1;
    move.type = net::CommandType::MoveTo;
    move.actor = first;
    move.point = {-3.0f, 2.0f, 0.0f};
    _client.send(move);
    _session->pollNetwork();
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_EQ(1u, results.size());
    EXPECT_TRUE(results[0].accepted) << net::commandRejectName(results[0].reason);
}

TEST_F(CoopStandInSessionTest, the_keys_win_over_an_attack_the_player_did_not_order) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    // The player's own attack, mid-swing: the keys wait for it, as the host's own do.
    auto ordered = _game->newAction<AttackObjectAction>(_sith);
    ordered->setUserAction(true);
    standIn->addAction(ordered);
    standIn->setMovementRestricted(true);
    _client.send(steerMessage(1, id, true));
    _session->pollNetwork();
    _session->beforeTick();
    EXPECT_FALSE(standIn->actions().empty()) << "a player order is not cut short";
    // An automatic continuation (not the player's order): the keys cancel it and move.
    standIn->clearAllActions();
    standIn->addAction(_game->newAction<AttackObjectAction>(_sith));
    standIn->setMovementRestricted(true);
    auto start = standIn->position();
    _client.send(steerMessage(2, id, true));
    _session->pollNetwork();
    _session->beforeTick();
    EXPECT_TRUE(standIn->actions().empty());
    EXPECT_EQ(Creature::MovementType::Run, standIn->movementType());
    EXPECT_NEAR(standIn->runSpeed() * _game->tickSeconds(), glm::distance(start, standIn->position()), 1e-3f);
}

TEST_F(CoopStandInSessionTest, moving_updates_are_held_to_the_rate_and_stops_never_are) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    // A flood within one tick: past the per-second allowance they are dropped.
    for (uint32_t seq = 1; seq <= static_cast<uint32_t>(net::kMaxSteersPerSecond) + 10; ++seq) {
        _client.transport->send(_client.connection, net::encode(steerMessage(seq, id, true)), net::SendMode::Unreliable);
    }
    _client.send(steerMessage(1000, id, false));
    tick();
    auto steer = _session->statusJson().at("clients").as_array()[0].as_object().at("steer").as_object();
    EXPECT_GE(steer.at("dropped").to_number<int64_t>(), 10);
    EXPECT_FALSE(steer.at("moving").as_bool()) << "the stop went through";
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
        configure(config);
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

    /** A derived fixture changes the host's config before the session exists. */
    virtual void configure(CoopConfig &) {}

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
        hello.password = _password;
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

    std::string _password; // what join() presents
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

// Dedicated host (docs/dedicated_host.md): a password, kicks and the leaderless rules.

class CoopDedicatedSessionTest : public CoopFreeSessionTest {
protected:
    void configure(CoopConfig &config) override {
        config.dedicated = true;
        config.askToJoin = false;
        config.password = "tableword";
        config.maxClients = 3;
    }

    uint32_t clientId() const {
        auto players = _session->players();
        return players.empty() ? 0 : players[0].clientId;
    }
};

TEST_F(CoopDedicatedSessionTest, a_guest_without_the_password_is_turned_away) {
    join(sampleGuest());
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::BadPassword, rejects[0].reason);
    EXPECT_TRUE(_client.take<net::Welcome>(net::MessageType::Welcome).empty());
    EXPECT_EQ(0u, guestId());
}

TEST_F(CoopDedicatedSessionTest, a_wrong_password_is_turned_away_and_a_prefix_of_the_right_one_is_wrong) {
    for (const char *wrong : {"tablewor", "tablewordd", "TABLEWORD", "x"}) {
        _password = wrong;
        _client.inbox.clear();
        join(sampleGuest());
        exchange();
        auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
        ASSERT_EQ(1u, rejects.size()) << wrong;
        EXPECT_EQ(net::RejectReason::BadPassword, rejects[0].reason) << wrong;
        // The connection is closed after a Reject; a fresh pair for the next try.
        _session.reset();
        _game->setCoopSession(nullptr);
        auto [host, client] = net::createLoopbackPair();
        _client = RawClient();
        _client.transport = std::move(client);
        CoopConfig config;
        config.role = CoopRole::Host;
        config.mode = "free";
        config.build = "build-A";
        config.k1 = {true, 1, 2, 3};
        configure(config);
        _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
        _game->setCoopSession(_session);
        exchange();
    }
}

TEST_F(CoopDedicatedSessionTest, the_right_password_joins_and_the_first_guest_drives_the_story) {
    _password = "tableword";
    join(sampleGuest());
    tick();
    EXPECT_FALSE(_client.take<net::Welcome>(net::MessageType::Welcome).empty());
    ASSERT_NE(0u, guestId());
    ASSERT_NE(0u, clientId());
    EXPECT_EQ(clientId(), _session->storyDriver());
    EXPECT_TRUE(_session->dedicated());
    // The driver is told so.
    bool told = false;
    for (const auto &notice : _client.take<net::Notice>(net::MessageType::Notice)) {
        told = told || notice.text.find("lead the story") != std::string::npos;
    }
    EXPECT_TRUE(told);
}

TEST_F(CoopDedicatedSessionTest, without_a_guest_there_is_no_driver_and_it_returns_when_the_driver_leaves) {
    tick();
    EXPECT_EQ(0u, _session->storyDriver());
    _password = "tableword";
    join(sampleGuest());
    tick();
    EXPECT_NE(0u, _session->storyDriver());
    _client.transport->close(_client.connection, "bye");
    exchange();
    tick();
    EXPECT_EQ(0u, _session->storyDriver());
}

TEST_F(CoopDedicatedSessionTest, kick_removes_a_player_and_tells_them_why) {
    _password = "tableword";
    join(sampleGuest());
    tick();
    uint32_t id = clientId();
    ASSERT_NE(0u, id);
    EXPECT_FALSE(_session->kick(id + 100)) << "no such player";
    EXPECT_TRUE(_session->kick(id));
    exchange();
    auto rejects = _client.take<net::Reject>(net::MessageType::Reject);
    ASSERT_EQ(1u, rejects.size());
    EXPECT_EQ(net::RejectReason::Kicked, rejects[0].reason);
    _session->pollNetwork(); // the close lands on the next poll
    EXPECT_TRUE(_session->players().empty());
    tick(); // the driver is worked out once a tick
    EXPECT_EQ(0u, _session->storyDriver());
}

TEST_F(CoopDedicatedSessionTest, the_host_is_not_a_player_in_a_ready_check) {
    _password = "tableword";
    join(sampleGuest());
    tick();
    _client.inbox.clear();
    // A scripted exit with a guest present asks the guests alone.
    EXPECT_TRUE(_session->interceptTransition("test_next", "", 0, 0));
    tick();
    auto checks = _client.take<net::ReadyCheck>(net::MessageType::ReadyCheck);
    ASSERT_FALSE(checks.empty());
    EXPECT_EQ(net::ReadyState::Active, checks.back().state);
    ASSERT_EQ(1u, checks.back().players.size());
    EXPECT_EQ("Alice", checks.back().players[0].name);
}

class CoopFreeReadyTest : public CoopFreeSessionTest {};

TEST_F(CoopFreeReadyTest, the_ordinary_host_is_still_in_the_ready_check) {
    join(sampleGuest());
    tick();
    _client.inbox.clear();
    EXPECT_TRUE(_session->interceptTransition("test_next", "", 0, 0));
    tick();
    auto checks = _client.take<net::ReadyCheck>(net::MessageType::ReadyCheck);
    ASSERT_FALSE(checks.empty());
    EXPECT_EQ(2u, checks.back().players.size());
}

TEST(CoopHelloPassword, a_hello_without_one_is_byte_for_byte_the_old_message_and_one_with_one_round_trips) {
    net::Hello plain;
    plain.build = "b";
    plain.playerName = "P";
    auto bytes = net::encode(plain);
    net::Hello withPassword = plain;
    withPassword.password = "secret";
    auto longer = net::encode(withPassword);
    EXPECT_GT(longer.size(), bytes.size());
    // The old layout: the longer message starts with exactly the shorter one's bytes.
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), longer.begin()));
    net::Hello back;
    ASSERT_TRUE(net::decode(longer, back));
    EXPECT_EQ("secret", back.password);
    ASSERT_TRUE(net::decode(bytes, back));
    EXPECT_TRUE(back.password.empty());
    // Trailing junk after the password is still a malformed message.
    auto junk = longer;
    junk.push_back(7);
    EXPECT_FALSE(net::decode(junk, back));
    EXPECT_STREQ("bad_password", net::rejectReasonName(net::RejectReason::BadPassword));
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
    for (int type = static_cast<int>(net::CommandType::MoveTo); type <= static_cast<int>(net::CommandType::BenchClose); ++type) {
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

// Workbenches and lab stations for guests

namespace {

// A compiled script that only calls one routine, as a placeable's OnUsed that opens a screen.
std::shared_ptr<script::ScriptProgram> scriptCalling(Game &game, const std::string &name, const std::string &routine) {
    auto program = std::make_shared<script::ScriptProgram>(name);
    int index = game.routines().getIndexByName(routine);
    EXPECT_GE(index, 0) << routine;
    program->add(script::Instruction::newACTION(index, 0));
    return program;
}

} // namespace

TEST_F(CoopNetTest, workbench_and_lab_placeables_are_bench_uses_where_other_screens_stay_refused) {
    traskJoins();
    auto makeUsable = [&](const std::string &tag, const std::string &onUsed, float x) {
        auto placeable = _game->newPlaceable();
        placeable->setTag(tag);
        placeable->setPosition(glm::vec3(x, 3.0f, 0.0f));
        placeable->setUsable(true);
        placeable->setOnUsed(onUsed);
        _area->add(placeable);
        return placeable;
    };
    auto program = [&](const std::string &script, const std::string &routine) {
        EXPECT_CALL(_engine.resourceModule().scripts(), get(script)).WillRepeatedly(Return(scriptCalling(*_game, script, routine)));
    };
    program("bench_wb", "ShowUpgradeScreen");
    program("bench_store", "OpenStore");
    auto workbench = makeUsable("wb", "bench_wb", 1.0f);
    auto store = makeUsable("store_console", "bench_store", 3.0f);

    EXPECT_EQ(CoopBench::Workbench, coopScriptBench(*_game, "bench_wb"));
    EXPECT_EQ(CoopBench::None, coopScriptBench(*_game, "bench_store"));
    EXPECT_EQ(CoopBench::None, coopScriptBench(*_game, "bench_not_a_script"));

    net::Command use;
    use.type = net::CommandType::UseObject;
    use.actor = _trask->id();
    CoopCommandTargets targets;
    use.target = workbench->id();
    EXPECT_EQ(net::CommandReject::None, validateCoopCommand(*_game, use, _trask->id(), targets));
    EXPECT_EQ(CoopUse::Bench, targets.use);
    use.target = store->id();
    EXPECT_EQ(net::CommandReject::NotSupported, validateCoopCommand(*_game, use, _trask->id(), targets)) << "other screens are still the host's";
    executeCoopCommand(*_game, use, targets);
    use.target = workbench->id();
    ASSERT_EQ(net::CommandReject::None, validateCoopCommand(*_game, use, _trask->id(), targets));
    executeCoopCommand(*_game, use, targets);
    EXPECT_TRUE(_trask->actions().empty()) << "the session carries a bench use out, not executeCoopCommand";

    // K1's Taris workbench also has a conversation (its script talks once, then opens the
    // screen): it is still a bench use for a guest, not the host's conversation.
    auto talking = makeUsable("wb_talks", "bench_wb", 5.0f);
    talking->setConversation("wb_dialog");
    use.target = talking->id();
    EXPECT_EQ(net::CommandReject::None, validateCoopCommand(*_game, use, _trask->id(), targets));
    EXPECT_EQ(CoopUse::Bench, targets.use);
}

namespace {

// One command from the guest's end; the host answers it on its next tick.
net::CommandReject guestCommand(CoopSession &session, RawClient &client, uint32_t actor, net::CommandType type, uint32_t target,
                                int32_t param, const std::string &item, const std::function<void()> &tick) {
    static uint32_t seq = 1000;
    net::Command command;
    command.seq = ++seq;
    command.type = type;
    command.actor = actor;
    command.target = target;
    command.param = param;
    command.item = item;
    client.send(command);
    session.pollNetwork();
    client.inbox.clear();
    tick();
    auto results = client.take<net::CommandResult>(net::MessageType::CommandResult);
    return results.empty() ? net::CommandReject::NotSupported : results.back().reason;
}

} // namespace

TEST_F(CoopFreeSessionTest, a_guest_opens_a_workbench_through_its_own_view_and_every_bench_command_is_checked) {
    auto placeable = _game->newPlaceable();
    placeable->setTag("workbench");
    placeable->setUsable(true);
    placeable->setOnUsed("bench_wb");
    EXPECT_CALL(_engine.resourceModule().scripts(), get("bench_wb"))
        .WillRepeatedly(Return(scriptCalling(*_game, "bench_wb", "ShowUpgradeScreen")));
    join(sampleGuest());
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id);
    auto guest = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(guest);
    placeable->setPosition(guest->position());
    _area->add(placeable);
    auto run = [&](net::CommandType type, uint32_t target, int32_t param, const std::string &item) {
        // A few ticks per command keep the guest under the host's rate limit.
        return guestCommand(*_session, _client, id, type, target, param, item, [this]() {
            tick();
            tick();
            tick();
        });
    };

    // Nothing is open yet: every bench command is refused.
    for (auto type : {net::CommandType::BenchUpgrade, net::CommandType::BenchRemove, net::CommandType::BenchCraft,
                      net::CommandType::BenchBreak, net::CommandType::BenchClose}) {
        EXPECT_EQ(net::CommandReject::BenchClosed, run(type, placeable->id(), 1, "1")) << net::commandTypeName(type);
    }

    // Using the workbench is accepted, and the guest's own view comes back.
    _client.inbox.clear();
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, placeable->id(), 0, ""));
    std::vector<net::BenchView> views;
    for (int i = 0; i < 5 && views.empty(); ++i) {
        guest->update(0.1f); // the walk to the bench and the use are the guest's queued action
        tick();
        views = _client.take<net::BenchView>(net::MessageType::BenchView);
    }
    ASSERT_FALSE(views.empty()) << "the host sends the bench view once the guest is there";
    EXPECT_EQ(placeable->id(), views.back().bench);
    EXPECT_EQ(net::BenchKind::Workbench, views.back().kind);
    EXPECT_TRUE(views.back().open);

    // The wrong bench id, junk numbers and things the guest does not have.
    EXPECT_EQ(net::CommandReject::BenchClosed, run(net::CommandType::BenchBreak, placeable->id() + 1, 1, ""));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, placeable->id(), 424242, "515151"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, placeable->id(), 5, "12abc"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, placeable->id(), 0, "5"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, placeable->id(), -3, "5"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, placeable->id(), 5, ""));
    EXPECT_EQ(net::CommandReject::UpgradeIncompatible, run(net::CommandType::BenchRemove, placeable->id(), 5, "x"));
    EXPECT_EQ(net::CommandReject::UpgradeIncompatible, run(net::CommandType::BenchRemove, placeable->id(), 5, "9"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchRemove, placeable->id(), 424242, "0"));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchCraft, placeable->id(), 0, "no_such_recipe"));
    EXPECT_EQ(net::CommandReject::BenchClosed, run(net::CommandType::BenchCraft, placeable->id(), 7, "no_such_recipe"));
    EXPECT_EQ(net::CommandReject::NotBreakable, run(net::CommandType::BenchBreak, placeable->id(), 424242, ""));
    EXPECT_EQ(net::CommandReject::NotBreakable, run(net::CommandType::BenchBreak, placeable->id(), 0, ""));

    // Closing it closes the view; commands after that are refused again.
    _client.inbox.clear();
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::BenchClose, placeable->id(), 0, ""));
    auto closed = _client.take<net::BenchView>(net::MessageType::BenchView);
    ASSERT_FALSE(closed.empty());
    EXPECT_FALSE(closed.back().open);
    EXPECT_EQ(net::CommandReject::BenchClosed, run(net::CommandType::BenchBreak, placeable->id(), 1, ""));
}

TEST_F(CoopFreeSessionTest, a_guests_upgrades_and_crafting_material_survive_in_its_character) {
    auto guest = sampleGuest();
    guest.items[1].upgrades = 0x14;
    guest.items[1].modelVariation = 3;
    guest.components = 12;
    guest.chemicals = 34;
    net::GuestCharacter back;
    ASSERT_TRUE(net::decode(net::encode(guest), back));
    EXPECT_EQ(0x14u, back.items[1].upgrades);
    EXPECT_EQ(3, back.items[1].modelVariation);
    EXPECT_EQ(12, back.components);
    EXPECT_EQ(34, back.chemicals);
    join(guest);
    tick();
    auto creature = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(creature);
    _client.inbox.clear();
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    auto saved = _client.take<net::GuestCharacter>(net::MessageType::GuestCharacter);
    ASSERT_FALSE(saved.empty());
    EXPECT_EQ(12, saved.back().components) << "the material goes back to the guest's machine";
    EXPECT_EQ(34, saved.back().chemicals);
}

TEST_F(CoopStandInSessionTest, a_story_mode_guest_uses_a_workbench_with_the_partys_material) {
    auto placeable = _game->newPlaceable();
    placeable->setTag("workbench");
    placeable->setUsable(true);
    placeable->setOnUsed("bench_wb");
    EXPECT_CALL(_engine.resourceModule().scripts(), get("bench_wb"))
        .WillRepeatedly(Return(scriptCalling(*_game, "bench_wb", "ShowUpgradeScreen")));
    _game->party().setCraftingMaterials(7, 9);
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    placeable->setPosition(standIn->position());
    _area->add(placeable);
    auto run = [&](net::CommandType type, uint32_t target, int32_t param, const std::string &item) {
        return guestCommand(*_session, _client, id, type, target, param, item, [this]() {
            tick();
            tick();
            tick();
        });
    };
    EXPECT_EQ(net::CommandReject::BenchClosed, run(net::CommandType::BenchBreak, placeable->id(), 1, ""));
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, placeable->id(), 0, ""));
    std::vector<net::BenchView> views;
    for (int i = 0; i < 5 && views.empty(); ++i) {
        standIn->update(0.1f);
        tick();
        views = _client.take<net::BenchView>(net::MessageType::BenchView);
    }
    ASSERT_FALSE(views.empty());
    EXPECT_EQ(7, views.back().components) << "the party's material, not a guest's own";
    EXPECT_EQ(9, views.back().chemicals);
    EXPECT_EQ(net::CommandReject::NotBreakable, run(net::CommandType::BenchBreak, placeable->id(), 424242, ""));
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchCraft, placeable->id(), 0, "no_such_recipe"));
    EXPECT_EQ(7u, _game->party().persistedState().itemComponent) << "refusals spend nothing";
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::BenchClose, placeable->id(), 0, ""));
}

// Guest minigames

namespace {

class QuietMiniGameHost : public IMiniGameHost {
public:
    void runScript(const std::string &, uint32_t) override {}
    void playMiniGameAnimation(const MiniGameActor &, const std::string &, bool, bool, bool) override {}
    void onMiniGameActorDied(const MiniGameActor &) override {}
};

MinigameSpec guestRaceSpec(MinigameType type) {
    MinigameSpec spec;
    spec.type = type;
    spec.movementPerSec = 100.0f;
    spec.lateralAccel = 300.0f;
    spec.player.models = {{"v_superbike", true}};
    spec.player.hitPoints = 100;
    spec.player.maxHitPoints = 100;
    spec.player.sphereRadius = 3.0f;
    return spec;
}

// guestCommand() (above, with the workbench tests) sends one command from the guest's end.


// Raw MiniGameControl bytes carry the reader's checks; this sends one the way a client does.
void sendControl(RawClient &client, uint32_t seq, float steer, bool fire = false) {
    net::MiniGameControl control;
    control.seq = seq;
    control.steer = steer;
    control.fire = fire;
    client.send(control);
}

} // namespace

TEST_F(CoopFreeSessionTest, a_guest_starts_a_race_drives_it_and_the_story_is_left_alone) {
    // An NPC whose conversation starts a race module.
    auto racer = addCreature("tar_racer", glm::vec3(1.0f, 1.0f, 0.0f), Faction::Friendly1);
    racer->setConversation("race_dlg");
    auto dialog = std::make_shared<Dialog>();
    dialog->startEntries.push_back(dialogLink(0));
    dialog->entries.resize(1);
    dialog->entries[0].script = "a_startrace";
    EXPECT_CALL(_engine.resourceModule().dialogs(), get("race_dlg")).WillRepeatedly(Return(dialog));
    auto program = std::make_shared<script::ScriptProgram>("a_startrace");
    program->add(script::Instruction::newCONSTS("tar_m03mg"));
    program->add(script::Instruction::newACTION(_game->routines().getIndexByName("StartNewModule"), 8));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("a_startrace")).WillRepeatedly(Return(program));

    auto record = sampleGuest();
    record.credits = 40;
    join(record);
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id);
    auto run = [&](net::CommandType type, uint32_t target, int32_t param) {
        return guestCommand(*_session, _client, id, type, target, param, "", [this]() {
            tick();
            tick();
            tick();
        });
    };

    // Using the NPC gets a prompt; the answer starts the race module on the host.
    _client.inbox.clear();
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, racer->id(), 0));
    auto offers = _client.take<net::Offer>(net::MessageType::Offer);
    ASSERT_FALSE(offers.empty());
    EXPECT_EQ(net::OfferKind::MiniGame, offers.back().kind);
    _game->setGlobalBoolean("TAR_RACE_WON", false);
    _game->party().giveGold(1000);
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::OfferAnswer, racer->id(), 1));
    EXPECT_EQ("tar_m03mg", TestGameModule::scheduledTransition(*_game).first);

    // The module loads: the game tells the session, which gives the guest the controls.
    _session->onMiniGameStarted(MinigameType::SwoopRace, "tar_m03mg");
    EXPECT_TRUE(_session->miniGameRemoteOwned());
    EXPECT_EQ("Alice", _session->miniGame().owner());
    EXPECT_TRUE(_session->miniGame().sandboxed());
    auto states = _client.take<net::MiniGameState>(net::MessageType::MiniGameState);
    (void)states;

    QuietMiniGameHost host;
    MiniGameSession simulation(host);
    simulation.load(guestRaceSpec(MinigameType::SwoopRace), MiniGameTrack(), {}, {});
    simulation.setObjectId(0, 77);

    // Its input arrives as messages and lands in the seam on the host's tick.
    sendControl(_client, 1, 0.5f, true);
    _session->pollNetwork();
    MiniGameInput input;
    _session->applyMiniGameInput(input);
    EXPECT_FLOAT_EQ(0.5f, input.steer);
    EXPECT_TRUE(input.fire);
    sendControl(_client, 1, -1.0f); // a replay of the same number is ignored
    _session->pollNetwork();
    _session->applyMiniGameInput(input);
    EXPECT_FLOAT_EQ(0.5f, input.steer);

    // The minigame's scripts change the story while it runs.
    _game->setGlobalBoolean("TAR_RACE_WON", true);
    _game->party().giveGold(5000);

    _client.inbox.clear();
    TestGameModule::advanceTick(*_game);
    _session->onMiniGameEnded(simulation);
    EXPECT_FALSE(_session->miniGameRemoteOwned());
    EXPECT_FALSE(_game->getGlobalBoolean("TAR_RACE_WON")) << "a guest's race does not win the story's race";
    EXPECT_EQ(1000, _game->party().gold()) << "and does not pay the party";
    auto final = _client.take<net::MiniGameState>(net::MessageType::MiniGameState);
    EXPECT_TRUE(final.empty()) << "the transport delivers on the next poll";
    exchange();
    final = _client.take<net::MiniGameState>(net::MessageType::MiniGameState);
    ASSERT_FALSE(final.empty());
    EXPECT_FALSE(final.back().active);
    EXPECT_TRUE(final.back().finished);
    EXPECT_EQ(100, final.back().prize);
    auto status = _session->statusJson();
    EXPECT_EQ(140, status.at("clients").as_array()[0].as_object().at("guest").as_object().at("credits").to_number<int64_t>())
        << "Free Mode pays the guest's own credits";
}

TEST_F(CoopFreeSessionTest, minigames_run_one_at_a_time_and_the_owner_can_let_go) {
    auto racer = addCreature("tar_racer", glm::vec3(1.0f, 1.0f, 0.0f), Faction::Friendly1);
    racer->setConversation("race_dlg");
    auto dialog = std::make_shared<Dialog>();
    dialog->startEntries.push_back(dialogLink(0));
    dialog->entries.resize(1);
    dialog->entries[0].script = "a_startrace";
    EXPECT_CALL(_engine.resourceModule().dialogs(), get("race_dlg")).WillRepeatedly(Return(dialog));
    auto program = std::make_shared<script::ScriptProgram>("a_startrace");
    program->add(script::Instruction::newCONSTS("tar_m03mg"));
    program->add(script::Instruction::newACTION(_game->routines().getIndexByName("StartNewModule"), 8));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("a_startrace")).WillRepeatedly(Return(program));
    join(sampleGuest());
    tick();
    uint32_t id = guestId();
    ASSERT_NE(0u, id);
    auto run = [&](net::CommandType type, uint32_t target, int32_t param) {
        return guestCommand(*_session, _client, id, type, target, param, "", [this]() {
            tick();
            tick();
            tick();
        });
    };
    QuietMiniGameHost host;
    MiniGameSession simulation(host);
    simulation.load(guestRaceSpec(MinigameType::SwoopRace), MiniGameTrack(), {}, {});
    simulation.setObjectId(0, 79);

    // The host is in a minigame of its own: a guest's request has to wait.
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, racer->id(), 0));
    _session->onMiniGameStarted(MinigameType::SwoopRace, "tar_m03mg");
    EXPECT_FALSE(_session->miniGameRemoteOwned());
    EXPECT_EQ(net::CommandReject::MiniGameBusy, run(net::CommandType::OfferAnswer, racer->id(), 1)) << "one minigame at a time";
    EXPECT_EQ(net::CommandReject::NoOffer, run(net::CommandType::OfferAnswer, racer->id(), 1)) << "the prompt is spent";
    _session->onMiniGameEnded(simulation);
    EXPECT_FALSE(_session->miniGame().running());

    // With the host free, the answer schedules the race once.
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, racer->id(), 0));
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::OfferAnswer, racer->id(), 1));
    EXPECT_EQ("tar_m03mg", TestGameModule::scheduledTransition(*_game).first);

    // Not the owner of anything yet: letting go is refused.
    EXPECT_EQ(net::CommandReject::NotYourMiniGame, run(net::CommandType::MiniGameQuit, 0, 0));
    _session->onMiniGameStarted(MinigameType::SwoopRace, "tar_m03mg");
    ASSERT_TRUE(_session->miniGameRemoteOwned());
    EXPECT_EQ(net::CommandReject::None, run(net::CommandType::MiniGameQuit, 0, 0));
    EXPECT_FALSE(_session->miniGameRemoteOwned()) << "the controls are the host's again";
    EXPECT_EQ(net::CommandReject::NotYourMiniGame, run(net::CommandType::MiniGameQuit, 0, 0));
}

TEST_F(CoopFreeSessionTest, a_turret_the_host_hands_over_is_steered_by_the_guest_and_pays_nothing) {
    join(sampleGuest());
    tick();
    ASSERT_NE(0u, guestId());
    EXPECT_FALSE(_session->handMiniGame(1, true)) << "nothing is running";
    _session->onMiniGameStarted(MinigameType::Turret, "m12ab");
    EXPECT_TRUE(_session->miniGame().running());
    EXPECT_FALSE(_session->miniGameRemoteOwned()) << "the host plays its own turret until it hands it over";
    EXPECT_FALSE(_session->handMiniGame(99, true)) << "no such player";

    uint32_t clientId = 0;
    auto status = _session->statusJson();
    for (const auto &client : status.at("clients").as_array()) {
        clientId = static_cast<uint32_t>(client.as_object().at("id").to_number<int64_t>());
    }
    ASSERT_NE(0u, clientId);
    EXPECT_TRUE(_session->handMiniGame(clientId, true));
    EXPECT_TRUE(_session->miniGameRemoteOwned());
    EXPECT_FALSE(_session->miniGame().sandboxed()) << "the host's own story minigame keeps its effects";

    net::MiniGameControl aim;
    aim.seq = 1;
    aim.pitchRate = 1.0f;
    aim.yawDelta = 12.0f;
    aim.fire = true;
    _client.send(aim);
    _session->pollNetwork();
    MiniGameInput input;
    _session->applyMiniGameInput(input);
    EXPECT_FLOAT_EQ(1.0f, input.pitchRate);
    EXPECT_FLOAT_EQ(12.0f, input.yawDelta);
    EXPECT_TRUE(input.fire);

    QuietMiniGameHost host;
    MiniGameSession simulation(host);
    simulation.load(guestRaceSpec(MinigameType::Turret), MiniGameTrack(), {}, {});
    simulation.setObjectId(0, 78);
    _session->onMiniGameEnded(simulation);
    exchange();
    auto final = _client.take<net::MiniGameState>(net::MessageType::MiniGameState);
    ASSERT_FALSE(final.empty());
    EXPECT_EQ(0, final.back().prize);
    EXPECT_EQ(net::MiniGameKind::Turret, final.back().kind);

    EXPECT_TRUE(_session->handMiniGame(0, false) == false) << "taking back what is not running";
}

// Host authority (task 23): a client that names what it does not own, an index out of range or a
// stale token gets a refusal and the world is exactly as it was.

namespace {

// Everything a refused command must leave alone: where creatures are and what they do, what they
// hold and wear, what containers hold, the party's credits.
std::string worldFingerprint(Game &game, const std::shared_ptr<Area> &area) {
    std::ostringstream out;
    for (const auto &object : area->objects()) {
        out << object->id() << ':' << object->tag() << '@' << object->position().x << ',' << object->position().y << ','
            << object->position().z << ' ' << object->items().size() << 'i';
        for (const auto &item : object->items()) {
            out << '[' << item->id() << ':' << item->stackSize() << ']';
        }
        if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
            out << " f" << creature->getFacing() << " hp" << creature->currentHitPoints() << " a" << creature->actions().size();
            for (const auto &[slot, item] : creature->equipment()) {
                out << " e" << slot << '=' << (item ? item->id() : 0);
            }
        }
        out << '\n';
    }
    out << "gold " << game.party().gold() << " xp " << game.party().xp() << " members " << game.party().getSize();
    return out.str();
}

struct HostileCommand {
    const char *what;
    net::CommandType type;
    uint32_t target;
    int32_t param;
    std::string item;
};

} // namespace

TEST_F(CoopHostileTest, commands_naming_what_a_player_does_not_own_or_an_index_out_of_range_change_nothing) {
    bothJoinTheParty();
    join(1, "Alice");
    join(2, "Bob");
    net::SlotRequest slot;
    say(1, slot);
    say(2, slot);
    exchange();
    tick();
    auto alice = lastSlot(1);
    ASSERT_TRUE(alice);
    ASSERT_EQ(net::SlotStatus::Assigned, alice->status);

    // Things Alice has no claim to: a hostile's belongings, the contents of a container she has not
    // opened, a locked container, a door, a trap trigger.
    auto loot = _game->newItem();
    loot->setTag("hostile_loot");
    _sith->addItem(loot);
    auto chest = _game->newPlaceable();
    chest->setTag("end_chest");
    chest->setHasInventory(true);
    chest->setPosition(glm::vec3(4.0f, 4.0f, 0.0f));
    auto treasure = _game->newItem();
    treasure->setTag("chest_treasure");
    chest->addItem(treasure);
    _area->add(chest);
    uint32_t lootId = loot->id();
    uint32_t treasureId = treasure->id();
    auto before = worldFingerprint(*_game, _area);

    const uint32_t kNobody = 424242;
    std::vector<HostileCommand> hostile {
        {"equip a hostile's item", net::CommandType::Equip, 0, static_cast<int32_t>(lootId), "4"},
        {"equip an item that does not exist", net::CommandType::Equip, 0, 424242, "4"},
        {"equip into a slot that does not exist", net::CommandType::Equip, 0, static_cast<int32_t>(lootId), "999"},
        {"equip into a negative slot", net::CommandType::Equip, 0, static_cast<int32_t>(lootId), "-1"},
        {"equip with a slot that is no number", net::CommandType::Equip, 0, static_cast<int32_t>(lootId), "abc"},
        {"equip with no slot at all", net::CommandType::Equip, 0, static_cast<int32_t>(lootId), ""},
        {"equip the largest id", net::CommandType::Equip, 0, INT32_MAX, "4"},
        {"equip the most negative id", net::CommandType::Equip, 0, INT32_MIN, "4"},
        {"use a hostile's item by id", net::CommandType::UseItem, _sith->id(), static_cast<int32_t>(lootId), ""},
        {"use an item by a tag nobody holds", net::CommandType::UseItem, _sith->id(), 0, "hostile_loot"},
        {"use a negative item id", net::CommandType::UseItem, _sith->id(), -5, ""},
        {"take from a container never opened", net::CommandType::TakeItem, chest->id(), static_cast<int32_t>(treasureId), ""},
        {"take everything from a container never opened", net::CommandType::TakeItem, chest->id(), 0, ""},
        {"take from a container that does not exist", net::CommandType::TakeItem, kNobody, 1, ""},
        {"close a container never opened", net::CommandType::CloseContainer, chest->id(), 0, ""},
        {"pick a reply with no terminal", net::CommandType::PickReply, 0, 0, ""},
        {"pick a reply at a negative row", net::CommandType::PickReply, 0, -1, ""},
        {"pick a reply at the largest row", net::CommandType::PickReply, 0, INT32_MAX, ""},
        {"buy from a store that is not open", net::CommandType::StoreBuy, kNobody, 1, ""},
        {"sell to a store that is not open", net::CommandType::StoreSell, kNobody, static_cast<int32_t>(lootId), ""},
        {"answer an offer nobody made", net::CommandType::OfferAnswer, _sith->id(), 1, ""},
        {"report a Pazaak result for no game", net::CommandType::PazaakResult, 0, 1000000, ""},
        {"report a negative Pazaak result", net::CommandType::PazaakResult, 0, INT32_MIN, ""},
        {"lay a mine that is not hers", net::CommandType::SetMine, 0, static_cast<int32_t>(lootId), ""},
        {"disarm a door", net::CommandType::DisarmTrap, _door->id(), 0, ""},
        {"disarm what does not exist", net::CommandType::DisarmTrap, kNobody, 0, ""},
        {"use a feat that does not exist", net::CommandType::UseFeat, _sith->id(), -1, ""},
        {"use the largest feat", net::CommandType::UseFeat, _sith->id(), INT32_MAX, ""},
        {"use the smallest feat", net::CommandType::UseFeat, _sith->id(), INT32_MIN, ""},
        {"use a power that does not exist", net::CommandType::UsePower, _sith->id(), 99999, ""},
        {"use a negative power", net::CommandType::UsePower, _sith->id(), -7, ""},
        {"use a skill that does not exist", net::CommandType::UseSkill, _door->id(), 99, ""},
        {"use a negative skill", net::CommandType::UseSkill, _door->id(), -1, ""},
        {"level up with garbage", net::CommandType::LevelUp, 0, 0, "zzzz|||"},
        {"level up with the longest text", net::CommandType::LevelUp, 0, 0, std::string(64, '|')},
        {"level up with a negative class", net::CommandType::LevelUp, 0, 0, "c-9999|a-1|s|f|p"},
        {"attack what does not exist", net::CommandType::Attack, kNobody, 0, ""},
        {"open a door that does not exist", net::CommandType::OpenDoor, kNobody, 0, ""},
        {"use an object that does not exist", net::CommandType::UseObject, kNobody, 0, ""},
        {"use a hostile as an object far away", net::CommandType::UseObject, _farSith->id(), 0, ""},
        {"move to a point in another galaxy", net::CommandType::MoveTo, 0, 0, ""},
    };
    uint32_t seq = 1000;
    size_t sent = 0;
    for (const auto &entry : hostile) {
        net::Command command;
        command.seq = ++seq;
        command.type = entry.type;
        command.actor = alice->creatureId;
        command.target = entry.target;
        command.point = entry.type == net::CommandType::MoveTo ? net::Vec3 {99000.0f, -99000.0f, 0.0f} : net::Vec3 {};
        command.param = entry.param;
        command.item = entry.item;
        say(1, command);
        ++sent;
        // A few ticks apart, so the host's per-second command limit is not what refuses them.
        exchange();
        tick();
        tick();
        tick();
    }
    auto results = _fake->received<net::CommandResult>(1, net::MessageType::CommandResult);
    std::map<uint32_t, net::CommandResult> bySeq;
    for (const auto &result : results) {
        bySeq[result.seq] = result;
    }
    ASSERT_EQ(sent, hostile.size());
    for (size_t i = 0; i < hostile.size(); ++i) {
        auto found = bySeq.find(static_cast<uint32_t>(1001 + i));
        ASSERT_NE(bySeq.end(), found) << hostile[i].what << ": no answer";
        EXPECT_FALSE(found->second.accepted) << hostile[i].what << " was accepted";
    }
    EXPECT_EQ(before, worldFingerprint(*_game, _area)) << "a refused command changed the world";
    EXPECT_EQ(1u, _sith->items().size());
    EXPECT_EQ(1u, chest->items().size());
    EXPECT_TRUE(_trask->actions().empty());
    EXPECT_FALSE(_session->players().empty());
    EXPECT_TRUE(_session->isPlayerControlled(alice->creatureId)) << "and Alice is still where she was";
}

TEST_F(CoopFreeHostileTest, a_guest_cannot_use_another_guests_things_open_a_store_it_is_not_at_or_settle_a_game_it_never_played) {
    startFree(false);
    joinFree(1, "Alice", "Alice");
    joinFree(2, "Bob", "Bob");
    tick();
    auto idOf = [&](const char *name) {
        auto status = clientStatus(name);
        return status.empty() || !status.contains("guest") ? 0u : static_cast<uint32_t>(status.at("guest").as_object().at("id").to_number<int64_t>());
    };
    uint32_t aliceId = idOf("Alice");
    uint32_t bobId = idOf("Bob");
    ASSERT_NE(0u, aliceId);
    ASSERT_NE(0u, bobId);
    auto bob = _game->getObjectById<Creature>(bobId);
    ASSERT_TRUE(bob);
    // Bob's bag, and something of the host's.
    auto bobsItem = _game->newItem();
    bobsItem->setTag("bobs_item");
    bob->addItem(bobsItem);
    auto hostsItem = _game->newItem();
    hostsItem->setTag("hosts_item");
    _player->addItem(hostsItem);
    auto credits = [&](const char *name) {
        return clientStatus(name).at("guest").as_object().at("credits").to_number<int64_t>();
    };
    int64_t aliceCredits = credits("Alice");
    int64_t bobCredits = credits("Bob");
    auto before = worldFingerprint(*_game, _area);

    std::vector<HostileCommand> hostile {
        {"equip Bob's item", net::CommandType::Equip, 0, static_cast<int32_t>(bobsItem->id()), "4"},
        {"equip the host's item", net::CommandType::Equip, 0, static_cast<int32_t>(hostsItem->id()), "4"},
        {"use Bob's item", net::CommandType::UseItem, aliceId, static_cast<int32_t>(bobsItem->id()), ""},
        {"use the host's item", net::CommandType::UseItem, aliceId, static_cast<int32_t>(hostsItem->id()), ""},
        {"buy at a store that is not open", net::CommandType::StoreBuy, 77, static_cast<int32_t>(bobsItem->id()), ""},
        {"sell Bob's item at a store that is not open", net::CommandType::StoreSell, 77, static_cast<int32_t>(bobsItem->id()), ""},
        {"close a store that is not open", net::CommandType::StoreClose, 77, 0, ""},
        {"answer an offer nobody made", net::CommandType::OfferAnswer, 77, 1, ""},
        {"win at a Pazaak game never started", net::CommandType::PazaakResult, 0, 500000, ""},
        {"lose at a Pazaak game never started", net::CommandType::PazaakResult, 0, -100, ""},
        {"settle Pazaak for an absurd amount", net::CommandType::PazaakResult, 0, INT32_MAX, ""},
        {"pick a terminal reply with none handed over", net::CommandType::PickReply, 0, 0, ""},
        {"lay Bob's item as a mine", net::CommandType::SetMine, 0, static_cast<int32_t>(bobsItem->id()), ""},
    };
    uint32_t seq = 500;
    for (const auto &entry : hostile) {
        net::Command command;
        command.seq = ++seq;
        command.type = entry.type;
        command.actor = aliceId;
        command.target = entry.target;
        command.param = entry.param;
        command.item = entry.item;
        say(1, command);
        exchange();
        tick();
        tick();
        tick();
    }
    std::map<uint32_t, net::CommandResult> bySeq;
    for (const auto &result : _fake->received<net::CommandResult>(1, net::MessageType::CommandResult)) {
        bySeq[result.seq] = result;
    }
    for (size_t i = 0; i < hostile.size(); ++i) {
        auto found = bySeq.find(static_cast<uint32_t>(501 + i));
        ASSERT_NE(bySeq.end(), found) << hostile[i].what << ": no answer";
        EXPECT_FALSE(found->second.accepted) << hostile[i].what << " was accepted";
    }
    EXPECT_EQ(before, worldFingerprint(*_game, _area));
    EXPECT_EQ(aliceCredits, credits("Alice"));
    EXPECT_EQ(bobCredits, credits("Bob"));
    EXPECT_TRUE(bob->getItemByTag("bobs_item"));
    EXPECT_TRUE(_player->getItemByTag("hosts_item"));
}

namespace {

// The random command tests run a modest number of rounds by default; the environment lengthens a hunt:
// REONE_COOP_FUZZ_ROUNDS=50000 REONE_COOP_FUZZ_SEED=7 build/bin/tests --gtest_filter='*random_commands*:*thousands_of_random*'
int fuzzRounds() {
    const char *value = std::getenv("REONE_COOP_FUZZ_ROUNDS");
    return value ? std::max(1, std::atoi(value)) : 1500;
}

uint32_t fuzzSeed(uint32_t fallback) {
    const char *value = std::getenv("REONE_COOP_FUZZ_SEED");
    return value ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : fallback;
}

// A command with every field drawn from what is worth trying: real object ids, someone else's, nothing,
// the extremes of an integer, strings of every shape.
net::Command randomCommand(std::mt19937 &random, uint32_t seq, uint32_t actor, const std::vector<uint32_t> &ids) {
    auto pick = [&](size_t bound) { return static_cast<size_t>(random() % bound); };
    auto anId = [&]() -> uint32_t {
        switch (pick(5)) {
        case 0:
            return 0;
        case 1:
            return UINT32_MAX;
        case 2:
            return static_cast<uint32_t>(random());
        default:
            return ids[pick(ids.size())];
        }
    };
    static const int32_t kInts[] {0, 1, -1, 2, 3, 7, 8, 9, 15, 30, 31, 99, 255, 256, 1000, -1000, 65535, 65536, INT32_MAX, INT32_MIN, INT32_MAX - 1, INT32_MIN + 1};
    static const char *const kItems[] {"", "0", "1", "-1", "4", "999", "abc", "g_i_medeqpmnt01", "end_trask", "hostile_loot", "c2|a-1|s|f|p",
                                       "c-9999|a-1|s|f|p", "||||", "c99|a99|s99|f99|p99", "99999999999999999999", " 12", "12 ", "0x10", "\xff\xfe"};
    net::Command command;
    command.seq = seq;
    std::vector<net::CommandType> types;
    for (int type = 0; type < 256; ++type) {
        if (std::string(net::commandTypeName(static_cast<net::CommandType>(type))) != "unknown") {
            types.push_back(static_cast<net::CommandType>(type));
        }
    }
    command.type = types[pick(types.size())];
    command.actor = pick(4) == 0 ? anId() : actor;
    command.target = anId();
    command.point = {pick(3) == 0 ? 1e5f : static_cast<float>(random() % 200) - 100.0f, pick(3) == 0 ? -1e5f : static_cast<float>(random() % 200) - 100.0f,
                     pick(4) == 0 ? 99999.0f : 0.0f};
    command.run = random() % 2 == 0;
    command.param = pick(3) == 0 ? static_cast<int32_t>(anId()) : kInts[pick(sizeof(kInts) / sizeof(kInts[0]))];
    command.item = pick(8) == 0 ? std::string(1 + pick(60), static_cast<char>('a' + pick(26))) : kItems[pick(sizeof(kItems) / sizeof(kItems[0]))];
    return command;
}

} // namespace

TEST_F(CoopHostileTest, thousands_of_random_commands_never_crash_the_host_and_a_refused_one_changes_nothing) {
    bothJoinTheParty();
    join(1, "Alice");
    join(2, "Bob");
    net::SlotRequest slot;
    say(1, slot);
    say(2, slot);
    exchange();
    tick();
    auto alice = lastSlot(1);
    ASSERT_TRUE(alice);
    ASSERT_EQ(net::SlotStatus::Assigned, alice->status);

    auto loot = _game->newItem();
    loot->setTag("hostile_loot");
    _sith->addItem(loot);
    auto chest = _game->newPlaceable();
    chest->setTag("end_chest");
    chest->setHasInventory(true);
    chest->setPosition(glm::vec3(4.0f, 4.0f, 0.0f));
    chest->addItem(_game->newItem());
    _area->add(chest);
    std::vector<uint32_t> ids {alice->creatureId, _player->id(), _trask->id(), _mission->id(), _sith->id(), _farSith->id(), _door->id(), chest->id(), loot->id()};

    std::mt19937 random(fuzzSeed(0xA11CE));
    auto before = worldFingerprint(*_game, _area);
    uint32_t seq = 10000;
    int refused = 0;
    int accepted = 0;
    for (int round = 0; round < fuzzRounds(); ++round) {
        auto command = randomCommand(random, ++seq, alice->creatureId, ids);
        // Orders that ask the session itself for something (pausing, readying, picking a slot) change the
        // session's books, not the world; the world is what is compared.
        say(1, command);
        exchange();
        tick();
        tick();
        tick();
        const net::CommandResult *answer = nullptr;
        auto results = _fake->received<net::CommandResult>(1, net::MessageType::CommandResult);
        for (const auto &result : results) {
            if (result.seq == seq) {
                answer = &result;
            }
        }
        ASSERT_NE(nullptr, answer) << "command " << seq << " (" << net::commandTypeName(command.type) << ") got no answer";
        auto after = worldFingerprint(*_game, _area);
        if (answer->accepted) {
            ++accepted;
            before = after;
        } else {
            ++refused;
            ASSERT_EQ(before, after) << "a refused " << net::commandTypeName(command.type) << " (" << net::commandRejectName(answer->reason)
                                     << ") changed the world; param " << command.param << " item '" << command.item << "'";
        }
        ASSERT_FALSE(_fake->wasClosedByHost(1)) << "a valid command ended the connection";
    }
    EXPECT_GT(refused, fuzzRounds() / 3);
    EXPECT_GT(accepted, 0) << "the generator should hit some real orders too";
}

TEST_F(CoopFreeHostileTest, random_commands_from_a_guest_never_crash_the_host_and_a_refused_one_changes_nothing) {
    startFree(false);
    joinFree(1, "Alice", "Alice");
    tick();
    auto status = clientStatus("Alice");
    ASSERT_TRUE(status.contains("guest"));
    uint32_t aliceId = static_cast<uint32_t>(status.at("guest").as_object().at("id").to_number<int64_t>());
    ASSERT_NE(0u, aliceId);
    auto hostsItem = _game->newItem();
    hostsItem->setTag("hosts_item");
    _player->addItem(hostsItem);
    auto chest = _game->newPlaceable();
    chest->setTag("end_chest");
    chest->setHasInventory(true);
    chest->addItem(_game->newItem());
    _area->add(chest);
    std::vector<uint32_t> ids {aliceId, _player->id(), _trask->id(), _sith->id(), _farSith->id(), _door->id(), chest->id(), hostsItem->id()};
    auto credits = [&]() { return clientStatus("Alice").at("guest").as_object().at("credits").to_number<int64_t>(); };

    std::mt19937 random(fuzzSeed(0xB0B));
    auto before = worldFingerprint(*_game, _area);
    int64_t money = credits();
    uint32_t seq = 20000;
    int refused = 0;
    for (int round = 0; round < fuzzRounds(); ++round) {
        auto command = randomCommand(random, ++seq, aliceId, ids);
        say(1, command);
        exchange();
        tick();
        tick();
        tick();
        const net::CommandResult *answer = nullptr;
        auto results = _fake->received<net::CommandResult>(1, net::MessageType::CommandResult);
        for (const auto &result : results) {
            if (result.seq == seq) {
                answer = &result;
            }
        }
        ASSERT_NE(nullptr, answer) << "command " << seq << " (" << net::commandTypeName(command.type) << ") got no answer";
        auto after = worldFingerprint(*_game, _area);
        if (answer->accepted) {
            before = after;
            money = credits();
        } else {
            ++refused;
            ASSERT_EQ(before, after) << "a refused " << net::commandTypeName(command.type) << " (" << net::commandRejectName(answer->reason)
                                     << ") changed the world; param " << command.param << " item '" << command.item << "'";
            ASSERT_EQ(money, credits()) << "a refused " << net::commandTypeName(command.type) << " moved credits";
        }
        ASSERT_FALSE(_fake->wasClosedByHost(1)) << "a valid command ended the connection";
    }
    EXPECT_GT(refused, fuzzRounds() / 3);
}

// Factions (docs/guest_scenes.md): what a guest needs to see enemies as the host does.

TEST_F(CoopSessionTest, each_creature_s_faction_travels_and_a_change_travels_alone) {
    _client.send(hello());
    exchange();
    _client.inbox.clear();
    tick();
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    std::map<uint32_t, uint8_t> factions;
    for (auto &record : snapshots[0].entities) {
        if (record.fields & net::kFieldFaction) {
            factions[record.state.id] = record.state.faction;
        }
    }
    EXPECT_EQ(static_cast<uint8_t>(Faction::Player), factions[_player->id()]);
    EXPECT_EQ(static_cast<uint8_t>(Faction::Friendly1), factions[_trask->id()]);
    EXPECT_EQ(static_cast<uint8_t>(Faction::Hostile1), factions[_sith->id()]);
    EXPECT_EQ(net::kNoFaction, factions.count(_door->id()) ? factions[_door->id()] : net::kNoFaction);

    // A companion joining is the player faction to the reputation rules.
    traskJoins();
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    bool trask = false;
    for (auto &record : snapshots[0].entities) {
        if (record.state.id == _trask->id() && (record.fields & net::kFieldFaction)) {
            trask = true;
            EXPECT_EQ(static_cast<uint8_t>(Faction::Player), record.state.faction);
        }
    }
    EXPECT_TRUE(trask);

    // ChangeToStandardFaction mid-scene: only the faction goes.
    _sith->setFaction(Faction::Hostile2);
    _client.inbox.clear();
    tick();
    snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_EQ(1u, snapshots.size());
    ASSERT_EQ(1u, snapshots[0].entities.size());
    EXPECT_EQ(_sith->id(), snapshots[0].entities[0].state.id);
    EXPECT_EQ(net::kFieldFaction, snapshots[0].entities[0].fields);
    EXPECT_EQ(static_cast<uint8_t>(Faction::Hostile2), snapshots[0].entities[0].state.faction);
}

TEST_F(CoopSessionTest, the_reputation_table_goes_to_each_client_when_it_changes) {
    auto &reputes = _engine.gameModule().reputes();
    IReputes::State state;
    state.factions.resize(3);
    state.labels.resize(3);
    state.values = {{100, 0, 50}, {0, 100, 50}, {50, 50, 100}};
    ON_CALL(reputes, state()).WillByDefault(Invoke([&state]() { return state; }));
    _client.send(hello());
    exchange();
    _client.inbox.clear();
    tick();
    auto tables = _client.take<net::ReputationTable>(net::MessageType::ReputationTable);
    ASSERT_EQ(1u, tables.size());
    ASSERT_EQ(3u, tables[0].values.size());
    EXPECT_EQ(0, tables[0].values[0][1]);
    EXPECT_EQ(50, tables[0].values[2][1]);

    // Unchanged: nothing more.
    _client.inbox.clear();
    for (int i = 0; i < 12; ++i) {
        tick();
    }
    EXPECT_TRUE(_client.take<net::ReputationTable>(net::MessageType::ReputationTable).empty());

    // AdjustReputation on the host: the new table follows within a check period.
    state.values[2][0] = 5;
    _client.inbox.clear();
    for (int i = 0; i < 12; ++i) {
        tick();
    }
    tables = _client.take<net::ReputationTable>(net::MessageType::ReputationTable);
    ASSERT_EQ(1u, tables.size());
    EXPECT_EQ(5, tables[0].values[2][0]);
}

// Guest scenes (docs/guest_scenes.md)

TEST_F(CoopFreeSessionTest, the_leader_s_scripted_jump_in_a_scene_takes_the_guest_along) {
    join(sampleGuest());
    tick();
    auto guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    // k_pend_door18: fade to black, then jump the party into the next room.
    _game->globalFade().request(GlobalFade::Direction::Out, 0.0f, 0.0f, glm::vec3(0.0f), GlobalFade::Source::Script);
    _game->globalFade().update(0.0f);
    glm::vec3 from = _player->position();
    glm::vec3 to(40.0f, 20.0f, 0.0f);
    _player->setPosition(to);
    _session->onScriptedJump(*_player, from, to);
    EXPECT_EQ(1u, _session->guestSceneJumps());
    float distance = glm::distance(glm::vec2(guest->position()), glm::vec2(to));
    EXPECT_LT(distance, 6.0f) << "beside the leader's destination";
    EXPECT_GT(distance, 0.1f) << "on a free spot, not on the leader";
    EXPECT_EQ(_player->getFacing(), guest->getFacing());
}

TEST_F(CoopFreeSessionTest, a_jump_soon_after_a_scene_s_fade_still_belongs_to_it) {
    join(sampleGuest());
    tick();
    auto guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    _game->globalFade().request(GlobalFade::Direction::Out, 0.0f, 0.0f, glm::vec3(0.0f), GlobalFade::Source::Script);
    _game->globalFade().update(0.0f);
    tick(); // the scene is seen
    _game->globalFade().request(GlobalFade::Direction::In, 0.0f, 0.0f, glm::vec3(0.0f), GlobalFade::Source::Script);
    _game->globalFade().update(0.1f);
    ASSERT_FALSE(_game->globalFade().fading());
    ASSERT_EQ(0.0f, _game->globalFade().opacity());
    tick();
    guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    glm::vec3 to(40.0f, 20.0f, 0.0f);
    glm::vec3 from = _player->position();
    _player->setPosition(to);
    _session->onScriptedJump(*_player, from, to);
    EXPECT_EQ(1u, _session->guestSceneJumps());
    EXPECT_LT(glm::distance(glm::vec2(guest->position()), glm::vec2(to)), 6.0f);
}

TEST_F(CoopFreeSessionTest, outside_a_scene_and_for_other_creatures_or_short_moves_guests_stay) {
    join(sampleGuest());
    tick();
    auto guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    glm::vec3 where = guest->position();
    // Free play: no fade, no conversation, a commandable leader.
    glm::vec3 to(40.0f, 20.0f, 0.0f);
    _session->onScriptedJump(*_player, _player->position(), to);
    // In a scene, but a creature other than the leader, or a step in place.
    _game->globalFade().request(GlobalFade::Direction::Out, 0.0f, 0.0f, glm::vec3(0.0f), GlobalFade::Source::Script);
    _game->globalFade().update(0.0f);
    _session->onScriptedJump(*_trask, _trask->position(), to);
    _session->onScriptedJump(*_player, _player->position(), _player->position() + glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(0u, _session->guestSceneJumps());
    EXPECT_EQ(where, guest->position());
}

// Co-op scenes (design note coop_playtest_1022 rows 9, 10, 11, 15, 16, 18, 19)

void reone::game::TestGameModule::equipForTest(Creature &creature, int slot, const std::shared_ptr<Item> &item) {
    creature._equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(creature.id());
}

namespace {

class ScenesConversation : public Conversation {
public:
    ScenesConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    /** As a running conversation stands for the session: its dialog and owner (no screen work). */
    void start(const std::shared_ptr<Dialog> &dialog, const std::shared_ptr<Object> &owner) {
        _dialog = dialog;
        _owner = owner;
        _currentEntry = &_dialog->entries[0];
    }

protected:
    void setReplyLines(std::vector<std::string>) override {}
    void setMessage(std::string) override {}
};

std::shared_ptr<Dialog> oneEntryDialog(const std::string &resRef) {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = resRef;
    Dialog::EntryReplyLink start;
    start.index = 0;
    dialog->startEntries.push_back(start);
    dialog->entries.resize(1);
    dialog->replies.resize(1);
    dialog->entries[0].text = "[Throughout the game you will find upgradeable items.]";
    dialog->entries[0].delay = 100;
    Dialog::EntryReplyLink reply;
    reply.index = 0;
    dialog->entries[0].replies.push_back(reply);
    dialog->replies[0].text = "[Use the workbench now.]";
    return dialog;
}

// The session is the game's for the test's length (creature and trigger code reach it through Game).
struct SessionOnGame {
    Game &game;
    SessionOnGame(Game &game, std::shared_ptr<CoopSession> session) :
        game(game) {
        game.setCoopSession(std::move(session));
    }
    ~SessionOnGame() { game.setCoopSession(nullptr); }
};

// ExecuteScript("inner", ...): the literal is pushed just before the call.
std::shared_ptr<script::ScriptProgram> scriptExecuting(Game &game, const std::string &name, const std::string &inner) {
    auto program = std::make_shared<script::ScriptProgram>(name);
    program->add(script::Instruction::newCONSTS(inner));
    program->add(script::Instruction::newACTION(game.routines().getIndexByName("ExecuteScript"), 1));
    return program;
}

} // namespace

// Row 15: a workbench's own talk is the host's personal screen.
TEST_F(CoopStandInSessionTest, a_workbench_s_own_talk_on_the_host_leaves_the_stand_in_where_it_is_and_free_to_move) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    auto bench = _game->newPlaceable();
    bench->setTag("Workbench");
    bench->setUsable(true);
    bench->setOnUsed("scenes_wb_used");
    bench->setConversation("scenes_wb_talk");
    bench->setPosition(glm::vec3(1.0f, 1.0f, 0.0f));
    _area->add(bench);
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_wb_used"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_wb_used", "ShowUpgradeScreen")));
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    auto standIn = _game->getObjectById<Creature>(id);
    standIn->setPosition(glm::vec3(7.0f, -4.0f, 0.0f)); // walking about the apartment
    glm::vec3 where = standIn->position();

    ScenesConversation talk(*_game, _engine.services());
    talk.start(oneEntryDialog("scenes_wb_talk"), bench);
    TestGameModule::setConversation(*_game, &talk);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    EXPECT_TRUE(coopPersonalConversation(*_game));
    _client.inbox.clear();
    tick();
    tick();
    EXPECT_EQ(id, standInId()) << "the stand-in is not removed for the host's bench";
    EXPECT_EQ(where, standIn->position()) << "nor moved";
    auto status = _session->statusJson().at("standIns").as_object();
    EXPECT_EQ("", std::string(status.at("pause").as_string()));
    EXPECT_FALSE(status.at("sharedScene").as_bool());
    for (const auto &snapshot : _client.take<net::Snapshot>(net::MessageType::Snapshot)) {
        if (snapshot.worldFields & net::kWorldConversation) {
            EXPECT_FALSE(snapshot.world.conversation.active) << "the talk stays on the host's screen";
        }
        if (snapshot.worldFields & net::kWorldScreen) {
            EXPECT_EQ(static_cast<uint8_t>(Game::Screen::InGame), snapshot.world.screen);
        }
    }
    // Nor frozen: its player's orders are taken.
    net::Command move;
    move.seq = 77;
    move.type = net::CommandType::MoveTo;
    move.actor = id;
    move.point = {6.0f, -3.0f, 0.0f};
    _client.send(move);
    _session->pollNetwork();
    _client.inbox.clear();
    tick();
    auto results = _client.take<net::CommandResult>(net::MessageType::CommandResult);
    ASSERT_FALSE(results.empty());
    EXPECT_TRUE(results.back().accepted) << net::commandRejectName(results.back().reason);

    // A story conversation is a shared scene: the stand-in leaves for it, as before.
    talk.start(oneEntryDialog("scenes_npc_talk"), _trask);
    EXPECT_FALSE(coopPersonalConversation(*_game));
    tick();
    EXPECT_EQ(0u, standInId());
    EXPECT_EQ("conversation", std::string(_session->statusJson().at("standIns").as_object().at("pause").as_string()));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    TestGameModule::setConversation(*_game, nullptr);
}

// Rows 15 and 16, Free Mode: a guest's bench stays open while the host talks at the same bench and upgrades.
TEST_F(CoopFreeSessionTest, a_guest_at_a_bench_stays_while_the_host_uses_the_same_bench) {
    auto bench = _game->newPlaceable();
    bench->setTag("Workbench");
    bench->setUsable(true);
    bench->setOnUsed("scenes_free_wb");
    bench->setConversation("scenes_free_wb_talk");
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_free_wb"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_free_wb", "ShowUpgradeScreen")));
    join(sampleGuest());
    tick();
    uint32_t id = guestId();
    auto guest = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(guest);
    bench->setPosition(guest->position());
    _area->add(bench);
    auto ticks = [this]() {
        tick();
        tick();
        tick();
    };
    ASSERT_EQ(net::CommandReject::None, guestCommand(*_session, _client, id, net::CommandType::UseObject, bench->id(), 0, "", ticks));
    std::vector<net::BenchView> views;
    for (int i = 0; i < 5 && views.empty(); ++i) {
        guest->update(0.1f);
        tick();
        views = _client.take<net::BenchView>(net::MessageType::BenchView);
    }
    ASSERT_FALSE(views.empty());
    ASSERT_TRUE(views.back().open);

    // The host uses the same bench: its tutorial talk, then its own upgrade screen.
    ScenesConversation talk(*_game, _engine.services());
    talk.start(oneEntryDialog("scenes_free_wb_talk"), bench);
    TestGameModule::setConversation(*_game, &talk);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    _client.inbox.clear();
    for (int i = 0; i < 4; ++i) {
        tick();
    }
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Upgrade));
    for (int i = 0; i < 4; ++i) {
        tick();
    }
    EXPECT_EQ(id, guestId()) << "the guest's character stays in the world";
    for (const auto &view : _client.take<net::BenchView>(net::MessageType::BenchView)) {
        EXPECT_TRUE(view.open) << "nothing closes the guest's own bench";
    }
    std::string wearer;
    for (const auto &[slot, item] : guest->equipment()) {
        if (item) {
            EXPECT_FALSE(_session->hostItemWornByGuest(item->id(), wearer)) << "Free Mode: the host's bench never shares a guest's items";
        }
    }
    EXPECT_EQ(net::CommandReject::None, guestCommand(*_session, _client, id, net::CommandType::BenchClose, bench->id(), 0, "", ticks));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    TestGameModule::setConversation(*_game, nullptr);
}

// Row 16, Story Mode: the inventory is shared, what another player wears is not.
TEST_F(CoopStandInSessionTest, a_story_mode_bench_refuses_an_item_another_player_has_equipped) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    auto bench = _game->newPlaceable();
    bench->setTag("workbench");
    bench->setUsable(true);
    bench->setOnUsed("scenes_story_wb");
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_story_wb"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_story_wb", "ShowUpgradeScreen")));
    auto hostsBlaster = _game->newItem();
    TestGameModule::equipForTest(*_player, InventorySlots::rightWeapon, hostsBlaster);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    bench->setPosition(standIn->position());
    _area->add(bench);
    auto run = [&](net::CommandType type, int32_t param, const std::string &item) {
        return guestCommand(*_session, _client, id, type, bench->id(), param, item, [this]() {
            tick();
            tick();
            tick();
        });
    };
    ASSERT_EQ(net::CommandReject::None, run(net::CommandType::UseObject, 0, ""));
    for (int i = 0; i < 5; ++i) {
        standIn->update(0.1f);
        tick();
    }
    EXPECT_EQ(net::CommandReject::ItemEquippedByPlayer, run(net::CommandType::BenchUpgrade, static_cast<int32_t>(hostsBlaster->id()), "5"))
        << "the host is holding that blaster";
    EXPECT_EQ(net::CommandReject::ItemEquippedByPlayer, run(net::CommandType::BenchRemove, static_cast<int32_t>(hostsBlaster->id()), "0"));
    EXPECT_EQ("Another player has that equipped", GuestBenchScreen::rejectionText(net::CommandReject::ItemEquippedByPlayer));
    // Something nobody wears is the bench's usual business (here: no such item).
    EXPECT_EQ(net::CommandReject::ItemUnavailable, run(net::CommandType::BenchUpgrade, 424242, "5"));

    // The host's own bench: a companion a guest plays is that guest's to change.
    std::string wearer;
    EXPECT_FALSE(_session->hostItemWornByGuest(hostsBlaster->id(), wearer)) << "the host's own blaster is the host's";
    auto traskBlaster = _game->newItem();
    TestGameModule::equipForTest(*_trask, InventorySlots::rightWeapon, traskBlaster);
    traskJoins();
    tick();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_TRUE(_session->hostItemWornByGuest(traskBlaster->id(), wearer));
    EXPECT_EQ("Guest", wearer);
}

// Row 9: back where it stood after a conversation, unless the scene moved the party.
TEST_F(CoopStandInSessionTest, after_a_conversation_the_stand_in_comes_back_where_it_stood) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    join();
    tick();
    uint32_t first = standInId();
    ASSERT_NE(0u, first);
    _game->getObjectById<Creature>(first)->setPosition(glm::vec3(17.0f, 9.0f, 0.0f)); // 19 m from the host
    _game->getObjectById<Creature>(first)->setFacing(1.25f);

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    ASSERT_EQ(0u, standInId());
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    auto back = _game->getObjectById<Creature>(standInId());
    ASSERT_TRUE(back);
    EXPECT_LT(glm::distance(glm::vec2(back->position()), glm::vec2(17.0f, 9.0f)), 1.0f) << "not yanked to the host";
    EXPECT_FLOAT_EQ(1.25f, back->getFacing());
    EXPECT_EQ(1u, _session->statusJson().at("standIns").as_object().at("returned").to_number<uint64_t>());

    // A cutscene that moves the party: the stand-in goes with the host's character.
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    _player->setPosition(glm::vec3(60.0f, 40.0f, 0.0f));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    back = _game->getObjectById<Creature>(standInId());
    ASSERT_TRUE(back);
    EXPECT_LT(glm::distance(back->position(), _player->position()), 4.0f);
}

TEST_F(CoopFreeSessionTest, after_a_conversation_a_guest_comes_back_where_it_stood) {
    join(sampleGuest());
    tick();
    auto guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    guest->setPosition(glm::vec3(-12.0f, 30.0f, 0.0f));
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    ASSERT_EQ(0u, guestId());
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    guest = _game->getObjectById<Creature>(guestId());
    ASSERT_TRUE(guest);
    EXPECT_LT(glm::distance(glm::vec2(guest->position()), glm::vec2(-12.0f, 30.0f)), 1.0f);
}

// Row 18: auto-combat on a player's character acts only in a fight.
TEST_F(CoopStandInSessionTest, auto_combat_on_a_player_s_character_does_nothing_out_of_combat_and_shows_on_its_hud) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    SessionOnGame on(*_game, _session);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    ASSERT_TRUE(_session->isPlayerControlled(id));
    standIn->setOnEndRound("scenes_round");
    // Out of combat the round script is never even looked up.
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_round")).Times(0);
    glm::vec3 where = standIn->position();
    auto ticks = [this]() { tick(); };
    EXPECT_EQ(net::CommandReject::None, guestCommand(*_session, _client, id, net::CommandType::AutoCombat, 0, 1, "", ticks));
    ASSERT_TRUE(standIn->autoCombat());
    for (int i = 0; i < 40; ++i) {
        standIn->update(0.1f);
    }
    EXPECT_TRUE(standIn->actions().empty()) << "no follow, no wandering";
    EXPECT_EQ(where, standIn->position());

    // The guest's HUD toggle reads the host's word.
    auto state = CoopReplication::capture(*standIn, "");
    EXPECT_NE(0, state.flags & net::kFlagAutoCombat);
    auto copy = _game->newCreature();
    CoopReplication::apply(*_game, *copy, state, net::kFieldFlags);
    EXPECT_TRUE(copy->autoCombat());
    state.flags &= ~net::kFlagAutoCombat;
    CoopReplication::apply(*_game, *copy, state, net::kFieldFlags);
    EXPECT_FALSE(copy->autoCombat());
}

// Row 19: only the host's character starts scripted conversations and cutscenes.
TEST_F(CoopNetTest, scripts_that_start_a_conversation_or_a_cutscene_are_told_apart) {
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_talks"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_talks", "ActionStartConversation")));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_movie"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_movie", "PlayMovie")));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_outer"))
        .WillRepeatedly(Return(scriptExecuting(*_game, "scenes_outer", "scenes_talks")));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_locks"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_locks", "SetLocked")));
    EXPECT_TRUE(coopScriptStartsScene(*_game, "scenes_talks"));
    EXPECT_TRUE(coopScriptStartsScene(*_game, "scenes_movie"));
    EXPECT_TRUE(coopScriptStartsScene(*_game, "scenes_outer")) << "through ExecuteScript";
    EXPECT_FALSE(coopScriptStartsScene(*_game, "scenes_locks"));
    EXPECT_FALSE(coopScriptStartsScene(*_game, "scenes_missing"));
    EXPECT_FALSE(coopScriptStartsScene(*_game, ""));
}

TEST_F(CoopStandInSessionTest, a_guest_walking_into_a_conversation_trigger_does_not_start_it_the_host_does) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    SessionOnGame on(*_game, _session);
    join();
    tick();
    uint32_t id = standInId();
    auto standIn = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(standIn);
    // k_ptar_carstr_en: Carth talks to whoever GetIsPC says walked in.
    int fetched = 0;
    auto program = scriptCalling(*_game, "scenes_carstr", "ActionStartConversation");
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_carstr")).WillRepeatedly(Invoke([&](const std::string &) {
        // The first fetch is the scan; the runs that follow find nothing to execute here.
        return ++fetched == 1 ? program : std::shared_ptr<script::ScriptProgram>();
    }));
    ASSERT_TRUE(coopScriptStartsScene(*_game, "scenes_carstr"));
    ASSERT_EQ(1, fetched);
    auto trigger = _game->newTrigger();
    trigger->setTag("tar02_carstr");
    trigger->setOnEnter("scenes_carstr");
    trigger->setOnClick("scenes_carstr");
    _area->add(trigger);

    EXPECT_TRUE(_session->hostHoldsScene(id, "scenes_carstr"));
    trigger->addTenant(standIn);
    EXPECT_EQ(1, fetched) << "the guest's stand-in does not run the scene's script";
    trigger->runClickScript(id);
    EXPECT_EQ(1, fetched) << "nor by clicking it";
    EXPECT_FALSE(_session->hostHoldsScene(_player->id(), "scenes_carstr"));
    trigger->addTenant(_player);
    EXPECT_EQ(2, fetched) << "the host's character does";
    EXPECT_FALSE(_session->hostHoldsScene(id, "")) << "no script, nothing to hold";
    EXPECT_TRUE(_session->statusJson().at("options").as_object().at("hostOnlyScenes").as_bool());
}

TEST_F(CoopStandInSessionTest, with_host_only_scenes_off_a_guest_starts_trigger_scenes_as_before) {
    _player->setMaxHitPoints(30);
    _player->setCurrentHitPoints(30);
    auto [host, client] = net::createLoopbackPair();
    _session.reset();
    _client = RawClient();
    _client.transport = std::move(client);
    CoopConfig config;
    config.role = CoopRole::Host;
    config.build = "build-A";
    config.k1 = {true, 1, 2, 3};
    config.hostOnlyScenes = false;
    _session = std::make_shared<CoopSession>(*_game, config, std::move(host));
    _session->pollNetwork();
    _client.poll();
    join();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_carstr_off"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_carstr_off", "ActionStartConversation")));
    EXPECT_FALSE(_session->hostHoldsScene(id, "scenes_carstr_off"));
}

// Row 11: a module's opening conversation waits for guests still loading it.
TEST_F(CoopSessionTest, a_conversation_right_after_a_load_waits_for_a_guest_still_loading) {
    SessionOnGame on(*_game, _session);
    _client.autoAck = false; // the guest is still loading: it acknowledges nothing
    _client.send(hello());
    exchange();
    tick();
    ASSERT_FALSE(moduleGenerations(_client).empty());
    auto npc = addCreature("carth", glm::vec3(1.0f, 0.0f, 0.0f), Faction::Friendly1);
    EXPECT_CALL(_engine.resourceModule().gffs(), get("scenes_wake", ResType::Dlg)).Times(0);
    _game->startDialog(npc, "scenes_wake");
    EXPECT_TRUE(_session->conversationHeld());
    EXPECT_EQ(Game::Screen::InGame, _game->currentScreen()) << "not started yet";
    auto status = _session->statusJson().at("standIns").as_object();
    EXPECT_EQ("scenes_wake", std::string(status.at("heldConversation").as_string()));
    tick();
    tick();
    EXPECT_TRUE(_session->conversationHeld()) << "still loading";
    Mock::VerifyAndClearExpectations(&_engine.resourceModule().gffs());

    // The guest's first state arrives: the conversation starts (its file is missing here, so it ends there).
    auto snapshots = _client.take<net::Snapshot>(net::MessageType::Snapshot);
    ASSERT_FALSE(snapshots.empty());
    EXPECT_CALL(_engine.resourceModule().gffs(), get("scenes_wake", ResType::Dlg)).Times(1);
    net::SnapshotAck ack {snapshots.back().generation, snapshots.back().tick};
    _client.transport->send(_client.connection, net::encode(ack), net::SendMode::Unreliable);
    tick();
    tick();
    EXPECT_FALSE(_session->conversationHeld());
    EXPECT_EQ(1u, _session->statusJson().at("standIns").as_object().at("conversationsHeld").to_number<uint64_t>());

    // Once everyone has loaded, conversations start at once.
    EXPECT_CALL(_engine.resourceModule().gffs(), get("scenes_later", ResType::Dlg)).Times(1);
    _game->startDialog(npc, "scenes_later");
    EXPECT_FALSE(_session->conversationHeld());
}

// Row 10: a late-loading guest gets each clip at the host's time now.
TEST(CoopAnimationTest, a_clip_goes_at_the_host_s_time_while_the_client_has_no_base_and_on_drift_after) {
    net::EntityState sent;
    sent.anim = "talk_normal";
    sent.animTime = 0.0f;
    sent.animSpeed = 1.0f;
    sent.animSentTick = 100;
    net::EntityState now = sent;
    now.animTime = 2.0f; // 60 ticks later at 30 Hz: where the client will have run it too
    float dt = 1.0f / 30.0f;
    EXPECT_FALSE(CoopReplication::animationDue(sent, now, 160, dt, false)) << "a client with a base runs the clip on itself";
    EXPECT_TRUE(CoopReplication::animationDue(sent, now, 160, dt, true))
        << "a loading client's full state carries the host's time now, not the time first sent";
    now.animTime = 0.5f; // the host restarted or slowed it
    EXPECT_TRUE(CoopReplication::animationDue(sent, now, 160, dt, false));
    net::EntityState still;
    EXPECT_FALSE(CoopReplication::animationDue(still, still, 160, dt, true)) << "no clip, nothing to send";
}

// Better stand-ins (design note companion_standins): a copy of a companion
// who fits, else John Shepard, and the copy rules.

namespace {

std::shared_ptr<resource::TwoDA> standInTable(const std::vector<std::string> &columns, const std::vector<std::vector<std::string>> &rows) {
    resource::TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<resource::TwoDA>(builder.build());
}

std::shared_ptr<resource::Gff> missionTemplate() {
    using resource::Gff;
    auto pistol = Gff::Builder().type(16).field(Gff::Field::newResRef("EquippedRes", "g_w_blstrpstl001")).build();
    auto blade = Gff::Builder().type(0).field(Gff::Field::newResRef("InventoryRes", "g_w_vbroshort08")).build();
    auto scoundrel = Gff::Builder().type(2).field(Gff::Field::newInt("Class", 2)).field(Gff::Field::newShort("ClassLevel", 3)).build();
    return Gff::Builder()
        .field(Gff::Field::newCExoLocString("FirstName", -1, "Mission"))
        .field(Gff::Field::newCExoString("Tag", "Mission"))
        .field(Gff::Field::newWord("Appearance_Type", 10))
        .field(Gff::Field::newByte("Gender", 1))
        .field(Gff::Field::newWord("PortraitId", 35))
        .field(Gff::Field::newResRef("Conversation", "k_hmis_dialog"))
        .field(Gff::Field::newResRef("ScriptHeartbeat", "k_hen_heartbt01"))
        .field(Gff::Field::newResRef("ScriptSpawn", "k_hen_spawn01"))
        .field(Gff::Field::newByte("Plot", 1))
        .field(Gff::Field::newShort("HitPoints", 18))
        .field(Gff::Field::newShort("MaxHitPoints", 21))
        .field(Gff::Field::newShort("CurrentHitPoints", 21))
        .field(Gff::Field::newList("ClassList", {scoundrel}))
        .field(Gff::Field::newList("Equip_ItemList", {pistol}))
        .field(Gff::Field::newList("ItemList", {blade}))
        .build();
}

std::shared_ptr<resource::Gff> itemTemplate(int baseItem, const std::string &tag) {
    using resource::Gff;
    return Gff::Builder()
        .field(Gff::Field::newInt("BaseItem", baseItem))
        .field(Gff::Field::newCExoString("Tag", tag))
        .field(Gff::Field::newResRef("TemplateResRef", tag))
        .field(Gff::Field::newByte("Dropable", 1))
        .build();
}

} // namespace

class CoopCastSessionTest : public CoopStandInSessionTest {
protected:
    void SetUp() override {
        CoopStandInSessionTest::SetUp();
        auto &gffs = _engine.resourceModule().gffs();
        ON_CALL(gffs, get(_, _)).WillByDefault(Return(std::shared_ptr<resource::Gff>()));
        ON_CALL(gffs, get("p_mission", resource::ResType::Utc)).WillByDefault(Return(missionTemplate()));
        ON_CALL(gffs, get("g_w_blstrpstl001", resource::ResType::Uti)).WillByDefault(Return(itemTemplate(12, "g_w_blstrpstl001")));
        ON_CALL(gffs, get("g_w_vbroshort08", resource::ResType::Uti)).WillByDefault(Return(itemTemplate(5, "g_w_vbroshort08")));
        // Blaster pistols and vibroblades go in either hand (baseitems.2da equipableslots 0x30).
        std::vector<std::vector<std::string>> rows(13, std::vector<std::string> {"0"});
        rows[5] = {"48"};
        rows[12] = {"48"};
        auto &twoDas = _engine.resourceModule().twoDas();
        ON_CALL(twoDas, get("baseitems")).WillByDefault(Return(standInTable({"equipableslots"}, rows)));
    }

    void enterModule(const std::string &name) {
        auto ifo = resource::Gff::Builder()
                       .field(resource::Gff::Field::newCExoLocString("Mod_Name", -1, name))
                       .field(resource::Gff::Field::newResRef("Mod_Entry_Area", "entryarea"))
                       .build();
        TestGameModule::loadModuleInfo(*_game->module(), name, *ifo);
    }

    boost::json::object cast() const {
        return _session->statusJson().at("clients").as_array().at(0).as_object().at("cast").as_object();
    }
};

TEST_F(CoopCastSessionTest, on_taris_a_guest_with_no_companion_plays_a_copy_of_one_who_fits) {
    enterModule("tar_m02aa");
    join();
    _client.inbox.clear();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id) << boost::json::serialize(_session->statusJson());
    auto copy = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(copy);
    auto status = cast();
    EXPECT_EQ("copy", status.at("kind").as_string());
    EXPECT_EQ("Mission", status.at("name").as_string());
    EXPECT_EQ(6, status.at("npc").to_number<int>());
    EXPECT_NE(std::string::npos, std::string(status.at("reason").as_string()).find("Taris")) << boost::json::serialize(status);

    // Its own name, the guest's name on the tag; a puppet outside the party with nothing of the story.
    EXPECT_EQ("Mission", copy->name());
    EXPECT_EQ(0u, copy->tag().find("coop_standin_"));
    EXPECT_TRUE(copy->isPuppet());
    EXPECT_FALSE(_game->party().isMember(*copy));
    EXPECT_EQ(1, _game->party().getSize());
    EXPECT_TRUE(copy->conversation().empty()) << "no companion dialogue: no plot globals, no influence";
    EXPECT_FALSE(copy->plotFlag());
    EXPECT_EQ(Faction::Player, copy->faction());
    EXPECT_EQ(10, copy->appearance());
    bool labelled = false;
    for (const auto &presence : _session->presences()) {
        if (presence.object == copy) {
            labelled = true;
            EXPECT_EQ("Mission (Guest)", presence.name);
        }
    }
    EXPECT_TRUE(labelled);

    // The template's gear, worn; nothing carried (the vibroblade in its pack never comes).
    auto pistol = copy->getEquippedItem(InventorySlots::rightWeapon);
    ASSERT_TRUE(pistol);
    EXPECT_EQ("g_w_blstrpstl001", pistol->blueprintResRef());
    EXPECT_FALSE(pistol->isDropable()) << "never loot";
    EXPECT_TRUE(copy->items().empty());
    EXPECT_TRUE(_session->isStandInItem(pistol->id()));
    EXPECT_FALSE(_session->isStandInItem(_player->id()));
    for (const auto &item : _player->items()) {
        EXPECT_NE(pistol, item) << "never the party's";
    }

    // The client builds it from the same template.
    bool sent = false;
    for (const auto &snapshot : _client.take<net::Snapshot>(net::MessageType::Snapshot)) {
        for (const auto &record : snapshot.entities) {
            if (record.state.id == id && !record.state.resref.empty()) {
                sent = true;
                EXPECT_NE(std::string::npos, record.state.resref.find(":p_mission"));
            }
        }
    }
    EXPECT_TRUE(sent);
}

TEST_F(CoopCastSessionTest, the_copy_keeps_its_identity_across_a_conversation_and_its_gear_goes_with_it) {
    enterModule("tar_m02aa");
    join();
    tick();
    uint32_t first = standInId();
    ASSERT_NE(0u, first);
    auto pistol = _game->getObjectById<Creature>(first)->getEquippedItem(InventorySlots::rightWeapon);
    ASSERT_TRUE(pistol);
    uint32_t pistolId = pistol->id();
    pistol.reset();

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));
    tick();
    EXPECT_EQ(0u, standInId());
    EXPECT_FALSE(_game->getObjectById<Item>(pistolId)) << "destroyed with it: nothing to duplicate";

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    tick();
    uint32_t second = standInId();
    ASSERT_NE(0u, second);
    EXPECT_EQ("Mission", _game->getObjectById<Creature>(second)->name()) << "the same character comes back";
    EXPECT_EQ("Mission", cast().at("name").as_string());
}

TEST_F(CoopCastSessionTest, a_companion_present_in_the_module_is_never_copied) {
    enterModule("tar_m04aa");
    addCreature("tar04_mission", glm::vec3(30.0f, 0.0f, 0.0f), Faction::Friendly1);
    join();
    tick();
    auto status = cast();
    EXPECT_EQ(8, status.at("npc").to_number<int>()) << boost::json::serialize(status) << " (Zaalbar is next)";
    bool skippedMission = false;
    for (const auto &entry : status.at("skipped").as_array()) {
        skippedMission = skippedMission || std::string(entry.as_string()).find("Mission is in this module") == 0;
    }
    EXPECT_TRUE(skippedMission);
    // This test's install has no Zaalbar template: the build falls back, and says so.
    EXPECT_EQ("clone", status.at("kind").as_string());
    EXPECT_NE(std::string::npos, std::string(status.at("reason").as_string()).find("build failed"));
    EXPECT_NE(0u, standInId()) << "the guest still plays";
}

TEST_F(CoopCastSessionTest, a_free_companion_is_reported_as_the_real_one) {
    enterModule("tar_m02aa");
    traskJoins();
    join();
    tick();
    EXPECT_EQ("companion", cast().at("kind").as_string());
    EXPECT_EQ(0u, standInId());
}

TEST_F(CoopCastSessionTest, the_endar_spire_has_john_shepard_and_the_wire_names_him) {
    enterModule("end_m01aa");
    // A soldier class for him to be (classes.2da row 0).
    auto &twoDas = _engine.resourceModule().twoDas();
    const std::vector<std::string> columns = {"name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
                                              "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> classRows(9, std::vector<std::string>(columns.size()));
    classRows[0] = {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "sol", "save", "attack", "sol", "", "", "0", "0"};
    ON_CALL(twoDas, get("classes")).WillByDefault(Return(standInTable(columns, classRows)));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(standInTable({"label", "sol_class", "sol_reco"}, {{"computeruse", "0", ""}})));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(standInTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(standInTable({"bab"}, {{"0"}})));
    auto classes = std::make_shared<Classes>(_engine.resourceModule().strings(), twoDas);
    ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([classes](ClassType type) { return classes->get(type); }));
    join();
    _client.inbox.clear();
    tick();
    uint32_t id = standInId();
    ASSERT_NE(0u, id);
    auto shepard = _game->getObjectById<Creature>(id);
    ASSERT_TRUE(shepard);
    auto status = cast();
    EXPECT_EQ("generic", status.at("kind").as_string()) << boost::json::serialize(status);
    EXPECT_EQ("Republic soldier", status.at("name").as_string());
    EXPECT_EQ("John Shepard", shepard->name());
    EXPECT_EQ(coopShepard().appearance, shepard->appearance());
    EXPECT_EQ(coopShepard().portrait, shepard->portraitId());
    EXPECT_TRUE(shepard->isPuppet());
    EXPECT_FALSE(_game->party().isMember(*shepard));
    bool sent = false;
    for (const auto &snapshot : _client.take<net::Snapshot>(net::MessageType::Snapshot)) {
        for (const auto &record : snapshot.entities) {
            if (record.state.id == id && !record.state.resref.empty()) {
                sent = true;
                EXPECT_NE(std::string::npos, record.state.resref.find(":coop_shepard"));
            }
        }
    }
    EXPECT_TRUE(sent);
}

TEST_F(CoopCastSessionTest, a_shepard_cameo_goes_to_guests_as_an_ownerless_stand_in_and_cannot_be_targeted) {
    enterModule("tar_m02af");
    auto &twoDas = _engine.resourceModule().twoDas();
    const std::vector<std::string> columns = {"name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
                                              "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> classRows(9, {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "sol", "save", "attack", "sol", "", "", "0", "0"});
    ON_CALL(twoDas, get("classes")).WillByDefault(Return(standInTable(columns, classRows)));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(standInTable({"label", "sol_class", "sol_reco"}, {{"computeruse", "0", ""}})));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(standInTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(standInTable({"bab"}, {{"0"}})));
    auto classes = std::make_shared<Classes>(_engine.resourceModule().strings(), twoDas);
    ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([classes](ClassType type) { return classes->get(type); }));
    join();
    tick();
    _client.inbox.clear();
    _game->setCoopSession(_session); // the director finds the session through the game
    _game->cameos().lenient = true;
    _game->cameos().update(*_game);
    tick();
    uint32_t cameoId = 0;
    for (const auto &object : _game->module()->area()->getObjectsByType(ObjectType::Creature)) {
        if (std::static_pointer_cast<Creature>(object)->isCameo()) {
            cameoId = object->id();
        }
    }
    ASSERT_NE(0u, cameoId);
    EXPECT_TRUE(_session->isStandIn(cameoId)) << "the host's targeting and bench rules treat him as a stand-in";
    EXPECT_NE(cameoId, standInId()) << "no guest owns him";
    bool sent = false;
    for (const auto &snapshot : _client.take<net::Snapshot>(net::MessageType::Snapshot)) {
        for (const auto &record : snapshot.entities) {
            if (record.state.id == cameoId && !record.state.resref.empty()) {
                sent = true;
                EXPECT_NE(std::string::npos, record.state.resref.find(std::string(":") + kCoopCameoTemplate)) << record.state.resref;
                EXPECT_EQ(std::string(kCameoTag), record.state.tag);
                EXPECT_TRUE(record.state.look.playerName.empty()) << "no name tag: nobody plays him";
            }
        }
    }
    EXPECT_TRUE(sent);
    // Gone: the wire forgets him too.
    _engine.options().game.cameos = false;
    _game->cameos().update(*_game);
    EXPECT_FALSE(_session->isStandIn(cameoId));
    _game->setCoopSession(nullptr);
}

TEST_F(CoopCastSessionTest, the_cameo_yields_where_a_guest_plays_john_shepard) {
    enterModule("end_m01aa");
    auto &twoDas = _engine.resourceModule().twoDas();
    const std::vector<std::string> columns = {"name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
                                              "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> classRows(9, {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "sol", "save", "attack", "sol", "", "", "0", "0"});
    ON_CALL(twoDas, get("classes")).WillByDefault(Return(standInTable(columns, classRows)));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(standInTable({"label", "sol_class", "sol_reco"}, {{"computeruse", "0", ""}})));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(standInTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(standInTable({"bab"}, {{"0"}})));
    auto classes = std::make_shared<Classes>(_engine.resourceModule().strings(), twoDas);
    ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([classes](ClassType type) { return classes->get(type); }));
    EXPECT_FALSE(_session->hostGuestPlaysShepard("end_m01aa")) << "nobody has joined";
    join();
    tick();
    ASSERT_EQ("generic", cast().at("kind").as_string());
    EXPECT_TRUE(_session->hostGuestPlaysShepard("end_m01aa"));
    EXPECT_TRUE(_session->hostGuestPlaysShepard("END_M01AA")) << "whatever the case";
    EXPECT_FALSE(_session->hostGuestPlaysShepard("tar_m02af")) << "another module";
}

TEST_F(CoopCastSessionTest, auto_level_up_levels_party_companions_but_not_the_player_character) {
    auto &twoDas = _engine.resourceModule().twoDas();
    const std::vector<std::string> columns = {"name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
                                              "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> classRows(9, std::vector<std::string>(columns.size()));
    classRows[0] = {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "sol", "save", "attack", "sol", "", "", "0", "0"};
    ON_CALL(twoDas, get("classes")).WillByDefault(Return(standInTable(columns, classRows)));
    ON_CALL(twoDas, get("skills")).WillByDefault(Return(standInTable({"label", "sol_class", "sol_reco"}, {{"computeruse", "1", "1"}})));
    ON_CALL(twoDas, get("save")).WillByDefault(Return(standInTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
    ON_CALL(twoDas, get("attack")).WillByDefault(Return(standInTable({"bab"}, {{"0"}})));
    auto classes = std::make_shared<Classes>(_engine.resourceModule().strings(), twoDas);
    ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([classes](ClassType type) { return classes->get(type); }));
    auto soldier = classes->get(ClassType::Soldier);
    ASSERT_TRUE(soldier);
    traskJoins();
    _trask->attributes().addClassLevels(soldier.get(), 1);
    _trask->setMaxHitPoints(10);
    _player->attributes().addClassLevels(soldier.get(), 1);
    _trask->setXP(3000); // two levels (1000, 3000)
    _player->setXP(3000);
    EXPECT_EQ(2, autoLevelPartyCompanions(*_game));
    EXPECT_EQ(3, _trask->attributes().getAggregateLevel());
    EXPECT_EQ(30, _trask->hitPoints()) << "a hit die per level";
    EXPECT_EQ(2, _trask->attributes().getSkillRank(SkillType::ComputerUse)) << "one recommended rank per level";
    EXPECT_EQ(1, _player->attributes().getAggregateLevel()) << "the player character levels through the screen";
    EXPECT_EQ(0, autoLevelPartyCompanions(*_game)) << "nothing more pending";
}

// Scripts drive a companion a guest controls (design note guest_npc_scripts):
// the Endar Spire's end_door02 jumps the party and Trask's hint conversation
// is an action queued on him; neither may be cut short by his player.

namespace {

// Queue an action the way a script routine does (AssignCommand, Action*).
void queueAsScript(Game &game, Object &object, std::shared_ptr<game::Action> action) {
    game.enterScriptRoutine();
    object.addAction(std::move(action));
    game.leaveScriptRoutine();
}

std::shared_ptr<script::ScriptProgram> scriptNaming(const std::string &name, const std::string &text) {
    auto program = std::make_shared<script::ScriptProgram>(name);
    program->add(script::Instruction::newCONSTS(text));
    return program;
}

} // namespace

TEST_F(CoopNetTest, actions_queued_inside_a_script_routine_are_the_script_s) {
    auto jump = _game->newAction<JumpToLocationAction>(std::make_shared<Location>(glm::vec3(1.0f), 0.0f));
    queueAsScript(*_game, *_trask, jump);
    EXPECT_TRUE(jump->isScriptAction());
    EXPECT_TRUE(_trask->hasScriptSequence());

    auto click = _game->newAction<MoveToLocationAction>(std::make_shared<Location>(glm::vec3(2.0f), 0.0f), true);
    _player->addAction(click);
    EXPECT_FALSE(click->isScriptAction()) << "a click or a co-op command is not a script's";

    auto order = _game->newAction<AttackObjectAction>(_sith);
    order->setUserAction(true);
    queueAsScript(*_game, *_player, order);
    EXPECT_FALSE(order->isScriptAction()) << "a player's order stays the player's";

    // The auto-combat round a player's hand asks the AI for is the AI's.
    _game->setAutoCombatScriptCaller(_trask->id());
    auto round = _game->newAction<MoveToLocationAction>(std::make_shared<Location>(glm::vec3(3.0f), 0.0f), true);
    queueAsScript(*_game, *_trask, round);
    _game->setAutoCombatScriptCaller(script::kObjectInvalid);
    EXPECT_FALSE(round->isScriptAction());
}

TEST_F(CoopNetTest, a_script_driving_the_guest_s_companion_holds_its_input_until_done) {
    traskJoins();
    ASSERT_EQ(net::CommandReject::None, check(net::CommandType::MoveTo));

    // k_pend_door18: AssignCommand(GetPartyMemberByIndex(1), ActionJumpToLocation(...)).
    auto jump = _game->newAction<JumpToLocationAction>(std::make_shared<Location>(glm::vec3(4.0f, 0.0f, 0.0f), 0.0f));
    queueAsScript(*_game, *_trask, jump);
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::MoveTo));
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::Cancel)) << "nor may the player clear the script's queue";
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::Attack, _sith->id()));

    // The jump runs as in single player; then the player has him back.
    _trask->update(0.0f);
    EXPECT_EQ(4.0f, _trask->position().x);
    _trask->update(0.0f);
    EXPECT_FALSE(_trask->hasScriptSequence());
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::MoveTo));

    // Fights, follows and wanders a script queues do not hold: the player may take over.
    queueAsScript(*_game, *_trask, _game->newAction<AttackObjectAction>(_sith));
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::MoveTo));
    _trask->clearAllActions();

    // NoClicksFor holds every player's clicks, the host's and a guest's.
    _game->blockClicksFor(0.5f);
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::MoveTo));
}

TEST_F(CoopNetTest, a_script_action_left_running_for_long_no_longer_holds_the_player) {
    traskJoins();
    queueAsScript(*_game, *_trask, _game->newAction<WaitAction>(1000.0f));
    _trask->update(0.0f); // it starts
    EXPECT_EQ(net::CommandReject::Busy, check(net::CommandType::MoveTo));
    int limit = static_cast<int>(kCoopScriptHoldSeconds * static_cast<float>(_game->tickRate())) + 2;
    for (int i = 0; i < limit; ++i) {
        TestGameModule::advanceTick(*_game);
    }
    EXPECT_EQ(net::CommandReject::None, check(net::CommandType::MoveTo)) << "a looping animation or a long wait left on";
}

TEST_F(CoopCompanionCombatTest, steering_waits_while_a_script_moves_the_companion_and_resumes_after) {
    traskTaken();
    auto steer = [&]() { return _session->statusJson().at("clients").as_array()[0].as_object().at("steer").as_object(); };
    auto steerTicks = [&]() { return steer().at("ticks").to_number<int64_t>(); };
    // The guest holds a movement key: the host steers Trask every tick.
    _client.send(steerMessage(1, _trask->id(), true, 0.0f, true));
    tick();
    tick();
    auto before = steerTicks();
    ASSERT_GE(before, 1) << "the keys drive him";

    // Trask's OnUserDefined 50 (k_pend_trask_d) queues his hint conversation;
    // k_pend_door18 queues a jump. Steering used to clear them every tick.
    auto jump = _game->newAction<JumpToLocationAction>(std::make_shared<Location>(glm::vec3(20.0f, 20.0f, 0.0f), 0.0f));
    queueAsScript(*_game, *_trask, jump);
    auto wait = _game->newAction<WaitAction>(0.5f);
    queueAsScript(*_game, *_trask, wait);
    tick();
    EXPECT_FALSE(jump->isCancelled()) << "the steering did not clear the script's jump";
    EXPECT_EQ(before, steerTicks()) << "no steering while the script holds him";
    EXPECT_FALSE(steer().at("moving").as_bool()) << "the held key waits for a fresh update";
    _trask->update(0.0f);
    EXPECT_EQ(20.0f, _trask->position().x) << "the jump ran";
    _client.send(steerMessage(2, _trask->id(), true, 0.0f, true));
    tick();
    EXPECT_FALSE(wait->isCancelled()) << "nor the script's wait";
    EXPECT_EQ(before, steerTicks());

    // Done: the player's keys (still held, resent) drive him again.
    wait->complete();
    _trask->update(0.0f);
    ASSERT_FALSE(_trask->hasScriptSequence());
    _client.send(steerMessage(3, _trask->id(), true, 0.0f, true));
    tick();
    EXPECT_GT(steerTicks(), before) << "the player's input is taken again";
}

TEST_F(CoopStandInSessionTest, a_script_that_names_a_guest_s_companion_sees_the_companion) {
    traskJoins();
    join();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    // tar02's Sith base trigger: GetTag(GetEnteringObject()) == "t3m4"; the Endar
    // Spire's doors name "end_trask".
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_names_trask"))
        .WillRepeatedly(Return(scriptNaming("k_names_trask", "END_TRASK")));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_names_other"))
        .WillRepeatedly(Return(scriptNaming("k_names_other", "end_door02")));
    EXPECT_TRUE(coopScriptNamesTag(*_game, "k_names_trask", "end_trask")) << "any case";
    EXPECT_FALSE(coopScriptNamesTag(*_game, "k_names_other", "end_trask"));
    EXPECT_EQ(_trask->id(), _session->scriptActor(_trask->id(), "k_names_trask"));
    EXPECT_EQ(_player->id(), _session->scriptActor(_trask->id(), "k_names_other")) << "other scripts read the PC as before";
    EXPECT_EQ(_player->id(), _session->scriptActor(_trask->id()));
}

TEST_F(CoopStandInSessionTest, a_guest_s_companion_counts_as_a_pc_for_door_and_trigger_scripts) {
    traskJoins();
    SessionOnGame on(*_game, _session);
    join();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    int routine = _game->routines().getIndexByName("GetIsPC");
    ASSERT_GE(routine, 0);
    auto trigger = _game->newTrigger();
    _area->add(trigger);
    auto isPC = [&](const std::shared_ptr<Creature> &creature, const std::shared_ptr<Object> &self) {
        script::ExecutionContext execution;
        execution.args.push_back({script::ArgKind::Caller, script::Variable::ofObject(self->id())});
        std::vector<script::Variable> args {script::Variable::ofObject(creature->id())};
        return _game->routines().get(routine).invoke(args, execution).intValue;
    };
    EXPECT_EQ(1, isPC(_player, trigger));
    EXPECT_EQ(1, isPC(_trask, trigger)) << "to a trigger's script, the guest's character, as a companion the host switches to";
    EXPECT_EQ(0, isPC(_sith, trigger));
    EXPECT_EQ(0, isPC(_trask, _sith)) << "creature scripts (the AI) keep one PC, as in single player";
    EXPECT_EQ(1, isPC(_player, _sith));
}

TEST_F(CoopStandInSessionTest, a_scene_trigger_a_script_moved_the_guest_s_companion_into_runs) {
    traskJoins();
    SessionOnGame on(*_game, _session);
    join();
    tick();
    ASSERT_TRUE(_session->isPlayerControlled(_trask->id()));
    EXPECT_CALL(_engine.resourceModule().scripts(), get("scenes_room3"))
        .WillRepeatedly(Return(scriptCalling(*_game, "scenes_room3", "ActionStartConversation")));
    EXPECT_TRUE(_session->hostHoldsScene(_trask->id(), "scenes_room3")) << "walked in by its player: held for the host";
    auto jump = _game->newAction<JumpToLocationAction>(std::make_shared<Location>(glm::vec3(6.0f, 0.0f, 0.0f), 0.0f));
    queueAsScript(*_game, *_trask, jump);
    _trask->update(0.0f);
    EXPECT_FALSE(_session->hostHoldsScene(_trask->id(), "scenes_room3")) << "a script jumped it there: the scene runs";
    for (int i = 0; i < _game->tickRate() + 2; ++i) {
        TestGameModule::advanceTick(*_game);
    }
    _trask->update(0.0f);
    EXPECT_TRUE(_session->hostHoldsScene(_trask->id(), "scenes_room3")) << "a second later it is the player's walk again";
}

TEST_F(CoopNetTest, a_client_s_level_up_icon_is_the_host_s_word) {
    // The guest's copy of the host's PC kept its own template level while
    // the shared XP grew: it showed a level-up the host did not have.
    auto copy = addCreature("player_copy", glm::vec3(0.0f), Faction::Player);
    copy->setXP(1000000);
    net::EntityState state = CoopReplication::capture(*_player, "");
    EXPECT_EQ(_player->isLevelUpPending(), (state.progress & net::kProgressLevelUp) != 0);
    state.progress = 0;
    CoopReplication::apply(*_game, *copy, state, net::kFieldProgress);
    EXPECT_FALSE(copy->isLevelUpPending()) << "the host says no level is due";
    state.progress = net::kProgressLevelUp;
    CoopReplication::apply(*_game, *copy, state, net::kFieldProgress);
    EXPECT_TRUE(copy->isLevelUpPending());
    copy->setXP(0);
    EXPECT_TRUE(copy->isLevelUpPending()) << "its own numbers do not count";
}
