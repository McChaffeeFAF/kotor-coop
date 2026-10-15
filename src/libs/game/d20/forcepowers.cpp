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

#include "reone/game/d20/forcepowers.h"

#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"

namespace reone {

namespace game {

bool isHUDForcePower(const Spell &spell) {
    return spell.userType == kForcePowerUserType && spell.category != 0;
}

std::vector<std::shared_ptr<Spell>> hudForcePowers(const std::set<SpellType> &known, const ISpells &spells, bool hostile) {
    std::vector<std::shared_ptr<Spell>> result;
    for (SpellType type : known) {
        auto spell = spells.get(type);
        if (spell && isHUDForcePower(*spell) && spell->hostile == hostile) {
            result.push_back(std::move(spell));
        }
    }
    return result;
}

} // namespace game

} // namespace reone
