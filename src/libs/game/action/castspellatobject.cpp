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

#include "reone/game/action/castspellatobject.h"
#include "reone/audio/mixer.h"
#include "reone/game/d20/forms.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/hitvisuals.h"
#include "reone/game/party.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"

namespace reone {

namespace game {

CastSpellAtObjectAction::CastSpellAtObjectAction(
    Game &game,
    ServicesView &services,
    std::shared_ptr<Spell> spell,
    std::shared_ptr<Object> target,
    std::optional<std::shared_ptr<Item>> item,
    bool cheat) :
    Action(game, services, ActionType::CastSpellAtObject),
    _spell(std::move(spell)),
    _target(std::move(target)),
    _item(std::move(item)),
    _schedule(_spell->conjTime, _spell->castTime),
    _cheat(cheat) {
    requireRuntimeObject(_target);
    if (_item) {
        requireRuntimeObject(*_item);
    }
}

CastSpellAtObjectAction::~CastSpellAtObjectAction() {
    hitvisuals::endCastHand(_castHand);
}

// The impact script runs as the caster: OBJECT_SELF is the caster and
// GetSpellTargetObject() is the target (k_inc_force, k_sup_grenade).
static void runScript(Game &game, const Spell &spell, const Object &caster, const Object &target) {
    if (spell.impactScript.empty()) {
        return;
    }

    auto location = std::make_shared<Location>(target.position(), target.getFacing());
    std::vector<script::Argument> args = {
        {script::ArgKind::Caller, script::Variable::ofObject(caster.id())},
        {script::ArgKind::SpellTargetObject, script::Variable::ofObject(target.id())},
        {script::ArgKind::SpellId, script::Variable::ofInt(static_cast<int>(spell.type))},
        {script::ArgKind::SpellLocation, script::Variable::ofLocation(std::move(location))},
    };
    game.scriptRunner().run(spell.impactScript, args);
}

void CastSpellAtObjectAction::execute(std::shared_ptr<Action> self, Object &actor, float dt) {
    Creature &caster = cast<Creature>(actor);

    // A K2 Force form (the K2 systems merge): using it makes it the active
    // form at once, with no cost and no cast; using the active form again
    // drops it (design note k2_merge, "Force forms").
    if (_spell->userType == forms::kFormUserType && forms::isForm(static_cast<int>(_spell->type))) {
        int form = static_cast<int>(_spell->type);
        if (caster.attributes().hasSpell(_spell->type)) {
            caster.setActiveForm(caster.activeForm() == form ? forms::kNoForm : form);
        }
        complete();
        return;
    }

    if (_target->isDead()) {
        finish(caster);
        return;
    }

    if (!_affordabilityChecked) {
        _affordabilityChecked = true;
        if (paysForcePoints() && !caster.canAffordForcePower(*_spell)) {
            finish(caster);
            return;
        }
    }

    // Close to the spell's range (spells.2da range via ranges.2da) before the
    // cast starts. Personal spells and spells on oneself need no approach.
    if (!_inRange) {
        float range = _spell->range;
        if (range > 0.0f && _target.get() != &caster && caster.getDistanceTo(*_target) > range) {
            if (caster.navigationFailed()) {
                finish(caster);
                return;
            }
            caster.navigateTo(_target->position(), true, range, dt);
            return;
        }
        _inRange = true;
        caster.setMovementType(Creature::MovementType::None);
    }
    if (!_forcePaid) {
        _forcePaid = true;
        if (paysForcePoints()) {
            caster.spendForcePoints(*_spell);
        }
    }
    caster.face(*_target);
    if (!_targetRecorded) {
        caster.setAttemptedSpellTarget(_target);
        _targetRecorded = true;
    }

    const CombatRound &round = _game.combat().addAction(self, actor);
    SpellSchedule::State state = _schedule.update(round, *self, dt);
    if (_grenade) {
        _grenade->update(state, *_target, dt);
    }

    // Gameplay updates
    switch (state) {
    case SpellSchedule::Conjure: {
        lock();

        bool lastItem = false;
        if (!_cheat && _item && _item.value()->activateSpellCost()) {
            bool removed = caster.removeItem(_item.value(), lastItem);
            if (!removed) {
                // A party member uses an item from the shared party
                // inventory, which the player character holds (K1 has one
                // party inventory): a companion the player controls, a
                // co-op client's companion or stand-in, a C13 companion.
                auto owner = _game.party().sharedItemHolder(caster);
                if (owner && _item.value()->owner() == owner->id()) {
                    removed = owner->removeItem(_item.value(), lastItem);
                }
            }
            if (!removed) {
                // Ran out of items since this action was enqueued. This may happen
                // in case of shared inventory (not implemented yet).
                //
                // Cheats ignore all requirements and do not remove the item.
                finish(caster);
                return;
            }
            if (lastItem) {
                _consumedItem = _item.value();
            }
        }

        caster.setMovementType(Creature::MovementType::None);
        caster.setMovementRestricted(true);

        scene::AnimationProperties animProp =
            scene::AnimationProperties::fromFlags(scene::AnimationFlags::blend);

        // A grenade is thrown, an item used, a Force power cast in the
        // pose of its spells.2da conjanim/castanim, held through the cast
        // time (design note v14_programmed_effects, section 9).
        CastAnimations anims = castActionAnimations(_spell->conjAnim, _spell->castAnim,
                                                    _item.has_value(), _spell->projModel != nullptr,
                                                    _spell->itemTargeting != 0);
        if (_spell->projModel) {
            _grenade = Grenade();
            _grenade->fire(caster, *_spell->projModel, /*swingTime=*/0.8f, /*throwTime=*/0.5f);
            caster.playAnimation(anims.conjure);
        } else {
            caster.playAnimation(anims.conjure, animProp);
            _castLoop = anims.cast;
        }
        return;
    }
    case SpellSchedule::Cast: {
        // The cast-hand visual (spells.2da casthandvisual) for the cast.
        if (!_castHand) {
            _castHand = hitvisuals::beginCastHand(_game, _services, static_cast<int>(_spell->type), caster);
        }
        if (!_castLoop.empty()) {
            caster.playAnimation(_castLoop, scene::AnimationProperties::fromFlags(scene::AnimationFlags::loopBlend));
        }
        if (_spell->castSound) {
            _services.audio.mixer.play(
                _spell->castSound,
                audio::AudioType::Sound,
                /*gain=*/1.0f,
                /*loop=*/false,
                caster.position());
        }
        return;
    }
    case SpellSchedule::Effect: {
        hitvisuals::endCastHand(_castHand);
        releaseCastPose(caster);
        caster.recordForcePowerUsed(_spell->type);
        // A cast excites the caster (excitedduration.2da row 2) and the
        // target (row 1): battle music, see Area::exciteCombat.
        if (auto module = _game.module(); module && module->area()) {
            module->area()->exciteCombat(caster, 2);
            if (auto target = dyn_cast<Creature>(_target)) {
                module->area()->exciteCombat(*target, 1);
            }
        }
        runScript(_game, *_spell, _impactCaller ? *_impactCaller : static_cast<const Object &>(caster), *_target);
        // Using an item's Activate Item property is an item activation: the
        // module hears of it once the spell has taken effect.
        if (_item) {
            if (auto module = _game.module()) {
                module->runOnActivateItem(*_item.value(), caster, *_target);
            }
        }
        releaseConsumedItem();
        break;
    }
    case SpellSchedule::Finish: {
        finish(caster);
        return;
    }
    default:
        break;
    }
}

void CastSpellAtObjectAction::cancel(std::shared_ptr<Action> self, Object &actor) {
    // A cancelled cast (e.g. its combat round was pruned) must release the
    // movement restriction it took at Conjure, like finishing does.
    finish(cast<Creature>(actor));
}

void CastSpellAtObjectAction::releaseCastPose(Creature &caster) {
    if (!_castLoop.empty()) {
        _castLoop.clear();
        caster.resumeStateDrivenAnimation();
    }
}

void CastSpellAtObjectAction::finish(Creature &caster) {
    hitvisuals::endCastHand(_castHand);
    releaseCastPose(caster);
    releaseConsumedItem();
    caster.setMovementRestricted(false);
    complete();
}

void CastSpellAtObjectAction::releaseConsumedItem() {
    if (_consumedItem) {
        auto item = std::move(_consumedItem);
        _consumedItem.reset();
        _game.destroyRuntimeObjectGraph(item);
    }
}

} // namespace game

} // namespace reone
