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
 * The K2 systems merge, data layer (design note k2_merge): K1 rows keep
 * their indices, K2 rows are appended with an explicit map, strrefs of
 * appended rows resolve through K2's talk table and their scripts through
 * K2's script set.
 */

#include <gtest/gtest.h>

#include <map>

#include "reone/resource/k2merge.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/replacements.h"
#include "reone/resource/strings.h"

using namespace reone;
using namespace reone::resource;

namespace {

std::shared_ptr<TwoDA> table(std::vector<std::string> columns,
                             std::vector<std::vector<std::string>> rows) {
    TwoDA::Builder builder;
    builder.columns(std::move(columns));
    for (auto &row : rows) {
        builder.row(std::move(row));
    }
    return builder.build();
}

class FakeTwoDAs : public ITwoDAs {
public:
    void clear() override {}
    std::shared_ptr<TwoDA> get(const std::string &key) override {
        auto it = tables.find(key);
        return it != tables.end() ? it->second : nullptr;
    }
    std::map<std::string, std::shared_ptr<TwoDA>> tables;
};

TwoDAMergeRule rule(TwoDAMergeKind kind,
                    std::vector<std::string> strRefs = {},
                    std::vector<std::string> scripts = {}) {
    TwoDAMergeRule result;
    result.table = "test";
    result.kind = kind;
    result.strRefColumns = std::move(strRefs);
    result.scriptColumns = std::move(scripts);
    return result;
}

} // namespace

TEST(K2Merge, index_aligned_keeps_k1_rows_and_appends_k2_rows_at_their_own_index) {
    auto k1 = table({"label", "name", "impactscript"},
                    {{"POWER_A", "10", "k_sp1_generic"}, {"POWER_B", "11", "k_sp1_generic"}});
    auto k2 = table({"label", "name", "impactscript", "weapmstr"},
                    {{"POWER_A", "900", "k_sp1_generic", "3"},
                     {"POWER_B", "901", "k_sp1_generic", "4"},
                     {"POWER_C", "902", "k_sp1_generic", "5"}});

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, rule(TwoDAMergeKind::IndexAligned, {"name"}, {"impactscript"}), map);

    ASSERT_EQ(3, merged->getRowCount());
    // K1's own cells win on shared rows: K1 strrefs, K1 scripts.
    EXPECT_EQ(10, merged->getInt(0, "name"));
    EXPECT_EQ("k_sp1_generic", merged->getString(1, "impactscript"));
    // Columns only K2 has are filled from K2's same row.
    EXPECT_EQ(3, merged->getInt(0, "weapmstr"));
    EXPECT_EQ(4, merged->getInt(1, "weapmstr"));
    // The appended row: strref flagged, script routed to K2.
    EXPECT_EQ(toK2StrRef(902), merged->getInt(2, "name"));
    EXPECT_EQ("k2:k_sp1_generic", merged->getString(2, "impactscript"));
    EXPECT_EQ(5, merged->getInt(2, "weapmstr"));

    EXPECT_EQ(2, map.k1Rows);
    EXPECT_EQ(3, map.k2Rows);
    EXPECT_EQ(1, map.appendedRows);
    EXPECT_EQ(0, map.labelMismatches);
    EXPECT_EQ(std::vector<int>({0, 1, 2}), map.k2ToMerged);
}

TEST(K2Merge, index_aligned_counts_shared_rows_whose_labels_differ) {
    auto k1 = table({"label"}, {{"IMPLANT_LEVEL_1"}, {"TOUGHNESS"}});
    auto k2 = table({"label"}, {{"XXXX_IMPLANT_LEVEL_1"}, {"TOUGHNESS"}, {"EVASION"}});

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, rule(TwoDAMergeKind::IndexAligned), map);

    EXPECT_EQ(1, map.labelMismatches);
    // The K1 row is kept as K1 has it.
    EXPECT_EQ("IMPLANT_LEVEL_1", merged->getString(0, "label"));
    EXPECT_EQ("EVASION", merged->getString(2, "label"));
}

TEST(K2Merge, k1_only_columns_are_blank_on_appended_rows) {
    auto k1 = table({"label", "effcrlvl01"}, {{"Soldier", "1"}});
    auto k2 = table({"label"}, {{"Soldier"}, {"JediMaster"}});

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, rule(TwoDAMergeKind::IndexAligned), map);

    EXPECT_EQ(1, merged->getInt(0, "effcrlvl01"));
    EXPECT_FALSE(merged->getIntOpt(1, "effcrlvl01").has_value());
}

TEST(K2Merge, label_matched_maps_shared_labels_and_appends_the_rest) {
    auto k1 = table({"label", "imp_impact_node"},
                    {{"vfx_imp_heal", "k1heal"}, {"vfx_dur_stun", "k1stun"}});
    auto k2 = table({"label", "imp_impact_node"},
                    {{"vfx_imp_heal", "k2heal"}, {"vfx_imp_plasma", "k2plasma"}, {"vfx_dur_stun", "k2stun"}});

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, rule(TwoDAMergeKind::LabelMatched), map);

    ASSERT_EQ(3, merged->getRowCount());
    EXPECT_EQ("k1heal", merged->getString(0, "imp_impact_node"));
    EXPECT_EQ("k1stun", merged->getString(1, "imp_impact_node"));
    EXPECT_EQ("k2plasma", merged->getString(2, "imp_impact_node"));
    // K2 row 1 (inserted by K2) lands after K1's rows; K2 row 2 is K1's row 1.
    EXPECT_EQ(std::vector<int>({0, 2, 1}), map.k2ToMerged);
    EXPECT_EQ(1, map.appendedRows);
}

// visualeffects: scripts pass the row label. K2-only rows keep their K2 label
// (Force Body is 9011 in both the K2 script and the merged table); a K2 label
// that a different K1 row already holds moves to a free label, and the K2 id
// is remapped to it.
TEST(K2Merge, label_matched_by_row_label_keeps_k2_labels_and_remaps_collisions) {
    auto build = [](std::vector<std::pair<std::string, std::vector<std::string>>> rows) {
        TwoDA::Builder builder;
        builder.columns({"label", "imp_impact_node"});
        for (auto &[label, values] : rows) {
            builder.row(label, values);
        }
        return std::shared_ptr<TwoDA>(builder.build());
    };
    auto k1 = build({{"1001", {"VFX_IMP_HEALING_SMALL", "k1heal"}},
                     {"1049", {"VFX_K1_ONLY", "k1only"}},
                     {"8002", {"VFX_DUR_STEALTH_FIELD", "k1stealth"}}});
    auto k2 = build({{"1001", {"VFX_IMP_HEALING_SMALL", "k2heal"}},
                     {"1049", {"VFX_IMP_POSION", "k2poison"}},        // collides with K1's 1049
                     {"8002", {"VFX_DUR_STEALTH_FIELD", "k2stealth"}},
                     {"9011", {"VFX_IMP_FORCE_BODY_I", "k2body"}}});
    auto byLabel = rule(TwoDAMergeKind::LabelMatched);
    byLabel.idsAreRowLabels = true;

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, byLabel, map);

    ASSERT_EQ(5, merged->getRowCount());
    std::map<std::string, std::string> nodeByLabel;
    for (int row = 0; row < merged->getRowCount(); ++row) {
        nodeByLabel[merged->rows()[row].label] = merged->getString(row, "imp_impact_node");
    }
    EXPECT_EQ("k1heal", nodeByLabel["1001"]);
    EXPECT_EQ("k1only", nodeByLabel["1049"]);
    EXPECT_EQ("k1stealth", nodeByLabel["8002"]);
    EXPECT_EQ("k2body", nodeByLabel["9011"]);   // kept its K2 label
    EXPECT_EQ("k2poison", nodeByLabel["9012"]); // next label free after both tables
    EXPECT_EQ(2, map.appendedRows);
    EXPECT_EQ(1, map.relabelledRows);

    EXPECT_EQ(1001, map.mapK2Label(1001));
    EXPECT_EQ(8002, map.mapK2Label(8002));
    EXPECT_EQ(9011, map.mapK2Label(9011));
    EXPECT_EQ(9012, map.mapK2Label(1049));
    EXPECT_EQ(-1, map.mapK2Label(4242));
}

// A K2 row matched to a K1 row by name under a different label maps to K1's.
TEST(K2Merge, label_matched_by_row_label_maps_shared_rows_to_k1_labels) {
    TwoDA::Builder a;
    a.columns({"label"});
    a.row("2000", {"VFX_SHARED"});
    TwoDA::Builder b;
    b.columns({"label"});
    b.row("2005", {"VFX_SHARED"});
    auto k1 = std::shared_ptr<TwoDA>(a.build());
    auto k2 = std::shared_ptr<TwoDA>(b.build());
    auto byLabel = rule(TwoDAMergeKind::LabelMatched);
    byLabel.idsAreRowLabels = true;

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, byLabel, map);

    ASSERT_EQ(1, merged->getRowCount());
    EXPECT_EQ(2000, map.mapK2Label(2005));
    EXPECT_EQ(0, map.appendedRows);
}

TEST(K2Merge, merged_set_maps_k2_ids_by_label_for_visualeffects_and_by_index_otherwise) {
    FakeTwoDAs k1;
    FakeTwoDAs k2;
    TwoDA::Builder k1Vfx;
    k1Vfx.columns({"label"});
    k1Vfx.row("1001", {"VFX_IMP_HEALING_SMALL"});
    TwoDA::Builder k2Vfx;
    k2Vfx.columns({"label"});
    k2Vfx.row("1001", {"VFX_IMP_HEALING_SMALL"});
    k2Vfx.row("9005", {"VFX_IMP_FORCE_SCREAM_I"});
    k1.tables["visualeffects"] = std::shared_ptr<TwoDA>(k1Vfx.build());
    k2.tables["visualeffects"] = std::shared_ptr<TwoDA>(k2Vfx.build());
    k1.tables["effecticon"] = table({"label"}, {{"a"}, {"c"}});
    k2.tables["effecticon"] = table({"label"}, {{"a"}, {"b"}, {"c"}});
    K2MergedTwoDAs merged(k1, k2);

    EXPECT_EQ(9005, merged.mapK2Id("visualeffects", 9005));
    EXPECT_EQ(1001, merged.mapK2Id("visualeffects", 1001));
    EXPECT_EQ("9005", merged.get("visualeffects")->rows()[1].label);
    // effecticon is read by row index: K2's row 1 was appended at 2.
    EXPECT_EQ(2, merged.mapK2Id("effecticon", 1));
    EXPECT_EQ(1, merged.mapK2Id("effecticon", 2));
    // A table with no merge: the id as it is.
    EXPECT_EQ(7, merged.mapK2Id("appearance", 7));
}

TEST(K2Merge, k2_wins_serves_k2_table_whole) {
    auto k1 = table({"level", "xp"}, {{"1", "0"}, {"2", "1000"}, {"3", "0xFFFFFFFF"}});
    auto k2 = table({"level", "xp"}, {{"1", "0"}, {"2", "1000"}, {"3", "3000"}, {"4", "6000"}});

    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, rule(TwoDAMergeKind::K2Wins), map);

    ASSERT_EQ(4, merged->getRowCount());
    EXPECT_EQ(3000, merged->getInt(2, "xp"));
    EXPECT_EQ(3, map.k1Rows);
    EXPECT_EQ(1, map.appendedRows);
}

TEST(K2Merge, merged_set_serves_k1_tables_without_a_rule_untouched) {
    FakeTwoDAs k1;
    FakeTwoDAs k2;
    k1.tables["appearance"] = table({"label"}, {{"k1"}});
    k2.tables["appearance"] = table({"label"}, {{"k2"}, {"k2b"}});
    k1.tables["feat"] = table({"label"}, {{"A"}});
    k2.tables["feat"] = table({"label"}, {{"A"}, {"B"}});
    K2MergedTwoDAs merged(k1, k2);

    EXPECT_EQ(k1.tables["appearance"], merged.get("appearance"));
    // K1's row, K2's appended row, then our own Grey row.
    EXPECT_EQ(3, merged.get("feat")->getRowCount());
    EXPECT_EQ(3, merged.get("FEAT")->getRowCount());
    EXPECT_EQ(2, merged.ownRowIndex("feat", "K2M_GREY"));
    ASSERT_NE(nullptr, merged.mergeMap("feat"));
    EXPECT_EQ(nullptr, merged.mergeMap("appearance"));
    EXPECT_EQ(1, merged.mapK2Row("feat", 1));
    EXPECT_EQ(7, merged.mapK2Row("appearance", 7));
}

TEST(K2Merge, merged_set_serves_k2_only_tables_and_nothing_for_absent_ones) {
    FakeTwoDAs k1;
    FakeTwoDAs k2;
    k2.tables["cls_st_jmaster"] = table({"level", "fortsave"}, {{"1", "2"}});
    K2MergedTwoDAs merged(k1, k2);

    ASSERT_NE(nullptr, merged.get("cls_st_jmaster"));
    EXPECT_EQ(2, merged.get("cls_st_jmaster")->getInt(0, "fortsave"));
    EXPECT_EQ(nullptr, merged.get("cls_st_nothing"));
}

TEST(K2Merge, flagged_strrefs_resolve_through_k2_talk_table_only) {
    Strings strings;
    strings.setTalkTable(0, std::make_shared<TalkTable>(std::vector<TalkTable::String> {{"k1 zero", ""}, {"k1 one", ""}}));

    // Without K2's table, a flagged strref resolves to nothing, never to K1.
    EXPECT_EQ("", strings.getText(toK2StrRef(1)));
    EXPECT_EQ("k1 one", strings.getText(1));

    strings.setK2TalkTable(std::make_shared<TalkTable>(std::vector<TalkTable::String> {{"k2 zero", ""}, {"k2 one", ""}}));
    EXPECT_EQ("k2 one", strings.getText(toK2StrRef(1)));
    EXPECT_EQ("k1 one", strings.getText(1));
    EXPECT_EQ("", strings.getText(toK2StrRef(5)));
}

TEST(K2Merge, script_names_with_the_k2_prefix_never_read_k1_scripts) {
    Resources k1;
    ResourceReplacements replacements;
    Scripts scripts(k1, replacements);

    // No K2 set: the prefixed name resolves to nothing.
    EXPECT_EQ(nullptr, scripts.get("k2:k_sp1_generic"));
    EXPECT_TRUE(isK2ScriptName("k2:k_sp1_generic"));
    EXPECT_FALSE(isK2ScriptName("k_sp1_generic"));
}
