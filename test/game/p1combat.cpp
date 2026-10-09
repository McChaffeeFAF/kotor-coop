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

// Playtest 1 combat fixes (batch P1-Combat, design note p1_combat).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/attack.h"
#include "reone/game/combat.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

// The K1 repute.2da rows that matter here: row index is the faction id, and
// there is no Player column (the Player row supplies it).
std::shared_ptr<TwoDA> makeK1Repute() {
    std::vector<std::string> columns {"label", "hostile_1", "friendly_1", "hostile_2", "friendly_2", "neutral"};
    std::vector<TwoDA::Row> rows {
        TwoDA::newRow("0", {"player", "0", "100", "0", "100", "50"}),
        TwoDA::newRow("1", {"hostile_1", "100", "0", "0", "0", "50"}),
        TwoDA::newRow("2", {"friendly_1", "0", "100", "0", "0", "50"}),
        TwoDA::newRow("3", {"hostile_2", "0", "0", "100", "0", "50"}),
        TwoDA::newRow("4", {"friendly_2", "0", "0", "0", "100", "50"}),
        TwoDA::newRow("5", {"neutral", "50", "50", "50", "50", "100"})};
    return std::make_shared<TwoDA>(std::move(columns), std::move(rows));
}

std::unique_ptr<Reputes> makeReputes(TestEngine &engine) {
    EXPECT_CALL(engine.resourceModule().twoDas(), get("repute"))
        .WillOnce(Return(makeK1Repute()));
    auto reputes = std::make_unique<Reputes>(engine.services().resource.twoDas);
    reputes->init();
    return reputes;
}

} // namespace

// Item 7: Trask's template is faction Neutral. The Sith (Hostile_1) regard
// Neutral as 50, so neither side attacked the other, and with Trask leading
// the Sith looked friendly. A party member counts as the player faction.
TEST(P1Combat, PartyMemberUsesPlayerFactionForReputation) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto reputes = makeReputes(engine);

    auto pc = game.newCreature();
    pc->setFaction(Faction::Player);
    auto trask = game.newCreature();
    trask->setFaction(Faction::Neutral);
    auto sith = game.newCreature();
    sith->setFaction(Faction::Hostile1);

    // Outside the party Trask is neutral to the Sith.
    EXPECT_FALSE(reputes->getIsEnemy(*sith, *trask));
    EXPECT_FALSE(reputes->getIsEnemy(*trask, *sith));
    EXPECT_EQ(Faction::Neutral, trask->reputationFaction());

    game.party().addMember(kNpcPlayer, pc);
    game.party().addAvailableMember(0, trask);
    ASSERT_TRUE(game.party().addMember(0, trask));

    EXPECT_EQ(Faction::Player, trask->reputationFaction());
    EXPECT_TRUE(reputes->getIsEnemy(*sith, *trask));
    EXPECT_TRUE(reputes->getIsEnemy(*trask, *sith));
    EXPECT_TRUE(reputes->getIsFriend(*trask, *pc));
    EXPECT_TRUE(reputes->getIsFriend(*pc, *trask));
    // The stored faction is untouched.
    EXPECT_EQ(Faction::Neutral, trask->faction());

    // Leaving the party restores the template faction's view.
    game.party().removeMember(0);
    EXPECT_FALSE(reputes->getIsEnemy(*sith, *trask));
}

// Item 18: the Endar Spire troopers' end_onedam rifles have the no-damage
// property plus +1 damage. The one-point floor on the weapon's own damage
// made every hit deal 2 (3 on a critical) instead of 1.
TEST(P1Combat, NoDamageWeaponHasNoBaseFloor) {
    EXPECT_EQ(0, weaponBaseDamage(0, /*noDamage=*/true));
    EXPECT_EQ(0, weaponBaseDamage(-1, /*noDamage=*/true));
    EXPECT_EQ(2, weaponBaseDamage(2, /*noDamage=*/true));
    // An ordinary weapon keeps the one-point floor.
    EXPECT_EQ(1, weaponBaseDamage(0, /*noDamage=*/false));
    EXPECT_EQ(1, weaponBaseDamage(-3, /*noDamage=*/false));
    EXPECT_EQ(5, weaponBaseDamage(5, /*noDamage=*/false));
}

// Item 11: a round dropped before it finished (a leader switch clears every
// member's queue) left both sides in combat mode for good, with nothing to
// end it. It now releases its participants as a finished round does.
TEST(P1Combat, CancelledRoundLetsParticipantsLeaveCombat) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);
    attacker->addAction(action);
    game.combat().addAction(action, *attacker);
    ASSERT_TRUE(attacker->isInCombat());
    ASSERT_TRUE(target->isInCombat());

    attacker->clearAllActions();
    game.combat().update(0.0f);
    EXPECT_EQ(0u, game.combat().roundCount());

    attacker->update(9.0f);
    target->update(9.0f);
    EXPECT_FALSE(attacker->isInCombat());
    EXPECT_FALSE(target->isInCombat());
}

// Item 11: Cancel Combat clears the queue and leaves combat mode at once.
TEST(P1Combat, CancelCombatLeavesCombatNow) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto action = game.newAction<AttackObjectAction>(target);
    attacker->addAction(action);
    game.combat().addAction(action, *attacker);
    ASSERT_TRUE(attacker->isInCombat());

    attacker->cancelCombatNow();
    EXPECT_FALSE(attacker->isInCombat());
    EXPECT_TRUE(attacker->actions().empty());
    EXPECT_FALSE(attacker->getAttackTarget());
}

// Item 10: auto-combat is per character and off by default. A leader without
// it drops a pending hand-over (the player is in charge); with it, the
// hand-over stays until the queue is empty.
TEST(P1Combat, AutoCombatIsPerCharacterAndGatesTheLeaderHandover) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto pc = game.newCreature();
    auto other = game.newCreature();
    game.party().addMember(kNpcPlayer, pc);
    ASSERT_EQ(pc, game.party().getLeader());
    EXPECT_FALSE(pc->autoCombat());

    pc->requestCombatHandover();
    pc->update(0.1f);
    EXPECT_FALSE(pc->isCombatHandoverPending());

    game.setAutoCombat(*pc, true);
    EXPECT_TRUE(pc->autoCombat());
    EXPECT_FALSE(other->autoCombat());
    // A queued player action runs first: the hand-over waits for it.
    pc->addAction(game.newAction<AttackObjectAction>(other));
    pc->update(0.1f);
    EXPECT_TRUE(pc->isCombatHandoverPending());
    pc->clearAllActions();
    pc->update(0.1f);
    EXPECT_FALSE(pc->isCombatHandoverPending());
}
