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

#include "reone/game/object/door.h"

#include <algorithm>
#include <limits>

#include "reone/graphics/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/model.h"
#include "reone/scene/types.h"

#include "reone/game/combatsounds.h"
#include "reone/game/di/services.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

// How long a leader's try at a locked transition door stays good for: long
// enough for a fail-to-open script to open the door (next update, plus the
// opening animation), short enough that a much later opening does not move
// the party unasked.
static constexpr float kPendingTransitionUseSeconds = 5.0f;

void Door::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    // Remember the template: the module snapshot writes the object over it,
    // so a revisit keeps what only the template holds (name, scripts).
    gff.readResRef(_blueprintResRef, "TemplateResRef");
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utd = _services.resource.gffs.get(templateRes, ResType::Utd)) {
            deserializeAll(*utd, SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    applyRestingState();
    updateTransform();
}

void Door::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readDword(_appearance, "Appearance");
    gff.readByte(_genericType, "GenericType");
    {
        // OpenState names the resting state the door was authored in. Every K1
        // and K2 blueprint that sets it sets AnimationState to the same value,
        // so OpenState on its own is authoritative.
        uint8_t openState;
        if (gff.readByte(openState, "OpenState")) {
            switch (openState) {
            case 0:
                _state = DoorState::Closed;
                break;
            case 1:
                _state = DoorState::Opened1;
                break;
            case 2:
                _state = DoorState::Opened2;
                break;
            default:
                warn(str(boost::format("Door: unsupported OpenState %d, treating as closed") % static_cast<int>(openState)));
                _state = DoorState::Closed;
                break;
            }
        }
    }
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    {
        float bearing;
        if (gff.readFloat(bearing, "Bearing")) {
            _orientation = yawQuat(bearing);
        }
    }
    gff.readFloat(_position[0], "X");
    gff.readFloat(_position[1], "Y");
    gff.readFloat(_position[2], "Z");
    gff.readEnum(_faction, "Faction");
    gff.readByte(_fort, "Fort");
    gff.readByte(_will, "Will");
    gff.readByte(_ref, "Ref");
    if (gff.readShort(_hitPoints, "HP")) {
        _maxHitPoints = _hitPoints;
    }
    gff.readShort(_currentHitPoints, "CurrentHP");
    if (identityContext.isSerializedState() && gff.has("CurrentHP")) {
        _dead = _currentHitPoints <= 0;
    }
    gff.readBool(_plot, "Plot");
    gff.readBool(_minOneHP, "Min1HP");
    if (gff.readString(_keyName, "KeyName")) {
        boost::to_lower(_keyName);
    }
    gff.readBool(_keyRequired, "KeyRequired");
    gff.readByte(_openLockDC, "OpenLockDC");
    gff.readByte(_closeLockDC, "CloseLockDC");
    gff.readByte(_secretDoorDC, "SecretDoorDC");
    gff.readResRef(_conversation, "Conversation");
    gff.readWord(_portraitId, "PortraitId");
    gff.readByte(_hardness, "Hardness");
    gff.readResRef(_onClosed, "OnClosed");
    gff.readResRef(_onDamaged, "OnDamaged");
    gff.readResRef(_onDeath, "OnDeath");
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onLock, "OnLock");
    gff.readResRef(_onMeleeAttacked, "OnMeleeAttacked");
    gff.readResRef(_onOpen, "OnOpen");
    gff.readResRef(_onSpellCastAt, "OnSpellCastAt");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readResRef(_onUnlock, "OnUnlock");
    gff.readResRef(_onUserDefined, "OnUserDefined");
    gff.readResRef(_onClick, "OnClick");
    gff.readResRef(_onFailToOpen, "OnFailToOpen");
    gff.readResRef(_onDialog, "OnDialog");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readByte(_disarmDC, "DisarmDC");
    gff.readByte(_trapDetectDC, "TrapDetectDC");
    gff.readByte(_trapFlag, "TrapFlag");
    gff.readBool(_trapOneShot, "TrapOneShot");
    gff.readBool(_locked, "Locked");
    gff.readBool(_lockable, "Lockable");
    gff.readByte(_linkedToFlags, "LinkedToFlags");
    gff.readString(_linkedTo, "LinkedTo");
    gff.readResRef(_linkedToModule, "LinkedToModule");
    gff.readWord(_loadScreenId, "LoadScreenID");
    gff.readLocString(_description, "Description", _services.resource.strings);
    gff.readBool(_static, "Static");
    gff.readBool(_notBlastable, "NotBlastable");
    gff.readLocString(_transitionDestin, "TransitionDestin", _services.resource.strings);

    // FIXME: deserialize EffectList, ActionList
}

void Door::loadAppearance() {
    std::shared_ptr<TwoDA> doors(_services.resource.twoDas.get("genericdoors"));
    std::string modelName(boost::to_lower_copy(doors->getString(_genericType, "modelname")));

    _linkedTransitionGeometry.clear();
    _usePositions.clear();
    auto walkmeshClosed = _services.resource.walkmeshes.get(modelName + "0", ResType::Dwk);
    if (walkmeshClosed) {
        loadLinkedTransitionGeometry(*walkmeshClosed);
        _usePositions = walkmeshClosed->usePositions;
    }

    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    auto modelSceneNode = sceneGraph.newModel(*model, ModelUsage::Door);
    modelSceneNode->setUser(*this);
    // modelSceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    _sceneNode = std::move(modelSceneNode);

    if (walkmeshClosed) {
        _walkmeshClosed = sceneGraph.newWalkmesh(*walkmeshClosed);
        _walkmeshClosed->setUser(*this);
    }

    auto walkmeshOpen1 = _services.resource.walkmeshes.get(modelName + "1", ResType::Dwk);
    if (walkmeshOpen1) {
        _walkmeshOpen1 = sceneGraph.newWalkmesh(*walkmeshOpen1);
        _walkmeshOpen1->setUser(*this);
        _walkmeshOpen1->setEnabled(false);
    }

    auto walkmeshOpen2 = _services.resource.walkmeshes.get(modelName + "2", ResType::Dwk);
    if (walkmeshOpen2) {
        _walkmeshOpen2 = sceneGraph.newWalkmesh(*walkmeshOpen2);
        _walkmeshOpen2->setUser(*this);
        _walkmeshOpen2->setEnabled(false);
    }

    // A door authored open must load directly into its opened pose, with the
    // matching walkmesh, rather than resting shut and playing an opening
    // transition it already finished before the module was entered.
    applyRestingState();
}

void Door::applyRestingState() {
    _transition = DoorTransition::None;
    _open = _state != DoorState::Closed;
    enableStateWalkmeshes();
    applyRestingPose();
}

void Door::enableStateWalkmeshes() {
    if (_walkmeshClosed) {
        _walkmeshClosed->setEnabled(_state == DoorState::Closed);
    }
    if (_walkmeshOpen1) {
        _walkmeshOpen1->setEnabled(_state == DoorState::Opened1);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setEnabled(_state == DoorState::Opened2);
    }
}

void Door::applyRestingPose() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    // Every K1 and K2 door model carries these as zero-length pose animations.
    // They finish on the update they are played, and a finished non-fire-forget
    // channel is retained, so the pose holds.
    switch (_state) {
    case DoorState::Opened1:
        model->playAnimation("opened1");
        break;
    case DoorState::Opened2:
        model->playAnimation("opened2");
        break;
    case DoorState::Closed:
    default:
        model->playAnimation("closed");
        break;
    }
}

void Door::loadLinkedTransitionGeometry(const Walkmesh &walkmesh) {
    AABB bounds;
    for (const auto &face : walkmesh.faces) {
        for (const auto &vertex : face.indices) {
            bounds.expand(walkmesh.vertices[vertex]);
        }
    }
    if (bounds.isDegenerate() || bounds.min().x == bounds.max().x || bounds.min().y == bounds.max().y) {
        return;
    }

    float z = bounds.min().z;
    _linkedTransitionGeometry = {
        glm::vec3(bounds.min().x, bounds.min().y, z),
        glm::vec3(bounds.min().x, bounds.max().y, z),
        glm::vec3(bounds.max().x, bounds.max().y, z),
        glm::vec3(bounds.max().x, bounds.min().y, z)};
}

bool Door::isSelectable() const {
    // A door already swinging open is on its way out of reach even though it is
    // not open yet, and offering it again would only re-run OnOpen.
    // An open area exit stays selectable (SWEEP_FINDINGS F4): a click walks
    // the leader through it rather than leaving the doorway trigger as the
    // only way out.
    if (!_static && _open && isAreaExit()) {
        return true;
    }
    return !_static && !_open && !isOpening();
}

void Door::damage(
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
    open();
    onOpen(damagerId);
    runDeathScript();
}

void Door::open() {
    if (isOpening()) {
        // Already swinging open. Restarting would rewind the leaf and re-run
        // whatever the caller does around the transition.
        return;
    }
    if (_state != DoorState::Closed && _transition == DoorTransition::None) {
        return;
    }

    _game.playSoundCue("door", combatsounds::objectSound(_services.resource.twoDas, *this, _dead ? "destroyed" : "opened"), _position);

    // A door that is swinging aside is still in the doorway. Retail K2 will not
    // let the player through one until the opening animation has finished, so
    // the closed walkmesh stays enabled for the whole transition and only comes
    // off when the door has physically arrived at its opened pose.
    beginTransition(DoorTransition::Opening);
}

void Door::close() {
    if (isClosing()) {
        return;
    }
    if (_state == DoorState::Closed && _transition == DoorTransition::None) {
        // Already shut and blocking. Closing again must not replay the
        // transition, which would briefly reopen the doorway to collision.
        return;
    }

    _game.playSoundCue("door", combatsounds::objectSound(_services.resource.twoDas, *this, "closed"), _position);

    // The mirror image: a door that is swinging shut has not sealed the doorway
    // yet. Scripts routinely close a door behind somebody who is still walking
    // through it - K2 103PER closes TO102PER on the same beat that sends the
    // player through it, and DOR_PER03 takes 2.667 seconds to shut - so the
    // doorway stays passable until the door reaches its closed pose.
    beginTransition(DoorTransition::Closing);
}

void Door::snapClosed() {
    _state = DoorState::Closed;
    applyRestingState();
}

void Door::beginTransition(DoorTransition transition) {
    // _state is left alone. It names where the door still physically stands,
    // and everything that can obstruct or admit a creature is read off it, so
    // the door keeps the collision of the state it is leaving until it arrives.
    _transition = transition;

    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (model) {
        model->playAnimation(transitionAnimation(transition));
    }
    if (isTransitionComplete()) {
        // No model, or no such animation on it: there is nothing to wait for,
        // so the door arrives immediately rather than being stranded in a
        // transition that can never complete.
        finishTransition();
    }
}

void Door::finishTransition() {
    _state = transitionTarget(_transition);
    applyRestingState();
}

void Door::update(float dt) {
    Object::update(dt);

    if (_transition != DoorTransition::None && isTransitionComplete()) {
        finishTransition();
    }
    updatePendingTransitionUse(dt);
}

void Door::setPendingTransitionUse(uint32_t userId) {
    _pendingTransitionUser = userId;
    _pendingTransitionTimeout = kPendingTransitionUseSeconds;
}

void Door::updatePendingTransitionUse(float dt) {
    if (!_pendingTransitionUser) {
        return;
    }
    _pendingTransitionTimeout -= dt;
    if (isClosing() || _pendingTransitionTimeout <= 0.0f || _linkedToModule.empty()) {
        _pendingTransitionUser = 0;
        return;
    }
    if (!isOpen() || isOpening()) {
        return;
    }
    uint32_t userId = _pendingTransitionUser;
    _pendingTransitionUser = 0;
    auto module = _game.module();
    auto self = _game.getObjectById<Door>(_id);
    auto user = _game.getObjectById<Creature>(userId);
    if (module && self && user && user == _game.party().getLeader()) {
        module->useTransitionDoor(self, user);
    }
}

bool Door::isTransitionComplete() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return true;
    }
    // Something other than this transition owns the model - a reversal, or an
    // animation the door never had - so there is nothing left to wait for.
    if (model->activeAnimationName() != transitionAnimation(_transition)) {
        return true;
    }
    return model->isAnimationFinished();
}

const char *Door::transitionAnimation(DoorTransition transition) {
    return transition == DoorTransition::Opening ? "opening1" : "closing1";
}

DoorState Door::transitionTarget(DoorTransition transition) {
    return transition == DoorTransition::Opening ? DoorState::Opened1 : DoorState::Closed;
}

void Door::onOpen(uint32_t triggererId) {
    if (_onOpen.empty()) {
        return;
    }
    if (auto *coop = _game.coop()) {
        triggererId = coop->scriptActor(triggererId); // a player's creature counts as the PC
    }
    _game.scriptRunner().run(
        _onOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastOpenedBy, Variable::ofObject(triggererId)},
         {script::ArgKind::ClickingObject, Variable::ofObject(triggererId)},
         {script::ArgKind::EnteringObject, Variable::ofObject(triggererId)}});
}

void Door::onFailToOpen(const Object &triggerer) {
    if (_locked) {
        _game.playSoundCue("door", combatsounds::objectSound(_services.resource.twoDas, *this, "locked"), _position);
    }
    if (_onFailToOpen.empty()) {
        return;
    }

    uint32_t clicker = triggerer.id();
    if (auto *coop = _game.coop()) {
        clicker = coop->scriptActor(clicker);
    }
    _game.scriptRunner().run(
        _onFailToOpen,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, Variable::ofObject(clicker)}});
}

void Door::runLockScript(bool locked, uint32_t actor) {
    noteLockChange(locked, actor);
    const std::string &script = locked ? _onLock : _onUnlock;
    if (script.empty()) {
        return;
    }
    _game.scriptRunner().run(script, _id);
}

void Door::runClosedScript(uint32_t closerId) {
    if (_onClosed.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onClosed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastClosedBy, Variable::ofObject(closerId)}});
}

void Door::runClickScript(uint32_t clickerId) {
    if (_onClick.empty()) {
        return;
    }
    if (auto *coop = _game.coop()) {
        clickerId = coop->scriptActor(clickerId); // a player's creature counts as the PC
    }
    _game.scriptRunner().run(
        _onClick,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, Variable::ofObject(clickerId)},
         {script::ArgKind::EnteringObject, Variable::ofObject(clickerId)}});
}

void Door::runMeleeAttackedScript(uint32_t attackerId) {
    if (_onMeleeAttacked.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onMeleeAttacked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(attackerId)}});
}

void Door::runDamagedScript() {
    if (_onDamaged.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Door::runDeathScript() {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

void Door::setLocked(bool locked) {
    _locked = locked;
}

std::vector<glm::vec3> Door::useWorldPositions() const {
    std::vector<glm::vec3> result;
    result.reserve(_usePositions.size());
    for (const auto &point : _usePositions) {
        result.push_back(glm::vec3(_transform * glm::vec4(point, 1.0f)));
    }
    return result;
}

bool canBashDoor(const Door &door, const Creature &actor, const IReputes &reputes) {
    return door.isLocked() &&
           !door.isOpen() &&
           door.isSelectable() &&
           !door.isDead() &&
           !door.plotFlag() &&
           !door.isNotBlastable() &&
           reputes.getIsEnemy(actor.reputationFaction(), door.faction()) &&
           (door.hitPoints() > 0 || door.currentHitPoints() > 0);
}

void Door::updateTransform() {
    Object::updateTransform();

    if (_walkmeshOpen1) {
        _walkmeshOpen1->setLocalTransform(_transform);
    }
    if (_walkmeshOpen2) {
        _walkmeshOpen2->setLocalTransform(_transform);
    }
    if (_walkmeshClosed) {
        _walkmeshClosed->setLocalTransform(_transform);
    }
}

} // namespace game

} // namespace reone
