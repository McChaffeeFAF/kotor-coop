/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/cooploot.h"

#include <cmath>

#include <boost/json.hpp>

#include "reone/game/object/area.h"

namespace reone {

namespace game {

std::string CoopLootLedger::key(const std::string &tag, const glm::vec3 &position, int ordinal) {
    // Half-metre cells: stable across a reload, distinct for same-tag bodies.
    auto cell = [](float v) { return std::to_string(static_cast<int>(std::floor(v * 2.0f))); };
    auto key = tag + "@" + cell(position.x) + "," + cell(position.y) + "," + cell(position.z);
    return ordinal > 0 ? key + "#" + std::to_string(ordinal) : key;
}

std::string coopLootKey(const Area &area, const Object &container) {
    auto key = CoopLootLedger::key(container.tag(), container.position());
    // Same tag, same cell: count the ones before it in the area's load order,
    // which a revisit or a reload repeats.
    int ordinal = 0;
    for (const auto &other : area.objects()) {
        if (!other || other.get() == &container) {
            break;
        }
        if (other->tag() == container.tag() && CoopLootLedger::key(other->tag(), other->position()) == key) {
            ++ordinal;
        }
    }
    return CoopLootLedger::key(container.tag(), container.position(), ordinal);
}

const CoopLootLedger::Record *CoopLootLedger::find(const std::string &module, const std::string &container) const {
    auto it = _records.find({module, container});
    return it != _records.end() ? &it->second : nullptr;
}

CoopLootLedger::Record &CoopLootLedger::capture(const std::string &module, const std::string &container, const std::vector<Entry> &baseline) {
    auto [it, added] = _records.try_emplace({module, container});
    if (added) {
        it->second.baseline = baseline;
    }
    return it->second;
}

void CoopLootLedger::markTaken(const std::string &module, const std::string &container, const std::string &guest, size_t index) {
    auto it = _records.find({module, container});
    if (it != _records.end()) {
        it->second.taken[guest].insert(index);
    }
}

bool CoopLootLedger::isTaken(const std::string &module, const std::string &container, const std::string &guest, size_t index) const {
    auto record = find(module, container);
    if (!record) {
        return false;
    }
    auto it = record->taken.find(guest);
    return it != record->taken.end() && it->second.count(index) > 0;
}

void CoopLootLedger::forgetGuest(const std::string &guest) {
    for (auto &[key, record] : _records) {
        record.taken.erase(guest);
    }
}

namespace {

constexpr int kFormatVersion = 1;
// Sanity limits: a save is user-editable input.
constexpr size_t kMaxFileBytes = 8u * 1024 * 1024;
constexpr size_t kMaxRecords = 20000;
constexpr size_t kMaxEntries = 4096;
constexpr size_t kMaxGuests = 16;
constexpr size_t kMaxText = 256;

} // namespace

bool operator==(const CoopLootLedger &a, const CoopLootLedger &b) {
    return a._records == b._records;
}

std::string CoopLootLedger::serialize() const {
    boost::json::array records;
    for (const auto &[ids, record] : _records) {
        boost::json::object item;
        item["module"] = ids.first;
        item["container"] = ids.second;
        boost::json::array baseline;
        for (const Entry &entry : record.baseline) {
            boost::json::object e;
            e["resref"] = entry.resref;
            e["tag"] = entry.tag;
            e["stack"] = entry.stack;
            e["plot"] = entry.plot;
            e["credits"] = entry.credits;
            baseline.push_back(std::move(e));
        }
        item["baseline"] = std::move(baseline);
        boost::json::object taken;
        for (const auto &[guest, indices] : record.taken) {
            boost::json::array list;
            for (size_t index : indices) {
                list.push_back(static_cast<uint64_t>(index));
            }
            taken[guest] = std::move(list);
        }
        item["taken"] = std::move(taken);
        records.push_back(std::move(item));
    }
    boost::json::object root;
    root["version"] = kFormatVersion;
    root["records"] = std::move(records);
    return boost::json::serialize(root);
}

bool CoopLootLedger::deserialize(const std::string &text, std::string &error) {
    auto fail = [&error](const std::string &message) {
        error = message;
        return false;
    };
    if (text.size() > kMaxFileBytes) {
        return fail("co-op loot ledger is too large");
    }

    boost::json::value root;
    try {
        boost::json::parse_options options;
        options.max_depth = 8;
        boost::system::error_code ec;
        root = boost::json::parse(text, ec, {}, options);
        if (ec) {
            return fail("co-op loot ledger is not valid JSON: " + ec.message());
        }
    } catch (const std::exception &e) {
        return fail(std::string("co-op loot ledger is not valid JSON: ") + e.what());
    }

    auto *object = root.if_object();
    if (!object) {
        return fail("co-op loot ledger is not an object");
    }
    auto version = object->if_contains("version");
    if (!version || !version->is_int64() || version->as_int64() != kFormatVersion) {
        return fail("co-op loot ledger has an unsupported version");
    }
    auto records = object->if_contains("records");
    if (!records || !records->is_array() || records->as_array().size() > kMaxRecords) {
        return fail("co-op loot ledger has no usable record list");
    }

    auto text_of = [](const boost::json::object &o, const char *name, std::string &out) {
        auto *v = o.if_contains(name);
        if (!v || !v->is_string() || v->as_string().size() > kMaxText) {
            return false;
        }
        out = std::string(v->as_string());
        return true;
    };
    auto flag_of = [](const boost::json::object &o, const char *name, bool &out) {
        auto *v = o.if_contains(name);
        if (!v || !v->is_bool()) {
            return false;
        }
        out = v->as_bool();
        return true;
    };

    std::map<std::pair<std::string, std::string>, Record> parsed;
    for (const auto &value : records->as_array()) {
        auto *item = value.if_object();
        std::string module, container;
        if (!item || !text_of(*item, "module", module) || !text_of(*item, "container", container)) {
            return fail("co-op loot ledger record is malformed");
        }
        auto *baseline = item->if_contains("baseline");
        auto *taken = item->if_contains("taken");
        if (!baseline || !baseline->is_array() || baseline->as_array().size() > kMaxEntries ||
            !taken || !taken->is_object() || taken->as_object().size() > kMaxGuests) {
            return fail("co-op loot ledger record is malformed");
        }
        Record record;
        for (const auto &entryValue : baseline->as_array()) {
            auto *e = entryValue.if_object();
            Entry entry;
            auto *stack = e ? e->if_contains("stack") : nullptr;
            if (!e || !text_of(*e, "resref", entry.resref) || !text_of(*e, "tag", entry.tag) ||
                !flag_of(*e, "plot", entry.plot) || !flag_of(*e, "credits", entry.credits) ||
                !stack || !stack->is_int64() || stack->as_int64() < 0 || stack->as_int64() > 0xffff) {
                return fail("co-op loot ledger entry is malformed");
            }
            entry.stack = static_cast<uint16_t>(stack->as_int64());
            record.baseline.push_back(std::move(entry));
        }
        for (const auto &[guest, listValue] : taken->as_object()) {
            auto *list = listValue.if_array();
            if (!list || list->size() > kMaxEntries) {
                return fail("co-op loot ledger guest list is malformed");
            }
            std::set<size_t> indices;
            for (const auto &index : *list) {
                if (!index.is_int64() || index.as_int64() < 0 ||
                    static_cast<size_t>(index.as_int64()) >= record.baseline.size()) {
                    return fail("co-op loot ledger takes an item outside the container");
                }
                indices.insert(static_cast<size_t>(index.as_int64()));
            }
            record.taken[std::string(guest)] = std::move(indices);
        }
        if (!parsed.emplace(std::make_pair(module, container), std::move(record)).second) {
            return fail("co-op loot ledger lists a container twice");
        }
    }
    _records = std::move(parsed);
    error.clear();
    return true;
}

} // namespace game

} // namespace reone
