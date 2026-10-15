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

#include "reone/game/minigame.h"

#include <algorithm>
#include <set>

#include <boost/algorithm/string.hpp>

#include "reone/graphics/animation.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/types.h"
#include "reone/resource/parser/gff/are.h"

namespace reone {

namespace game {

namespace {

template <class TModels>
std::vector<MinigameModelSpec> parseModels(const TModels &models) {
    std::vector<MinigameModelSpec> result;
    for (const auto &m : models) {
        if (m.Model.empty()) {
            continue;
        }
        MinigameModelSpec spec;
        spec.resRef = m.Model;
        spec.rotating = m.RotatingModel != 0;
        result.push_back(std::move(spec));
    }
    return result;
}

template <class TBullet>
MinigameBulletSpec parseBullet(const TBullet &bullet) {
    MinigameBulletSpec spec;
    spec.modelResRef = bullet.Bullet_Model;
    spec.collisionSound = bullet.Collision_Sound;
    spec.damage = bullet.Damage;
    spec.lifespan = bullet.Lifespan;
    spec.rateOfFire = bullet.Rate_Of_Fire;
    spec.speed = bullet.Speed;
    spec.targetType = bullet.Target_Type;
    return spec;
}

template <class TScripts>
MiniGameScripts parseFollowerSlots(const TScripts &s) {
    MiniGameScripts slots;
    slots[static_cast<size_t>(MiniGameSlot::OnCreate)] = s.OnCreate;
    slots[static_cast<size_t>(MiniGameSlot::OnHitBullet)] = s.OnHitBullet;
    slots[static_cast<size_t>(MiniGameSlot::OnHitFollower)] = s.OnHitFollower;
    slots[static_cast<size_t>(MiniGameSlot::OnAnimEvent)] = s.OnAnimEvent;
    slots[static_cast<size_t>(MiniGameSlot::OnHeartbeat)] = s.OnHeartbeat;
    slots[static_cast<size_t>(MiniGameSlot::OnDamage)] = s.OnDamage;
    slots[static_cast<size_t>(MiniGameSlot::OnDeath)] = s.OnDeath;
    slots[static_cast<size_t>(MiniGameSlot::OnFire)] = s.OnFire;
    slots[static_cast<size_t>(MiniGameSlot::OnHitObstacle)] = s.OnHitObstacle;
    slots[static_cast<size_t>(MiniGameSlot::OnTrackLoop)] = s.OnTrackLoop;
    slots[static_cast<size_t>(MiniGameSlot::OnAccelerate)] = s.OnAccelerate;
    slots[static_cast<size_t>(MiniGameSlot::OnBrake)] = s.OnBrake;
    slots[static_cast<size_t>(MiniGameSlot::OnHitWorld)] = s.OnHitWorld;
    return slots;
}

} // namespace

const char *miniGameSlotName(MiniGameSlot slot) {
    switch (slot) {
    case MiniGameSlot::OnCreate:
        return "OnCreate";
    case MiniGameSlot::OnHitBullet:
        return "OnHitBullet";
    case MiniGameSlot::OnHitFollower:
        return "OnHitFollower";
    case MiniGameSlot::OnAnimEvent:
        return "OnAnimEvent";
    case MiniGameSlot::OnHeartbeat:
        return "OnHeartbeat";
    case MiniGameSlot::OnDamage:
        return "OnDamage";
    case MiniGameSlot::OnDeath:
        return "OnDeath";
    case MiniGameSlot::OnFire:
        return "OnFire";
    case MiniGameSlot::OnHitObstacle:
        return "OnHitObstacle";
    case MiniGameSlot::OnTrackLoop:
        return "OnTrackLoop";
    case MiniGameSlot::OnAccelerate:
        return "OnAccelerate";
    case MiniGameSlot::OnBrake:
        return "OnBrake";
    case MiniGameSlot::OnHitWorld:
        return "OnHitWorld";
    default:
        return "?";
    }
}

std::optional<MinigameSpec> parseMinigameSpec(const resource::generated::ARE &are) {
    if (are.MiniGame.Type == 0) {
        return std::nullopt;
    }
    MinigameSpec spec;
    spec.type = minigameTypeFromUint(are.MiniGame.Type);
    spec.cameraViewAngle = are.MiniGame.CameraViewAngle;
    spec.nearClip = are.MiniGame.Near_Clip;
    spec.farClip = are.MiniGame.Far_Clip;
    spec.lateralAccel = are.MiniGame.LateralAccel;
    spec.movementPerSec = are.MiniGame.MovementPerSec;
    spec.useInertia = are.MiniGame.UseInertia != 0;
    spec.bumpPlane = are.MiniGame.Bump_Plane;
    spec.depthOfField = are.MiniGame.DOF;
    spec.doBumping = are.MiniGame.DoBumping != 0;
    spec.music = are.MiniGame.Music;
    spec.mouse.axisX = are.MiniGame.Mouse.AxisX;
    spec.mouse.axisY = are.MiniGame.Mouse.AxisY;
    spec.mouse.flipAxisX = are.MiniGame.Mouse.FlipAxisX != 0;
    spec.mouse.flipAxisY = are.MiniGame.Mouse.FlipAxisY != 0;

    const auto &src = are.MiniGame.Player;
    spec.player.cameraResRef = src.Camera;
    spec.player.trackResRef = src.Track;
    spec.player.minimumSpeed = src.Minimum_Speed;
    spec.player.maximumSpeed = src.Maximum_Speed;
    spec.player.accelSecs = src.Accel_Secs;
    spec.player.sphereRadius = src.Sphere_Radius;
    spec.player.hitPoints = src.Hit_Points;
    spec.player.maxHitPoints = src.Max_HPs;
    spec.player.invincePeriod = src.Invince_Period;
    spec.player.bumpDamage = src.Bump_Damage;
    spec.player.numLoops = src.Num_Loops;
    spec.player.cameraRotate = src.CameraRotate != 0;
    spec.player.sounds.death = src.Sounds.Death;
    spec.player.sounds.engine = src.Sounds.Engine;
    spec.player.startOffset = glm::vec3(src.Start_Offset_X, src.Start_Offset_Y, src.Start_Offset_Z);
    spec.player.targetOffset = glm::vec3(src.Target_Offset_X, src.Target_Offset_Y, src.Target_Offset_Z);
    spec.player.tunnelXPos = src.TunnelXPos;
    spec.player.tunnelXNeg = src.TunnelXNeg;
    spec.player.tunnelYPos = src.TunnelYPos;
    spec.player.tunnelYNeg = src.TunnelYNeg;
    spec.player.tunnelZPos = src.TunnelZPos;
    spec.player.tunnelZNeg = src.TunnelZNeg;
    spec.player.tunnelInfinite = src.TunnelInfinite;
    spec.player.scripts.onCreate = src.Scripts.OnCreate;
    spec.player.scripts.onDeath = src.Scripts.OnDeath;
    spec.player.scripts.onTrackLoop = src.Scripts.OnTrackLoop;
    spec.player.scripts.onDamage = src.Scripts.OnDamage;
    spec.player.scripts.onAccelerate = src.Scripts.OnAccelerate;
    spec.player.scripts.onHeartbeat = src.Scripts.OnHeartbeat;
    spec.player.scripts.onFire = src.Scripts.OnFire;
    spec.player.scripts.onHitBullet = src.Scripts.OnHitBullet;
    spec.player.scripts.onHitFollower = src.Scripts.OnHitFollower;
    spec.player.scripts.onHitObstacle = src.Scripts.OnHitObstacle;
    spec.player.slots = parseFollowerSlots(src.Scripts);
    spec.player.models = parseModels(src.Models);
    for (const auto &b : src.Gun_Banks) {
        MinigameGunBankSpec bank;
        bank.bankId = b.BankID;
        bank.gunModelResRef = b.Gun_Model;
        bank.fireSound = b.Fire_Sound;
        bank.bullet = parseBullet(b.Bullet);
        spec.player.gunBanks.push_back(std::move(bank));
    }

    std::set<std::string> seenTracks;
    auto addTrack = [&](const std::string &ref) {
        if (!ref.empty() && seenTracks.insert(ref).second) {
            spec.trackResRefs.push_back(ref);
        }
    };
    addTrack(src.Track);

    for (const auto &e : are.MiniGame.Enemies) {
        MinigameEnemySpec enemy;
        enemy.trackResRef = e.Track;
        enemy.hitPoints = e.Hit_Points;
        enemy.maxHitPoints = e.Max_HPs;
        enemy.sphereRadius = e.Sphere_Radius;
        enemy.invincePeriod = e.Invince_Period;
        enemy.bumpDamage = e.Bump_Damage;
        enemy.numLoops = e.Num_Loops;
        enemy.trigger = e.Trigger != 0;
        enemy.sounds.death = e.Sounds.Death;
        enemy.sounds.engine = e.Sounds.Engine;
        enemy.scripts.onCreate = e.Scripts.OnCreate;
        enemy.scripts.onDeath = e.Scripts.OnDeath;
        enemy.scripts.onDamage = e.Scripts.OnDamage;
        enemy.scripts.onHeartbeat = e.Scripts.OnHeartbeat;
        enemy.scripts.onTrackLoop = e.Scripts.OnTrackLoop;
        enemy.onCreate = e.Scripts.OnCreate;
        enemy.slots = parseFollowerSlots(e.Scripts);
        enemy.models = parseModels(e.Models);
        for (const auto &b : e.Gun_Banks) {
            MinigameGunBankSpec bank;
            bank.bankId = b.BankID;
            bank.gunModelResRef = b.Gun_Model;
            bank.fireSound = b.Fire_Sound;
            bank.bullet = parseBullet(b.Bullet);
            bank.horizSpread = b.Horiz_Spread;
            bank.vertSpread = b.Vert_Spread;
            bank.inaccuracy = b.Inaccuracy;
            bank.sensingRadius = b.Sensing_Radius;
            enemy.gunBanks.push_back(std::move(bank));
        }
        spec.enemies.push_back(std::move(enemy));
        addTrack(e.Track);
    }

    for (const auto &o : are.MiniGame.Obstacles) {
        MinigameObstacleSpec obs;
        obs.name = o.Name;
        obs.onCreate = o.Scripts.OnCreate;
        obs.slots[static_cast<size_t>(MiniGameSlot::OnCreate)] = o.Scripts.OnCreate;
        obs.slots[static_cast<size_t>(MiniGameSlot::OnHitBullet)] = o.Scripts.OnHitBullet;
        obs.slots[static_cast<size_t>(MiniGameSlot::OnHitFollower)] = o.Scripts.OnHitFollower;
        obs.slots[static_cast<size_t>(MiniGameSlot::OnAnimEvent)] = o.Scripts.OnAnimEvent;
        obs.slots[static_cast<size_t>(MiniGameSlot::OnHeartbeat)] = o.Scripts.OnHeartbeat;
        spec.obstacles.push_back(std::move(obs));
    }

    return spec;
}

// Tracks

namespace {

constexpr float kTrackRateDivisor = 100.0f; // speed 100 plays a track as authored
constexpr float kDefaultSwoopMovementPerSec = 6.0f;
constexpr float kTunnelWallTolerance = 0.01f;
constexpr float kBumpClearance = 0.1f;

template <class Key, class Value, class Mix>
bool sampleKeys(const std::vector<Key> &keys, float time, Value &out, Mix mix) {
    if (keys.empty()) {
        return false;
    }
    if (keys.size() == 1 || time <= keys.front().time) {
        out = keys.front().value;
        return true;
    }
    if (time >= keys.back().time) {
        out = keys.back().value;
        return true;
    }
    auto right = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const Key &k) {
        return t < k.time;
    });
    auto left = right - 1;
    float span = right->time - left->time;
    float factor = span > 0.0f ? (time - left->time) / span : 0.0f;
    out = mix(left->value, right->value, factor);
    return true;
}

} // namespace

MiniGameTrack::MiniGameTrack(glm::mat4 base,
                             glm::vec3 restPosition,
                             glm::quat restOrientation,
                             std::vector<PositionKey> positions,
                             std::vector<OrientationKey> orientations,
                             float length) :
    _base(base),
    _restPosition(restPosition),
    _restOrientation(restOrientation),
    _positions(std::move(positions)),
    _orientations(std::move(orientations)),
    _length(length) {
    std::sort(_positions.begin(), _positions.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
    std::sort(_orientations.begin(), _orientations.end(), [](const auto &a, const auto &b) { return a.time < b.time; });
}

MiniGameTrack MiniGameTrack::fixed(const glm::vec3 &position) {
    return MiniGameTrack(glm::translate(position), glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {}, {}, 0.0f);
}

glm::mat4 MiniGameTrack::hookAt(float time) const {
    glm::vec3 delta(0.0f);
    sampleKeys(_positions, time, delta, [](const glm::vec3 &a, const glm::vec3 &b, float f) {
        return glm::mix(a, b, f);
    });
    glm::quat orientation(_restOrientation);
    sampleKeys(_orientations, time, orientation, [](const glm::quat &a, const glm::quat &b, float f) {
        return glm::slerp(a, b, f);
    });
    return _base * glm::translate(_restPosition + delta) * glm::mat4_cast(orientation);
}

MiniGameTrack buildMiniGameTrack(const graphics::Model *model, const glm::vec3 &placement) {
    if (!model) {
        return MiniGameTrack::fixed(placement);
    }
    auto hook = model->getNodeByNameRecursive("modelhook");
    if (!hook) {
        return MiniGameTrack::fixed(placement);
    }
    glm::mat4 base = glm::translate(placement);
    if (hook->parent()) {
        base *= hook->parent()->absoluteTransform();
    }
    std::vector<MiniGameTrack::PositionKey> positions;
    std::vector<MiniGameTrack::OrientationKey> orientations;
    float length = 0.0f;
    if (auto anim = model->getAnimation("track")) {
        length = anim->length();
        if (auto animHook = anim->getNodeByName("modelhook")) {
            auto &vectors = animHook->vectorTracks();
            auto pos = vectors.find(graphics::ControllerTypes::position);
            if (pos != vectors.end()) {
                for (const auto &kf : pos->second.keyframes()) {
                    positions.push_back({kf.time, kf.value});
                }
            }
            auto &quats = animHook->quaternionTracks();
            auto ori = quats.find(graphics::ControllerTypes::orientation);
            if (ori != quats.end()) {
                for (const auto &kf : ori->second.keyframes()) {
                    orientations.push_back({kf.time, kf.value});
                }
            }
        }
    }
    return MiniGameTrack(base, hook->restPosition(), hook->restOrientation(),
                         std::move(positions), std::move(orientations), length);
}

// Geometry

float miniGameSegmentDistance(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &q0, const glm::vec3 &q1) {
    glm::vec3 d1 = p1 - p0;
    glm::vec3 d2 = q1 - q0;
    glm::vec3 r = p0 - q0;
    float a = glm::dot(d1, d1);
    float e = glm::dot(d2, d2);
    float f = glm::dot(d2, r);
    float s = 0.0f;
    float t = 0.0f;
    constexpr float eps = 1e-8f;
    if (a <= eps && e <= eps) {
        return glm::length(r);
    }
    if (a <= eps) {
        t = glm::clamp(f / e, 0.0f, 1.0f);
    } else {
        float c = glm::dot(d1, r);
        if (e <= eps) {
            s = glm::clamp(-c / a, 0.0f, 1.0f);
        } else {
            float b = glm::dot(d1, d2);
            float denom = a * e - b * b;
            s = denom > eps ? glm::clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = glm::clamp(-c / a, 0.0f, 1.0f);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = glm::clamp((b - c) / a, 0.0f, 1.0f);
            }
        }
    }
    return glm::length((p0 + d1 * s) - (q0 + d2 * t));
}

bool miniGameSegmentHitsBox2D(const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &boxMin, const glm::vec3 &boxMax) {
    float tmin = 0.0f;
    float tmax = 1.0f;
    for (int axis = 0; axis < 2; ++axis) {
        float origin = p0[axis];
        float dir = p1[axis] - p0[axis];
        if (glm::abs(dir) < 1e-8f) {
            if (origin < boxMin[axis] || origin > boxMax[axis]) {
                return false;
            }
            continue;
        }
        float t1 = (boxMin[axis] - origin) / dir;
        float t2 = (boxMax[axis] - origin) / dir;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tmin = glm::max(tmin, t1);
        tmax = glm::min(tmax, t2);
        if (tmin > tmax) {
            return false;
        }
    }
    return true;
}

int pazaakCardIndexForModelVariation(int modelVariation, bool tsl) {
    if (modelVariation >= 1 && modelVariation <= 18) {
        return (modelVariation + 11) % 18;
    }
    if (!tsl) {
        return -1;
    }
    switch (modelVariation) {
    case 19:
        return 22;
    case 20:
        return 19;
    case 21:
        return 18;
    case 22:
        return 20;
    case 23:
        return 21;
    default:
        return -1;
    }
}

// Session

void MiniGameSession::load(const MinigameSpec &spec,
                           MiniGameTrack playerTrack,
                           std::vector<MiniGameTrack> enemyTracks,
                           std::vector<ObstaclePlacement> obstacles) {
    _actors.clear();
    _enemyList.clear();
    _obstacleList.clear();
    _ticks = 0;
    _elapsed = 0.0f;
    _scriptsRun = 0;
    _started = false;
    _speedHeld = false;
    _lateralVelocity = glm::vec3(0.0f);
    _previousInput = glm::vec3(0.0f);

    _type = spec.type;
    _lateralAccel = spec.lateralAccel;
    _movementPerSec = spec.movementPerSec;
    if (_movementPerSec <= 0.0f && _type == MinigameType::SwoopRace) {
        _movementPerSec = kDefaultSwoopMovementPerSec;
    }
    _useInertia = spec.useInertia;
    _doBumping = spec.doBumping;
    _nearClip = spec.nearClip;
    _farClip = spec.farClip;
    _speedBlur = false;
    _speedBlurRatio = 0.0f;

    auto applyFollower = [](MiniGameActor &actor,
                            uint32_t hitPoints,
                            uint32_t maxHitPoints,
                            float sphereRadius,
                            float invincePeriod,
                            int bumpDamage,
                            int numLoops) {
        // The follower setters keep their defaults for out-of-range values.
        if (static_cast<int>(hitPoints) > 0) {
            actor.hitPoints = static_cast<int>(hitPoints);
        }
        if (static_cast<int>(maxHitPoints) > 0) {
            actor.maxHitPoints = static_cast<int>(maxHitPoints);
        }
        if (sphereRadius >= 0.0f) {
            actor.sphereRadius = sphereRadius;
        }
        if (invincePeriod >= 0.0f) {
            actor.invincePeriod = invincePeriod;
        }
        actor.bumpDamage = bumpDamage;
        actor.numLoops = numLoops;
    };

    // Player
    MiniGameActor player;
    player.kind = MiniGameActorKind::Player;
    player.models = spec.player.models;
    player.name = spec.player.models.empty() ? std::string() : spec.player.models.front().resRef;
    player.scripts = spec.player.slots;
    player.sounds = spec.player.sounds;
    applyFollower(player, spec.player.hitPoints, spec.player.maxHitPoints, spec.player.sphereRadius,
                  spec.player.invincePeriod, spec.player.bumpDamage, spec.player.numLoops);
    for (const auto &bank : spec.player.gunBanks) {
        player.gunBanks.push_back({bank, 0.0f});
    }
    player.track = std::move(playerTrack);
    _actors.push_back(std::move(player));

    _minSpeed = spec.player.minimumSpeed;
    _maxSpeed = 0.0f;
    if (spec.player.maximumSpeed >= 0.0f) {
        _maxSpeed = spec.player.maximumSpeed;
    }
    float accelSecs = spec.player.accelSecs;
    if (accelSecs == 0.0f) {
        _accelPerSecond = _maxSpeed - _minSpeed;
    } else if (accelSecs < 0.0f) {
        _accelPerSecond = -1.0f;
    } else {
        _accelPerSecond = (_maxSpeed - _minSpeed) / accelSecs;
    }
    _tunnelPos = glm::vec3(spec.player.tunnelXPos, spec.player.tunnelYPos, spec.player.tunnelZPos);
    _tunnelNeg = glm::vec3(spec.player.tunnelXNeg, spec.player.tunnelYNeg, spec.player.tunnelZNeg);
    _tunnelInfinite = spec.player.tunnelInfinite;
    _startOffset = spec.player.startOffset;
    _offset = _startOffset;
    _targetOffset = spec.player.targetOffset;

    // Enemies
    for (size_t i = 0; i < spec.enemies.size(); ++i) {
        const auto &enemySpec = spec.enemies[i];
        MiniGameActor enemy;
        enemy.kind = MiniGameActorKind::Enemy;
        enemy.specIndex = i;
        enemy.models = enemySpec.models;
        enemy.name = enemySpec.models.empty() ? enemySpec.trackResRef : enemySpec.models.front().resRef;
        enemy.scripts = enemySpec.slots;
        enemy.sounds = enemySpec.sounds;
        enemy.trigger = enemySpec.trigger;
        applyFollower(enemy, enemySpec.hitPoints, enemySpec.maxHitPoints, enemySpec.sphereRadius,
                      enemySpec.invincePeriod, enemySpec.bumpDamage, enemySpec.numLoops);
        for (const auto &bank : enemySpec.gunBanks) {
            enemy.gunBanks.push_back({bank, 0.0f});
        }
        if (i < enemyTracks.size()) {
            enemy.track = std::move(enemyTracks[i]);
        }
        _enemyList.push_back(_actors.size());
        _actors.push_back(std::move(enemy));
    }

    // Obstacles
    for (size_t i = 0; i < obstacles.size(); ++i) {
        auto &placement = obstacles[i];
        MiniGameActor obstacle;
        obstacle.kind = MiniGameActorKind::Obstacle;
        obstacle.specIndex = i;
        obstacle.name = placement.name;
        if (i < spec.obstacles.size()) {
            obstacle.scripts = spec.obstacles[i].slots;
        }
        obstacle.track = MiniGameTrack::fixed(placement.position);
        obstacle.hasHitbump = placement.hasHitbump;
        obstacle.hitbumpMin = placement.hitbumpMin;
        obstacle.hitbumpMax = placement.hitbumpMax;
        _obstacleList.push_back(_actors.size());
        _actors.push_back(std::move(obstacle));
    }

    for (auto &actor : _actors) {
        placeActor(actor);
        actor.previousPosition = actor.position;
    }
}

void MiniGameSession::start() {
    if (_started || _actors.empty()) {
        return;
    }
    _started = true;
    // Creation order: the player, then the enemies in file order, then the
    // obstacles. The lists are copied: a creation script may change them.
    std::vector<size_t> order;
    order.push_back(0);
    order.insert(order.end(), _enemyList.begin(), _enemyList.end());
    order.insert(order.end(), _obstacleList.begin(), _obstacleList.end());
    for (size_t index : order) {
        runScriptSlot(_actors[index], MiniGameSlot::OnCreate);
    }
}

bool MiniGameSession::runScriptSlot(MiniGameActor &actor, MiniGameSlot slot) {
    // Every slot runs from here: OnCreate OnHitBullet OnHitFollower
    // OnAnimEvent OnHeartbeat OnDamage OnDeath OnFire OnHitObstacle
    // OnTrackLoop OnAccelerate OnBrake OnHitWorld.
    const std::string &script = actor.scripts[static_cast<size_t>(slot)];
    if (script.empty()) {
        return false;
    }
    ++_scriptsRun;
    _host.runScript(script, actor.objectId);
    // An authored script replaces the engine default; it can still ask for
    // it through the SWMG_On* routines.
    return true;
}

void MiniGameSession::update(float dt, const MiniGameInput &input) {
    if (_actors.empty() || dt <= 0.0f) {
        return;
    }
    ++_ticks;
    _elapsed += dt;

    auto &playerActor = _actors.front();
    if (!playerActor.dead) {
        updatePlayerMotion(dt, input);
    }

    // Object updates: the player and every listed enemy run their heartbeat
    // and count down their invulnerability; enemies move along their tracks.
    std::vector<size_t> order;
    if (!playerActor.dead) {
        order.push_back(0);
    }
    order.insert(order.end(), _enemyList.begin(), _enemyList.end());
    for (size_t index : order) {
        auto &actor = _actors[index];
        if (index != 0) {
            actor.previousPosition = actor.position;
            advanceTrack(actor, dt);
            placeActor(actor);
        }
        updateFollower(actor, dt);
    }
    std::vector<size_t> obstacles(_obstacleList);
    for (size_t index : obstacles) {
        runScriptSlot(_actors[index], MiniGameSlot::OnHeartbeat);
    }

    updatePlayerGuns(dt, input.fire);

    if (!_actors.front().dead) {
        collideObstacles();
    }
    if (!_actors.front().dead) {
        collideFollowers();
    }
}

void MiniGameSession::updateFollower(MiniGameActor &actor, float dt) {
    runScriptSlot(actor, MiniGameSlot::OnHeartbeat);
    if (actor.invulnerableTime > 0.0f) {
        actor.invulnerableTime = glm::max(0.0f, actor.invulnerableTime - dt);
    }
}

void MiniGameSession::updatePlayerMotion(float dt, const MiniGameInput &input) {
    auto &p = _actors.front();
    p.previousPosition = p.position;

    // Speed moves toward the maximum at the acceleration per second, unless a
    // bump already set it this update.
    if (!_speedHeld && p.speed != _maxSpeed) {
        float step = _accelPerSecond * dt;
        if (_maxSpeed <= p.speed) {
            p.speed = (p.speed - step <= _maxSpeed) ? _maxSpeed : p.speed - step;
        } else {
            p.speed = (_maxSpeed <= p.speed + step) ? _maxSpeed : p.speed + step;
        }
    }
    _speedHeld = false;

    // Lateral steering (translation minigames). The turret reports its aim
    // through setPlayerAim instead.
    if (_type == MinigameType::SwoopRace) {
        glm::vec3 in(glm::clamp(input.steer, -1.0f, 1.0f), 0.0f, 0.0f);
        glm::vec3 delta(0.0f);
        float top = _movementPerSec;
        if (_useInertia && top > 0.0f) {
            // dv/dt = L * u - (L / M) * v, the input blended from the previous
            // update's to this one's; 4th-order Runge-Kutta.
            float L = _lateralAccel;
            float damping = L / top;
            glm::vec3 u0 = _previousInput;
            glm::vec3 um = (_previousInput + in) * 0.5f;
            glm::vec3 u1 = in;
            glm::vec3 v = _lateralVelocity;
            auto accel = [&](const glm::vec3 &vel, const glm::vec3 &u) {
                return L * u - damping * vel;
            };
            glm::vec3 k1v = accel(v, u0);
            glm::vec3 k1x = v;
            glm::vec3 k2v = accel(v + 0.5f * dt * k1v, um);
            glm::vec3 k2x = v + 0.5f * dt * k1v;
            glm::vec3 k3v = accel(v + 0.5f * dt * k2v, um);
            glm::vec3 k3x = v + 0.5f * dt * k2v;
            glm::vec3 k4v = accel(v + dt * k3v, u1);
            glm::vec3 k4x = v + dt * k3v;
            _lateralVelocity = v + (dt / 6.0f) * (k1v + 2.0f * k2v + 2.0f * k3v + k4v);
            delta = (dt / 6.0f) * (k1x + 2.0f * k2x + 2.0f * k3x + k4x);
        } else {
            _lateralVelocity = in * top;
            delta = _lateralVelocity * dt;
        }
        _previousInput = in;

        glm::vec3 unclamped = _offset + delta;
        glm::vec3 clamped = unclamped;
        clampOffset(clamped);
        if (glm::abs(unclamped.x - clamped.x) > kTunnelWallTolerance) {
            _lateralVelocity = glm::vec3(0.0f);
        }
        _offset = clamped;
    }

    advanceTrack(p, dt);
    placeActor(p);
}

void MiniGameSession::clampOffset(glm::vec3 &offset) const {
    for (int axis = 0; axis < 3; ++axis) {
        if (_tunnelInfinite[axis] == 0.0f) {
            float hi = _startOffset[axis] + _tunnelPos[axis];
            float lo = _startOffset[axis] + _tunnelNeg[axis];
            if (offset[axis] > hi) {
                offset[axis] = hi;
            } else if (offset[axis] < lo) {
                offset[axis] = lo;
            }
        } else if (_type == MinigameType::Turret) {
            if (offset[axis] < 0.0f) {
                offset[axis] += 360.0f;
            } else if (offset[axis] > 359.0f) {
                offset[axis] -= 360.0f;
            }
        }
    }
}

void MiniGameSession::advanceTrack(MiniGameActor &actor, float dt) {
    if (actor.drivenExternally || !actor.trackRunning) {
        return;
    }
    float length = actor.track.length();
    if (length <= 0.0f) {
        return;
    }
    actor.trackTime += dt * actor.speed / kTrackRateDivisor;
    while (actor.trackTime >= length) {
        if (actor.numLoops < 0) {
            actor.trackTime -= length;
        } else if (actor.numLoops > 1) {
            --actor.numLoops;
            actor.trackTime -= length;
        } else {
            actor.trackTime = length;
            break;
        }
        runScriptSlot(actor, MiniGameSlot::OnTrackLoop);
    }
    while (actor.trackTime < 0.0f) {
        actor.trackTime += length;
    }
}

void MiniGameSession::placeActor(MiniGameActor &actor) {
    if (actor.drivenExternally) {
        return;
    }
    actor.hook = actor.track.hookAt(actor.trackTime);
    if (actor.kind == MiniGameActorKind::Player && _type == MinigameType::SwoopRace) {
        actor.position = glm::vec3(actor.hook * glm::vec4(_offset, 1.0f));
    } else {
        actor.position = glm::vec3(actor.hook[3]);
    }
}

void MiniGameSession::setActorPosition(MiniGameActor &actor, const glm::vec3 &position, const glm::quat &orientation) {
    actor.drivenExternally = true;
    actor.previousPosition = actor.position;
    actor.position = position;
    actor.hook = glm::translate(position) * glm::mat4_cast(orientation);
}

void MiniGameSession::updatePlayerGuns(float dt, bool fire) {
    auto &p = _actors.front();
    for (size_t i = 0; i < p.gunBanks.size(); ++i) {
        auto &bank = p.gunBanks[i];
        if (bank.cooldown > 0.0f) {
            bank.cooldown = glm::max(0.0f, bank.cooldown - dt);
        }
        if (!fire || p.dead || bank.cooldown > 0.0f) {
            continue;
        }
        bank.cooldown = bank.spec.bullet.rateOfFire;
        _host.onMiniGameGunFired(p, i);
        gunFired(p, i);
    }
}

void MiniGameSession::gunFired(MiniGameActor &actor, size_t bankIndex) {
    if (bankIndex < actor.gunBanks.size()) {
        const auto &bullet = actor.gunBanks[bankIndex].spec.bullet;
        actor.lastBulletFiredDamage = static_cast<int>(bullet.damage);
        actor.lastBulletFiredTarget = static_cast<int>(bullet.targetType);
    }
    runScriptSlot(actor, MiniGameSlot::OnFire);
}

void MiniGameSession::collideObstacles() {
    auto &p = _actors.front();
    std::vector<size_t> obstacles(_obstacleList);
    for (size_t index : obstacles) {
        auto &o = _actors[index];
        if (!o.hasHitbump) {
            continue;
        }
        // A hit is the bike's movement this update entering the hitbump.
        bool wasInside = p.previousPosition.x >= o.hitbumpMin.x && p.previousPosition.x <= o.hitbumpMax.x &&
                         p.previousPosition.y >= o.hitbumpMin.y && p.previousPosition.y <= o.hitbumpMax.y;
        if (wasInside || !miniGameSegmentHitsBox2D(p.previousPosition, p.position, o.hitbumpMin, o.hitbumpMax)) {
            continue;
        }
        p.lastObstacleHit = o.objectId;
        o.lastFollowerHit = p.objectId;
        if (!runScriptSlot(o, MiniGameSlot::OnHitFollower) && !runScriptSlot(p, MiniGameSlot::OnHitObstacle)) {
            defaultObstacleHit(p);
        }
        if (p.dead) {
            return;
        }
    }
}

void MiniGameSession::defaultObstacleHit(MiniGameActor &follower) {
    // A swoop bike shrugs an obstacle off by default; anything else dies.
    if (_type == MinigameType::SwoopRace) {
        return;
    }
    kill(follower);
}

void MiniGameSession::collideFollowers() {
    auto &p = _actors.front();
    std::vector<size_t> enemies(_enemyList);
    glm::vec3 push(0.0f);
    int contacts = 0;
    int bump = 0;
    for (auto it = enemies.rbegin(); it != enemies.rend(); ++it) {
        auto &e = _actors[*it];
        if (e.dead || !(e.trigger || _doBumping)) {
            continue;
        }
        float distance = miniGameSegmentDistance(p.previousPosition, p.position, e.previousPosition, e.position);
        float reach = p.sphereRadius + e.sphereRadius;
        if (distance >= reach) {
            continue;
        }
        if (!e.trigger) {
            glm::vec3 away = p.position - e.position;
            float len = glm::length(away);
            glm::vec3 dir = len > 1e-6f ? away / len : glm::vec3(1.0f, 0.0f, 0.0f);
            push += (e.position + dir * (reach + kBumpClearance)) - p.position;
            if (!e.invulnerable()) {
                playAnimation(e, "damage", false, false, false);
                playAnimation(e, "Ready_01", true, true, false);
                changeHitPoints(e, -p.bumpDamage);
                e.invulnerableTime = e.invincePeriod;
            }
            bump = glm::max(bump, e.bumpDamage);
        }
        p.lastFollowerHit = e.objectId;
        runScriptSlot(p, MiniGameSlot::OnHitFollower);
        e.lastFollowerHit = p.objectId;
        runScriptSlot(e, MiniGameSlot::OnHitFollower);
        ++contacts;
        if (p.dead) {
            return;
        }
    }
    if (contacts == 0) {
        return;
    }
    if (_type == MinigameType::SwoopRace) {
        glm::vec3 local = glm::vec3(glm::inverse(p.hook) * glm::vec4(p.position + push / static_cast<float>(contacts), 1.0f));
        glm::vec3 moved = local;
        clampOffset(moved);
        _offset = moved;
        placeActor(p);
    }
    if (!p.invulnerable()) {
        p.invulnerableTime = p.invincePeriod + p.invincePeriod;
        changeHitPoints(p, -bump);
        _speedHeld = true;
    }
}

void MiniGameSession::changeHitPoints(MiniGameActor &actor, int delta) {
    if (!actor.follower()) {
        return;
    }
    actor.lastHPChange = delta;
    if (!runScriptSlot(actor, MiniGameSlot::OnDamage)) {
        defaultDamage(actor);
    }
}

void MiniGameSession::defaultDamage(MiniGameActor &actor) {
    if (!actor.follower()) {
        return;
    }
    actor.hitPoints += actor.lastHPChange;
    if (actor.lastHPChange < 0) {
        if (actor.hitPoints < 1) {
            kill(actor);
        } else {
            playAnimation(actor, "damage", false, false, false);
            playAnimation(actor, "Ready_01", true, true, false);
        }
    } else {
        actor.hitPoints = glm::min(actor.hitPoints, actor.maxHitPoints);
    }
}

void MiniGameSession::kill(MiniGameActor &actor) {
    if (actor.dead) {
        return;
    }
    actor.dead = true;
    if (!runScriptSlot(actor, MiniGameSlot::OnDeath)) {
        defaultDeath(actor);
    }
}

void MiniGameSession::defaultDeath(MiniGameActor &actor) {
    actor.dead = true;
    actor.trackRunning = false;
    if (!actor.sounds.death.empty()) {
        _host.playMiniGameSound(actor.sounds.death, actor.position);
    }
    _host.onMiniGameActorDied(actor);
    actor.gunBanks.clear();
    unlist(actor);
}

void MiniGameSession::unlist(MiniGameActor &actor) {
    size_t index = static_cast<size_t>(&actor - _actors.data());
    auto drop = [index](std::vector<size_t> &list) {
        list.erase(std::remove(list.begin(), list.end(), index), list.end());
    };
    drop(_enemyList);
    drop(_obstacleList);
    actor.listed = false;
}

void MiniGameSession::bulletHit(MiniGameActor &target, int damage, int targetType, uint32_t shooter, const std::string &part) {
    target.lastBulletHitDamage = damage;
    target.lastBulletHitTarget = targetType;
    target.lastBulletHitShooter = shooter;
    target.lastBulletHitPart = part;
    if (!runScriptSlot(target, MiniGameSlot::OnHitBullet)) {
        defaultBulletHit(target);
    }
}

void MiniGameSession::defaultBulletHit(MiniGameActor &target) {
    if (target.follower()) {
        changeHitPoints(target, -target.lastBulletHitDamage);
    }
}

void MiniGameSession::playAnimation(MiniGameActor &actor, const std::string &name, bool looping, bool queue, bool overlay) {
    _host.playMiniGameAnimation(actor, name, looping, queue, overlay);
}

void MiniGameSession::removeAnimation(MiniGameActor &actor, const std::string &name) {
    _host.removeMiniGameAnimation(actor, name);
}

void MiniGameSession::setPlayerSpeed(float speed) {
    if (!_actors.empty()) {
        _actors.front().speed = speed;
    }
}

void MiniGameSession::setPlayerMaxSpeed(float speed) {
    if (speed >= 0.0f) {
        _maxSpeed = speed;
    }
}

void MiniGameSession::setPlayerOffset(const glm::vec3 &offset) {
    _offset = offset;
    clampOffset(_offset);
    if (!_actors.empty()) {
        placeActor(_actors.front());
    }
}

void MiniGameSession::setPlayerOrigin(const glm::vec3 &origin) {
    _startOffset = origin;
    if (!_started) {
        _offset = origin;
    }
}

float MiniGameSession::playerSpeed() const {
    return _actors.empty() ? 0.0f : _actors.front().speed;
}

MiniGameActor *MiniGameSession::actorByObjectId(uint32_t id) {
    for (auto &actor : _actors) {
        if (actor.objectId == id) {
            return &actor;
        }
    }
    return nullptr;
}

MiniGameActor *MiniGameSession::player() {
    return _actors.empty() ? nullptr : &_actors.front();
}

const MiniGameActor *MiniGameSession::player() const {
    return _actors.empty() ? nullptr : &_actors.front();
}

MiniGameActor *MiniGameSession::enemy(int index) {
    if (index < 0 || index >= static_cast<int>(_enemyList.size())) {
        return nullptr;
    }
    return &_actors[_enemyList[index]];
}

MiniGameActor *MiniGameSession::obstacle(int index) {
    if (index < 0 || index >= static_cast<int>(_obstacleList.size())) {
        return nullptr;
    }
    return &_actors[_obstacleList[index]];
}

MiniGameActor *MiniGameSession::findByName(const std::string &name) {
    for (auto &actor : _actors) {
        if (boost::iequals(actor.name, name)) {
            return &actor;
        }
        for (const auto &model : actor.models) {
            if (boost::iequals(model.resRef, name)) {
                return &actor;
            }
        }
    }
    return nullptr;
}

} // namespace game

} // namespace reone
