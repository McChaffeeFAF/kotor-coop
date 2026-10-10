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

Talent item(TalentRole role, int id, ItemKind kind, uint32_t object, int rank = 3, int value = 0) {
    Talent t = talent(role, id, 0, rank);
    t.item = object;
    t.itemKind = kind;
    t.value = value;
    t.count = 1;
    if (role != TalentRole::Grenade && role != TalentRole::AttackSingle && role != TalentRole::AttackArea) {
        t.selfOnly = true;
    }
    return t;
}

Talent kit(int id, ItemKind kind, uint32_t object, int rank, int value) {
    Talent t = item(TalentRole::Heal, id, kind, object, rank, value);
    t.exclusion = kind == ItemKind::RepairKit ? 0x01 : 0x02;
    return t;
}

Talent typedGrenade(GrenadeKind kind, uint32_t object, int id = 87) {
    Talent t = item(TalentRole::Grenade, id, ItemKind::Grenade, object);
    t.grenade = kind;
    t.radius = kGrenadeRadius;
    t.exclusion = 0x02;
    t.rare = kind == GrenadeKind::Thermal || kind == GrenadeKind::Cryoban || kind == GrenadeKind::Plasma;
    return t;
}

Talent stim(int id, const std::string &family, uint32_t object, int rank = 3) {
    Talent t = item(TalentRole::Buff, id, ItemKind::Stim, object, rank);
    t.family = family;
    t.exclusion = 0x02;
    t.rare = rank >= 9;
    return t;
}

/** A view with a cluster of enemies far from the party, for grenades. */
CompanionView grenadeView() {
    CompanionView view = baseView();
    view.self.ranged = true;
    view.attackRange = 20.0f;
    view.enemies = {actor(10, {12.0f, 0.0f, 0.0f}), actor(11, {13.0f, 1.0f, 0.0f})};
    return view;
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

TEST(CompanionBrain, medpac_on_self_or_ally_when_badly_hurt) {
    CompanionView view = baseView();
    view.talents.push_back(medpac());
    view.allies[0].hitPoints = 45; // hurt, but above the item threshold: no kit
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.allies[0].hitPoints = 30; // in reach and badly hurt: given a medpac
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(64, d.talent.id);
    EXPECT_EQ(2u, d.target);
    EXPECT_EQ("ally-medpac", d.reason);
    // Out of reach: not this round.
    view.allies[0].position = {30.0f, 0.0f, 0.0f};
    view.leaderPosition = {0.0f, -2.0f, 0.0f};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.self.hitPoints = 30;
    d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(1u, d.target);
    EXPECT_EQ("medpac", d.reason);
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

// Consumables, Force powers and grenades (the full toolkit).

TEST(CompanionBrain, describes_items_by_label_and_category) {
    auto medpac = describeSpell(0x1408, false, true, "ITEM_ABILITY_MED_PACK", 0, 3);
    EXPECT_EQ(TalentRole::Heal, medpac.role);
    EXPECT_EQ(ItemKind::Medpac, medpac.itemKind);
    EXPECT_FALSE(medpac.rare);
    EXPECT_TRUE(describeSpell(0x1408, false, true, "ITEM_ABILITY_LIFE_SUPPORT_KIT", 0, 9).rare);
    EXPECT_EQ(ItemKind::RepairKit, describeSpell(0x1404, false, true, "ITEM_ABILITY_DROID_REPAIR_KIT", 0, 3).itemKind);
    auto antitoxin = describeSpell(0x1048, false, true, "ITEM_ABILITY_REMOVE_POISON", 0, 9);
    EXPECT_EQ(TalentRole::Cure, antitoxin.role);
    EXPECT_EQ(ItemKind::Antidote, antitoxin.itemKind);
    auto adrenal = describeSpell(0x1808, false, true, "ITEM_ABILITY_ADRENAL_SHOT_STRENGTH", 0, 3);
    EXPECT_EQ(TalentRole::Buff, adrenal.role);
    EXPECT_EQ(ItemKind::Stim, adrenal.itemKind);
    auto hyper = describeSpell(0x1808, false, true, "ITEM_ABILITY_HYPER_ADRENAL_SHOT_STRENGTH", 0, 6);
    EXPECT_EQ(spellFamily(adrenal, "ITEM_ABILITY_ADRENAL_SHOT_STRENGTH", 69),
              spellFamily(hyper, "ITEM_ABILITY_HYPER_ADRENAL_SHOT_STRENGTH", 72));
    auto shot1 = describeSpell(0x1808, false, true, "ITEM_ABILITY_COMBAT_SHOT_1", 0, 3);
    EXPECT_EQ("stim:COMBAT_SHOT", spellFamily(shot1, "ITEM_ABILITY_COMBAT_SHOT_1", 75));
    auto shield = describeSpell(0x1808, false, true, "ITEM_ABILITY_ENERGY_SHIELD", 1, 3);
    EXPECT_EQ(ItemKind::Shield, shield.itemKind);
    EXPECT_EQ("shield", spellFamily(shield, "ITEM_ABILITY_ENERGY_SHIELD", 99));
    EXPECT_TRUE(describeSpell(0x1808, false, true, "DROID_ITEM_ENERGY_SHIELD_1", 1, 6).droidItem);
    EXPECT_EQ(ItemKind::Cloak, describeSpell(0x1808, false, true, "ITEM_ABILITY_CLOAKING_DEVICE", 0, 3).itemKind);
    EXPECT_FALSE(describeSpell(0x1808, false, true, "ITEM_ABILITY_CLOAKING_DEVICE", 0, 3).role.has_value());
    // Plot rows and launcher ammunition are never used.
    auto plot = describeSpell(0, true, true, "PLOT_KOR_GRENADE_FIRE", 2, 3);
    EXPECT_EQ(ItemKind::Plot, plot.itemKind);
    EXPECT_FALSE(plot.role.has_value());
    EXPECT_FALSE(describeSpell(0, true, true, "ITEM_ABILITY_ROCKET_ION", 3, 3).role.has_value());
    // Droid utilities are told apart by label.
    auto stunRay = describeSpell(0x8000, true, true, "DROID_ITEM_STUN_RAY_1", 0, 6);
    EXPECT_EQ(TalentRole::AttackSingle, stunRay.role);
    EXPECT_EQ(ItemKind::DroidUtility, stunRay.itemKind);
    EXPECT_EQ(kEffectDisable, stunRay.effects);
    EXPECT_TRUE(stunRay.droidItem);
    EXPECT_EQ(kEffectDamage | kEffectDispel, describeSpell(0x8000, true, true, "DROID_ITEM_SHIELD_DISRUPTOR_1", 0, 6).effects);
    EXPECT_EQ(kEffectDamage, describeSpell(0x8000, true, true, "DROID_ITEM_FLAME_THROWER_1", 0, 6).effects);
    EXPECT_EQ(TalentRole::AttackArea, describeSpell(0x8000, true, true, "DROID_ITEM_GRAVITY_GENERATOR_1", 0, 6).role);
    // Force power effects come from the category digits.
    EXPECT_EQ(kEffectDisable, describeSpell(0x1201, true, false, "FORCE_POWER_STUN", 0, 3).effects);
    EXPECT_EQ(kEffectDebuff, describeSpell(0x1011, true, false, "FORCE_POWER_SLOW", 0, 3).effects);
    EXPECT_EQ(kEffectDispel, describeSpell(0x1028, true, false, "FORCE_POWER_FORCE_BREACH", 0, 9).effects);
    EXPECT_EQ(kEffectDamage | kEffectDisable, describeSpell(0x1301, true, false, "FORCE_POWER_CHOKE", 0, 6).effects);
    EXPECT_EQ(kEffectCure, describeSpell(0x2441, false, false, "FORCE_POWER_HEAL", 0, 9).effects);
    // Power lines share their prerequisite root.
    auto armor = describeSpell(0x1808, false, false, "FORCE_POWER_FORCE_ARMOR", 0, 9);
    EXPECT_EQ("#18", spellFamily(armor, "FORCE_POWER_FORCE_ARMOR", 18));
    EXPECT_TRUE(isItemLabel("item_ability_med_pack"));
    EXPECT_FALSE(isItemLabel("FORCE_POWER_HEAL"));
}

TEST(CompanionBrain, describes_every_grenade_kind) {
    struct Case {
        const char *label;
        GrenadeKind kind;
        bool rare;
    };
    for (const Case &c : {Case {"ITEM_ABILITY_GRENADE_FRAGMENTATION", GrenadeKind::Frag, false},
                          Case {"ITEM_ABILITY_GRENADE_STUN", GrenadeKind::Stun, false},
                          Case {"ITEM_ABILITY_GRENADE_THERMAL_DETONATOR", GrenadeKind::Thermal, true},
                          Case {"ITEM_ABILITY_GRENADE_POISON", GrenadeKind::Poison, false},
                          Case {"ITEM_ABILITY_GRENADE_SONIC", GrenadeKind::Sonic, false},
                          Case {"ITEM_ABILITY_GRENADE_SONIC_2", GrenadeKind::Sonic, false},
                          Case {"ITEM_ABILITY_GRENADE_ADHESIVE", GrenadeKind::Adhesive, false},
                          Case {"ITEM_ABILITY_GRENADE_CRYOBAN", GrenadeKind::Cryoban, true},
                          Case {"ITEM_ABILITY_GRENADE_FIRE", GrenadeKind::Plasma, true},
                          Case {"ITEM_ABILITY_GRENADE_PLASMA", GrenadeKind::Plasma, true},
                          Case {"ITEM_ABILITY_GRENADE_ION", GrenadeKind::Ion, false}}) {
        auto traits = describeSpell(0, true, true, c.label, 0, 3);
        EXPECT_EQ(TalentRole::Grenade, traits.role) << c.label;
        EXPECT_EQ(ItemKind::Grenade, traits.itemKind) << c.label;
        EXPECT_EQ(c.kind, traits.grenade) << c.label;
        EXPECT_EQ(c.rare, traits.rare) << c.label;
        EXPECT_FLOAT_EQ(kGrenadeRadius, traits.radius) << c.label;
    }
}

TEST(CompanionBrain, grenade_values_by_enemy_type) {
    Actor organic = actor(10, {});
    Actor droid = actor(11, {});
    droid.droid = true;
    Actor stunned = actor(12, {});
    stunned.disabled = true;
    Actor shielded = actor(13, {});
    shielded.shielded = true;
    auto value = [](GrenadeKind kind, const Actor &enemy) { return grenadeValue(typedGrenade(kind, 600), enemy); };
    EXPECT_GT(value(GrenadeKind::Ion, droid), value(GrenadeKind::Frag, droid));
    EXPECT_GT(value(GrenadeKind::Ion, shielded), value(GrenadeKind::Ion, organic));
    EXPECT_LT(value(GrenadeKind::Ion, organic), value(GrenadeKind::Frag, organic));
    EXPECT_GT(value(GrenadeKind::Poison, organic), 0.0f);
    EXPECT_EQ(0.0f, value(GrenadeKind::Poison, droid));
    Actor immune = organic;
    immune.immunePoison = true;
    EXPECT_EQ(0.0f, value(GrenadeKind::Poison, immune));
    EXPECT_GT(value(GrenadeKind::Stun, organic), value(GrenadeKind::Frag, organic));
    EXPECT_EQ(0.0f, value(GrenadeKind::Stun, stunned));
    Actor stunImmune = organic;
    stunImmune.immuneStun = true;
    EXPECT_EQ(0.0f, value(GrenadeKind::Stun, stunImmune));
    EXPECT_EQ(0.0f, value(GrenadeKind::Adhesive, stunned));
    EXPECT_GT(value(GrenadeKind::Adhesive, organic), 0.0f);
    EXPECT_GT(value(GrenadeKind::Cryoban, organic), value(GrenadeKind::Cryoban, stunned));
    EXPECT_GT(value(GrenadeKind::Thermal, organic), value(GrenadeKind::Frag, organic));
    EXPECT_GT(value(GrenadeKind::Plasma, organic), value(GrenadeKind::Plasma, droid));
    EXPECT_EQ(value(GrenadeKind::Sonic, organic), value(GrenadeKind::Frag, organic));
    Actor dying = actor(14, {}, 10, 100);
    EXPECT_LT(value(GrenadeKind::Frag, dying), value(GrenadeKind::Frag, organic));
}

TEST(CompanionBrain, ion_grenade_on_droids_frag_on_organics) {
    CompanionView view = grenadeView();
    view.talents.push_back(typedGrenade(GrenadeKind::Frag, 600, 87));
    view.talents.push_back(typedGrenade(GrenadeKind::Ion, 601, 95));
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(87, d.talent.id); // organics: frag
    for (Actor &enemy : view.enemies) {
        enemy.droid = true;
    }
    d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(95, d.talent.id); // droids: ion
    // A lone droid is still worth an ion grenade; a frag is not thrown at one enemy.
    view.enemies.pop_back();
    EXPECT_EQ(95, decide(view, first()).talent.id);
    view.talents.pop_back();
    EXPECT_NE(87, decide(view, first()).talent.id);
}

TEST(CompanionBrain, poison_grenade_only_on_organics) {
    CompanionView view = grenadeView();
    view.talents.push_back(typedGrenade(GrenadeKind::Poison, 600, 90));
    EXPECT_EQ(90, decide(view, first()).talent.id);
    for (Actor &enemy : view.enemies) {
        enemy.droid = true;
    }
    EXPECT_NE(90, decide(view, first()).talent.id);
}

TEST(CompanionBrain, stun_and_adhesive_set_up_allies_and_skip_the_disabled) {
    CompanionView view = grenadeView();
    view.allies[0].ranged = false; // a melee ally to set up
    view.talents.push_back(typedGrenade(GrenadeKind::Frag, 600, 87));
    view.talents.push_back(typedGrenade(GrenadeKind::Stun, 601, 88));
    EXPECT_EQ(88, decide(view, first()).talent.id);
    for (Actor &enemy : view.enemies) {
        enemy.disabled = true;
    }
    EXPECT_EQ(87, decide(view, first()).talent.id); // already stunned: frag instead
    view.talents.pop_back();
    view.talents.push_back(typedGrenade(GrenadeKind::Adhesive, 602, 92));
    for (Actor &enemy : view.enemies) {
        enemy.disabled = false;
    }
    EXPECT_EQ(92, decide(view, first()).talent.id);
}

TEST(CompanionBrain, rare_grenades_kept_for_a_fight_going_badly) {
    for (GrenadeKind kind : {GrenadeKind::Thermal, GrenadeKind::Cryoban, GrenadeKind::Plasma}) {
        CompanionView view = grenadeView();
        view.talents.push_back(typedGrenade(kind, 600, 89));
        EXPECT_NE(89, decide(view, first()).talent.id);
        view.partyDown = 1; // a party member is down
        EXPECT_EQ(89, decide(view, first()).talent.id);
    }
}

TEST(CompanionBrain, sonic_grenade_needs_a_cluster_and_never_hits_allies) {
    CompanionView view = grenadeView();
    view.talents.push_back(typedGrenade(GrenadeKind::Sonic, 600, 91));
    EXPECT_EQ(91, decide(view, first()).talent.id);
    view.enemies[1].position = {20.0f, 0.0f, 0.0f}; // no cluster
    EXPECT_NE(91, decide(view, first()).talent.id);
    view.enemies[1].position = {13.0f, 1.0f, 0.0f};
    view.allies[0].position = {14.0f, 3.0f, 0.0f}; // an ally by the far enemy
    EXPECT_NE(91, decide(view, first()).talent.id);
}

TEST(CompanionBrain, grenade_picks_the_best_cluster) {
    CompanionView view = grenadeView();
    view.enemies.push_back(actor(12, {16.0f, 8.0f, 0.0f}));
    view.enemies.push_back(actor(13, {17.0f, 8.0f, 0.0f}));
    view.enemies.push_back(actor(14, {16.0f, 9.0f, 0.0f}));
    view.talents.push_back(typedGrenade(GrenadeKind::Frag, 600, 87));
    Decision d = decide(view, first());
    ASSERT_EQ(87, d.talent.id);
    EXPECT_GE(d.target, 12u); // the three-enemy cluster
    // Droids throw no grenades (the rows exclude droids).
    view.self.droid = true;
    EXPECT_NE(87, decide(view, first()).talent.id);
}

TEST(CompanionBrain, matches_kits_to_droid_and_organic_allies) {
    CompanionView view = baseView();
    view.talents.push_back(kit(64, ItemKind::Medpac, 500, 3, 50));
    view.talents.push_back(kit(84, ItemKind::RepairKit, 501, 3, 50));
    view.allies[0].hitPoints = 30;
    view.allies[0].droid = true;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(84, d.talent.id);
    EXPECT_EQ(2u, d.target);
    EXPECT_EQ("ally-repair-kit", d.reason);
    view.allies[0].droid = false;
    d = decide(view, first());
    EXPECT_EQ(64, d.talent.id);
    EXPECT_EQ("ally-medpac", d.reason);
}

TEST(CompanionBrain, weakest_kit_for_moderate_damage_best_when_critical) {
    CompanionView view = baseView();
    view.talents.push_back(kit(64, ItemKind::Medpac, 500, 3, 50));
    view.talents.push_back(kit(65, ItemKind::Medpac, 501, 6, 120));
    Talent lifeSupport = kit(66, ItemKind::Medpac, 502, 9, 300);
    lifeSupport.rare = true;
    view.talents.push_back(lifeSupport);
    view.allies[0].hitPoints = 35;
    EXPECT_EQ(64, decide(view, first()).talent.id); // no waste on moderate damage
    view.allies[0].hitPoints = 15; // critical: the fight is going badly, the best kit
    EXPECT_EQ(66, decide(view, first()).talent.id);
    // Without the rare kit, the advanced medpac.
    view.talents.pop_back();
    EXPECT_EQ(65, decide(view, first()).talent.id);
}

TEST(CompanionBrain, maintains_buffs_and_prefers_the_strongest) {
    CompanionView view = baseView();
    Talent aura = talent(TalentRole::Buff, 18, 15, 3);
    aura.selfOnly = true;
    aura.family = "#18";
    Talent armor = talent(TalentRole::Buff, 17, 15, 9);
    armor.selfOnly = true;
    armor.family = "#18";
    view.talents = {aura, armor};
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(17, d.talent.id); // the strongest of the line
    view.self.activeSpells = {17};
    view.self.activeFamilies = {"#18"};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind); // running: no recast, no weaker one
    view.self.activeSpells.clear();
    view.self.activeFamilies.clear(); // expired: recast
    EXPECT_EQ(17, decide(view, first()).talent.id);
    // Armor unaffordable: Aura instead.
    view.talents[1].forceCost = 200;
    EXPECT_EQ(18, decide(view, first()).talent.id);
    // A party buff beats a personal one of the same rank when allies stand by.
    Talent valor = talent(TalentRole::Buff, 22, 20, 3);
    valor.onCaster = true;
    valor.radius = kAreaHealRadius;
    valor.family = "#22";
    view.talents.push_back(valor);
    EXPECT_EQ(22, decide(view, first()).talent.id);
}

TEST(CompanionBrain, buffs_a_tough_fight_even_at_one_enemy) {
    CompanionView view = baseView();
    Talent armor = talent(TalentRole::Buff, 17, 15, 9);
    armor.selfOnly = true;
    armor.family = "#18";
    view.talents.push_back(armor);
    view.enemies.pop_back();
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].boss = true;
    EXPECT_EQ(17, decide(view, first()).talent.id);
}

TEST(CompanionBrain, tough_and_bad_fights) {
    CompanionView view = baseView();
    EXPECT_FALSE(toughFight(view));
    EXPECT_FALSE(goingBadly(view));
    view.enemies.push_back(actor(12, {5.0f, 0.0f, 0.0f}));
    EXPECT_TRUE(toughFight(view)); // three enemies
    view.enemies.pop_back();
    view.enemies[0].level = 10; // far above self
    EXPECT_TRUE(toughFight(view));
    view.enemies[0].level = 1;
    view.enemies[0].maxHitPoints = view.enemies[0].hitPoints = 300; // more health than the party
    EXPECT_TRUE(toughFight(view));
    EXPECT_FALSE(goingBadly(view));
    view.enemies[0].maxHitPoints = view.enemies[0].hitPoints = 500; // more than twice
    EXPECT_TRUE(goingBadly(view));
    CompanionView down = baseView();
    down.partyDown = 1;
    EXPECT_TRUE(goingBadly(down));
    CompanionView hurt = baseView();
    hurt.allies[0].hitPoints = 20;
    EXPECT_TRUE(goingBadly(hurt));
}

TEST(CompanionBrain, stim_before_a_tough_fight_never_stacked) {
    CompanionView view = baseView();
    view.talents.push_back(stim(69, "stim:ADRENAL_SHOT_STRENGTH", 700));
    view.talents.push_back(stim(70, "stim:ADRENAL_SHOT_ALACRITY", 701));
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind); // two plain enemies: no stim
    view.enemies.push_back(actor(12, {5.0f, 0.0f, 0.0f}));
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ("stim", d.reason);
    EXPECT_EQ(1u, d.target);
    // One running: no second stim while the fight goes well.
    view.self.activeSpells = {d.talent.id};
    view.self.activeFamilies = {d.talent.family};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    // Going badly: another type, never the same one.
    view.partyDown = 1;
    std::string running = view.self.activeFamilies[0];
    d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ("stim", d.reason);
    EXPECT_NE(running, d.talent.family);
    // Both running: nothing more.
    view.self.activeFamilies = {"stim:ADRENAL_SHOT_ALACRITY", "stim:ADRENAL_SHOT_STRENGTH"};
    EXPECT_NE("stim", decide(view, first()).reason);
    // Droids take no stims.
    view.self.droid = true;
    view.self.activeFamilies.clear();
    view.self.activeSpells.clear();
    EXPECT_NE("stim", decide(view, first()).reason);
}

TEST(CompanionBrain, energy_shield_when_shot_at) {
    CompanionView view = baseView();
    Talent shield = item(TalentRole::Buff, 99, ItemKind::Shield, 800);
    shield.family = "shield";
    view.talents.push_back(shield);
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind); // nobody shooting
    view.enemies[0].ranged = true;
    view.enemies[0].target = view.self.id;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(99, d.talent.id);
    EXPECT_EQ("shield", d.reason);
    view.self.shielded = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    // A droid uses its own shield, not an organic one.
    view.self.shielded = false;
    view.self.droid = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    Talent droidShield = item(TalentRole::Buff, 110, ItemKind::Shield, 801, 6);
    droidShield.droidItem = true;
    droidShield.family = "shield";
    view.talents.push_back(droidShield);
    EXPECT_EQ(110, decide(view, first()).talent.id);
}

TEST(CompanionBrain, antidote_when_poisoned) {
    CompanionView view = baseView();
    Talent antitoxin = item(TalentRole::Cure, 67, ItemKind::Antidote, 900, 9);
    antitoxin.exclusion = 0x02;
    view.talents.push_back(antitoxin);
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.self.poisoned = true;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(67, d.talent.id);
    EXPECT_EQ(1u, d.target);
    view.self.poisoned = false;
    view.allies[0].poisoned = true;
    d = decide(view, first());
    EXPECT_EQ(2u, d.target);
    EXPECT_EQ("ally-antidote", d.reason);
    // Not on a droid.
    view.allies[0].droid = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.allies[0].droid = false;
    // Force Heal cures poison and comes first.
    Talent heal = forceHeal();
    heal.effects = kEffectCure;
    view.talents.push_back(heal);
    d = decide(view, first());
    EXPECT_EQ(28, d.talent.id);
    EXPECT_EQ("cure-party", d.reason);
}

TEST(CompanionBrain, plot_items_are_never_used) {
    CompanionView view = grenadeView();
    view.talents.push_back(kit(64, ItemKind::Plot, 500, 3, 50));
    Talent plotGrenade = typedGrenade(GrenadeKind::Frag, 601, 97);
    plotGrenade.itemKind = ItemKind::Plot;
    view.talents.push_back(plotGrenade);
    Talent plotStim = stim(69, "stim:X", 702);
    plotStim.itemKind = ItemKind::Plot;
    view.talents.push_back(plotStim);
    view.enemies.push_back(actor(12, {13.0f, -1.0f, 0.0f})); // a tough fight
    view.self.hitPoints = 30;
    Decision d = decide(view, first());
    EXPECT_EQ(DecisionKind::Attack, d.kind);
    EXPECT_FALSE(describeSpell(0x1408, false, true, "PLOT_UNK_COLD_SERUM", 1, 3).role.has_value());
}

TEST(CompanionBrain, powers_fit_the_target) {
    CompanionView view = baseView();
    view.enemies[1].position = {8.0f, 8.0f, 0.0f}; // a second enemy, not the target
    Talent stun = talent(TalentRole::AttackSingle, 46, 20, 3);
    stun.effects = kEffectDisable;
    stun.exclusion = 0x02;
    Talent slow = talent(TalentRole::AttackSingle, 45, 15, 3);
    slow.effects = kEffectDebuff;
    Talent lightning = talent(TalentRole::AttackSingle, 35, 20, 6);
    lightning.effects = kEffectDamage;
    view.talents = {stun};
    EXPECT_EQ(46, decide(view, first()).talent.id);
    view.enemies[0].disabled = true; // already stunned
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].disabled = false;
    view.enemies[0].immuneStun = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].immuneStun = false;
    view.enemies[0].droid = true; // Stun excludes droids
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].droid = false;
    // Slow only on a strong enemy.
    view.talents = {slow};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].boss = true;
    EXPECT_EQ(45, decide(view, first()).talent.id);
    // A strong enemy: a stun beats a stronger damage power.
    view.talents = {stun, lightning};
    EXPECT_EQ(46, decide(view, first()).talent.id);
    // A weak one, alone: the damage power.
    view.enemies[0].boss = false;
    view.enemies[0].maxHitPoints = 50;
    view.enemies[0].hitPoints = 50;
    view.enemies.pop_back();
    EXPECT_EQ(35, decide(view, first()).talent.id);
    // Force resistance: no Force power at all.
    view.enemies[0].forceResistant = true;
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
}

TEST(CompanionBrain, breach_only_against_shields_and_resistance) {
    CompanionView view = baseView();
    Talent breach = talent(TalentRole::AttackSingle, 19, 25, 9);
    breach.effects = kEffectDispel;
    view.talents = {breach};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind);
    view.enemies[0].forceResistant = true;
    EXPECT_EQ(19, decide(view, first()).talent.id);
}

TEST(CompanionBrain, droid_utilities_for_droids) {
    CompanionView view = baseView();
    Talent stunRay = item(TalentRole::AttackSingle, 116, ItemKind::DroidUtility, 1000, 6);
    stunRay.effects = kEffectDisable;
    stunRay.droidItem = true;
    Talent flamer = item(TalentRole::AttackSingle, 120, ItemKind::DroidUtility, 1001, 6);
    flamer.effects = kEffectDamage;
    flamer.droidItem = true;
    view.talents = {stunRay, flamer};
    EXPECT_EQ(DecisionKind::Attack, decide(view, first()).kind); // an organic cannot fire them
    view.self.droid = true;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(116, d.talent.id); // two enemies: the stun ray first
    EXPECT_EQ("droid-utility", d.reason);
    view.enemies[0].disabled = true;
    view.enemies[1].disabled = true;
    EXPECT_EQ(120, decide(view, first()).talent.id);
}

TEST(CompanionBrain, repair_kit_for_a_damaged_droid_ally_by_anyone) {
    CompanionView view = baseView();
    view.talents.push_back(kit(84, ItemKind::RepairKit, 501, 3, 50));
    view.allies[0].droid = true;
    view.allies[0].hitPoints = 20;
    Decision d = decide(view, first());
    ASSERT_EQ(DecisionKind::TalentOnObject, d.kind);
    EXPECT_EQ(84, d.talent.id);
    EXPECT_EQ(2u, d.target);
    // Not on an organic.
    view.allies[0].droid = false;
    EXPECT_NE(84, decide(view, first()).talent.id);
}

TEST(CompanionBrain, party_does_not_double_up_on_items_or_patients) {
    CompanionView view = baseView();
    view.talents.push_back(kit(64, ItemKind::Medpac, 500, 3, 50));
    view.allies[0].hitPoints = 30;
    EXPECT_EQ("ally-medpac", decide(view, first()).reason);
    view.tended = {2}; // another companion is already giving ally 2 a kit
    EXPECT_NE("ally-medpac", decide(view, first()).reason);
    view.tended.clear();
    view.claimedItems = {500}; // the last medpac is spoken for
    EXPECT_NE("ally-medpac", decide(view, first()).reason);
    view.talents[0].count = 3; // a stack: there are more
    EXPECT_EQ("ally-medpac", decide(view, first()).reason);
    // The last shield too.
    CompanionView shot = baseView();
    Talent shield = item(TalentRole::Buff, 99, ItemKind::Shield, 800);
    shield.family = "shield";
    shot.talents.push_back(shield);
    shot.enemies[0].ranged = true;
    shot.enemies[0].target = shot.self.id;
    EXPECT_EQ("shield", decide(shot, first()).reason);
    shot.claimedItems = {800};
    EXPECT_NE("shield", decide(shot, first()).reason);
}
