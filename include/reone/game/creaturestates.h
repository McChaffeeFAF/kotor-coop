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

#include <optional>
#include <set>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

#include "types.h"

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

// statescripts.2da, removefxondeath.2da and formations.2da
// (design note c11_data).

class Creature;
struct EffectInstance;

/**
 * statescripts.2da row a creature's state runs after its heartbeat:
 * confusion row 3, fear row 2, psychic static row 1; none otherwise.
 */
std::optional<int> stateScriptRow(const Creature &creature);

/** The row's scriptname, lower case; empty when there is none. */
std::string stateScriptName(resource::ITwoDAs &twoDas, int row);

/** removefxondeath.2da effecttype values: effect types a death leaves on. */
std::set<int> effectTypesKeptOnDeath(resource::ITwoDAs &twoDas);

/** Equipped and innate effects, and the listed types, survive death. */
bool keepsEffectOnDeath(const EffectInstance &effect, const std::set<int> &keptTypes);

/** Removes every effect a death takes away (K2 behaviour). */
void removeEffectsOnDeath(resource::ITwoDAs &twoDas, Creature &creature);

/**
 * formations.2da slot nPosition (1-based) of pattern row: distance metres
 * from the anchor, at the anchor's facing + angle (counter-clockwise, 90 is
 * straight ahead). std::nullopt without the row or cell.
 */
std::optional<glm::vec3> formationSlot(
    resource::ITwoDAs &twoDas, int pattern, int position,
    const glm::vec3 &anchorPosition, float anchorFacing);

/** One effecticon.2da row shown for an effect a creature carries. */
struct EffectIconEntry {
    int row {0};
    std::string icon; // iconresref
    bool good {false};
    int descriptionStrRef {-1};
    int priority {0};
};

/**
 * The effect icons of a creature's effects (SetEffectIcon rows, each once),
 * in ascending effecticon.2da priority, then row. Data only: the HUD draws
 * them (reone choice for the order; design note c11_data).
 */
std::vector<EffectIconEntry> creatureEffectIcons(resource::ITwoDAs &twoDas, const Creature &creature);

} // namespace game

} // namespace reone
