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

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

class CreatureAttributes;
class Spell;

/**
 * Who a creature's force-point maximum is computed for (design note c03_stats).
 */
struct ForcePointSubject {
    int basePoints {0};             // ForcePoints: the stored base, raised by forcedie per spellcaster level
    bool droid {false};             // droids have no force points
    bool playerProgression {false}; // the player character: only Force-class levels count
};

/**
 * Maximum force points.
 *
 * The player character sums, over its Force-class levels, max(1, forcedie +
 * Wisdom modifier), and has none unless its latest class slot is a Force
 * class. Anyone else with a Force class has base + Wisdom modifier x total
 * level, never less than the total level.
 */
int computeMaxForcePoints(const CreatureAttributes &attributes, const ForcePointSubject &subject);

/**
 * Who pays for a force power, and what they bring to the price.
 */
struct ForcePowerPayer {
    bool playerCharacter {false}; // alignment scaling applies to the PC only
    int goodEvil {50};            // 0..100
    int charismaModifier {0};
    /// The K2 systems merge's neutral bonus: no forceadjust surcharge.
    bool waiveOppositeSurcharge {false};
    /// Force forms: Potency and Mastery raise the cost by 20%.
    float formMultiplier {1.0f};
};

/**
 * The force points a power costs: spells.2da forcepoints, scaled for the
 * player character by forceadjust.2da (goodcost/evilcost at row GoodEvil/10)
 * with Charisma trimming a surcharge, truncated and at least 1 when the base
 * cost is not zero. A missing forceadjust table leaves the base cost.
 */
int computeForcePowerCost(const Spell &spell, const ForcePowerPayer &payer, const resource::TwoDA *forceAdjust);

} // namespace game

} // namespace reone
