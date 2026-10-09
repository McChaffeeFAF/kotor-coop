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

#include "reone/system/randomutil.h"

#include "reone/system/logutil.h"

#include <ctime>

namespace reone {

static uint64_t splitmix64(uint64_t &x) {
    uint64_t z = (x += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

static inline uint32_t rotl(uint32_t x, int k) {
    return (x << k) | (x >> (32 - k));
}

void RandomGenerator::seed(uint64_t seed) {
    _seed = seed;
    _draws = 0;
    uint64_t x = seed;
    uint64_t a = splitmix64(x);
    uint64_t b = splitmix64(x);
    _state[0] = static_cast<uint32_t>(a);
    _state[1] = static_cast<uint32_t>(a >> 32);
    _state[2] = static_cast<uint32_t>(b);
    _state[3] = static_cast<uint32_t>(b >> 32);
    if ((_state[0] | _state[1] | _state[2] | _state[3]) == 0) {
        _state[0] = 1; // the all-zero state is a fixed point
    }
}

uint32_t RandomGenerator::next() {
    ++_draws;
    uint32_t result = rotl(_state[1] * 5, 7) * 9;
    uint32_t t = _state[1] << 9;
    _state[2] ^= _state[0];
    _state[3] ^= _state[1];
    _state[1] ^= _state[2];
    _state[0] ^= _state[3];
    _state[2] ^= t;
    _state[3] = rotl(_state[3], 11);
    return result;
}

int RandomGenerator::uniformInt(int min, int max) {
    if (max <= min) {
        return min;
    }
    // Lemire multiply-shift with rejection: unbiased, and the same result for
    // a given state on every platform.
    uint32_t range = static_cast<uint32_t>(static_cast<int64_t>(max) - min + 1);
    if (range == 0) {
        return static_cast<int>(next()); // the full 32-bit range
    }
    uint64_t m = static_cast<uint64_t>(next()) * range;
    uint32_t low = static_cast<uint32_t>(m);
    if (low < range) {
        uint32_t threshold = (0u - range) % range;
        while (low < threshold) {
            m = static_cast<uint64_t>(next()) * range;
            low = static_cast<uint32_t>(m);
        }
    }
    return static_cast<int>(static_cast<int64_t>(min) + static_cast<int64_t>(m >> 32));
}

float RandomGenerator::uniformFloat(float min, float max) {
    // 24 random bits fill a float mantissa exactly, so unit is in [0, 1).
    float unit = static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
    float value = min + (max - min) * unit;
    // Rounding can land exactly on max for wide ranges; keep it half-open.
    return (value < max || max <= min) ? value : min;
}

static RandomGenerator &gameplayGenerator() {
    static RandomGenerator generator(static_cast<uint64_t>(time(nullptr)));
    return generator;
}

static RandomGenerator &cosmeticGenerator() {
    // Distinct from the gameplay default, so the two never run in lockstep.
    static RandomGenerator generator(static_cast<uint64_t>(time(nullptr)) ^ 0xc05e7a1c5eedull);
    return generator;
}

static int g_forbiddenDepth = 0;
static uint64_t g_violations = 0;

static void checkGameplayDraw() {
    if (g_forbiddenDepth > 0 && g_violations++ == 0) {
        warn("Gameplay random stream drawn from presentation code; use cosmeticRandomInt/Float");
    }
}

RandomGenerator &gameplayRandom() {
    return gameplayGenerator();
}

RandomGenerator &cosmeticRandom() {
    return cosmeticGenerator();
}

void setRandomSeed(uint32_t seed) {
    gameplayGenerator().seed(seed);
    // A reseed starts a new run; violations counted before it belong to the old one
    // (the state dump carries the count, so it must not depend on what ran earlier).
    g_violations = 0;
}

void setCosmeticRandomSeed(uint32_t seed) {
    cosmeticGenerator().seed(seed);
}

int randomInt(int min, int max) {
    checkGameplayDraw();
    return gameplayGenerator().uniformInt(min, max);
}

float randomFloat(float min, float max) {
    checkGameplayDraw();
    return gameplayGenerator().uniformFloat(min, max);
}

int cosmeticRandomInt(int min, int max) {
    return cosmeticGenerator().uniformInt(min, max);
}

float cosmeticRandomFloat(float min, float max) {
    return cosmeticGenerator().uniformFloat(min, max);
}

GameplayRandomForbidden::GameplayRandomForbidden() {
    ++g_forbiddenDepth;
}

GameplayRandomForbidden::~GameplayRandomForbidden() {
    --g_forbiddenDepth;
}

uint64_t gameplayRandomViolations() {
    return g_violations;
}

} // namespace reone
