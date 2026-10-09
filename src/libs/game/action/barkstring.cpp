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

#include "reone/game/action/barkstring.h"

#include "reone/game/game.h"
#include "reone/resource/di/services.h"
#include "reone/resource/strings.h"

namespace reone {

namespace game {

void BarkStringAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    // The bark bubble, timed as BarkString times it.
    std::string text = _services.resource.strings.getText(_strRef);
    if (!text.empty()) {
        float duration = barkDuration(text);
        _game.setBarkBubbleText(text, duration);
    }
    complete();
}

} // namespace game

} // namespace reone
