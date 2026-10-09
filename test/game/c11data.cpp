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

// Gameplay data K1 ships that the engine now reads (campaign batch C11,
// design note c11_data).

#include <limits>
#include <map>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/resource.h"

#include "reone/game/equipmentrules.h"
#include "reone/game/experience.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/storesession.h"
#include "reone/game/upgradebench.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
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

// K1's exptable.2da: levels 1-20 and the unreachable level 21.
std::shared_ptr<TwoDA> makeK1ExpTable() {
    std::vector<std::vector<std::string>> rows;
    for (int level = 1; level <= 20; ++level) {
        rows.push_back({std::to_string(level), std::to_string((level - 1) * level * 500)});
    }
    rows.push_back({"21", "0xFFFFFFFF"});
    return makeTable({"level", "xp"}, rows);
}

// Releases the shared mock after a test that supplied tables.
void resetTwoDAs(TestEngine &engine) {
    testing::Mock::VerifyAndClearExpectations(&engine.resourceModule().twoDas());
    engine.resourceModule().twoDas().allowAnyLookup();
}

class C11Routines : public Test {
protected:
    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};
    Routines routines {GameID::KotOR, &game, &engine.services()};

    void SetUp() override {
        routines.init();
        // Every row an equippable body item, enough rows for base item 55.
        std::vector<std::vector<std::string>> rows(60, {"0x00002", "i_generic", "99"});
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable({"equipableslots", "itemclass", "stacking"}, rows)));
    }
    void TearDown() override { resetTwoDAs(engine); }

    script::Variable call(const std::string &name, const std::vector<script::Variable> &args) {
        script::Routine &routine = routines.get(routines.getIndexByName(name));
        script::ExecutionContext execution;
        execution.routines = &routines;
        return routine.invoke(args, execution);
    }

    std::shared_ptr<Item> makeItem(const std::string &tag) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", tag))
                       .field(Gff::Field::newInt("BaseItem", 0))
                       .field(Gff::Field::newWord("StackSize", 1))
                       .build();
        auto item = game.newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    }
};

} // namespace

// A1 -------------------------------------------------------------------------

TEST_F(C11Routines, get_item_possessor_names_the_holder_or_nothing) {
    auto holder = game.newCreature();
    auto held = makeItem("holocron");
    holder->addItem(held);
    auto loose = makeItem("loose");

    EXPECT_EQ(holder->id(), call("GetItemPossessor", {script::Variable::ofObject(held->id())}).objectId);
    EXPECT_EQ(script::kObjectInvalid, call("GetItemPossessor", {script::Variable::ofObject(loose->id())}).objectId);
    // Not an item at all.
    EXPECT_EQ(script::kObjectInvalid, call("GetItemPossessor", {script::Variable::ofObject(holder->id())}).objectId);
}

TEST_F(C11Routines, set_item_non_equippable_blocks_equipping_until_cleared) {
    auto creature = game.newCreature();
    auto robe = makeItem("sandpeople_robe");
    EXPECT_EQ(EquipmentCandidateReason::None, getItemUseRestriction(*creature, *robe));

    call("SetItemNonEquippable", {script::Variable::ofObject(robe->id()), script::Variable::ofInt(1)});
    EXPECT_TRUE(robe->isNonEquippable());
    EXPECT_EQ(EquipmentCandidateReason::NonEquippable, getItemUseRestriction(*creature, *robe));

    call("SetItemNonEquippable", {script::Variable::ofObject(robe->id()), script::Variable::ofInt(0)});
    EXPECT_FALSE(robe->isNonEquippable());
    EXPECT_EQ(EquipmentCandidateReason::None, getItemUseRestriction(*creature, *robe));

    // A non-item is ignored.
    call("SetItemNonEquippable", {script::Variable::ofObject(creature->id()), script::Variable::ofInt(1)});
}

TEST_F(C11Routines, live_content_is_never_available_and_module_name_needs_a_module) {
    for (int pkg = 1; pkg <= 6; ++pkg) {
        EXPECT_EQ(0, call("GetIsLiveContentAvailable", {script::Variable::ofInt(pkg)}).intValue);
    }
    EXPECT_EQ("", call("GetModuleFileName", {}).strValue);
}

TEST_F(C11Routines, item_cost_multiplier_is_per_base_item_and_replaces) {
    auto &party = game.party();
    EXPECT_FLOAT_EQ(1.0f, party.itemCostMultiplier(55));
    party.setItemCostMultiplier(55, 3.0f);
    party.setItemCostMultiplier(55, 3.0f); // not cumulative
    EXPECT_FLOAT_EQ(3.0f, party.itemCostMultiplier(55));
    EXPECT_FLOAT_EQ(1.0f, party.itemCostMultiplier(54));
    EXPECT_FLOAT_EQ(1.0f, party.itemCostMultiplier(-1));
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", "medpac"))
                   .field(Gff::Field::newInt("BaseItem", 55))
                   .field(Gff::Field::newDword("Cost", 40))
                   .field(Gff::Field::newWord("StackSize", 1))
                   .build();
    auto medpac = game.newItem();
    medpac->deserialize(*gff, SerializedIdentityContext::templateResource());
    EXPECT_EQ(40, StoreSession::itemValue(*medpac));
    EXPECT_EQ(120, StoreSession::itemValue(*medpac, party.itemCostMultiplier(55)));
    // Never below 1 credit.
    EXPECT_EQ(1, StoreSession::itemValue(*medpac, 0.0f));
}

TEST_F(C11Routines, video_effect_holds_until_disabled_and_cancel_clears_switch_back) {
    EXPECT_FALSE(game.scriptVideoEffect());
    call("EnableVideoEffect", {script::Variable::ofInt(1)});
    EXPECT_TRUE(game.scriptVideoEffect());
    call("DisableVideoEffect", {});
    EXPECT_FALSE(game.scriptVideoEffect());

    game.party().setPostDialogSwitchBack(1234);
    call("CancelPostDialogCharacterSwitch", {});
    EXPECT_EQ(0u, game.party().postDialogSwitchBack());
}

// A2: XP -----------------------------------------------------------------------

TEST(C11Experience, exptable_thresholds_match_the_old_formula_and_cap_the_level) {
    auto &engine = testEngine();
    auto &twoDas = engine.resourceModule().twoDas();
    EXPECT_CALL(twoDas, get("exptable")).Times(AnyNumber()).WillRepeatedly(Return(makeK1ExpTable()));
    // Identical to the hard-coded level * (level + 1) * 500 it replaces...
    for (int level = 1; level < 20; ++level) {
        EXPECT_EQ(level * (level + 1) * 500, experienceForNextLevel(twoDas, level)) << level;
    }
    EXPECT_EQ(0, *experienceToReachLevel(twoDas, 1));
    // ...except at the last level, which K1 marks unreachable.
    EXPECT_FALSE(experienceToReachLevel(twoDas, 21));
    EXPECT_EQ(std::numeric_limits<int>::max(), experienceForNextLevel(twoDas, 20));
    resetTwoDAs(engine);
}

TEST(C11Experience, table_size_comes_from_the_data) {
    auto &engine = testEngine();
    auto &twoDas = engine.resourceModule().twoDas();
    // A K2-shaped table: 50 levels, then the unreachable row.
    std::vector<std::vector<std::string>> rows;
    for (int level = 1; level <= 50; ++level) {
        rows.push_back({std::to_string(level), std::to_string((level - 1) * level * 500)});
    }
    rows.push_back({"51", "0xFFFFFFFF"});
    EXPECT_CALL(twoDas, get("exptable")).Times(AnyNumber()).WillRepeatedly(Return(makeTable({"level", "xp"}, rows)));
    EXPECT_EQ(20 * 21 * 500, experienceForNextLevel(twoDas, 20));
    EXPECT_EQ(49 * 50 * 500, experienceForNextLevel(twoDas, 49));
    EXPECT_EQ(std::numeric_limits<int>::max(), experienceForNextLevel(twoDas, 50));
    resetTwoDAs(engine);
}

TEST(C11Experience, kill_xp_reads_xptable_at_the_party_level_and_cr) {
    auto &engine = testEngine();
    auto &twoDas = engine.resourceModule().twoDas();
    // K1's first two xptable rows, columns c0..c3 of 21.
    EXPECT_CALL(twoDas, get("exptable")).Times(AnyNumber()).WillRepeatedly(Return(makeK1ExpTable()));
    EXPECT_CALL(twoDas, get("xptable")).Times(AnyNumber()).WillRepeatedly(Return(makeTable(
        {"level", "c0", "c1", "c2", "c3"},
        {{"1", "50", "75", "200", "300"},
         {"2", "25", "75", "150", "250"}})));
    EXPECT_EQ(1, levelForExperience(twoDas, 0));
    EXPECT_EQ(1, levelForExperience(twoDas, 999));
    EXPECT_EQ(2, levelForExperience(twoDas, 1000));
    EXPECT_EQ(20, levelForExperience(twoDas, 5000000));
    EXPECT_EQ(50, killExperience(twoDas, 1, 0.5f));  // fractional CR reads c0
    EXPECT_EQ(200, killExperience(twoDas, 1, 2.9f)); // truncated
    EXPECT_EQ(150, killExperience(twoDas, 2, 2.0f));
    EXPECT_EQ(150, killExperience(twoDas, 20, 2.0f)); // row clamps to the table
    EXPECT_EQ(0, killExperience(twoDas, 1, 9.0f));    // no such column
    // A K2-style GAME_XP_General row scales the award, rounding up.
    EXPECT_CALL(twoDas, get("npc")).Times(AnyNumber()).WillRepeatedly(Return(makeTable(
        {"label", "percentxp"}, {{"Carth", "80"}, {"GAME_XP_General", "90"}})));
    EXPECT_EQ(68, killExperience(twoDas, 1, 1.0f)); // 75 * 0.9 = 67.5
    resetTwoDAs(engine);
}

TEST(C11Experience, without_a_table_the_k1_values_apply) {
    auto &engine = testEngine();
    auto &twoDas = engine.resourceModule().twoDas();
    EXPECT_EQ(1000, experienceForNextLevel(twoDas, 1));
    EXPECT_EQ(210000, experienceForNextLevel(twoDas, 20));
}

// A2: upcrystals ---------------------------------------------------------------

TEST(C11Crystals, upcrystals_names_the_blade_per_saber_kind) {
    // K1's rows 0 (blue) and 5 (Heart of the Guardian); the Heart's blades
    // differ between saber kinds, which the old hard-coded list could not say.
    auto upcrystals = makeTable(
        {"label", "template", "shortmdlvar", "longmdlvar", "doublemdlvar"},
        {{"BLUE", "G_W_SBRCRSTL14", "g_w_shortsbr01", "g_w_lghtsbr01", "G_W_DBLSBR001"},
         {"Heart", "G1_W_SBRCRSTL20", "g1_w_shortsbr01", "g1_w_lghtsbr01", "G1_W_DBLSBR001"}});
    std::map<std::string, int> variations {
        {"g_w_shortsbr01", 1}, {"g_w_lghtsbr01", 1}, {"g_w_dblsbr001", 1},
        {"g1_w_shortsbr01", 6}, {"g1_w_lghtsbr01", 7}, {"g1_w_dblsbr001", 6}};
    ColorCrystalTable table(*upcrystals, [&](const std::string &resRef) {
        auto it = variations.find(resRef);
        return it == variations.end() ? -1 : it->second;
    });
    EXPECT_EQ(1, table.modelVariation("g_w_sbrcrstl14", 8));
    EXPECT_EQ(1, table.modelVariation("G_W_SBRCRSTL14", 10));
    EXPECT_EQ(7, table.modelVariation("g1_w_sbrcrstl20", 8));
    EXPECT_EQ(6, table.modelVariation("g1_w_sbrcrstl20", 9));
    EXPECT_EQ(-1, table.modelVariation("g_w_sbrcrstl18", 8));
    EXPECT_EQ("g1_w_sbrcrstl20", table.crystalFor(7, 8));
    EXPECT_EQ("g1_w_sbrcrstl20", table.crystalFor(6, 10));
    EXPECT_EQ("g_w_sbrcrstl14", table.crystalFor(1, 9));
    EXPECT_EQ("", table.crystalFor(-1, 8));
}

TEST(C11Crystals, without_the_table_the_k1_colours_apply) {
    ColorCrystalTable table;
    // Identical to the list the table replaces.
    EXPECT_EQ(colorCrystalModelVariation("g_w_sbrcrstl16"), table.modelVariation("g_w_sbrcrstl16", 8));
    EXPECT_EQ(3, table.modelVariation("g_w_sbrcrstl16", 10));
    EXPECT_EQ("g_w_sbrcrstl18", table.crystalFor(2, 8));
}
