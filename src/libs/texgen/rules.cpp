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

#include "reone/texgen/rules.h"

#include "reone/system/exception/validation.h"

namespace reone {

namespace texgen {

bool globMatch(const std::string &pattern, const std::string &text) {
    std::string p = boost::to_lower_copy(pattern);
    std::string t = boost::to_lower_copy(text);
    size_t pi = 0;
    size_t ti = 0;
    size_t star = std::string::npos;
    size_t mark = 0;
    while (ti < t.size()) {
        if (pi < p.size() && (p[pi] == '?' || p[pi] == t[ti])) {
            ++pi;
            ++ti;
        } else if (pi < p.size() && p[pi] == '*') {
            star = pi++;
            mark = ti;
        } else if (star != std::string::npos) {
            pi = star + 1;
            ti = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == '*') {
        ++pi;
    }
    return pi == p.size();
}

static Rule make(const char *match, const char *className, Action action, float scale = 1.0f) {
    Rule rule;
    rule.match = match;
    rule.className = className;
    rule.action = action;
    rule.scale = scale;
    return rule;
}

Rules Rules::defaults() {
    Rules rules;
    // Lightmaps carry baked light, not surface detail.
    rules.add(make("*_lm*", "lightmap", Action::Skip));
    // Text, decals and the user interface are flat art.
    {
        Rule font = make("*", "text", Action::Skip);
        font.font = true;
        rules.add(font);
        Rule decal = make("*", "decal", Action::Skip);
        decal.decal = true;
        rules.add(decal);
    }
    rules.add(make("fnt_*", "text", Action::Skip));
    rules.add(make("font*", "text", Action::Skip));
    for (const char *ui : {"lbl_*", "ui_*", "gui_*", "btn_*", "i_*", "po_*"}) {
        rules.add(make(ui, "ui", Action::Skip));
    }
    // Effects: flames, beams, glows and the like.
    for (const char *fx : {"fx_*", "*_fx", "*_fx_*", "flare*", "glow*"}) {
        rules.add(make(fx, "effect", Action::Skip));
    }
    // Heads (player and NPC): skin looks wrong with strong relief, so soften.
    rules.add(make("p_*", "face", Action::Generate, 0.35f));
    rules.add(make("n_*", "face", Action::Generate, 0.35f));
    // Alpha-cutout foliage: soften, the silhouette is the alpha.
    {
        Rule foliage = make("*", "foliage", Action::Generate, 0.5f);
        foliage.cutout = true;
        rules.add(foliage);
    }
    return rules;
}

static std::optional<bool> optBool(const boost::json::object &o, const char *key) {
    auto *v = o.if_contains(key);
    if (!v) {
        return std::nullopt;
    }
    if (!v->is_bool()) {
        throw ValidationException(std::string("texgen rules: '") + key + "' must be true or false");
    }
    return v->as_bool();
}

static std::optional<float> optNumber(const boost::json::object &o, const char *key) {
    auto *v = o.if_contains(key);
    if (!v) {
        return std::nullopt;
    }
    if (!v->is_number()) {
        throw ValidationException(std::string("texgen rules: '") + key + "' must be a number");
    }
    return static_cast<float>(v->to_number<double>());
}

Rules Rules::parse(const std::string &text) {
    boost::json::value json;
    try {
        json = boost::json::parse(text);
    } catch (const std::exception &e) {
        throw ValidationException(std::string("texgen rules: not valid JSON: ") + e.what());
    }
    if (!json.is_object() || !json.as_object().if_contains("rules") || !json.as_object().at("rules").is_array()) {
        throw ValidationException("texgen rules: expected an object with a \"rules\" array");
    }
    Rules rules;
    int index = 0;
    for (const auto &item : json.as_object().at("rules").as_array()) {
        ++index;
        if (!item.is_object()) {
            throw ValidationException("texgen rules: rule " + std::to_string(index) + " is not an object");
        }
        const auto &o = item.as_object();
        Rule rule;
        auto *match = o.if_contains("match");
        if (!match || !match->is_string()) {
            throw ValidationException("texgen rules: rule " + std::to_string(index) + " needs a \"match\" string");
        }
        rule.match = std::string(match->as_string());
        if (auto *cls = o.if_contains("class"); cls && cls->is_string()) {
            rule.className = std::string(cls->as_string());
        } else {
            rule.className = "surface";
        }
        if (auto *action = o.if_contains("action")) {
            std::string value = action->is_string() ? std::string(action->as_string()) : "";
            if (value == "skip") {
                rule.action = Action::Skip;
            } else if (value == "generate") {
                rule.action = Action::Generate;
            } else {
                throw ValidationException("texgen rules: rule " + std::to_string(index) + ": \"action\" is \"generate\" or \"skip\"");
            }
        }
        rule.strength = optNumber(o, "strength");
        if (auto scale = optNumber(o, "scale")) {
            rule.scale = *scale;
        }
        if (rule.strength && *rule.strength < 0.0f) {
            throw ValidationException("texgen rules: rule " + std::to_string(index) + ": strength cannot be negative");
        }
        if (rule.scale < 0.0f) {
            throw ValidationException("texgen rules: rule " + std::to_string(index) + ": scale cannot be negative");
        }
        rule.cutout = optBool(o, "cutout");
        rule.decal = optBool(o, "decal");
        rule.font = optBool(o, "font");
        rule.shiny = optBool(o, "shiny");
        if (auto v = optNumber(o, "minSize")) {
            rule.minSize = static_cast<int>(*v);
        }
        if (auto v = optNumber(o, "maxSize")) {
            rule.maxSize = static_cast<int>(*v);
        }
        rules.add(std::move(rule));
    }
    return rules;
}

Rules Rules::load(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw ValidationException("texgen rules: cannot open " + path.string());
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

Decision Rules::classify(const TextureInfo &info, float baseStrength) const {
    for (const auto &rule : _rules) {
        if (!globMatch(rule.match, info.name)) {
            continue;
        }
        if (rule.cutout && *rule.cutout != info.cutout) {
            continue;
        }
        if (rule.decal && *rule.decal != info.decal) {
            continue;
        }
        if (rule.font && *rule.font != info.font) {
            continue;
        }
        if (rule.shiny && *rule.shiny != info.shiny) {
            continue;
        }
        int side = std::max(info.width, info.height);
        if (rule.minSize && side < *rule.minSize) {
            continue;
        }
        if (rule.maxSize && side > *rule.maxSize) {
            continue;
        }
        Decision decision;
        decision.className = rule.className;
        decision.action = rule.action;
        decision.strength = rule.strength ? *rule.strength : baseStrength * rule.scale;
        decision.rule = rule.match;
        return decision;
    }
    Decision decision;
    decision.className = "surface";
    decision.strength = baseStrength;
    return decision;
}

boost::json::object Rules::toJson() const {
    boost::json::array array;
    for (const auto &rule : _rules) {
        boost::json::object o;
        o["match"] = rule.match;
        o["class"] = rule.className;
        o["action"] = rule.action == Action::Skip ? "skip" : "generate";
        if (rule.strength) {
            o["strength"] = *rule.strength;
        } else if (rule.scale != 1.0f) {
            o["scale"] = rule.scale;
        }
        if (rule.cutout) {
            o["cutout"] = *rule.cutout;
        }
        if (rule.decal) {
            o["decal"] = *rule.decal;
        }
        if (rule.font) {
            o["font"] = *rule.font;
        }
        if (rule.shiny) {
            o["shiny"] = *rule.shiny;
        }
        if (rule.minSize) {
            o["minSize"] = *rule.minSize;
        }
        if (rule.maxSize) {
            o["maxSize"] = *rule.maxSize;
        }
        array.push_back(std::move(o));
    }
    boost::json::object root;
    root["version"] = 1;
    root["rules"] = std::move(array);
    return root;
}

std::string Rules::fingerprint() const {
    return boost::json::serialize(toJson());
}

} // namespace texgen

} // namespace reone
