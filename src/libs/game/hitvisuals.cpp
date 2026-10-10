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

#include "reone/game/hitvisuals.h"

#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/di/services.h"
#include "reone/game/visualeffectpresenter.h"
#include "reone/game/visualeffects.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/scene/node/model.h"

namespace reone {

namespace game {

namespace hitvisuals {

// Bludgeoning, piercing and slashing are rows 0-2 and never show a visual.
static constexpr int kFirstVisualDamageRow = 3;

std::vector<int> damageTypeVisuals(const resource::TwoDA &table, int damageFlags, bool ranged) {
    std::vector<int> rows;
    const char *column = ranged ? "rangedeffect" : "visualeffect";
    for (int row = kFirstVisualDamageRow; row < table.getRowCount() && row < 31; ++row) {
        if ((damageFlags & (1 << row)) == 0) {
            continue;
        }
        int visual = table.getInt(row, column, -1);
        if (visual > 0) {
            rows.push_back(visual);
        }
    }
    return rows;
}

static void present(Game &game, ServicesView &services, int row, uint32_t targetId,
                    const glm::vec3 &location, float facing) {
    auto desc = services.game.visualEffects.get(static_cast<uint32_t>(row));
    if (!desc) {
        return;
    }
    auto request = std::make_shared<VisualEffectPresenter::Request>();
    request->desc = *desc;
    request->targetId = targetId;
    request->location = location;
    request->facing = facing;
    // Fire-and-forget: nothing in the simulation holds it.
    request->ended = true;
    if (auto *coop = game.coop()) {
        coop->onVisualEffectBegin(request);
    }
    game.visualEffectPresenter().begin(std::move(request));
}

std::shared_ptr<VisualEffectPresenter::Request> beginCastHand(Game &game, ServicesView &services, int spellRow, const Object &caster) {
    auto desc = services.game.visualEffects.get(VisualEffectDesc::castHandId(spellRow));
    if (!desc || !*desc) {
        return nullptr;
    }
    auto request = std::make_shared<VisualEffectPresenter::Request>();
    request->desc = *desc;
    request->targetId = caster.id();
    request->location = caster.position();
    request->facing = caster.getFacing();
    if (auto *coop = game.coop()) {
        coop->onVisualEffectBegin(request);
    }
    game.visualEffectPresenter().begin(request);
    return request;
}

void endCastHand(std::shared_ptr<VisualEffectPresenter::Request> &request) {
    if (request) {
        request->ended = true;
        request.reset();
    }
}

void presentOn(Game &game, ServicesView &services, int row, const Object &target) {
    present(game, services, row, target.id(), target.position(), target.getFacing());
}

void presentAt(Game &game, ServicesView &services, int row, const glm::vec3 &position, float facing) {
    present(game, services, row, 0, position, facing);
}

static glm::vec3 lookAtPoint(const Object &target) {
    if (auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(target.sceneNode())) {
        if (auto hook = model->getNodeByName("lookathook")) {
            return hook->origin();
        }
        if (auto impact = model->getNodeByName("impact")) {
            return impact->origin();
        }
    }
    return target.position() + glm::vec3(0.0f, 0.0f, 1.0f);
}

void presentAttack(Game &game, ServicesView &services, const Object &target,
                   bool ranged, bool landed, bool deflected, int damageFlags) {
    if (ranged) {
        // A landed bolt shows its visuals on arrival (Projectile::presentImpact).
        if (deflected) {
            presentOn(game, services, kBlasterDeflection, target);
        }
        return;
    }
    if (!landed) {
        return;
    }
    bool creature = dyn_cast<Creature>(&target) != nullptr;
    if (creature) {
        presentOn(game, services, kSparksMelee, target);
    } else {
        glm::vec3 point(lookAtPoint(target));
        presentAt(game, services, kSparksLarge, point, target.getFacing());
        presentAt(game, services, kSparksParry, point, target.getFacing());
    }
    if (auto table = services.resource.twoDas.get("damagehitvisual")) {
        for (int row : damageTypeVisuals(*table, damageFlags, /*ranged=*/false)) {
            if (creature) {
                presentOn(game, services, row, target);
            } else {
                presentAt(game, services, row, lookAtPoint(target), target.getFacing());
            }
        }
    }
}

} // namespace hitvisuals

} // namespace game

} // namespace reone
