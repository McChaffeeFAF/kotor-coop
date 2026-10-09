/*
 * Copyright (c) 2026 The reone project contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Campaign batch C02: module and object events K1 data fills. The notes
// these pin are in design note c02_events.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/closedoor.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/rest.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/dialog.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

constexpr int kLocalFirst = 50;
constexpr int kLocalSecond = 51;
constexpr int kLocalThird = 52;

// A probe script calls a no-argument routine and stores, on OBJECT_SELF,
// whether the result is the expected object (or the int result itself).
struct Observation {
    std::string routine;
    std::optional<uint32_t> equalsObject;
    int localIndex {0};
};

std::shared_ptr<script::ScriptProgram> probeProgram(
    const Routines &routines,
    const std::string &name,
    const std::vector<Observation> &observations) {
    auto program = std::make_shared<script::ScriptProgram>(name);
    int setLocalNumber = routines.getIndexByName("SetLocalNumber");
    for (const auto &observation : observations) {
        program->add(script::Instruction::newACTION(routines.getIndexByName(observation.routine), 0));
        if (observation.equalsObject) {
            program->add(script::Instruction::newCONSTO(static_cast<int>(*observation.equalsObject)));
            program->add(script::Instruction(script::InstructionType::EQUALOO));
        }
        program->add(script::Instruction::newCONSTI(observation.localIndex));
        program->add(script::Instruction::newCONSTO(static_cast<int>(script::kObjectSelf)));
        program->add(script::Instruction::newACTION(setLocalNumber, 3));
    }
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

// Script requests, in the order they were made, and per resref.
std::vector<std::string> &requestLog() {
    static std::vector<std::string> log;
    return log;
}

class C02Conversation : public Conversation {
public:
    C02Conversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}
    void setBarkText(std::string text, float duration) override {}
};

class C02EventsTest : public Test {
protected:
    void SetUp() override {
        requestLog().clear();
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    void countRequests(const std::string &resRef) {
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                requestLog().push_back(key);
                return std::shared_ptr<script::ScriptProgram>();
            }));
    }

    void probe(const std::string &resRef, std::vector<Observation> observations) {
        auto program = probeProgram(*_routines, resRef, observations);
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([program](const std::string &key) {
                requestLog().push_back(key);
                return program;
            }));
    }

    int requests(const std::string &resRef) const {
        return static_cast<int>(std::count(requestLog().begin(), requestLog().end(), resRef));
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPartyMember(const std::string &tag, int npc) {
        auto creature = addCreature(tag);
        creature->setCurrentHitPoints(10);
        if (npc != kNpcPlayer) {
            _game->party().addAvailableMember(npc, creature);
        }
        _game->party().addMember(npc, creature);
        if (npc == kNpcPlayer) {
            _game->party().setPlayer(creature);
        }
        return creature;
    }

    std::shared_ptr<Item> addItemTo(Object &owner, const std::string &tag) {
        auto item = _game->newItem();
        item->setTag(tag);
        owner.addItem(item);
        return item;
    }

    void loadModuleInfo(std::vector<Gff::Field> extra) {
        std::vector<Gff::Field> fields {Gff::Field::newResRef("Mod_Entry_Area", "entryarea")};
        for (auto &field : extra) {
            fields.push_back(std::move(field));
        }
        auto ifo = std::make_shared<Gff>(0xffffffff, std::move(fields));
        TestGameModule::loadModuleInfo(*_game->module(), "c02test", *ifo);
    }

    int invokeInt(const std::string &routine) {
        script::ExecutionContext ctx;
        return _routines->get(_routines->getIndexByName(routine)).invoke({}, ctx).intValue;
    }

    uint32_t invokeObject(const std::string &routine) {
        script::ExecutionContext ctx;
        return _routines->get(_routines->getIndexByName(routine)).invoke({}, ctx).objectId;
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

// A square polygon of side 2 at the origin of its owner.
std::vector<std::shared_ptr<Gff>> squareGeometry(const char *x, const char *y, const char *z) {
    std::vector<std::shared_ptr<Gff>> points;
    for (auto [px, py] : std::vector<std::pair<float, float>> {{0, 0}, {2, 0}, {2, 2}, {0, 2}}) {
        points.push_back(Gff::Builder()
                             .field(Gff::Field::newFloat(x, px))
                             .field(Gff::Field::newFloat(y, py))
                             .field(Gff::Field::newFloat(z, 0.0f))
                             .build());
    }
    return points;
}

std::shared_ptr<Dialog> makeEndDialog() {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "c02_dialog";
    Dialog::EntryReplyLink start;
    start.index = 0;
    dialog->startEntries.push_back(start);
    dialog->entries.resize(2);
    dialog->replies.resize(2);
    dialog->entries[0].text = "first";
    dialog->entries[0].delay = 1;
    Dialog::EntryReplyLink reply0;
    reply0.index = 0;
    Dialog::EntryReplyLink reply1;
    reply1.index = 1;
    dialog->entries[0].replies = {reply0, reply1};
    dialog->replies[0].text = "go on";
    Dialog::EntryReplyLink toLast;
    toLast.index = 1;
    dialog->replies[0].entries.push_back(toLast);
    dialog->replies[1].text = "other";
    dialog->entries[1].text = "last";
    dialog->entries[1].delay = 1;
    return dialog;
}

} // namespace

// --- ifo:Mod_OnPlrDying, ifo:Mod_OnPlrDeath -------------------------------

TEST_F(C02EventsTest, player_death_raises_dying_then_death_in_the_next_update) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnPlrDying", "c02_dying"),
                    Gff::Field::newResRef("Mod_OnPlrDeath", "c02_death")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    probe("c02_dying", {{"GetLastPlayerDying", player->id(), kLocalFirst}});
    probe("c02_death", {{"GetLastPlayerDied", player->id(), kLocalSecond}});

    player->damage(100, nullptr);
    ASSERT_TRUE(player->isDead());

    // Queued on the module, not run inside the damage.
    EXPECT_EQ(0, requests("c02_death"));
    EXPECT_EQ(2u, module->pendingModuleEventCount());

    module->updateEvents(0.0f);

    ASSERT_EQ(2u, requestLog().size());
    EXPECT_EQ("c02_dying", requestLog()[0]);
    EXPECT_EQ("c02_death", requestLog()[1]);
    // The scripts run as the module.
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, module->getLocalNumber(kLocalSecond));
    // The record stays for the routines after the scripts.
    EXPECT_EQ(player->id(), invokeObject("GetLastPlayerDied"));
    EXPECT_EQ(player->id(), invokeObject("GetLastPlayerDying"));
}

TEST_F(C02EventsTest, player_death_is_not_raised_for_a_creature_outside_the_party) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnPlrDeath", "c02_death")});
    addPartyMember("player", kNpcPlayer);
    countRequests("c02_death");
    auto enemy = addCreature("enemy");

    enemy->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);

    EXPECT_TRUE(enemy->isDead());
    EXPECT_EQ(0, requests("c02_death"));
    EXPECT_EQ(script::kObjectInvalid, invokeObject("GetLastPlayerDied"));
}

TEST_F(C02EventsTest, player_death_routines_report_invalid_before_any_death) {
    for (const char *name : {"GetLastPlayerDied", "GetLastPlayerDying", "GetLastRespawnButtonPresser",
                             "GetModuleItemAcquired", "GetModuleItemAcquiredFrom",
                             "GetModuleItemLost", "GetModuleItemLostBy"}) {
        EXPECT_EQ(script::kObjectInvalid, invokeObject(name)) << name;
    }
}

TEST_F(C02EventsTest, player_death_immortal_player_survives_zero_hit_points) {
    auto player = addPartyMember("player", kNpcPlayer);
    player->setImmortal(true);

    player->damage(100, nullptr);

    EXPECT_FALSE(player->isDead());
    EXPECT_EQ(0u, _game->module()->pendingModuleEventCount());
}

TEST_F(C02EventsTest, player_death_immortal_player_option_defaults_off) {
    EXPECT_FALSE(GameOptions().immortalPlayer);
}

// --- party death: leadership, recovery ------------------------------------

TEST_F(C02EventsTest, player_death_leader_falls_control_passes_to_a_standing_member) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    ASSERT_EQ(player, _game->party().getLeader());

    player->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);

    EXPECT_EQ(trask, _game->party().getLeader());
    EXPECT_EQ(PartyDefeatState::None, _game->module()->defeatState());
}

TEST_F(C02EventsTest, player_death_fallen_member_gets_up_at_one_hp_once_no_enemy_is_near) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);

    trask->damage(100, nullptr);
    ASSERT_TRUE(trask->isDead());
    _game->module()->updateEvents(0.0f);

    EXPECT_FALSE(trask->isDead());
    EXPECT_EQ(1, trask->currentHitPoints());
}

TEST_F(C02EventsTest, player_death_fallen_member_stays_down_while_an_enemy_is_near) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    auto enemy = addCreature("enemy");
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([enemy](const Creature &a, const Creature &b) {
            return &a == enemy.get() || &b == enemy.get();
        }));

    player->setObjectSeen(enemy, true);

    trask->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);
    EXPECT_TRUE(trask->isDead());

    enemy->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);
    EXPECT_FALSE(trask->isDead());
}

// An enemy that is near but out of the fight (not in combat, not perceiving
// the party nor perceived by it: a dormant turret beyond a wall) does not keep
// a fallen member down (tar_m09aa, the elevator turrets).
TEST_F(C02EventsTest, player_death_fallen_member_gets_up_beside_an_unaware_enemy) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    auto enemy = addCreature("enemy");
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([enemy](const Creature &a, const Creature &b) {
            return &a == enemy.get() || &b == enemy.get();
        }));

    trask->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);
    EXPECT_FALSE(trask->isDead());
    EXPECT_EQ(1, trask->currentHitPoints());
}

// A standing member left "in combat" with nothing to fight is stood down, so
// the fallen get up (tar_m09aa: T3-M4 stayed in combat after the halls were
// cleared and the PC lay at 0 HP for good).
TEST_F(C02EventsTest, player_death_leftover_combat_is_stood_down_and_the_fallen_get_up) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto t3 = addPartyMember("t3m4", 7);
    t3->activateCombat();
    ASSERT_TRUE(t3->isInCombat());

    player->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);

    EXPECT_FALSE(t3->isInCombat());
    EXPECT_FALSE(player->isDead());
    EXPECT_EQ(1, player->currentHitPoints());
}

// While an enemy is still in the fight, a member's combat state stays.
TEST_F(C02EventsTest, player_death_combat_stays_while_an_enemy_is_perceived) {
    loadModuleInfo({});
    auto player = addPartyMember("player", kNpcPlayer);
    auto t3 = addPartyMember("t3m4", 7);
    auto enemy = addCreature("enemy");
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([enemy](const Creature &a, const Creature &b) {
            return &a == enemy.get() || &b == enemy.get();
        }));
    t3->setObjectSeen(enemy, true);
    t3->activateCombat();

    player->damage(100, nullptr);
    _game->module()->updateEvents(0.0f);

    EXPECT_TRUE(t3->isInCombat());
    EXPECT_TRUE(player->isDead());
}

// --- defeat, ifo:Mod_OnSpawnBtnDn, EndGame --------------------------------

TEST_F(C02EventsTest, defeat_every_member_down_fades_presses_respawn_then_ends_the_game) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnSpawnBtnDn", "c02_respawn")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    probe("c02_respawn", {{"GetLastRespawnButtonPresser", player->id(), kLocalFirst}});

    player->damage(100, nullptr);
    trask->damage(100, nullptr);
    module->updateEvents(0.0f);
    EXPECT_EQ(PartyDefeatState::Fading, module->defeatState());

    // The fade takes three seconds of simulation time (90 ticks at 30 Hz);
    // the respawn press is raised then and its script runs a tick later.
    int ticks = 0;
    while (requests("c02_respawn") == 0 && ticks < 200) {
        EXPECT_FALSE(_game->endGameRequested());
        module->updateEvents(1.0f / 30.0f);
        ++ticks;
    }
    EXPECT_GE(ticks, 90);
    EXPECT_LE(ticks, 92);
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));

    // Nobody stood up: the host shows the death screen at the next tick.
    EXPECT_EQ(PartyDefeatState::Ended, module->defeatState());
    EXPECT_TRUE(_game->partyDefeatPending());
    EXPECT_FALSE(_game->endGameRequested());
}

TEST_F(C02EventsTest, defeat_is_cancelled_when_somebody_stands_up_again) {
    loadModuleInfo({});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);

    player->damage(100, nullptr);
    module->updateEvents(0.0f);
    ASSERT_EQ(PartyDefeatState::Fading, module->defeatState());

    // A respawn script (or any script) resurrects the player.
    ASSERT_TRUE(player->resurrect(0));
    module->updateEvents(0.0f);

    EXPECT_EQ(PartyDefeatState::None, module->defeatState());
    EXPECT_FALSE(_game->endGameRequested());
}

TEST_F(C02EventsTest, end_game_routine_requests_the_end_for_the_next_tick) {
    script::ExecutionContext ctx;
    _routines->get(_routines->getIndexByName("EndGame")).invoke({script::Variable::ofInt(1)}, ctx);
    EXPECT_TRUE(_game->endGameRequested());
}

// --- ifo:Mod_OnAcquirItem, ifo:Mod_OnUnAqreItem ---------------------------

TEST_F(C02EventsTest, acquire_item_given_to_a_party_member_records_item_and_source) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnAcquirItem", "c02_acquire")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    auto giver = addCreature("giver");
    auto item = addItemTo(*giver, "ptar_sitharmor");
    probe("c02_acquire", {{"GetModuleItemAcquired", item->id(), kLocalFirst},
                          {"GetModuleItemAcquiredFrom", giver->id(), kLocalSecond}});

    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, item, *player, giver->id()));
    EXPECT_EQ(0, requests("c02_acquire"));
    module->updateEvents(0.0f);

    EXPECT_EQ(1, requests("c02_acquire"));
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, module->getLocalNumber(kLocalSecond));
}

TEST_F(C02EventsTest, acquire_item_taken_from_a_container_reports_the_container) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnAcquirItem", "c02_acquire")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    auto locker = _game->newPlaceable();
    _area->add(locker);
    auto first = addItemTo(*locker, "first");
    auto second = addItemTo(*locker, "second");
    first->setDropable(true);
    second->setDropable(true);
    countRequests("c02_acquire");

    locker->moveDropableItemsTo(*player);
    module->updateEvents(0.0f);

    // One event per item.
    EXPECT_EQ(2, requests("c02_acquire"));
    EXPECT_EQ(second->id(), invokeObject("GetModuleItemAcquired"));
    EXPECT_EQ(locker->id(), invokeObject("GetModuleItemAcquiredFrom"));
}

TEST_F(C02EventsTest, acquire_item_created_on_a_party_member_has_no_source) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnAcquirItem", "c02_acquire")});
    auto player = addPartyMember("player", kNpcPlayer);
    auto item = addItemTo(*player, "g_i_progspike01");
    countRequests("c02_acquire");

    raisePartyItemEvents(*_game, item->tag(), item, nullptr, player.get());
    _game->module()->updateEvents(0.0f);

    EXPECT_EQ(1, requests("c02_acquire"));
    EXPECT_EQ(item->id(), invokeObject("GetModuleItemAcquired"));
    EXPECT_EQ(script::kObjectInvalid, invokeObject("GetModuleItemAcquiredFrom"));
}

TEST_F(C02EventsTest, acquire_item_moves_outside_or_within_the_party_raise_nothing) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnAcquirItem", "c02_acquire"),
                    Gff::Field::newResRef("Mod_OnUnAqreItem", "c02_lose")});
    auto player = addPartyMember("player", kNpcPlayer);
    auto trask = addPartyMember("trask", 0);
    auto a = addCreature("a");
    auto b = addCreature("b");
    auto item = addItemTo(*a, "thing");
    auto own = addItemTo(*player, "own");
    countRequests("c02_acquire");
    countRequests("c02_lose");

    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, item, *b, a->id()));
    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, own, *trask, player->id()));
    _game->module()->updateEvents(0.0f);

    EXPECT_EQ(0, requests("c02_acquire"));
    EXPECT_EQ(0, requests("c02_lose"));
}

TEST_F(C02EventsTest, lose_item_given_away_by_a_party_member_records_item_and_loser) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnUnAqreItem", "c02_lose")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    auto npc = addCreature("npc");
    auto item = addItemTo(*player, "k_computer_spike");
    probe("c02_lose", {{"GetModuleItemLost", item->id(), kLocalFirst},
                       {"GetModuleItemLostBy", player->id(), kLocalSecond}});

    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, item, *npc, player->id()));
    module->updateEvents(0.0f);

    EXPECT_EQ(1, requests("c02_lose"));
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, module->getLocalNumber(kLocalSecond));
}

// --- ifo:Mod_OnClientEntr, ifo:Mod_OnClientLeav ---------------------------

TEST_F(C02EventsTest, client_enter_runs_as_the_module_with_the_entering_leader) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnClientEntr", "c02_enter")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    probe("c02_enter", {{"GetEnteringObject", player->id(), kLocalFirst}});

    module->runOnClientEnterScript(*player);

    EXPECT_EQ(1, requests("c02_enter"));
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
}

TEST_F(C02EventsTest, client_leave_runs_as_the_module_with_the_exiting_leader) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnClientLeav", "c02_leave")});
    auto module = _game->module();
    auto player = addPartyMember("player", kNpcPlayer);
    probe("c02_leave", {{"GetExitingObject", player->id(), kLocalFirst}});

    module->runOnClientLeaveScript(*player);

    EXPECT_EQ(1, requests("c02_leave"));
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
}

// --- ifo:Mod_OnHeartbeat ---------------------------------------------------

TEST_F(C02EventsTest, module_heartbeat_runs_every_six_seconds_of_simulation) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnHeartbeat", "c02_heartbeat")});
    auto module = _game->module();
    countRequests("c02_heartbeat");

    module->updateEvents(5.9f);
    EXPECT_EQ(0, requests("c02_heartbeat"));
    module->updateEvents(0.2f);
    EXPECT_EQ(1, requests("c02_heartbeat"));
    module->updateEvents(5.8f);
    EXPECT_EQ(1, requests("c02_heartbeat"));
    module->updateEvents(0.2f);
    EXPECT_EQ(2, requests("c02_heartbeat"));
}

// --- utp:OnClosed -----------------------------------------------------------

TEST_F(C02EventsTest, placeable_closed_runs_on_the_next_update_with_the_closer) {
    auto player = addPartyMember("player", kNpcPlayer);
    // utp:OnClosed
    auto locker = _game->newPlaceable();
    locker->setOnClosed("c02_closed");
    _area->add(locker);
    probe("c02_closed", {{"GetLastClosedBy", player->id(), kLocalFirst}});

    locker->signalClosed(player->id());
    EXPECT_FALSE(locker->isOpen());
    EXPECT_EQ(0, requests("c02_closed"));
    EXPECT_EQ(1u, locker->pendingScriptEventCount());

    locker->update(0.0f);

    EXPECT_EQ(1, requests("c02_closed"));
    EXPECT_EQ(1, locker->getLocalNumber(kLocalFirst));
    EXPECT_EQ(0u, locker->pendingScriptEventCount());
}

TEST_F(C02EventsTest, door_closed_by_the_close_action_reports_the_closer) {
    auto npc = addCreature("npc");
    auto door = _game->newDoor();
    door->setOnClosed("c02_door_closed");
    door->open();
    ASSERT_TRUE(door->isOpen());
    probe("c02_door_closed", {{"GetLastClosedBy", npc->id(), kLocalFirst}});
    npc->setPosition(door->position());

    auto action = _game->newAction<CloseDoorAction>(door);
    action->execute(action, *npc, 0.0f);

    EXPECT_EQ(1, requests("c02_door_closed"));
    EXPECT_EQ(1, door->getLocalNumber(kLocalFirst));
}

// --- utp:OnMeleeAttacked ----------------------------------------------------

TEST_F(C02EventsTest, placeable_melee_attacked_reports_the_attacker) {
    auto attacker = addCreature("attacker");
    auto target = _game->newPlaceable();
    target->setOnMeleeAttacked("c02_attacked"); // utp:OnMeleeAttacked
    probe("c02_attacked", {{"GetLastAttacker", attacker->id(), kLocalFirst}});

    target->runMeleeAttackedScript(attacker->id());

    EXPECT_EQ(1, requests("c02_attacked"));
    EXPECT_EQ(1, target->getLocalNumber(kLocalFirst));
}

// --- utp:OnEndDialogue ------------------------------------------------------

TEST_F(C02EventsTest, placeable_end_dialogue_every_placeable_hears_a_normal_end) {
    auto player = addPartyMember("player", kNpcPlayer);
    auto terminal = _game->newPlaceable();
    terminal->setOnEndDialogue("c02_enddlg"); // utp:OnEndDialogue
    _area->add(terminal);
    countRequests("c02_enddlg");

    C02Conversation conversation(*_game, _engine.services());
    conversation.start(makeEndDialog(), terminal);
    ASSERT_TRUE(conversation.isRunning());
    conversation.updateSimulation(2.0f);
    ASSERT_TRUE(conversation.pickReplyAtRow(0));
    conversation.updateSimulation(2.0f);
    ASSERT_FALSE(conversation.isRunning());

    EXPECT_EQ(0, requests("c02_enddlg"));
    EXPECT_EQ(1u, terminal->pendingScriptEventCount());
    terminal->update(0.0f);
    EXPECT_EQ(1, requests("c02_enddlg"));
}

// --- ute:OnEntered ------------------------------------------------------------

TEST_F(C02EventsTest, encounter_entered_fires_once_per_entry_even_when_inactive) {
    auto player = addPartyMember("player", kNpcPlayer);
    auto encounter = _game->newEncounter();
    auto gff = Gff::Builder()
                   .field(Gff::Field::newResRef("OnEntered", "c02_enc_enter"))
                   .field(Gff::Field::newResRef("OnExit", "c02_enc_exit"))
                   .field(Gff::Field::newByte("Active", 0))
                   .field(Gff::Field::newByte("PlayerOnly", 1))
                   .field(Gff::Field::newFloat("XPosition", 10.0f))
                   .field(Gff::Field::newFloat("YPosition", 10.0f))
                   .field(Gff::Field::newList("Geometry", squareGeometry("X", "Y", "Z")))
                   .build();
    encounter->deserialize(*gff, SerializedIdentityContext::templateResource());
    _area->add(encounter);
    probe("c02_enc_enter", {{"GetEnteringObject", player->id(), kLocalFirst}});
    countRequests("c02_enc_exit");

    player->setPosition(glm::vec3(0.0f));
    encounter->update(0.0f);
    EXPECT_EQ(0, requests("c02_enc_enter"));

    player->setPosition(glm::vec3(11.0f, 11.0f, 0.0f));
    encounter->update(0.0f);
    encounter->update(0.0f);
    EXPECT_EQ(1, requests("c02_enc_enter"));
    EXPECT_EQ(1, encounter->getLocalNumber(kLocalFirst));
    EXPECT_TRUE(encounter->isOccupant(player->id()));

    player->setPosition(glm::vec3(20.0f, 20.0f, 0.0f));
    encounter->update(0.0f);
    EXPECT_EQ(1, requests("c02_enc_exit"));
}

TEST_F(C02EventsTest, encounter_entered_player_only_ignores_other_creatures) {
    auto npc = addCreature("npc");
    auto encounter = _game->newEncounter();
    auto gff = Gff::Builder()
                   .field(Gff::Field::newResRef("OnEntered", "c02_enc_enter"))
                   .field(Gff::Field::newByte("PlayerOnly", 1))
                   .field(Gff::Field::newList("Geometry", squareGeometry("X", "Y", "Z")))
                   .build();
    encounter->deserialize(*gff, SerializedIdentityContext::templateResource());
    _area->add(encounter);
    countRequests("c02_enc_enter");

    npc->setPosition(glm::vec3(1.0f, 1.0f, 0.0f));
    encounter->update(0.0f);

    EXPECT_EQ(0, requests("c02_enc_enter"));
}

// --- utd:OnClick, utt:OnClick ---------------------------------------------

TEST_F(C02EventsTest, door_click_runs_first_when_a_party_member_reaches_the_door) {
    auto player = addPartyMember("player", kNpcPlayer);
    auto door = _game->newDoor();
    door->setOnClick("c02_door_click"); // utd:OnClick
    door->setOnFailToOpen("c02_door_fail");
    door->setLocked(true);
    probe("c02_door_click", {{"GetClickingObject", player->id(), kLocalFirst}});
    countRequests("c02_door_fail");
    player->setPosition(door->position());

    auto action = _game->newAction<OpenDoorAction>(door);
    action->execute(action, *player, 0.0f);

    ASSERT_EQ(2u, requestLog().size());
    EXPECT_EQ("c02_door_click", requestLog()[0]);
    EXPECT_EQ("c02_door_fail", requestLog()[1]);
    EXPECT_EQ(1, door->getLocalNumber(kLocalFirst));
}

TEST_F(C02EventsTest, door_click_is_not_run_for_a_creature_outside_the_party) {
    addPartyMember("player", kNpcPlayer);
    auto npc = addCreature("npc");
    auto door = _game->newDoor();
    door->setOnClick("c02_door_click");
    countRequests("c02_door_click");
    npc->setPosition(door->position());

    auto action = _game->newAction<OpenDoorAction>(door);
    action->execute(action, *npc, 0.0f);

    EXPECT_EQ(0, requests("c02_door_click"));
}

TEST_F(C02EventsTest, trigger_click_is_loaded_and_runs_with_the_clicking_leader) {
    auto player = addPartyMember("player", kNpcPlayer);
    auto trigger = _game->newTrigger();
    auto gff = Gff::Builder()
                   .field(Gff::Field::newResRef("OnClick", "c02_trigger_click"))
                   .field(Gff::Field::newResRef("LinkedToModule", "danm14ac"))
                   .build();
    trigger->deserialize(*gff, SerializedIdentityContext::templateResource());
    ASSERT_TRUE(trigger->hasClickScript());
    probe("c02_trigger_click", {{"GetClickingObject", player->id(), kLocalFirst}});

    trigger->runClickScript(player->id());

    EXPECT_EQ(1, requests("c02_trigger_click"));
    EXPECT_EQ(1, trigger->getLocalNumber(kLocalFirst));
}

// --- utc:ScriptRested -------------------------------------------------------

TEST_F(C02EventsTest, rested_restores_the_creature_and_runs_its_script_and_the_module_rest) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnPlrRest", "c02_plr_rest")});
    auto player = addPartyMember("player", kNpcPlayer);
    player->setOnRested("c02_rested"); // utc:ScriptRested
    player->setCurrentHitPoints(1);
    countRequests("c02_rested");
    countRequests("c02_plr_rest");

    auto action = _game->newAction<RestAction>();
    action->execute(action, *player, 0.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(1, requests("c02_rested"));
    EXPECT_FALSE(player->isDead());
    EXPECT_EQ(std::max(1, player->maxHitPoints()), player->currentHitPoints());

    _game->module()->updateEvents(0.0f);
    EXPECT_EQ(1, requests("c02_plr_rest"));
}

TEST_F(C02EventsTest, rested_is_refused_with_a_hostile_creature_near) {
    auto player = addPartyMember("player", kNpcPlayer);
    auto enemy = addCreature("enemy");
    auto &reputes = static_cast<MockReputes &>(_engine.services().game.reputes);
    EXPECT_CALL(reputes, getIsEnemy(An<const Creature &>(), An<const Creature &>()))
        .Times(AnyNumber())
        .WillRepeatedly(Return(true));
    player->setOnRested("c02_rested");
    countRequests("c02_rested");

    auto action = _game->newAction<RestAction>();
    action->execute(action, *player, 0.0f);

    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(0, requests("c02_rested"));
}

// tar_m03ad k_ptar_puzzlebox reads GetLocked(OBJECT_SELF) on the puzzle box,
// and k_ptar_openbox / tar03_holo* lock and unlock placeables: SetLocked and
// GetLocked take a door or a placeable.
TEST_F(C02EventsTest, set_and_get_locked_work_on_placeables) {
    auto box = _game->newPlaceable();
    box->setHasInventory(true);
    _area->add(box);
    script::ExecutionContext ctx;
    auto &setLocked = _routines->get(_routines->getIndexByName("SetLocked"));
    auto &getLocked = _routines->get(_routines->getIndexByName("GetLocked"));

    setLocked.invoke({script::Variable::ofObject(box->id()), script::Variable::ofInt(1)}, ctx);
    EXPECT_TRUE(box->isLocked());
    EXPECT_EQ(1, getLocked.invoke({script::Variable::ofObject(box->id())}, ctx).intValue);

    setLocked.invoke({script::Variable::ofObject(box->id()), script::Variable::ofInt(0)}, ctx);
    EXPECT_FALSE(box->isLocked());
    EXPECT_EQ(0, getLocked.invoke({script::Variable::ofObject(box->id())}, ctx).intValue);
}
