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

#include "../action.h"
#include "../castspell.h"
#include "../object.h"
#include "../types.h"

namespace reone {

namespace game {
class Item;
class Spell;

class CastSpellAtObjectAction : public Action {
public:
    CastSpellAtObjectAction(
        Game &game,
        ServicesView &services,
        std::shared_ptr<Spell> spell,
        std::shared_ptr<Object> target,
        std::optional<std::shared_ptr<Item>> item,
        bool cheat = false);

    static bool classof(Action *from) {
        return from->type() == ActionType::CastSpellAtObject;
    }

    void execute(std::shared_ptr<Action> self, Object &actor, float dt) override;
    void cancel(std::shared_ptr<Action> self, Object &actor) override;
    void finish(Creature &caster);

    const std::shared_ptr<Spell> &spell() const { return _spell; }
    const std::optional<std::shared_ptr<Item>> &item() const { return _item; }
    const std::shared_ptr<Object> &target() const { return _target; }

    /**
     * C13 smarter companions only: the impact script runs as this object, not
     * the caster. The retail item scripts (k_sup_healing) heal and cure
     * OBJECT_SELF, so a medpac, repair kit or anti-toxin a companion gives to
     * an ally runs as the ally (design note c13_companions, "Ally items").
     */
    void setImpactCaller(std::shared_ptr<Object> caller) { _impactCaller = std::move(caller); }

private:
    std::shared_ptr<Spell> _spell;
    std::shared_ptr<Object> _target;
    std::shared_ptr<Object> _impactCaller;
    std::optional<std::shared_ptr<Item>> _item;
    SpellSchedule _schedule;
    std::optional<Grenade> _grenade;
    bool _cheat;
    // A single-use item whose last charge this cast spent. It has left the
    // inventory, but stays alive until the module's OnActivateItem script
    // has seen it (or the cast ends without taking effect).
    std::shared_ptr<Item> _consumedItem;

    void releaseConsumedItem();
    bool _targetRecorded {false};
    bool _inRange {false};
    // Force points (design note c03_stats, "Power cost"): a power the caster
    // cannot afford when the cast is taken up is dropped; the cost is paid
    // once, as the cast begins.
    bool _affordabilityChecked {false};
    bool _forcePaid {false};

    bool paysForcePoints() const { return !_cheat && !_item; }
};

} // namespace game

} // namespace reone
