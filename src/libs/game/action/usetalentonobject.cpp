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

#include "reone/game/action/usetalentonobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/talents.h"

namespace reone {

namespace game {

void UseTalentOnObjectAction::dispatchToAction() {
    assert(!_action && "dispatchToAction is called twice");
    switch (_chosenTalent->type()) {
    case TalentType::Feat: {
        _action = _game.newAction<UseFeatAction>(
            static_cast<FeatType>(_chosenTalent->value()),
            _target);
        break;
    }
    case TalentType::Spell: {
        auto spell = _services.game.spells.get(static_cast<SpellType>(_chosenTalent->value()));
        if (!spell) {
            return;
        }
        // A talent found on an item (GetCreatureTalentBest over medpacs,
        // droid utilities) casts through that item, consuming its charge.
        std::optional<std::shared_ptr<Item>> item;
        if (auto granting = _chosenTalent->item() == script::kObjectSelf
                                ? nullptr
                                : _game.getObjectById<Item>(_chosenTalent->item())) {
            item = std::move(granting);
        }
        _action = _game.newAction<CastSpellAtObjectAction>(
            std::move(spell), _target, std::move(item), /*cheat=*/false);
        break;
    }
    default:
        break;
    }
}

void UseTalentOnObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    if (!_itemResolved) {
        _itemResolved = true;
        // TalentSpell(n) names no item. When the actor does not know the
        // spell itself, cast it from an item it carries (k_inc_generic uses
        // TalentSpell(84) for the droid repair kit, TalentSpell(67) for the
        // anti-toxin).
        auto creature = dyn_cast<Creature>(&actor);
        auto cast = _action ? dyn_cast<CastSpellAtObjectAction>(_action.get()) : nullptr;
        if (creature && cast && !cast->item() &&
            !creature->attributes().hasSpell(cast->spell()->type)) {
            if (auto item = findItemGrantingSpell(*creature, cast->spell()->type)) {
                _action = _game.newAction<CastSpellAtObjectAction>(
                    cast->spell(), _target, std::optional<std::shared_ptr<Item>>(std::move(item)), /*cheat=*/false);
            }
        }
    }

    if (!_action) {
        complete();
        return;
    }

    _action->execute(_action, actor, dt);

    // An attack or cast in progress is locked, and so is the talent that
    // runs it: ClearAllActions from the AI's end-of-round script must not
    // drop it half-way (design note c09_ai_combat, "Talent actions").
    if (_action->locked()) {
        lock();
    }
    if (_action->isCompleted()) {
        complete();
    }
}

void UseTalentOnObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    if (!_action || _action->isCompleted() || _action->isCancelled()) {
        return;
    }
    _action->cancel(_action, actor);
    _action->markCancelled();
}

std::optional<SavedActionRecord> UseTalentOnObjectAction::saveFacingState() const {
    // ActionUseTalentOnObject is a runtime dispatcher. Physical feats already
    // have the canonical retail ActionId 12 representation on the dispatched
    // action; spells and unsupported talents continue to fail closed.
    return _action ? _action->saveFacingState() : std::nullopt;
}

} // namespace game

} // namespace reone
