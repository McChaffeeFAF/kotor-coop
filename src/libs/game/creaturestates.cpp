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

#include "reone/system/portablemath.h"
#include "reone/game/creaturestates.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include <boost/algorithm/string/case_conv.hpp>

#include "reone/game/effect.h"
#include "reone/game/object/creature.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"

namespace reone {

namespace game {

static constexpr int kStateRowPsychicStatic = 1;
static constexpr int kStateRowFear = 2;
static constexpr int kStateRowConfusion = 3;

std::optional<int> stateScriptRow(const Creature &creature) {
    if (creature.isDead()) {
        return std::nullopt;
    }
    if (creature.hasEffect(EffectType::Confused) || creature.hasEffect(EffectType::DroidConfused)) {
        return kStateRowConfusion;
    }
    if (creature.hasEffect(EffectType::Frightened)) {
        return kStateRowFear;
    }
    if (creature.hasEffect(EffectType::PsychicStatic)) {
        return kStateRowPsychicStatic;
    }
    return std::nullopt;
}

std::string stateScriptName(resource::ITwoDAs &twoDas, int row) {
    auto table = twoDas.get("statescripts");
    if (!table || row < 0 || row >= table->getRowCount()) {
        return "";
    }
    return boost::to_lower_copy(table->getString(row, "scriptname"));
}

std::set<int> effectTypesKeptOnDeath(resource::ITwoDAs &twoDas) {
    std::set<int> result;
    auto table = twoDas.get("removefxondeath");
    if (!table) {
        return result;
    }
    for (int row = 0; row < table->getRowCount(); ++row) {
        if (auto type = table->getIntOpt(row, "effecttype")) {
            result.insert(*type);
        }
    }
    return result;
}

bool keepsEffectOnDeath(const EffectInstance &effect, const std::set<int> &keptTypes) {
    auto duration = effect.durationType();
    if (duration == DurationType::Equipped || duration == DurationType::Innate) {
        return true;
    }
    return effect.effect && keptTypes.count(static_cast<int>(effect.effect->type())) != 0;
}

void removeEffectsOnDeath(resource::ITwoDAs &twoDas, Creature &creature) {
    std::set<int> kept = effectTypesKeptOnDeath(twoDas);
    std::vector<EffectId> removed;
    for (const auto &effect : creature.effects()) {
        if (!keepsEffectOnDeath(effect, kept)) {
            removed.push_back(effect.id);
        }
    }
    for (auto id : removed) {
        creature.removeEffectsById(id);
    }
}

std::optional<glm::vec3> formationSlot(
    resource::ITwoDAs &twoDas, int pattern, int position,
    const glm::vec3 &anchorPosition, float anchorFacing) {
    auto table = twoDas.get("formations");
    if (!table || pattern < 0 || pattern >= table->getRowCount() || position < 1) {
        return std::nullopt;
    }
    auto angle = table->getFloatOpt(pattern, "angle_" + std::to_string(position));
    auto distance = table->getFloatOpt(pattern, "distance_" + std::to_string(position));
    if (!angle || !distance) {
        return std::nullopt;
    }
    // Facing f looks along (-sin f, cos f), so angle 90 (straight ahead) is
    // the heading f + 90 degrees; the slot's heading is f + angle.
    float heading = anchorFacing + glm::radians(*angle);
    return anchorPosition + glm::vec3(portable::cosf(heading), portable::sinf(heading), 0.0f) * *distance;
}

std::vector<EffectIconEntry> creatureEffectIcons(resource::ITwoDAs &twoDas, const Creature &creature) {
    std::vector<EffectIconEntry> result;
    auto table = twoDas.get("effecticon");
    if (!table) {
        return result;
    }
    std::set<int> seen;
    for (const auto &effect : creature.effects()) {
        int row = effect.effect ? effect.effect->effectIcon() : 0;
        if (row <= 0 || row >= table->getRowCount() || !seen.insert(row).second) {
            continue;
        }
        EffectIconEntry entry;
        entry.row = row;
        entry.icon = boost::to_lower_copy(table->getString(row, "iconresref"));
        entry.good = table->getInt(row, "good", 0) != 0;
        entry.descriptionStrRef = table->getInt(row, "description", -1);
        entry.priority = table->getInt(row, "priority", 0);
        if (!entry.icon.empty()) {
            result.push_back(std::move(entry));
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.priority != b.priority ? a.priority < b.priority : a.row < b.row;
    });
    return result;
}

} // namespace game

} // namespace reone
