/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#include "reone/system/renderdiag.h"

#include "reone/system/logutil.h"

#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace reone {

namespace renderdiag {

namespace {

struct Category {
    int count {0};
    std::set<std::string> keys;
    std::vector<std::string> order;
};

std::mutex g_mutex;
std::map<std::string, Category> g_categories;

std::string jsonEscape(const std::string &s) {
    std::string out;
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += ' ';
            } else {
                out += c;
            }
        }
    }
    return out;
}

} // namespace

bool report(const std::string &category, const std::string &key, const std::string &message) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto &cat = g_categories[category];
        ++cat.count;
        if (!cat.keys.insert(key).second) {
            return false;
        }
        cat.order.push_back(key);
    }
    warn("[" + category + "] " + message, LogChannel::Render);
    return true;
}

bool reported(const std::string &category, const std::string &key) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_categories.find(category);
    return it != g_categories.end() && it->second.keys.count(key) > 0;
}

int count(const std::string &category) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_categories.find(category);
    return it == g_categories.end() ? 0 : it->second.count;
}

int distinct(const std::string &category) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_categories.find(category);
    return it == g_categories.end() ? 0 : static_cast<int>(it->second.keys.size());
}

std::string summaryJson() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &[name, cat] : g_categories) {
        if (!first) {
            out << ",";
        }
        first = false;
        out << "\"" << jsonEscape(name) << "\":{\"count\":" << cat.count << ",\"distinct\":" << cat.keys.size() << ",\"keys\":[";
        for (size_t i = 0; i < cat.order.size() && i < 20; ++i) {
            out << (i ? "," : "") << "\"" << jsonEscape(cat.order[i]) << "\"";
        }
        out << "]}";
    }
    out << "}";
    return out.str();
}

void reset() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_categories.clear();
}

} // namespace renderdiag

} // namespace reone
