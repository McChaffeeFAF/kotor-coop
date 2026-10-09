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

#include "reone/game/effect/lightsaberthrow.h"

#include <glm/gtx/norm.hpp>

#include "reone/game/effect/damage.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

// Applied to the thrower (the script's OBJECT_SELF), temporary, for the
// round. The engine does the damage: each target is hit once, always, for
// 1d6 per two levels of the thrower (at least 1d6), as the saber reaches it.
// While the effect lasts the thrower is held in the throw pose
// (Creature::getHeldPoseAnimation) and cannot act.

bool LightsaberThrowEffect::onApply(Object &object, const EffectInstance &instance) {
    auto *thrower = dyn_cast<Creature>(&object);
    if (!thrower || thrower->isDead()) {
        return false;
    }
    std::vector<std::shared_ptr<Object>> targets;
    for (auto *target : {&_target1, &_target2, &_target3}) {
        auto resolved = target->resolve();
        if (!resolved) {
            break; // the targets come in order; the first missing one ends the list
        }
        targets.push_back(std::move(resolved));
    }
    if (targets.empty()) {
        return false;
    }

    // The flight path: thrower, each target in turn, back to the thrower.
    std::vector<float> reached;
    glm::vec3 from = thrower->position();
    float length = 0.0f;
    for (const auto &target : targets) {
        length += glm::distance(from, target->position());
        reached.push_back(length);
        from = target->position();
    }
    length += glm::distance(from, thrower->position());

    Flight flight;
    for (size_t i = 0; i < targets.size(); ++i) {
        Hit hit;
        hit.time = length > 0.0f ? kFlightTime * reached[i] / length : 0.0f;
        hit.target = targets[i];
        flight.hits.push_back(std::move(hit));
    }
    _flights[instance.id] = std::move(flight);
    thrower->enterHeldPose();
    return true;
}

void LightsaberThrowEffect::onRemove(Object &object, const EffectInstance &instance) {
    _flights.erase(instance.id);
    if (auto *thrower = dyn_cast<Creature>(&object)) {
        thrower->leaveHeldPose();
    }
}

bool LightsaberThrowEffect::onUpdate(Object &object, const EffectInstance &instance, float dt) {
    auto it = _flights.find(instance.id);
    auto *thrower = dyn_cast<Creature>(&object);
    if (it == _flights.end() || !thrower) {
        return true;
    }
    Flight &flight = it->second;
    flight.elapsed += dt;
    while (flight.next < flight.hits.size() && flight.hits[flight.next].time <= flight.elapsed) {
        auto target = flight.hits[flight.next].target.resolve();
        ++flight.next;
        if (!target || target->isDead() || thrower->isDead()) {
            continue;
        }
        int dice = std::max(1, thrower->attributes().getAggregateLevel() / 2);
        int amount = 0;
        for (int i = 0; i < dice; ++i) {
            amount += randomInt(1, 6);
        }
        auto damage = std::make_shared<DamageEffect>(amount, DamageType::Blaster, DamagePower::Normal);
        damage->setSaveFacingCreator(object.game().getObjectById(object.id()));
        target->applyEffect(damage, DurationType::Instant);
    }
    return true;
}

} // namespace game

} // namespace reone
