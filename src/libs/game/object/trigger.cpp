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

#include "reone/game/object/trigger.h"

#include <algorithm>

#include "reone/game/di/services.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/node/model.h"
#include "reone/scene/node/trigger.h"
#include "reone/system/logutil.h"

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kDebugTestDuration = 0.25f;
static constexpr float kDebugInsideDuration = 0.25f;
static constexpr float kDebugEnterDuration = 1.5f;

static glm::vec4 debugColorForState(Trigger::DebugState state) {
    switch (state) {
    case Trigger::DebugState::Entered:
        return glm::vec4(1.0f, 0.42f, 0.12f, 0.95f);
    case Trigger::DebugState::Inside:
        return glm::vec4(0.16f, 0.95f, 0.38f, 0.95f);
    case Trigger::DebugState::Tested:
        return glm::vec4(1.0f, 0.88f, 0.18f, 0.95f);
    default:
        return glm::vec4(0.48f, 0.74f, 1.0f, 0.85f);
    }
}

void Trigger::loadFromBlueprint(const std::string &resRef) {
    std::shared_ptr<Gff> utt(_services.resource.gffs.get(resRef, ResType::Utt));
    if (!utt) {
        return;
    }
    deserialize(*utt, SerializedIdentityContext::templateResource(resRef));
}

void Trigger::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utt = _services.resource.gffs.get(templateRes, ResType::Utt)) {
            deserializeAll(*utt, SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);
    loadAppearance();
    updateTransform();
}

void Trigger::configureLinkedDoorTransition(const std::shared_ptr<Door> &door) {
    _linkedDoor = door;
    _linkedDoorTransition = true;
    _linkedToModule = door->linkedToModule();
    _linkedTo = door->linkedTo();
    _linkedToFlags = door->linkedToFlags();
    _transitionDestin = door->transitionDestination();
    _name = _transitionDestin.str();
    _geometry = door->linkedTransitionGeometry();

    setPosition(door->position());
    setFacing(door->getFacing());
    loadAppearance();
    updateTransform();
}

void Trigger::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    gff.readResRef(_onHeartbeat, "ScriptHeartbeat");
    gff.readResRef(_onEnter, "ScriptOnEnter");
    gff.readResRef(_onExit, "ScriptOnExit");
    gff.readResRef(_onClick, "OnClick");
    gff.readResRef(_onUserDefined, "ScriptUserDefine");
    gff.readResRef(_onTrapTriggered, "OnTrapTriggered");
    gff.readResRef(_onDisarm, "OnDisarm");
    gff.readByte(_trapType, "TrapType");
    gff.readBool(_trapOneShot, "TrapOneShot");
    if (gff.readString(_linkedTo, "LinkedTo")) {
        boost::to_lower(_linkedTo);
    }
    gff.readByte(_linkedToFlags, "LinkedToFlags");
    gff.readResRef(_linkedToModule, "LinkedToModule");
    gff.readBool(_autoRemoveKey, "AutoRemoveKey");
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }
    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }
    gff.readEnum(_faction, "Faction");
    gff.readString(_keyName, "KeyName");
    gff.readBool(_trapDisarmable, "TrapDisarmable");
    gff.readBool(_trapDetectable, "TrapDetectable");
    gff.readByte(_trapFlag, "TrapFlag");
    gff.readByte(_trapDetectDC, "TrapDetectDC");
    gff.readByte(_disarmDC, "DisarmDC");
    gff.readBool(_trapDetectedByParty, "TrapDetected");
    gff.readBool(_trapFlagged, "TrapFlagged");
    gff.readInt(_triggerType, "Type");
    gff.readByte(_cursor, "Cursor");
    gff.readFloat(_highlightHeight, "HighlightHeight");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    // Ignore XOrientation and YOrientation.
    //
    // The vanilla engine applies orientation only when instantiating
    // from a template, and it does not apply orientation when loading
    // from a savegame. It also uses script Facing only to the Trigger
    // object and not to the corresponding mesh.
    //
    // It appears that no script in the game relies on this behavior,
    // therefore we do not support it.

    gff.readWord(_loadScreenId, "LoadScreenID");
    gff.readLocString(_transitionDestin, "TransitionDestin", _services.resource.strings);
    gff.readBool(_setByPlayerParty, "SetByPlayerParty");
    gff.readBool(_commandable, "Commandable");

    auto geometry = gff.getList("Geometry");
    if (!geometry.empty()) {
        _geometry.clear();
        for (auto &point : geometry) {
            float x = point->getFloat("PointX");
            float y = point->getFloat("PointY");
            float z = point->getFloat("PointZ");
            _geometry.push_back(glm::vec3(x, y, z));
        }
    }

    // Not handled:
    // - CreatorId (IDs are not deserialized; a loaded mine keeps its faction)
    // - PortraitId
    // - VarTable
    // - SWVarTable
}

void Trigger::loadAppearance() {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    _sceneNode = sceneGraph.newTrigger(_geometry);
    if (!_sceneNode) {
        return;
    }
    _sceneNode->setLocalTransform(glm::translate(_position));
    syncDebugVisual();
    if (_trapDetectedByParty && isTrap()) {
        showTrapMarker();
    }
}

void Trigger::update(float dt) {
    Object::update(dt);

    _debugTestAge = glm::max(0.0f, _debugTestAge - dt);
    _debugInsideAge = glm::max(0.0f, _debugInsideAge - dt);
    _debugEnterAge = glm::max(0.0f, _debugEnterAge - dt);

    if (!isActive()) {
        _tenants.clear();
        syncDebugVisual();
        return;
    }

    std::vector<std::shared_ptr<Object>> tenantsToRemove;
    for (auto &tenant : _tenants) {
        if (tenant) {
            glm::vec2 position2d(tenant->position());
            if (isIn(position2d))
                continue;
        }
        tenantsToRemove.push_back(tenant);
    }
    // _tenants is ordered by address, which differs from run to run. Fire the
    // exit scripts in object id order so two runs agree on it.
    std::sort(tenantsToRemove.begin(), tenantsToRemove.end(), [](const auto &a, const auto &b) {
        return (a ? a->id() : 0u) < (b ? b->id() : 0u);
    });
    for (auto &tenant : tenantsToRemove) {
        _tenants.erase(tenant);

        if (_onExit.empty() || !tenant) {
            continue;
        }

        _game.scriptRunner().run(
            _onExit,
            {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
             {script::ArgKind::ExitingObject, script::Variable::ofObject(tenant->id())}});
    }

    syncDebugVisual();
}

void Trigger::addTenant(const std::shared_ptr<Object> &object) {
    _tenants.insert(object);
    syncDebugVisual();
    if (object && isTrap() && trapFiresOn(*object)) {
        if (partyHoldsTrapKey()) {
            // The trap's key, held by the party, disarms it instead.
            disarmTrap(object->id());
        } else {
            fireTrap(object->id());
        }
    }
    if (_onEnter.empty()) {
        return;
    }

    uint32_t entering = object->id();
    if (auto *coop = _game.coop()) {
        entering = coop->scriptActor(entering); // a player's creature counts as the PC
    }
    _game.scriptRunner().run(
        _onEnter,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::EnteringObject,
          script::Variable::ofObject(entering)}});
}

void Trigger::runClickScript(uint32_t clickerId) {
    if (_onClick.empty()) {
        return;
    }
    if (auto *coop = _game.coop()) {
        clickerId = coop->scriptActor(clickerId); // a player's creature counts as the PC
    }
    _game.scriptRunner().run(
        _onClick,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ClickingObject, script::Variable::ofObject(clickerId)},
         {script::ArgKind::EnteringObject, script::Variable::ofObject(clickerId)}});
}

void Trigger::removeTenant(const Object *object) {
    for (auto it = _tenants.begin(); it != _tenants.end();) {
        if (it->get() == object) {
            it = _tenants.erase(it);
            syncDebugVisual();
        } else {
            ++it;
        }
    }
}

bool Trigger::isIn(const glm::vec2 &point) const {
    auto sceneNode = std::static_pointer_cast<TriggerSceneNode>(_sceneNode);
    return sceneNode && sceneNode->isIn(point);
}

bool Trigger::isTenant(const std::shared_ptr<Object> &object) const {
    auto maybeTenant = find(_tenants.begin(), _tenants.end(), object);
    return maybeTenant != _tenants.end();
}

bool Trigger::isActive() const {
    if (!_linkedDoorTransition) {
        return true;
    }
    auto door = _linkedDoor.resolve();
    return door && door->isOpen();
}

bool Trigger::acceptsTransitionActivator(const std::shared_ptr<Object> &activator) const {
    if (_linkedToModule.empty() || !isActive()) {
        return false;
    }
    if (!activator) {
        return false;
    }
    // Only the player character or the current party leader moves the party
    // between modules; a following companion crossing a transition is ignored.
    // Taking control of a companion makes it the leader, so it keeps the
    // ability to transition and stays controlled in the destination.
    const Party &party = _game.party();
    return activator == party.actualPlayer() || activator == party.getLeader();
}

bool Trigger::detachLinkedDoorTransition(const Door &door) {
    auto linkedDoor = _linkedDoor.resolve();
    if (!_linkedDoorTransition || linkedDoor.get() != &door) {
        return false;
    }

    _linkedDoor.reset();
    _linkedToModule.clear();
    _linkedTo.clear();
    _tenants.clear();
    syncDebugVisual();
    return true;
}

Trigger::DebugState Trigger::debugState() const {
    if (_debugEnterAge > 0.0f) {
        return DebugState::Entered;
    }
    if (!_tenants.empty() || _debugInsideAge > 0.0f) {
        return DebugState::Inside;
    }
    if (_debugTestAge > 0.0f) {
        return DebugState::Tested;
    }
    return DebugState::Default;
}

glm::vec4 Trigger::debugColor() const {
    return debugColorForState(debugState());
}

void Trigger::markDebugTested(bool inside) {
    _debugTestAge = kDebugTestDuration;
    if (inside) {
        _debugInsideAge = kDebugInsideDuration;
    }
    syncDebugVisual();
}

void Trigger::markDebugEntered() {
    _debugEnterAge = kDebugEnterDuration;
    syncDebugVisual();
}

void Trigger::syncDebugVisual() {
    if (!_sceneNode) {
        return;
    }
    static_cast<TriggerSceneNode *>(_sceneNode.get())->setDebugColor(debugColor());
}

// Traps ----------------------------------------------------------------------

TrapFields Trigger::trapFields() const {
    TrapFields fields;
    fields.trapped = isTrap();
    fields.detectable = _trapDetectable;
    fields.disarmable = _trapDisarmable;
    fields.oneShot = _trapOneShot;
    fields.baseType = _trapType;
    fields.detectDC = _trapDetectDC;
    fields.disarmDC = _disarmDC;
    fields.keyTag = _keyName;
    return fields;
}

glm::vec3 Trigger::trapCenter() const {
    if (_geometry.empty()) {
        return _position;
    }
    glm::vec3 sum(0.0f);
    for (const auto &point : _geometry) {
        sum += point;
    }
    return _position + sum / static_cast<float>(_geometry.size());
}

bool Trigger::isTrapDetectedBy(const Object &creature) const {
    if (_trapDetectedBy.count(creature.id()) != 0) {
        return true;
    }
    return _trapDetectedByParty && _game.party().isMember(creature);
}

void Trigger::setTrapDetectedBy(uint32_t creatureId, bool byParty) {
    _trapDetectedBy.insert(creatureId);
    if (byParty && !_trapDetectedByParty) {
        _trapDetectedByParty = true;
        if (isTrap()) {
            showTrapMarker();
        }
    }
}

bool Trigger::trapFiresOn(const Object &object) const {
    auto creature = dyn_cast<Creature>(&object);
    if (!creature || creature->isDead()) {
        return false;
    }
    // A mine is hostile to whoever is hostile to its creator.
    Faction trapFaction = _faction;
    if (auto creator = _game.getObjectById(_creatorId)) {
        if (auto creatorCreature = dyn_cast<Creature>(creator.get())) {
            trapFaction = creatorCreature->faction();
        }
    }
    int reputation = _services.game.reputes.getReputation(creature->faction(), trapFaction);
    return game::trapFiresOn(reputation, trapFaction, creature->faction());
}

bool Trigger::partyHoldsTrapKey() const {
    if (_keyName.empty()) {
        return false;
    }
    const Party &party = _game.party();
    for (int i = 0; i < party.getSize(); ++i) {
        auto member = party.getMember(i);
        if (member && member->getItemByTag(_keyName)) {
            return true;
        }
    }
    return false;
}

void Trigger::fireTrap(uint32_t victimId) {
    if (!isTrap()) {
        return;
    }
    std::string script = _onTrapTriggered;
    if (script.empty()) {
        if (auto row = loadTrapRow(_services.resource.twoDas, _trapType)) {
            script = row->script;
        }
    }
    bool oneShot = _trapOneShot;
    if (oneShot) {
        // Off before the script runs, so the script cannot set it off again.
        _trapFlag = 0;
        removeTrapMarker();
    }
    if (!script.empty()) {
        _game.scriptRunner().run(
            script,
            {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
             {script::ArgKind::EnteringObject, script::Variable::ofObject(victimId)}});
    }
    if (oneShot) {
        removeTrapFromArea();
    }
}

void Trigger::disarmTrap(uint32_t disarmerId) {
    _lastDisarmedBy = disarmerId;
    _trapFlag = 0;
    removeTrapMarker();
    if (!_onDisarm.empty()) {
        _game.scriptRunner().run(
            _onDisarm,
            {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
    }
    removeTrapFromArea();
}

void Trigger::disableTrap() {
    _trapFlag = 0;
    removeTrapMarker();
    removeTrapFromArea();
}

void Trigger::removeTrapFromArea() {
    // A trap trigger that also carries OnEnter/OnExit scripts stays, disarmed.
    if (!_onEnter.empty() || !_onExit.empty() || !_onHeartbeat.empty()) {
        return;
    }
    if (auto module = _game.module()) {
        if (auto area = module->area()) {
            area->destroyObject(*this);
        }
    }
}

void Trigger::configureMine(int trapRow, const glm::vec3 &position, Faction faction,
                            uint32_t creatorId, int detectDC, int disarmDC) {
    _triggerType = kTriggerTypeTrap;
    _trapFlag = 1;
    _trapType = static_cast<uint8_t>(trapRow);
    _trapOneShot = true;
    _trapDetectable = true;
    _trapDisarmable = true;
    _trapDetectDC = static_cast<uint8_t>(glm::clamp(detectDC, 0, 255));
    _disarmDC = static_cast<uint8_t>(glm::clamp(disarmDC, 0, 255));
    _faction = faction;
    _creatorId = creatorId;
    if (auto creator = _game.getObjectById(creatorId)) {
        _setByPlayerParty = _game.party().isMember(*creator);
    }
    if (auto row = loadTrapRow(_services.resource.twoDas, trapRow)) {
        if (row->nameStrRef >= 0) {
            _name = _services.resource.strings.getText(row->nameStrRef);
        }
    }
    _geometry = {
        glm::vec3(-kMineHalfSize, -kMineHalfSize, 0.0f),
        glm::vec3(kMineHalfSize, -kMineHalfSize, 0.0f),
        glm::vec3(kMineHalfSize, kMineHalfSize, 0.0f),
        glm::vec3(-kMineHalfSize, kMineHalfSize, 0.0f)};
    _position = position;
    loadAppearance();
    updateTransform();
}

void Trigger::showTrapMarker() {
    if (_trapMarker) {
        return;
    }
    auto row = loadTrapRow(_services.resource.twoDas, _trapType);
    std::string modelName = row && !row->model.empty() ? row->model : "v_mnfrag";
    auto model = _services.resource.models.get(modelName);
    if (!model) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    _trapMarker = sceneGraph.newModel(*model, ModelUsage::Placeable);
    if (!_trapMarker) {
        return;
    }
    _trapMarker->setUser(*this);
    _trapMarker->setPickable(true);
    _trapMarker->setLocalTransform(glm::translate(trapCenter()));
    sceneGraph.addRoot(_trapMarker);
}

void Trigger::removeTrapMarker() {
    if (!_trapMarker) {
        return;
    }
    _services.scene.graphs.get(_sceneName).removeRoot(*_trapMarker);
    _trapMarker.reset();
}

int Trigger::cursorRow() const {
    switch (_triggerType) {
    case 1:
        return 1;
    case kTriggerTypeTrap:
        return 10;
    default:
        return _cursor;
    }
}

bool Trigger::isSelectable() const {
    return isTrap() && _trapDetectedByParty;
}

glm::vec3 Trigger::getSelectablePosition() const {
    if (_trapMarker) {
        return _trapMarker->getWorldCenterOfAABB();
    }
    return trapCenter();
}

} // namespace game

} // namespace reone
