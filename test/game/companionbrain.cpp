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

// C13 "Smarter companions": the pure decision. design note c13_companions.

#include <cmath>

#include <glm/geometric.hpp>
#include <gtest/gtest.h>

#include "reone/game/companionbrain.h"

using namespace reone;
using namespace reone::game;
using namespace reone::game::companion;

namespace {

Actor actor(uint32_t id, glm::vec3 position, int hp = 100, int maxHp = 100) {
    Actor a;
    a.id = id;
    a.position = position;
    a.hitPoints = hp;
    a.maxHitPoints = maxHp;
    return a;
}

CompanionView baseView() {
    CompanionView view;
    view.self = actor(1, {0.0f, 0.0f, 0.0f});
    view.forcePoints = 100;
    view.attackRange = 2.0f;
    view.leaderPosition = {0.0f, -2.0f, 0.0f};
    view.allies.push_back(actor(2, {0.0f, -2.0f, 0.0f}));
    view.enemies.push_back(actor(10, {3.0f, 0.0f, 0.0f}));
    view.enemies.push_back(actor(11, {4.0f, 1.0f, 0.0f}));
    return view;
}

Talent talent(TalentRole role, int id, int cost = 0, int rank = 1) {
    Talent t;
    t.role = role;
    t.id = id;
    t.forceCost = cost;
    t.rank = rank;
    return t;
}

Talent forceHeal(int id = 28, int cost = 25) {
    Talent t = talent(TalentRole::Heal, id, cost);
    t.onCaster = true;
    t.radius = kAreaHealRadius;
    return t;
}

Talent medpac() {
    Talent t = talent(TalentRole::Heal, 64);
    t.item = 500;
    t.selfOnly = true;
    t.exclusion = 0x02;
    return t;
}

Talent grenade() {
    Talent t = talent(TalentRole::Grenade, 87);
    t.item = 600;
    t.radius = kGrenadeRadius;
    return t;
}

Random first() {
    return [](int min, int) { return min; };
}

Random last() {
    return [](int, int max) { return max; };
}

} // namespace

TEST(CompanionBrain, classifies_spells_by_category) {
    EXPECT_EQ(TalentRole::Heal, classifySpell(0x2441, false, false));   // Cure / Heal
    EXPECT_EQ(TalentRole::Heal, classifySpell(0x1408, false, true));    // medpac
    EXPECT_EQ(TalentRole::Buff, classifySpell(0x1808, false, false));   // Force Armor
    EXPECT_EQ(TalentRole::Buff, classifySpell(0x2301, false, false));   // Force Valor
    EXPECT_EQ(TalentRole::AttackSingle, classifySpell(0x1201, true, false)); // Stun
    EXPECT_EQ(TalentRole::AttackArea, classifySpell(0x2302, true, false));   // Force Wave
    EXPECT_EQ(TalentRole::Grenade, classifySpell(0, true, true));
    EXPECT_FALSE(classifySpell(0, true, false).has_value());
    EXPECT_FALSE(classifySpell(0x8000, true, true).has_value()); // droid utilities stay retail
    EXPECT_FALSE(classifySpell(0x1048, false, true).has_value()); // anti-toxin
}

TEST(CompanionBrain, heals_most_hurt_ally_first) {
    CompanionView view = baseView();
    view.allies.push_back(actor(3, {1.0f, 1.0f, 0.0f}, 20, 100));
    view.allies[0].hitPoints = 40; // 40%, less hurt than ally 3
    Talent touch = talent(TalentRole::Heal, 200, 10);
    view.talents.push_back(touch);
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(200, d.talent.id);
    EXPECT_EQ(3u, d.target);
}

TEST(CompanionBrain, party_heal_is_cast_on_self_when_it_reaches_the_patient) {
    CompanionView view = baseView();
    view.allies[0].hitPoints = 30;
    view.talents.push_back(forceHeal());
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(28, d.talent.id);
    EXPECT_EQ(1u, d.target);
    // Out of reach: no heal, the round goes to the fight.
    view.allies[0].position = {30.0f, 0.0f, 0.0f};
    EXPECT_NE(28, decide(view, first()).talent.id);
}

TEST(CompanionBrain, no_heal_at_full_or_moderate_health) {
    CompanionView view = baseView();
    view.self.hitPoints = 60;
    view.allies[0].hitPoints = 51;
    view.talents.push_back(forceHeal());
    view.talents.push_back(medpac());
    Decision d = decide(view, first());
    EXPECT_NE(TalentRole::Heal, d.talent.role);
    EXPECT_EQ(DecisionKind::Attack, d.kind);
}

TEST(CompanionBrain, medpac_only_on_self_when_badly_hurt) {
    CompanionView view = baseView();
    view.talents.push_back(medpac());
    view.allies[0].hitPoints = 10; // an ally is hurt: medpacs are not thrown to others
    Decision d = decide(view, first());
    EXPECT_EQ(DecisionKind::Attack, d.kind);
    view.self.hitPoints = 30;
    d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(64, d.talent.id);
    EXPECT_EQ(1u, d.target);
    // Not on a droid.
    view.self.droid = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
}

TEST(CompanionBrain, keeps_force_reserve_for_heals) {
    CompanionView view = baseView();
    view.talents.push_back(forceHeal(28, 25));
    Talent armor = talent(TalentRole::Buff, 17, 15);
    armor.selfOnly = true;
    view.talents.push_back(armor);
    view.forcePoints = 30; // 30 - 15 < 25 reserve
    EXPECT_EQ(25, forceReserve(view));
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.forcePoints = 40;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(17, d.talent.id);
}

TEST(CompanionBrain, no_redundant_buff_and_none_on_last_enemy) {
    CompanionView view = baseView();
    Talent armor = talent(TalentRole::Buff, 17, 15);
    armor.selfOnly = true;
    view.talents.push_back(armor);
    view.self.activeSpells = {17};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.self.activeSpells.clear();
    view.enemies.pop_back(); // the last enemy
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
}

TEST(CompanionBrain, grenade_not_thrown_near_allies) {
    CompanionView view = baseView();
    view.self.ranged = true;
    view.attackRange = 20.0f;
    view.enemies = {actor(10, {12.0f, 0.0f, 0.0f}), actor(11, {13.0f, 1.0f, 0.0f})};
    view.talents.push_back(grenade());
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(87, d.talent.id);
    // An ally fighting beside the enemies: no grenade.
    view.allies[0].position = {11.0f, 0.5f, 0.0f};
    d = decide(view, first());
    EXPECT_NE(87, d.talent.id);
    EXPECT_FALSE(catchesAllies(view, {20.0f, 0.0f, 0.0f}, kGrenadeRadius, false));
    EXPECT_TRUE(catchesAllies(view, {13.0f, 0.0f, 0.0f}, kGrenadeRadius, false));
}

TEST(CompanionBrain, grenade_never_catches_self_or_a_lone_enemy) {
    CompanionView view = baseView(); // enemies 3 m away: the blast would reach self
    view.allies.clear();
    view.talents.push_back(grenade());
    EXPECT_NE(87, decide(view, first()).talent.id);
    view.enemies = {actor(10, {12.0f, 0.0f, 0.0f})};
    EXPECT_NE(87, decide(view, first()).talent.id);
}

TEST(CompanionBrain, area_power_spares_allies) {
    CompanionView view = baseView();
    Talent wave = talent(TalentRole::AttackArea, 26, 10);
    wave.onCaster = true;
    wave.radius = kAreaPowerRadius;
    view.talents.push_back(wave);
    // Ally 2 m away is inside the caster-centred blast.
    EXPECT_NE(26, decide(view, first()).talent.id);
    view.allies[0].position = {-10.0f, -10.0f, 0.0f};
    view.leaderPosition = view.allies[0].position;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(26, d.talent.id);
}

TEST(CompanionBrain, targets_leader_target_weak_enemies_and_casters) {
    CompanionView view = baseView();
    EXPECT_EQ(10u, chooseTarget(view)); // nearest
    view.leaderTarget = 11;
    EXPECT_EQ(11u, chooseTarget(view));
    view.enemies[0].hitPoints = 10; // nearly dead beats the leader's target? 25 vs 30: no
    EXPECT_EQ(11u, chooseTarget(view));
    view.enemies[0].caster = true; // weak caster: yes
    EXPECT_EQ(10u, chooseTarget(view));
}

TEST(CompanionBrain, does_not_chase_across_the_map) {
    CompanionView view = baseView();
    view.enemies = {actor(10, {40.0f, 0.0f, 0.0f})};
    EXPECT_EQ(0u, chooseTarget(view));
    EXPECT_EQ(DecisionKind::None, decide(view, first()).kind);
}

TEST(CompanionBrain, hold_only_fights_within_reach) {
    CompanionView view = baseView();
    view.hold = true;
    view.enemies = {actor(10, {6.0f, 0.0f, 0.0f})};
    EXPECT_EQ(0u, chooseTarget(view));
    view.enemies[0].position = {2.0f, 0.0f, 0.0f};
    EXPECT_EQ(10u, chooseTarget(view));
}

TEST(CompanionBrain, respects_stealth_and_passive) {
    CompanionView view = baseView();
    view.stealthed = true;
    EXPECT_EQ(DecisionKind::None, decide(view, first()).kind);
    view.stealthed = false;
    view.passive = true;
    EXPECT_EQ(DecisionKind::None, decide(view, first()).kind);
}

TEST(CompanionBrain, single_powers_use_the_gameplay_draw_and_skip_affected_targets) {
    CompanionView view = baseView();
    view.talents.push_back(talent(TalentRole::AttackSingle, 46, 20)); // Stun
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(46, d.talent.id);
    EXPECT_EQ(DecisionKind::Attack, decide(view, last()).kind);
    view.enemies[0].activeSpells = {46};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
}

TEST(CompanionBrain, avoids_known_mines) {
    CompanionView view = baseView();
    view.enemies = {actor(10, {6.0f, 0.0f, 0.0f}), actor(11, {0.0f, 6.0f, 0.0f})};
    view.hazards.push_back(Hazard {{3.0f, 0.0f, 0.0f}, 2.5f});
    EXPECT_EQ(11u, chooseTarget(view)); // the path to 10 crosses the mine
    // Standing on a cloud: step out first.
    view.hazards[0].position = {0.5f, 0.0f, 0.0f};
    Decision d = decide(view, first());
    ASSERT_TRUE(d.moveFirst.has_value());
    EXPECT_GT(glm::length(*d.moveFirst - view.hazards[0].position), view.hazards[0].radius);
}

TEST(CompanionBrain, ranged_backs_off_and_melee_clears_line_of_fire) {
    CompanionView view = baseView();
    view.self.ranged = true;
    view.attackRange = 20.0f;
    view.preferredDistance = 20.0f;
    view.enemies = {actor(10, {1.5f, 0.0f, 0.0f})};
    Decision d = decide(view, first());
    ASSERT_TRUE(d.moveFirst.has_value());
    EXPECT_LT(d.moveFirst->x, 0.0f);
    view.mayReposition = false;
    EXPECT_FALSE(decide(view, first()).moveFirst.has_value());

    CompanionView melee = baseView();
    melee.self.position = {5.0f, 0.0f, 0.0f};
    melee.leaderPosition = {5.0f, 0.0f, 0.0f};
    Actor shooter = actor(2, {0.0f, 0.0f, 0.0f});
    shooter.ranged = true;
    shooter.target = 10;
    melee.allies = {shooter};
    melee.enemies = {actor(10, {12.0f, 0.0f, 0.0f})};
    d = decide(melee, first());
    ASSERT_TRUE(d.moveFirst.has_value());
    EXPECT_NEAR(kSideStep, std::abs(d.moveFirst->y), 1e-4f);
}

TEST(CompanionBrain, generic_scripts) {
    EXPECT_TRUE(isGenericCombatScript("k_hen_combend01"));
    EXPECT_TRUE(isGenericCombatScript("K_AI_MASTER"));
    EXPECT_FALSE(isGenericCombatScript("end_trask_end"));
    EXPECT_FALSE(isGenericCombatScript(""));
}
