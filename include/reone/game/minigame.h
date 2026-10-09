/*
 * Copyright (c) 2020-2026 The reone project contributors
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

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace reone {

namespace resource {

namespace generated {

struct ARE;

}

} // namespace resource

namespace graphics {

class Model;

}

namespace game {

/**
 * Script slots of a minigame object, in the order the engine numbers them.
 * Obstacles only use the first five.
 */
enum class MiniGameSlot {
    OnCreate = 0,
    OnHitBullet,
    OnHitFollower,
    OnAnimEvent,
    OnHeartbeat,
    OnDamage,
    OnDeath,
    OnFire,
    OnHitObstacle,
    OnTrackLoop,
    OnAccelerate,
    OnBrake,
    OnHitWorld,
    Count
};

constexpr size_t kMiniGameSlotCount = static_cast<size_t>(MiniGameSlot::Count);

using MiniGameScripts = std::array<std::string, kMiniGameSlotCount>;

const char *miniGameSlotName(MiniGameSlot slot);

enum class MinigameType {
    None = 0,
    SwoopRace = 1,
    Turret = 2,
    Unknown
};

inline MinigameType minigameTypeFromUint(uint32_t value) {
    switch (value) {
    case 0:
        return MinigameType::None;
    case 1:
        return MinigameType::SwoopRace;
    case 2:
        return MinigameType::Turret;
    default:
        return MinigameType::Unknown;
    }
}

inline const char *minigameTypeName(MinigameType t) {
    switch (t) {
    case MinigameType::None:
        return "none";
    case MinigameType::SwoopRace:
        return "swooprace";
    case MinigameType::Turret:
        return "turret";
    default:
        return "unknown";
    }
}

struct MinigameModelSpec {
    std::string resRef;

    // Vanilla "RotatingModel": models flagged here follow the player/enemy
    // rotation; the rest stay fixed in the track frame. On the K1 turret the
    // gun and HUD models rotate while the Ebon Hawk hull does not.
    bool rotating {false};
};

struct MinigameBulletSpec {
    std::string modelResRef;
    std::string collisionSound;
    uint32_t damage {0};
    float lifespan {0.0f};    // seconds before the bullet is culled
    float rateOfFire {0.0f};  // seconds between shots from the owning bank
    float speed {0.0f};       // world units per second
    uint32_t targetType {0};  // 1 = player, 2 = enemies
};

struct MinigameGunBankSpec {
    uint32_t bankId {0};
    std::string gunModelResRef;
    std::string fireSound;
    MinigameBulletSpec bullet;

    // Enemy-only aiming parameters; zero on player banks.
    float horizSpread {0.0f};
    float vertSpread {0.0f};
    float inaccuracy {0.0f};
    float sensingRadius {0.0f};
};

struct MinigameSoundSpec {
    std::string death;
    std::string engine;
};

struct MinigamePlayerScriptSpec {
    std::string onCreate;
    std::string onDeath;
    std::string onTrackLoop;
    std::string onDamage;
    std::string onAccelerate;
    std::string onHeartbeat;
    std::string onFire;
    std::string onHitBullet;
    std::string onHitFollower;
    std::string onHitObstacle;
};

struct MinigamePlayerSpec {
    std::string cameraResRef;
    std::string trackResRef;
    std::vector<MinigameModelSpec> models;
    std::vector<MinigameGunBankSpec> gunBanks;
    float minimumSpeed {0.0f};
    float maximumSpeed {0.0f};
    float accelSecs {0.0f};
    float sphereRadius {0.0f};
    uint32_t hitPoints {0};
    uint32_t maxHitPoints {0};
    float invincePeriod {0.0f};
    int bumpDamage {0};
    int numLoops {0};
    bool cameraRotate {false};
    MinigameSoundSpec sounds;
    MinigamePlayerScriptSpec scripts;
    MiniGameScripts slots;

    // Placement of the player actor relative to its track hook, and of the
    // aim/target point relative to the actor.
    glm::vec3 startOffset {0.0f};
    glm::vec3 targetOffset {0.0f};

    // Tunnel bounds, as stored in the .are. For the swoop race these are the
    // angular limits (degrees) vanilla applies to bike lean/rotation per axis;
    // for the turret they are the aim limits (X = pitch, Z = yaw).
    float tunnelXPos {0.0f};
    float tunnelXNeg {0.0f};
    float tunnelYPos {0.0f};
    float tunnelYNeg {0.0f};
    float tunnelZPos {0.0f};
    float tunnelZNeg {0.0f};

    // Per-axis "unbounded" flags. A non-zero component means the matching
    // tunnel limits do not apply (the K1 turret sets Z, giving free 360 yaw).
    glm::vec3 tunnelInfinite {0.0f};
};

struct MinigameEnemyScriptSpec {
    std::string onCreate;
    std::string onDeath;
    std::string onDamage;
    std::string onHeartbeat;
    std::string onTrackLoop;
};

struct MinigameEnemySpec {
    std::string trackResRef;
    std::vector<MinigameModelSpec> models;
    std::vector<MinigameGunBankSpec> gunBanks;
    uint32_t hitPoints {0};
    uint32_t maxHitPoints {0};
    float sphereRadius {0.0f};
    float invincePeriod {0.0f};
    int bumpDamage {0};
    int numLoops {0};
    bool trigger {false};
    MinigameSoundSpec sounds;
    MinigameEnemyScriptSpec scripts;
    MiniGameScripts slots;

    // Kept for call sites that only need the creation hook.
    std::string onCreate;
};

struct MinigameObstacleSpec {
    std::string name;
    std::string onCreate;
    MiniGameScripts slots;
};

// Vanilla mouse axis binding for the minigame. The axis identifiers are opaque
// engine constants; only the flip flags have a confirmed meaning.
struct MinigameMouseSpec {
    uint32_t axisX {0};
    uint32_t axisY {0};
    bool flipAxisX {false};
    bool flipAxisY {false};
};

// Passive data parsed from the .are MiniGame struct. Stored on Area.
struct MinigameSpec {
    MinigameType type {MinigameType::None};
    float cameraViewAngle {0.0f};
    float nearClip {0.0f};
    float farClip {0.0f};
    float lateralAccel {0.0f};
    float movementPerSec {0.0f};
    bool useInertia {false};
    uint32_t bumpPlane {0};
    uint32_t depthOfField {0};
    bool doBumping {false};
    std::string music;
    MinigameMouseSpec mouse;
    MinigamePlayerSpec player;
    std::vector<MinigameEnemySpec> enemies;
    std::vector<MinigameObstacleSpec> obstacles;

    // Unique track resrefs referenced by player and enemies.
    // Derived at parse time; not stored separately in the .are format.
    std::vector<std::string> trackResRefs;
};

/**
 * Convert the MiniGame struct of a parsed .are into engine-side metadata.
 *
 * Returns nullopt when the area declares no minigame (Type 0).
 */
std::optional<MinigameSpec> parseMinigameSpec(const resource::generated::ARE &are);

/**
 * The only way player intent reaches a minigame. Local keyboard and mouse
 * handlers, the harness autopilot and (later) a remote co-op player all write
 * this; the session reads it once per tick. Whoever started the minigame owns
 * it, everyone else watches.
 */
struct MiniGameInput {
    // Lateral steering (swoop), -1 = full left .. +1 = full right.
    float steer {0.0f};

    // Fire held. On the swoop this is the gear-shift key.
    bool fire {false};

    // Turret aim rates, degrees per second, positive = up / left.
    float pitchRate {0.0f};
    float yawRate {0.0f};

    // One-shot aim deltas in degrees (mouse), consumed by the next tick.
    float pitchDelta {0.0f};
    float yawDelta {0.0f};
};

/**
 * A track's hook path, sampled from the track model's "track" animation. The
 * simulation reads positions from here, never from scene nodes, so it steps
 * identically at any frame rate.
 */
class MiniGameTrack {
public:
    struct PositionKey {
        float time {0.0f};
        glm::vec3 value {0.0f};
    };
    struct OrientationKey {
        float time {0.0f};
        glm::quat value {1.0f, 0.0f, 0.0f, 0.0f};
    };

    MiniGameTrack() = default;

    /**
     * @param base world transform of the hook's parent (track placement times
     *             the rest pose of the hook's ancestors)
     * @param restPosition the hook's rest position in its parent
     * @param restOrientation the hook's rest orientation
     * @param positions animation position deltas (added to the rest position)
     * @param orientations animation orientations (replace the rest one)
     * @param length animation length in seconds; 0 for a static track
     */
    MiniGameTrack(glm::mat4 base,
                  glm::vec3 restPosition,
                  glm::quat restOrientation,
                  std::vector<PositionKey> positions,
                  std::vector<OrientationKey> orientations,
                  float length);

    /** A static hook at a world position. */
    static MiniGameTrack fixed(const glm::vec3 &position);

    /** World transform of the hook at a track time. */
    glm::mat4 hookAt(float time) const;

    float length() const { return _length; }

private:
    glm::mat4 _base {1.0f};
    glm::vec3 _restPosition {0.0f};
    glm::quat _restOrientation {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<PositionKey> _positions;
    std::vector<OrientationKey> _orientations;
    float _length {0.0f};
};

/**
 * Build the hook path of a track model placed at \p placement. A model without
 * a "modelhook" node, or a missing model, gives a static hook at the placement.
 */
MiniGameTrack buildMiniGameTrack(const graphics::Model *model, const glm::vec3 &placement);

enum class MiniGameActorKind {
    Player,
    Enemy,
    Obstacle
};

struct MiniGameGunBank {
    MinigameGunBankSpec spec;
    float cooldown {0.0f};
};

/**
 * One minigame object: the player, an enemy (follower) or an obstacle. The
 * script-visible state lives here; objectId is the registered game object
 * scripts see as OBJECT_SELF.
 */
struct MiniGameActor {
    MiniGameActorKind kind {MiniGameActorKind::Enemy};
    size_t specIndex {0};
    uint32_t objectId {0};
    std::string name;
    std::vector<MinigameModelSpec> models;
    MiniGameScripts scripts;
    MinigameSoundSpec sounds;

    int hitPoints {100};
    int maxHitPoints {100};
    float sphereRadius {0.0f};
    float invincePeriod {0.0f};
    float invulnerableTime {0.0f};
    int bumpDamage {0};
    int numLoops {1};
    bool trigger {false};
    float speed {100.0f};

    bool dead {false};
    bool listed {true}; // still in the enemy / obstacle list

    // Positioned by the presentation (the turret's scene rails) rather than
    // by sampling the track here.
    bool drivenExternally {false};

    MiniGameTrack track;
    float trackTime {0.0f};
    bool trackRunning {true};
    glm::mat4 hook {1.0f};
    glm::vec3 position {0.0f};
    glm::vec3 previousPosition {0.0f};

    uint32_t lastFollowerHit {0};
    uint32_t lastObstacleHit {0};
    int lastHPChange {0};
    int lastBulletHitDamage {0};
    int lastBulletHitTarget {0};
    uint32_t lastBulletHitShooter {0};
    std::string lastBulletHitPart;
    int lastBulletFiredDamage {0};
    int lastBulletFiredTarget {0};
    std::string lastEvent;
    std::string lastEventModelName;

    std::vector<MiniGameGunBank> gunBanks;

    // Per-sound playback settings (0 = death, 1 = engine).
    std::array<int, 2> soundFrequency {0, 0};
    std::array<int, 2> soundFrequencyRandom {0, 0};
    std::array<int, 2> soundVolume {127, 127};

    // Obstacle collision geometry (the model's "hitbump"), world space.
    bool hasHitbump {false};
    glm::vec3 hitbumpMin {0.0f};
    glm::vec3 hitbumpMax {0.0f};

    bool follower() const { return kind != MiniGameActorKind::Obstacle; }
    bool invulnerable() const { return invulnerableTime > 0.0f; }
};

/**
 * Presentation of a minigame: the swoop and turret views implement this. The
 * session calls it for script-requested animations, sounds, the default death
 * and gun fire; none of it feeds back into the simulation.
 */
class IMiniGameView {
public:
    virtual ~IMiniGameView() = default;

    virtual void playMiniGameAnimation(const MiniGameActor &actor,
                                       const std::string &name,
                                       bool looping,
                                       bool queue,
                                       bool overlay) {}
    virtual void removeMiniGameAnimation(const MiniGameActor &actor, const std::string &name) {}
    virtual void playMiniGameSound(const std::string &resRef, const glm::vec3 &position) {}
    virtual void onMiniGameActorDied(const MiniGameActor &actor) {}
    virtual void onMiniGameGunFired(const MiniGameActor &actor, size_t bankIndex) {}
};

/**
 * What the session needs from the engine: script execution, plus the view.
 * The simulation itself has no engine or scene dependency.
 */
class IMiniGameHost : public IMiniGameView {
public:
    virtual void runScript(const std::string &resRef, uint32_t callerId) = 0;
};

/**
 * The minigame simulation: actors, player speed and steering, tracks,
 * collisions, hit points and the script slots. Runs once per fixed tick.
 */
class MiniGameSession {
public:
    struct ObstaclePlacement {
        std::string name;
        glm::vec3 position {0.0f};
        bool hasHitbump {false};
        glm::vec3 hitbumpMin {0.0f}; // world space
        glm::vec3 hitbumpMax {0.0f};
    };

    explicit MiniGameSession(IMiniGameHost &host) :
        _host(host) {
    }

    /**
     * Build the actors from the area data. The enemy tracks follow the order
     * of spec.enemies. Object ids are assigned afterwards with setObjectId,
     * then start() runs the creation scripts.
     */
    void load(const MinigameSpec &spec,
              MiniGameTrack playerTrack,
              std::vector<MiniGameTrack> enemyTracks,
              std::vector<ObstaclePlacement> obstacles);

    void setObjectId(size_t actorIndex, uint32_t id) { _actors[actorIndex].objectId = id; }

    /** Run every follower's OnCreate: the player first, then the enemies. */
    void start();

    /** One fixed simulation step. */
    void update(float dt, const MiniGameInput &input);

    // Events (also used by the SWMG_ routines)

    /** A hit point change: stored as the last change, then OnDamage or the default. */
    void changeHitPoints(MiniGameActor &actor, int delta);

    /** SWMG_OnDamage: apply the stored change. */
    void defaultDamage(MiniGameActor &actor);

    /** Kill once: OnDeath or the default. */
    void kill(MiniGameActor &actor);

    /** SWMG_OnDeath. */
    void defaultDeath(MiniGameActor &actor);

    /** A bullet reached \p target: OnHitBullet or the default. */
    void bulletHit(MiniGameActor &target, int damage, int targetType, uint32_t shooter, const std::string &part);

    /** SWMG_OnBulletHit. */
    void defaultBulletHit(MiniGameActor &target);

    /** SWMG_OnObstacleHit: the default for the follower that hit an obstacle. */
    void defaultObstacleHit(MiniGameActor &follower);

    /** A gun bank of \p actor fired (used by the turret's enemy guns). */
    void gunFired(MiniGameActor &actor, size_t bankIndex);

    void playAnimation(MiniGameActor &actor, const std::string &name, bool looping, bool queue, bool overlay);
    void removeAnimation(MiniGameActor &actor, const std::string &name);

    // Player motion

    void setPlayerSpeed(float speed);
    void setPlayerMaxSpeed(float speed);
    void setPlayerOffset(const glm::vec3 &offset);
    void setPlayerOrigin(const glm::vec3 &origin);

    /** The turret drives its aim itself and reports it as the offset (degrees). */
    void setPlayerAim(const glm::vec3 &offsetDegrees) { _offset = offsetDegrees; }

    /** The turret moves enemies on scene rails and reports their positions. */
    void setActorPosition(MiniGameActor &actor, const glm::vec3 &position);

    // Lookups

    MiniGameActor *actorByObjectId(uint32_t id);
    MiniGameActor *player();
    const MiniGameActor *player() const;
    MiniGameActor *enemy(int index);
    MiniGameActor *obstacle(int index);
    MiniGameActor *findByName(const std::string &name);
    int enemyCount() const { return static_cast<int>(_enemyList.size()); }
    int obstacleCount() const { return static_cast<int>(_obstacleList.size()); }
    std::vector<MiniGameActor> &actors() { return _actors; }
    const std::vector<MiniGameActor> &actors() const { return _actors; }

    MinigameType type() const { return _type; }
    uint64_t tickCount() const { return _ticks; }
    float elapsed() const { return _elapsed; }

    float minSpeed() const { return _minSpeed; }
    float maxSpeed() const { return _maxSpeed; }
    float accelPerSecond() const { return _accelPerSecond; }
    float playerSpeed() const;
    const glm::vec3 &offset() const { return _offset; }
    const glm::vec3 &origin() const { return _startOffset; }
    const glm::vec3 &targetOffset() const { return _targetOffset; }
    const glm::vec3 &tunnelPos() const { return _tunnelPos; }
    const glm::vec3 &tunnelNeg() const { return _tunnelNeg; }
    const glm::vec3 &tunnelInfinite() const { return _tunnelInfinite; }
    float lateralAccel() const { return _lateralAccel; }
    float movementPerSec() const { return _movementPerSec; }
    const glm::vec3 &lateralVelocity() const { return _lateralVelocity; }
    bool speedBlurEnabled() const { return _speedBlur; }
    float speedBlurRatio() const { return _speedBlurRatio; }
    float nearClip() const { return _nearClip; }
    float farClip() const { return _farClip; }

    void setMinSpeed(float speed) { _minSpeed = speed; }
    void setAccelPerSecond(float accel) { _accelPerSecond = accel; }
    void setTunnelPos(const glm::vec3 &v) { _tunnelPos = v; }
    void setTunnelNeg(const glm::vec3 &v) { _tunnelNeg = v; }
    void setTunnelInfinite(const glm::vec3 &v) { _tunnelInfinite = v; }
    void setLateralAccel(float accel) { _lateralAccel = accel; }
    void setSpeedBlur(bool enabled, float ratio) {
        _speedBlur = enabled;
        _speedBlurRatio = ratio;
    }
    void setCameraClip(float nearClip, float farClip) {
        _nearClip = nearClip;
        _farClip = farClip;
    }

    // Counters for diagnostics and tests
    size_t scriptsRun() const { return _scriptsRun; }

private:
    IMiniGameHost &_host;

    MinigameType _type {MinigameType::None};
    std::vector<MiniGameActor> _actors;
    std::vector<size_t> _enemyList;
    std::vector<size_t> _obstacleList;

    float _lateralAccel {0.0f};
    float _movementPerSec {0.0f};
    bool _useInertia {false};
    bool _doBumping {false};
    bool _speedBlur {false};
    float _speedBlurRatio {0.0f};
    float _nearClip {0.0f};
    float _farClip {0.0f};

    float _minSpeed {0.0f};
    float _maxSpeed {0.0f};
    float _accelPerSecond {0.0f};
    glm::vec3 _offset {0.0f};
    glm::vec3 _startOffset {0.0f};
    glm::vec3 _targetOffset {0.0f};
    glm::vec3 _tunnelPos {0.0f};
    glm::vec3 _tunnelNeg {0.0f};
    glm::vec3 _tunnelInfinite {0.0f};
    glm::vec3 _lateralVelocity {0.0f};
    glm::vec3 _previousInput {0.0f};
    bool _speedHeld {false};

    uint64_t _ticks {0};
    float _elapsed {0.0f};
    size_t _scriptsRun {0};
    bool _started {false};

    bool runScriptSlot(MiniGameActor &actor, MiniGameSlot slot);
    void clampOffset(glm::vec3 &offset) const;
    void updatePlayerMotion(float dt, const MiniGameInput &input);
    void advanceTrack(MiniGameActor &actor, float dt);
    void placeActor(MiniGameActor &actor);
    void updateFollower(MiniGameActor &actor, float dt);
    void updatePlayerGuns(float dt, bool fire);
    void collideObstacles();
    void collideFollowers();
    void unlist(MiniGameActor &actor);
};

/** Distance between two line segments. */
float miniGameSegmentDistance(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &q0, const glm::vec3 &q1);

/**
 * Whether a segment crosses a box, tested in the ground plane (the swoop
 * obstacle geometry is authored flat, level with the course).
 */
bool miniGameSegmentHitsBox2D(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &boxMin, const glm::vec3 &boxMax);

/**
 * Pazaak card index (0-based, into the collection counts) for a card item's
 * ModelVariation, or -1 when it is not a card the title knows.
 */
int pazaakCardIndexForModelVariation(int modelVariation, bool tsl);

} // namespace game

} // namespace reone
