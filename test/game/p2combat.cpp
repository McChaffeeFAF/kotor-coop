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

// Playtest 1b, batch P2-Combat (design note p2_combat): saber duels, hit and
// clash effects, ranged party members, saber glow.

#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/combat.h"
#include "reone/game/game.h"
#include "reone/game/hitvisuals.h"
#include "reone/game/types.h"
#include "reone/resource/2da.h"
#include "reone/game/object/creature.h"

using namespace reone;
using namespace reone::game;

// Item 6: the lead attack of a round between two creatures that fight each
// other is a duel swing from the start, not only once the reply appends.
TEST(P2Combat, round_against_an_opponent_attacking_back_is_a_duel_from_the_start) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto jedi = game.newCreature();
    auto sith = game.newCreature();

    // The Sith's last round was against the Jedi.
    sith->beginCombatAttack(jedi, FeatType::Invalid);

    auto attack = game.newAction<AttackObjectAction>(sith);
    const CombatRound &round = game.combat().addAction(attack, *jedi);
    EXPECT_TRUE(round.duel);
    ASSERT_EQ(1u, round.actions.size());

    // The reply still joins the same round.
    auto reply = game.newAction<AttackObjectAction>(jedi);
    const CombatRound &replyRound = game.combat().addAction(reply, *sith);
    EXPECT_EQ(&round, &replyRound);
    EXPECT_EQ(2u, round.actions.size());
}

TEST(P2Combat, round_against_an_opponent_fighting_someone_else_is_not_a_duel) {
    TestEngine &engine = testEngine();
    StubConsole console;
    Game game(resource::GameID::KotOR, "", engine.options(), engine.services(), console);
    auto attacker = game.newCreature();
    auto target = game.newCreature();
    auto bystander = game.newCreature();
    target->beginCombatAttack(bystander, FeatType::Invalid);

    auto attack = game.newAction<AttackObjectAction>(target);
    const CombatRound &round = game.combat().addAction(attack, *attacker);
    EXPECT_FALSE(round.duel);

    auto idle = game.newCreature();
    auto attack2 = game.newAction<AttackObjectAction>(idle);
    const CombatRound &round2 = game.combat().addAction(attack2, *attacker);
    EXPECT_FALSE(round2.duel);
}

// Item 4: damagehitvisual.2da, as K1 and K2 ship it: only the blaster row
// has a visual, and only for ranged attacks (VFX_COM_BLASTER_IMPACT).
TEST(P2Combat, damage_hit_visuals_come_from_the_damage_types_present) {
    resource::TwoDA::Builder builder;
    builder.columns({"label", "visualeffect", "rangedeffect"});
    for (const char *label : {"Bludgeoning", "Piercing", "Slashing", "Universal", "Acid", "Cold",
                              "Light_Side", "Electrical", "Fire", "Dark_Side", "Sonic", "Ion"}) {
        builder.row({label, "****", "****"});
    }
    builder.row({"Blaster", "****", "4024"});
    std::unique_ptr<resource::TwoDA> table(builder.build());

    int blaster = static_cast<int>(DamageType::Blaster);
    EXPECT_EQ(std::vector<int>({4024}), hitvisuals::damageTypeVisuals(*table, blaster, true));
    EXPECT_TRUE(hitvisuals::damageTypeVisuals(*table, blaster, false).empty());
    // Physical damage never shows a damage-type visual.
    EXPECT_TRUE(hitvisuals::damageTypeVisuals(*table, static_cast<int>(DamageType::Slashing), false).empty());
}
