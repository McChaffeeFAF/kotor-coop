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

// Rules driven by 2DA columns K1 defines (campaign batch C01).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/resource.h"

#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/skills.h"
#include "reone/game/d20/spells.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/poison.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/game/script/routines.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routine.h"
#include "reone/script/variable.h"

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

// classes.2da with a soldier (row 0) and a Jedi Consular (row 4).
std::shared_ptr<TwoDA> makeClassesTable() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable",
        "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> rows(9, std::vector<std::string>(columns.size()));
    rows[0] = {"1", "2", "10", "4", "14", "10", "10", "10", "10", "10", "sol", "save", "attack", "sol", "", "", "0", "0"};
    rows[4] = {"1", "2", "6", "2", "10", "10", "10", "10", "14", "10", "jcn", "save", "attack", "jcn", "", "JCN", "8", "1"};
    // A class with a force die but no spellcasting gets no force points.
    rows[6] = {"1", "2", "12", "2", "10", "10", "10", "10", "10", "10", "sol", "save", "attack", "", "", "", "5", "0"};
    return makeTable(columns, rows);
}

// feat.2da: 4 rows; the soldier is granted feat 1 at level 1 and feat 3 at
// level 2, and recommends 2 then 0.
std::shared_ptr<TwoDA> makeFeatTable() {
    return makeTable(
        {"label", "sol_list", "sol_granted", "sol_recom", "jcn_list", "jcn_granted", "jcn_recom"},
        {{"f0", "0", "-1", "2", "0", "-1", ""},
         {"f1", "3", "1", "", "4", "-1", ""},
         {"f2", "1", "-1", "1", "0", "1", "1"},
         {"f3", "3", "2", "", "4", "", ""}});
}

std::shared_ptr<TwoDA> makeSkillsTable() {
    return makeTable(
        {"label", "sol_class", "sol_reco", "jcn_class", "jcn_reco"},
        {{"computeruse", "0", "3", "1", ""},
         {"demolitions", "1", "1", "0", ""},
         {"stealth", "0", "", "0", ""},
         {"awareness", "1", "2", "1", "1"}});
}

class ClassRules : public Test {
protected:
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    std::unique_ptr<Classes> classes;

    void SetUp() override {
        ON_CALL(twoDas, get("classes")).WillByDefault(Return(makeClassesTable()));
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeSkillsTable()));
        ON_CALL(twoDas, get("feat")).WillByDefault(Return(makeFeatTable()));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
        classes = std::make_unique<Classes>(strings, twoDas);
    }
};

class FakeFeats : public IFeats {
public:
    std::set<FeatType> selectable;
    // A feat that needs another (prerequisite -> dependant).
    std::map<FeatType, FeatType> requires;

    void init() override {}
    std::shared_ptr<Feat> get(FeatType) const override { return nullptr; }
    int getLevelUpChoiceCount(const CreatureAttributes &, const CreatureClass &) const override { return 0; }
    bool isLevelUpCandidate(FeatType type, const CreatureAttributes &attributes, const CreatureClass &) const override {
        if (attributes.hasFeat(type) || selectable.count(type) == 0) {
            return false;
        }
        auto it = requires.find(type);
        return it == requires.end() || attributes.hasFeat(it->second);
    }
    std::vector<FeatType> getLevelUpCandidates(const CreatureAttributes &, const CreatureClass &) const override { return {}; }
    std::vector<FeatDisplayEntry> getLevelUpDisplayEntries(const CreatureAttributes &, const CreatureClass &) const override { return {}; }
};

} // namespace

TEST_F(ClassRules, force_die_counts_only_for_spellcaster_classes) {
    auto soldier = classes->get(ClassType::Soldier);
    auto consular = classes->get(ClassType::JediConsular);
    auto combatDroid = classes->get(ClassType::CombatDroid);
    ASSERT_TRUE(soldier && consular && combatDroid);

    EXPECT_FALSE(soldier->isSpellCaster());
    EXPECT_EQ(0, soldier->forcePointsPerLevel());
    EXPECT_TRUE(consular->isSpellCaster());
    EXPECT_EQ(8, consular->forcePointsPerLevel());
    EXPECT_EQ(0, combatDroid->forcePointsPerLevel());

    CreatureAttributes attributes;
    attributes.addClassLevels(soldier.get(), 3);
    attributes.addClassLevels(consular.get(), 2);
    EXPECT_EQ(16, attributes.getAggregateForceDie());
}

TEST_F(ClassRules, granted_feats_follow_class_level) {
    auto soldier = classes->get(ClassType::Soldier);
    ASSERT_TRUE(soldier);

    EXPECT_THAT(soldier->getGrantedFeats(1), ElementsAre(static_cast<FeatType>(1)));
    EXPECT_THAT(soldier->getGrantedFeats(2), ElementsAre(static_cast<FeatType>(3)));
    EXPECT_TRUE(soldier->getGrantedFeats(3).empty());

    // A new character of the class starts with its level-1 grants.
    EXPECT_TRUE(soldier->defaultAttributes().hasFeat(static_cast<FeatType>(1)));
    EXPECT_FALSE(soldier->defaultAttributes().hasFeat(static_cast<FeatType>(3)));

    CreatureAttributes attributes;
    soldier->grantFeatsUpToLevel(attributes, 2);
    EXPECT_TRUE(attributes.hasFeat(static_cast<FeatType>(1)));
    EXPECT_TRUE(attributes.hasFeat(static_cast<FeatType>(3)));
    EXPECT_FALSE(attributes.hasFeat(static_cast<FeatType>(0)));

    // Another class's grant column is its own.
    auto consular = classes->get(ClassType::JediConsular);
    EXPECT_THAT(consular->getGrantedFeats(1), ElementsAre(static_cast<FeatType>(2)));
}

TEST_F(ClassRules, recommendations_are_ordered_by_priority) {
    auto soldier = classes->get(ClassType::Soldier);
    ASSERT_TRUE(soldier);
    EXPECT_THAT(soldier->recommendedFeats(), ElementsAre(static_cast<FeatType>(2), static_cast<FeatType>(0)));
    EXPECT_THAT(soldier->recommendedSkills(), ElementsAre(SkillType::Demolitions, SkillType::Awareness, SkillType::ComputerUse));

    auto consular = classes->get(ClassType::JediConsular);
    EXPECT_THAT(consular->recommendedSkills(), ElementsAre(SkillType::Awareness));
}

TEST_F(ClassRules, recommended_feats_skip_non_candidates_and_respect_count) {
    auto soldier = classes->get(ClassType::Soldier);
    ASSERT_TRUE(soldier);
    FakeFeats feats;
    feats.selectable = {static_cast<FeatType>(0), static_cast<FeatType>(2)};
    CreatureAttributes attributes;

    EXPECT_THAT(pickRecommendedFeats(feats, attributes, *soldier, 1), ElementsAre(static_cast<FeatType>(2)));
    EXPECT_THAT(pickRecommendedFeats(feats, attributes, *soldier, 5),
                ElementsAre(static_cast<FeatType>(2), static_cast<FeatType>(0)));

    // Feat 0 needs feat 2: it becomes a candidate once 2 is picked first.
    feats.requires[static_cast<FeatType>(0)] = static_cast<FeatType>(2);
    EXPECT_THAT(pickRecommendedFeats(feats, attributes, *soldier, 2),
                ElementsAre(static_cast<FeatType>(2), static_cast<FeatType>(0)));

    attributes.addFeat(static_cast<FeatType>(2));
    EXPECT_THAT(pickRecommendedFeats(feats, attributes, *soldier, 2), ElementsAre(static_cast<FeatType>(0)));
}

TEST(SkillRules, recommended_allocation_fills_each_skill_to_its_cap_in_order) {
    std::map<SkillType, int> ranks {{SkillType::Stealth, 1}};
    auto cost = [](SkillType skill) { return skill == SkillType::Awareness ? 2 : 1; };
    auto cap = [](SkillType skill) { return skill == SkillType::Demolitions ? 4 : 2; };

    int left = allocateRecommendedSkills(
        {SkillType::Demolitions, SkillType::Awareness, SkillType::Stealth}, ranks, 7, cost, cap);

    EXPECT_EQ(4, ranks[SkillType::Demolitions]);
    EXPECT_EQ(1, ranks[SkillType::Awareness]); // 3 points left: one rank at cost 2
    EXPECT_EQ(2, ranks[SkillType::Stealth]);   // the last point
    EXPECT_EQ(0, left);
}

TEST(SkillRules, skill_value_applies_use_rules) {
    Skill stealth;
    stealth.untrained = false;
    stealth.keyAbility = Ability::Dexterity;
    stealth.armorCheckPenalty = true;
    stealth.droidCanUse = false;

    SkillUser user;
    user.ranks = 0;
    user.keyAbilityModifier = 3;
    EXPECT_EQ(0, computeSkillValue(stealth, user)); // trained-only, no ranks

    user.ranks = 4;
    user.armorCheckPenalty = 2;
    EXPECT_EQ(5, computeSkillValue(stealth, user)); // 4 + 3 - 2

    user.droid = true;
    EXPECT_EQ(-1, computeSkillValue(stealth, user));

    Skill persuade;
    persuade.npcCanUse = false;
    SkillUser npc;
    npc.partyMember = false;
    npc.keyAbilityModifier = 2;
    EXPECT_EQ(-1, computeSkillValue(persuade, npc));
    npc.partyMember = true;
    npc.armorCheckPenalty = 5; // persuade ignores armour
    EXPECT_EQ(2, computeSkillValue(persuade, npc)); // untrained: 0 ranks + modifier
}

TEST(PoisonRules, profile_reads_poison_row) {
    auto poison = makeTable(
        {"label", "dc_save", "duration", "period", "dam_hp", "dam_fp", "dam_str", "dam_dex", "dam_con", "dam_int", "dam_wis", "dam_chr", "abil_dur"},
        {{"mild", "15", "36", "12", "0", "0", "1", "1", "0", "0", "0", "1", "60"},
         {"dmg", "25", "30", "3", "4", "2", "0", "0", "0", "0", "0", "0", ""}});
    auto mild = loadPoisonProfile(*poison, 0);
    ASSERT_TRUE(mild);
    EXPECT_EQ(15, mild->saveDC);
    EXPECT_FLOAT_EQ(36.0f, mild->duration);
    EXPECT_FLOAT_EQ(12.0f, mild->period);
    EXPECT_EQ(1, mild->abilityDamage[static_cast<int>(Ability::Strength)]);
    EXPECT_EQ(0, mild->abilityDamage[static_cast<int>(Ability::Constitution)]);
    EXPECT_EQ(1, mild->abilityDamage[static_cast<int>(Ability::Charisma)]);
    EXPECT_FLOAT_EQ(60.0f, mild->abilityDuration);
    auto damage = loadPoisonProfile(*poison, 1);
    ASSERT_TRUE(damage);
    EXPECT_EQ(4, damage->hitPointDamage);
    EXPECT_EQ(2, damage->forceDamage);
    EXPECT_FALSE(loadPoisonProfile(*poison, 2));
}

namespace {

std::shared_ptr<TwoDA> makeRestrictedBaseItems() {
    // row 0: armour needing feats 5 and 6, denied to subrace 1 (Wookiee);
    // row 1: melee weapon for non-droids; row 2: droid-only item.
    return makeTable(
        {"equipableslots", "itemclass", "stacking", "reqfeat0", "reqfeat1", "reqfeat2", "droidorhuman", "denysubrace", "armorcheckpen"},
        {{"0x00002", "a_armor", "1", "5", "6", "", "1", "0x00000002", "3"},
         {"0x00030", "w_sword", "99", "", "", "", "1", "0x00000000", ""},
         {"0x00001", "d_scope", "", "", "", "", "2", "0x00000000", ""}});
}

std::shared_ptr<Item> makeItem(Game &game, int baseItem) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", "item" + std::to_string(baseItem)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", 1))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    return item;
}

class RulesEngine : public TestEngine {
public:
    RulesEngine() { init(); }
};

class ItemRules : public Test {
protected:
    RulesEngine engine;
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeRestrictedBaseItems()));
    }
};

} // namespace

TEST_F(ItemRules, baseitems_stacking_is_the_stack_limit_when_maxstack_is_absent) {
    EXPECT_EQ(1, makeItem(game, 0)->maxStackSize());
    EXPECT_EQ(99, makeItem(game, 1)->maxStackSize());
    EXPECT_EQ(std::numeric_limits<uint16_t>::max(), makeItem(game, 2)->maxStackSize());
}

TEST_F(ItemRules, use_limits_follow_required_feats_body_type_and_subrace) {
    auto armor = makeItem(game, 0);
    auto sword = makeItem(game, 1);
    auto scope = makeItem(game, 2);
    EXPECT_THAT(armor->requiredFeats(), ElementsAre(static_cast<FeatType>(5), static_cast<FeatType>(6)));
    EXPECT_EQ(3, armor->armorCheckPenalty());

    auto human = game.newCreature();
    human->setRacialType(RacialType::Human);
    EXPECT_EQ(EquipmentCandidateReason::MissingRequiredFeat, getItemUseRestriction(*human, *armor));
    human->attributes().addFeat(static_cast<FeatType>(5));
    EXPECT_EQ(EquipmentCandidateReason::MissingRequiredFeat, getItemUseRestriction(*human, *armor));
    human->attributes().addFeat(static_cast<FeatType>(6));
    EXPECT_EQ(EquipmentCandidateReason::None, getItemUseRestriction(*human, *armor));
    EXPECT_EQ(EquipmentCandidateReason::None, getItemUseRestriction(*human, *sword));
    EXPECT_EQ(EquipmentCandidateReason::WrongBodyType, getItemUseRestriction(*human, *scope));

    auto wookiee = game.newCreature();
    wookiee->setRacialType(RacialType::Human);
    wookiee->setSubrace(Subrace::Wookie);
    wookiee->attributes().addFeat(static_cast<FeatType>(5));
    wookiee->attributes().addFeat(static_cast<FeatType>(6));
    EXPECT_EQ(EquipmentCandidateReason::SubraceDenied, getItemUseRestriction(*wookiee, *armor));

    auto droid = game.newCreature();
    droid->setRacialType(RacialType::Droid);
    EXPECT_EQ(EquipmentCandidateReason::WrongBodyType, getItemUseRestriction(*droid, *sword));
    EXPECT_EQ(EquipmentCandidateReason::None, getItemUseRestriction(*droid, *scope));

    // The equipment screen lists the item but will not equip it.
    auto decision = evaluateEquipmentCandidate(*droid, InventorySlots::rightWeapon, sword.get());
    EXPECT_TRUE(decision.visible);
    EXPECT_FALSE(decision.valid);
    EXPECT_EQ(EquipmentCandidateReason::WrongBodyType, decision.reason);
}

TEST_F(ItemRules, poison_doses_each_period_until_its_duration) {
    auto victim = game.newCreature();
    victim->setMaxHitPoints(100);
    victim->setCurrentHitPoints(100);

    PoisonProfile profile;
    profile.saveDC = 1000; // cannot be saved against
    profile.duration = 9.0f;
    profile.period = 3.0f;
    profile.hitPointDamage = 4;
    profile.abilityDamage[static_cast<int>(Ability::Strength)] = 1;
    profile.abilityDuration = 60.0f;
    auto poison = std::make_shared<PoisonEffect>(Poison::DamageMild, profile);

    EffectInstance instance;
    instance.id = 77;
    ASSERT_TRUE(poison->onApply(*victim, instance));
    EXPECT_TRUE(poison->onUpdate(*victim, instance, 2.0f));
    EXPECT_EQ(100, victim->currentHitPoints());
    EXPECT_TRUE(poison->onUpdate(*victim, instance, 1.0f));
    EXPECT_EQ(96, victim->currentHitPoints());
    EXPECT_TRUE(poison->onUpdate(*victim, instance, 3.0f));
    EXPECT_EQ(92, victim->currentHitPoints());
    EXPECT_FALSE(poison->onUpdate(*victim, instance, 3.0f)); // third dose, then it wears off
    EXPECT_EQ(88, victim->currentHitPoints());
    int strengthLosses = static_cast<int>(std::count_if(
        victim->effects().begin(), victim->effects().end(),
        [](const EffectInstance &effect) { return effect.type() == EffectType::AbilityDecrease; }));
    EXPECT_EQ(3, strengthLosses);

    // A save at DC 0 always succeeds: the poison never takes hold.
    profile.saveDC = 0;
    auto weak = std::make_shared<PoisonEffect>(Poison::DamageMild, profile);
    EXPECT_FALSE(weak->onApply(*victim, instance));
}

TEST(FeatRules, attack_feats_follow_usetype_and_show_highest_owned_tier) {
    std::map<FeatType, std::shared_ptr<Feat>> table;
    auto add = [&table](int id, int useType, int successor) {
        auto feat = std::make_shared<Feat>();
        feat->useType = useType;
        feat->successor = static_cast<FeatType>(successor);
        table[static_cast<FeatType>(id)] = feat;
    };
    add(28, 0, 17); // power attack -> improved
    add(17, 0, 83); // improved -> master
    add(83, 0, 0);  // master: blank successor reads as 0
    add(8, 0, 19);  // critical strike -> improved (not owned)
    add(29, 1, 18); // power blast
    add(5, -1, 0);  // not an attack feat
    auto lookup = [&table](FeatType type) {
        auto it = table.find(type);
        return it != table.end() ? it->second : nullptr;
    };
    std::set<FeatType> owned {static_cast<FeatType>(28), static_cast<FeatType>(17), static_cast<FeatType>(8),
                              static_cast<FeatType>(29), static_cast<FeatType>(5)};

    EXPECT_THAT(selectAttackFeats(owned, false, lookup),
                ElementsAre(static_cast<FeatType>(8), static_cast<FeatType>(17)));
    EXPECT_THAT(selectAttackFeats(owned, true, lookup), ElementsAre(static_cast<FeatType>(29)));

    owned.insert(static_cast<FeatType>(83));
    owned.insert(static_cast<FeatType>(0));
    EXPECT_THAT(selectAttackFeats(owned, false, lookup),
                ElementsAre(static_cast<FeatType>(8), static_cast<FeatType>(83)));
}

namespace {

class FakeSpells : public ISpells {
public:
    std::map<SpellType, std::shared_ptr<Spell>> spells;
    // power -> prerequisite
    std::map<SpellType, SpellType> requires;

    std::shared_ptr<Spell> get(SpellType type) const override {
        auto it = spells.find(type);
        return it != spells.end() ? it->second : nullptr;
    }
    bool isLevelUpCandidate(SpellType, const CreatureAttributes &, const CreatureClass &, const std::set<SpellType> &) const override { return false; }
    std::vector<SpellType> getLevelUpCandidates(const CreatureAttributes &, const CreatureClass &, const std::set<SpellType> &chosen) const override {
        std::vector<SpellType> result;
        for (const auto &[type, spell] : spells) {
            auto need = requires.find(type);
            if (chosen.count(type) == 0 && (need == requires.end() || chosen.count(need->second) > 0)) {
                result.push_back(type);
            }
        }
        return result;
    }
    std::vector<SpellDisplayEntry> getLevelUpDisplayEntries(const CreatureAttributes &, const CreatureClass &, const std::set<SpellType> &) const override { return {}; }
};

} // namespace

TEST_F(ClassRules, recommended_powers_follow_the_alignment_order) {
    auto consular = classes->get(ClassType::JediConsular);
    ASSERT_TRUE(consular);
    FakeSpells spells;
    auto add = [&spells](int id, int dark, int light) {
        auto spell = std::make_shared<Spell>();
        spell->darkRecommendation = dark;
        spell->lightRecommendation = light;
        spells.spells[static_cast<SpellType>(id)] = spell;
    };
    add(10, 0, 1);  // light side's first choice
    add(9, 1, 0);   // dark side's first choice
    add(23, 2, 2);  // second for both
    add(12, 3, 0);  // dark only, needs 23
    spells.requires[static_cast<SpellType>(12)] = static_cast<SpellType>(23);
    CreatureAttributes attributes;

    EXPECT_THAT(pickRecommendedPowers(spells, attributes, *consular, 2, false),
                ElementsAre(static_cast<SpellType>(10), static_cast<SpellType>(23)));
    EXPECT_THAT(pickRecommendedPowers(spells, attributes, *consular, 3, true),
                ElementsAre(static_cast<SpellType>(9), static_cast<SpellType>(23), static_cast<SpellType>(12)));
    // Unranked powers are never picked, even when points remain.
    EXPECT_THAT(pickRecommendedPowers(spells, attributes, *consular, 5, false),
                ElementsAre(static_cast<SpellType>(10), static_cast<SpellType>(23)));
}

TEST(SpellRules, spell_range_letters_resolve_through_ranges) {
    EXPECT_EQ(0, getSpellRangeRow("P"));
    EXPECT_EQ(1, getSpellRangeRow("T"));
    EXPECT_EQ(2, getSpellRangeRow("s"));
    EXPECT_EQ(3, getSpellRangeRow("M"));
    EXPECT_EQ(4, getSpellRangeRow("L"));
    EXPECT_EQ(19, getSpellRangeRow("W"));
    EXPECT_EQ(-1, getSpellRangeRow(""));

    NiceMock<MockTextures> textures;
    NiceMock<MockAudioClips> audioClips;
    NiceMock<MockModels> models;
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    std::vector<std::vector<std::string>> rangeRows(20, {"", "0"});
    rangeRows[1] = {"SpellRngTouch", "2.25"};
    rangeRows[3] = {"SpellRngMed", "15"};
    rangeRows[19] = {"SpellRngThrow", "15"};
    ON_CALL(twoDas, get("ranges")).WillByDefault(Return(makeTable({"label", "primaryrange"}, rangeRows)));
    ON_CALL(twoDas, get("spells")).WillByDefault(Return(makeTable(
        {"label", "range", "dark_recom", "light_recom"},
        {{"cure", "T", "", "4"}, {"push", "M", "7", ""}, {"self", "P", "", ""}})));
    Spells spells(textures, audioClips, models, strings, twoDas);
    spells.init();
    EXPECT_FLOAT_EQ(2.25f, spells.get(static_cast<SpellType>(0))->range);
    EXPECT_FLOAT_EQ(15.0f, spells.get(static_cast<SpellType>(1))->range);
    EXPECT_FLOAT_EQ(0.0f, spells.get(static_cast<SpellType>(2))->range);
    EXPECT_EQ(4, spells.get(static_cast<SpellType>(0))->lightRecommendation);
    EXPECT_EQ(7, spells.get(static_cast<SpellType>(1))->darkRecommendation);
}

TEST(RaceRules, playable_racial_type_follows_racialtypes_playerrace) {
    StubConsole console;
    auto &engine = testEngine();
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    std::vector<std::vector<std::string>> rows(7, {"", ""});
    rows[5] = {"Droid", "1"};
    rows[6] = {"Human", "0"};
    EXPECT_CALL(engine.resourceModule().twoDas(), get("racialtypes"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "playerrace"}, rows)));

    auto call = [&routines](uint32_t object) {
        script::Routine &routine = routines.get(routines.getIndexByName("GetIsPlayableRacialType"));
        script::ExecutionContext execution;
        execution.routines = &routines;
        return routine.invoke({script::Variable::ofObject(object)}, execution).intValue;
    };
    auto droid = game.newCreature();
    droid->setRacialType(RacialType::Droid);
    auto human = game.newCreature();
    human->setRacialType(RacialType::Human);
    auto elf = game.newCreature();
    elf->setRacialType(RacialType::Elf);
    EXPECT_EQ(1, call(droid->id()));
    EXPECT_EQ(0, call(human->id()));
    EXPECT_EQ(0, call(elf->id()));
    testing::Mock::VerifyAndClearExpectations(&engine.resourceModule().twoDas());
    engine.resourceModule().twoDas().allowAnyLookup();
}

TEST(EffectIconRules, set_effect_icon_links_the_icon_to_the_effect) {
    StubConsole console;
    auto &engine = testEngine();
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    Routines routines(GameID::KotOR, &game, &engine.services());
    routines.init();
    auto effect = std::make_shared<AbilityDecreaseEffect>(Ability::Dexterity, 3);
    script::Routine &routine = routines.get(routines.getIndexByName("SetEffectIcon"));
    script::ExecutionContext execution;
    execution.routines = &routines;
    auto result = routine.invoke({script::Variable::ofEffect(effect), script::Variable::ofInt(41)}, execution);
    EXPECT_EQ(effect, result.engineType);
    EXPECT_EQ(41, effect->effectIcon());
}
