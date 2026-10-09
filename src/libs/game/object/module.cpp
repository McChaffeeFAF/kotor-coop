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

#include "reone/system/portablemath.h"
#include "reone/game/coopsession.h"
#include "reone/game/scripteventqueue.h"
#include "reone/game/object/module.h"

#include "reone/game/action/interactobject.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/trap.h"
#include "reone/game/object/trigger.h"
#include "reone/game/d20/feats.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/globalfade.h"
#include "reone/game/party.h"
#include "reone/game/script/savedsituation.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"

#include "../action/commonactions.h"

#include <algorithm>
#include <boost/algorithm/string/case_conv.hpp>

using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static bool canUseSecurityOnPlaceable(const Placeable &placeable, const Creature &actor) {
    return placeable.hasInventory() &&
           placeable.isLocked() &&
           !placeable.isKeyRequired() &&
           actor.attributes().hasSkill(SkillType::Security);
}

void Module::load(std::string name, const Gff &ifo, bool restoreSavedWorld) {
    auto parsed = resource::generated::parseIFO(ifo);
    if (parsed.Mod_Entry_Area.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }
    auto are = _services.resource.gffs.get(parsed.Mod_Entry_Area, ResType::Are);
    if (!are) {
        throw ResourceNotFoundException(
            "Area ARE not found: " + parsed.Mod_Entry_Area);
    }
    auto git = _services.resource.gffs.get(parsed.Mod_Entry_Area, ResType::Git);
    if (!git) {
        throw ResourceNotFoundException(
            "Area GIT not found: " + parsed.Mod_Entry_Area);
    }
    load(std::move(name), ifo, *are, *git, restoreSavedWorld);
    if (!restoreSavedWorld) {
        runSpawnScripts();
    }
}

void Module::load(
    std::string name,
    const Gff &ifo,
    const Gff &are,
    const Gff &git,
    bool restoreSavedWorld) {
    _name = std::move(name);
    if (restoreSavedWorld) {
        _game.captureSaveResourceShadow(
            {SaveResourceKind::ModuleIfo, _name}, ifo);
    }
    _publishedSavedEvents.clear();
    _savedEventsPublished = false;

    auto ifoParsed = resource::generated::parseIFO(ifo);
    _isSaveGame = restoreSavedWorld && ifoParsed.Mod_IsSaveGame != 0;
    if (restoreSavedWorld) {
        const auto identityContext =
            SerializedIdentityContext::moduleGraph(_name);
        deserializeRuntimeState(ifo, identityContext);
        deserializeSavedEventQueue(ifo, identityContext);
        loadLimboCreatures(ifo);
    } else {
        _savedEventQueue = SavedEventQueue {};
        _savedEventLive.clear();
    }
    loadInfo(ifoParsed);
    loadArea(ifoParsed, are, git, restoreSavedWorld);

    _area->initCameras(_info.entryPosition, _info.entryFacing);

    loadPlayer();

}

void Module::activate() {
    _area->activate();
}

void Module::loadInfo(const resource::generated::IFO &ifo) {
    // Mod_Name is a localized string: KotOR II modules carry a talk table
    // reference, while KotOR modules usually carry the text inline. LocString
    // resolves whichever form is present, and yields an empty string when
    // neither is.
    _localizedName = LocString(
                         ifo.Mod_Name.first,
                         ifo.Mod_Name.second,
                         _services.resource.strings)
                         .str();

    // Entry location

    _info.entryArea = ifo.Mod_Entry_Area;
    if (_info.entryArea.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }

    _info.entryPosition.x = ifo.Mod_Entry_X;
    _info.entryPosition.y = ifo.Mod_Entry_Y;
    _info.entryPosition.z = ifo.Mod_Entry_Z;

    float dirX = ifo.Mod_Entry_Dir_X;
    float dirY = ifo.Mod_Entry_Dir_Y;
    _info.entryFacing = -portable::atan2f(dirX, dirY);

    _info.onModLoad = boost::to_lower_copy(ifo.Mod_OnModLoad);
    _info.onModStart = boost::to_lower_copy(ifo.Mod_OnModStart);
    _info.onActivateItem = boost::to_lower_copy(ifo.Mod_OnActvtItem);
    _info.onAcquireItem = boost::to_lower_copy(ifo.Mod_OnAcquirItem);
    _info.onUnacquireItem = boost::to_lower_copy(ifo.Mod_OnUnAqreItem);
    _info.onClientEnter = boost::to_lower_copy(ifo.Mod_OnClientEntr);
    _info.onClientLeave = boost::to_lower_copy(ifo.Mod_OnClientLeav);
    _info.onHeartbeat = boost::to_lower_copy(ifo.Mod_OnHeartbeat);
    _info.onPlayerDeath = boost::to_lower_copy(ifo.Mod_OnPlrDeath);
    _info.onPlayerDying = boost::to_lower_copy(ifo.Mod_OnPlrDying);
    _info.onRespawnButton = boost::to_lower_copy(ifo.Mod_OnSpawnBtnDn);
    _info.onPlayerRest = boost::to_lower_copy(ifo.Mod_OnPlrRest);
    _info.xpScale = ifo.Mod_XPScale;
    _info.minutesPerHour = ifo.Mod_MinPerHour;
    _info.startYear = ifo.Mod_StartYear;
    _info.startMonth = ifo.Mod_StartMonth;
    _info.startDay = ifo.Mod_StartDay;
    _info.startHour = ifo.Mod_StartHour;
    _info.dawnHour = ifo.Mod_DawnHour;
    _info.duskHour = ifo.Mod_DuskHour;
}

void Module::runOnActivateItem(const Object &item, const Object &activator, const Object &target) {
    _itemActivation.item = item.id();
    _itemActivation.activator = activator.id();
    _itemActivation.target = target.id();
    _itemActivation.targetPosition = target.position();
    // The target location faces along the line from the activator to the
    // target; the activator's own facing when the two coincide.
    glm::vec2 delta(target.position() - activator.position());
    _itemActivation.targetFacing = glm::length(delta) > 0.0f
                                       ? -portable::atan2f(delta.x, delta.y)
                                       : activator.getFacing();
    if (_info.onActivateItem.empty()) {
        return;
    }
    // GetItemActivated/GetItemActivator read the event's own arguments; the
    // target and its location come from the record above.
    _game.scriptRunner().run(
        _info.onActivateItem,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ItemActivated, script::Variable::ofObject(item.id())},
         {script::ArgKind::ItemActivator, script::Variable::ofObject(activator.id())}});
}

// Module events -------------------------------------------------------------

// Time from the last party member falling to the end of the game, in seconds
// of simulation time. The screen fades to black over it.
static constexpr float kDefeatDelay = 3.0f;

// A fallen party member does not get up while a hostile creature this close to
// a standing member is still alive.
static constexpr float kRecoveryHostileRange = 30.0f;

void Module::raisePlayerDeath(const Creature &creature) {
    // Dying first, then the death. K1 has no bleeding-out phase, so the two
    // are raised together.
    _moduleEvents.push_back({ModuleEvent::Kind::PlayerDying, creature.id(), script::kObjectInvalid});
    _moduleEvents.push_back({ModuleEvent::Kind::PlayerDeath, creature.id(), script::kObjectInvalid});
}

void Module::raiseRespawnButton(const Object &presser) {
    _moduleEvents.push_back({ModuleEvent::Kind::RespawnButton, presser.id(), script::kObjectInvalid});
}

void Module::raiseItemAcquired(const Object &item, uint32_t from) {
    _moduleEvents.push_back({ModuleEvent::Kind::ItemAcquired, item.id(), from});
}

void Module::raiseItemLost(const Object &item, uint32_t lostBy) {
    _moduleEvents.push_back({ModuleEvent::Kind::ItemLost, item.id(), lostBy});
}

void Module::raisePlayerRest(const Object &creature) {
    _moduleEvents.push_back({ModuleEvent::Kind::PlayerRest, creature.id(), script::kObjectInvalid});
}

void Module::runOnClientEnterScript(const Object &entering) {
    if (_info.onClientEnter.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _info.onClientEnter,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::EnteringObject, script::Variable::ofObject(entering.id())}});
}

void Module::runOnClientLeaveScript(const Object &exiting) {
    if (_info.onClientLeave.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _info.onClientLeave,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ExitingObject, script::Variable::ofObject(exiting.id())}});
}

void Module::updateEvents(float dt) {
    _heartbeatElapsed += dt;
    if (_heartbeatElapsed >= kHeartbeatInterval) {
        _heartbeatElapsed -= kHeartbeatInterval;
        runHeartbeatScript();
        if (_game.module().get() != this) {
            return;
        }
    }

    // Events raised by the scripts run below wait for the next tick.
    auto events = std::move(_moduleEvents);
    _moduleEvents.clear();
    for (const auto &event : events) {
        dispatchModuleEvent(event);
        if (_game.module().get() != this) {
            // A script ended the module (a transition or the end of the game).
            return;
        }
    }

    updatePartyVitality(dt);
}

void Module::dispatchModuleEvent(const ModuleEvent &event) {
    switch (event.kind) {
    case ModuleEvent::Kind::PlayerDying:
        _eventRecord.lastPlayerDying = event.object;
        runPlayerDyingScript(event.object);
        break;
    case ModuleEvent::Kind::PlayerDeath:
        _eventRecord.lastPlayerDied = event.object;
        runPlayerDeathScript(event.object);
        break;
    case ModuleEvent::Kind::RespawnButton:
        _eventRecord.lastRespawnButtonPresser = event.object;
        runRespawnButtonScript(event.object);
        break;
    case ModuleEvent::Kind::ItemAcquired:
        _eventRecord.itemAcquired = event.object;
        _eventRecord.itemAcquiredFrom = event.other;
        runAcquireItemScript(event.object, event.other);
        break;
    case ModuleEvent::Kind::ItemLost:
        _eventRecord.itemLost = event.object;
        _eventRecord.itemLostBy = event.other;
        runUnacquireItemScript(event.object, event.other);
        break;
    case ModuleEvent::Kind::PlayerRest:
        runPlayerRestScript(event.object);
        break;
    }
}

void Module::runHeartbeatScript() {
    if (_info.onHeartbeat.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onHeartbeat, _id);
}

// The event's objects are on the module's event record, which the Get*
// routines read; the scripts run as the module.

void Module::runPlayerDyingScript(uint32_t creature) {
    if (_info.onPlayerDying.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onPlayerDying, _id);
}

void Module::runPlayerDeathScript(uint32_t creature) {
    if (_info.onPlayerDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onPlayerDeath, _id);
}

void Module::runRespawnButtonScript(uint32_t presser) {
    if (_info.onRespawnButton.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onRespawnButton, _id);
}

void Module::runAcquireItemScript(uint32_t item, uint32_t from) {
    if (_info.onAcquireItem.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onAcquireItem, _id);
}

void Module::runUnacquireItemScript(uint32_t item, uint32_t lostBy) {
    if (_info.onUnacquireItem.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onUnacquireItem, _id);
}

void Module::runPlayerRestScript(uint32_t creature) {
    if (_info.onPlayerRest.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onPlayerRest, _id);
}

void Module::updatePartyVitality(float dt) {
    Party &party = _game.party();
    std::vector<std::shared_ptr<Creature>> standing;
    std::vector<std::shared_ptr<Creature>> fallen;
    bool inCombat = false;
    for (int i = 0; i < party.getSize(); ++i) {
        auto member = party.getMember(i);
        if (!member) {
            continue;
        }
        if (member->isDead()) {
            fallen.push_back(member);
        } else {
            standing.push_back(member);
            inCombat = inCombat || member->isInCombat();
        }
    }
    if (standing.empty() && fallen.empty()) {
        return;
    }

    // Everyone down: fade out, press respawn for the player, then end.
    if (standing.empty()) {
        switch (_defeat) {
        case PartyDefeatState::None:
            _defeat = PartyDefeatState::Fading;
            _defeatElapsed = 0.0f;
            reone::info("Module: the party is defeated");
            _game.globalFade().request(GlobalFade::Direction::Out, 0.0f, kDefeatDelay);
            break;
        case PartyDefeatState::Fading:
            _defeatElapsed += dt;
            if (_defeatElapsed >= kDefeatDelay) {
                // Press the respawn button for the player, as retail does.
                // The module's script runs next tick and may raise someone.
                auto presser = party.actualPlayer() ? party.actualPlayer() : fallen.front();
                raiseRespawnButton(*presser);
                _defeat = PartyDefeatState::Ended;
            }
            break;
        case PartyDefeatState::Ended:
            if (_moduleEvents.empty() && !_defeatReported) {
                _defeatReported = true;
                _game.onPartyDefeated();
            }
            break;
        }
        return;
    }
    if (_defeat != PartyDefeatState::None) {
        // Somebody got up (a script resurrected them): the defeat is off.
        _defeat = PartyDefeatState::None;
        _defeatElapsed = 0.0f;
        _defeatReported = false;
        _game.globalFade().request(GlobalFade::Direction::In, 0.0f, 1.0f);
    }

    // The leader fell and others stand: control passes to the next of them.
    auto leader = party.getLeader();
    if (leader && leader->isDead()) {
        for (int i = 1; i < party.getSize(); ++i) {
            auto member = party.getMember(i);
            if (member && !member->isDead()) {
                party.setPartyLeaderByIndex(i);
                break;
            }
        }
    }

    // Fallen members get back up at 1 HP once the fight is over. The fight is
    // over when no living enemy near a standing member is in combat, and none
    // perceives the party or is perceived by it. An unaware hostile beyond a
    // wall is not a fight (tar_m09aa: the dormant turrets and shield droid by
    // the elevator kept the PC at 0 HP for good after the halls were cleared).
    if (!_area) {
        return;
    }
    // Only worth asking when someone is down or a standing member is in
    // combat with nothing it is attacking (the check walks the area).
    bool leftover = std::any_of(standing.begin(), standing.end(), [this](const auto &m) {
        return m->isInCombat() && !hasLiveAttack(*m);
    });
    if (fallen.empty() && !leftover) {
        return;
    }
    if (!fightGoingOn(standing)) {
        // A standing member left in combat with nobody to fight (T3-M4 after
        // the tar_m09aa halls, still "in combat" with nothing queued) would
        // otherwise hold the fallen down for good: stand it down.
        for (const auto &member : standing) {
            if (member->isInCombat() && !hasLiveAttack(*member)) {
                member->deactivateCombat(0.0f);
            }
        }
        inCombat = std::any_of(standing.begin(), standing.end(), [](const auto &m) { return m->isInCombat(); });
        if (fallen.empty() || inCombat) {
            return;
        }
        for (const auto &member : fallen) {
            member->resurrect(0);
        }
    }
}

bool Module::fightGoingOn(const std::vector<std::shared_ptr<Creature>> &standing) const {
    Party &party = _game.party();
    const float range2 = kRecoveryHostileRange * kRecoveryHostileRange;
    for (const auto &object : _area->getObjectsByType(ObjectType::Creature)) {
        auto creature = std::static_pointer_cast<Creature>(object);
        if (creature->isDead() || party.isMember(*creature)) {
            continue;
        }
        for (const auto &member : standing) {
            if (!_services.game.reputes.getIsEnemy(*creature, *member)) {
                continue;
            }
            const auto &theirs = creature->perception();
            const auto &ours = member->perception();
            bool seen = theirs.sees(member->id()) || ours.sees(creature->id());
            bool near = member->getSquareDistanceTo(*creature) <= range2;
            bool heardOrFighting = creature->isInCombat() ||
                                   theirs.hears(member->id()) || ours.hears(creature->id());
            if (seen || (near && heardOrFighting)) {
                return true;
            }
        }
    }
    return false;
}

bool Module::hasLiveAttack(const Creature &member) const {
    // An attack the player ordered on something that is not a creature (a
    // door, a placeable) or on a living target is not leftover combat.
    if (auto target = member.getAttackTarget()) {
        auto creature = std::dynamic_pointer_cast<Creature>(target);
        if (!creature || !creature->isDead()) {
            return true;
        }
    }
    for (const auto &action : member.actions()) {
        if (action && action->type() == ActionType::AttackObject) {
            return true;
        }
    }
    return false;
}

void Module::loadArea(
    const resource::generated::IFO &ifo,
    const Gff &are,
    const Gff &git,
    bool restoreSavedWorld) {
    reone::info("Load area '" + _info.entryArea + "'");

    const auto identityContext = restoreSavedWorld
                                     ? SerializedIdentityContext::moduleGraph(_name)
                                     : SerializedIdentityContext::templateResource(_name);
    std::shared_ptr<Area> candidateArea;
    std::vector<std::shared_ptr<Object>> noObsolete;
    _game.replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            if (restoreSavedWorld) {
                auto area = std::find_if(
                    ifo.Mod_Area_list.begin(),
                    ifo.Mod_Area_list.end(),
                    [this](const auto &entry) {
                        return entry.Area_Name == _info.entryArea;
                    });
                uint32_t areaId = area == ifo.Mod_Area_list.end()
                                      ? 1
                                      : area->ObjectId;
                candidateArea = _game.newSavedArea(areaId, identityContext);
            } else {
                candidateArea = _game.newArea();
            }
            candidateArea->load(
                _info.entryArea, are, git, identityContext);
        },
        [&]() noexcept {
            _area = std::move(candidateArea);
        });

}

void Module::runSpawnScripts() {
    _area->runSpawnScripts();
}
void Module::loadLimboCreatures(const resource::Gff &ifo) {
    _limboCreatures.clear();
    const auto identityContext = SerializedIdentityContext::moduleGraph(_name);
    for (const auto &creatureGff : ifo.getList("Creature List")) {
        auto creature = _game.newCreature(*creatureGff, identityContext);
        creature->captureSaveRecord(
            *creatureGff,
            identityContext,
            {SaveRecordOriginKind::ModuleLimboCreature, _name});
        _limboCreatures.push_back(std::move(creature));
    }
}

void Module::loadPlayer() {
    _player = std::make_unique<Player>(*this, *_area, *_area->getCamera(CameraType::ThirdPerson), _game.party());
}

void Module::loadParty(const std::string &entry, bool preserveSavedPlacement) {
    glm::vec3 position(0.0f);
    float facing = 0.0f;
    getEntryPoint(entry, position, facing);

    _area->loadParty(position, facing, preserveSavedPlacement);
    _area->onPartyLeaderMoved(true);
    _area->update3rdPersonCameraFacing();

    // Where the party stands is restored from the save; the area's authored
    // OnEnter still runs. Retail defers that script and hands it the
    // load-from-save answer captured when the enter event was made, so the
    // script sees the restore it was created during. Here the script runs
    // inline, still inside the load, so it reads the same answer from the
    // live value without anything needing to carry it.
    // The module hears the party arrive before the area does.
    if (auto leader = _game.party().getLeader()) {
        runOnClientEnterScript(*leader);
    }
    _area->runOnEnterScript();
}

void Module::runOnLoadScript() {
    if (_info.onModLoad.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onModLoad, id());
}

void Module::runOnStartScript() {
    if (_info.onModStart.empty()) {
        return;
    }
    _game.scriptRunner().run(_info.onModStart, id());
}

void Module::getEntryPoint(const std::string &waypoint, glm::vec3 &position, float &facing) const {
    position = _info.entryPosition;
    facing = _info.entryFacing;

    if (!waypoint.empty()) {
        std::shared_ptr<Object> object(
            _area->getObjectByTag(boost::to_lower_copy(waypoint)));
        if (object) {
            position = object->position();
            facing = object->getFacing();
        } else {
            debug("Module entry '" + waypoint + "' not found; using default entry");
        }
    }
}

bool Module::handle(const input::Event &event) {
    if (_player && _player->handle(event))
        return true;
    if (_area->handle(event))
        return true;

    switch (event.type) {
    case input::EventType::MouseMotion:
        if (handleMouseMotion(event.motion))
            return true;
        break;
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button))
            return true;
        break;
    case input::EventType::KeyDown:
        if (handleKeyDown(event.key))
            return true;
        break;
    default:
        break;
    }

    return false;
}

bool Module::handleMouseMotion(const input::MouseMotionEvent &event) {
    CursorType cursor = CursorType::Default;

    auto object = _area->getObjectAt(event.x, event.y);
    if (object && object->isSelectable()) {
        auto objectPtr = _game.getObjectById(object->id());
        _area->hilightObject(objectPtr);

        bool hostile = object->type() == ObjectType::Creature && !object->isDead()
            ? isHostileToPartyLeader(*static_cast<Creature *>(object)) : false;
        cursor = contextualCursor(object->type(), object->isDead(), hostile);
        if (auto trigger = dyn_cast<Trigger>(object)) {
            cursor = cursorTypeForCursorsRow(_services.resource.twoDas, trigger->cursorRow());
        }
    } else {
        _area->hilightObject(nullptr);
    }

    _game.setCursorType(cursor);

    return true;
}

bool Module::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    auto object = _area->getObjectAt(event.x, event.y);
    if (!object || !object->isSelectable()) {
        return false;
    }
    clickObject(_game.getObjectById(object->id()));

    return true;
}

bool Module::clickObject(const std::shared_ptr<Object> &object) {
    if (object != _area->selectedObject()) {
        _area->selectObject(object);
        return false;
    }
    onObjectClick(object);
    return true;
}

void Module::onObjectClick(const std::shared_ptr<Object> &object) {
    // A co-op client's click is a command to the host, never a local action.
    if (auto *coop = _game.coop(); coop && coop->interceptClick(object)) {
        return;
    }
    switch (object->type()) {
    case ObjectType::Creature:
        onCreatureClick(std::static_pointer_cast<Creature>(object));
        break;
    case ObjectType::Door:
        onDoorClick(std::static_pointer_cast<Door>(object));
        break;
    case ObjectType::Placeable:
        onPlaceableClick(std::static_pointer_cast<Placeable>(object));
        break;
    case ObjectType::Trigger: {
        // A detected trap: the default action disables it.
        auto trap = std::static_pointer_cast<Trigger>(object);
        auto leader = _game.party().getLeader();
        if (leader && trap->isTrap() && trap->trapFields().disarmable) {
            leader->clearAllActions();
            auto action = _game.newAction<TrapSkillAction>(ActionType::DisableTrap, trap);
            action->setUserAction(true);
            leader->addAction(std::move(action));
        }
        break;
    }
    default:
        break;
    }
}

bool Module::isHostileToPartyLeader(const Creature &creature) const {
    if (creature.isDead()) {
        return false;
    }
    return _services.game.reputes.getIsEnemy(creature, *_game.party().getLeader());
}

void Module::onCreatureClick(const std::shared_ptr<Creature> &creature) {
    debug(str(boost::format("Module: click: creature '%s', faction %d") % creature->tag() % static_cast<int>(creature->faction())));

    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());

    if (creature->isDead()) {
        if (!creature->items().empty()) {
            partyLeader->clearAllActions();
            partyLeader->addAction(_game.newAction<OpenContainerAction>(creature));
        }
    } else {
        if (isHostileToPartyLeader(*creature)) {
            partyLeader->clearAllActions();
            auto action = _game.newAction<AttackObjectAction>(creature);
            action->setUserAction(true);
            partyLeader->addAction(std::move(action));
        } else if (!creature->conversation().empty()) {
            partyLeader->clearAllActions();
            auto action = _game.newAction<StartConversationAction>(creature, "");
            action->setPlayerOrder(true);
            partyLeader->addAction(std::move(action));
        }
    }
}

void Module::onDoorClick(const std::shared_ptr<Door> &door) {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());
    if (door->isAreaExit()) {
        if (_game.instantDoorTransitions()) {
            // Harness escape hatch: leave from where the leader stands.
            useTransitionDoorOnArrival(door, partyLeader);
            return;
        }
        // Retail walks the leader to the exit first; the transition, or the
        // door's fail-to-open, happens on arrival.
        partyLeader->clearAllActions();
        auto action = _game.newAction<OpenDoorAction>(door);
        action->setUserAction(true);
        action->setUseTransition(true);
        partyLeader->addAction(std::move(action));
        return;
    }
    if (!door->isOpen()) {
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<OpenDoorAction>(door));
    }
}

void Module::useTransitionDoorOnArrival(const std::shared_ptr<Door> &door, const std::shared_ptr<Creature> &user) {
    if (!door || !user || !door->isAreaExit() || !door->isRuntimeLive()) {
        return;
    }
    // Retail checks the lock only on a shut door. An open transition door
    // is used as it stands, locked or not: a fail-to-open script may have
    // opened it without unlocking it (the Taris elevator in a disguise).
    bool shut = !door->isOpen() || door->isOpening();
    if (shut && door->isLocked()) {
        tryUnlockDoorWithKey(_game, *door, *user, _game.party());
    }
    if (shut && door->isLocked()) {
        door->setPendingTransitionUse(user->id());
        door->onFailToOpen(*user);
        return;
    }
    if (door->isOpening()) {
        // Unlocked but still swinging open: go through once it is open.
        door->setPendingTransitionUse(user->id());
        return;
    }
    useTransitionDoor(door, user);
}

void Module::useTransitionDoor(const std::shared_ptr<Door> &door, const std::shared_ptr<Creature> &user) {
    if (!door || !user || door->linkedToModule().empty()) {
        return;
    }
    if (_area->refuseTransitionWhilePartyIsDown(*user)) {
        return;
    }
    // Co-op: the exit starts the party's ready check.
    if (auto *coop = _game.coop(); coop && coop->interceptTransition(door->linkedToModule(), door->linkedTo(), 0, user->id())) {
        return;
    }
    _game.scheduleModuleTransition(door->linkedToModule(), door->linkedTo());
}

void Module::onPlaceableClick(const std::shared_ptr<Placeable> &placeable) {
    std::shared_ptr<Creature> partyLeader(_game.party().getLeader());

    switch (placeable->clickAction()) {
    case Placeable::ClickAction::OpenContainer:
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<OpenContainerAction>(placeable));
        break;
    case Placeable::ClickAction::Use:
        // Walk up, then OnUsed, which may start the conversation itself.
        partyLeader->clearAllActions();
        partyLeader->addAction(_game.newAction<InteractObjectAction>(placeable));
        break;
    case Placeable::ClickAction::Converse:
        partyLeader->clearAllActions();
        {
            auto action = _game.newAction<StartConversationAction>(placeable, "");
            action->setPlayerOrder(true);
            partyLeader->addAction(std::move(action));
        }
        break;
    case Placeable::ClickAction::None:
        if (placeable->hasInventory()) {
            // A locked container: the click only stops what the leader did.
            partyLeader->clearAllActions();
        }
        break;
    }
}

size_t Module::pendingSavedEventCount() const {
    return static_cast<size_t>(std::count(
        _savedEventLive.begin(), _savedEventLive.end(), true));
}

void Module::deserializeSavedEventQueue(
    const resource::Gff &ifo,
    const SerializedIdentityContext &identityContext) {
    _savedEventQueue = SavedEventQueue::fromGff(ifo, identityContext);
    _savedEventLive.clear();
    _savedEventReferencesBound.clear();
    _savedEventLive.reserve(_savedEventQueue.events.size());
    for (const auto &event : _savedEventQueue.events) {
        _savedEventLive.push_back(event.shouldRestore());
    }
    _publishedSavedEvents.clear();
    _savedEventsPublished = false;
}

std::vector<SavedEventRecord> Module::saveEventSnapshot() const {
    // Stable-frame semantic snapshot; byte encoding is deliberately later E3.
    std::vector<SavedEventRecord> result;
    for (size_t index = 0; index < _savedEventQueue.events.size(); ++index) {
        if (index < _savedEventLive.size() && _savedEventLive[index]) {
            result.push_back(_savedEventQueue.events[index]);
        }
    }
    return result;
}

size_t Module::enqueueSaveEvent(SavedEventRecord event) {
    // New events bind through the current B registry before becoming visible to
    // a save snapshot; raw IDs never gain cross-session authority.
    const bool referencesBound = event.bindObjectReferences(_game);
    _savedEventQueue.events.push_back(std::move(event));
    _savedEventLive.push_back(true);
    _savedEventReferencesBound.push_back(referencesBound);
    return _savedEventQueue.events.size() - 1;
}

size_t Module::enqueueBoundSaveEvent(
    SavedEventRecord event, bool referencesBound) {
    // Ordinary travel captures Party timers while the source registry still
    // owns their reference domain. The record already carries C4 exact-
    // incarnation handles; looking its numeric carriers up again after the
    // destination publishes could alias an unrelated object.
    _savedEventQueue.events.push_back(std::move(event));
    _savedEventLive.push_back(true);
    _savedEventReferencesBound.push_back(referencesBound);
    return _savedEventQueue.events.size() - 1;
}

bool Module::cancelSaveEvent(size_t index) {
    if (index >= _savedEventLive.size() || !_savedEventLive[index]) {
        return false;
    }
    _savedEventLive[index] = false;
    for (auto &published : _publishedSavedEvents) {
        if (published.savedIndex == index) {
            published.delivered = true;
        }
    }
    return true;
}

void Module::bindSavedEventQueue() {
    _savedEventReferencesBound.clear();
    _savedEventReferencesBound.reserve(_savedEventQueue.events.size());
    for (auto &event : _savedEventQueue.events) {
        _savedEventReferencesBound.push_back(
            !event.shouldRestore() || event.bindObjectReferences(_game));
    }
}

void Module::publishSavedEventQueue() {
    if (_savedEventsPublished) {
        return;
    }
    SavedScriptSituationImporter importer(
        _game, _services.resource.scripts);
    _publishedSavedEvents.clear();

    for (size_t index = 0; index < _savedEventQueue.events.size(); ++index) {
        const auto &savedEvent = _savedEventQueue.events[index];
        if (!savedEvent.shouldRestore() ||
            index >= _savedEventReferencesBound.size() ||
            !_savedEventReferencesBound[index] ||
            savedEvent.executionSupport() != SavedExecutionSupport::Executable) {
            continue;
        }

        PublishedSavedEvent event;
        event.savedIndex = index;
        // Both fields are Dwords and a day is at most 255 * 60 * 1000 * 24 ms,
        // so the composition cannot overflow the 64-bit clock.
        event.dueMilliseconds =
            static_cast<uint64_t>(savedEvent.day) *
                _game.millisecondsPerWorldDay() +
            savedEvent.time;
        if (savedEvent.eventId == static_cast<uint32_t>(SavedEventType::Timed)) {
            auto situation = std::get_if<SerializedScriptSituation>(
                &savedEvent.payload);
            if (!situation) {
                continue;
            }
            auto imported = importer.import(*situation);
            if (!imported) {
                warn("Module: preserving unsupported timed event: " + imported.message);
                continue;
            }
            event.continuation = std::move(imported.continuation);
        }
        _publishedSavedEvents.push_back(std::move(event));
    }
    _savedEventsPublished = true;
}

void Module::dispatchDueSavedEvents() {
    for (auto &published : _publishedSavedEvents) {
        if (published.delivered) {
            continue;
        }
        if (published.dueMilliseconds <= _game.worldTimeMilliseconds()) {
            deliverSavedEvent(published);
        }
    }
}

void Module::deliverSavedEvent(PublishedSavedEvent &published) {
    published.delivered = true;
    if (published.savedIndex < _savedEventLive.size()) {
        _savedEventLive[published.savedIndex] = false;
    }
    const auto &savedEvent = _savedEventQueue.events[published.savedIndex];
    auto target = savedEvent.object.boundObject();
    if (!target) {
        return;
    }

    switch (static_cast<SavedEventType>(savedEvent.eventId)) {
    case SavedEventType::Timed:
        if (published.continuation) {
            _game.scriptRunner().run(
                *published.continuation, _game, target->id());
        }
        break;
    case SavedEventType::ApplyEffect: {
        auto savedEffect = std::get_if<EffectInstance>(&savedEvent.payload);
        if (!savedEffect) {
            break;
        }
        EffectInstance effect(*savedEffect);
        if (effect.durationType() == DurationType::Temporary) {
            auto remaining = _game.remainingEffectDuration(effect);
            if (!remaining || *remaining <= 0.0f) {
                break;
            }
            effect.remainingDuration = *remaining;
        }
        if (effect.hasStableId()) {
            _game.importEffectId(effect.id);
        } else {
            effect.id = _game.allocateEffectId();
        }
        target->restoreEffect(std::move(effect));
        break;
    }
    case SavedEventType::RemoveEffect: {
        auto effect = std::get_if<EffectInstance>(&savedEvent.payload);
        if (effect) {
            target->removeEffectsById(effect->id);
        }
        break;
    }
    case SavedEventType::SignalEvent: {
        auto saved = std::get_if<SavedScriptEvent>(&savedEvent.payload);
        if (!saved) {
            break;
        }
        std::vector<std::shared_ptr<Object>> objects;
        objects.reserve(saved->objects.size());
        for (const auto &reference : saved->objects) {
            objects.push_back(reference.boundObject());
        }
        auto caller = savedEvent.caller.boundObject();
        _game.scriptEvents().signal(
            target->id(),
            caller ? caller->id() : script::kObjectInvalid,
            eventFromSaved(*saved, std::move(objects)));
        break;
    }
    default:
        break;
    }
}


void Module::update(float dt) {
    // Process the module object's own action queue so delayed/assigned commands
    // scheduled by module scripts (e.g. Mod_OnModLoad) execute. Without this the
    // module is never ticked and its DelayCommand continuations never run.
    // Script events signalled outside the tick (conversation, GUI, console)
    // run at the first drain of the next tick.
    _game.scriptEvents().runDue();
    Object::update(dt);
    dispatchDueSavedEvents();
    _game.scriptEvents().runDue();
    updateEvents(dt);
    _game.scriptEvents().runDue();
    if (_game.module().get() != this) {
        return;
    }

    if (_game.cameraType() == CameraType::ThirdPerson) {
        _player->update(dt);
    }
    _area->update(dt);
}

std::vector<ContextAction> Module::getContextActions(const std::shared_ptr<Object> &object) const {
    std::vector<ContextAction> actions;

    switch (object->type()) {
    case ObjectType::Creature: {
        auto leader = _game.party().getLeader();
        auto creature = std::static_pointer_cast<Creature>(object);
        if (isHostileToPartyLeader(*creature)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
            auto weapon = leader->getEquippedItem(InventorySlots::rightWeapon);
            // Attack feats come from feat.2da usetype: melee feats with a
            // melee weapon (or none), ranged feats with a ranged weapon.
            std::set<FeatType> owned(leader->attributes().feats());
            auto attackFeats = selectAttackFeats(
                owned,
                weapon && weapon->isRanged(),
                [this](FeatType type) { return _services.game.feats.get(type); });
            for (auto feat : attackFeats) {
                actions.push_back(ContextAction(feat));
            }

            // The leader's own items and, for a companion, the party's.
            for (const auto &[item, spell] : _game.party().usableItemSpells(*leader, ItemAttributes::Kind::Attacking)) {
                actions.push_back(ContextAction(item, spell));
            }
        }
        break;
    }
    case ObjectType::Door: {
        auto door = std::static_pointer_cast<Door>(object);
        auto leader = _game.party().getLeader();
        if (canBashDoor(*door, *leader, _services.game.reputes)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
        }
        if (!door->isOpen() && door->isLocked() && !door->isKeyRequired() && leader->attributes().hasSkill(SkillType::Security)) {
            actions.push_back(ContextAction(SkillType::Security));
        }
        break;
    }
    case ObjectType::Placeable: {
        auto placeable = cast<Placeable>(object);
        auto leader = _game.party().getLeader();
        if (canBashPlaceable(*placeable, *leader, _services.game.reputes)) {
            actions.push_back(ContextAction(ActionType::AttackObject));
        }
        if (canUseSecurityOnPlaceable(*placeable, *leader)) {
            actions.push_back(ContextAction(SkillType::Security));
        }
        break;
    }
    case ObjectType::Trigger: {
        auto trap = cast<Trigger>(object);
        if (trap->isTrap() && trap->isTrapDetectedByParty() && trap->trapFields().disarmable) {
            actions.push_back(ContextAction(ActionType::DisableTrap));
            actions.push_back(ContextAction(ActionType::RecoverTrap));
        }
        break;
    }
    default:
        break;
    }

    return actions;
}

bool Module::handleKeyDown(const input::KeyEvent &event) {
    switch (event.code) {
    case input::KeyCode::Space: {
        if (auto *coop = _game.coop(); coop && coop->handlePauseKey()) {
            return true;
        }
        bool paused = !_game.isPaused();
        _game.setPaused(paused);
        return true;
    }
    default:
        return false;
    }
}

} // namespace game

} // namespace reone
