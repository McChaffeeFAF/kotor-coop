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

#include "../types.h"

namespace reone {

namespace game {

class ISpells;
class Spell;

/** spells.2da usertype of a Force power. */
constexpr int kForcePowerUserType = 1;

/**
 * Whether a known spell is a Force power the HUD offers (design note k1hud_parity_2):
 * a usertype 1 row with a talent category. The conversation-only powers
 * (Affect Mind, Dominate) have no category and are never offered.
 */
bool isHUDForcePower(const Spell &spell);

/**
 * The Force powers a creature knows that the HUD offers, in spells.2da row
 * order. hostile: the target menu's powers (hostilesetting 1); otherwise
 * the quick bar's personal powers, cast on the user.
 */
std::vector<std::shared_ptr<Spell>> hudForcePowers(const std::set<SpellType> &known, const ISpells &spells, bool hostile);

} // namespace game

} // namespace reone
