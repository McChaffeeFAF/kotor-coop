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

#include "types.h"

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

// Weapon on-hit properties (item property 32, iprp_onhit subtypes;
// design note c11_data, "Item properties").

class Creature;
class Game;
class Item;
struct ServicesView;

enum class OnHitKind {
    Sleep = 0,
    Stun = 1,
    Paralyze = 2,
    Confusion = 3,
    Fear = 4,
    Slow = 5,
    AbilityDrain = 6,
    ItemPoison = 7,
    SlayRacialGroup = 8,
    SlayAlignmentGroup = 9,
    InstantDeath = 10
};

/** The fixed per-subtype rule: which save resists it and whether it lasts. */
struct OnHitRule {
    /** 1 Fortitude, 2 Reflex, 3 Will; 0 when there is no save. */
    int save {0};
    SavingThrowType saveType {SavingThrowType::None};
    /** Chance and duration come from iprp_onhitdur. */
    bool timed {false};
    /** Chance when not timed (100 = always). */
    int chance {0};
};

std::optional<OnHitRule> onHitRule(int subtype);

/** One on-hit roll, resolved from the tables. */
struct OnHitRoll {
    int chance {0};
    int durationRounds {0};
    int saveDC {20};
};

/** Chance and duration (iprp_onhitdur by Param1Value) and save DC (iprp_onhitdc by CostValue, default 20). */
OnHitRoll resolveOnHitRoll(resource::ITwoDAs &twoDas, int subtype, int param1Value, int costValue, const OnHitRule &rule);

/**
 * After a hit: every active on-hit property of the weapon rolls its chance,
 * lets the target save, and applies its effect. Tick only (gameplay RNG).
 */
void applyOnHitProperties(Game &game, ServicesView &services, Creature &attacker, const Item &weapon, Creature &target);

} // namespace game

} // namespace reone
