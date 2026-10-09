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

/**
 * The K2 systems merge (design note k2_merge): prestige classes, Force
 * forms and the level-50 table.
 */

#include <gtest/gtest.h>

#include "reone/game/crafting.h"
#include "reone/game/d20/attributes.h"
#include "reone/game/d20/forcepoints.h"
#include "reone/game/d20/forms.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/d20/spell.h"
#include "reone/game/experience.h"
#include "reone/resource/2da.h"
#include "reone/resource/k2merge.h"

#include <limits>

using namespace reone;
using namespace reone::game;

// --- Prestige classes ---------------------------------------------------

TEST(K2Prestige, alignment_bands_follow_the_k2_engine) {
    using prestige::AlignmentBand;
    EXPECT_EQ(AlignmentBand::Dark, prestige::alignmentBand(0));
    EXPECT_EQ(AlignmentBand::Dark, prestige::alignmentBand(40));
    EXPECT_EQ(AlignmentBand::Neutral, prestige::alignmentBand(41));
    EXPECT_EQ(AlignmentBand::Neutral, prestige::alignmentBand(50));
    EXPECT_EQ(AlignmentBand::Neutral, prestige::alignmentBand(59));
    EXPECT_EQ(AlignmentBand::Light, prestige::alignmentBand(60));
    EXPECT_EQ(AlignmentBand::Light, prestige::alignmentBand(100));
    // ALIGNMENT_NEUTRAL 1, ALIGNMENT_LIGHT_SIDE 2, ALIGNMENT_DARK_SIDE 3.
    EXPECT_EQ(3, prestige::alignmentConstant(AlignmentBand::Dark));
    EXPECT_EQ(1, prestige::alignmentConstant(AlignmentBand::Neutral));
    EXPECT_EQ(2, prestige::alignmentConstant(AlignmentBand::Light));
}

namespace {

prestige::Facts revealedJedi(int goodEvil) {
    prestige::Facts facts;
    facts.k2Merged = true;
    facts.playerCharacter = true;
    facts.revealed = true;
    facts.goodEvil = goodEvil;
    facts.classes = {ClassType::Scout, ClassType::JediSentinel};
    return facts;
}

} // namespace

TEST(K2Prestige, light_dark_and_neutral_offers) {
    auto light = prestige::offeredClasses(revealedJedi(80));
    EXPECT_EQ((std::vector<ClassType> {ClassType::JediWeaponMaster, ClassType::JediMaster, ClassType::JediWatchman}), light);
    auto dark = prestige::offeredClasses(revealedJedi(10));
    EXPECT_EQ((std::vector<ClassType> {ClassType::SithMarauder, ClassType::SithLord, ClassType::SithAssassin}), dark);
    EXPECT_EQ(6u, prestige::offeredClasses(revealedJedi(50)).size());
}

TEST(K2Prestige, the_alignment_at_the_reveal_decides_the_offer) {
    auto facts = revealedJedi(90); // light now
    facts.revealGoodEvil = 20;      // dark at the reveal
    auto offer = prestige::offeredClasses(facts);
    ASSERT_EQ(3u, offer.size());
    EXPECT_EQ(ClassType::SithMarauder, offer.front());
}

TEST(K2Prestige, nothing_is_offered_before_the_reveal_or_without_the_merge) {
    auto facts = revealedJedi(50);
    facts.revealed = false;
    EXPECT_TRUE(prestige::offeredClasses(facts).empty());
    facts = revealedJedi(50);
    facts.k2Merged = false;
    EXPECT_TRUE(prestige::offeredClasses(facts).empty());
    facts = revealedJedi(50);
    facts.playerCharacter = false;
    EXPECT_TRUE(prestige::offeredClasses(facts).empty());
    facts = revealedJedi(50);
    facts.classes = {ClassType::Scout};
    EXPECT_TRUE(prestige::offeredClasses(facts).empty()); // not a Jedi
    facts = revealedJedi(50);
    facts.classes.push_back(ClassType::JediWatchman);
    EXPECT_TRUE(prestige::offeredClasses(facts).empty()); // already prestige
}

TEST(K2Prestige, grey_feat_and_neutral_surcharge) {
    EXPECT_TRUE(prestige::grantsGreyFeat(50));
    EXPECT_FALSE(prestige::grantsGreyFeat(60));
    EXPECT_FALSE(prestige::grantsGreyFeat(40));
    EXPECT_EQ(1, prestige::greySavingThrowBonus(true, 45));
    EXPECT_EQ(0, prestige::greySavingThrowBonus(true, 70));
    EXPECT_EQ(0, prestige::greySavingThrowBonus(false, 45));
    EXPECT_TRUE(prestige::waivesOppositeAlignmentSurcharge(true, 45));
    EXPECT_FALSE(prestige::waivesOppositeAlignmentSurcharge(false, 45));
    EXPECT_FALSE(prestige::waivesOppositeAlignmentSurcharge(true, 30));
}

TEST(K2Prestige, the_force_point_surcharge_is_waived_for_a_neutral_pc) {
    auto forceAdjust = resource::TwoDA::Builder()
                           .columns({"goodcost", "evilcost"})
                           .row({"1.75", "0.5"})
                           .row({"1.5", "0.7"})
                           .row({"1.5", "0.8"})
                           .row({"1.25", "0.85"})
                           .row({"1.25", "0.9"})
                           .row({"1", "1"})
                           .row({"0.9", "1.25"})
                           .row({"0.85", "1.25"})
                           .row({"0.8", "1.5"})
                           .row({"0.7", "1.5"})
                           .row({"0.5", "1.75"})
                           .build();
    Spell heal;
    heal.forcePoints = 20;
    heal.goodEvil = 'G';
    ForcePowerPayer payer;
    payer.playerCharacter = true;
    payer.goodEvil = 45; // row 4: light powers cost 1.25x
    EXPECT_EQ(25, computeForcePowerCost(heal, payer, forceAdjust.get()));
    payer.waiveOppositeSurcharge = true;
    EXPECT_EQ(20, computeForcePowerCost(heal, payer, forceAdjust.get()));
    // A discount is kept.
    Spell drain;
    drain.forcePoints = 20;
    drain.goodEvil = 'E';
    EXPECT_EQ(18, computeForcePowerCost(drain, payer, forceAdjust.get()));
    // Force Potency / Mastery: +20% on top.
    payer.formMultiplier = forms::forcePointCostMultiplier(forms::kPotency);
    EXPECT_EQ(24, computeForcePowerCost(heal, payer, forceAdjust.get()));
}

TEST(K2Prestige, a_third_class_slot_takes_the_prestige_class) {
    CreatureAttributes attributes;
    // The slot model only compares class pointers; stand-ins do.
    auto *soldier = reinterpret_cast<CreatureClass *>(0x10);
    auto *sentinel = reinterpret_cast<CreatureClass *>(0x20);
    auto *watchman = reinterpret_cast<CreatureClass *>(0x30);
    auto *lord = reinterpret_cast<CreatureClass *>(0x40);
    attributes.addMultiClass(soldier);
    attributes.addMultiClass(sentinel);
    EXPECT_TRUE(attributes.addPrestigeClass(watchman));
    ASSERT_EQ(3u, attributes.classLevels().size());
    EXPECT_EQ(watchman, attributes.classLevels().back().first);
    EXPECT_EQ(0, attributes.classLevels().back().second);
    // Base and Jedi keep positions 1 and 2, which K1's scripts read.
    EXPECT_EQ(sentinel, attributes.classLevels()[1].first);
    // Only one prestige slot, and never a class twice.
    EXPECT_FALSE(attributes.addPrestigeClass(lord));
    EXPECT_FALSE(attributes.addPrestigeClass(watchman));
    // AddMultiClass keeps K2's two-slot rule for scripts: it replaces the last.
    attributes.addMultiClass(lord);
    EXPECT_EQ(3u, attributes.classLevels().size());
    EXPECT_EQ(lord, attributes.classLevels().back().first);
}

// --- Force forms -------------------------------------------------------------

TEST(K2Forms, lightsaber_forms_need_a_lightsaber) {
    EXPECT_TRUE(forms::isInEffect(forms::kShien, forms::kShien, true));
    EXPECT_FALSE(forms::isInEffect(forms::kShien, forms::kShien, false));
    EXPECT_TRUE(forms::isInEffect(forms::kPotency, forms::kPotency, false));
    EXPECT_FALSE(forms::isInEffect(forms::kShien, forms::kAtaru, true));
    EXPECT_EQ(forms::kNoForm, forms::saberFormInEffect(forms::kJuyo, false));
    EXPECT_EQ(forms::kNoForm, forms::saberFormInEffect(forms::kChannel, true));
}

TEST(K2Forms, attack_damage_and_defense) {
    EXPECT_EQ(1, forms::attackModifier(forms::kShiiCho, false));
    EXPECT_EQ(0, forms::attackModifier(forms::kMakashi, false));
    EXPECT_EQ(3, forms::attackModifier(forms::kMakashi, true));
    EXPECT_EQ(2, forms::attackModifier(forms::kShien, false));
    EXPECT_EQ(3, forms::damageModifier(forms::kMakashi));
    EXPECT_EQ(3, forms::defenseModifier(forms::kShiiCho, false));
    EXPECT_EQ(0, forms::defenseModifier(forms::kShiiCho, true));
    EXPECT_EQ(2, forms::defenseModifier(forms::kSoresu, true));
    EXPECT_EQ(-2, forms::defenseModifier(forms::kAtaru, false));
    EXPECT_EQ(3, forms::defenseModifier(forms::kAtaru, true));
    EXPECT_EQ(-5, forms::defenseModifier(forms::kShien, true));
    EXPECT_EQ(1, forms::defenseModifier(forms::kNiman, true));
    EXPECT_EQ(-2, forms::defenseModifier(forms::kJuyo, true));
    EXPECT_EQ(-4, forms::defenseModifier(forms::kJuyo, false));
    EXPECT_EQ(-1, forms::threatRangeModifier(forms::kSoresu));
    EXPECT_EQ(1, forms::threatRangeModifier(forms::kAtaru));
    EXPECT_EQ(1, forms::criticalMultiplierModifier(forms::kShien));
    EXPECT_EQ(4, forms::criticalConfirmationBonus(forms::kJuyo));
    EXPECT_EQ(1, forms::extraAttacks(forms::kJuyo));
}

TEST(K2Forms, saves_costs_and_regeneration) {
    EXPECT_EQ(2, forms::forceSaveModifier(forms::kMakashi, true));
    EXPECT_EQ(0, forms::forceSaveModifier(forms::kMakashi, false));
    EXPECT_EQ(2, forms::forceSaveModifier(forms::kChannel, false));
    EXPECT_EQ(-4, forms::forceSaveModifier(forms::kMastery, false));
    EXPECT_EQ(-2, forms::opponentForceSaveModifier(forms::kMastery));
    EXPECT_FLOAT_EQ(1.2f, forms::forcePointCostMultiplier(forms::kPotency));
    EXPECT_FLOAT_EQ(1.0f, forms::forcePointCostMultiplier(forms::kChannel));
    EXPECT_FLOAT_EQ(0.1f, forms::inCombatForceRegenBase(forms::kNoForm, true, 0.1f, 1.0f));
    EXPECT_FLOAT_EQ(0.3f, forms::inCombatForceRegenBase(forms::kNiman, true, 0.1f, 1.0f));
    EXPECT_FLOAT_EQ(0.1f, forms::inCombatForceRegenBase(forms::kNiman, false, 0.1f, 1.0f));
    EXPECT_FLOAT_EQ(0.6f, forms::inCombatForceRegenBase(forms::kAffinity, false, 0.1f, 1.0f));
    EXPECT_FLOAT_EQ(0.5f, forms::forceRegenBonus(forms::kChannel));
}

TEST(K2Forms, the_form_schedule_follows_k2s_teachers) {
    EXPECT_TRUE(forms::formsDue(ClassType::Scout, 20, 50).empty());
    EXPECT_EQ((std::vector<int> {forms::kShiiCho, forms::kChannel}), forms::formsDue(ClassType::JediGuardian, 1, 50));
    EXPECT_EQ((std::vector<int> {forms::kShiiCho, forms::kChannel, forms::kSoresu, forms::kAtaru, forms::kMakashi,
                                 forms::kShien, forms::kNiman, forms::kJuyo}),
              forms::formsDue(ClassType::JediWeaponMaster, 12, 50));
    // The Consular line learns the dark-side Potency first below 50.
    auto dark = forms::formsDue(ClassType::SithLord, 8, 20);
    auto light = forms::formsDue(ClassType::JediMaster, 8, 80);
    ASSERT_GE(dark.size(), 4u);
    ASSERT_GE(light.size(), 4u);
    EXPECT_EQ(forms::kPotency, dark[3]);
    EXPECT_EQ(forms::kAffinity, light[3]);
    // The Sentinel line ends with Niman, already due at 10.
    auto sentinel = forms::formsDue(ClassType::JediSentinel, 12, 50);
    EXPECT_EQ(forms::kShien, sentinel[3]);
    EXPECT_EQ(7u, sentinel.size());
}

// --- The level-50 table ----------------------------------------------------

namespace {

class TableSet : public resource::ITwoDAs {
public:
    void clear() override {}
    std::shared_ptr<resource::TwoDA> get(const std::string &key) override {
        auto it = tables.find(key);
        return it != tables.end() ? it->second : nullptr;
    }
    std::map<std::string, std::shared_ptr<resource::TwoDA>> tables;
};

// Levels 1..levels at level * (level - 1) * 500 XP, then the unreachable cap.
std::shared_ptr<resource::TwoDA> expTable(int levels) {
    resource::TwoDA::Builder builder;
    builder.columns({"level", "xp"});
    for (int level = 1; level <= levels; ++level) {
        builder.row({std::to_string(level), std::to_string(level * (level - 1) * 500)});
    }
    builder.row({std::to_string(levels + 1), "0xFFFFFFFF"});
    return builder.build();
}

} // namespace

TEST(K2LevelCap, the_merged_exptable_runs_to_level_50) {
    TableSet k1;
    TableSet k2;
    k1.tables["exptable"] = expTable(20);
    k2.tables["exptable"] = expTable(50);
    resource::K2MergedTwoDAs merged(k1, k2);

    // K1 alone: level 20 is the cap.
    EXPECT_EQ(20, levelForExperience(k1, 10000000));
    EXPECT_EQ(std::numeric_limits<int>::max(), experienceForNextLevel(k1, 20));
    // Merged: level 21 at 210,000 XP, and a cap of 50.
    EXPECT_EQ(210000, experienceForNextLevel(merged, 20));
    EXPECT_EQ(21, levelForExperience(merged, 210000));
    EXPECT_EQ(50, levelForExperience(merged, 100000000));
    EXPECT_EQ(std::numeric_limits<int>::max(), experienceForNextLevel(merged, 50));
}

TEST(K2LevelCap, per_level_tables_run_to_level_50) {
    TableSet k1;
    TableSet k2;
    resource::TwoDA::Builder k1Attack;
    k1Attack.columns({"bab"});
    resource::TwoDA::Builder k2Attack;
    k2Attack.columns({"bab"});
    for (int level = 1; level <= 50; ++level) {
        if (level <= 20) {
            k1Attack.row({std::to_string(level)});
        }
        k2Attack.row({std::to_string(level)});
    }
    k1.tables["cls_atk_1"] = k1Attack.build();
    k2.tables["cls_atk_1"] = k2Attack.build();
    resource::K2MergedTwoDAs merged(k1, k2);
    auto table = merged.get("cls_atk_1");
    ASSERT_NE(nullptr, table);
    EXPECT_EQ(50, table->getRowCount());
    EXPECT_EQ(20, table->getInt(19, "bab"));
    EXPECT_EQ(50, table->getInt(49, "bab"));
}

TEST(K2Merge, our_grey_row_follows_the_merged_feats) {
    TableSet k1;
    TableSet k2;
    k1.tables["feat"] = resource::TwoDA::Builder().columns({"label", "name", "icon"}).row({"A", "1", "i_a"}).build();
    k2.tables["feat"] = resource::TwoDA::Builder()
                            .columns({"label", "name", "icon"})
                            .row({"A", "1", "i_a"})
                            .row({"B", "2", "i_b"})
                            .build();
    resource::K2MergedTwoDAs merged(k1, k2);
    auto feats = merged.get("feat");
    ASSERT_EQ(3, feats->getRowCount());
    int grey = merged.ownRowIndex("feat", prestige::kGreyFeatLabel);
    EXPECT_EQ(2, grey);
    EXPECT_EQ(prestige::kGreyFeatLabel, feats->getString(grey, "label"));
    EXPECT_TRUE(resource::isOwnStrRef(feats->getInt(grey, "name")));
    EXPECT_EQ(-1, merged.ownRowIndex("spells", prestige::kGreyFeatLabel));
}

// --- Crafting ------------------------------------------------------------------

TEST(K2Crafting, material_value_and_breakdown_yield) {
    EXPECT_EQ(1, crafting::materialValue(0));
    EXPECT_EQ(1, crafting::materialValue(39));
    EXPECT_EQ(25, crafting::materialValue(500));
    // Repair (or Treat Injury) rank / 20, capped at 1, truncated, at least 1.
    EXPECT_EQ(1, crafting::breakdownYield(25, 0));
    EXPECT_EQ(12, crafting::breakdownYield(25, 10));
    EXPECT_EQ(25, crafting::breakdownYield(25, 20));
    EXPECT_EQ(25, crafting::breakdownYield(25, 40));
}

TEST(K2Crafting, chemical_items_and_unbreakable_items) {
    using crafting::Material;
    EXPECT_EQ(Material::Chemicals, crafting::materialOfBaseItem(55)); // medical equipment
    EXPECT_EQ(Material::Chemicals, crafting::materialOfBaseItem(25)); // fragmentation grenade
    EXPECT_EQ(Material::Chemicals, crafting::materialOfBaseItem(54)); // combat shots
    EXPECT_EQ(Material::Components, crafting::materialOfBaseItem(12)); // blaster pistol
    EXPECT_EQ(Material::Components, crafting::materialOfBaseItem(8)); // lightsaber
    EXPECT_EQ(SkillType::Repair, crafting::breakdownSkill(Material::Components));
    EXPECT_EQ(SkillType::TreatInjury, crafting::breakdownSkill(Material::Chemicals));
    EXPECT_FALSE(crafting::isBreakableBaseItem(57)); // credits
    EXPECT_FALSE(crafting::isBreakableBaseItem(52)); // datapad
    EXPECT_TRUE(crafting::isBreakableBaseItem(12));
}

TEST(K2Crafting, recipes_come_from_k2s_tables) {
    TableSet tables;
    tables.tables["itemcreate"] = resource::TwoDA::Builder()
                                      .columns({"label", "skill", "group", "level", "align", "base_skill", "dunno", "overridetag"})
                                      .row({"u_r_targ_01", "4", "1", "0", "0", "3", "0", ""})
                                      .row({"w_blaste_05", "10", "4", "0", "0", "5", "0", ""})
                                      .build();
    tables.tables["chemicalcreate"] = resource::TwoDA::Builder()
                                          .columns({"label", "skilllevel", "group", "base_skill"})
                                          .row({"g_i_medeqpmnt01", "1", "0", "7"})
                                          .row({"hkpart01", "0", "5", "7"})
                                          .build();
    auto recipes = crafting::loadRecipes(tables);
    // The upgrade part (group 1) and the quest item (group 5) are not offered.
    ASSERT_EQ(2u, recipes.size());
    EXPECT_EQ("w_blaste_05", recipes[0].templateResRef);
    EXPECT_EQ(SkillType::Repair, recipes[0].skill);
    EXPECT_EQ(10, recipes[0].skillRank);
    EXPECT_EQ(crafting::Material::Components, recipes[0].material);
    EXPECT_EQ("g_i_medeqpmnt01", recipes[1].templateResRef);
    EXPECT_EQ(SkillType::TreatInjury, recipes[1].skill);
    EXPECT_EQ(crafting::Material::Chemicals, recipes[1].material);
}

// --- New classes at a level-up (co-op guests) ---------------------------

TEST(K2PrestigeNewClass, an_unlocked_prestige_class_of_the_right_band_is_allowed) {
    using V = prestige::NewClassVerdict;
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(revealedJedi(80), ClassType::JediMaster));
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(revealedJedi(10), ClassType::SithLord));
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(revealedJedi(50), ClassType::JediWatchman));
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(revealedJedi(50), ClassType::SithAssassin)) << "neutral gets all six";
}

TEST(K2PrestigeNewClass, a_locked_prestige_class_is_refused_as_locked) {
    using V = prestige::NewClassVerdict;
    auto notRevealed = revealedJedi(80);
    notRevealed.revealed = false;
    EXPECT_EQ(V::Locked, prestige::checkNewClass(notRevealed, ClassType::JediMaster));
    auto notMerged = revealedJedi(80);
    notMerged.k2Merged = false;
    EXPECT_EQ(V::Locked, prestige::checkNewClass(notMerged, ClassType::JediMaster));
    auto noJedi = revealedJedi(80);
    noJedi.classes = {ClassType::Soldier};
    EXPECT_EQ(V::Locked, prestige::checkNewClass(noJedi, ClassType::JediMaster));
    auto notPc = revealedJedi(80);
    notPc.playerCharacter = false;
    EXPECT_EQ(V::Locked, prestige::checkNewClass(notPc, ClassType::JediMaster));
}

TEST(K2PrestigeNewClass, the_wrong_alignment_band_is_refused_as_alignment) {
    using V = prestige::NewClassVerdict;
    EXPECT_EQ(V::Alignment, prestige::checkNewClass(revealedJedi(80), ClassType::SithLord));
    EXPECT_EQ(V::Alignment, prestige::checkNewClass(revealedJedi(10), ClassType::JediWeaponMaster));
    EXPECT_EQ(V::Alignment, prestige::checkNewClass(revealedJedi(60), ClassType::SithMarauder)) << "60 is light";
    EXPECT_EQ(V::Alignment, prestige::checkNewClass(revealedJedi(40), ClassType::JediMaster)) << "40 is dark";
    // The band at the reveal decides, not the alignment now.
    auto drifted = revealedJedi(10);
    drifted.revealGoodEvil = 80;
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(drifted, ClassType::JediMaster));
    EXPECT_EQ(V::Alignment, prestige::checkNewClass(drifted, ClassType::SithLord));
}

TEST(K2PrestigeNewClass, a_fourth_class_or_a_second_prestige_class_has_no_slot) {
    using V = prestige::NewClassVerdict;
    auto full = revealedJedi(80);
    full.classes = {ClassType::Soldier, ClassType::JediGuardian, ClassType::JediMaster};
    EXPECT_EQ(V::SlotsFull, prestige::checkNewClass(full, ClassType::JediWatchman));
    EXPECT_EQ(V::SlotsFull, prestige::checkNewClass(full, ClassType::Scout));
    auto twoBase = revealedJedi(80);
    twoBase.classes = {ClassType::Soldier, ClassType::Scout};
    EXPECT_EQ(V::SlotsFull, prestige::checkNewClass(twoBase, ClassType::Scoundrel));
    auto prestiged = revealedJedi(80);
    prestiged.classes = {ClassType::JediGuardian, ClassType::JediMaster};
    EXPECT_EQ(V::SlotsFull, prestige::checkNewClass(prestiged, ClassType::Scout)) << "no base class after a prestige class";
}

TEST(K2PrestigeNewClass, a_second_base_class_needs_no_unlock) {
    using V = prestige::NewClassVerdict;
    prestige::Facts plain;
    plain.classes = {ClassType::Soldier};
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(plain, ClassType::Scout));
    EXPECT_EQ(V::Allowed, prestige::checkNewClass(plain, ClassType::JediConsular));
    EXPECT_EQ(V::Invalid, prestige::checkNewClass(plain, ClassType::Soldier)) << "already held";
}

TEST(K2PrestigeNewClass, classes_a_level_up_cannot_take_are_invalid) {
    using V = prestige::NewClassVerdict;
    prestige::Facts plain;
    plain.classes = {ClassType::Soldier};
    for (auto type : {ClassType::CombatDroid, ClassType::ExpertDroid, ClassType::Minion, ClassType::TechSpecialist,
                      ClassType::BountyHunter, ClassType::Invalid, static_cast<ClassType>(99), static_cast<ClassType>(-1)}) {
        EXPECT_EQ(V::Invalid, prestige::checkNewClass(plain, type)) << static_cast<int>(type);
    }
}
