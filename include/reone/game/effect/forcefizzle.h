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

#pragma once

#include "../effect.h"

#include "visual.h"

namespace reone {

namespace game {

class ForceFizzleEffect : public Effect {
public:
    ForceFizzleEffect() :
        Effect(EffectType::ForceFizzle) {
    }

    static constexpr int kVisual = 4036; // VFX_COM_JEDI_FORCE_FIZZLE

    /** The visual it shows (visualeffects.2da row), set by the routine. */
    void setVisual(std::shared_ptr<VisualEffect> visual) { _visual = std::move(visual); }

    bool onApply(Object &object, const EffectInstance &instance) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override {
        if (_visual) {
            _visual->retireAreaRuntime(retainedObjects);
        }
    }

private:
    std::shared_ptr<VisualEffect> _visual;
};

} // namespace game

} // namespace reone
