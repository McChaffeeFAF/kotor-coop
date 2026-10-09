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

#include <cstdint>

namespace reone {

/**
 * A small portable pseudo-random generator (xoshiro128**, seeded through
 * splitmix64) with its own bounded-integer and float mappings. Unlike
 * std::default_random_engine with std::uniform_*_distribution, the sequence a
 * seed produces is the same on every compiler and standard library, which a
 * host-authoritative simulation needs.
 */
class RandomGenerator {
public:
    explicit RandomGenerator(uint64_t seed = 0) { this->seed(seed); }

    void seed(uint64_t seed);

    uint32_t next();

    /**
     * @param min lower bound (inclusive)
     * @param max upper bound (inclusive)
     */
    int uniformInt(int min, int max);

    /**
     * @return a value in [min, max)
     */
    float uniformFloat(float min, float max);

    uint64_t seedValue() const { return _seed; }

    /** Number of 32-bit values drawn since the last seed. */
    uint64_t draws() const { return _draws; }

private:
    uint32_t _state[4] {};
    uint64_t _seed {0};
    uint64_t _draws {0};
};

/**
 * Gameplay stream: host-owned and seedable. Dice, saving throws, the script
 * Random/d2-d100 routines, AI and movement random picks, character appearance
 * and name rolls, and anything else that changes simulation state draw from
 * this stream, from simulation code only. Rendering, audio and
 * frame-rate-dependent code must never draw from it: use
 * cosmeticRandomInt/cosmeticRandomFloat there.
 */
RandomGenerator &gameplayRandom();

/**
 * Cosmetic stream: particles, grass, footstep and soundset picks, dialog
 * camera variants, projectile jitter and other visual or audio-only choices.
 * It may be drawn at any rate, from any frame.
 */
RandomGenerator &cosmeticRandom();

/** Reseeds the gameplay stream, for deterministic scripted runs. */
void setRandomSeed(uint32_t seed);

/** Reseeds the cosmetic stream. */
void setCosmeticRandomSeed(uint32_t seed);

/**
 * Gameplay stream.
 *
 * @param min lower bound (inclusive)
 * @param max upper bound (inclusive)
 */
int randomInt(int min, int max);

/**
 * Gameplay stream.
 *
 * @return a value in [min, max)
 */
float randomFloat(float min, float max);

/** Cosmetic stream, inclusive bounds. */
int cosmeticRandomInt(int min, int max);

/** Cosmetic stream, a value in [min, max). */
float cosmeticRandomFloat(float min, float max);

/**
 * Presentation guard. While one is alive (the game holds one around
 * rendering and per-frame presentation), a draw from the gameplay stream is
 * counted as a violation and logged the first time. The draw still happens,
 * so a violation surfaces as a determinism failure rather than a crash.
 */
class GameplayRandomForbidden {
public:
    GameplayRandomForbidden();
    ~GameplayRandomForbidden();

    GameplayRandomForbidden(const GameplayRandomForbidden &) = delete;
    GameplayRandomForbidden &operator=(const GameplayRandomForbidden &) = delete;
};

/** Gameplay-stream draws made while a GameplayRandomForbidden was alive. */
uint64_t gameplayRandomViolations();

} // namespace reone
