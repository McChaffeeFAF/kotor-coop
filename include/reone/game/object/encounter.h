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

#include "../object.h"
#include "reone/resource/gff.h"

#include <set>

namespace reone {

namespace game {

class Creature;

class Encounter : public Object {
public:
    struct SavedRuntimeState {
        int32_t areaListMaxSize {0};
        int32_t areaListSize {0};
        std::vector<uint32_t> areaObjectIds;
        float areaPoints {0.0f};
        int32_t currentSpawns {0};
        int32_t customScriptId {0};
        bool exhausted {false};
        uint32_t heartbeatDay {0};
        uint32_t heartbeatTime {0};
        uint32_t lastEntered {0};
        uint32_t lastLeft {0};
        uint32_t lastSpawnDay {0};
        uint32_t lastSpawnTime {0};
        int32_t numberSpawned {0};
        float spawnPoolActive {0.0f};
        bool started {false};
    };

    Encounter(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Encounter,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Encounter;
    }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    const SavedRuntimeState &savedRuntimeState() const { return _savedRuntimeState; }
    std::shared_ptr<Object> savedAreaObject(size_t index) const;

    /**
     * Track the creatures inside the encounter's polygon: OnEntered for each
     * that comes in, OnExit for each that leaves. Active or not (a script on
     * an inactive encounter's OnEntered is what activates it).
     */
    void update(float dt) override;

    /** Whether a point (world x, y) lies inside the encounter's polygon. */
    bool contains(const glm::vec2 &point) const;
    bool isOccupant(uint32_t objectId) const;
    bool isPlayerOnly() const { return _playerOnly; }

    // Spawning (design note c09_ai_combat, "Encounters")

    /** How many times the encounter may spawn (Respawns; -1 for no limit) and how often it has (CurrentSpawns). */
    int maxSpawns() const { return _respawns; }
    void setMaxSpawns(int spawns) { _respawns = spawns; }
    int currentSpawns() const { return _savedRuntimeState.currentSpawns; }
    void setCurrentSpawns(int spawns) { _savedRuntimeState.currentSpawns = spawns; }

    int difficulty() const { return _difficulty; }
    void setDifficulty(int difficulty) { _difficulty = difficulty; }

    bool isActive() const { return _active; }
    /**
     * SetEncounterActive. Switching on also runs the trigger check at once,
     * so a qualifying creature already inside spawns the encounter.
     */
    void setActive(bool active);
    bool isStarted() const { return _savedRuntimeState.started; }
    /** Living creatures this encounter spawned (NumberSpawned). */
    int numberSpawned() const { return _savedRuntimeState.numberSpawned; }
    /** Creatures chosen by the trigger but not placed yet. */
    const std::vector<std::string> &pendingSpawns() const { return _pendingSpawns; }
    /** Whether the reset timer is running (exhausted, waiting to re-arm). */
    bool isResetPending() const { return _resetPending; }

    /**
     * K2 behaviour: free walkable ground for a creature near \p origin. The
     * point itself, then square rings from 1 m out, growing by \p radius,
     * up to 20 m. Returns false when nothing is free.
     */
    bool findSpawnSpot(const Creature &creature, const glm::vec3 &origin, float radius, glm::vec3 &spot) const;

    /**
     * A creature this encounter spawned died (died=true) or was removed
     * alive (died=false). The last death exhausts the encounter: OnExhausted
     * runs and the encounter switches itself off.
     */
    void onSpawnedCreatureGone(bool died);

    /** The creature resrefs a trigger would spawn, drawn with randomInt. */
    std::vector<std::string> chooseSpawns() const;

private:
    friend class ModuleSnapshotBuilder;
    struct SpawnPoint {
        glm::vec3 position {0.0f};
        float orientation {0.0f};
    };

    struct EncounterCreature {
        uint32_t _appearance {0};
        float _cr {0.0f};
        std::string _resRef;
        bool _singleSpawn {false};
        int32_t _guaranteedCount {0};
    };

    // Serializable
    resource::LocString _locName;
    bool _active {false};
    bool _reset {false};
    int32_t _resetTime {0};
    int32_t _respawns {0};
    int32_t _spawnOption {0};
    int32_t _maxCreatures {0};
    int32_t _recCreatures {0};
    bool _playerOnly {false};
    Faction _faction {Faction::Invalid};
    int32_t _difficultyIndex {0};
    int32_t _difficulty {0};

    std::string _onEntered;
    std::string _onExit;
    std::string _onExhausted;
    std::string _onHeartbeat;
    std::string _onUserDefined;

    std::vector<EncounterCreature> _creatures;
    std::vector<glm::vec3> _geometry;
    std::vector<SpawnPoint> _spawnPoints;
    SavedRuntimeState _savedRuntimeState;
    // END Serializable

    // Creatures inside the polygon, by object id (ordered, so the scripts
    // fire in a stable order from run to run).
    std::set<uint32_t> _occupants;

    // Spawning runtime (not serialized beyond _savedRuntimeState).
    // The queue of chosen, unplaced creatures is saved (SpawnList); the
    // spawn-point rotation is not, as in K2.
    std::vector<std::string> _pendingSpawns;
    std::vector<float> _pendingSpawnCRs;
    int _pendingTotal {0};
    int _spawnPointIndex {0};
    int _spawnPointCount {0};
    // Reset timer: runs from LastSpawnDay/LastSpawnTime, which are saved.
    bool _resetPending {false};

    void runEnteredScript(uint32_t entering);
    void runExitScript(uint32_t exiting);
    void runExhaustedScript();

    bool qualifiesAsTrigger(const Creature &creature) const;
    void checkTrigger(uint32_t entering);
    void spawnNext();
    void updateReset(float dt);
    float spawnCR(const std::string &resRef) const;

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void deserializeSavedRuntimeState(const resource::Gff &gff);
    void deserializeCreatures(const resource::Gff &gff);
    void deserializeGeometry(const resource::Gff &gff);
    void deserializeSpawnPoints(const resource::Gff &gff);
};

} // namespace game

} // namespace reone
