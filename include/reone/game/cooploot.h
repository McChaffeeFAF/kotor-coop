/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace reone {

namespace game {

class Area;
class Object;

/**
 * Free Mode per-guest loot, kept for the session (host only).
 *
 * Each guest loots its own copy of a container or body. The copy and what
 * each guest took are recorded per module, container and guest, so leaving
 * a module and coming back restores them. Object ids change between visits,
 * so a container is keyed by its tag and position. A host's save carries the
 * ledger as an optional loose file (kSaveFileName) beside the slot's other
 * files; saves without it load with an empty ledger. Guest characters are
 * not saved.
 */
class CoopLootLedger {
public:
    struct Entry {
        std::string resref;
        std::string tag;
        uint16_t stack {1};
        bool plot {false};
        bool credits {false};

        friend bool operator==(const Entry &a, const Entry &b) {
            return a.resref == b.resref && a.tag == b.tag && a.stack == b.stack &&
                   a.plot == b.plot && a.credits == b.credits;
        }
    };

    struct Record {
        std::vector<Entry> baseline;
        std::map<std::string, std::set<size_t>> taken; // guest name -> baseline indices

        friend bool operator==(const Record &a, const Record &b) {
            return a.baseline == b.baseline && a.taken == b.taken;
        }
    };

    /**
     * Stable key of a container within its module. \p ordinal tells apart
     * same-tag containers in one half-metre cell (0: the first, whose key is
     * the plain tag and cell, as older saves wrote it).
     */
    static std::string key(const std::string &tag, const glm::vec3 &position, int ordinal = 0);

    const Record *find(const std::string &module, const std::string &container) const;
    /** Record the copy taken when a container was first opened (kept if already there). */
    Record &capture(const std::string &module, const std::string &container, const std::vector<Entry> &baseline);
    void markTaken(const std::string &module, const std::string &container, const std::string &guest, size_t index);
    bool isTaken(const std::string &module, const std::string &container, const std::string &guest, size_t index) const;
    /** Drop what this guest took everywhere: the name is free for someone else now. */
    void forgetGuest(const std::string &guest);
    void clear() { _records.clear(); }
    size_t size() const { return _records.size(); }

    /** The loose file in a save slot's directory that carries the ledger. */
    static constexpr const char *kSaveFileName = "coop_loot.json";

    /** Versioned JSON text for a save slot; records come out in key order, so equal ledgers write equal bytes. */
    std::string serialize() const;

    /**
     * Parse text written by serialize(). Strict: anything malformed, over a
     * sanity limit or from a newer version fails with a message in \p error
     * and leaves this ledger untouched.
     */
    bool deserialize(const std::string &text, std::string &error);

    friend bool operator==(const CoopLootLedger &a, const CoopLootLedger &b);

private:
    std::map<std::pair<std::string, std::string>, Record> _records;
};

/** The ledger key of a container in its area: same-tag neighbours in one cell get ordinals in the area's object order. */
std::string coopLootKey(const Area &area, const Object &container);

} // namespace game

} // namespace reone
