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

#include <boost/json.hpp>

namespace reone {

namespace texgen {

/// What a rule can tell about a texture besides its name.
struct TextureInfo {
    std::string name;
    int width {0};
    int height {0};
    bool hasAlpha {false};
    bool cutout {false}; ///< punch-through alpha: foliage, fences, grates
    bool decal {false};
    bool font {false};   ///< a TXI that describes a font (text)
    bool shiny {false};  ///< a TXI that names an environment map or a bumpy-shiny map
};

enum class Action {
    Generate,
    Skip
};

struct Decision {
    std::string className;
    Action action {Action::Generate};
    float strength {2.0f};
    std::string rule; ///< the pattern of the rule that decided, "" for the fallback
};

struct Rule {
    std::string match; ///< a glob on the texture name (* and ?), case-insensitive
    std::string className;
    Action action {Action::Generate};
    std::optional<float> strength; ///< absolute strength
    float scale {1.0f};            ///< multiplies the base strength when no absolute strength is given
    // Conditions on the texture; a condition that is not set does not matter.
    std::optional<bool> cutout;
    std::optional<bool> decal;
    std::optional<bool> font;
    std::optional<bool> shiny;
    std::optional<int> minSize; ///< the larger side, at least
    std::optional<int> maxSize; ///< the larger side, at most
};

/**
 * Texture classes by name and properties. The first rule that matches decides; a texture
 * no rule matches is a plain surface at the base strength. The rules are JSON:
 *
 *   {"version": 1, "rules": [
 *     {"match": "*_lm*", "class": "lightmap", "action": "skip"},
 *     {"match": "p_*", "class": "face", "scale": 0.35},
 *     {"match": "*", "decal": true, "class": "decal", "action": "skip"} ]}
 */
class Rules {
public:
    /// The rules built from public naming conventions (heads, UI, lightmaps, effects, decals, text, foliage).
    static Rules defaults();
    static Rules parse(const std::string &json);
    static Rules load(const std::filesystem::path &path);

    Decision classify(const TextureInfo &info, float baseStrength) const;

    const std::vector<Rule> &rules() const { return _rules; }
    void add(Rule rule) { _rules.push_back(std::move(rule)); }

    boost::json::object toJson() const;
    /// Stable text of the rules, for the manifest's change detection.
    std::string fingerprint() const;

private:
    std::vector<Rule> _rules;
};

/// A case-insensitive glob: * matches any run, ? any one character.
bool globMatch(const std::string &pattern, const std::string &text);

} // namespace texgen

} // namespace reone
