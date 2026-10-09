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

#include "reone/game/object/placeable.h"

#include "reone/game/object/creature.h"
#include "reone/game/reputes.h"

#include <algorithm>
#include <limits>

#include <boost/format.hpp>

#include "reone/game/combatsounds.h"
#include "reone/game/di/services.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"
#include "reone/game/script/runner.h"
#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/graphics/model.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

void Placeable::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utp(_services.resource.gffs.get(resRef, ResType::Utp));
    if (!utp) {
        return;
    }
    deserialize(*utp, SerializedIdentityContext::templateResource(resRef));
}

void Placeable::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    // Remember the template: the module snapshot writes the object over it,
    // so a revisit keeps what only the template holds (name, scripts).
    gff.readResRef(_blueprintResRef, "TemplateResRef");
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utp = _services.resource.gffs.get(templateRes, ResType::Utp)) {
            deserializeAll(*utp, SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    playStateAnimation();
    updateTransform();
}

void Placeable::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    gff.readEnum(_faction, "Faction");
    gff.readBool(_plot, "Plot");
    gff.readBool(_minOneHP, "Min1HP");
    gff.readByte(_openLockDC, "OpenLockDCk");
    gff.readString(_keyName, "KeyName");
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readByte(_disarmDC, "DisarmDC");
    gff.readByte(_trapDetectDC, "TrapDetectDC");
    gff.readByte(_trapFlag, "TrapFlag");
    gff.readBool(_trapOneShot, "TrapOneShot");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_usable, "Useable");
    gff.readBool(_static, "Static");
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readBool(_groundPile, "GroundPile");
    gff.readDword(_appearance, "Appearance");
    if (gff.readShort(_hitPoints, "HP")) {
        _maxHitPoints = _hitPoints;
    }
    gff.readShort(_currentHitPoints, "CurrentHP");
    gff.readByte(_hardness, "Hardness");
    if (identityContext.isSerializedState() && gff.has("CurrentHP")) {
        _dead = _currentHitPoints <= 0;
    }
    gff.readByte(_fort, "Fort");
    gff.readByte(_will, "Will");
    gff.readByte(_ref, "Ref");
    gff.readBool(_lockable, "Lockable");
    gff.readBool(_locked, "Locked");
    gff.readBool(_hasInventory, "HasInventory");
    gff.readBool(_keyRequired, "KeyRequired");
    gff.readByte(_closeLockDC, "CloseLockDC");
    gff.readBool(_open, "Open");
    uint8_t animationState = static_cast<uint8_t>(_animationState);
    if (gff.readByte(animationState, "AnimationState")) {
        _animationState = animationState;
    }
    gff.readBool(_partyInteract, "PartyInteract");
    gff.readWord(_portraitId, "PortraitId");
    gff.readResRef(_conversation, "Conversation");
    gff.readByte(_bodyBagId, "BodyBag");
    gff.readBool(_dieWhenEmpty, "DieWhenEmpty");
    gff.readByte(_lightState, "LightState");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readResRef(_onClosed, "OnClosed");
    gff.readResRef(_onDamaged, "OnDamaged");
    gff.readResRef(_onDeath, "OnDeath");
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onInvDisturbed, "OnInvDisturbed");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUsed, "OnUsed");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onDialog, "OnDialog");
    gff.readResRef(_onEndDialogue, "OnEndDialogue");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readInt(_animation, "Animation");
    {
        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = yawQuat(bearing);
        }
    }
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[2], "Z");
    gff.readBool(_isBodyBag, "IsBodyBag");
    gff.readBool(_isBodyBagVisible, "IsBodyBagVisible");
    gff.readBool(_isCorpse, "IsCorpse");
    gff.readBool(_commandable, "Commandable");

    deserializeOwnedItems(
        gff,
        identityContext,
        SaveRecordOriginKind::PlaceableItem,
        true);

    // FIXME: deserialize EffectList, ActionList
}

void Placeable::loadAppearance() {
    std::shared_ptr<TwoDA> placeables(_services.resource.twoDas.get("placeables"));
    std::string modelName(boost::to_lower_copy(placeables->getString(_appearance, "modelname")));

    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    auto sceneNode = sceneGraph.newModel(*model, ModelUsage::Placeable);
    sceneNode->setUser(*this);
    sceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    _sceneNode = std::move(sceneNode);

    auto walkmesh = _services.resource.walkmeshes.get(modelName, ResType::Pwk);
    if (walkmesh) {
        _walkmesh = sceneGraph.newWalkmesh(*walkmesh);
    }
}

const char *placeableStateAnimation(int animationState, bool open) {
    // utp AnimationState (K2 behaviour, design note c11_data): the looping
    // pose a placeable starts in. An open instance plays open.
    if (open) {
        return "open";
    }
    switch (animationState) {
    case 1:
        return "open";
    case 2:
        return "close";
    case 3:
        return "dead";
    case 4:
        return "on";
    case 5:
        return "off";
    default:
        return "default";
    }
}

void Placeable::playStateAnimation() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    std::string name(placeableStateAnimation(_animationState, _open));
    if (!model->model().getAnimation(name)) {
        return;
    }
    AnimationProperties properties;
    properties.flags = AnimationFlags::loop;
    model->playAnimation(name, nullptr, properties);
}

// utp AnimationState values (placeableStateAnimation).
static constexpr int kStateOpen = 1;
static constexpr int kStateClosed = 2;
static constexpr int kStateOn = 4;
static constexpr int kStateOff = 5;

void Placeable::changeAnimationState(int state, const char *transition) {
    bool changed = _animationState != state;
    _animationState = state;
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model || _dead) {
        return;
    }
    if (changed && transition && model->model().getAnimation(transition)) {
        _transitionAnimation = transition;
        model->playAnimation(_transitionAnimation, nullptr, AnimationProperties());
        return;
    }
    if (!changed && !_transitionAnimation.empty()) {
        return; // already on its way there
    }
    _transitionAnimation.clear();
    playStateAnimation();
}

void Placeable::openContainer(uint32_t opener) {
    // K1 and K2: the lid opens as the container is looted and stays open
    // after its screen closes.
    changeAnimationState(kStateOpen, "close2open");
    onOpen(opener);
}

struct PlaceableAnimationStep {
    int state;
    const char *transition;
    const char *loop;
};

static std::optional<PlaceableAnimationStep> placeableAnimationStep(AnimationType type) {
    switch (type) {
    case AnimationType::PlaceableOpen:
        return PlaceableAnimationStep {kStateOpen, "close2open", "open"};
    case AnimationType::PlaceableClose:
        return PlaceableAnimationStep {kStateClosed, "open2close", "close"};
    case AnimationType::PlaceableActivate:
        return PlaceableAnimationStep {kStateOn, "off2on", "on"};
    case AnimationType::PlaceableDeactivate:
        return PlaceableAnimationStep {kStateOff, "on2off", "off"};
    default:
        return std::nullopt;
    }
}

static std::string animloopName(AnimationType type) {
    int loop = static_cast<int>(type) - static_cast<int>(AnimationType::PlaceableAnimloop01);
    if (loop < 0 || loop > 9) {
        return "";
    }
    return str(boost::format("animloop%02d") % (loop + 1));
}

std::string Placeable::getAnimationName(AnimationType type) const {
    if (auto step = placeableAnimationStep(type)) {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model && model->model().getAnimation(step->transition)) {
            return step->transition;
        }
        return step->loop;
    }
    return animloopName(type);
}

void Placeable::playAnimation(AnimationType type, AnimationProperties properties) {
    if (auto step = placeableAnimationStep(type)) {
        changeAnimationState(step->state, step->transition);
        return;
    }
    std::string name(animloopName(type));
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!name.empty() && model && model->model().getAnimation(name)) {
        _transitionAnimation.clear();
        properties.flags |= AnimationFlags::loop;
        model->playAnimation(name, nullptr, properties);
    }
}

void Placeable::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
    if (_dead || _plot || _notBlastable) {
        return;
    }
    if (amount <= 0) {
        return;
    }

    setLastDamager(damager);
    uint32_t damagerId = getLastDamager();
    int currentHitPoints = _currentHitPoints > 0 ? _currentHitPoints : _hitPoints;
    if (amount == std::numeric_limits<int>::max()) {
        _currentHitPoints = isMinOneHP() ? 1 : 0;
    } else {
        int adjustedAmount = applyDamageToHitPoints(amount, currentHitPoints);
        _game.floatingText().addDamage(
            *this, amount, adjustedAmount, damagerId);
    }

    runDamagedScript();

    if (_currentHitPoints > 0) {
        return;
    }

    _dead = true;
    _locked = false;
    _open = true;
    _game.playSoundCue("placeable", combatsounds::objectSound(_services.resource.twoDas, *this, "destroyed"), _position);
    onOpen(damagerId);
    runDeathScript();
}

void Placeable::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    if (auto *coop = _game.coop()) {
        triggererId = coop->scriptActor(triggererId); // a player's creature counts as the PC
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)}});
}

Placeable::ClickAction Placeable::clickAction() const {
    if (_hasInventory) {
        if (!isLocked()) {
            return ClickAction::OpenContainer;
        }
        // A locked container with OnUsed is still used: its script answers
        // the click (tar03_box's k_ptar_puzzlebox starts the puzzle, the
        // spice crates' k_pebn_spice barks or talks).
        return _onUsed.empty() ? ClickAction::None : ClickAction::Use;
    }
    if (!_onUsed.empty()) {
        return ClickAction::Use;
    }
    if (!conversation().empty()) {
        return ClickAction::Converse;
    }
    return ClickAction::None;
}

void Placeable::runOnUsed(std::shared_ptr<Object> usedBy) {
    if (_onUsed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back(script::ArgKind::Caller, Variable::ofObject(_id));

    if (usedBy) {
        uint32_t user = usedBy->id();
        if (auto *coop = _game.coop()) {
            user = coop->scriptActor(user); // a player's creature counts as the PC
        }
        args.emplace_back(script::ArgKind::LastUsedBy, Variable::ofObject(user));
    }

    _game.scriptRunner().run(_onUsed, args);
}

void Placeable::runOnInvDisturbed(uint32_t triggerrer, InventoryDisturbType type, uint32_t item) {
    if (_onInvDisturbed.empty()) {
        return;
    }

    std::vector<script::Argument> args;
    args.emplace_back();

    // FIXME: implement LastDisturbed
    // triggerrer ? triggerrer->id() : kObjectInvalid

    _game.scriptRunner().run(
        _onInvDisturbed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastDisturbed, Variable::ofObject(triggerrer)},
         {script::ArgKind::InventoryDisturbItem, Variable::ofObject(item)},
         {script::ArgKind::InventoryDisturbType, Variable::ofInt(static_cast<int>(type))}});
}

void Placeable::signalClosed(uint32_t closer) {
    _open = false;
    _pendingScriptEvents.push_back({PendingScriptEvent::Kind::Closed, closer});
    // An emptied body bag removes itself.
    if (_dieWhenEmpty && _items.empty()) {
        if (auto module = _game.module(); module && module->area()) {
            module->area()->destroyObject(*this);
        }
    }
}

void Placeable::signalEndDialogue() {
    _pendingScriptEvents.push_back({PendingScriptEvent::Kind::EndDialogue, script::kObjectInvalid});
}

void Placeable::update(float dt) {
    Object::update(dt);
    if (!_transitionAnimation.empty()) {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (!model || model->isAnimationFinished()) {
            _transitionAnimation.clear();
            playStateAnimation();
        }
    }
    if (_pendingScriptEvents.empty()) {
        return;
    }
    if (!isRuntimeLive()) {
        _pendingScriptEvents.clear();
        return;
    }
    // Events raised by the scripts below wait for the next update.
    auto events = std::move(_pendingScriptEvents);
    _pendingScriptEvents.clear();
    for (const auto &event : events) {
        switch (event.kind) {
        case PendingScriptEvent::Kind::Closed:
            runClosedScript(event.actor);
            break;
        case PendingScriptEvent::Kind::EndDialogue:
            runEndDialogueScript();
            break;
        }
        if (!isRuntimeLive()) {
            return;
        }
    }
}

void Placeable::runLockScript(bool locked, uint32_t actor) {
    noteLockChange(locked, actor);
    const std::string &script = locked ? _onLock : _onUnlock;
    if (script.empty()) {
        return;
    }
    _game.scriptRunner().run(script, _id);
}

void Placeable::runClosedScript(uint32_t closer) {
    if (_onClosed.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClosed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastClosedBy, Variable::ofObject(closer)}});
}

void Placeable::runEndDialogueScript() {
    if (_onEndDialogue.empty()) {
        return;
    }
    _game.scriptRunner().run(_onEndDialogue, _id);
}

void Placeable::runMeleeAttackedScript(uint32_t attackerId) {
    if (_onMeleeAttacked.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onMeleeAttacked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(attackerId)}});
}

void Placeable::runDamagedScript() {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Placeable::runDeathScript() {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Placeable::updateTransform() {
    Object::updateTransform();

    if (_walkmesh) {
        _walkmesh->setLocalTransform(_transform);
    }
}

bool canBashPlaceable(const Placeable &placeable, const Creature &actor, const IReputes &reputes) {
    return placeable.hasInventory() &&
           placeable.isLocked() &&
           placeable.isSelectable() &&
           !placeable.isDead() &&
           !placeable.plotFlag() &&
           !placeable.isNotBlastable() &&
           reputes.getIsEnemy(actor.reputationFaction(), placeable.faction()) &&
           (placeable.hitPoints() > 0 || placeable.currentHitPoints() > 0);
}

} // namespace game

} // namespace reone
