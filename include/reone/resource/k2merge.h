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

#pragma once

#include "reone/system/cache.h"

#include "2da.h"
#include "provider/2das.h"
#include "resources.h"

#include <map>
#include <set>

namespace reone {

namespace resource {

/**
 * K2 systems merged into K1's data (design note k2_merge).
 *
 * Built at run time from the player's own K1 and K2 installs; nothing is
 * copied or written. K1 rows keep their indices, K2-only rows are appended
 * after them, and every merged table keeps an explicit K2-row-to-merged-row
 * map. Without a K2 install none of this exists and K1 data is served as is.
 */

/// Strrefs of appended K2 rows carry this flag and resolve through K2's TLK.
constexpr int kK2StrRefFlag = 0x40000000;

inline bool isK2StrRef(int strRef) {
    return strRef >= 0 && (strRef & kK2StrRefFlag) != 0;
}

inline int toK2StrRef(int strRef) {
    return strRef >= 0 ? (strRef | kK2StrRefFlag) : strRef;
}

/// Strrefs of our own rows (the Grey feat) carry this flag and resolve
/// through our own string table (k2MergeOwnStrings).
constexpr int kOwnStrRefFlag = 0x20000000;

inline bool isOwnStrRef(int strRef) {
    return strRef >= 0 && (strRef & kOwnStrRefFlag) != 0 && (strRef & kK2StrRefFlag) == 0;
}

/// Our own strings, by index (strref = kOwnStrRefFlag | index). Marked ours in
/// the spec: they are not K1's or K2's text.
const std::vector<std::string> &k2MergeOwnStrings();

/// A row of our own appended after a merged table.
struct OwnTwoDARow {
    std::string table;
    std::string label;
    std::vector<std::pair<std::string, std::string>> cells;
};

/// Our own rows: the Grey feat (feat.2da).
const std::vector<OwnTwoDARow> &k2MergeOwnRows();

/// Scripts named by appended K2 rows are K2 programs: this prefix routes
/// them to K2's scripts and K2's routine numbering.
constexpr char kK2ScriptPrefix[] = "k2:";

inline bool isK2ScriptName(const std::string &name) {
    return name.size() > 3 && name.compare(0, 3, kK2ScriptPrefix) == 0;
}

enum class TwoDAMergeKind {
    /// K2's table extends K1's row for row: K2 row i < K1's row count is K1
    /// row i; later K2 rows are appended at their own index.
    IndexAligned,

    /// K2 inserted rows among K1's: a K2 row maps to the K1 row with the same
    /// label, otherwise it is appended after K1's rows.
    LabelMatched,

    /// K2's table replaces K1's (it is K1's with rows added, the shared rows
    /// verified equal except K1's cap row).
    K2Wins,

    /// K1 has no such table; K2's is served, with its strrefs flagged.
    K2Only
};

struct TwoDAMergeRule {
    std::string table;
    TwoDAMergeKind kind {TwoDAMergeKind::IndexAligned};
    std::vector<std::string> strRefColumns;
    std::vector<std::string> scriptColumns;
    /**
     * Scripts refer to a row by its row label, not its index (visualeffects:
     * 1001, 9011, ...). Appended K2 rows then keep K2's label unless a K1 row
     * already has it, and K2 ids are mapped label to label.
     */
    bool idsAreRowLabels {false};
};

/// The tables the merge builds, and how.
const std::vector<TwoDAMergeRule> &k2MergeRules();
const TwoDAMergeRule *findK2MergeRule(const std::string &table);

struct TwoDAMergeMap {
    int k1Rows {0};
    int k2Rows {0};
    int mergedRows {0};
    int appendedRows {0};
    int labelMismatches {0}; // IndexAligned: shared rows whose labels differ
    std::vector<int> k2ToMerged;
    /// idsAreRowLabels: K2 row label -> merged row label, for every K2 row
    /// with a numeric label. Equal unless the label collided with a K1 row.
    std::map<int, int> k2LabelToMerged;
    int relabelledRows {0}; // appended K2 rows given a new label (a collision)

    /// Merged row for a K2 row, or -1 when K2 has no such row.
    int mapK2Row(int k2Row) const {
        return k2Row >= 0 && k2Row < static_cast<int>(k2ToMerged.size()) ? k2ToMerged[k2Row] : -1;
    }

    /// Merged row label for a K2 row label, or -1 when K2 has no such row.
    int mapK2Label(int k2Label) const {
        auto it = k2LabelToMerged.find(k2Label);
        return it != k2LabelToMerged.end() ? it->second : -1;
    }
};

/**
 * Merge one table. `k1` may be null (K2Only). The result keeps K1's columns in
 * K1's order, followed by the columns only K2 has.
 */
std::shared_ptr<TwoDA> mergeTwoDA(
    const TwoDA *k1,
    const TwoDA &k2,
    const TwoDAMergeRule &rule,
    TwoDAMergeMap &map);

/**
 * The merged table set: an ITwoDAs over K1's tables and K2's. Tables without
 * a merge rule are K1's, untouched.
 */
class K2MergedTwoDAs : public ITwoDAs, boost::noncopyable {
public:
    K2MergedTwoDAs(ITwoDAs &k1, ITwoDAs &k2) :
        _k1(k1),
        _k2(k2) {
    }

    void clear() override;

    std::shared_ptr<TwoDA> get(const std::string &key) override;

    /// The row map of a merged table, or null for an unmerged one.
    const TwoDAMergeMap *mergeMap(const std::string &table);

    /// Merged row of a K2 row of a table; the row itself for unmerged tables.
    int mapK2Row(const std::string &table, int k2Row);

    /**
     * What a K2 script's id for a row of this table is in the merged table:
     * the row label for a table whose ids are row labels (visualeffects),
     * else the row index. -1 when K2 has no such row.
     */
    int mapK2Id(const std::string &table, int k2Id);

    /// Row count of K1's own table (before appending), or -1.
    int k1RowCount(const std::string &table);

    /// Index of one of our own rows in a merged table, by label, or -1.
    int ownRowIndex(const std::string &table, const std::string &label);

    ITwoDAs &k1() { return _k1; }
    ITwoDAs &k2() { return _k2; }

private:
    ITwoDAs &_k1;
    ITwoDAs &_k2;

    std::map<std::string, std::shared_ptr<TwoDA>> _merged;
    std::map<std::string, TwoDAMergeMap> _maps;
    std::map<std::string, std::map<std::string, int>> _ownRows;
    std::set<std::string> _missing;

    void ensureMerged(const std::string &table);
};

/**
 * The K2 data set: K2's tables, compiled scripts, models, textures and
 * sounds, read from the player's K2 install. It is standalone (no fallback)
 * and is consulted only through the merge, the "k2:" script prefix and the
 * fallback layer below.
 */
void mountK2DataSources(Resources &set, const std::filesystem::path &k2Path);

/// Resource types the fallback layer may serve from K2 when K1 has none.
const std::set<ResType> &k2FallbackResourceTypes();

/**
 * K1's resources with K2 behind them: a lookup of a fallback type that K1
 * cannot answer is tried in the K2 data set. K1 always wins a name both have.
 * Mounts and retirements act on K1's set only.
 */
class K2FallbackResources : public IResources, boost::noncopyable {
public:
    K2FallbackResources(std::unique_ptr<IResources> primary,
                        IResources &fallback,
                        std::set<ResType> types = k2FallbackResourceTypes()) :
        _primary(std::move(primary)),
        _fallback(fallback),
        _types(std::move(types)) {
    }

    void clear() override { _primary->clear(); }
    void clearOwner(ResourceOwner owner) override { _primary->clearOwner(owner); }
    ResourceMountToken mountToken() const override { return _primary->mountToken(); }
    void rollbackTo(ResourceMountToken token) override { _primary->rollbackTo(token); }

    void addEXE(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addEXE(path, bucket);
    }
    void addKEY(const std::filesystem::path &path,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addKEY(path, bucket);
    }
    void addERF(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addERF(path, owner, bucket);
    }
    void addMemERF(ByteBuffer buffer,
                   ResourceOwner owner,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addMemERF(std::move(buffer), owner, bucket);
    }
    void addRIM(const std::filesystem::path &path,
                ResourceOwner owner = ResourceOwner::Global,
                std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addRIM(path, owner, bucket);
    }
    void addMemRIM(ByteBuffer buffer,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addMemRIM(std::move(buffer), owner, bucket);
    }
    void addFolder(const std::filesystem::path &path,
                   ResourceOwner owner = ResourceOwner::Global,
                   std::optional<ResourceSourceBucket> bucket = std::nullopt) override {
        _primary->addFolder(path, owner, bucket);
    }

    Resource get(const ResourceId &id) override;
    std::optional<Resource> find(const ResourceId &id) override;
    std::optional<Resource> findExcludingOwners(
        const ResourceId &id,
        const std::set<ResourceOwner> &excludedOwners) override;

    IResources &primary() { return *_primary; }

    /// Names answered from K2 so far (for the merge report).
    const std::set<ResourceId> &fallbackHits() const { return _hits; }

private:
    std::unique_ptr<IResources> _primary;
    IResources &_fallback;
    std::set<ResType> _types;
    std::set<ResourceId> _hits;
};

} // namespace resource

} // namespace reone
