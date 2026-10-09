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

#include "reone/resource/k2merge.h"

#include <optional>

#include "reone/resource/container/erf.h"
#include "reone/resource/container/folder.h"
#include "reone/resource/container/keybif.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/presentationresources.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"

namespace reone {

namespace resource {

static constexpr char kDeletedCell[] = "****";

static std::string lowerAscii(std::string value) {
    for (auto &ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

const std::vector<TwoDAMergeRule> &k2MergeRules() {
    using K = TwoDAMergeKind;
    static const std::vector<TwoDAMergeRule> kRules {
        // Feats, Force powers and classes: K2 extends K1 row for row.
        {"feat", K::IndexAligned, {"name", "description"}, {}},
        {"spells", K::IndexAligned, {"name", "spelldesc"}, {"impactscript"}},
        {"classes", K::IndexAligned, {"name", "plural", "lower", "description"}, {}},
        // Per-level tables: K1 stops at 20 rows, K2 goes to 50.
        {"featgain", K::IndexAligned, {}, {}},
        {"classpowergain", K::IndexAligned, {}, {}},
        {"cls_spgn_jedi", K::IndexAligned, {}, {}},
        {"cls_atk_1", K::IndexAligned, {}, {}},
        {"cls_atk_2", K::IndexAligned, {}, {}},
        {"cls_atk_3", K::IndexAligned, {}, {}},
        {"cls_st_cm_drd", K::IndexAligned, {}, {}},
        {"cls_st_ex_drd", K::IndexAligned, {}, {}},
        {"cls_st_jedi_c", K::IndexAligned, {}, {}},
        {"cls_st_jedi_g", K::IndexAligned, {}, {}},
        {"cls_st_jedi_s", K::IndexAligned, {}, {}},
        {"cls_st_minion", K::IndexAligned, {}, {}},
        {"cls_st_scndrl", K::IndexAligned, {}, {}},
        {"cls_st_scout", K::IndexAligned, {}, {}},
        {"cls_st_soldier", K::IndexAligned, {}, {}},
        {"acbonus", K::IndexAligned, {}, {}},
        // Same rows; K2 adds the prestige classes' columns.
        {"skills", K::IndexAligned, {"name", "description"}, {}},
        // The 51-row table (level 50). Its first 20 rows equal K1's; K1's
        // 21st is the "cannot reach level 21" cap.
        {"exptable", K::K2Wins, {}, {}},
        // Prestige classes' saving throws.
        {"cls_st_techspec", K::K2Only, {}, {}},
        {"cls_st_jweapmas", K::K2Only, {}, {}},
        {"cls_st_jmaster", K::K2Only, {}, {}},
        {"cls_st_jwatch", K::K2Only, {}, {}},
        {"cls_st_sithmar", K::K2Only, {}, {}},
        {"cls_st_sithlord", K::K2Only, {}, {}},
        {"cls_st_sithass", K::K2Only, {}, {}},
        // K2 inserted rows among K1's: matched by label.
        // Scripts pass the row label (VFX_IMP_FORCE_BODY_I is 9011), so K2's
        // appended rows keep their labels.
        {"visualeffects", K::LabelMatched, {}, {}, true},
        {"effecticon", K::LabelMatched, {"description", "namestrref"}, {}},
        // Lightsaber crystals (upgrade bench).
        {"upcrystals", K::IndexAligned, {}, {}},
        // K2 crafting and breakdown (upgrade bench).
        {"itemcreate", K::K2Only, {"name"}, {}},
        {"chemicalcreate", K::K2Only, {"name"}, {}}};
    return kRules;
}

const std::vector<std::string> &k2MergeOwnStrings() {
    static const std::vector<std::string> kStrings {
        "Grey",
        "Prerequisites: Prestige class, taken while neutral\n\n"
        "Revan walked the path of the light and the path of the dark, and came "
        "back to stand between them. A character who takes a prestige class "
        "while holding the balance of the Force gains +1 to all saving throws "
        "for as long as they stay neutral."};
    return kStrings;
}

const std::vector<OwnTwoDARow> &k2MergeOwnRows() {
    static const std::vector<OwnTwoDARow> kRows {
        {"feat",
         "K2M_GREY",
         {{"label", "K2M_GREY"},
          {"name", std::to_string(kOwnStrRefFlag | 0)},
          {"description", std::to_string(kOwnStrRefFlag | 1)},
          {"icon", "I_empathy01"},
          {"allclassescanuse", "0"},
          {"constant", "FEAT_K2M_GREY"},
          {"pips", "1"}}}};
    return kRows;
}

const TwoDAMergeRule *findK2MergeRule(const std::string &table) {
    auto key = lowerAscii(table);
    for (const auto &rule : k2MergeRules()) {
        if (rule.table == key) {
            return &rule;
        }
    }
    return nullptr;
}

static int findColumn(const std::vector<std::string> &columns, const std::string &name) {
    auto key = lowerAscii(name);
    for (size_t i = 0; i < columns.size(); ++i) {
        if (lowerAscii(columns[i]) == key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// A row label that is a plain non-negative decimal number.
static std::optional<int> parseNumericLabel(const std::string &label) {
    if (label.empty() || label.size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    for (char ch : label) {
        if (ch < '0' || ch > '9') {
            return std::nullopt;
        }
        value = value * 10 + (ch - '0');
    }
    return value;
}

static bool isBlankCell(const std::string &value) {
    return value.empty() || value == kDeletedCell;
}

static std::string flagStrRef(const std::string &value) {
    if (isBlankCell(value)) {
        return value;
    }
    try {
        size_t used = 0;
        int strRef = std::stoi(value, &used);
        if (used != value.size() || strRef < 0) {
            return value;
        }
        return std::to_string(toK2StrRef(strRef));
    } catch (const std::exception &) {
        return value;
    }
}

static std::string prefixScript(const std::string &value) {
    if (isBlankCell(value) || isK2ScriptName(value)) {
        return value;
    }
    return std::string(kK2ScriptPrefix) + lowerAscii(value);
}

static bool contains(const std::vector<std::string> &names, const std::string &name) {
    auto key = lowerAscii(name);
    return std::any_of(names.begin(), names.end(), [&key](const auto &value) {
        return lowerAscii(value) == key;
    });
}

// A K2 row rewritten for the merged table: strrefs flagged, scripts
// prefixed, and laid out in the merged column order.
static std::vector<std::string> convertK2Row(
    const TwoDA &k2,
    int k2Row,
    const std::vector<std::string> &mergedColumns,
    const TwoDAMergeRule &rule) {

    std::vector<std::string> values(mergedColumns.size());
    const auto &k2Columns = k2.columns();
    const auto &k2Values = k2.rows()[k2Row].values;
    for (size_t column = 0; column < mergedColumns.size(); ++column) {
        int k2Column = findColumn(k2Columns, mergedColumns[column]);
        if (k2Column < 0) {
            values[column] = kDeletedCell;
            continue;
        }
        std::string value = k2Values[k2Column];
        if (contains(rule.strRefColumns, mergedColumns[column])) {
            value = flagStrRef(value);
        } else if (contains(rule.scriptColumns, mergedColumns[column])) {
            value = prefixScript(value);
        }
        values[column] = std::move(value);
    }
    return values;
}

// A K1 row laid out in the merged column order: K1's own cells, then K2's
// cells of the same K2 row for the columns only K2 has.
static std::vector<std::string> convertK1Row(
    const TwoDA &k1,
    int k1Row,
    const TwoDA &k2,
    int k2Row,
    const std::vector<std::string> &mergedColumns,
    const TwoDAMergeRule &rule) {

    std::vector<std::string> values = k1.rows()[k1Row].values;
    values.resize(mergedColumns.size(), kDeletedCell);
    size_t k1ColumnCount = k1.columns().size();
    if (k2Row < 0) {
        return values;
    }
    const auto &k2Values = k2.rows()[k2Row].values;
    for (size_t column = k1ColumnCount; column < mergedColumns.size(); ++column) {
        int k2Column = findColumn(k2.columns(), mergedColumns[column]);
        if (k2Column < 0) {
            continue;
        }
        std::string value = k2Values[k2Column];
        if (contains(rule.strRefColumns, mergedColumns[column])) {
            value = flagStrRef(value);
        }
        values[column] = std::move(value);
    }
    return values;
}

std::shared_ptr<TwoDA> mergeTwoDA(
    const TwoDA *k1,
    const TwoDA &k2,
    const TwoDAMergeRule &rule,
    TwoDAMergeMap &map) {

    map = TwoDAMergeMap();
    map.k2Rows = k2.getRowCount();

    if (!k1 || rule.kind == TwoDAMergeKind::K2Only || rule.kind == TwoDAMergeKind::K2Wins) {
        std::vector<TwoDA::Row> rows;
        rows.reserve(k2.rows().size());
        for (int row = 0; row < k2.getRowCount(); ++row) {
            rows.push_back(TwoDA::newRow(k2.rows()[row].label, convertK2Row(k2, row, k2.columns(), rule)));
            map.k2ToMerged.push_back(row);
        }
        map.k1Rows = k1 ? k1->getRowCount() : 0;
        map.mergedRows = static_cast<int>(rows.size());
        map.appendedRows = std::max(0, map.mergedRows - map.k1Rows);
        return std::make_shared<TwoDA>(k2.columns(), std::move(rows));
    }

    map.k1Rows = k1->getRowCount();

    std::vector<std::string> columns = k1->columns();
    for (const auto &column : k2.columns()) {
        if (findColumn(columns, column) < 0) {
            columns.push_back(column);
        }
    }

    std::vector<TwoDA::Row> rows;
    rows.reserve(std::max(k1->rows().size(), k2.rows().size()));
    map.k2ToMerged.assign(k2.rows().size(), -1);

    if (rule.kind == TwoDAMergeKind::IndexAligned) {
        for (int row = 0; row < k1->getRowCount(); ++row) {
            int k2Row = row < k2.getRowCount() ? row : -1;
            rows.push_back(TwoDA::newRow(k1->rows()[row].label, convertK1Row(*k1, row, k2, k2Row, columns, rule)));
            if (k2Row >= 0) {
                map.k2ToMerged[k2Row] = row;
                const auto &k1Label = k1->rows()[row].label;
                const auto &k2Label = k2.rows()[k2Row].label;
                int k1LabelColumn = findColumn(k1->columns(), "label");
                int k2LabelColumn = findColumn(k2.columns(), "label");
                std::string left = k1LabelColumn >= 0 ? k1->rows()[row].values[k1LabelColumn] : k1Label;
                std::string right = k2LabelColumn >= 0 ? k2.rows()[k2Row].values[k2LabelColumn] : k2Label;
                if (lowerAscii(left) != lowerAscii(right)) {
                    ++map.labelMismatches;
                }
            }
        }
        for (int row = k1->getRowCount(); row < k2.getRowCount(); ++row) {
            rows.push_back(TwoDA::newRow(k2.rows()[row].label, convertK2Row(k2, row, columns, rule)));
            map.k2ToMerged[row] = static_cast<int>(rows.size()) - 1;
            ++map.appendedRows;
        }
    } else {
        // LabelMatched: the "label" column when there is one, else the row
        // label.
        int k1LabelColumn = findColumn(k1->columns(), "label");
        int k2LabelColumn = findColumn(k2.columns(), "label");
        auto labelOf = [](const TwoDA &table, int column, int row) {
            return lowerAscii(column >= 0 ? table.rows()[row].values[column] : table.rows()[row].label);
        };
        std::map<std::string, int> k1ByLabel;
        for (int row = 0; row < k1->getRowCount(); ++row) {
            k1ByLabel.emplace(labelOf(*k1, k1LabelColumn, row), row);
        }
        std::vector<int> k2ForK1(k1->rows().size(), -1);
        for (int row = 0; row < k2.getRowCount(); ++row) {
            auto label = labelOf(k2, k2LabelColumn, row);
            auto it = isBlankCell(label) ? k1ByLabel.end() : k1ByLabel.find(label);
            if (it != k1ByLabel.end() && k2ForK1[it->second] < 0) {
                k2ForK1[it->second] = row;
                map.k2ToMerged[row] = it->second;
            }
        }
        for (int row = 0; row < k1->getRowCount(); ++row) {
            rows.push_back(TwoDA::newRow(k1->rows()[row].label, convertK1Row(*k1, row, k2, k2ForK1[row], columns, rule)));
        }
        // Row labels in use; with idsAreRowLabels an appended K2 row keeps
        // its own label unless it is taken, and then gets the next free one.
        std::set<int> usedLabels;
        int nextFree = 0;
        if (rule.idsAreRowLabels) {
            for (int row = 0; row < k1->getRowCount(); ++row) {
                if (auto label = parseNumericLabel(k1->rows()[row].label)) {
                    usedLabels.insert(*label);
                }
            }
            for (int row = 0; row < k2.getRowCount(); ++row) {
                if (auto label = parseNumericLabel(k2.rows()[row].label)) {
                    nextFree = std::max(nextFree, *label + 1);
                }
            }
            if (!usedLabels.empty()) {
                nextFree = std::max(nextFree, *usedLabels.rbegin() + 1);
            }
            // Shared rows: a K2 id means K1's row.
            for (int row = 0; row < k2.getRowCount(); ++row) {
                auto k2Label = parseNumericLabel(k2.rows()[row].label);
                int merged = map.k2ToMerged[row];
                if (!k2Label || merged < 0) {
                    continue;
                }
                if (auto k1Label = parseNumericLabel(k1->rows()[merged].label)) {
                    map.k2LabelToMerged[*k2Label] = *k1Label;
                }
            }
        }
        for (int row = 0; row < k2.getRowCount(); ++row) {
            if (map.k2ToMerged[row] >= 0) {
                continue;
            }
            std::string label = std::to_string(rows.size());
            if (rule.idsAreRowLabels) {
                auto k2Label = parseNumericLabel(k2.rows()[row].label);
                int merged = k2Label && usedLabels.count(*k2Label) == 0 ? *k2Label : nextFree++;
                if (!k2Label || merged != *k2Label) {
                    ++map.relabelledRows;
                }
                usedLabels.insert(merged);
                if (k2Label) {
                    map.k2LabelToMerged[*k2Label] = merged;
                }
                label = std::to_string(merged);
            }
            rows.push_back(TwoDA::newRow(std::move(label), convertK2Row(k2, row, columns, rule)));
            map.k2ToMerged[row] = static_cast<int>(rows.size()) - 1;
            ++map.appendedRows;
        }
    }

    map.mergedRows = static_cast<int>(rows.size());
    return std::make_shared<TwoDA>(std::move(columns), std::move(rows));
}

void K2MergedTwoDAs::clear() {
    _merged.clear();
    _maps.clear();
    _ownRows.clear();
    _missing.clear();
    _k1.clear();
    _k2.clear();
}

void K2MergedTwoDAs::ensureMerged(const std::string &table) {
    if (_merged.count(table) != 0 || _missing.count(table) != 0) {
        return;
    }
    const auto *rule = findK2MergeRule(table);
    if (!rule) {
        _missing.insert(table);
        return;
    }
    auto k2 = _k2.get(table);
    auto k1 = rule->kind == TwoDAMergeKind::K2Only ? std::shared_ptr<TwoDA>() : _k1.get(table);
    if (!k2) {
        // Nothing to merge in: K1's table (if any) is served as is.
        _missing.insert(table);
        return;
    }
    if (!k1 && rule->kind != TwoDAMergeKind::K2Only) {
        _missing.insert(table);
        return;
    }
    TwoDAMergeMap map;
    auto merged = mergeTwoDA(k1.get(), *k2, *rule, map);
    if (map.labelMismatches > 0) {
        debug(str(boost::format("K2 merge: %s: %d shared rows have differing labels") % table % map.labelMismatches));
    }
    // Our own rows go after everything else.
    std::vector<TwoDA::Row> rows = merged->rows();
    for (const auto &own : k2MergeOwnRows()) {
        if (own.table != table) {
            continue;
        }
        std::vector<std::string> values(merged->columns().size(), kDeletedCell);
        for (const auto &[column, value] : own.cells) {
            int index = findColumn(merged->columns(), column);
            if (index >= 0) {
                values[index] = value;
            }
        }
        _ownRows[table][own.label] = static_cast<int>(rows.size());
        rows.push_back(TwoDA::newRow(std::to_string(rows.size()), std::move(values)));
    }
    if (static_cast<int>(rows.size()) != merged->getRowCount()) {
        merged = std::make_shared<TwoDA>(merged->columns(), std::move(rows));
    }
    _merged.emplace(table, std::move(merged));
    _maps.emplace(table, std::move(map));
}

int K2MergedTwoDAs::ownRowIndex(const std::string &table, const std::string &label) {
    auto key = lowerAscii(table);
    ensureMerged(key);
    auto it = _ownRows.find(key);
    if (it == _ownRows.end()) {
        return -1;
    }
    auto row = it->second.find(label);
    return row != it->second.end() ? row->second : -1;
}

std::shared_ptr<TwoDA> K2MergedTwoDAs::get(const std::string &key) {
    auto table = lowerAscii(key);
    ensureMerged(table);
    auto it = _merged.find(table);
    if (it != _merged.end()) {
        return it->second;
    }
    return _k1.get(key);
}

const TwoDAMergeMap *K2MergedTwoDAs::mergeMap(const std::string &table) {
    auto key = lowerAscii(table);
    ensureMerged(key);
    auto it = _maps.find(key);
    return it != _maps.end() ? &it->second : nullptr;
}

int K2MergedTwoDAs::mapK2Row(const std::string &table, int k2Row) {
    const auto *map = mergeMap(table);
    return map ? map->mapK2Row(k2Row) : k2Row;
}

int K2MergedTwoDAs::mapK2Id(const std::string &table, int k2Id) {
    const auto *map = mergeMap(table);
    if (!map) {
        return k2Id;
    }
    const auto *rule = findK2MergeRule(table);
    return rule && rule->idsAreRowLabels ? map->mapK2Label(k2Id) : map->mapK2Row(k2Id);
}

int K2MergedTwoDAs::k1RowCount(const std::string &table) {
    auto k1 = _k1.get(table);
    return k1 ? k1->getRowCount() : -1;
}

const std::set<ResType> &k2FallbackResourceTypes() {
    static const std::set<ResType> kTypes {
        ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds,
        ResType::Mdl, ResType::Mdx,
        ResType::Wav};
    return kTypes;
}

void mountK2DataSources(Resources &set, const std::filesystem::path &k2Path) {
    if (!isK2Install(k2Path)) {
        throw std::runtime_error(
            "Not a K2 install (chitin.key and swkotor2.exe expected): " + k2Path.string());
    }
    static const std::set<ResType> kKeyTypes {
        ResType::TwoDA, ResType::Ncs,
        ResType::Mdl, ResType::Mdx,
        ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds,
        ResType::Wav,
        ResType::Uti};
    static const std::set<ResType> kTextureTypes {ResType::Tga, ResType::Tpc, ResType::Txi, ResType::Dds};
    static const std::set<ResType> kSoundTypes {ResType::Wav};

    auto keyPath = findFileIgnoreCase(k2Path, "chitin.key");
    auto key = std::make_unique<KeyBifResourceContainer>(*keyPath);
    key->init();
    set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(key), kKeyTypes),
            ResourceOwner::Global,
            ResourceSourceBucket::KeyBif);

    // Icons are in the GUI pack, effect and world textures in the world pack.
    for (const char *name : {"texturepacks/swpc_tex_gui.erf", "texturepacks/swpc_tex_tpa.erf"}) {
        if (auto pack = findFileIgnoreCase(k2Path, name)) {
            auto erf = std::make_unique<ErfResourceContainer>(*pack);
            erf->init();
            set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(erf), kTextureTypes),
                    ResourceOwner::Global,
                    ResourceSourceBucket::EncapsulatedClass2);
        }
    }

    // Power and effect sounds.
    if (auto soundsPath = findFileIgnoreCase(k2Path, "streamsounds")) {
        auto folder = std::make_unique<FolderResourceContainer>(*soundsPath);
        folder->init();
        set.add(std::make_unique<TypeFilteredResourceContainer>(std::move(folder), kSoundTypes),
                ResourceOwner::Global,
                ResourceSourceBucket::LooseDirectory);
    }
}

Resource K2FallbackResources::get(const ResourceId &id) {
    auto found = find(id);
    if (!found) {
        throw ResourceNotFoundException(id.string());
    }
    return *found;
}

std::optional<Resource> K2FallbackResources::find(const ResourceId &id) {
    if (auto found = _primary->find(id)) {
        return found;
    }
    if (_types.count(id.type) == 0) {
        return std::nullopt;
    }
    auto found = _fallback.find(id);
    if (found) {
        _hits.insert(id);
    }
    return found;
}

std::optional<Resource> K2FallbackResources::findExcludingOwners(
    const ResourceId &id,
    const std::set<ResourceOwner> &excludedOwners) {
    if (auto found = _primary->findExcludingOwners(id, excludedOwners)) {
        return found;
    }
    if (_types.count(id.type) == 0) {
        return std::nullopt;
    }
    return _fallback.find(id);
}

} // namespace resource

} // namespace reone
