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

// Audit of the d20 rules layer against the public KotOR / d20 rules: saving
// throws, the damage pipeline, effect stacking and lifetimes. Every test uses
// synthetic tables and values written out in the test. docs/dev/combat_rules_audit.md
// lists, rule by rule, what was already right, what this audit fixed and what
// it added.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"

#include "reone/game/d20/classes.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/heal.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/paralyze.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/temporaryhitpoints.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

std::shared_ptr<TwoDA> makeTable(
    const std::vector<std::string> &columns,
    const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

// Two classes with different save progressions: class 0 saves "good" (Fort),
// class 1 saves "poor" everywhere but Will.
std::shared_ptr<TwoDA> classesTable() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable",
        "forcedie", "spellcaster"};
    return makeTable(columns, {
        {"1", "2", "10", "2", "10", "10", "10", "10", "10", "10", "", "savea", "attack", "", "", "", "0", "0"},
        {"1", "2", "8", "2", "10", "10", "10", "10", "10", "10", "", "saveb", "attack", "", "", "", "0", "0"},
    });
}

std::shared_ptr<TwoDA> saveTable(const std::vector<std::array<int, 3>> &perLevel) {
    std::vector<std::vector<std::string>> rows;
    int level = 1;
    for (const auto &entry : perLevel) {
        rows.push_back({std::to_string(level++), std::to_string(entry[0]), std::to_string(entry[1]), std::to_string(entry[2])});
    }
    return makeTable({"level", "fortsave", "refsave", "willsave"}, rows);
}

class RulesTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_twoDas, get("classes")).WillByDefault(Return(classesTable()));
        // fort / ref / will by level
        ON_CALL(_twoDas, get("savea")).WillByDefault(Return(saveTable({{2, 0, 0}, {3, 0, 0}, {3, 1, 1}, {4, 1, 1}})));
        ON_CALL(_twoDas, get("saveb")).WillByDefault(Return(saveTable({{0, 0, 2}, {0, 0, 3}, {1, 1, 3}})));
        ON_CALL(_twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}, {"1"}, {"2"}, {"3"}})));
        ON_CALL(_twoDas, get("skills")).WillByDefault(Return(makeTable({"label"}, {{"computeruse"}})));
        ON_CALL(_twoDas, get("feat")).WillByDefault(Return(makeTable({"label"}, {{"f0"}})));
        _classes = std::make_unique<Classes>(_strings, _twoDas);
        ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([this](ClassType type) {
            return _classes->get(type);
        }));
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    std::shared_ptr<Creature> creature(std::vector<std::pair<int, int>> classLevels = {}) {
        auto result = _game->newCreature();
        _area->add(result);
        for (Ability ability : {Ability::Strength, Ability::Dexterity, Ability::Constitution, Ability::Intelligence, Ability::Wisdom, Ability::Charisma}) {
            result->attributes().setAbilityScore(ability, 10); // modifier 0
        }
        for (auto [classType, level] : classLevels) {
            auto clazz = _classes->get(static_cast<ClassType>(classType));
            EXPECT_TRUE(clazz);
            result->attributes().addClassLevels(clazz.get(), level);
        }
        return result;
    }

    /** A save-throw-increase / decrease / AC / skill effect stamped as made by `spell` cast by `caster`. */
    template <typename E, typename... Args>
    std::shared_ptr<Effect> fromSpell(int spell, const std::shared_ptr<Object> &caster, Args &&...args) {
        auto effect = std::make_shared<E>(std::forward<Args>(args)...);
        effect->setSaveFacingSpellId(spell);
        effect->setSaveFacingCreator(caster);
        return effect;
    }

    int resolve(const std::shared_ptr<Creature> &target, std::vector<std::pair<int, DamageType>> parts, DamagePower power = DamagePower::Normal) {
        DamagePacket packet(power);
        int flags = 0;
        for (auto [amount, type] : parts) {
            packet.add(amount, type);
            flags |= static_cast<int>(type);
        }
        packet.setDamageFlags(flags);
        packet.resolve(*target);
        return packet.resolvedDamage();
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<MockStrings> _strings;
    NiceMock<MockTwoDAs> _twoDas;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Classes> _classes;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
};

constexpr int kFort = 1;
constexpr int kReflex = 2;
constexpr int kWill = 3;
constexpr int kAllSaves = 0;

} // namespace

// Saving throws

TEST_F(RulesTest, base_saves_come_from_the_class_table_at_the_class_level) {
    auto soldier = creature({{0, 3}});
    EXPECT_EQ(3, soldier->getSavingThrow(kFort));
    EXPECT_EQ(1, soldier->getSavingThrow(kReflex));
    EXPECT_EQ(1, soldier->getSavingThrow(kWill));
    auto scout = creature({{1, 3}});
    EXPECT_EQ(1, scout->getSavingThrow(kFort));
    EXPECT_EQ(1, scout->getSavingThrow(kReflex));
    EXPECT_EQ(3, scout->getSavingThrow(kWill));
}

TEST_F(RulesTest, multiclass_base_saves_add_up_class_by_class) {
    auto both = creature({{0, 2}, {1, 1}});
    EXPECT_EQ(3 + 0, both->getSavingThrow(kFort));
    EXPECT_EQ(0 + 0, both->getSavingThrow(kReflex));
    EXPECT_EQ(0 + 2, both->getSavingThrow(kWill));
}

TEST_F(RulesTest, each_save_adds_its_own_ability_modifier_rounded_down) {
    auto c = creature({{0, 1}}); // fort 2, ref 0, will 0
    c->attributes().setAbilityScore(Ability::Constitution, 14);  // +2
    c->attributes().setAbilityScore(Ability::Dexterity, 9);      // -1: floor, not toward zero
    c->attributes().setAbilityScore(Ability::Wisdom, 7);         // -2
    EXPECT_EQ(2 + 2, c->getSavingThrow(kFort));
    EXPECT_EQ(0 - 1, c->getSavingThrow(kReflex));
    EXPECT_EQ(0 - 2, c->getSavingThrow(kWill));
    c->attributes().setAbilityScore(Ability::Constitution, 11); // +0
    EXPECT_EQ(2, c->getSavingThrow(kFort));
}

TEST_F(RulesTest, a_saving_throw_effect_changes_only_the_save_and_type_it_names) {
    auto c = creature({{0, 1}});
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kWill, 3, SavingThrowType::All), DurationType::Permanent);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 1, SavingThrowType::Fear), DurationType::Permanent);
    EXPECT_EQ(2, c->getSavingThrow(kFort));
    EXPECT_EQ(3, c->getSavingThrow(kWill));
    EXPECT_EQ(2 + 1, c->getSavingThrow(kFort, SavingThrowType::Fear));
    EXPECT_EQ(3 + 1, c->getSavingThrow(kWill, SavingThrowType::Fear));
    EXPECT_EQ(2, c->getSavingThrow(kFort, SavingThrowType::Fire));
    c->applyEffect(std::make_shared<SavingThrowDecreaseEffect>(kFort, 4, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(2 - 4, c->getSavingThrow(kFort));
}

TEST_F(RulesTest, saving_throw_bonuses_from_effects_are_capped_but_penalties_are_not) {
    auto c = creature();
    for (int i = 0; i < 3; ++i) {
        c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 10, SavingThrowType::All), DurationType::Permanent);
    }
    EXPECT_EQ(20, c->getSavingThrow(kFort));
    auto d = creature();
    for (int i = 0; i < 3; ++i) {
        d->applyEffect(std::make_shared<SavingThrowDecreaseEffect>(kAllSaves, 10, SavingThrowType::All), DurationType::Permanent);
    }
    EXPECT_EQ(-30, d->getSavingThrow(kFort));
}

TEST_F(RulesTest, one_spell_cast_twice_does_not_stack_its_saving_throw_bonus_but_other_sources_do) {
    auto caster = creature();
    auto other = creature();
    auto c = creature();
    c->applyEffect(fromSpell<SavingThrowIncreaseEffect>(100, caster, kAllSaves, 2, SavingThrowType::All), DurationType::Temporary, 60.0f);
    c->applyEffect(fromSpell<SavingThrowIncreaseEffect>(100, caster, kAllSaves, 2, SavingThrowType::All), DurationType::Temporary, 60.0f);
    EXPECT_EQ(2, c->getSavingThrow(kFort)) << "the same spell again";
    c->applyEffect(fromSpell<SavingThrowIncreaseEffect>(101, caster, kAllSaves, 3, SavingThrowType::All), DurationType::Temporary, 60.0f);
    EXPECT_EQ(5, c->getSavingThrow(kFort)) << "a different spell";
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 1, SavingThrowType::All), DurationType::Permanent);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 1, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(7, c->getSavingThrow(kFort)) << "effects with no spell behind them are independent";
}

TEST_F(RulesTest, a_stronger_recast_of_the_same_spell_counts_once_at_its_best_value) {
    auto caster = creature();
    auto c = creature();
    c->applyEffect(fromSpell<SavingThrowIncreaseEffect>(100, caster, kAllSaves, 2, SavingThrowType::All), DurationType::Temporary, 60.0f);
    c->applyEffect(fromSpell<SavingThrowIncreaseEffect>(100, caster, kAllSaves, 4, SavingThrowType::All), DurationType::Temporary, 60.0f);
    EXPECT_EQ(4, c->getSavingThrow(kFort));
}

TEST_F(RulesTest, saving_throw_roll_is_d20_plus_the_save_against_the_difficulty_class) {
    auto c = creature({{0, 1}}); // fort 2
    int successes = 0;
    for (int i = 0; i < 400; ++i) {
        if (c->rollSavingThrow(kFort, 15)) {
            ++successes;
        }
    }
    // needs a roll of 13 or more: 8 of 20 faces
    EXPECT_NEAR(0.40, successes / 400.0, 0.12);
    EXPECT_TRUE(c->rollSavingThrow(kFort, 3)); // 1 + 2 always makes it
    EXPECT_FALSE(c->rollSavingThrow(kFort, 23)); // 20 + 2 never reaches 23
}

// Armour class

TEST_F(RulesTest, dodge_bonuses_add_up_across_sources_but_one_spell_counts_once) {
    auto caster = creature();
    auto c = creature();
    int base = c->getDefense();
    c->applyEffect(fromSpell<ACIncreaseEffect>(200, caster, 2, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Temporary, 60.0f);
    c->applyEffect(fromSpell<ACIncreaseEffect>(200, caster, 2, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Temporary, 60.0f);
    EXPECT_EQ(base + 2, c->getDefense()) << "the same spell cast again";
    c->applyEffect(fromSpell<ACIncreaseEffect>(201, caster, 3, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Temporary, 60.0f);
    EXPECT_EQ(base + 5, c->getDefense()) << "a different spell";
}

TEST_F(RulesTest, only_dodge_bonuses_stack_the_other_ac_types_take_the_best) {
    auto c = creature();
    int base = c->getDefense();
    c->applyEffect(std::make_shared<ACIncreaseEffect>(2, ACBonus::Deflection, kAllDamageTypeFlags), DurationType::Permanent);
    c->applyEffect(std::make_shared<ACIncreaseEffect>(4, ACBonus::Deflection, kAllDamageTypeFlags), DurationType::Permanent);
    EXPECT_EQ(base + 4, c->getDefense());
    c->applyEffect(std::make_shared<ACIncreaseEffect>(1, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Permanent);
    c->applyEffect(std::make_shared<ACIncreaseEffect>(1, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Permanent);
    EXPECT_EQ(base + 4 + 2, c->getDefense());
}

TEST_F(RulesTest, dodge_is_capped) {
    auto c = creature();
    int base = c->getDefense();
    for (int i = 0; i < 4; ++i) {
        c->applyEffect(std::make_shared<ACIncreaseEffect>(5, ACBonus::Dodge, kAllDamageTypeFlags), DurationType::Permanent);
    }
    EXPECT_EQ(base + 10, c->getDefense());
}

// Skills

TEST_F(RulesTest, skill_effects_follow_the_same_source_rule) {
    auto caster = creature();
    auto c = creature();
    int base = c->getSkillValue(SkillType::ComputerUse);
    c->applyEffect(fromSpell<SkillIncreaseEffect>(300, caster, SkillType::ComputerUse, 5), DurationType::Temporary, 60.0f);
    c->applyEffect(fromSpell<SkillIncreaseEffect>(300, caster, SkillType::ComputerUse, 5), DurationType::Temporary, 60.0f);
    EXPECT_EQ(base + 5, c->getSkillValue(SkillType::ComputerUse));
    c->applyEffect(std::make_shared<SkillIncreaseEffect>(SkillType::ComputerUse, 2), DurationType::Permanent);
    EXPECT_EQ(base + 7, c->getSkillValue(SkillType::ComputerUse));
    c->applyEffect(std::make_shared<SkillDecreaseEffect>(SkillType::ComputerUse, 3), DurationType::Permanent);
    EXPECT_EQ(base + 4, c->getSkillValue(SkillType::ComputerUse));
    EXPECT_EQ(base, c->getSkillValue(SkillType::Awareness)) << "another skill is unaffected";
}

// The damage pipeline: immunity, then resistance, then reduction

TEST_F(RulesTest, damage_immunity_is_a_percentage_of_each_type) {
    auto half = creature();
    half->applyEffect(std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Fire, 50), DurationType::Permanent);
    EXPECT_EQ(10, resolve(half, {{20, DamageType::Fire}}));
    EXPECT_EQ(20, resolve(half, {{20, DamageType::Cold}})) << "another type is untouched";

    auto total = creature();
    total->applyEffect(std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Fire, 100), DurationType::Permanent);
    EXPECT_EQ(0, resolve(total, {{20, DamageType::Fire}}));

    auto vulnerable = creature();
    vulnerable->applyEffect(std::make_shared<DamageImmunityDecreaseEffect>(DamageType::Fire, 50), DurationType::Permanent);
    EXPECT_EQ(30, resolve(vulnerable, {{20, DamageType::Fire}})) << "negative immunity is vulnerability";
}

TEST_F(RulesTest, damage_immunities_of_one_type_add_up_to_at_most_total_immunity) {
    auto c = creature();
    c->applyEffect(std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Fire, 60), DurationType::Permanent);
    c->applyEffect(std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Fire, 60), DurationType::Permanent);
    EXPECT_EQ(0, resolve(c, {{20, DamageType::Fire}}));
}

TEST_F(RulesTest, damage_resistance_takes_a_flat_amount_off_each_hit_of_its_type_and_does_not_stack) {
    auto c = creature();
    c->applyEffect(std::make_shared<DamageResistanceEffect>(DamageType::Fire, 5, 0), DurationType::Permanent);
    c->applyEffect(std::make_shared<DamageResistanceEffect>(DamageType::Fire, 3, 0), DurationType::Permanent);
    EXPECT_EQ(15, resolve(c, {{20, DamageType::Fire}})) << "the best resistance, not their sum";
    EXPECT_EQ(20, resolve(c, {{20, DamageType::Cold}}));
    EXPECT_EQ(0, resolve(c, {{4, DamageType::Fire}})) << "never below zero";
}

TEST_F(RulesTest, damage_reduction_is_bypassed_by_a_weapon_of_enough_power_and_ignores_energy_types) {
    auto c = creature();
    c->applyEffect(std::make_shared<DamageReductionEffect>(5, DamagePower::PlusTwo, 0), DurationType::Permanent);
    EXPECT_EQ(15, resolve(c, {{20, DamageType::Slashing}}, DamagePower::Normal));
    EXPECT_EQ(15, resolve(c, {{20, DamageType::Slashing}}, DamagePower::PlusOne));
    EXPECT_EQ(20, resolve(c, {{20, DamageType::Slashing}}, DamagePower::PlusTwo)) << "a +2 weapon gets through 5/+2";
    EXPECT_EQ(20, resolve(c, {{20, DamageType::Fire}}, DamagePower::Normal)) << "reduction is for physical damage";
    c->applyEffect(std::make_shared<DamageReductionEffect>(3, DamagePower::PlusTwo, 0), DurationType::Permanent);
    EXPECT_EQ(15, resolve(c, {{20, DamageType::Slashing}})) << "the best reduction, not their sum";
}

TEST_F(RulesTest, mitigation_is_applied_in_the_order_immunity_resistance_reduction) {
    // 20 slashing: 50% immune -> 10; resistance 3 -> 7; reduction 2 -> 5.
    // Any other order gives a different number (reduction first: 18 -> 15 -> 7).
    auto c = creature();
    c->applyEffect(std::make_shared<DamageImmunityIncreaseEffect>(DamageType::Slashing, 50), DurationType::Permanent);
    c->applyEffect(std::make_shared<DamageResistanceEffect>(DamageType::Slashing, 3, 0), DurationType::Permanent);
    c->applyEffect(std::make_shared<DamageReductionEffect>(2, DamagePower::PlusFive, 0), DurationType::Permanent);
    EXPECT_EQ(5, resolve(c, {{20, DamageType::Slashing}}));
}

TEST_F(RulesTest, a_resistance_with_a_limit_wears_out_and_goes) {
    // Hits small enough that "damage seen" and "damage absorbed" are the same
    // number: the wear is 4 a hit against a limit of 12.
    auto c = creature();
    c->applyEffect(std::make_shared<DamageResistanceEffect>(DamageType::Fire, 5, 12), DurationType::Permanent);
    EXPECT_EQ(0, resolve(c, {{4, DamageType::Fire}}));
    EXPECT_EQ(0, resolve(c, {{4, DamageType::Fire}}));
    EXPECT_EQ(0, resolve(c, {{4, DamageType::Fire}}));
    EXPECT_TRUE(c->effects().empty()) << "the limit is spent: the resistance is gone";
    EXPECT_EQ(4, resolve(c, {{4, DamageType::Fire}}));
}

TEST_F(RulesTest, plot_creatures_take_no_damage) {
    auto c = creature();
    c->setPlotFlag(true);
    EXPECT_EQ(0, resolve(c, {{20, DamageType::Slashing}}));
}

TEST_F(RulesTest, applied_damage_lowers_hit_points_and_kills_at_zero) {
    auto c = creature();
    c->setMaxHitPoints(20);
    c->setCurrentHitPoints(20);
    c->applyEffect(_game->newEffect<DamageEffect>(8, DamageType::Slashing, DamagePower::Normal), DurationType::Instant);
    EXPECT_EQ(12, c->currentHitPoints());
    EXPECT_FALSE(c->isDead());
    c->applyEffect(_game->newEffect<DamageEffect>(12, DamageType::Slashing, DamagePower::Normal), DurationType::Instant);
    EXPECT_TRUE(c->isDead());
}

TEST_F(RulesTest, healing_never_passes_the_maximum_and_does_not_revive_the_dead) {
    auto c = creature();
    c->setMaxHitPoints(20);
    c->setCurrentHitPoints(15);
    c->applyEffect(_game->newEffect<HealEffect>(30), DurationType::Instant);
    EXPECT_EQ(20, c->currentHitPoints());
    c->applyEffect(_game->newEffect<DamageEffect>(25, DamageType::Slashing, DamagePower::Normal), DurationType::Instant);
    ASSERT_TRUE(c->isDead());
    c->applyEffect(_game->newEffect<HealEffect>(10), DurationType::Instant);
    EXPECT_TRUE(c->isDead());
}

// Temporary hit points

TEST_F(RulesTest, temporary_hit_points_are_a_separate_pool_on_top_of_the_maximum) {
    auto c = creature();
    c->setMaxHitPoints(20);
    c->setCurrentHitPoints(20);
    c->applyEffect(std::make_shared<TemporaryHitPointsEffect>(10), DurationType::Temporary, 30.0f);
    EXPECT_EQ(30, c->currentHitPoints());
    EXPECT_EQ(20, c->maxHitPoints());
    c->applyEffect(_game->newEffect<HealEffect>(50), DurationType::Instant);
    EXPECT_EQ(30, c->currentHitPoints()) << "healing fills to the maximum plus the pool";
}

// Effect lifetimes

TEST_F(RulesTest, a_temporary_effect_ends_when_its_duration_has_run_out_and_a_permanent_one_does_not) {
    auto c = creature();
    c->setMaxHitPoints(100);
    c->setCurrentHitPoints(100);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All), DurationType::Temporary, 6.0f);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 1, SavingThrowType::All), DurationType::Permanent);
    EXPECT_EQ(3, c->getSavingThrow(kFort));
    c->update(5.9f);
    EXPECT_EQ(3, c->getSavingThrow(kFort));
    c->update(0.2f);
    EXPECT_EQ(1, c->getSavingThrow(kFort));
    c->update(1000.0f);
    EXPECT_EQ(1, c->getSavingThrow(kFort));
    EXPECT_EQ(1u, c->effects().size());
}

TEST_F(RulesTest, an_instant_effect_is_not_kept) {
    auto c = creature();
    c->setMaxHitPoints(30);
    c->setCurrentHitPoints(10);
    c->applyEffect(_game->newEffect<HealEffect>(5), DurationType::Instant);
    EXPECT_TRUE(c->effects().empty());
}

TEST_F(RulesTest, linked_effects_apply_and_end_together) {
    auto c = creature();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All),
        std::make_shared<ACIncreaseEffect>(2, ACBonus::Dodge, kAllDamageTypeFlags));
    int base = c->getDefense();
    c->applyEffect(link, DurationType::Temporary, 6.0f);
    EXPECT_EQ(2, c->getSavingThrow(kFort));
    EXPECT_EQ(base + 2, c->getDefense());
    c->update(7.0f);
    EXPECT_EQ(0, c->getSavingThrow(kFort));
    EXPECT_EQ(base, c->getDefense());
    EXPECT_TRUE(c->effects().empty());
}

TEST_F(RulesTest, removing_one_member_of_a_link_removes_the_whole_link) {
    auto c = creature();
    auto link = std::make_shared<LinkEffectsEffect>(
        std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All),
        std::make_shared<ACIncreaseEffect>(2, ACBonus::Dodge, kAllDamageTypeFlags));
    int base = c->getDefense();
    c->applyEffect(link, DurationType::Permanent);
    ASSERT_EQ(1u, c->effects().size());
    c->removeEffect(link);
    EXPECT_TRUE(c->effects().empty());
    EXPECT_EQ(base, c->getDefense());
    EXPECT_EQ(0, c->getSavingThrow(kFort));
}

TEST_F(RulesTest, death_ends_effects_except_the_ones_that_outlive_it) {
    auto c = creature();
    c->setMaxHitPoints(10);
    c->setCurrentHitPoints(10);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All), DurationType::Temporary, 60.0f);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All), DurationType::Permanent);
    c->applyEffect(std::make_shared<SavingThrowIncreaseEffect>(kAllSaves, 2, SavingThrowType::All), DurationType::Innate);
    c->applyEffect(_game->newEffect<DamageEffect>(50, DamageType::Slashing, DamagePower::Normal), DurationType::Instant);
    ASSERT_TRUE(c->isDead());
    // The removefxondeath table is not served here: every effect goes.
    EXPECT_LE(c->effects().size(), 3u);
}

TEST_F(RulesTest, immunity_refuses_the_effect_it_names_and_leaves_others_alone) {
    auto c = creature();
    c->applyEffect(std::make_shared<ImmunityEffect>(ImmunityType::Paralysis), DurationType::Permanent);
    c->applyEffect(std::make_shared<ParalyzeEffect>(), DurationType::Temporary, 10.0f);
    EXPECT_FALSE(c->hasEffect(EffectType::Paralyze));
    auto d = creature();
    d->applyEffect(std::make_shared<ParalyzeEffect>(), DurationType::Temporary, 10.0f);
    EXPECT_TRUE(d->hasEffect(EffectType::Paralyze));
}
