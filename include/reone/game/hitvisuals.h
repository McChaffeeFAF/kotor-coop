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

#include <glm/vec3.hpp>
#include <memory>
#include <vector>

#include "reone/game/visualeffectpresenter.h"

namespace reone {

namespace resource {
class TwoDA;
}

namespace game {

class Game;
class Object;
class ServicesView;

/**
 * Hit and clash visuals (design note p2_combat, item 4). Presentation only:
 * they go straight to the visual effect presenter, never through the
 * simulation's effect lists, and draw nothing from the gameplay stream.
 */
namespace hitvisuals {

// visualeffects.2da rows (K1 and K2 share them).
constexpr int kSparksLarge = 4003;      // VFX_COM_SPARKS_LARGE
constexpr int kSparksMelee = 4004;      // VFX_COM_SPARKS_LIGHTSABER
constexpr int kSparksParry = 4011;      // VFX_COM_SPARKS_PARRY_METAL
constexpr int kBlasterDeflection = 4023; // VFX_COM_BLASTER_DEFLECTION
constexpr int kBlasterImpactGround = 4032; // VFX_COM_BLASTER_IMPACT_GROUND

/**
 * The damagehitvisual.2da visuals for a blow's damage flags: one row per
 * non-physical damage type present (bludgeoning, piercing and slashing have
 * none), from "rangedeffect" for a ranged attack and "visualeffect" for a
 * melee one. Empty cells give nothing.
 */
std::vector<int> damageTypeVisuals(const resource::TwoDA &table, int damageFlags, bool ranged);

/** Show a fire-and-forget row on an object (its model's attach points). */
void presentOn(Game &game, ServicesView &services, int row, const Object &target);

/** Show a fire-and-forget row at a world position. */
void presentAt(Game &game, ServicesView &services, int row, const glm::vec3 &position, float facing);

/**
 * A blow landed, was parried or was deflected: show its visuals. A landed
 * melee blow on a creature throws sparks at its impact node; on a door or
 * placeable sparks and parry sparks at its look-at point. A deflected bolt
 * shows the deflection flash on the deflector. A melee blow's damage types
 * add their damagehitvisual rows; a bolt's are shown when it arrives
 * (Projectile::presentImpact).
 */
void presentAttack(Game &game, ServicesView &services, const Object &target,
                   bool ranged, bool landed, bool deflected, int damageFlags);

/**
 * A spell's cast-hand visual (spells.2da casthandvisual) on the caster's
 * right hand, until endCastHand. Null when the spell names none that K1
 * ships. Goes to co-op guests like any visual effect.
 */
std::shared_ptr<VisualEffectPresenter::Request> beginCastHand(Game &game, ServicesView &services, int spellRow, const Object &caster);

/** End a cast-hand visual begun by beginCastHand (null: nothing). */
void endCastHand(std::shared_ptr<VisualEffectPresenter::Request> &request);

} // namespace hitvisuals

} // namespace game

} // namespace reone
