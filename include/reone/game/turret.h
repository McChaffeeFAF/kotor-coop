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

#include <limits>
#include <map>

#include "reone/input/event.h"

#include "minigame.h"
#include "minigameview.h"

namespace reone {

namespace audio {

class AudioClip;

}

namespace scene {

class ModelSceneNode;

}

namespace game {

class Game;
class FirstPersonCamera;
struct ServicesView;

/**
 * Aim state of the player turret, in radians.
 *
 * Vanilla stores the aim limits in the .are MiniGame Player "Tunnel" fields:
 * X is the pitch axis and Z is the yaw axis (Y is unused by the turret). The
 * stored values are signed degree limits, so the usable range of an axis is
 * [Neg, Pos] rather than [-Neg, +Pos]; K1's Ebon Hawk turret pitches between
 * TunnelXNeg = 2 and TunnelXPos = 45 degrees. An axis whose TunnelInfinite
 * component is set is unbounded and wraps instead of clamping, which is how the
 * turret gets free 360 degree yaw.
 */
class TurretAim {
public:
    void configure(const MinigamePlayerSpec &player);

    /**
     * Return to the authored starting aim. Replaying a session must not inherit
     * the previous run's aim.
     */
    void reset();

    void addPitch(float radians);
    void addYaw(float radians);

    float pitch() const { return _pitch; }
    float yaw() const { return _yaw; }

    // Authored starting aim, already clamped into the authored bounds.
    float startPitch() const { return _startPitch; }
    float startYaw() const { return _startYaw; }

    // How far an axis can travel, in radians; a full turn when unbounded.
    float pitchTravel() const;
    float yawTravel() const;

    /**
     * Unit vector the turret is pointing along, in the track frame. Odyssey
     * models look down +Y, so this is Rz(yaw) * Rx(pitch) applied to +Y.
     */
    glm::vec3 forward() const;

    glm::quat orientation() const;

    bool pitchBounded() const { return _pitchBounded; }
    bool yawBounded() const { return _yawBounded; }
    float minPitch() const { return _minPitch; }
    float maxPitch() const { return _maxPitch; }
    float minYaw() const { return _minYaw; }
    float maxYaw() const { return _maxYaw; }

private:
    float _pitch {0.0f};
    float _yaw {0.0f};
    float _startPitch {0.0f};
    float _startYaw {0.0f};

    bool _pitchBounded {true};
    bool _yawBounded {true};
    float _minPitch {0.0f};
    float _maxPitch {0.0f};
    float _minYaw {0.0f};
    float _maxYaw {0.0f};
};

/**
 * Turn rate of one aim axis while a key is held.
 *
 * This is reone's existing keyboard rotation model (ThirdPersonCamera): a held
 * key starts at a minimum rate and accelerates to a cap, so a tap gives fine
 * adjustment while a hold sweeps. The rates are scaled by the axis's authored
 * travel and calibrated so an unbounded axis reproduces ThirdPersonCamera's
 * rates exactly; a narrow axis such as the turret's 43 degree pitch band then
 * moves proportionally slower, and both axes cross their full authored travel
 * in about the same time.
 *
 * The rate is integrated analytically rather than stepped, so the angle covered
 * over a given interval does not depend on how many frames it is split into.
 */
class TurretAimRate {
public:
    void configure(float travelRadians);

    /**
     * -1, 0 or +1. Engaging an axis, or reversing it, restarts the acceleration
     * ramp from the minimum rate.
     */
    void setDirection(int direction);

    /**
     * Angle to apply this step, signed by the held direction; zero when idle.
     */
    float advance(float dt);

    /** Drop any held direction and accumulated acceleration. */
    void reset();

    int direction() const { return _direction; }
    float minRate() const { return _minRate; }
    float maxRate() const { return _maxRate; }
    float rate() const;

private:
    float _minRate {0.0f};
    float _maxRate {0.0f};
    float _acceleration {0.0f};
    int _direction {0};
    float _held {0.0f}; // seconds the current direction has been engaged

    float rateAt(float held) const;
    float angleOver(float from, float to) const;
};

/**
 * Rate-of-fire gate for a single gun bank. Vanilla stores the interval between
 * shots in the bank's Bullet.Rate_Of_Fire.
 */
class TurretGunTimer {
public:
    explicit TurretGunTimer(float rateOfFire = 0.0f) :
        _rateOfFire(rateOfFire) {
    }

    void update(float dt);

    bool ready() const { return _cooldown <= 0.0f; }

    /**
     * Consume the gate: returns false (and changes nothing) when still cooling
     * down, otherwise arms the cooldown and returns true.
     */
    bool tryFire();

    float cooldown() const { return _cooldown; }
    float rateOfFire() const { return _rateOfFire; }

private:
    float _rateOfFire {0.0f};
    float _cooldown {0.0f};
};

/**
 * Scene-free state of a bullet in flight.
 */
struct TurretBullet {
    glm::vec3 position {0.0f};
    glm::vec3 direction {0.0f, 1.0f, 0.0f};

    // World orientation of the firing actor at the moment of the shot. Authored
    // bolt models are modelled along +Y, so this is what makes the bolt's long
    // axis lie along its travel instead of along world Y.
    glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};

    float speed {0.0f};
    float life {0.0f};
    float lifespan {0.0f};
    uint32_t damage {0};
    bool fromPlayer {false};

    // Set on creation, cleared by the first advance(). Firing and bullet
    // integration run in the same tick, so without this a bolt is carried
    // speed*dt down range before it is ever drawn and never appears at the
    // barrel it came from. Holding the first step presents the authored muzzle
    // transform once; flight then proceeds normally.
    bool atMuzzle {true};

    /**
     * Advance the bullet by one step. Returns false once it has outlived its
     * lifespan and should be culled.
     *
     * The first call after spawning consumes the muzzle frame and does not
     * integrate; every later call moves the bullet.
     */
    bool advance(float dt);

    /**
     * Mark the bullet spent so the next cull pass removes it.
     */
    void expire() { life = std::numeric_limits<float>::max(); }
};

/**
 * Whether a destroyed fighter has finished presenting its death effect.
 *
 * The shipped fighter carries its destruction as a non-looping "die" animation
 * (mgf_sithfighter authors 2.3s), so the animation's own length is how long the
 * wreck is meant to stay on screen. A fighter with no authored death animation
 * has nothing to wait for and is released at once.
 */
bool turretDeathEffectComplete(float elapsed, float duration);

/**
 * Switch off the always-on effect models a minigame actor carries.
 *
 * The model loader resolves every authored reference node into a separate
 * model and attaches it there; on mgf_sithfighter those are the fx_ref engine
 * flares hanging at its OmenRef nodes, which burn for as long as the node
 * lives because nothing animates them. A fighter that has just been destroyed
 * should stop showing engine glow at once, while its hull death animation and
 * the emitters that animation detonates play out.
 *
 * Only loader-built reference attachments are touched, so the actor's meshes,
 * lights, emitters and any gun bank models hooked on by the minigame are left
 * running. Identifying them by the authored reference flag keeps this free of
 * per-model node names.
 */
void turretDisableReferenceAttachments(scene::ModelSceneNode &model);

/**
 * How far along its own forward axis a bolt must start so that none of its
 * visible geometry lies behind the muzzle it was fired from.
 *
 * A bolt model's origin is not its tail. The shipped Ebon Hawk bolt
 * (mgb_ebonleft) draws from -1.953 to +8.620 along model +Y, so pinning the
 * origin to the authored bullethook0 buries nearly two units of bolt inside the
 * gun. Starting the bolt ahead by however far it reaches backwards puts its
 * tail on the muzzle plane instead.
 *
 * \param modelMinForward the model-space minimum of the bolt's visible bounds
 *                        along the model forward axis (+Y)
 * \return a non-negative displacement; zero for a bolt drawn wholly ahead of
 *         its origin
 */
float turretMuzzleClearance(float modelMinForward);

/**
 * Direction an actor with this world orientation fires along. Odyssey models
 * look down +Y, so both the bolt's travel and its visual long axis come from the
 * same orientation - which is also why every gun bank of one actor fires
 * symmetrically, differing only in muzzle position.
 */
glm::vec3 turretFireDirection(const glm::quat &shooterOrientation);

/**
 * World transform of a bullet in flight: its position, oriented by the actor
 * that fired it. Kept separate from the scene node so it can be checked without
 * a graphics context.
 */
glm::mat4 turretBulletTransform(const TurretBullet &bullet);

bool sphereContainsPoint(const glm::vec3 &center, float radius, const glm::vec3 &point);

/**
 * True when the ray from \p origin along \p direction (assumed normalized)
 * enters the sphere ahead of the origin, within \p maxDistance.
 */
bool rayIntersectsSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float maxDistance);

/**
 * Presentation and aiming of the turret minigame (.are MiniGame Type 2).
 *
 * The player is a gun turret riding an authored rail: the player track model
 * animates a "modelhook" node, and everything is parented to it, so the whole
 * scene flies past while the player only aims and shoots. Enemies ride their
 * own tracks the same way.
 *
 * Player models are split by the .are "RotatingModel" flag: flagged models (the
 * gun, the HUD panes) follow the aim, the rest (the Ebon Hawk hull) stay fixed
 * in the track frame. Gun banks mount on the rotating group's "gunbankN" hooks
 * and spawn bullets from the gun model's "bullethook0".
 *
 * Hit points, damage, deaths, the cockpit HUD and the outcome belong to the
 * MiniGameSession and the module's scripts (k_pebo_hawkhit, k_heartbeat,
 * k_pebo_sthcreate, k_pebo_sthdeathN); this class reports aim, positions,
 * shots and bullet hits to the session and draws what the scripts ask for.
 */
class Turret : public IMiniGameView {
public:
    struct GunBank {
        const MinigameGunBankSpec *spec {nullptr};
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        TurretGunTimer timer;
    };

    struct Enemy {
        const MinigameEnemySpec *spec {nullptr};
        size_t actorIndex {0};
        std::shared_ptr<scene::ModelSceneNode> trackNode;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::vector<std::shared_ptr<scene::ModelSceneNode>> extraNodes;
        std::vector<GunBank> gunBanks;
        glm::vec3 position {0.0f};
        glm::vec3 previousPosition {0.0f};
        glm::quat orientation {1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 forward {0.0f, 1.0f, 0.0f};
        bool alive {true};
        bool deathHandled {false};

        // Death effect bookkeeping. A destroyed fighter keeps playing its
        // authored "die" animation for that animation's own length, after which
        // everything it owns leaves the scene.
        float deathElapsed {0.0f};
        float deathDuration {0.0f};
        bool nodesReleased {false};
        bool detachedFromTrack {false};
    };

    struct Bullet {
        TurretBullet sim;
        std::shared_ptr<scene::ModelSceneNode> modelNode;
        std::string modelResRef;
        std::string collisionSound;
        uint32_t targetType {0};
        size_t shooterActor {0};
    };

    enum class Outcome {
        InProgress,
        Won,
        Lost
    };

    Turret(Game &game, ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Load the minigame actors for the spec and activate the turret.
     *
     * @param camera the area first-person camera, reused as the turret camera; may be null
     * @param areaName the area resref, used to look up the LYT track placements
     * @return true when the turret became active
     */
    bool start(MiniGameSession &session, const MinigameSpec &spec, FirstPersonCamera *camera, const std::string &areaName);

    /**
     * Deactivate the turret and remove everything it added to the scene.
     */
    void stop();

    /** Before the session step: aim from the input seam, positions to the session. */
    void prepare(float dt);

    /** After the session step: enemy guns, bullets, death effects, HUD animations. */
    void update(float dt);

    bool handle(const input::Event &event);

    bool isActive() const { return _active; }

    // IMiniGameView

    void playMiniGameAnimation(const MiniGameActor &actor,
                               const std::string &name,
                               bool looping,
                               bool queue,
                               bool overlay) override;
    void removeMiniGameAnimation(const MiniGameActor &actor, const std::string &name) override;
    void playMiniGameSound(const std::string &resRef, const glm::vec3 &position) override;
    void onMiniGameActorDied(const MiniGameActor &actor) override;
    void onMiniGameGunFired(const MiniGameActor &actor, size_t bankIndex) override;

    // END IMiniGameView

    // Diagnostics

    /** Won once every fighter is destroyed, lost once the hawk is. */
    Outcome outcome() const;
    bool finished() const { return outcome() != Outcome::InProgress; }

    const TurretAim &aim() const { return _aim; }
    glm::vec3 position() const { return _anchorPosition; }
    const glm::mat4 &anchorTransform() const { return _anchorTransform; }
    glm::mat4 eyeTransform() const;
    int hitPoints() const;
    int maxHitPoints() const;
    float elapsed() const { return _elapsed; }
    size_t enemyCount() const { return _enemies.size(); }
    size_t enemiesAlive() const;
    const std::vector<Enemy> &enemies() const { return _enemies; }
    size_t bulletCount() const { return _bullets.size(); }
    size_t gunBankCount() const { return _gunBanks.size(); }
    float playerBulletSpeed() const;
    size_t shotsFired() const { return _shotsFired; }
    size_t hitsLanded() const { return _hitsLanded; }
    size_t hitsTaken() const { return _hitsTaken; }
    const std::string &trackResRef() const { return _trackResRef; }
    const std::string &anchorSource() const { return _anchorSource; }

    // Eye offset the camera mount contributes, in the rotating group's frame.
    glm::vec3 cameraHookOffset() const { return glm::vec3(_cameraHookLocal[3]); }
    bool haveCameraHook() const { return _haveCameraHook; }

    // END Diagnostics

private:
    Game &_game;
    ServicesView &_services;

    bool _active {false};
    MiniGameSession *_session {nullptr};

    // Owned copy of the area metadata: the runtime holds pointers into its
    // enemy and gun bank lists, which must outlive the area on a module change.
    MinigameSpec _spec;

    std::string _areaName;
    std::string _trackResRef;
    std::string _anchorSource;

    FirstPersonCamera *_camera {nullptr};

    // Player actors

    std::shared_ptr<scene::ModelSceneNode> _playerTrackNode;

    // Root of the models that stay fixed in the track frame (the hull).
    std::shared_ptr<scene::ModelSceneNode> _bodyRoot;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _bodyChildNodes;

    // Root of the models that follow the aim (the gun, the HUD).
    std::shared_ptr<scene::ModelSceneNode> _turretRoot;
    std::vector<std::shared_ptr<scene::ModelSceneNode>> _turretChildNodes;

    // Static model-space transform of the camera mount's "camerahook" node. The
    // mount model itself is not added to the scene.
    glm::mat4 _cameraHookLocal {1.0f};
    bool _haveCameraHook {false};

    std::vector<GunBank> _gunBanks;
    std::vector<Enemy> _enemies;
    std::vector<Bullet> _bullets;

    // Culled bullet models, kept for reuse: the scene graph owns every node it
    // creates until the graph is cleared, so a node per shot would accumulate
    // for the whole minigame.
    std::map<std::string, std::vector<std::shared_ptr<scene::ModelSceneNode>>> _bulletNodePool;

    MiniGameAnimator _animator;

    // Runtime state

    TurretAim _aim;
    TurretAimRate _pitchRate;
    TurretAimRate _yawRate;
    glm::vec3 _anchorPosition {0.0f};
    glm::mat4 _anchorTransform {1.0f};
    float _playerSphereRadius {0.0f};
    float _elapsed {0.0f};
    size_t _shotsFired {0};
    size_t _hitsLanded {0};
    size_t _hitsTaken {0};

    // Local key state feeding the input seam.
    int _keyPitch {0};
    int _keyYaw {0};
    bool _fireKey {false};
    bool _fireButton {false};

    // Loading

    std::shared_ptr<scene::ModelSceneNode> loadModel(const std::string &resRef);
    std::shared_ptr<scene::ModelSceneNode> loadTrack(const std::string &resRef,
                                                     const glm::vec3 &position);
    bool loadPlayer(const MinigameSpec &spec);
    void loadEnemies(const MinigameSpec &spec);

    /**
     * Retire everything a fighter owns: its gun bank models, its model root
     * with the reference models, lights and emitters the loader built under it,
     * and its rail. Safe to call twice.
     */
    void releaseEnemyNodes(Enemy &enemy);

    /**
     * Take a destroyed fighter off its flight rail, keeping the world pose it
     * died in, and re-root it alongside the tracks.
     *
     * Emitter particles are children of the emitter that spawned them, so a
     * wreck still attached to the looping rail hook carries its whole explosion
     * with it. Detaching stops the rail translating the fighter; the authored
     * "die" animation still plays on top. Safe to call twice.
     */
    void detachEnemyFromTrack(Enemy &enemy);
    void loadGunBanks(const std::vector<MinigameGunBankSpec> &specs,
                      scene::ModelSceneNode &mount,
                      std::vector<GunBank> &out);

    // Simulation

    void updateAnchor();
    void updatePlayerTransforms();
    void updateCamera();
    void updateEnemies(float dt);
    void updateBullets(float dt);
    void spawnBullet(const MinigameGunBankSpec &bank,
                     const glm::vec3 &origin,
                     const glm::quat &orientation,
                     bool fromPlayer,
                     size_t shooterActor);
    void playSound(const std::string &resRef);
    std::vector<scene::ModelSceneNode *> playerModels();
    Enemy *enemyForActor(const MiniGameActor &actor);

    // Hide the authored canopy glass shells for this session's cockpit only.
    void suppressCanopyGlass();

    void detachAll();
    std::shared_ptr<scene::ModelSceneNode> acquireBulletNode(const std::string &resRef);
    void releaseBulletNode(Bullet &bullet);
    void setCameraFieldOfView(float fovDegrees);

    // Input

    void publishInput();
    bool handleKeyDown(const input::KeyEvent &event);
    bool handleKeyUp(const input::KeyEvent &event);
    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButton(const input::MouseButtonEvent &event);

    float cameraAspect() const;
};

inline const char *turretOutcomeName(Turret::Outcome outcome) {
    switch (outcome) {
    case Turret::Outcome::Won:
        return "won";
    case Turret::Outcome::Lost:
        return "lost";
    default:
        return "aborted";
    }
}

} // namespace game

} // namespace reone
