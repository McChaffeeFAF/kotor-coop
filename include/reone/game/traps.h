/*
 * Copyright (c) 2026 The reone project contributors
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

#include <optional>
#include <memory>
#include <string>

#include <glm/vec3.hpp>

#include "types.h"

namespace reone {

namespace resource {

class ITwoDAs;

} // namespace resource

namespace game {

// Traps, mines and the Demolitions skill (design note c11_data, "Traps").

class Area;
class Creature;
class Game;
class Item;
struct ServicesView;
class Trigger;

/** The trap fields a trapped trigger, door or placeable carries. */
struct TrapFields {
    bool trapped {false};
    bool detectable {false};
    bool disarmable {false};
    bool oneShot {true};
    int baseType {0};
    int detectDC {0};
    int disarmDC {0};
    std::string keyTag;
};

/** One traps.2da row. */
struct TrapRow {
    std::string script;
    int setDC {0};
    int detectDCMod {0};
    int disarmDCMod {0};
    int nameStrRef {-1};
    std::string kitResRef;
    std::string model;
    std::string explosionSound;
};

constexpr int kTriggerTypeTrap = 2;
constexpr int kItemPropertyTrap = 46;
constexpr float kTrapDetectInterval = 3.0f;
constexpr float kTrapDetectRange = 3.0f;
constexpr float kTrapApproachDistance = 1.5f;
constexpr float kMineHalfSize = 2.0f;
constexpr float kSetMineDuration = 2.0f;
/** No roll disarms or recovers a trap at this DC or above. */
constexpr int kUnbeatableTrapDC = 66;

std::optional<TrapRow> loadTrapRow(resource::ITwoDAs &twoDas, int row);

/** Easy −5, normal 0, difficult +5 (options clientDifficulty 0/1/2). */
int trapDifficultyShift(int clientDifficulty);

/**
 * A trap fires on a creature whose reputation toward the trap's faction is
 * 10 or less and whose faction is not the trap's.
 */
bool trapFiresOn(int reputation, Faction trapFaction, Faction creatureFaction);

/** Awareness + d10 against the detect DC. */
bool trapDetectRollSucceeds(int awareness, int d10, int detectDC);

enum class TrapSkillOutcome {
    Success,
    Failure,
    /** Failed by enough to set the trap off. */
    Triggered
};

/** Disarm: total against the DC; failing by more than 10 sets it off. */
TrapSkillOutcome resolveDisarm(int total, int disarmDC);

/** Recover: total against DC + 10; failing by more than 5 sets it off. */
TrapSkillOutcome resolveRecover(int total, int disarmDC);

enum class SetMineOutcome {
    Success,
    Failure,
    CriticalFailure
};

/** Setting a mine; only a real d20 roll (not taking 20) fails critically. */
SetMineOutcome resolveSetMine(int total, int setDC, bool tookTwenty);

/** XP for a disarm or recover: level × 10, or × 15 when DC ≥ level + 20. */
int trapSkillXP(int level, int dc);

/** traps.2da row of a mine item: the subtype of its first property when that is type 46, otherwise -1. */
int mineTrapRow(const Item &item);

/** The d20 part of a Demolitions roll: 20 out of combat (take 20), else a d20. */
int demolitionsRoll(const Creature &creature, bool &tookTwenty);

/**
 * Party members near an undetected trap roll to detect it, every
 * kTrapDetectInterval seconds (the caller times it). Tick only.
 */
void updateTrapDetection(Game &game, Area &area);

enum class TrapSkill {
    Disarm,
    Recover,
    Flag,
    Examine
};

/**
 * Walks actor to the trap and applies the skill. Returns true once the action
 * is over (done, failed or impossible).
 */
bool performTrapSkill(Game &game, ServicesView &services, Creature &actor, Trigger &trap, TrapSkill skill, float dt);

/**
 * Lays the mine item at position. Returns the mine trigger on success or on a
 * critical failure (which also sets it off on the setter); nullptr otherwise.
 */
/**
 * Lays a mine of a traps.2da row at position without a setter's skill roll or
 * a mine item (SpawnMine). Returns the mine trigger, nullptr when the row or
 * the area is missing.
 */
std::shared_ptr<Trigger> spawnMine(Game &game, ServicesView &services, int trapRow, const glm::vec3 &position, Faction faction,
                                   uint32_t creatorId, int detectDC, int disarmDC);

std::shared_ptr<Trigger> setMine(Game &game, ServicesView &services, Creature &setter, Item &mine, const glm::vec3 &position);

} // namespace game

} // namespace reone
