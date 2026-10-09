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

#include "reone/game/talents.h"

#include "reone/game/d20/feats.h"
#include "reone/game/d20/spells.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"

namespace reone {

namespace game {

namespace {

constexpr int kTalentTypeForce = 0;
constexpr int kTalentTypeFeat = 1;

struct Candidate {
    TalentChoice choice;
    int maxCR {0};
};

/** Collects the matching candidates of one pool and reduces them per mode. */
class PoolCollector {
public:
    PoolCollector(const TalentQuery &query) :
        _query(query) {
    }

    void offer(TalentChoice choice, uint32_t category, int maxCR, uint32_t exclusion) {
        // A talent with no category (weapon proficiencies, passive feats) is
        // never an AI talent, even for an all-zero query.
        if (category == 0 || !talentCategoryMatches(category, _query.category)) {
            return;
        }
        if (_query.inclusion != 0 && (exclusion & _query.inclusion) != 0) {
            return;
        }
        if (_query.mode == TalentSearchMode::Best) {
            if (maxCR > _query.crMax) {
                return;
            }
            if (!_candidates.empty() && maxCR < _bestCR) {
                return;
            }
            if (_candidates.empty() || maxCR > _bestCR) {
                _candidates.clear();
                _bestCR = maxCR;
            }
        }
        _candidates.push_back(Candidate {std::move(choice), maxCR});
    }

    std::optional<TalentChoice> pick(const TalentRandom &random) const {
        if (_candidates.empty()) {
            return std::nullopt;
        }
        int index = _candidates.size() == 1
                        ? 0
                        : random(0, static_cast<int>(_candidates.size()) - 1);
        index = std::clamp(index, 0, static_cast<int>(_candidates.size()) - 1);
        return _candidates[index].choice;
    }

    bool empty() const { return _candidates.empty(); }

private:
    const TalentQuery &_query;
    std::vector<Candidate> _candidates;
    int _bestCR {0};
};

bool isExcluded(const TalentQuery &query, int type, int id) {
    return query.excludeType == type && query.excludeId == id;
}

bool isPoolExcluded(const TalentQuery &query, int type) {
    return query.excludeType == type && query.excludeId == -1;
}

template <class Fn>
void forEachCarriedItem(const Creature &creature, Fn fn) {
    for (const auto &[slot, item] : creature.equipment()) {
        if (item && !fn(item)) {
            return;
        }
    }
    for (const auto &item : creature.items()) {
        if (item && !fn(item)) {
            return;
        }
    }
}

std::optional<TalentChoice> searchSpellPool(
    const Creature &creature,
    const ISpells &spells,
    const TalentQuery &query,
    const TalentRandom &random) {

    // Known powers first; item-granted spells only when no power matches.
    PoolCollector known(query);
    for (SpellType type : creature.attributes().spells()) {
        int id = static_cast<int>(type);
        if (isExcluded(query, kTalentTypeForce, id)) {
            continue;
        }
        auto spell = spells.get(type);
        if (!spell) {
            continue;
        }
        // Only powers the creature can pay for (design note c03_stats).
        if (!creature.canAffordForcePower(*spell)) {
            continue;
        }
        known.offer(TalentChoice {TalentType::Spell, id, nullptr}, spell->category, spell->maxCR, spell->exclusion);
    }
    if (auto choice = known.pick(random)) {
        return choice;
    }

    PoolCollector items(query);
    forEachCarriedItem(creature, [&](const std::shared_ptr<Item> &item) {
        auto type = item->activateSpell();
        if (!type) {
            return true;
        }
        int id = static_cast<int>(*type);
        if (isExcluded(query, kTalentTypeForce, id)) {
            return true;
        }
        auto spell = spells.get(*type);
        if (spell) {
            items.offer(TalentChoice {TalentType::Spell, id, item}, spell->category, spell->maxCR, spell->exclusion);
        }
        return true;
    });
    return items.pick(random);
}

std::optional<TalentChoice> searchFeatPool(
    const Creature &creature,
    const IFeats &feats,
    const TalentQuery &query,
    const TalentRandom &random) {

    PoolCollector pool(query);
    for (FeatType type : creature.attributes().feats()) {
        int id = static_cast<int>(type);
        if (isExcluded(query, kTalentTypeFeat, id)) {
            continue;
        }
        auto feat = feats.get(type);
        if (!feat) {
            continue;
        }
        pool.offer(TalentChoice {TalentType::Feat, id, nullptr}, feat->category, feat->maxCR, feat->exclusion);
    }
    return pool.pick(random);
}

} // namespace

bool talentCategoryMatches(uint32_t category, uint32_t query) {
    for (int shift = 0; shift < 16; shift += 4) {
        uint32_t wanted = (query >> shift) & 0xf;
        if (wanted != 0xf && wanted != ((category >> shift) & 0xf)) {
            return false;
        }
    }
    return true;
}

std::optional<TalentChoice> findCreatureTalent(
    const Creature &creature,
    const ISpells &spells,
    const IFeats &feats,
    const TalentQuery &query,
    const TalentRandom &random) {

    bool spellsFirst = random(0, 1) == 0;
    for (int pass = 0; pass < 2; ++pass) {
        bool spellPool = (pass == 0) == spellsFirst;
        std::optional<TalentChoice> choice;
        if (spellPool) {
            if (!isPoolExcluded(query, kTalentTypeForce)) {
                choice = searchSpellPool(creature, spells, query, random);
            }
        } else if (!isPoolExcluded(query, kTalentTypeFeat)) {
            choice = searchFeatPool(creature, feats, query, random);
        }
        if (choice) {
            return choice;
        }
    }
    return std::nullopt;
}

std::shared_ptr<Item> findItemGrantingSpell(const Creature &creature, SpellType spell) {
    std::shared_ptr<Item> result;
    forEachCarriedItem(creature, [&](const std::shared_ptr<Item> &item) {
        if (item->activateSpell() == spell) {
            result = item;
            return false;
        }
        return true;
    });
    return result;
}

bool creatureHasTalent(const Creature &creature, TalentType type, int id) {
    if (id < 0) {
        return false;
    }
    switch (type) {
    case TalentType::Spell:
        return creature.attributes().hasSpell(static_cast<SpellType>(id)) ||
               findItemGrantingSpell(creature, static_cast<SpellType>(id)) != nullptr;
    case TalentType::Feat:
        return creature.attributes().hasFeat(static_cast<FeatType>(id));
    case TalentType::Skill:
        return creature.attributes().hasSkill(static_cast<SkillType>(id));
    default:
        return false;
    }
}

uint32_t getTalentCategory(const ISpells &spells, const IFeats &feats, TalentType type, int id) {
    if (id < 0) {
        return 0;
    }
    switch (type) {
    case TalentType::Spell: {
        auto spell = spells.get(static_cast<SpellType>(id));
        return spell ? spell->category : 0;
    }
    case TalentType::Feat: {
        auto feat = feats.get(static_cast<FeatType>(id));
        return feat ? feat->category : 0;
    }
    default:
        return 0;
    }
}

} // namespace game

} // namespace reone
