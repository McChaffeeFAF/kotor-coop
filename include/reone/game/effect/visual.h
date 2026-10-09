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
#include "../visualeffectpresenter.h"

namespace reone {

namespace game {

class ServicesView;
struct VisualEffectDesc;

/**
 * An EffectVisualEffect: the effect object is simulation (it exists and
 * expires on the tick like any effect); what it draws is handed to the
 * VisualEffectPresenter, which spawns and removes the models per frame.
 */
class VisualEffect : public Effect {
public:
    VisualEffect(int visualEffectId, bool missEffect, ServicesView &services);
    ~VisualEffect();

    void applyTo(Object &object) override;
    void onRemove(Object &object, const EffectInstance &instance) override;
    void retireAreaRuntime(
        const std::set<const Object *> &retainedObjects) override;
    void setLocation(glm::vec3 loc) { _location = loc; }
    void setFacing(float facing) { _facing = facing; }

    /**
     * How long an instant visual at a location holds its slot: the longest
     * impact animation or impact sound of its row. From model data only.
     */
    float duration() const;

    const VisualEffectDesc *desc() const { return _desc; }

private:
    int _visualEffectId;
    bool _missEffect;
    const VisualEffectDesc *_desc {nullptr};
    std::optional<glm::vec3> _location;
    float _facing {0.0f};
    ServicesView &_services;

    /** One presentation request per application of this effect. */
    std::vector<std::shared_ptr<VisualEffectPresenter::Request>> _requests;

    void endRequests(std::optional<uint32_t> targetId);
};

} // namespace game

} // namespace reone
