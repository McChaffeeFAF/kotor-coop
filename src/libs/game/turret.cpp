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

#include "reone/game/turret.h"

#include <array>
#include <cmath>
#include <stack>

#include <glm/gtc/constants.hpp>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/camera/firstperson.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/types.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/models.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/node/model.h"
#include "reone/system/logutil.h"
#include "reone/system/portablemath.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

namespace {

// Odyssey models look down +Y.
const glm::vec3 kModelForward(0.0f, 1.0f, 0.0f);

// reone's existing keyboard rotation rates (ThirdPersonCamera), used here as the
// calibration point for a full-turn axis. MiniGame.LateralAccel is deliberately
// not used: the swoop race authors 300 and steers laterally with it, the turret
// authors 1200 and never translates at all, and KotOR.js ignores it for the
// turret too, so it has no confirmed turret aim meaning.
constexpr float kTurnRateMin = 1.0f;      // radians per second
constexpr float kTurnRateMax = 2.5f;      // radians per second
constexpr float kTurnAcceleration = 1.0f; // radians per second squared

// Mouse aim sensitivity, matching FirstPersonCamera's kMouseMultiplier so the
// turret feels like the rest of the engine.
const float kMouseAimRadiansPerPixel = glm::pi<float>() / 4000.0f;

// Engine default field of view, restored to the reused first-person camera on
// exit (mirrors Area's default camera FOV).
constexpr float kDefaultCameraFovDegrees = 75.0f;

// Distance an enemy shoots from when its gun bank declares no sensing radius.
constexpr float kFallbackSensingRadius = 200.0f;

// Names of the hook nodes vanilla uses to assemble a minigame actor.
const std::string kModelHookName("modelhook");
const std::string kCameraHookName("camerahook");
const std::string kBulletHookName("bullethook0");

std::string gunBankHookName(uint32_t bankId) {
    return str(boost::format("gunbank%u") % bankId);
}

glm::vec3 transformOrigin(const glm::mat4 &transform) {
    return glm::vec3(transform[3]);
}

glm::vec3 transformForward(const glm::mat4 &transform) {
    glm::vec3 forward(glm::mat3(transform) * kModelForward);
    float length = glm::length(forward);
    return length > 0.0f ? forward / length : kModelForward;
}

// Rotation of a world transform, with any scale divided out.
glm::quat transformOrientation(const glm::mat4 &transform) {
    glm::mat3 basis(transform);
    for (int i = 0; i < 3; ++i) {
        float length = glm::length(basis[i]);
        basis[i] = length > 0.0f ? basis[i] / length : glm::vec3(0.0f);
    }
    return glm::normalize(glm::quat_cast(basis));
}

// Minigame actors idle on a looping "ready" animation (the fighters bank and
// the turret lights cycle). Names differ between actor and gun models.
void playIdleAnimation(ModelSceneNode &node) {
    static const std::array<const char *, 2> kIdleNames {"ready_01", "ready"};
    for (const auto *name : kIdleNames) {
        if (node.model().getAnimation(name)) {
            node.playAnimation(name, nullptr,
                               AnimationProperties::fromFlags(AnimationFlags::loop));
            return;
        }
    }
}

/**
 * Silence a projectile's emitters for ordinary flight.
 *
 * EmitterSceneNode latches its birth rate from the model at time zero and, in
 * Fountain mode, then emits for as long as the node lives - no animation is
 * involved. The shipped bolt models carry the explosion presentation as
 * emitters (mgb_ebonleft has plume, rim and inner nodes textured
 * LMG_explplume01 / LMG_explrim01), so left alone every bolt trails an
 * explosion for its whole flight and sustained fire fills the cockpit. Vanilla
 * only shows those on the authored explosion event, which reone reaches through
 * ModelSceneNode::signalEvent("detonate"), so free-running emission on a bolt in
 * flight is never wanted. Mesh geometry - the visible bolt itself - is
 * untouched.
 */
void silenceEmitters(SceneNode &node) {
    for (auto &child : node.children()) {
        if (child->type() == SceneNodeType::Emitter) {
            child->setEnabled(false);
            continue;
        }
        silenceEmitters(*child);
    }
}

float normalizeAngle(float radians) {
    while (radians > glm::pi<float>()) {
        radians -= glm::two_pi<float>();
    }
    while (radians < -glm::pi<float>()) {
        radians += glm::two_pi<float>();
    }
    return radians;
}

} // namespace

void TurretAim::configure(const MinigamePlayerSpec &player) {
    // The .are stores signed degree limits per axis, so the usable range is
    // [Neg, Pos]; take the min/max in case an area authors them the other way
    // round. An axis flagged in TunnelInfinite is unbounded and wraps instead.
    _pitchBounded = player.tunnelInfinite.x == 0.0f;
    _yawBounded = player.tunnelInfinite.z == 0.0f;
    _minPitch = glm::radians(glm::min(player.tunnelXNeg, player.tunnelXPos));
    _maxPitch = glm::radians(glm::max(player.tunnelXNeg, player.tunnelXPos));
    _minYaw = glm::radians(glm::min(player.tunnelZNeg, player.tunnelZPos));
    _maxYaw = glm::radians(glm::max(player.tunnelZNeg, player.tunnelZPos));

    // Start_Offset is the authored starting aim, on the same axes and in the
    // same units as the Tunnel limits: X is pitch, Z is yaw. K1 authors it only
    // on the turret area - all three swoop areas leave Start_Offset, Tunnel and
    // Mouse zeroed - and m12ab's Start_Offset_X of 7 degrees sits inside its
    // authored 2..45 degree pitch band. It is an angle, not a translation.
    _startPitch = glm::radians(player.startOffset.x);
    _startYaw = glm::radians(player.startOffset.z);
    if (_pitchBounded) {
        _startPitch = glm::clamp(_startPitch, _minPitch, _maxPitch);
    } else {
        _startPitch = normalizeAngle(_startPitch);
    }
    if (_yawBounded) {
        _startYaw = glm::clamp(_startYaw, _minYaw, _maxYaw);
    } else {
        _startYaw = normalizeAngle(_startYaw);
    }

    reset();
}

void TurretAim::reset() {
    _pitch = _startPitch;
    _yaw = _startYaw;
}

float TurretAim::pitchTravel() const {
    return _pitchBounded ? glm::max(0.0f, _maxPitch - _minPitch) : glm::two_pi<float>();
}

float TurretAim::yawTravel() const {
    return _yawBounded ? glm::max(0.0f, _maxYaw - _minYaw) : glm::two_pi<float>();
}

void TurretAimRate::configure(float travelRadians) {
    // Scale reone's full-turn rates by how much travel this axis actually has,
    // so a narrow axis is proportionally gentler and every axis crosses its
    // authored range in about the same time.
    float scale = travelRadians > 0.0f
                      ? glm::min(travelRadians / glm::two_pi<float>(), 1.0f)
                      : 1.0f;
    _minRate = kTurnRateMin * scale;
    _maxRate = kTurnRateMax * scale;
    _acceleration = kTurnAcceleration * scale;
    reset();
}

void TurretAimRate::setDirection(int direction) {
    int clamped = direction > 0 ? 1 : (direction < 0 ? -1 : 0);
    if (clamped == _direction) {
        return;
    }
    _direction = clamped;
    _held = 0.0f;
}

void TurretAimRate::reset() {
    _direction = 0;
    _held = 0.0f;
}

float TurretAimRate::rateAt(float held) const {
    return glm::min(_maxRate, _minRate + _acceleration * glm::max(0.0f, held));
}

float TurretAimRate::rate() const {
    return _direction != 0 ? rateAt(_held) : 0.0f;
}

float TurretAimRate::angleOver(float from, float to) const {
    // Integrate the ramp exactly rather than stepping it, so the angle covered
    // over an interval is the same however many frames it is split into.
    if (to <= from) {
        return 0.0f;
    }
    float capAt = _acceleration > 0.0f ? (_maxRate - _minRate) / _acceleration
                                       : 0.0f;
    float rampFrom = glm::min(from, capAt);
    float rampTo = glm::min(to, capAt);
    float angle = 0.0f;
    if (rampTo > rampFrom) {
        angle += _minRate * (rampTo - rampFrom) +
                 0.5f * _acceleration * (rampTo * rampTo - rampFrom * rampFrom);
    }
    float flatFrom = glm::max(from, capAt);
    float flatTo = glm::max(to, capAt);
    if (flatTo > flatFrom) {
        angle += _maxRate * (flatTo - flatFrom);
    }
    return angle;
}

float TurretAimRate::advance(float dt) {
    if (_direction == 0 || dt <= 0.0f) {
        return 0.0f;
    }
    float from = _held;
    _held += dt;
    return static_cast<float>(_direction) * angleOver(from, _held);
}

void TurretAim::addPitch(float radians) {
    _pitch += radians;
    if (_pitchBounded) {
        _pitch = glm::clamp(_pitch, _minPitch, _maxPitch);
    } else {
        _pitch = normalizeAngle(_pitch);
    }
}

void TurretAim::addYaw(float radians) {
    _yaw += radians;
    if (_yawBounded) {
        _yaw = glm::clamp(_yaw, _minYaw, _maxYaw);
    } else {
        _yaw = normalizeAngle(_yaw);
    }
}

void TurretAim::set(float pitchRadians, float yawRadians) {
    if (!std::isfinite(pitchRadians) || !std::isfinite(yawRadians)) {
        return;
    }
    float turn = glm::two_pi<float>();
    float yaw = yawRadians;
    for (float candidate : {yawRadians - turn, yawRadians + turn}) {
        if (std::fabs(candidate - _yaw) < std::fabs(yaw - _yaw)) {
            yaw = candidate;
        }
    }
    _pitch = 0.0f;
    _yaw = 0.0f;
    addPitch(pitchRadians);
    addYaw(yaw);
}

glm::quat TurretAim::orientation() const {
    // Built from the portable sine and cosine: the aim sets the bolts' heading.
    float halfPitch = 0.5f * _pitch;
    glm::quat pitch(portable::cosf(halfPitch), portable::sinf(halfPitch), 0.0f, 0.0f);
    float halfYaw = 0.5f * _yaw;
    glm::quat yaw(portable::cosf(halfYaw), 0.0f, 0.0f, portable::sinf(halfYaw));
    return yaw * pitch;
}

glm::vec3 TurretAim::forward() const {
    return orientation() * kModelForward;
}

void TurretGunTimer::update(float dt) {
    if (_cooldown > 0.0f) {
        _cooldown = glm::max(0.0f, _cooldown - dt);
    }
}

bool TurretGunTimer::tryFire() {
    if (_cooldown > 0.0f) {
        return false;
    }
    _cooldown = _rateOfFire;
    return true;
}

bool TurretBullet::advance(float dt) {
    // A bolt is created and integrated within the same tick, so its first step
    // is spent standing at the muzzle: that is the only frame in which it is
    // drawn where the authored gun bank actually points.
    if (atMuzzle) {
        atMuzzle = false;
        return true;
    }
    life += dt;
    if (life >= lifespan) {
        return false;
    }
    position += direction * speed * dt;
    return true;
}

bool turretDeathEffectComplete(float elapsed, float duration) {
    if (duration <= 0.0f) {
        return true;
    }
    return elapsed >= duration;
}

float turretMuzzleClearance(float modelMinForward) {
    return glm::max(0.0f, -modelMinForward);
}

void turretDisableReferenceAttachments(ModelSceneNode &model) {
    std::stack<std::reference_wrapper<const graphics::ModelNode>> pending;
    if (auto root = model.model().rootNode()) {
        pending.push(*root);
    }
    while (!pending.empty()) {
        const auto &node = pending.top().get();
        pending.pop();
        if (node.isReference()) {
            if (auto attachment = model.getAttachment(node.name())) {
                attachment->setEnabled(false);
            }
        }
        for (const auto &child : node.children()) {
            pending.push(*child);
        }
    }
}

glm::vec3 turretFireDirection(const glm::quat &shooterOrientation) {
    glm::vec3 direction(shooterOrientation * kModelForward);
    float length = glm::length(direction);
    return length > 0.0f ? direction / length : kModelForward;
}

glm::mat4 turretBulletTransform(const TurretBullet &bullet) {
    glm::mat4 transform(1.0f);
    transform *= glm::translate(bullet.position);
    transform *= glm::mat4_cast(bullet.orientation);
    return transform;
}

bool sphereContainsPoint(const glm::vec3 &center, float radius, const glm::vec3 &point) {
    if (radius <= 0.0f) {
        return false;
    }
    return glm::distance2(center, point) <= radius * radius;
}

bool rayIntersectsSphere(const glm::vec3 &origin,
                         const glm::vec3 &direction,
                         const glm::vec3 &center,
                         float radius,
                         float maxDistance) {
    if (radius <= 0.0f || maxDistance <= 0.0f) {
        return false;
    }
    glm::vec3 toCenter(center - origin);
    float alongRay = glm::dot(toCenter, direction);
    float perpSq = glm::dot(toCenter, toCenter) - alongRay * alongRay;
    float radiusSq = radius * radius;
    if (perpSq > radiusSq) {
        return false;
    }
    float halfChord = glm::sqrt(radiusSq - perpSq);
    float exit = alongRay + halfChord;
    if (exit < 0.0f) {
        return false;
    }
    float entry = alongRay - halfChord;
    return glm::max(entry, 0.0f) <= maxDistance;
}

std::shared_ptr<ModelSceneNode> Turret::loadModel(const std::string &resRef) {
    if (resRef.empty()) {
        return nullptr;
    }
    auto model = _services.resource.models.get(resRef);
    if (!model) {
        return nullptr;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    auto node = sceneGraph.newModel(*model, ModelUsage::Placeable);
    node->setDrawDistance(_game.options().graphics.drawDistance);
    playIdleAnimation(*node);
    return node;
}

std::shared_ptr<ModelSceneNode> Turret::loadTrack(const std::string &resRef,
                                                  const glm::vec3 &position) {
    auto node = loadModel(resRef);
    if (!node) {
        return nullptr;
    }
    node->setLocalTransform(glm::translate(position));
    // Tracks are authored as a single looping animation that walks the
    // "modelhook" node along the rail; everything riding the track is parented
    // to that hook.
    auto animations = node->model().getAnimationNames();
    if (!animations.empty()) {
        node->playAnimation(animations.front(), nullptr,
                            AnimationProperties::fromFlags(AnimationFlags::loop));
    }
    // The scene graph decides culling per model root and prunes the whole
    // subtree behind a culled one. A track root sits at its authored origin
    // while the rail animation carries the hook - and everything riding it,
    // including the camera - far away, so distance and frustum culling of the
    // root would blink the actors in and out. This is the case the scene node
    // culling flag exists for.
    node->setCullingEnabled(false);
    node->setDrawDistance(std::numeric_limits<float>::max());
    _services.scene.graphs.get(kSceneMain).addRoot(node);
    return node;
}

void Turret::loadGunBanks(const std::vector<MinigameGunBankSpec> &specs,
                          ModelSceneNode &mount,
                          std::vector<GunBank> &out) {
    for (const auto &spec : specs) {
        GunBank bank;
        bank.spec = &spec;
        bank.timer = TurretGunTimer(spec.bullet.rateOfFire);
        bank.modelNode = loadModel(spec.gunModelResRef);
        if (bank.modelNode) {
            mount.attach(gunBankHookName(spec.bankId), *bank.modelNode);
        }
        out.push_back(std::move(bank));
    }
}

bool Turret::loadPlayer(const MinigameSpec &spec) {
    for (const auto &model : spec.player.models) {
        auto node = loadModel(model.resRef);
        if (!node) {
            debug("turret: player model '" + model.resRef + "' missing");
            continue;
        }
        if (model.rotating) {
            if (!_turretRoot) {
                _turretRoot = std::move(node);
            } else {
                _turretRoot->addChild(*node);
                _turretChildNodes.push_back(std::move(node));
            }
        } else {
            if (!_bodyRoot) {
                _bodyRoot = std::move(node);
            } else {
                _bodyRoot->addChild(*node);
                _bodyChildNodes.push_back(std::move(node));
            }
        }
    }
    if (!_turretRoot) {
        return false;
    }

    // Both groups ride the player track's hook so the hull and the gun stay
    // together; only the rotating group takes the aim rotation on top.
    if (_playerTrackNode) {
        _playerTrackNode->attach(kModelHookName, *_turretRoot);
        if (_bodyRoot) {
            _playerTrackNode->attach(kModelHookName, *_bodyRoot);
        }
    } else {
        // No usable track: the actors become roots of their own. They are still
        // driven by transform rather than by the camera's own frame, so keep
        // them out of the culling pass for the same reason as the tracks.
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        _turretRoot->setCullingEnabled(false);
        sceneGraph.addRoot(_turretRoot);
        if (_bodyRoot) {
            _bodyRoot->setCullingEnabled(false);
            sceneGraph.addRoot(_bodyRoot);
        }
    }

    loadGunBanks(spec.player.gunBanks, *_turretRoot, _gunBanks);
    return true;
}

void Turret::loadEnemies(const MinigameSpec &spec) {
    auto layout = _services.resource.layouts.get(_areaName);
    for (size_t i = 0; i < spec.enemies.size(); ++i) {
        const auto &enemySpec = spec.enemies[i];
        Enemy enemy;
        enemy.spec = &enemySpec;
        // Session actors are the player followed by the enemies in file order.
        enemy.actorIndex = i + 1;

        glm::vec3 trackPos(0.0f);
        if (layout) {
            if (auto placement = layout->findTrackByName(enemySpec.trackResRef)) {
                trackPos = placement->get().position;
            }
        }
        enemy.trackNode = loadTrack(enemySpec.trackResRef, trackPos);

        for (const auto &model : enemySpec.models) {
            auto node = loadModel(model.resRef);
            if (!node) {
                continue;
            }
            if (!enemy.modelNode) {
                enemy.modelNode = std::move(node);
            } else {
                enemy.modelNode->addChild(*node);
                enemy.extraNodes.push_back(std::move(node));
            }
        }
        if (!enemy.modelNode) {
            debug("turret: enemy on track '" + enemySpec.trackResRef + "' has no model");
            continue;
        }
        if (enemy.trackNode) {
            enemy.trackNode->attach(kModelHookName, *enemy.modelNode);
        } else {
            enemy.modelNode->setCullingEnabled(false);
            _services.scene.graphs.get(kSceneMain).addRoot(enemy.modelNode);
        }
        for (const auto &bankSpec : enemySpec.gunBanks) {
            GunBank bank;
            bank.spec = &bankSpec;
            bank.timer = TurretGunTimer(bankSpec.bullet.rateOfFire);
            bank.modelNode = loadModel(bankSpec.gunModelResRef);
            if (bank.modelNode) {
                enemy.modelNode->attach(gunBankHookName(bankSpec.bankId), *bank.modelNode);
            }
            enemy.gunBanks.push_back(std::move(bank));
        }
        _enemies.push_back(std::move(enemy));
    }
}

bool Turret::start(MiniGameSession &session, const MinigameSpec &spec, FirstPersonCamera *camera, const std::string &areaName) {
    if (_active) {
        return false;
    }
    _session = &session;
    _spec = spec;
    _areaName = areaName;
    _camera = camera;
    _elapsed = 0.0f;
    _shotsFired = 0;
    _hitsLanded = 0;
    _hitsTaken = 0;
    _keyPitch = 0;
    _keyYaw = 0;
    _fireKey = false;
    _fireButton = false;

    _trackResRef = _spec.player.trackResRef;
    _playerSphereRadius = _spec.player.sphereRadius;
    // Replaying a session starts from the authored aim with no carried-over
    // input velocity.
    _aim.configure(_spec.player);
    _pitchRate.configure(_aim.pitchTravel());
    _yawRate.configure(_aim.yawTravel());

    glm::vec3 trackPos(0.0f);
    _anchorSource = "origin";
    if (auto layout = _services.resource.layouts.get(_areaName)) {
        if (auto placement = layout->findTrackByName(_trackResRef)) {
            trackPos = placement->get().position;
            _anchorSource = "lyt-track";
        }
    }
    _playerTrackNode = loadTrack(_trackResRef, trackPos);
    if (_playerTrackNode) {
        if (_anchorSource == "origin") {
            _anchorSource = "track-model";
        }
    } else {
        _anchorSource = "no-track";
    }

    if (!loadPlayer(_spec)) {
        debug("turret: no visible player models loaded");
        detachAll();
        _camera = nullptr;
        _session = nullptr;
        return false;
    }
    loadEnemies(_spec);

    // The camera mount model is not added to the scene: only its static
    // "camerahook" transform is needed, and vanilla hides the mount anyway.
    _cameraHookLocal = glm::mat4(1.0f);
    _haveCameraHook = false;
    if (!_spec.player.cameraResRef.empty()) {
        if (auto cameraModel = _services.resource.models.get(_spec.player.cameraResRef)) {
            if (auto hook = cameraModel->getNodeByNameRecursive(kCameraHookName)) {
                _cameraHookLocal = hook->absoluteTransform();
                _haveCameraHook = true;
            }
        }
    }

    _active = true;
    suppressCanopyGlass();
    setCameraFieldOfView(_spec.cameraViewAngle > 0.0f ? _spec.cameraViewAngle
                                                      : kDefaultCameraFovDegrees);
    updateAnchor();
    updatePlayerTransforms();
    updateCamera();
    prepare(0.0f);
    return true;
}

void Turret::stop() {
    if (!_active) {
        return;
    }
    // Restore the reused camera's projection before releasing it.
    if (_camera) {
        _camera->cameraSceneNode()->setPerspectiveProjection(
            glm::radians(kDefaultCameraFovDegrees),
            cameraAspect(),
            kDefaultClipPlaneNear,
            kDefaultClipPlaneFar);
    }
    _animator.clear();
    _active = false;
    _camera = nullptr;
    _session = nullptr;
    _pitchRate.reset();
    _yawRate.reset();
    detachAll();
}

namespace {

// Detach whatever was attached to a hook node of \p model. ModelSceneNode
// attachments are parented to a nested node, so clearing the model's own
// children is not enough.
void clearHook(const std::shared_ptr<ModelSceneNode> &model, const std::string &hookName) {
    if (!model) {
        return;
    }
    if (auto hook = model->getNodeByName(hookName)) {
        hook->removeAllChildren();
    }
}

int directionOf(float value) {
    return value > 0.0f ? 1 : (value < 0.0f ? -1 : 0);
}

} // namespace

void Turret::detachEnemyFromTrack(Enemy &enemy) {
    if (!enemy.modelNode || enemy.detachedFromTrack) {
        return;
    }
    enemy.detachedFromTrack = true;
    if (!enemy.trackNode) {
        return; // already a scene root: nothing is translating it
    }
    // Preserve the pose the fighter died in, then move it from the rail's hook
    // to the scene root the tracks themselves live under. The hook keeps
    // looping, but the wreck no longer inherits it; anything the "die"
    // animation does to the model still applies on top of this transform.
    glm::mat4 world = enemy.modelNode->absoluteTransform();
    clearHook(enemy.trackNode, kModelHookName);
    enemy.modelNode->setLocalTransform(world);
    // A wreck sits where it was killed while the rail carries on, so the
    // culling that assumes actors stay near their track root no longer holds.
    enemy.modelNode->setCullingEnabled(false);
    _services.scene.graphs.get(kSceneMain).addRoot(enemy.modelNode);
}

void Turret::releaseEnemyNodes(Enemy &enemy) {
    // Everything a fighter owns hangs off its model root: the authored gun bank
    // models on their hooks, and - built by the model loader itself - the
    // reference models the fighter names (mgf_sithfighter attaches an fx_ref
    // engine flare at each of its OmenRef nodes), plus its lights and emitters.
    // Dropping the root and its rail therefore retires the whole fighter,
    // without this code having to know which effects it authored.
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    for (auto &bank : enemy.gunBanks) {
        if (bank.spec) {
            clearHook(enemy.modelNode, gunBankHookName(bank.spec->bankId));
        }
    }
    enemy.gunBanks.clear();
    clearHook(enemy.trackNode, kModelHookName);
    if (enemy.modelNode) {
        _animator.forget(*enemy.modelNode);
        for (auto &extra : enemy.extraNodes) {
            _animator.forget(*extra);
        }
        enemy.modelNode->removeAllChildren();
        sceneGraph.removeRoot(*enemy.modelNode);
    }
    if (enemy.trackNode) {
        sceneGraph.removeRoot(*enemy.trackNode);
    }
    enemy.extraNodes.clear();
    enemy.modelNode.reset();
    enemy.trackNode.reset();
    enemy.nodesReleased = true;
}

void Turret::detachAll() {
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);

    for (auto &bullet : _bullets) {
        releaseBulletNode(bullet);
    }
    _bullets.clear();
    for (auto &pooled : _bulletNodePool) {
        pooled.second.clear();
    }
    _bulletNodePool.clear();

    for (auto &enemy : _enemies) {
        releaseEnemyNodes(enemy);
    }
    _enemies.clear();

    for (auto &bank : _gunBanks) {
        if (bank.spec) {
            clearHook(_turretRoot, gunBankHookName(bank.spec->bankId));
        }
    }
    _gunBanks.clear();

    clearHook(_playerTrackNode, kModelHookName);
    if (_turretRoot) {
        _turretRoot->removeAllChildren();
        sceneGraph.removeRoot(*_turretRoot);
    }
    if (_bodyRoot) {
        _bodyRoot->removeAllChildren();
        sceneGraph.removeRoot(*_bodyRoot);
    }
    if (_playerTrackNode) {
        sceneGraph.removeRoot(*_playerTrackNode);
    }
    _turretChildNodes.clear();
    _bodyChildNodes.clear();
    _turretRoot.reset();
    _bodyRoot.reset();
    _playerTrackNode.reset();
}

size_t Turret::enemiesAlive() const {
    size_t count = 0;
    for (const auto &enemy : _enemies) {
        if (enemy.alive) {
            ++count;
        }
    }
    return count;
}

int Turret::hitPoints() const {
    auto player = _session ? _session->player() : nullptr;
    return player ? player->hitPoints : 0;
}

int Turret::maxHitPoints() const {
    auto player = _session ? _session->player() : nullptr;
    return player ? player->maxHitPoints : 0;
}

float Turret::playerBulletSpeed() const {
    for (const auto &bank : _gunBanks) {
        if (bank.spec && bank.spec->bullet.speed > 0.0f) {
            return bank.spec->bullet.speed;
        }
    }
    return 0.0f;
}

Turret::Outcome Turret::outcome() const {
    auto player = _session ? _session->player() : nullptr;
    if (player && player->dead) {
        return Outcome::Lost;
    }
    if (!_enemies.empty() && enemiesAlive() == 0) {
        return Outcome::Won;
    }
    return Outcome::InProgress;
}

std::vector<ModelSceneNode *> Turret::playerModels() {
    std::vector<ModelSceneNode *> models;
    if (_turretRoot) {
        models.push_back(_turretRoot.get());
    }
    for (auto &node : _turretChildNodes) {
        models.push_back(node.get());
    }
    if (_bodyRoot) {
        models.push_back(_bodyRoot.get());
    }
    for (auto &node : _bodyChildNodes) {
        models.push_back(node.get());
    }
    return models;
}

Turret::Enemy *Turret::enemyForActor(const MiniGameActor &actor) {
    if (!_session) {
        return nullptr;
    }
    size_t index = static_cast<size_t>(&actor - _session->actors().data());
    for (auto &enemy : _enemies) {
        if (enemy.actorIndex == index) {
            return &enemy;
        }
    }
    return nullptr;
}

void Turret::prepare(float dt) {
    if (!_active || !_session) {
        return;
    }
    publishInput();
    auto &input = _game.miniGameInput();
    _pitchRate.setDirection(directionOf(input.pitchRate));
    _yawRate.setDirection(directionOf(input.yawRate));
    if (dt > 0.0f) {
        _aim.addPitch(_pitchRate.advance(dt));
        _aim.addYaw(_yawRate.advance(dt));
    }
    _aim.addPitch(glm::radians(input.pitchDelta));
    _aim.addYaw(glm::radians(input.yawDelta));
    input.pitchDelta = 0.0f;
    input.yawDelta = 0.0f;

    updateAnchor();
    updatePlayerTransforms();

    // The session sees the aim as the player offset, in degrees: X pitch,
    // Z yaw in [0, 360). k_heartbeat turns the radar with it.
    float yawDegrees = std::fmod(glm::degrees(_aim.yaw()), 360.0f);
    if (yawDegrees < 0.0f) {
        yawDegrees += 360.0f;
    }
    _session->setPlayerAim(glm::vec3(glm::degrees(_aim.pitch()), 0.0f, yawDegrees));
    if (auto player = _session->player()) {
        _session->setActorPosition(*player, _anchorPosition, transformOrientation(_anchorTransform));
    }
    for (auto &enemy : _enemies) {
        if (!enemy.modelNode || enemy.actorIndex >= _session->actors().size()) {
            continue;
        }
        enemy.previousPosition = enemy.position;
        const glm::mat4 &transform = enemy.modelNode->absoluteTransform();
        enemy.position = transformOrigin(transform);
        enemy.orientation = transformOrientation(transform);
        enemy.forward = turretFireDirection(enemy.orientation);
        auto &actor = _session->actors()[enemy.actorIndex];
        if (!actor.dead) {
            _session->setActorPosition(actor, enemy.position, enemy.orientation);
        }
    }
}

void Turret::prepareReplica(float dt, bool firing) {
    if (!_active || !_session) {
        return;
    }
    // The host's aim, reported as the player offset in degrees: X pitch, Z yaw.
    const glm::vec3 &aim = _session->offset();
    _aim.set(glm::radians(aim.x), glm::radians(aim.z));
    updateAnchor();
    updatePlayerTransforms();
    auto &actors = _session->actors();
    for (auto &enemy : _enemies) {
        if (!enemy.modelNode || enemy.actorIndex >= actors.size() || !enemy.alive) {
            continue;
        }
        // Off its rail: the host says where the fighter is, and the rail's own clock here
        // would only drift from it.
        detachEnemyFromTrack(enemy);
        const auto &actor = actors[enemy.actorIndex];
        enemy.modelNode->setLocalTransform(actor.hook);
        enemy.previousPosition = enemy.position;
        enemy.position = actor.position;
        enemy.orientation = transformOrientation(actor.hook);
        enemy.forward = turretFireDirection(enemy.orientation);
    }
    // The player's shots, at each gun's own rate while the host's player holds fire.
    auto player = _session->player();
    for (size_t b = 0; b < _gunBanks.size(); ++b) {
        auto &timer = _gunBanks[b].timer;
        timer.update(dt);
        if (firing && player && !player->dead && timer.tryFire()) {
            onMiniGameGunFired(*player, b);
        }
    }
}

glm::vec2 Turret::replicaRateStep(const MiniGameInput &input, float dt) {
    _pitchRate.setDirection(directionOf(input.pitchRate));
    _yawRate.setDirection(directionOf(input.yawRate));
    if (dt <= 0.0f) {
        return glm::vec2(0.0f);
    }
    return glm::vec2(glm::degrees(_pitchRate.advance(dt)), glm::degrees(_yawRate.advance(dt)));
}

void Turret::update(float dt) {
    if (!_active || dt <= 0.0f) {
        return;
    }
    _elapsed += dt;
    updateCamera();
    updateEnemies(dt);
    updateBullets(dt);
    _animator.update(dt);
}

void Turret::updateAnchor() {
    if (_playerTrackNode) {
        if (auto hook = _playerTrackNode->getNodeByName(kModelHookName)) {
            _anchorTransform = hook->absoluteTransform();
        } else {
            _anchorTransform = _playerTrackNode->absoluteTransform();
        }
    } else {
        _anchorTransform = glm::mat4(1.0f);
    }
    _anchorPosition = transformOrigin(_anchorTransform);
}

void Turret::updatePlayerTransforms() {
    // The roots are parented to the track hook, so their local transform is the
    // actor's placement within the hook frame: both groups sit on the hook and
    // only the rotating one takes the aim rotation.
    if (_bodyRoot) {
        _bodyRoot->setLocalTransform(glm::mat4(1.0f));
    }
    if (_turretRoot) {
        _turretRoot->setLocalTransform(glm::mat4_cast(_aim.orientation()));
    }
}

glm::mat4 Turret::eyeTransform() const {
    if (!_turretRoot) {
        return _anchorTransform;
    }
    glm::mat4 eye = _turretRoot->absoluteTransform();
    if (_haveCameraHook) {
        eye *= _cameraHookLocal;
    }
    return eye;
}

void Turret::updateCamera() {
    if (!_camera || !_turretRoot) {
        return;
    }
    _camera->cameraSceneNode()->setLocalTransform(eyeTransform());
}

void Turret::setCameraFieldOfView(float fovDegrees) {
    if (!_camera) {
        return;
    }
    float nearClip = _spec.nearClip > 0.0f ? _spec.nearClip : kDefaultClipPlaneNear;
    float farClip = _spec.farClip > 0.0f ? _spec.farClip : kDefaultClipPlaneFar;
    _camera->cameraSceneNode()->setPerspectiveProjection(
        glm::radians(fovDegrees),
        cameraAspect(),
        nearClip,
        farClip);
}

float Turret::cameraAspect() const {
    const auto &graphicsOpts = _game.options().graphics;
    if (graphicsOpts.height <= 0) {
        return 1.0f;
    }
    return graphicsOpts.width / static_cast<float>(graphicsOpts.height);
}

void Turret::onMiniGameGunFired(const MiniGameActor &actor, size_t bankIndex) {
    if (actor.kind != MiniGameActorKind::Player || !_turretRoot || bankIndex >= _gunBanks.size()) {
        return;
    }
    auto &bank = _gunBanks[bankIndex];
    if (!bank.spec) {
        return;
    }
    glm::mat4 muzzle = _turretRoot->absoluteTransform();
    if (bank.modelNode) {
        if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
            muzzle = hook->absoluteTransform();
        } else {
            muzzle = bank.modelNode->absoluteTransform();
        }
        bank.modelNode->playAnimation("fire");
    }
    // The muzzle decides where the bolt appears and where it goes. The gun
    // banks are not aligned with the hull they are mounted on: on the K1
    // turret each bullethook0 sits five degrees below the turret body and
    // toed half a degree inboard, so the pair converges down range.
    spawnBullet(*bank.spec,
                transformOrigin(muzzle),
                transformOrientation(muzzle),
                /*fromPlayer=*/true,
                0);
    if (!bank.spec->fireSound.empty()) {
        playSound(bank.spec->fireSound);
    }
    _turretRoot->playAnimation("fire");
    ++_shotsFired;
}

void Turret::spawnBullet(const MinigameGunBankSpec &bank,
                         const glm::vec3 &origin,
                         const glm::quat &orientation,
                         bool fromPlayer,
                         size_t shooterActor) {
    Bullet bullet;
    bullet.sim.position = origin;
    bullet.sim.orientation = orientation;
    bullet.sim.direction = turretFireDirection(orientation);
    bullet.sim.speed = bank.bullet.speed;
    bullet.sim.lifespan = bank.bullet.lifespan;
    bullet.sim.damage = bank.bullet.damage;
    bullet.sim.fromPlayer = fromPlayer;
    bullet.modelResRef = bank.bullet.modelResRef;
    bullet.collisionSound = bank.bullet.collisionSound;
    bullet.targetType = bank.bullet.targetType;
    bullet.shooterActor = shooterActor;

    bullet.modelNode = acquireBulletNode(bullet.modelResRef);
    if (bullet.modelNode) {
        // The bolt is drawn from its own origin, which sits partway along the
        // shot rather than at its tail. Start it far enough ahead that the part
        // reaching backwards clears the gun instead of being drawn inside it.
        bullet.sim.position += bullet.sim.direction *
                               turretMuzzleClearance(bullet.modelNode->model().aabb().min().y);
        bullet.modelNode->setLocalTransform(turretBulletTransform(bullet.sim));
        _services.scene.graphs.get(kSceneMain).addRoot(bullet.modelNode);
    }
    _bullets.push_back(std::move(bullet));
}

std::shared_ptr<ModelSceneNode> Turret::acquireBulletNode(const std::string &resRef) {
    auto pooled = _bulletNodePool.find(resRef);
    if (pooled != _bulletNodePool.end() && !pooled->second.empty()) {
        auto node = std::move(pooled->second.back());
        pooled->second.pop_back();
        return node;
    }
    auto node = loadModel(resRef);
    if (node) {
        silenceEmitters(*node);
    }
    return node;
}

void Turret::releaseBulletNode(Bullet &bullet) {
    if (!bullet.modelNode) {
        return;
    }
    _services.scene.graphs.get(kSceneMain).removeRoot(*bullet.modelNode);
    _bulletNodePool[bullet.modelResRef].push_back(std::move(bullet.modelNode));
}

void Turret::updateEnemies(float dt) {
    for (auto &enemy : _enemies) {
        if (!enemy.modelNode) {
            continue;
        }
        if (!enemy.alive) {
            if (!enemy.deathHandled) {
                enemy.deathHandled = true;
                // The authored death animation decides how long the wreck stays.
                if (auto dying = enemy.modelNode->model().getAnimation("die")) {
                    enemy.deathDuration = dying->length();
                }
                // The engine glow is not part of the death presentation: it
                // burns steadily for as long as its node lives, so a wreck that
                // kept it would trail a lit flare through the whole animation.
                turretDisableReferenceAttachments(*enemy.modelNode);
                // Particles are children of the emitter that made them, so a
                // wreck still riding its rail carries its whole explosion along
                // with it. Take the fighter off the rail first, keeping it where
                // it died, and the blast stays at the kill instead of sailing
                // across the sky.
                detachEnemyFromTrack(enemy);
                enemy.modelNode->playAnimation("die");
            } else if (!enemy.nodesReleased) {
                // "die" only fades the hull mesh; the fx_ref flares, lights and
                // emitters the fighter owns are untouched by it and would keep
                // riding the rail forever. Retire the whole fighter once the
                // authored effect has played out.
                enemy.deathElapsed += dt;
                if (turretDeathEffectComplete(enemy.deathElapsed, enemy.deathDuration)) {
                    releaseEnemyNodes(enemy);
                }
            }
            continue;
        }

        for (size_t b = 0; b < enemy.gunBanks.size(); ++b) {
            auto &bank = enemy.gunBanks[b];
            bank.timer.update(dt);
            if (!bank.spec || !bank.timer.ready()) {
                continue;
            }
            float sensing = bank.spec->sensingRadius > 0.0f
                                ? bank.spec->sensingRadius
                                : kFallbackSensingRadius;
            if (!rayIntersectsSphere(enemy.position, enemy.forward,
                                     _anchorPosition, _playerSphereRadius, sensing)) {
                continue;
            }
            if (!bank.timer.tryFire()) {
                continue;
            }
            glm::vec3 muzzle = enemy.position;
            if (bank.modelNode) {
                if (auto hook = bank.modelNode->getNodeByName(kBulletHookName)) {
                    muzzle = transformOrigin(hook->absoluteTransform());
                }
            }
            spawnBullet(*bank.spec, muzzle, enemy.orientation, /*fromPlayer=*/false, enemy.actorIndex);
            if (!bank.spec->fireSound.empty()) {
                playSound(bank.spec->fireSound);
            }
            if (_session && !_replica && enemy.actorIndex < _session->actors().size()) {
                _session->gunFired(_session->actors()[enemy.actorIndex], b);
            }
        }
    }
}

void Turret::updateBullets(float dt) {
    for (auto it = _bullets.begin(); it != _bullets.end();) {
        auto &bullet = *it;
        if (!bullet.sim.advance(dt)) {
            releaseBulletNode(bullet);
            it = _bullets.erase(it);
            continue;
        }
        if (bullet.modelNode) {
            bullet.modelNode->setLocalTransform(turretBulletTransform(bullet.sim));
        }

        // Target type: 1 = the player, 2 = the enemies, 3 = both. A co-op client's bolts
        // are only drawn: the host decides what they hit.
        MiniGameActor *target = nullptr;
        if (_session && !_replica) {
            if ((bullet.targetType & 2) != 0 || (bullet.targetType == 0 && bullet.sim.fromPlayer)) {
                for (auto &enemy : _enemies) {
                    if (!enemy.alive || !enemy.spec || enemy.actorIndex == bullet.shooterActor) {
                        continue;
                    }
                    if (sphereContainsPoint(enemy.position, enemy.spec->sphereRadius, bullet.sim.position)) {
                        target = &_session->actors()[enemy.actorIndex];
                        break;
                    }
                }
            }
            if (!target && bullet.shooterActor != 0 &&
                ((bullet.targetType & 1) != 0 || (bullet.targetType == 0 && !bullet.sim.fromPlayer)) &&
                sphereContainsPoint(_anchorPosition, _playerSphereRadius, bullet.sim.position)) {
                target = _session->player();
            }
        }

        if (target) {
            if (!bullet.collisionSound.empty()) {
                playSound(bullet.collisionSound);
                _game.recordMiniGameSound(bullet.collisionSound);
            }
            uint32_t shooter = bullet.shooterActor < _session->actors().size()
                                   ? _session->actors()[bullet.shooterActor].objectId
                                   : 0;
            if (target->kind == MiniGameActorKind::Player) {
                ++_hitsTaken;
            } else {
                ++_hitsLanded;
            }
            int damage = static_cast<int>(bullet.sim.damage);
            int targetType = static_cast<int>(bullet.targetType);
            releaseBulletNode(bullet);
            it = _bullets.erase(it);
            // The session may run scripts that end the minigame; nothing of
            // this bullet is touched after it.
            _session->bulletHit(*target, damage, targetType, shooter, std::string());
            if (!_active) {
                return;
            }
            continue;
        }
        ++it;
    }
}

void Turret::suppressCanopyGlass() {
    if (!_turretRoot) {
        return;
    }
    // Hide the canopy glass until additive environment-mapped model materials
    // can be rendered without compositing across the cockpit. Both shells are
    // authored inward-facing and enclose the camera, so at full strength they
    // lay the scratch texture over the whole field of view - twice. The nodes
    // stay in the loaded model and the shipped resource is untouched; only this
    // session's cockpit instance stops drawing them.
    static const std::array<const char *, 2> kGlassNodes {"sphere04", "sphere05"};
    for (const auto *name : kGlassNodes) {
        auto node = _turretRoot->getNodeByName(name);
        if (!node) {
            debug(str(boost::format("turret: canopy glass node '%s' not present") % name));
            continue;
        }
        node->setEnabled(false);
    }
}

void Turret::playMiniGameAnimation(const MiniGameActor &actor,
                                   const std::string &name,
                                   bool looping,
                                   bool queue,
                                   bool overlay) {
    if (actor.kind == MiniGameActorKind::Player) {
        for (auto *model : playerModels()) {
            _animator.play(*model, name, looping, queue, overlay);
        }
        return;
    }
    auto enemy = enemyForActor(actor);
    if (!enemy || !enemy->modelNode || !enemy->alive) {
        return;
    }
    _animator.play(*enemy->modelNode, name, looping, queue, overlay);
    for (auto &extra : enemy->extraNodes) {
        _animator.play(*extra, name, looping, queue, overlay);
    }
}

void Turret::removeMiniGameAnimation(const MiniGameActor &actor, const std::string &name) {
    if (actor.kind == MiniGameActorKind::Player) {
        for (auto *model : playerModels()) {
            _animator.remove(*model, name);
        }
        return;
    }
    auto enemy = enemyForActor(actor);
    if (!enemy || !enemy->modelNode) {
        return;
    }
    _animator.remove(*enemy->modelNode, name);
}

void Turret::playMiniGameSound(const std::string &resRef, const glm::vec3 &position) {
    playSound(resRef);
}

void Turret::onMiniGameActorDied(const MiniGameActor &actor) {
    if (auto enemy = enemyForActor(actor)) {
        enemy->alive = false;
    }
}

void Turret::playSound(const std::string &resRef) {
    auto clip = _services.resource.audioClips.get(resRef);
    if (!clip) {
        return;
    }
    _services.audio.mixer.play(std::move(clip), AudioType::Sound);
}

void Turret::publishInput() {
    auto &input = _game.miniGameInput();
    if (_keyPitch != 0 || _keyYaw != 0) {
        input.pitchRate = static_cast<float>(_keyPitch);
        input.yawRate = static_cast<float>(_keyYaw);
    }
}

bool Turret::handle(const input::Event &event) {
    switch (event.type) {
    case input::EventType::KeyDown:
        return handleKeyDown(event.key);
    case input::EventType::KeyUp:
        return handleKeyUp(event.key);
    case input::EventType::MouseMotion:
        return handleMouseMotion(event.motion);
    case input::EventType::MouseButtonDown:
    case input::EventType::MouseButtonUp:
        return handleMouseButton(event.button);
    default:
        return false;
    }
}

bool Turret::handleKeyDown(const input::KeyEvent &event) {
    auto &input = _game.miniGameInput();
    switch (event.code) {
    case input::KeyCode::Up:
    case input::KeyCode::W:
        _keyPitch = 1;
        break;
    case input::KeyCode::Down:
    case input::KeyCode::S:
        _keyPitch = -1;
        break;
    case input::KeyCode::Left:
    case input::KeyCode::A:
        _keyYaw = 1;
        break;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        _keyYaw = -1;
        break;
    case input::KeyCode::Space:
        _fireKey = true;
        input.fire = true;
        return true;
    case input::KeyCode::Escape:
        return true;
    default:
        return false;
    }
    input.pitchRate = static_cast<float>(_keyPitch);
    input.yawRate = static_cast<float>(_keyYaw);
    return true;
}

bool Turret::handleKeyUp(const input::KeyEvent &event) {
    auto &input = _game.miniGameInput();
    switch (event.code) {
    case input::KeyCode::Up:
    case input::KeyCode::W:
        if (_keyPitch == 1) {
            _keyPitch = 0;
        }
        break;
    case input::KeyCode::Down:
    case input::KeyCode::S:
        if (_keyPitch == -1) {
            _keyPitch = 0;
        }
        break;
    case input::KeyCode::Left:
    case input::KeyCode::A:
        if (_keyYaw == 1) {
            _keyYaw = 0;
        }
        break;
    case input::KeyCode::Right:
    case input::KeyCode::D:
        if (_keyYaw == -1) {
            _keyYaw = 0;
        }
        break;
    case input::KeyCode::Space:
        _fireKey = false;
        input.fire = _fireButton;
        return true;
    default:
        return false;
    }
    input.pitchRate = static_cast<float>(_keyPitch);
    input.yawRate = static_cast<float>(_keyYaw);
    return true;
}

bool Turret::handleMouseMotion(const input::MouseMotionEvent &event) {
    if (!_active) {
        return false;
    }
    float yaw = glm::degrees(-event.xrel * kMouseAimRadiansPerPixel);
    float pitch = glm::degrees(-event.yrel * kMouseAimRadiansPerPixel);
    if (_spec.mouse.flipAxisX) {
        yaw = -yaw;
    }
    if (_spec.mouse.flipAxisY) {
        pitch = -pitch;
    }
    auto &input = _game.miniGameInput();
    input.yawDelta += yaw;
    input.pitchDelta += pitch;
    return true;
}

bool Turret::handleMouseButton(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _fireButton = event.pressed;
    _game.miniGameInput().fire = _fireKey || _fireButton;
    return true;
}

} // namespace game

} // namespace reone
