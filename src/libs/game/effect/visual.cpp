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

#include "reone/game/coopsession.h"
#include "reone/game/effect/visual.h"
#include "reone/audio/clip.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/area.h"
#include "reone/game/visualeffects.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"

namespace reone {

namespace game {

VisualEffect::VisualEffect(int visualEffectId, bool missEffect, ServicesView &services) :
    Effect(EffectType::Visual),
    _visualEffectId(visualEffectId),
    _missEffect(missEffect),
    _desc(services.game.visualEffects.get(visualEffectId).value_or(nullptr)),
    _services(services) {
    setSaveFacingInteger(0, visualEffectId);
    setSaveFacingInteger(2, missEffect ? 1 : 0);
}

VisualEffect::~VisualEffect() {
    endRequests(std::nullopt);
}

void VisualEffect::endRequests(std::optional<uint32_t> targetId) {
    for (auto &request : _requests) {
        if (!targetId || request->targetId == *targetId) {
            request->ended = true;
        }
    }
    _requests.erase(
        std::remove_if(_requests.begin(), _requests.end(), [](auto &request) { return request->ended; }),
        _requests.end());
}

void VisualEffect::retireAreaRuntime(
    const std::set<const Object *> &) {
    endRequests(std::nullopt);
}

void VisualEffect::onRemove(Object &object, const EffectInstance &) {
    // The effect object has ended on the tick; its models finish per frame.
    bool atLocation = _location.has_value() || dyn_cast<Area>(&object) != nullptr;
    endRequests(atLocation ? std::optional<uint32_t>(0) : std::optional<uint32_t>(object.id()));
}

static float durationAnim(const std::shared_ptr<graphics::Model> &model) {
    if (!model) {
        return 0.0f;
    }
    if (std::shared_ptr<graphics::Animation> anim = model->getAnimation("impact")) {
        return anim->length();
    }
    return 0.0f;
}

static float durationSound(const std::shared_ptr<audio::AudioClip> &sound) {
    if (!sound) {
        return 0.0f;
    }
    return sound->duration();
}

void VisualEffect::applyTo(Object &object) {
    if (!_desc) {
        return;
    }
    auto request = std::make_shared<VisualEffectPresenter::Request>();
    request->desc = _desc;
    if (_location || dyn_cast<Area>(&object)) {
        request->targetId = 0;
        request->location = _location.value_or(object.position());
        request->facing = _facing;
    } else {
        request->targetId = object.id();
        request->location = object.position();
        request->facing = object.getFacing();
    }
    request->sourceId = _beamSourceId;
    request->sourceBodyNode = _beamBodyNode;
    _requests.push_back(request);
    if (auto *coop = object.game().coop()) {
        coop->onVisualEffectBegin(request);
    }
    object.game().visualEffectPresenter().begin(std::move(request));
}

float VisualEffect::duration() const {
    if (!_desc) {
        return 0.0f;
    }
    float anim = std::max({durationAnim(_desc->impHeadConNode),
                           durationAnim(_desc->impImpactNode),
                           durationAnim(_desc->rootModel())});
    float sound = durationSound(_desc->soundImpact);
    return std::max(anim, sound);
}

} // namespace game

} // namespace reone
