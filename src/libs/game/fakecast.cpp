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

#include "reone/game/fakecast.h"

#include "reone/audio/mixer.h"
#include "reone/game/d20/spell.h"
#include "reone/game/di/services.h"
#include "reone/game/object/creature.h"

using namespace reone::scene;

namespace reone {

namespace game {

// Clip names in the K1 creature supermodel.
static const char *kConjureAnimation = "handconjure";
static const char *kCastAnimation = "castout1";

bool FakeCast::update(Creature &caster, const Spell *spell, const glm::vec3 &target, ServicesView &services, float dt) {
    switch (_stage) {
    case Stage::Start:
        if (!spell) {
            _stage = Stage::Done;
            return true;
        }
        caster.face(target);
        caster.setMovementType(Creature::MovementType::None);
        caster.setMovementRestricted(true);
        _elapsed = 0.0f;
        _stage = Stage::Conjure;
        if (spell->conjTime > 0.0f) {
            caster.playAnimation(kConjureAnimation, AnimationProperties::fromFlags(AnimationFlags::loopBlend));
        }
        [[fallthrough]];
    case Stage::Conjure:
        _elapsed += dt;
        if (_elapsed < spell->conjTime) {
            return false;
        }
        _elapsed = 0.0f;
        _stage = Stage::Cast;
        caster.playAnimation(kCastAnimation, AnimationProperties::fromFlags(AnimationFlags::loopBlend));
        if (spell->castSound) {
            services.audio.mixer.play(
                spell->castSound,
                audio::AudioType::Sound,
                /*gain=*/1.0f,
                /*loop=*/false,
                caster.position());
        }
        return false;
    case Stage::Cast:
        _elapsed += dt;
        if (_elapsed < spell->castTime) {
            return false;
        }
        finish(caster);
        return true;
    case Stage::Done:
    default:
        return true;
    }
}

void FakeCast::cancel(Creature &caster) {
    if (_stage == Stage::Conjure || _stage == Stage::Cast) {
        finish(caster);
    }
    _stage = Stage::Done;
}

void FakeCast::finish(Creature &caster) {
    caster.setMovementRestricted(false);
    caster.resumeStateDrivenAnimation();
    _stage = Stage::Done;
}

} // namespace game

} // namespace reone
