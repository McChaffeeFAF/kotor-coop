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

#include "reone/texgen/manifest.h"

#include <boost/json.hpp>

#include "reone/system/exception/validation.h"

namespace reone {

namespace texgen {

void Hasher::add(const void *data, size_t size) {
    auto *bytes = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) {
        _state ^= bytes[i];
        _state *= 1099511628211ull;
    }
}

std::string Hasher::hex() const {
    std::stringstream ss;
    ss << std::hex << std::setfill('0') << std::setw(16) << _state;
    return ss.str();
}

std::string Manifest::toText() const {
    boost::json::array textures;
    for (const auto &[name, entry] : entries) {
        boost::json::object o;
        o["texture"] = entry.texture;
        o["class"] = entry.className;
        o["status"] = entry.status;
        if (!entry.reason.empty()) {
            o["reason"] = entry.reason;
        }
        o["strength"] = entry.strength;
        o["sourceHash"] = entry.sourceHash;
        textures.push_back(std::move(o));
    }
    boost::json::object root;
    root["tool"] = "texgen";
    root["version"] = 1;
    root["textures"] = std::move(textures);
    return boost::json::serialize(root);
}

Manifest Manifest::fromText(const std::string &text) {
    boost::json::value json;
    try {
        json = boost::json::parse(text);
    } catch (const std::exception &e) {
        throw ValidationException(std::string("texgen manifest: not valid JSON: ") + e.what());
    }
    if (!json.is_object() || !json.as_object().if_contains("textures") || !json.as_object().at("textures").is_array()) {
        throw ValidationException("texgen manifest: expected a \"textures\" array");
    }
    Manifest manifest;
    for (const auto &item : json.as_object().at("textures").as_array()) {
        if (!item.is_object()) {
            throw ValidationException("texgen manifest: a texture entry is not an object");
        }
        const auto &o = item.as_object();
        auto str = [&](const char *key) {
            auto *v = o.if_contains(key);
            return v && v->is_string() ? std::string(v->as_string()) : std::string();
        };
        ManifestEntry entry;
        entry.texture = str("texture");
        if (entry.texture.empty()) {
            throw ValidationException("texgen manifest: a texture entry has no name");
        }
        entry.className = str("class");
        entry.status = str("status");
        entry.reason = str("reason");
        entry.sourceHash = str("sourceHash");
        if (auto *v = o.if_contains("strength"); v && v->is_number()) {
            entry.strength = static_cast<float>(v->to_number<double>());
        }
        manifest.entries[entry.texture] = entry;
    }
    return manifest;
}

Manifest Manifest::load(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return fromText(ss.str());
}

void Manifest::save(const std::filesystem::path &path) const {
    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw ValidationException("texgen: cannot write " + tmp.string());
        }
        out << toText();
    }
    std::filesystem::rename(tmp, path);
}

} // namespace texgen

} // namespace reone
