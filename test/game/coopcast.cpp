/* Copyright (c) 2026 The reone project contributors */

// Story Mode stand-ins: who a guest plays (coopcast.h) and the automatic
// level-up the copies and "Auto Level Up" share (autolevel.h).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/resource.h"

#include "reone/game/autolevel.h"
#include "reone/game/coopcast.h"
#include "reone/game/coopstandin.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spells.h"
#include "reone/resource/2da.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

constexpr int kBastila = 0;
constexpr int kCanderous = 1;
constexpr int kCarth = 2;
constexpr int kHK47 = 3;
constexpr int kJolee = 4;
constexpr int kJuhani = 5;
constexpr int kMission = 6;
constexpr int kT3M4 = 7;
constexpr int kZaalbar = 8;

CoopCastInput input(const std::string &module) {
    CoopCastInput result;
    result.module = module;
    return result;
}

bool skipped(const CoopCastChoice &choice, const std::string &name, const std::string &why) {
    for (const auto &[who, reason] : choice.skipped) {
        if (who == name && reason.find(why) != std::string::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(CoopCast, the_endar_spire_has_no_companion_to_copy_so_shepard_turns_up_as_a_republic_soldier) {
    auto choice = chooseCoopCast(input("end_m01aa"));
    EXPECT_EQ(CoopCastKind::Generic, choice.kind);
    EXPECT_EQ(kCoopShepardTemplate, choice.templ);
    EXPECT_EQ("Republic soldier", choice.name);
    EXPECT_EQ(ClassType::Soldier, choice.clazz);
    EXPECT_NE(std::string::npos, choice.reason.find("John Shepard"));
    EXPECT_EQ(0, choice.tier);
}

TEST(CoopCast, shepard_is_one_identity_and_only_the_outfit_changes_by_area) {
    EXPECT_EQ("John Shepard", coopShepardName());
    EXPECT_EQ(170, coopShepard().appearance);
    EXPECT_EQ(24, coopShepard().portrait);
    EXPECT_EQ(Gender::Male, coopShepard().gender);
    const auto *endar = coopGenericFor("end_m01ab");
    const auto *dantooine = coopGenericFor("danm13");
    const auto *lowerCity = coopGenericFor("tar_m03aa");
    const auto *upperCity = coopGenericFor("tar_m02aa");
    ASSERT_TRUE(endar && dantooine && lowerCity && upperCity);
    EXPECT_STREQ("Jedi apprentice", dantooine->label);
    EXPECT_EQ(ClassType::JediGuardian, dantooine->clazz);
    EXPECT_STREQ("swoop gang member", lowerCity->label) << "the longest prefix wins";
    EXPECT_STREQ("Taris mercenary", upperCity->label);
    EXPECT_NE(std::string(endar->kit[0].second), std::string(dantooine->kit[0].second));
    EXPECT_EQ(nullptr, coopGenericFor("stunt_00"));
}

TEST(CoopCast, taris_copies_the_first_companion_who_fits_preferring_a_role_the_party_lacks) {
    auto in = input("tar_m02aa");
    in.partyNpcs = {kCarth};
    in.partyRoles = {CoopPartyRole::Tank}; // a soldier PC and Carth
    auto choice = chooseCoopCast(in);
    ASSERT_EQ(CoopCastKind::Copy, choice.kind);
    EXPECT_EQ(kMission, choice.npc) << "Mission is first and a scout";
    EXPECT_EQ("p_mission", choice.templ);
    EXPECT_EQ(CoopPartyRole::Scout, choice.role);
    EXPECT_TRUE(skipped(choice, "Carth", "in the party"));

    // With a scout already along, the first who brings what is missing.
    in.partyRoles = {CoopPartyRole::Scout};
    EXPECT_EQ(kCanderous, chooseCoopCast(in).npc) << "Zaalbar is a scout too; Canderous a tank";

    // Every role covered: the first who fits.
    in.partyRoles = {CoopPartyRole::Tank, CoopPartyRole::Scout, CoopPartyRole::Caster};
    EXPECT_EQ(kMission, chooseCoopCast(in).npc);
}

TEST(CoopCast, a_copy_is_never_in_the_party_another_guests_or_present_in_the_module) {
    auto in = input("tar_m04aa");
    in.partyNpcs = {kMission};
    in.takenNpcs = {kZaalbar};
    in.presentNpcs = {kCanderous};
    auto choice = chooseCoopCast(in);
    ASSERT_EQ(CoopCastKind::Copy, choice.kind);
    EXPECT_EQ(kT3M4, choice.npc);
    EXPECT_TRUE(skipped(choice, "Mission", "in the party"));
    EXPECT_TRUE(skipped(choice, "Zaalbar", "another guest"));
    EXPECT_TRUE(skipped(choice, "Canderous", "in this module"));

    // Nobody left: the character of the place.
    in.presentNpcs = {kCanderous, kT3M4, kCarth};
    choice = chooseCoopCast(in);
    EXPECT_EQ(CoopCastKind::Generic, choice.kind);
    EXPECT_EQ("swoop gang member", choice.name);
    EXPECT_NE(std::string::npos, choice.reason.find("no companion fits Taris"));
}

TEST(CoopCast, story_clashes_keep_companions_out_and_globals_lift_them) {
    // Bastila is never copied on Taris (a captive until rescued) nor on the Leviathan.
    EXPECT_FALSE(coopCopyExclusion(input("tar_m02aa"), kBastila).empty());
    EXPECT_FALSE(coopCopyExclusion(input("lev_m40aa"), kBastila).empty());
    EXPECT_FALSE(coopCopyExclusion(input("lev_m40aa"), kCarth).empty());
    EXPECT_FALSE(coopCopyExclusion(input("end_m01aa"), kCarth).empty());
    EXPECT_TRUE(coopCopyExclusion(input("danm13"), kBastila).empty());

    // Juhani waits in the grove until DAN_JUHANI_DONE; Zaalbar is a prisoner while kas_ZaalbarPris.
    std::map<std::string, bool> globals;
    auto in = input("danm14aa");
    in.globalBoolean = [&globals](const std::string &name) { return globals[name]; };
    EXPECT_NE(std::string::npos, coopCopyExclusion(in, kJuhani).find("grove"));
    globals["DAN_JUHANI_DONE"] = true;
    EXPECT_TRUE(coopCopyExclusion(in, kJuhani).empty());

    in.module = "kas_m22aa";
    EXPECT_TRUE(coopCopyExclusion(in, kZaalbar).empty());
    globals["kas_ZaalbarPris"] = true;
    EXPECT_NE(std::string::npos, coopCopyExclusion(in, kZaalbar).find("prisoner"));
    EXPECT_FALSE(coopCopyExclusion(in, kJolee).empty()) << "Jolee is in the Shadowlands until G_JoleeJoined";
    globals["G_JoleeJoined"] = true;
    EXPECT_TRUE(coopCopyExclusion(in, kJolee).empty());

    // HK-47 is for sale on Tatooine, so not copied there; Korriban takes him.
    EXPECT_FALSE(coopCopyExclusion(input("tat_m17aa"), kHK47).empty());
    auto korriban = chooseCoopCast(input("korr_m33aa"));
    EXPECT_EQ(kCanderous, korriban.npc);
    EXPECT_EQ(2, korriban.tier);
}

TEST(CoopCast, modules_the_tables_do_not_know_copy_the_solo_character_as_before) {
    auto choice = chooseCoopCast(input("001ebo"));
    EXPECT_EQ(CoopCastKind::Clone, choice.kind);
    EXPECT_NE(std::string::npos, choice.reason.find("001ebo"));
    EXPECT_EQ(CoopCastKind::Clone, chooseCoopCast(input("")).kind);
}

TEST(CoopCast, the_choice_is_deterministic) {
    auto in = input("manm26aa");
    in.partyNpcs = {kCarth, kBastila};
    in.partyRoles = {CoopPartyRole::Tank, CoopPartyRole::Caster};
    auto first = chooseCoopCast(in);
    for (int i = 0; i < 5; ++i) {
        auto again = chooseCoopCast(in);
        EXPECT_EQ(first.npc, again.npc);
        EXPECT_EQ(first.reason, again.reason);
    }
    EXPECT_EQ(kMission, first.npc);
}

TEST(CoopCast, every_roster_companion_and_table_entry_is_consistent) {
    EXPECT_EQ(9u, coopRoster().size());
    for (const auto &planet : coopPlanets()) {
        for (int npc : planet.companions) {
            EXPECT_TRUE(coopRosterEntry(npc)) << planet.prefix;
        }
        EXPECT_GE(planet.tier, 0);
        EXPECT_LE(planet.tier, 3);
    }
    for (const auto &line : coopGearLines()) {
        EXPECT_EQ(4u, line.tiers.size()) << line.label;
    }
    for (const auto &entry : coopRoster()) {
        EXPECT_TRUE(isCoopTemplateName(entry.templ));
    }
    EXPECT_TRUE(isCoopTemplateName(kCoopShepardTemplate));
}

// Gear tiers

TEST(CoopGear, a_template_item_below_the_planets_tier_gives_way_to_the_same_kind_of_item) {
    // HK-47's blaster rifle on Taris, Dantooine, Korriban, the Star Forge.
    EXPECT_EQ("", coopTieredItem(77, "g_w_blstrrfl001", 0));
    EXPECT_EQ("g_w_blstrrfl002", coopTieredItem(77, "g_w_blstrrfl001", 1));
    EXPECT_EQ("g_w_blstrrfl003", coopTieredItem(77, "g_w_blstrrfl001", 2));
    EXPECT_EQ("g_w_blstrrfl004", coopTieredItem(77, "G_W_BLSTRRFL001", 3));
    // Already as good, or better: kept.
    EXPECT_EQ("", coopTieredItem(77, "g_w_blstrrfl003", 2));
    EXPECT_EQ("", coopTieredItem(77, "g_w_blstrrfl004", 1));
    // Items the table does not list (Carth's own blaster, a lightsaber): kept.
    EXPECT_EQ("", coopTieredItem(12, "g_w_blstrpstl010", 3));
    EXPECT_EQ("", coopTieredItem(8, "g_w_lghtsbr01", 3));
    // A tier past the table's end takes its last item; a repeated entry is no swap.
    EXPECT_EQ("g_w_blstrrfl004", coopTieredItem(77, "g_w_blstrrfl001", 9));
    EXPECT_EQ("", coopTieredItem(19, "g_w_bowcstr003", 2));
    EXPECT_EQ("g_w_bowcstr002", coopTieredItem(19, "g_w_bowcstr003", 3));
    // The base type decides, not the name.
    EXPECT_EQ("", coopTieredItem(12, "g_w_blstrrfl001", 3));
}

// The template field and the name tag

TEST(CoopStandInTemplate, the_template_field_carries_the_template_and_refuses_junk) {
    auto resref = coopStandInResRef(6, 0, 7, "p_carth");
    EXPECT_EQ("@standin:6:0:7:p_carth", resref);
    int appearance = 0, gender = 0;
    uint32_t source = 0;
    std::string templ = "x";
    ASSERT_TRUE(parseCoopStandInResRef(resref, appearance, gender, source, &templ));
    EXPECT_EQ("p_carth", templ);
    ASSERT_TRUE(parseCoopStandInResRef(coopStandInResRef(6, 0, 7), appearance, gender, source, &templ));
    EXPECT_EQ("", templ);
    EXPECT_EQ("@standin:6:0:7", coopStandInResRef(6, 0, 7, "Bad Name"));
    for (std::string junk : {"@standin:6:0:7:", "@standin:6:0:7:P_CARTH", "@standin:6:0:7:a:b", "@standin:6:0:7:abcdefghijklmnopq"}) {
        EXPECT_FALSE(parseCoopStandInResRef(junk, appearance, gender, source, &templ)) << junk;
    }
    EXPECT_EQ("HK-47 (John)", coopStandInLabel("HK-47", "John"));
    EXPECT_EQ("John", coopStandInLabel("John", "John"));
    EXPECT_EQ("John", coopStandInLabel("", "John"));
}

// Auto-level

namespace {

std::shared_ptr<TwoDA> makeTable(const std::vector<std::string> &columns, const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

// A soldier (row 0, primaryabil STR, 2 skill points, a feat every level) and
// a Jedi Consular (row 4, primaryabil WIS, powers from classpowergain).
std::shared_ptr<TwoDA> makeClasses() {
    const std::vector<std::string> columns = {
        "name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha", "primaryabil",
        "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
    std::vector<std::vector<std::string>> rows(9, std::vector<std::string>(columns.size()));
    rows[0] = {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "STR", "sol", "save", "attack", "sol", "sol", "", "0", "0"};
    rows[4] = {"1", "2", "6", "2", "10", "16", "10", "14", "14", "12", "WIS", "jcn", "save", "attack", "jcn", "jcn", "jcn", "8", "1"};
    // No primaryabil: the ability the defaults favour most (DEX).
    rows[1] = {"1", "2", "8", "6", "12", "16", "12", "14", "12", "10", "", "sol", "save", "attack", "sol", "sol", "", "0", "0"};
    return makeTable(columns, rows);
}

class AutoLevel : public Test {
protected:
    NiceMock<MockStrings> strings;
    NiceMock<MockTwoDAs> twoDas;
    std::unique_ptr<Classes> classes;

    void SetUp() override {
        ON_CALL(twoDas, get("classes")).WillByDefault(Return(makeClasses()));
        // Skills: demolitions then awareness recommended for the soldier (both class skills),
        // computer use third (cross-class: two points a rank).
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(makeTable(
            {"label", "sol_class", "sol_reco", "jcn_class", "jcn_reco"},
            {{"computeruse", "0", "3", "1", "1"},
             {"demolitions", "1", "1", "0", ""},
             {"stealth", "0", "", "0", ""},
             {"awareness", "1", "2", "1", "2"}})));
        // Feats: the soldier recommends f2 then f0 (f0 needs f2), grants f3 at class level 3.
        ON_CALL(twoDas, get("feat")).WillByDefault(Return(makeTable(
            {"label", "sol_list", "sol_granted", "sol_recom", "jcn_list", "jcn_granted", "jcn_recom"},
            {{"f0", "0", "-1", "2", "0", "-1", ""},
             {"f1", "3", "1", "", "4", "-1", ""},
             {"f2", "1", "-1", "1", "0", "-1", "1"},
             {"f3", "3", "3", "", "4", "", ""}})));
        ON_CALL(twoDas, get("featgain")).WillByDefault(Return(makeTable(
            {"label", "sol_reg", "jcn_reg"}, {{"1", "1", "1"}, {"2", "1", "0"}, {"3", "1", "1"}, {"4", "1", "0"}, {"5", "1", "0"}})));
        ON_CALL(twoDas, get("classpowergain")).WillByDefault(Return(makeTable(
            {"label", "jcn"}, {{"1", "2"}, {"2", "1"}, {"3", "1"}, {"4", "1"}, {"5", "2"}})));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(makeTable({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(makeTable({"bab"}, {{"0"}})));
        classes = std::make_unique<Classes>(strings, twoDas);
    }
};

class FakeFeats : public IFeats {
public:
    std::set<FeatType> selectable;
    std::map<FeatType, FeatType> requires;

    void init() override {}
    std::shared_ptr<Feat> get(FeatType) const override { return nullptr; }
    int getLevelUpChoiceCount(const CreatureAttributes &attributes, const CreatureClass &clazz) const override {
        return clazz.getFeatGain(attributes.getClassLevel(clazz.type()) + 1);
    }
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

class FakeSpells : public ISpells {
public:
    std::map<SpellType, std::shared_ptr<Spell>> spells;

    std::shared_ptr<Spell> get(SpellType type) const override {
        auto it = spells.find(type);
        return it != spells.end() ? it->second : nullptr;
    }
    bool isLevelUpCandidate(SpellType, const CreatureAttributes &, const CreatureClass &, const std::set<SpellType> &) const override { return false; }
    std::vector<SpellType> getLevelUpCandidates(const CreatureAttributes &attributes, const CreatureClass &, const std::set<SpellType> &chosen) const override {
        std::vector<SpellType> result;
        for (const auto &[type, spell] : spells) {
            if (!chosen.count(type) && !attributes.hasSpell(type)) {
                result.push_back(type);
            }
        }
        return result;
    }
    std::vector<SpellDisplayEntry> getLevelUpDisplayEntries(const CreatureAttributes &, const CreatureClass &, const std::set<SpellType> &) const override { return {}; }
    void add(int id, int dark, int light) {
        auto spell = std::make_shared<Spell>();
        spell->darkRecommendation = dark;
        spell->lightRecommendation = light;
        spells[static_cast<SpellType>(id)] = spell;
    }
};

} // namespace

TEST_F(AutoLevel, the_ability_point_goes_to_the_primary_ability_else_the_favoured_one) {
    auto soldier = classes->get(ClassType::Soldier);
    auto consular = classes->get(ClassType::JediConsular);
    auto scout = classes->get(ClassType::Scout);
    ASSERT_TRUE(soldier && consular && scout);
    EXPECT_EQ(Ability::Strength, autoLevelAbility(*soldier));
    EXPECT_EQ(Ability::Wisdom, autoLevelAbility(*consular));
    EXPECT_EQ(Ability::Dexterity, autoLevelAbility(*scout));
}

TEST_F(AutoLevel, a_soldier_level_takes_the_recommended_skills_and_feats_in_order) {
    auto soldier = classes->get(ClassType::Soldier);
    ASSERT_TRUE(soldier);
    FakeFeats feats;
    feats.selectable = {static_cast<FeatType>(0), static_cast<FeatType>(2)};
    feats.requires[static_cast<FeatType>(0)] = static_cast<FeatType>(2);
    FakeSpells spells;
    CreatureAttributes attributes;
    attributes.addClassLevels(soldier.get(), 1);
    attributes.setAbilityScore(Ability::Intelligence, 12); // +1: (2 + 1) / 2 = 1 point

    auto picks = autoLevelPicks(feats, spells, *soldier, attributes, false);
    EXPECT_EQ(ClassType::Soldier, picks.clazz);
    EXPECT_EQ(-1, picks.ability) << "level 2 is no fourth level";
    EXPECT_EQ(1, picks.skillPoints);
    EXPECT_THAT(picks.skills, ElementsAre(Pair(SkillType::Demolitions, 1)));
    EXPECT_EQ(1, picks.featChoices);
    EXPECT_THAT(picks.feats, ElementsAre(static_cast<FeatType>(2)));
    EXPECT_TRUE(picks.powers.empty());

    auto after = applyAutoLevelPicks(*soldier, attributes, picks);
    EXPECT_EQ(2, after.getAggregateLevel());
    EXPECT_EQ(1, after.getSkillRank(SkillType::Demolitions));
    EXPECT_TRUE(after.hasFeat(static_cast<FeatType>(2)));

    // The next level: f0 (its prerequisite now owned), the class's level-3 grant, more skill.
    auto third = autoLevelPicks(feats, spells, *soldier, after, false);
    EXPECT_THAT(third.feats, ElementsAre(static_cast<FeatType>(0)));
    auto level3 = applyAutoLevelPicks(*soldier, after, third);
    EXPECT_EQ(3, level3.getAggregateLevel());
    EXPECT_TRUE(level3.hasFeat(static_cast<FeatType>(3))) << "feat.2da sol_granted at class level 3";
    EXPECT_EQ(2, level3.getSkillRank(SkillType::Demolitions));

    // Same input, same level.
    auto again = autoLevelPicks(feats, spells, *soldier, attributes, false);
    EXPECT_EQ(picks.skills, again.skills);
    EXPECT_EQ(picks.feats, again.feats);
}

TEST_F(AutoLevel, skills_stop_at_the_rank_cap_and_cross_class_costs_two) {
    auto soldier = classes->get(ClassType::Soldier);
    FakeFeats feats;
    FakeSpells spells;
    CreatureAttributes attributes;
    attributes.addClassLevels(soldier.get(), 1);
    attributes.setAbilityScore(Ability::Intelligence, 20); // +5: (2 + 5) / 2 = 3 points
    attributes.setSkillRank(SkillType::Demolitions, 5);    // the cap at level 2
    attributes.setSkillRank(SkillType::Awareness, 4);

    auto picks = autoLevelPicks(feats, spells, *soldier, attributes, false);
    EXPECT_EQ(3, picks.skillPoints);
    // Demolitions is full; awareness takes one to 5; computer use (cross-class, cap 2) takes one for two points.
    EXPECT_THAT(picks.skills, ElementsAre(Pair(SkillType::ComputerUse, 1), Pair(SkillType::Awareness, 1)));
    EXPECT_EQ(0, picks.skillPointsLeft);
}

TEST_F(AutoLevel, every_fourth_level_raises_the_primary_ability) {
    auto soldier = classes->get(ClassType::Soldier);
    FakeFeats feats;
    FakeSpells spells;
    CreatureAttributes attributes;
    attributes.addClassLevels(soldier.get(), 3);
    attributes.setAbilityScore(Ability::Strength, 16);
    auto picks = autoLevelPicks(feats, spells, *soldier, attributes, false);
    EXPECT_EQ(static_cast<int>(Ability::Strength), picks.ability);
    EXPECT_EQ(17, applyAutoLevelPicks(*soldier, attributes, picks).getAbilityScore(Ability::Strength));
}

TEST_F(AutoLevel, a_consular_gains_the_classpowergain_count_in_the_alignment_order) {
    auto consular = classes->get(ClassType::JediConsular);
    ASSERT_TRUE(consular);
    FakeFeats feats;
    FakeSpells spells;
    spells.add(10, 0, 1); // light first
    spells.add(9, 1, 0);  // dark first
    spells.add(23, 2, 2);
    spells.add(5, 0, 0);  // unranked: never picked
    CreatureAttributes attributes;
    attributes.addClassLevels(consular.get(), 4);
    auto light = autoLevelPicks(feats, spells, *consular, attributes, false);
    EXPECT_EQ(2, light.powerChoices) << "classpowergain jcn level 5";
    EXPECT_THAT(light.powers, ElementsAre(static_cast<SpellType>(10), static_cast<SpellType>(23)));
    auto dark = autoLevelPicks(feats, spells, *consular, attributes, true);
    EXPECT_THAT(dark.powers, ElementsAre(static_cast<SpellType>(9), static_cast<SpellType>(23)));
    EXPECT_EQ(0, light.featChoices) << "featgain jcn_reg level 5 is 0";
    auto after = applyAutoLevelPicks(*consular, attributes, light);
    EXPECT_TRUE(after.hasSpell(static_cast<SpellType>(10)));
    EXPECT_TRUE(after.hasSpell(static_cast<SpellType>(23)));
    EXPECT_EQ(5, after.getClassLevel(ClassType::JediConsular));
}
