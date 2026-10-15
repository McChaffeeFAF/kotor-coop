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

#include "reone/game/object/encounter.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/script/runner.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/strings.h"
#include "reone/scene/collision.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

void Encounter::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto ute = _services.resource.gffs.get(templateRes, ResType::Ute)) {
            deserializeAll(*ute, SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);
}

void Encounter::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    deserializeRuntimeState(gff, identityContext);
    deserializeSavedRuntimeState(gff);
    if (gff.readString(_tag, "Tag")) {
        boost::to_lower(_tag);
    }

    if (gff.readLocString(_locName, "LocalizedName", _services.resource.strings)) {
        _name = _locName.str();
    }

    gff.readBool(_active, "Active");
    gff.readBool(_reset, "Reset");
    gff.readInt(_resetTime, "ResetTime");
    gff.readInt(_respawns, "Respawns");
    gff.readInt(_spawnOption, "SpawnOption");
    gff.readInt(_maxCreatures, "MaxCreatures");
    gff.readInt(_recCreatures, "RecCreatures");
    gff.readBool(_playerOnly, "PlayerOnly");
    gff.readEnum(_faction, "Faction");

    // index into encdifficulty.2da
    gff.readInt(_difficultyIndex, "DifficultyIndex");
    gff.readInt(_difficulty, "Difficulty");

    gff.readFloat(_position[0], "XPosition");
    gff.readFloat(_position[1], "YPosition");
    gff.readFloat(_position[2], "ZPosition");

    gff.readResRef(_onEntered, "OnEntered");
    gff.readResRef(_onExit, "OnExit");
    gff.readResRef(_onExhausted, "OnExhausted");
    gff.readResRef(_onHeartbeat, "OnHeartbeat");
    gff.readResRef(_onUserDefined, "OnUserDefined");

    deserializeCreatures(gff);
    deserializeGeometry(gff);
    deserializeSpawnPoints(gff);

    updateTransform();
}

void Encounter::deserializeSavedRuntimeState(const resource::Gff &gff) {
    _savedRuntimeState = SavedRuntimeState {};
    gff.readInt(_savedRuntimeState.areaListMaxSize, "AreaListMaxSize");
    gff.readInt(_savedRuntimeState.areaListSize, "AreaListSize");
    gff.readFloat(_savedRuntimeState.areaPoints, "AreaPoints");
    gff.readInt(_savedRuntimeState.currentSpawns, "CurrentSpawns");
    gff.readInt(_savedRuntimeState.customScriptId, "CustomScriptId");
    gff.readBool(_savedRuntimeState.exhausted, "Exhausted");
    gff.readDword(_savedRuntimeState.heartbeatDay, "HeartbeatDay");
    gff.readDword(_savedRuntimeState.heartbeatTime, "HeartbeatTime");
    gff.readDword(_savedRuntimeState.lastEntered, "LastEntered");
    gff.readDword(_savedRuntimeState.lastLeft, "LastLeft");
    gff.readDword(_savedRuntimeState.lastSpawnDay, "LastSpawnDay");
    gff.readDword(_savedRuntimeState.lastSpawnTime, "LastSpawnTime");
    gff.readInt(_savedRuntimeState.numberSpawned, "NumberSpawned");
    gff.readFloat(_savedRuntimeState.spawnPoolActive, "SpawnPoolActive");
    gff.readBool(_savedRuntimeState.started, "Started");

    // Chosen but unplaced creatures (K2 SpawnList).
    _pendingSpawns.clear();
    _pendingSpawnCRs.clear();
    for (const auto &entry : gff.getList("SpawnList")) {
        std::string resRef;
        float cr = 0.0f;
        entry->readResRef(resRef, "SpawnResRef");
        entry->readFloat(cr, "SpawnCR");
        if (!resRef.empty()) {
            _pendingSpawns.push_back(resRef);
            _pendingSpawnCRs.push_back(cr);
        }
    }
    _pendingTotal = static_cast<int>(_pendingSpawns.size());
    _spawnPointIndex = 0;
    _spawnPointCount = 0;
    // A saved reset time means the encounter is waiting to re-arm.
    _resetPending = _savedRuntimeState.lastSpawnDay != 0 || _savedRuntimeState.lastSpawnTime != 0;

    size_t index = 0;
    for (const auto &entry : gff.getList("AreaList")) {
        uint32_t id = 0;
        if (entry->readDword(id, "AreaObject")) {
            _savedRuntimeState.areaObjectIds.push_back(id);
            _savedReferenceIds.insert_or_assign(
                "EncounterArea/" + std::to_string(index), id);
        }
        ++index;
    }
}

std::shared_ptr<Object> Encounter::savedAreaObject(size_t index) const {
    return savedReference("EncounterArea/" + std::to_string(index));
}

bool Encounter::contains(const glm::vec2 &point) const {
    // Even-odd rule over the polygon; the points are relative to the
    // encounter's position.
    bool inside = false;
    size_t count = _geometry.size();
    if (count < 3) {
        return false;
    }
    glm::vec2 origin(_position);
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
        glm::vec2 a(origin + glm::vec2(_geometry[i]));
        glm::vec2 b(origin + glm::vec2(_geometry[j]));
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

bool Encounter::isOccupant(uint32_t objectId) const {
    return _occupants.count(objectId) > 0;
}

void Encounter::update(float dt) {
    Object::update(dt);

    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    // One pending creature is placed per update, as the trigger queued them.
    if (!_pendingSpawns.empty()) {
        spawnNext();
    }
    updateReset(dt);
    if (_geometry.size() < 3) {
        return;
    }
    const Party &party = _game.party();
    std::set<uint32_t> inside;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        if (object->isDead()) {
            continue;
        }
        if (_playerOnly && !party.isMember(*object)) {
            continue;
        }
        if (contains(glm::vec2(object->position()))) {
            inside.insert(object->id());
        }
    }
    std::vector<uint32_t> left;
    for (uint32_t id : _occupants) {
        if (inside.count(id) == 0) {
            left.push_back(id);
        }
    }
    std::vector<uint32_t> entered;
    for (uint32_t id : inside) {
        if (_occupants.count(id) == 0) {
            entered.push_back(id);
        }
    }
    _occupants = std::move(inside);
    for (uint32_t id : left) {
        runExitScript(id);
    }
    for (uint32_t id : entered) {
        // The trigger check comes before the OnEntered script, so a script
        // that switches the encounter on re-runs the check itself.
        checkTrigger(id);
        runEnteredScript(id);
    }
}

void Encounter::setActive(bool active) {
    _active = active;
    if (active) {
        checkTrigger(script::kObjectInvalid);
    }
}

bool Encounter::qualifiesAsTrigger(const Creature &creature) const {
    if (creature.isDead()) {
        return false;
    }
    // Only a creature hostile to the encounter's faction sets it off.
    if (!_services.game.reputes.getIsEnemy(creature.reputationFaction(), _faction)) {
        return false;
    }
    return !_playerOnly || _game.party().isMember(creature);
}

void Encounter::checkTrigger(uint32_t entering) {
    if (!_active || _savedRuntimeState.started) {
        return;
    }
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    bool triggered = false;
    for (const auto &object : area->getObjectsByType(ObjectType::Creature)) {
        const auto &creature = static_cast<const Creature &>(*object);
        bool inside = creature.id() == entering || contains(glm::vec2(creature.position()));
        if (inside && qualifiesAsTrigger(creature)) {
            triggered = true;
            break;
        }
    }
    if (!triggered) {
        return;
    }
    _savedRuntimeState.started = true;
    _pendingSpawns = chooseSpawns();
    _pendingTotal = static_cast<int>(_pendingSpawns.size());
    _pendingSpawnCRs.clear();
    for (const auto &resRef : _pendingSpawns) {
        _pendingSpawnCRs.push_back(spawnCR(resRef));
    }
    _spawnPointIndex = 0;
    _spawnPointCount = 0;
    debug(str(boost::format("Encounter %s triggered: %d creatures") % _tag % _pendingTotal));
}

std::vector<std::string> Encounter::chooseSpawns() const {
    std::vector<std::string> result;
    if (_creatures.empty()) {
        return result;
    }
    // Highest challenge rating first; the sort is stable, so equal ratings
    // keep their template order.
    std::vector<const EncounterCreature *> sorted;
    for (const auto &creature : _creatures) {
        sorted.push_back(&creature);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const EncounterCreature *a, const EncounterCreature *b) {
        return a->_cr > b->_cr;
    });
    int low = std::max(0, _recCreatures);
    int high = std::max(low, _maxCreatures);
    int remaining = randomInt(low, high);
    if (remaining <= 0) {
        return result;
    }
    // 1. Every single-spawn creature, once.
    bool allSingle = true;
    for (auto *creature : sorted) {
        if (!creature->_singleSpawn) {
            allSingle = false;
            continue;
        }
        result.push_back(creature->_resRef);
        if (--remaining == 0) {
            return result;
        }
    }
    if (allSingle) {
        return result;
    }
    // 2. Guaranteed copies of the repeatable creatures.
    std::vector<const EncounterCreature *> pool;
    for (auto *creature : sorted) {
        if (creature->_singleSpawn) {
            continue;
        }
        pool.push_back(creature);
        for (int i = 0; i < creature->_guaranteedCount; ++i) {
            result.push_back(creature->_resRef);
            if (--remaining == 0) {
                return result;
            }
        }
    }
    // 3. The rest drawn uniformly from the repeatable creatures.
    for (; remaining > 0; --remaining) {
        int index = randomInt(0, static_cast<int>(pool.size()) - 1);
        result.push_back(pool[index]->_resRef);
    }
    return result;
}

void Encounter::spawnNext() {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area || _pendingSpawns.empty()) {
        return;
    }
    std::string resRef = _pendingSpawns.front();
    _pendingSpawns.erase(_pendingSpawns.begin());
    if (!_pendingSpawnCRs.empty()) {
        _pendingSpawnCRs.erase(_pendingSpawnCRs.begin());
    }

    // K2 behaviour: each spawn point takes (queue length / points) creatures,
    // integer division, then the next point is used, wrapping round. With
    // fewer creatures than points the quota is 0, so the first creature
    // moves on to the second point and every later one stays there. The
    // point's Orientation is the creature's facing.
    const SpawnPoint *point = nullptr;
    if (!_spawnPoints.empty()) {
        int points = static_cast<int>(_spawnPoints.size());
        int quota = _pendingTotal / points;
        if (_spawnPointCount == quota) {
            _spawnPointIndex = (_spawnPointIndex + 1) % points;
            _spawnPointCount = 0;
        }
        ++_spawnPointCount;
        point = &_spawnPoints[_spawnPointIndex];
    }

    std::shared_ptr<Creature> creature;
    try {
        creature = _game.newCreatureFromBlueprint(resRef);
    } catch (const std::exception &e) {
        warn(str(boost::format("Encounter %s: cannot spawn %s: %s") % _tag % resRef % e.what()));
        return;
    }
    if (!creature) {
        return;
    }
    // K2 behaviour: free walkable ground within 20 m of the spawn point;
    // failing that (or with no spawn point), within 20 m of the encounter's
    // own position. A creature with nowhere to stand is not spawned.
    float radius = std::max(0.1f, creature->creaturePersonalSpace());
    glm::vec3 position(0.0f);
    bool placed = point && findSpawnSpot(*creature, point->position, radius, position);
    if (!placed) {
        placed = findSpawnSpot(*creature, _position, radius, position);
    }
    if (!placed) {
        warn(str(boost::format("Encounter %s: no free ground for %s") % _tag % resRef));
        return;
    }
    creature->setPosition(position);
    // K2 behaviour: the facing direction is (sin o, cos o), which is a
    // reone facing of -o.
    creature->setFacing(point ? -point->orientation : 0.0f);
    creature->setEncounterId(_id);
    area->add(creature);
    ++_savedRuntimeState.numberSpawned;
    creature->runSpawnScript();
}

void Encounter::onSpawnedCreatureGone(bool died) {
    if (_savedRuntimeState.numberSpawned <= 0) {
        return;
    }
    --_savedRuntimeState.numberSpawned;
    if (!died || _savedRuntimeState.numberSpawned > 0 || !_pendingSpawns.empty()) {
        return;
    }
    // Exhausted: every creature it spawned is dead.
    runExhaustedScript();
    if (_reset) {
        // K2 behaviour: the reset timer starts from the current game time,
        // kept in LastSpawnDay/LastSpawnTime so that it is saved.
        uint64_t now = _game.worldTimeMilliseconds();
        uint32_t perDay = _game.millisecondsPerWorldDay();
        _savedRuntimeState.lastSpawnDay = static_cast<uint32_t>(now / perDay);
        _savedRuntimeState.lastSpawnTime = static_cast<uint32_t>(now % perDay);
        if (_savedRuntimeState.lastSpawnDay == 0 && _savedRuntimeState.lastSpawnTime == 0) {
            _savedRuntimeState.lastSpawnTime = 1;
        }
        _resetPending = true;
    }
    _active = false;
    _savedRuntimeState.started = false;
}

void Encounter::updateReset(float dt) {
    if (!_resetPending || _spawnOption != 1 || !_reset) {
        return;
    }
    // K2 behaviour: re-arm once ResetTime seconds of game time have passed
    // since exhaustion, or the day has changed.
    uint64_t now = _game.worldTimeMilliseconds();
    uint32_t perDay = _game.millisecondsPerWorldDay();
    uint64_t day = now / perDay;
    uint64_t since = static_cast<uint64_t>(_savedRuntimeState.lastSpawnDay) * perDay + _savedRuntimeState.lastSpawnTime;
    bool due = day != _savedRuntimeState.lastSpawnDay ||
               now >= since + static_cast<uint64_t>(std::max(0, _resetTime)) * 1000u;
    if (!due) {
        return;
    }
    if (_savedRuntimeState.currentSpawns < _respawns || _respawns == -1) {
        ++_savedRuntimeState.currentSpawns;
        _resetPending = false;
        _savedRuntimeState.lastSpawnDay = 0;
        _savedRuntimeState.lastSpawnTime = 0;
        setActive(true);
    }
}

float Encounter::spawnCR(const std::string &resRef) const {
    for (const auto &creature : _creatures) {
        if (creature._resRef == resRef) {
            return creature._cr;
        }
    }
    return 0.0f;
}

bool Encounter::findSpawnSpot(const Creature &creature, const glm::vec3 &origin, float radius, glm::vec3 &spot) const {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto accept = [&](float x, float y) {
        scene::Collision collision;
        glm::vec3 probe(x, y, origin.z + 1.0f);
        if (!sceneGraph.testElevation(probe, collision)) {
            return false;
        }
        glm::vec3 ground(x, y, collision.intersection.z);
        // Clear of walls by the creature's radius: ground under each side.
        for (const glm::vec2 &offset : {glm::vec2(radius, 0.0f), glm::vec2(-radius, 0.0f), glm::vec2(0.0f, radius), glm::vec2(0.0f, -radius)}) {
            scene::Collision edge;
            if (!sceneGraph.testElevation(glm::vec3(x + offset.x, y + offset.y, origin.z + 1.0f), edge)) {
                return false;
            }
        }
        if (area && !area->isPointClearOfCreatures(creature, ground)) {
            return false;
        }
        spot = ground;
        return true;
    };
    if (accept(origin.x, origin.y)) {
        return true;
    }
    constexpr float kFirstRing = 1.0f;
    constexpr float kSearchRange = 20.0f;
    for (float d = kFirstRing; d < kSearchRange; d += radius) {
        // The top and bottom rows, then the two side columns.
        for (float x = -d; x <= d + 1e-4f; x += radius) {
            if (accept(origin.x + x, origin.y - d) || accept(origin.x + x, origin.y + d)) {
                return true;
            }
        }
        for (float y = -d + radius; y < d - 1e-4f; y += radius) {
            if (accept(origin.x - d, origin.y + y) || accept(origin.x + d, origin.y + y)) {
                return true;
            }
        }
    }
    return false;
}

void Encounter::runExhaustedScript() {
    if (_onExhausted.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onExhausted,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)}});
}

void Encounter::runEnteredScript(uint32_t entering) {
    setLastEntered(entering);
    if (_onEntered.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onEntered,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::EnteringObject, script::Variable::ofObject(entering)}});
}

void Encounter::runExitScript(uint32_t exiting) {
    setLastLeft(exiting);
    if (_onExit.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onExit,
        {{script::ArgKind::Caller, script::Variable::ofObject(_id)},
         {script::ArgKind::ExitingObject, script::Variable::ofObject(exiting)}});
}

void Encounter::deserializeCreatures(const resource::Gff &gff) {
    // A later layer (the GIT over the UTE) replaces the list it carries.
    if (!gff.getList("CreatureList").empty()) {
        _creatures.clear();
    }
    for (const auto &creatureGff : gff.getList("CreatureList")) {
        EncounterCreature creature;
        creatureGff->readDword(creature._appearance, "Appearance");
        creatureGff->readFloat(creature._cr, "CR");
        creatureGff->readResRef(creature._resRef, "ResRef");
        creatureGff->readBool(creature._singleSpawn, "SingleSpawn");
        creatureGff->readInt(creature._guaranteedCount, "GuaranteedCount");
        _creatures.push_back(std::move(creature));
    }
}

void Encounter::deserializeGeometry(const resource::Gff &gff) {
    // A later layer (the GIT over the UTE) replaces the list it carries.
    if (!gff.getList("Geometry").empty()) {
        _geometry.clear();
    }
    for (const auto &pointGff : gff.getList("Geometry")) {
        glm::vec3 point;
        pointGff->readFloat(point[0], "X");
        pointGff->readFloat(point[1], "Y");
        pointGff->readFloat(point[2], "Z");
        _geometry.push_back(point);
    }
}

void Encounter::deserializeSpawnPoints(const resource::Gff &gff) {
    // A later layer (the GIT over the UTE) replaces the list it carries.
    if (!gff.getList("SpawnPointList").empty()) {
        _spawnPoints.clear();
    }
    for (const auto &pointGff : gff.getList("SpawnPointList")) {
        SpawnPoint point;
        pointGff->readFloat(point.position[0], "X");
        pointGff->readFloat(point.position[1], "Y");
        pointGff->readFloat(point.position[2], "Z");
        pointGff->readFloat(point.orientation, "Orientation");
        _spawnPoints.push_back(point);
    }
}

} // namespace game

} // namespace reone
