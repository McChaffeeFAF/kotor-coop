/*
 * Copyright (c) 2020-2023 The reone project contributors
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

// A companion leading the party hands a conversation to the PC (playtest 2,
// item 28; design note c11_data).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/movetopoint.h"
#include "reone/game/conversationleader.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class ConversationLeaderTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        TwoDA::Builder appearance;
        appearance.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
        appearance.row({"S", "1", "1", "-1", "", "", ""});
        std::shared_ptr<TwoDA> table(appearance.build());
        ON_CALL(_engine.resourceModule().twoDas(), get("appearance")).WillByDefault(Return(table));
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);

        _pc = addCreature("player", glm::vec3(1.0f, 0.0f, 0.0f));
        _game->party().addMember(kNpcPlayer, _pc);
        _game->party().setPlayer(_pc);
        _game->party().setActualPlayer(_pc);

        _carth = addCreature("carth", glm::vec3(5.0f, 5.0f, 0.0f));
        _game->party().addAvailableMember(2, _carth);
        _game->party().addMember(2, _carth);
        _game->party().setPartyLeader(2);
        ASSERT_EQ(_game->party().getLeader(), _carth);
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, glm::vec3 position, bool partyInteract = false) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", tag))
                       .field(Gff::Field::newDword("Appearance_Type", 0))
                       .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                       .field(Gff::Field::newByte("BodyBag", 0xff))
                       .field(Gff::Field::newByte("PerceptionRange", 0xff))
                       .field(Gff::Field::newByte("PartyInteract", partyInteract ? 1 : 0))
                       .build();
        auto creature = _game->newCreature();
        creature->deserialize(*gff, SerializedIdentityContext::templateResource());
        creature->setCurrentHitPoints(10);
        creature->setPosition(position);
        _area->add(creature);
        return creature;
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _pc;
    std::shared_ptr<Creature> _carth;
};

} // namespace

TEST_F(ConversationLeaderTest, a_leading_companion_hands_an_npc_conversation_to_the_pc) {
    auto zelka = addCreature("zelka021", glm::vec3(9.0f, 5.0f, 0.0f));

    EXPECT_TRUE(handConversationToPlayer(*_game, _carth, zelka));

    EXPECT_EQ(_game->party().getLeader(), _pc);
    // The PC stands where the companion stood, and the other way round.
    EXPECT_EQ(_pc->position(), glm::vec3(5.0f, 5.0f, 0.0f));
    EXPECT_EQ(_carth->position(), glm::vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(_game->party().postDialogSwitchBack(), _carth->id());
}

TEST_F(ConversationLeaderTest, control_goes_back_to_the_companion_when_the_conversation_ends) {
    auto zelka = addCreature("zelka021", glm::vec3(9.0f, 5.0f, 0.0f));
    ASSERT_TRUE(handConversationToPlayer(*_game, _carth, zelka));

    switchBackAfterConversation(*_game);

    EXPECT_EQ(_game->party().getLeader(), _carth);
    EXPECT_EQ(_game->party().postDialogSwitchBack(), 0u);
}

TEST_F(ConversationLeaderTest, cancelled_switch_back_leaves_the_pc_in_control) {
    auto zelka = addCreature("zelka021", glm::vec3(9.0f, 5.0f, 0.0f));
    ASSERT_TRUE(handConversationToPlayer(*_game, _carth, zelka));

    _game->party().setPostDialogSwitchBack(0); // CancelPostDialogCharacterSwitch
    switchBackAfterConversation(*_game);

    EXPECT_EQ(_game->party().getLeader(), _pc);
}

TEST_F(ConversationLeaderTest, a_party_interact_object_talks_to_the_companion) {
    auto droid = addCreature("helper", glm::vec3(9.0f, 5.0f, 0.0f), /*partyInteract=*/true);

    EXPECT_FALSE(handConversationToPlayer(*_game, _carth, droid));

    EXPECT_EQ(_game->party().getLeader(), _carth);
    EXPECT_EQ(_carth->position(), glm::vec3(5.0f, 5.0f, 0.0f));
}

TEST_F(ConversationLeaderTest, the_pc_leading_keeps_its_conversation) {
    _game->party().setPartyLeader(kNpcPlayer);
    auto zelka = addCreature("zelka021", glm::vec3(9.0f, 5.0f, 0.0f));

    EXPECT_FALSE(handConversationToPlayer(*_game, _pc, zelka));
    EXPECT_EQ(_game->party().postDialogSwitchBack(), 0u);
}

TEST_F(ConversationLeaderTest, a_companion_talking_to_a_party_member_is_not_handed_over) {
    EXPECT_FALSE(handConversationToPlayer(*_game, _carth, _pc));
    EXPECT_EQ(_game->party().getLeader(), _carth);
}

// --- Leader switch and action queues ------------------------------------------

// The owner's Taris back room (playtest 2, item 33): Carth leads and is sent to
// pick the lab door; the player switches back to the PC before he gets there.
// His order must survive the switch, or the door never opens and the scene
// behind it is lost.
TEST_F(ConversationLeaderTest, a_players_order_survives_a_leader_switch_out_of_combat) {
    auto order = _game->newAction<MoveToPointAction>(glm::vec3(20.0f, 0.0f, 0.0f));
    order->setUserAction(true);
    _carth->addAction(order);

    _game->party().setPartyLeader(kNpcPlayer);

    ASSERT_EQ(_game->party().getLeader(), _pc);
    EXPECT_EQ(_carth->getCurrentAction(), order);
}

TEST_F(ConversationLeaderTest, automatic_actions_are_reset_by_a_leader_switch) {
    auto automatic = _game->newAction<MoveToPointAction>(glm::vec3(20.0f, 0.0f, 0.0f));
    _carth->addAction(automatic);

    _game->party().setPartyLeader(kNpcPlayer);

    EXPECT_EQ(_carth->getCurrentAction(), nullptr);
}
