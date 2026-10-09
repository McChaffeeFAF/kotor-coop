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

// Example tests that compile small NWScript sources (test/support/nsscomp.h) and
// run them against game fixtures: the scripts call the engine's real routines
// through the real routine table. See docs/testing_scripts.md.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../support/nsscomp.h"

#include "reone/game/action/opendoor.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::testsupport;
using namespace testing;

namespace {

class NssScriptsTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    // Compiles source and serves it to the engine under resRef, through the NCS
    // file format, as if the game had loaded it from a module.
    void script(const std::string &resRef, const std::string &source) {
        auto program = compileNssRoundTrip(resRef, source, *_routines);
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Return(program));
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setCurrentHitPoints(10);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPlayer() {
        auto player = addCreature("player");
        _game->party().addMember(kNpcPlayer, player);
        _game->party().setPlayer(player);
        return player;
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

} // namespace

// A trigger's OnEnter that has an NPC start a conversation with whoever walked in.
TEST_F(NssScriptsTest, a_trigger_on_enter_has_the_npc_start_a_conversation_with_the_creature_that_entered) {
    auto player = addPlayer();
    auto npc = addCreature("guide");
    script("trig_conv",
           "void main() {\n"
           "    object oEnterer = GetEnteringObject();\n"
           "    object oGuide = GetObjectByTag(\"guide\");\n"
           "    if (GetIsObjectValid(oGuide) && GetLocalNumber(OBJECT_SELF, 1) == 0) {\n"
           "        SetLocalNumber(OBJECT_SELF, 1, 1);\n"
           "        AssignCommand(oGuide, ActionStartConversation(oEnterer, \"guide_dlg\"));\n"
           "    }\n"
           "}\n");
    auto trigger = _game->newTrigger();
    auto gff = Gff::Builder()
                   .field(Gff::Field::newResRef("ScriptOnEnter", "trig_conv"))
                   .build();
    trigger->deserialize(*gff, SerializedIdentityContext::templateResource());
    _area->add(trigger);

    trigger->addTenant(player);

    ASSERT_EQ(1u, npc->actions().size()) << "the guide queued one action";
    auto action = std::dynamic_pointer_cast<StartConversationAction>(npc->actions().front());
    ASSERT_NE(nullptr, action);
    EXPECT_EQ(1, trigger->getLocalNumber(1)) << "the script's own bookkeeping ran";
    EXPECT_TRUE(player->actions().empty());

    // Entering again does nothing: the script guards itself with a local.
    trigger->addTenant(player);
    EXPECT_EQ(1u, npc->actions().size());
}

// A locked door's OnFailToOpen that counts the attempts and gives way on the third.
TEST_F(NssScriptsTest, a_door_on_fail_to_open_counts_attempts_and_unlocks_on_the_third) {
    auto player = addPlayer();
    script("door_fail",
           "void main() {\n"
           "    int nTries = GetLocalNumber(OBJECT_SELF, 1) + 1;\n"
           "    SetLocalNumber(OBJECT_SELF, 1, nTries);\n"
           "    if (nTries >= 3) {\n"
           "        SetLocked(OBJECT_SELF, FALSE);\n"
           "    }\n"
           "}\n");
    auto door = _game->newDoor();
    door->setOnFailToOpen("door_fail");
    door->setLocked(true);
    _area->add(door);
    player->setPosition(door->position());

    for (int attempt = 1; attempt <= 2; ++attempt) {
        auto action = _game->newAction<OpenDoorAction>(door);
        action->execute(action, *player, 0.0f);
        EXPECT_EQ(attempt, door->getLocalNumber(1));
        EXPECT_TRUE(door->isLocked()) << "attempt " << attempt;
    }
    auto third = _game->newAction<OpenDoorAction>(door);
    third->execute(third, *player, 0.0f);
    EXPECT_EQ(3, door->getLocalNumber(1));
    EXPECT_FALSE(door->isLocked());
}

// A conditional script, as a conversation or a trigger asks one: its return is the answer.
TEST_F(NssScriptsTest, a_starting_conditional_answers_from_the_game_state) {
    auto player = addPlayer();
    script("cond_wounded",
           "int StartingConditional() {\n"
           "    return GetCurrentHitPoints(GetObjectByTag(\"player\")) < 5;\n"
           "}\n");
    player->setMaxHitPoints(10);
    player->setCurrentHitPoints(10);
    EXPECT_EQ(0, _game->scriptRunner().run("cond_wounded", player->id()));
    player->setCurrentHitPoints(3);
    EXPECT_EQ(1, _game->scriptRunner().run("cond_wounded", player->id()));
}

// String building and a global write, through the engine's own routines.
TEST_F(NssScriptsTest, a_script_can_build_strings_and_set_globals_through_the_engine) {
    auto player = addPlayer();
    script("global_set",
           "void main() {\n"
           "    string sName = \"quest_\" + IntToString(2 + 3);\n"
           "    SetGlobalNumber(sName, 7);\n"
           "}\n");
    _game->scriptRunner().run("global_set", player->id());
    EXPECT_EQ(7, _game->getGlobalNumber("quest_5"));
}
