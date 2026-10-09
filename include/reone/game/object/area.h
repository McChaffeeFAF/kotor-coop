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

#include <map>

#include "reone/game/areamusic.h"
#include "reone/game/mapexploration.h"
#include "reone/game/messagebus.h"
#include "reone/game/minigame.h"
#include "reone/game/transitioncandidate.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/are.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/types.h"
#include "reone/system/timer.h"

#include "../object.h"
#include "../object/camera/animated.h"
#include "../object/camera/dialog.h"
#include "../object/camera/firstperson.h"
#include "../object/camera/static.h"
#include "../object/camera/thirdperson.h"
#include "../pathfinder.h"
#include "../types.h"

namespace reone {

namespace game {

const float kHeartbeatInterval = 6.0f;

class Creature;
class Door;
class Location;
class Object;
class Room;
class Trigger;
class ModuleSnapshotBuilder;

using RoomMap = std::unordered_map<std::string, std::shared_ptr<Room>>;
using ObjectList = std::vector<std::shared_ptr<Object>>;

class Area : public Object {
public:
    struct Grass {
        std::shared_ptr<graphics::Texture> texture;
        float density {0.0f};
        float quadSize {0.0f};
        int ambient {0};
        int diffuse {0};
        glm::vec4 probabilities {0.0f};
    };

    using SearchCriteriaList = std::vector<std::pair<CreatureType, int>>;

    Area(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services);

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Area;
    }

    void load(
        std::string name,
        const resource::Gff &are,
        const resource::Gff &git,
        const SerializedIdentityContext &identityContext);
    void activate();

    bool handle(const input::Event &event);
    void update(float dt);

    void destroyObject(const Object &object);
    void initCameras(const glm::vec3 &entryPosition, float entryFacing);

    void onPartyLeaderMoved(bool roomChanged = false);
    void updateRoomVisibility();

    void startDialog(const std::shared_ptr<Object> &object, const std::string &resRef);
    void update3rdPersonCameraFacing();
    void update3rdPersonCameraTarget();
    bool landObject(Object &object);

    /** The walkmesh point under `position`, if the ground there is walkable. */
    std::optional<glm::vec3> walkablePointAt(const glm::vec3 &position);

    /** True when a creature could step straight from origin to dest. */
    bool isStraightWalkClear(const glm::vec3 &origin, const glm::vec3 &dest, const Object *exclude);
    void add(const std::shared_ptr<Object> &object);

    bool moveCreature(const std::shared_ptr<Creature> &creature, const glm::vec2 &dir, bool run, float dt,
                      float maxDistance = FLT_MAX);

    /**
     * One step of a knockback throw (Force push): move the creature straight
     * along dir by distance without turning it. The step must end on walkable
     * walkmesh; unless ignoreObstacles, walls, doors, placeables and other
     * creatures in the way block it. Returns false, leaving the creature
     * where it was, when the step is blocked.
     */
    bool knockbackCreature(Creature &creature, const glm::vec2 &dir, float distance, bool ignoreObstacles);

    /**
     * True when no living creature that collides with \p mover stands within
     * their combined personal space of \p point.
     */
    bool isPointClearOfCreatures(const Creature &mover, const glm::vec3 &point) const;

    /**
     * True when \p a and \p b pass through each other: creature collision,
     * local avoidance and side-step probes skip the pair.
     */
    bool ignoresCreatureCollision(const Creature &a, const Creature &b) const;
    void determineObjectRoom(Object &object);

    bool isUnescapable() const { return _unescapable; }

    // ARE Flags bits.
    static constexpr uint32_t kFlagInterior = 0x1;
    static constexpr uint32_t kFlagUnderground = 0x2;
    static constexpr uint32_t kFlagNatural = 0x4;

    uint32_t flags() const { return _flags; }
    bool isInterior() const { return (_flags & kFlagInterior) != 0; }
    bool isUnderground() const { return (_flags & kFlagUnderground) != 0; }
    bool isNatural() const { return (_flags & kFlagNatural) != 0; }

    /** Weather chances, in percent; all zero in an interior area. */
    struct Weather {
        int chanceRain {0};
        int chanceSnow {0};
        int chanceLightning {0};
        int windPower {0};
    };

    const Weather &weather() const { return _weather; }

    bool hasMinigame() const { return _miniGameSpec.has_value(); }
    const MinigameSpec &miniGame() const { return *_miniGameSpec; }

    Object *getObjectAt(int x, int y) const;

    // Presentation-active module transitions, for the destination banner.
    // Read-only: no tenants, scripts or transitions are touched.
    std::vector<TransitionPortal> transitionPresentationPortals() const;

    /**
     * An area transition is refused while any party member is down. The
     * refusal runs k_trg_transfail as the creature that tried to leave.
     *
     * @return true if the transition must not happen
     */
    bool refuseTransitionWhilePartyIsDown(const Object &activator);
    glm::vec3 getSelectableScreenCoords(const std::shared_ptr<Object> &object, const glm::mat4 &projection, const glm::mat4 &view) const;

    const CameraStyle &camStyleDefault() const { return _camStyleDefault; }
    /** The track the area's music controller selects now (may be empty). */
    std::string music() const { return _musicState.current(); }
    AreaMusic &musicState() { return _musicState; }
    /** are Rooms/AmbientScale of a room (1 when not listed). */
    float roomAmbientScale(const std::string &room) const;
    const AreaMusic &musicState() const { return _musicState; }

    enum class MusicSlot {
        Day,
        Night,
        Battle
    };

    /** ambientmusic.2da row -> track (empty resref for silence). */
    AreaMusic::Track musicTrack(int row) const;
    /** ambientsound.2da row -> track (empty resref for silence). */
    AreaMusic::Track ambientTrack(int row) const;

    /**
     * MusicBackgroundChange*, MusicBattleChange: store the row. Changing a
     * background slot switches background music on (it restarts with the
     * new track).
     */
    void changeMusic(MusicSlot slot, int row);
    /** AmbientSoundChange*: store the row and switch the ambient bed on. */
    void changeAmbient(bool day, int row);

    /** Tell the game's players to follow this area if it is the current one. */
    void refreshMusic();

    /**
     * Combat excitement (excitedduration.2da row: 1 damage, 2 spell cast).
     * A party member with a living enemy within 30 m turns battle music on
     * for the row's duration; see design note c04_audio.
     */
    void exciteCombat(const Creature &creature, int excitedRow);
    float combatExcitement() const { return _combatExcitement; }

    /**
     * PlayRoomAnimation: loop the room model's "scriptloopNN" animation,
     * replacing the previous script loop. False if no such room/animation.
     */
    bool playRoomAnimation(const std::string &room, int animation);

    /** RevealMap: the explored grid over this area's map. */
    MapExploration &mapExploration() { return _mapExploration; }
    const MapExploration &mapExploration() const { return _mapExploration; }
    /** World point to the map's normalized texture space (ARE Map points). */
    glm::vec2 worldToMapPoint(const glm::vec2 &world) const;
    const ObjectList &objects() const { return _objects; }
    const std::string &localizedName() const { return _localizedName; }
    const RoomMap &rooms() const { return _rooms; }
    const Grass &grass() const { return _grass; }
    const glm::vec3 &ambientColor() const { return _ambientColor; }

    Pathfinder &pathfinder() { return _pathfinder; }
    /**
     * Rebuild the pathfinder's door barriers: every closed door that is
     * locked or static blocks planning across its doorway.
     */
    void refreshPathBarriers();

    /**
     * Put a follower that fell behind back at its formation slot beside the
     * leader, or the nearest free spot, as area entry places the party.
     */
    void regroupWithLeader(const std::shared_ptr<Creature> &follower);

    void setUnescapable(bool value);

    // Objects

    std::shared_ptr<Object> createObject(ObjectType type, const std::string &blueprintResRef, const std::shared_ptr<Location> &location);

    /**
     * End this Area's ownership of an exact still-live runtime Object.
     *
     * This removes Area indexes, Room/Trigger tenancy and presentation
     * attachment without firing authored exit behavior or ending semantic
     * object lifetime. It is the transfer seam for a world object moving into
     * another owning graph.
     */
    bool releaseObject(const std::shared_ptr<Object> &object);

    /** Whether this exact Object is currently owned by this Area. */
    bool isObjectResident(const Object &object) const;

    /** Whether this exact runtime Object is queued for semantic destruction. */
    bool isObjectPendingDestruction(const Object &object) const;

    bool isObjectSeen(const Creature &subject, const Object &object) const;
    // Geometry only: nothing in the room geometry between the two at eye height.
    bool hasLineOfSight(const Creature &subject, const Object &object) const;
    /** Whether nothing but the ignored objects blocks the line between two points. */
    bool hasLineOfSightBetween(const glm::vec3 &from, const glm::vec3 &to, const Object *ignoreA, const Object *ignoreB) const;
    /** The shut door the sight line from subject to object meets first, if that is what blocks it. */
    Door *closedDoorOnLineOfSight(const Creature &subject, const Object &object) const;
    void refreshPerceptionFor(Creature &creature);

    ObjectList &getObjectsByType(ObjectType type);
    std::shared_ptr<Object> getObjectByTag(const std::string &tag, int nth = 0) const;

    // END Objects

    // Object Search

    /**
     * Find the nth nearest object for which the specified predicate returns true.
     *
     * @param nth a 0-based object index
     */
    std::shared_ptr<Object> getNearestObject(const glm::vec3 &origin, int nth, const std::function<bool(const std::shared_ptr<Object> &)> &predicate);

    /**
     * @param nth 0-based index of the creature
     * @return nth nearest creature to the target object, that matches the specified criterias
     */
    std::shared_ptr<Creature> getNearestCreature(const std::shared_ptr<Object> &target, const SearchCriteriaList &criterias, int nth = 0);

    /**
     * @param nth 0-based index of the creature
     * @return nth nearest creature to the location, that matches the specified criterias
     */
    std::shared_ptr<Creature> getNearestCreatureToLocation(const Location &location, const SearchCriteriaList &criterias, int nth = 0);

    // END Object Search

    // Cameras

    Camera *getCamera(CameraType type);

    void setStaticCamera(int cameraId);
    void setThirdPartyCameraStyle(CameraStyleType type);

    template <class T>
    T *getCamera(CameraType type) {
        return static_cast<T *>(getCamera(type));
    };

    // END Cameras

    // Party

    /** End one retained creature's current Area lifetime. */
    void retireCreatureAreaRuntime(const std::shared_ptr<Creature> &creature);
    void retirePartyMemberAreaRuntime(const std::shared_ptr<Creature> &member);
    void loadParty(
        const glm::vec3 &position,
        float facing,
        bool preserveSavedPlacement = false);
    /** End every session-owned creature's residency in this Area. */
    void retirePartyAreaRuntime();
    /** Place a newly controlled actor without ending any existing Area lifetime. */
    void placeControlledCreature(
        const std::shared_ptr<Creature> &creature,
        const glm::vec3 &position,
        float facing);
    /** Reconcile/position the selected party without ending this Area lifetime. */
    void repositionParty(const glm::vec3 &position, float facing);
    void repositionParty();

    // END Party

    // Perception

    void updatePerception(float dt);

    /** GetLastTrapDetected: the trap a creature detected last (not saved). */
    uint32_t lastTrapDetected(uint32_t creatureId) const;
    void setLastTrapDetected(uint32_t creatureId, uint32_t trapId) { _lastTrapDetected[creatureId] = trapId; }

    // END Perception

    // Object Selection

    void hilightObject(std::shared_ptr<Object> object);
    void selectObject(std::shared_ptr<Object> object, bool force = false);

    std::shared_ptr<Object> hilightedObject() const { return _hilightedObject; }
    std::shared_ptr<Object> selectedObject() const { return _selectedObject; }

    // END Object Selection

    // Stealth

    bool isStealthXPEnabled() const { return _stealthXPEnabled; }

    int maxStealthXP() const { return _maxStealthXP; }
    int currentStealthXP() const { return _currentStealthXP; }
    int stealthXPDecrement() const { return _stealthXPDecrement; }

    void setStealthXPEnabled(bool value);
    void setMaxStealthXP(int value);
    void setCurrentStealthXP(int value);
    void setStealthXPDecrement(int value);

    /**
     * AwardStealthXP: hand the current pool to the party once, then empty the
     * pool and switch stealth XP off. Returns the amount awarded.
     */
    int awardStealthXP();

    // END Stealth

    // Fog

    bool fogEnabled() const { return _fogEnabled; }
    float fogNear() const { return _fogNear; }
    float fogFar() const { return _fogFar; }
    const glm::vec3 &fogColor() const { return _fogColor; }
    /** SetAreaFogColor: also pushes the fog to the scene. */
    void setFogColor(const glm::vec3 &color);

    // END Fog

    // Scripts

    void runSpawnScripts();
    void runOnEnterScript();
    void runOnExitScript();

    // END Scripts

    // Listeners

    MessageBus &messageBus() { return _messageBus; }
    void updateMessageBus();

    // END Listeners

    // Scene

    scene::ISceneGraph &graph();

    // END Scene

private:
    friend class CoopSession;
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    std::string _sceneName;

    Pathfinder _pathfinder;
    std::string _localizedName;
    resource::generated::ARE_Map _map;
    RoomMap _rooms;
    resource::Visibility _visibility;
    CameraStyle _camStyleDefault;
    CameraStyle _camStyleCombat;
    AreaMusic _musicState;
    std::map<std::string, float> _roomAmbientScale;
    float _combatExcitement {0.0f}; /**< seconds of battle music left */
    std::unordered_map<std::string, std::string> _roomScriptLoops;
    MapExploration _mapExploration;
    Timer _heartbeatTimer;
    bool _unescapable {false};
    uint32_t _flags {0};
    Weather _weather;
    Grass _grass;
    std::optional<MinigameSpec> _miniGameSpec;
    glm::vec3 _ambientColor {0.0f};
    Timer _perceptionTimer;
    float _trapDetectAge {0.0f};
    std::map<uint32_t, uint32_t> _lastTrapDetected;
    std::shared_ptr<Object> _hilightedObject;
    std::shared_ptr<Object> _selectedObject;
    bool _forceSelection {false};

    // Scripts

    std::string _onEnter;
    std::string _onExit;
    std::string _onHeartbeat;

    // END Scripts

    // Cameras

    std::shared_ptr<FirstPersonCamera> _firstPersonCamera;
    std::shared_ptr<ThirdPersonCamera> _thirdPersonCamera;
    std::shared_ptr<DialogCamera> _dialogCamera;
    std::shared_ptr<AnimatedCamera> _animatedCamera;
    StaticCamera *_staticCamera {nullptr};

    // END Cameras

    // Objects

    ObjectList _objects;
    std::unordered_map<ObjectType, ObjectList> _objectsByType;
    std::unordered_map<std::string, ObjectList> _objectsByTag;
    std::set<uint32_t> _objectsToDestroy;

    // END Objects

    // Listeners
    MessageBus _messageBus;
    // END Listeners

    // Stealth

    bool _stealthXPEnabled {false};
    int _maxStealthXP {0};
    int _currentStealthXP {0};
    int _stealthXPDecrement {0};

    // END Stealth

    // Fog

    bool _fogEnabled {false};
    float _fogNear {0.0f};
    float _fogFar {0.0f};
    glm::vec3 _fogColor {0.0f};

    // END Fog

    void init();

    void loadLYT();
    void loadVIS();
    void applySceneProperties();
    void attachRoomToSceneGraph(Room &room);
    void attachObjectToSceneGraph(const std::shared_ptr<Object> &object);
    void detachObjectRuntime(const std::shared_ptr<Object> &object);

    void doDestroyObject(uint32_t objectId, bool destroyRuntimeObject = true);
    void doDestroyObjects();
    void updateVisibility();
    void updateHeartbeat(float dt);

    void loadPartyMember(
        const std::shared_ptr<Creature> &member,
        int index,
        bool preserveSavedPlacement);
    void repositionPartyMember(
        const std::shared_ptr<Creature> &member,
        int index);
    glm::vec3 findPartyPosition(const Creature &member, const glm::vec3 &position) const;

    struct CreatureCollision {
        const Creature *creature {nullptr};
        float time {0.0f};
        glm::vec2 normal {0.0f};
    };

    bool findCreatureCollision(
        const Creature &creature,
        const glm::vec3 &origin,
        const glm::vec3 &destination,
        CreatureCollision &outCollision,
        const Creature *ignoredCreature = nullptr) const;

    void doUpdatePerception();
    void updatePerceptionPair(
        const std::shared_ptr<Creature> &observer,
        const std::shared_ptr<Creature> &target);
    void updateObjectSelection();

    bool matchesCriterias(const Creature &creature, const SearchCriteriaList &criterias, std::shared_ptr<Object> target = nullptr) const;

    /**
     * Certain VIS files in the original game have a bug: room A is visible from
     * room B, but room B is not visible from room A. This function makes room
     * relations symmetric.
     */
    resource::Visibility fixVisibility(const resource::Visibility &visiblity);

    void checkTriggersIntersection(const std::shared_ptr<Object> &triggerrer, bool fireTransitions = true);

    // Fire OnEnter for non-transition triggers the party leader currently
    // occupies (so triggers fire when the leader is placed/spawned inside them,
    // not only when moving across the boundary). Module-transition triggers are
    // left to movement-based firing to avoid spawn bounce.
    void updateLeaderTriggerOccupancy();

    // Loading ARE

    void loadARE(const resource::generated::ARE &are);

    void loadCameraStyle(const resource::generated::ARE &are);
    void loadAmbientColor(const resource::generated::ARE &are);
    void loadScripts(const resource::generated::ARE &are);
    void loadExploredMap(const resource::Gff &git);
    void revealAroundParty();
    void loadMap(const resource::generated::ARE &are);
    void loadStealthXP(const resource::generated::ARE &are);
    void loadGrass(const resource::generated::ARE &are);
    void loadFog(const resource::generated::ARE &are);
    void loadMiniGame(const resource::generated::ARE &are);
    void loadBehaviourFlags(const resource::generated::ARE &are);

    // END Loading ARE

    // Loading GIT

    void loadGIT(
        const resource::generated::GIT &git,
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void loadProperties(const resource::generated::GIT &git);
    void updateCombatMusic(float dt);
    void loadCreatures(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadDoors(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadPlaceables(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadWaypoints(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadTriggers(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadSounds(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadCameras(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadEncounters(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadStores(const resource::Gff &gff, const SerializedIdentityContext &identityContext);
    void loadItems(const resource::Gff &gff, const SerializedIdentityContext &identityContext);

    // END Loading GIT

    // User input

    bool handleKeyDown(const input::KeyEvent &event);

    // END User input
};

} // namespace game

} // namespace reone
