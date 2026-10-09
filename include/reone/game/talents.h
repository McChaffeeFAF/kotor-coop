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

#include <functional>
#include <limits>
#include <memory>
#include <optional>

#include "types.h"

namespace reone {

namespace game {

class Creature;
class IFeats;
class ISpells;
class Item;

/**
 * Talent categories are four hex digits (spells.2da / feat.2da "category").
 * A query matches digit by digit: a query digit of F matches anything, any
 * other digit must be equal. See design note routines_batch1.
 */
bool talentCategoryMatches(uint32_t category, uint32_t query);

enum class TalentSearchMode {
    Best,  // highest maxcr within the pool, ties broken at random
    Random // uniform among every match in the pool
};

struct TalentQuery {
    uint32_t category {0};
    TalentSearchMode mode {TalentSearchMode::Random};
    int crMax {std::numeric_limits<int>::max()};
    uint32_t inclusion {0}; // skip talents whose exclusion bits intersect this; 0 skips nothing
    int excludeType {-1};   // TALENT_TYPE_FORCE (0) or TALENT_TYPE_FEAT (1); -1 excludes nothing
    int excludeId {-1};     // id to skip within excludeType; -1 skips the whole type
};

struct TalentChoice {
    TalentType type {TalentType::Invalid};
    int id {-1};
    std::shared_ptr<Item> item; // set when an item grants the spell
};

/** Returns a random integer in [min, max]. */
using TalentRandom = std::function<int(int, int)>;

/**
 * Picks a talent of creature matching query. The spell pool (known powers,
 * then item-granted spells) and the feat pool are searched in random order;
 * the first pool yielding a match wins.
 */
std::optional<TalentChoice> findCreatureTalent(
    const Creature &creature,
    const ISpells &spells,
    const IFeats &feats,
    const TalentQuery &query,
    const TalentRandom &random);

/** The first item carried by creature (equipped first) that casts spell. */
std::shared_ptr<Item> findItemGrantingSpell(const Creature &creature, SpellType spell);

bool creatureHasTalent(const Creature &creature, TalentType type, int id);

/** 2DA category of a talent; 0 for skills, invalid talents and missing rows. */
uint32_t getTalentCategory(const ISpells &spells, const IFeats &feats, TalentType type, int id);

} // namespace game

} // namespace reone
