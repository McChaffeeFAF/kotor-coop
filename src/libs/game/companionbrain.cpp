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

// The pure decision of the "Smarter companions" brain (C13).
// Behaviour notes: design note c13_companions.

#include "reone/game/companionbrain.h"

#include <cctype>

#include <glm/geometric.hpp>

namespace reone {

namespace game {

namespace companion {

namespace {

constexpr uint32_t kExcludesDroids = 0x02;
constexpr uint32_t kExcludesOrganics = 0x01;
constexpr uint32_t kCuresPoison = 0x40; // category bit of Heal, Cure and the anti-toxin

float planarDistance(const glm::vec3 &a, const glm::vec3 &b) {
    return glm::length(glm::vec2(a.x - b.x, a.y - b.y));
}

/** Distance in the ground plane from p to the segment a-b. */
float segmentDistance(const glm::vec3 &p, const glm::vec3 &a, const glm::vec3 &b) {
    glm::vec2 ab(b.x - a.x, b.y - a.y);
    glm::vec2 ap(p.x - a.x, p.y - a.y);
    float lengthSq = glm::dot(ab, ab);
    float t = lengthSq > 0.0f ? std::clamp(glm::dot(ap, ab) / lengthSq, 0.0f, 1.0f) : 0.0f;
    return glm::length(ap - ab * t);
}

glm::vec3 awayFrom(const glm::vec3 &from, const glm::vec3 &origin, float distance) {
    glm::vec2 dir(from.x - origin.x, from.y - origin.y);
    float length = glm::length(dir);
    dir = length > 1e-4f ? dir / length : glm::vec2(1.0f, 0.0f);
    return glm::vec3(from.x + dir.x * distance, from.y + dir.y * distance, from.z);
}

std::string upper(const std::string &s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return result;
}

bool has(const std::string &s, const char *part) {
    return s.find(part) != std::string::npos;
}

bool startsWith(const std::string &s, const char *prefix) {
    return s.rfind(prefix, 0) == 0;
}

/** The talent may be used on this target (spells.2da exclusion bits). */
bool usableOn(const Talent &talent, const Actor &target) {
    if (target.droid && (talent.exclusion & kExcludesDroids)) {
        return false;
    }
    if (!target.droid && (talent.exclusion & kExcludesOrganics)) {
        return false;
    }
    return true;
}

/**
 * The user may use this item. Droids use no medpacs, stims, anti-toxins,
 * grenades or organic shields (those rows exclude droids); a droid's own
 * utilities and shields are for droids. Anyone may use a repair kit.
 */
bool userMay(const Talent &talent, const Actor &user) {
    if (talent.item == 0) {
        return true;
    }
    if (talent.droidItem) {
        return user.droid;
    }
    switch (talent.itemKind) {
    case ItemKind::RepairKit:
        return true;
    case ItemKind::Shield:
        return !user.droid;
    case ItemKind::Medpac:
    case ItemKind::PartyHeal:
    case ItemKind::Antidote:
    case ItemKind::Stim:
    case ItemKind::Grenade:
        return !(user.droid && (talent.exclusion & kExcludesDroids));
    case ItemKind::Cloak:
    case ItemKind::Plot:
    case ItemKind::Launcher:
        return false;
    default:
        return true;
    }
}

bool inHazard(const CompanionView &view, const glm::vec3 &point) {
    for (const Hazard &hazard : view.hazards) {
        if (planarDistance(point, hazard.position) < hazard.radius) {
            return true;
        }
    }
    return false;
}

bool pathCrossesHazard(const CompanionView &view, const glm::vec3 &from, const glm::vec3 &to) {
    for (const Hazard &hazard : view.hazards) {
        if (segmentDistance(hazard.position, from, to) < hazard.radius) {
            return true;
        }
    }
    return false;
}

const Actor *findEnemy(const CompanionView &view, uint32_t id) {
    for (const Actor &enemy : view.enemies) {
        if (enemy.id == id) {
            return &enemy;
        }
    }
    return nullptr;
}

/** Talents of a role, strongest first (rank, then item value); then by id. */
std::vector<const Talent *> talentsOf(const CompanionView &view, TalentRole role) {
    std::vector<const Talent *> result;
    for (const Talent &talent : view.talents) {
        if (talent.role == role) {
            result.push_back(&talent);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Talent *a, const Talent *b) {
        if (a->rank != b->rank) {
            return a->rank > b->rank;
        }
        if (a->value != b->value) {
            return a->value > b->value;
        }
        if (a->id != b->id) {
            return a->id < b->id;
        }
        return a->item < b->item;
    });
    return result;
}

/** False for the last of an item another party member is about to use this round. */
bool unclaimed(const CompanionView &view, const Talent &talent) {
    return talent.item == 0 || talent.count > 1 ||
           !std::binary_search(view.claimedItems.begin(), view.claimedItems.end(), talent.item);
}

bool isTended(const CompanionView &view, const Actor &patient) {
    return std::binary_search(view.tended.begin(), view.tended.end(), patient.id);
}

bool affordable(const CompanionView &view, const Talent &talent, int reserve) {
    return talent.forceCost <= 0 || view.forcePoints >= talent.forceCost + reserve;
}

/** An enemy worth a debuff or a disabling power: a boss, two levels up, or more health than self. */
bool strong(const CompanionView &view, const Actor &enemy) {
    return enemy.boss || enemy.level >= view.self.level + 2 || enemy.maxHitPoints > view.self.maxHitPoints;
}

Decision useOn(const Talent &talent, uint32_t target, std::string reason) {
    Decision decision;
    decision.kind = DecisionKind::TalentOnObject;
    decision.talent = talent;
    decision.target = target;
    decision.reason = std::move(reason);
    return decision;
}

/** Live party members that pass the filter, most hurt first; ties go to the lower id. */
template <class Pred>
std::vector<const Actor *> partyWhere(const CompanionView &view, Pred pred) {
    std::vector<const Actor *> result;
    if (view.self.hitPoints > 0 && pred(view.self)) {
        result.push_back(&view.self);
    }
    for (const Actor &ally : view.allies) {
        if (ally.hitPoints > 0 && pred(ally)) {
            result.push_back(&ally);
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const Actor *a, const Actor *b) {
        if (a->healthRatio() != b->healthRatio()) {
            return a->healthRatio() < b->healthRatio();
        }
        return a->id < b->id;
    });
    return result;
}

/** Medpacs and kits for the most hurt party member in reach, matched to it. */
std::optional<Decision> chooseItemHeal(const CompanionView &view, const std::vector<const Talent *> &heals, bool badly) {
    auto hurt = partyWhere(view, [](const Actor &a) { return a.healthRatio() < kItemHealThreshold; });
    // A recovery stim heals the whole party: for two or more badly hurt in a losing fight.
    if (hurt.size() >= 2 && badly) {
        for (const Talent *heal : heals) {
            if (heal->itemKind == ItemKind::PartyHeal && userMay(*heal, view.self) && unclaimed(view, *heal)) {
                return useOn(*heal, view.self.id, "party-stim");
            }
        }
    }
    for (const Actor *patient : hurt) {
        bool isSelf = patient->id == view.self.id;
        if ((!isSelf && planarDistance(view.self.position, patient->position) > kAllyItemRange) ||
            isTended(view, *patient)) {
            continue; // out of reach, or another party member is already on it
        }
        std::vector<const Talent *> kits;
        for (const Talent *heal : heals) {
            if (heal->item == 0 || heal->itemKind == ItemKind::PartyHeal || !usableOn(*heal, *patient) ||
                !userMay(*heal, view.self) || !unclaimed(view, *heal)) {
                continue;
            }
            if (heal->rare && !badly) {
                continue; // the best kits are kept for a fight going badly
            }
            kits.push_back(heal);
        }
        if (kits.empty()) {
            continue;
        }
        // heals are strongest first: the weakest kit covers moderate damage,
        // the strongest is for the critically hurt.
        bool critical = patient->healthRatio() < kCriticalHealth || badly;
        const Talent *kit = critical ? kits.front() : kits.back();
        std::string reason = kit->itemKind == ItemKind::RepairKit ? "repair-kit" : "medpac";
        return useOn(*kit, patient->id, isSelf ? reason : "ally-" + reason);
    }
    return std::nullopt;
}

std::optional<Decision> chooseHeal(const CompanionView &view, bool badly) {
    auto hurt = partyWhere(view, [](const Actor &a) { return a.healthRatio() < kHealThreshold; });
    if (hurt.empty()) {
        return std::nullopt;
    }
    auto heals = talentsOf(view, TalentRole::Heal);
    // Force heals first, for the most hurt party member.
    const Actor *patient = hurt.front();
    bool isSelf = patient->id == view.self.id;
    for (const Talent *heal : heals) {
        if (heal->item != 0 || !affordable(view, *heal, 0) || !usableOn(*heal, *patient)) {
            continue;
        }
        if (heal->onCaster) {
            if (isSelf || planarDistance(view.self.position, patient->position) <= heal->radius) {
                return useOn(*heal, view.self.id, "heal-party");
            }
        } else if (heal->selfOnly) {
            if (isSelf) {
                return useOn(*heal, view.self.id, "heal-self");
            }
        } else {
            return useOn(*heal, patient->id, "heal-ally");
        }
    }
    return chooseItemHeal(view, heals, badly);
}

/** Poison: a Force heal that cures it (Heal), else an anti-toxin. */
std::optional<Decision> chooseCure(const CompanionView &view) {
    auto poisoned = partyWhere(view, [](const Actor &a) { return a.poisoned; });
    if (poisoned.empty()) {
        return std::nullopt;
    }
    auto heals = talentsOf(view, TalentRole::Heal);
    auto cures = talentsOf(view, TalentRole::Cure);
    for (const Actor *patient : poisoned) {
        bool isSelf = patient->id == view.self.id;
        float distance = planarDistance(view.self.position, patient->position);
        for (const Talent *heal : heals) {
            if (heal->item != 0 || !(heal->effects & kEffectCure) || !affordable(view, *heal, 0) ||
                !usableOn(*heal, *patient)) {
                continue;
            }
            if (heal->onCaster && (isSelf || distance <= heal->radius)) {
                return useOn(*heal, view.self.id, "cure-party");
            }
            if (!heal->onCaster && !heal->selfOnly) {
                return useOn(*heal, patient->id, "cure-ally");
            }
            if (heal->selfOnly && isSelf) {
                return useOn(*heal, view.self.id, "cure-self");
            }
        }
        if ((!isSelf && distance > kAllyItemRange) || isTended(view, *patient)) {
            continue;
        }
        for (const Talent *cure : cures) {
            if (cure->item != 0 && usableOn(*cure, *patient) && userMay(*cure, view.self) && unclaimed(view, *cure)) {
                return useOn(*cure, patient->id, isSelf ? "antidote" : "ally-antidote");
            }
        }
    }
    return std::nullopt;
}

bool hasStimRunning(const Actor &actor) {
    return std::any_of(actor.activeFamilies.begin(), actor.activeFamilies.end(),
                       [](const std::string &family) { return startsWith(family, "stim:"); });
}

/** Energy shields when shot at or at a tough fight; stims at a tough fight. */
std::optional<Decision> chooseConsumable(const CompanionView &view, bool tough, bool badly) {
    bool shotAt = std::any_of(view.enemies.begin(), view.enemies.end(), [&view](const Actor &enemy) {
        return enemy.ranged && enemy.target == view.self.id;
    });
    auto buffs = talentsOf(view, TalentRole::Buff);
    if (!view.self.shielded && !view.self.hasFamily("shield") && (shotAt || tough)) {
        for (const Talent *shield : buffs) {
            if (shield->itemKind != ItemKind::Shield || !userMay(*shield, view.self) || !unclaimed(view, *shield) ||
                view.self.hasSpellEffect(shield->id) || (shield->rare && !badly)) {
                continue;
            }
            return useOn(*shield, view.self.id, "shield");
        }
    }
    // One stim at a time unless the fight is going badly; never the same type twice.
    if (tough && (badly || !hasStimRunning(view.self))) {
        for (const Talent *stim : buffs) {
            if (stim->itemKind != ItemKind::Stim || !userMay(*stim, view.self) || !unclaimed(view, *stim) ||
                view.self.hasSpellEffect(stim->id) || view.self.hasFamily(stim->family) ||
                (stim->rare && !badly)) {
                continue;
            }
            return useOn(*stim, view.self.id, "stim");
        }
    }
    return std::nullopt;
}

/** Force buffs, kept up through a fight: the strongest of each line, never one already running. */
std::optional<Decision> chooseBuff(const CompanionView &view, int reserve) {
    auto buffs = talentsOf(view, TalentRole::Buff);
    // At equal rank, a party-wide buff beats a personal one when allies are near.
    std::stable_sort(buffs.begin(), buffs.end(), [&view](const Talent *a, const Talent *b) {
        if (a->rank != b->rank) {
            return a->rank > b->rank;
        }
        return !view.allies.empty() && a->onCaster && !b->onCaster;
    });
    std::vector<std::string> covered;
    auto isCovered = [&covered](const std::string &family) {
        return !family.empty() && std::find(covered.begin(), covered.end(), family) != covered.end();
    };
    for (const Talent *buff : buffs) {
        if (buff->item != 0 || (!buff->selfOnly && !buff->onCaster)) {
            continue; // items are consumables; single-target buffs on others are left to the player
        }
        if (view.self.hasSpellEffect(buff->id) || view.self.hasFamily(buff->family)) {
            covered.push_back(buff->family); // its line is running: no weaker recast
            continue;
        }
        if (isCovered(buff->family) || !affordable(view, *buff, reserve) || !usableOn(*buff, view.self)) {
            continue;
        }
        return useOn(*buff, view.self.id, "buff");
    }
    return std::nullopt;
}

/** The best grenade throw: the most value in one blast that spares the party. */
std::optional<Decision> chooseGrenade(const CompanionView &view, bool badly) {
    bool meleeAllies = std::any_of(view.allies.begin(), view.allies.end(), [](const Actor &a) { return !a.ranged; });
    const Talent *bestGrenade = nullptr;
    const Actor *bestCentre = nullptr;
    float bestValue = 0.0f;
    for (const Talent *grenade : talentsOf(view, TalentRole::Grenade)) {
        if (!userMay(*grenade, view.self) || !unclaimed(view, *grenade) || (grenade->rare && !badly)) {
            continue;
        }
        float radius = grenade->radius > 0.0f ? grenade->radius : kGrenadeRadius;
        for (const Actor &centre : view.enemies) {
            float fromSelf = planarDistance(view.self.position, centre.position);
            if (planarDistance(view.leaderPosition, centre.position) > kChaseLimit && fromSelf > view.attackRange) {
                continue;
            }
            if (view.hold && fromSelf > std::max(view.attackRange, kMeleeReach)) {
                continue;
            }
            if (catchesAllies(view, centre.position, radius, false)) {
                continue;
            }
            float value = 0.0f;
            for (const Actor &enemy : view.enemies) {
                if (planarDistance(centre.position, enemy.position) <= radius) {
                    value += grenadeValue(*grenade, enemy);
                }
            }
            // A stun or adhesive grenade sets the enemy up for the melee allies.
            if (meleeAllies && (grenade->grenade == GrenadeKind::Stun || grenade->grenade == GrenadeKind::Adhesive)) {
                value *= 1.2f;
            }
            if (value >= kMinGrenadeValue && value > bestValue) {
                bestGrenade = grenade;
                bestCentre = &centre;
                bestValue = value;
            }
        }
    }
    if (!bestGrenade) {
        return std::nullopt;
    }
    return useOn(*bestGrenade, bestCentre->id, "grenade");
}

/** An enemy a hostile talent would work on. */
bool affects(const Talent &talent, const Actor &enemy) {
    if (!usableOn(talent, enemy)) {
        return false;
    }
    if (talent.item == 0 && enemy.forceResistant && !(talent.effects & kEffectDispel)) {
        return false;
    }
    if ((talent.effects & kEffectDisable) && !(talent.effects & kEffectDamage) && (enemy.disabled || enemy.immuneStun)) {
        return false;
    }
    return true;
}

std::optional<Decision> chooseAreaPower(const CompanionView &view, const Actor &target, int reserve) {
    for (const Talent *area : talentsOf(view, TalentRole::AttackArea)) {
        if (!affordable(view, *area, reserve) || !userMay(*area, view.self) || !unclaimed(view, *area)) {
            continue;
        }
        float radius = area->radius > 0.0f ? area->radius : kAreaPowerRadius;
        glm::vec3 centre = area->onCaster ? view.self.position : target.position;
        int count = 0;
        for (const Actor &enemy : view.enemies) {
            if (planarDistance(centre, enemy.position) <= radius && affects(*area, enemy)) {
                ++count;
            }
        }
        if (count < 2) {
            continue;
        }
        if (catchesAllies(view, centre, radius, area->onCaster)) {
            continue;
        }
        // Where a power's shape is unknown, it must also spare the allies
        // around the caster.
        if (!area->onCaster && catchesAllies(view, view.self.position, radius, true)) {
            continue;
        }
        return useOn(*area, target.id, area->item != 0 ? "droid-utility" : "area-power");
    }
    return std::nullopt;
}

/** Single-target powers and droid utilities, scored for this target; used on half the rounds. */
std::optional<Decision> chooseSingle(const CompanionView &view, const Actor &target, int reserve, const Random &random) {
    if (target.healthRatio() < kWeakEnemyThreshold) {
        return std::nullopt; // finish it with the weapon
    }
    bool isStrong = strong(view, target);
    bool exposed = target.shielded || target.forceResistant;
    int budget = view.forcePoints - reserve;
    std::vector<std::pair<int, const Talent *>> scored;
    for (const Talent *power : talentsOf(view, TalentRole::AttackSingle)) {
        if (!affordable(view, *power, reserve) || !userMay(*power, view.self) || !unclaimed(view, *power) ||
            target.hasSpellEffect(power->id) || !affects(*power, target)) {
            continue;
        }
        bool damage = power->effects & kEffectDamage;
        bool disable = (power->effects & kEffectDisable) && !target.disabled && !target.immuneStun;
        bool debuff = (power->effects & kEffectDebuff) && isStrong;
        bool dispel = (power->effects & kEffectDispel) && exposed;
        if (power->effects != 0 && !damage && !disable && !debuff && !dispel) {
            continue; // nothing it would add here: slow and affliction are for strong enemies
        }
        int score = power->rank;
        if (disable) {
            score += isStrong ? 6 : (view.enemies.size() >= 2 ? 3 : 0);
        }
        if (debuff) {
            score += 4;
        }
        if (dispel) {
            score += 8;
        }
        if (power->forceCost > 0 && power->forceCost * 2 > budget) {
            score -= 2; // it would eat most of the spare Force
        }
        scored.emplace_back(score, power);
    }
    if (scored.empty() || random(0, 1) != 0) {
        return std::nullopt;
    }
    std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    size_t best = 0;
    while (best + 1 < scored.size() && scored[best + 1].first == scored[0].first) {
        ++best;
    }
    int index = best == 0 ? 0 : std::clamp(random(0, static_cast<int>(best)), 0, static_cast<int>(best));
    const Talent *chosen = scored[index].second;
    return useOn(*chosen, target.id, chosen->item != 0 ? "droid-utility" : "power");
}

/** A point beside the line of fire from shooter to target, on self's side. */
glm::vec3 sideStep(const glm::vec3 &self, const glm::vec3 &shooter, const glm::vec3 &target) {
    glm::vec2 dir(target.x - shooter.x, target.y - shooter.y);
    float length = glm::length(dir);
    dir = length > 1e-4f ? dir / length : glm::vec2(1.0f, 0.0f);
    glm::vec2 perp(-dir.y, dir.x);
    float side = glm::dot(glm::vec2(self.x - shooter.x, self.y - shooter.y), perp) >= 0.0f ? 1.0f : -1.0f;
    return glm::vec3(self.x + perp.x * side * kSideStep, self.y + perp.y * side * kSideStep, self.z);
}

std::optional<glm::vec3> reposition(const CompanionView &view, const Actor &target) {
    if (view.hold || !view.mayReposition) {
        return std::nullopt;
    }
    if (view.self.ranged) {
        // Ranged: step back from an enemy in melee reach, when a melee ally can take it.
        bool meleeAlly = std::any_of(view.allies.begin(), view.allies.end(), [](const Actor &a) { return !a.ranged; });
        if (!meleeAlly) {
            return std::nullopt;
        }
        const Actor *closest = nullptr;
        float closestDistance = kMeleeReach;
        for (const Actor &enemy : view.enemies) {
            float distance = planarDistance(view.self.position, enemy.position);
            if (distance < closestDistance) {
                closest = &enemy;
                closestDistance = distance;
            }
        }
        if (!closest) {
            return std::nullopt;
        }
        float distance = std::min(kBackOffDistance, std::max(0.0f, view.preferredDistance - closestDistance));
        if (distance <= 0.5f) {
            return std::nullopt;
        }
        glm::vec3 point = awayFrom(view.self.position, closest->position, distance);
        if (inHazard(view, point)) {
            return std::nullopt;
        }
        return point;
    }
    // Melee: do not stand in a ranged ally's line of fire.
    for (const Actor &ally : view.allies) {
        if (!ally.ranged || ally.target == 0) {
            continue;
        }
        const Actor *allyTarget = findEnemy(view, ally.target);
        if (!allyTarget) {
            continue;
        }
        if (planarDistance(view.self.position, allyTarget->position) <= kMeleeReach ||
            planarDistance(view.self.position, ally.position) <= 1.0f) {
            continue; // already fighting at the target, or beside the shooter
        }
        if (segmentDistance(view.self.position, ally.position, allyTarget->position) < kLineOfFireWidth) {
            glm::vec3 point = sideStep(view.self.position, ally.position, allyTarget->position);
            if (!inHazard(view, point)) {
                return point;
            }
        }
    }
    return std::nullopt;
}

GrenadeKind grenadeKindOf(const std::string &label) {
    if (has(label, "FRAG")) {
        return GrenadeKind::Frag;
    }
    if (has(label, "STUN")) {
        return GrenadeKind::Stun;
    }
    if (has(label, "THERMAL")) {
        return GrenadeKind::Thermal;
    }
    if (has(label, "POISON")) {
        return GrenadeKind::Poison;
    }
    if (has(label, "SONIC")) {
        return GrenadeKind::Sonic;
    }
    if (has(label, "ADHESIVE")) {
        return GrenadeKind::Adhesive;
    }
    if (has(label, "CRYO") || has(label, "COLD")) {
        return GrenadeKind::Cryoban;
    }
    if (has(label, "FIRE") || has(label, "PLASMA")) {
        return GrenadeKind::Plasma;
    }
    if (has(label, "ION")) {
        return GrenadeKind::Ion;
    }
    return GrenadeKind::Other;
}

uint32_t effectsOf(uint32_t category) {
    uint32_t kind = (category >> 8) & 0xf;
    uint32_t sub = (category >> 4) & 0xf;
    uint32_t effects = 0;
    if (kind & 1) {
        effects |= kEffectDamage;
    }
    if (kind & 2) {
        effects |= kEffectDisable;
    }
    if (sub & 1) {
        effects |= kEffectDebuff;
    }
    if (sub & 2) {
        effects |= kEffectDispel;
    }
    return effects;
}

} // namespace

std::optional<TalentRole> classifySpell(uint32_t category, bool hostile, bool fromItem) {
    uint32_t scope = (category >> 12) & 0xf;
    uint32_t kind = (category >> 8) & 0xf;
    if (hostile) {
        if (category == 0) {
            // Grenades carry no category; hostile items without one are thrown.
            if (fromItem) {
                return TalentRole::Grenade;
            }
            return std::nullopt;
        }
        if (scope == 8 && fromItem) {
            return std::nullopt; // droid utilities: told apart by label (describeSpell)
        }
        return scope == 2 ? TalentRole::AttackArea : TalentRole::AttackSingle;
    }
    if (category == 0) {
        return std::nullopt;
    }
    if (kind & 4) {
        return TalentRole::Heal;
    }
    if ((kind & 8) || scope == 2) {
        return TalentRole::Buff;
    }
    return std::nullopt;
}

bool isItemLabel(const std::string &label) {
    std::string name = upper(label);
    return startsWith(name, "ITEM_ABILITY_") || startsWith(name, "DROID_ITEM_") || startsWith(name, "PLOT_");
}

SpellTraits describeSpell(uint32_t category, bool hostile, bool fromItem, const std::string &label,
                          uint32_t itemTargeting, int rank) {
    SpellTraits traits;
    std::string name = upper(label);
    uint32_t kind = (category >> 8) & 0xf;
    if (!fromItem) {
        traits.role = classifySpell(category, hostile, false);
        if (hostile) {
            traits.effects = effectsOf(category);
        } else if (traits.role == TalentRole::Heal && (category & kCuresPoison)) {
            traits.effects = kEffectCure;
        }
        return traits;
    }
    if (startsWith(name, "PLOT_")) {
        traits.itemKind = ItemKind::Plot;
        return traits;
    }
    if (itemTargeting == 3) {
        traits.itemKind = ItemKind::Launcher;
        return traits;
    }
    if (has(name, "CLOAK")) {
        traits.itemKind = ItemKind::Cloak;
        return traits;
    }
    traits.droidItem = startsWith(name, "DROID_ITEM_");
    if (hostile) {
        if (has(name, "GRENADE") || (category == 0 && !traits.droidItem)) {
            traits.role = TalentRole::Grenade;
            traits.itemKind = ItemKind::Grenade;
            traits.grenade = grenadeKindOf(name);
            traits.radius = kGrenadeRadius;
            switch (traits.grenade) {
            case GrenadeKind::Stun:
            case GrenadeKind::Adhesive:
                traits.effects = kEffectDisable;
                break;
            case GrenadeKind::Cryoban:
                traits.effects = kEffectDamage | kEffectDisable;
                break;
            default:
                traits.effects = kEffectDamage;
                break;
            }
            traits.rare = traits.grenade == GrenadeKind::Thermal || traits.grenade == GrenadeKind::Cryoban ||
                          traits.grenade == GrenadeKind::Plasma;
            return traits;
        }
        if (traits.droidItem) {
            traits.itemKind = ItemKind::DroidUtility;
            traits.role = TalentRole::AttackSingle;
            if (has(name, "STUN")) {
                traits.effects = kEffectDisable;
            } else if (has(name, "CARBONITE")) {
                traits.effects = kEffectDamage | kEffectDisable;
            } else if (has(name, "DISRUPT")) {
                traits.effects = kEffectDamage | kEffectDispel;
            } else if (has(name, "GRAVITY")) {
                traits.role = TalentRole::AttackArea;
                traits.effects = kEffectDisable;
            } else {
                traits.effects = kEffectDamage; // flamethrower and the like
            }
            return traits;
        }
        traits.role = classifySpell(category, true, true);
        traits.effects = effectsOf(category);
        return traits;
    }
    if (has(name, "SHIELD")) {
        traits.itemKind = ItemKind::Shield;
        traits.role = TalentRole::Buff;
        return traits;
    }
    if (kind & 4) {
        traits.role = TalentRole::Heal;
        if (has(name, "RECOVERY")) {
            traits.itemKind = ItemKind::PartyHeal;
            traits.rare = true;
        } else if (has(name, "REPAIR") || has(name, "CONSTRUCTION") || has(name, "DROID")) {
            traits.itemKind = ItemKind::RepairKit;
        } else {
            traits.itemKind = ItemKind::Medpac;
            traits.rare = rank >= 9;
        }
        return traits;
    }
    if (category & kCuresPoison) {
        traits.role = TalentRole::Cure;
        traits.itemKind = ItemKind::Antidote;
        return traits;
    }
    if (kind & 8) {
        traits.role = TalentRole::Buff;
        traits.itemKind = ItemKind::Stim;
        traits.rare = rank >= 9;
        return traits;
    }
    traits.role = classifySpell(category, false, true);
    return traits;
}

std::string spellFamily(const SpellTraits &traits, const std::string &label, int rootId) {
    if (traits.itemKind == ItemKind::Shield) {
        return "shield";
    }
    if (traits.itemKind == ItemKind::Stim) {
        std::string stem = upper(label);
        for (const char *prefix : {"ITEM_ABILITY_", "DROID_ITEM_"}) {
            if (startsWith(stem, prefix)) {
                stem = stem.substr(std::string(prefix).size());
            }
        }
        if (startsWith(stem, "HYPER_")) {
            stem = stem.substr(6);
        }
        while (!stem.empty() && (std::isdigit(static_cast<unsigned char>(stem.back())) || stem.back() == '_')) {
            stem.pop_back();
        }
        return "stim:" + stem;
    }
    return "#" + std::to_string(rootId);
}

float grenadeValue(const Talent &grenade, const Actor &enemy) {
    float value;
    switch (grenade.grenade) {
    case GrenadeKind::Poison:
        value = !enemy.droid && !enemy.immunePoison && !enemy.poisoned ? 1.0f : 0.0f;
        break;
    case GrenadeKind::Stun:
        value = !enemy.disabled && !enemy.immuneStun ? 1.5f : 0.0f;
        break;
    case GrenadeKind::Adhesive:
        value = !enemy.disabled ? 1.25f : 0.0f;
        break;
    case GrenadeKind::Cryoban:
        value = !enemy.disabled ? 1.5f : 1.0f;
        break;
    case GrenadeKind::Ion:
        value = enemy.droid ? 2.5f : (enemy.shielded ? 1.5f : 0.25f);
        break;
    case GrenadeKind::Thermal:
        value = 1.5f;
        break;
    case GrenadeKind::Plasma:
        value = enemy.droid ? 1.0f : 1.25f;
        break;
    default:
        value = 1.0f;
        break;
    }
    if (enemy.healthRatio() < kWeakEnemyThreshold) {
        value *= 0.5f; // nearly dead: not worth a grenade
    }
    return value;
}

bool toughFight(const CompanionView &view) {
    if (view.enemies.size() >= 3) {
        return true;
    }
    int enemyHealth = 0;
    for (const Actor &enemy : view.enemies) {
        if (enemy.boss || enemy.level >= view.self.level + 3) {
            return true;
        }
        enemyHealth += enemy.hitPoints;
    }
    int partyHealth = view.self.hitPoints;
    for (const Actor &ally : view.allies) {
        partyHealth += ally.hitPoints;
    }
    return !view.enemies.empty() && enemyHealth > partyHealth;
}

bool goingBadly(const CompanionView &view) {
    if (view.partyDown > 0 || view.self.healthRatio() < kCriticalHealth) {
        return true;
    }
    float ratios = view.self.healthRatio();
    int partyHealth = view.self.hitPoints;
    for (const Actor &ally : view.allies) {
        if (ally.healthRatio() < kCriticalHealth) {
            return true;
        }
        ratios += ally.healthRatio();
        partyHealth += ally.hitPoints;
    }
    if (ratios / static_cast<float>(view.allies.size() + 1) < 0.5f) {
        return true;
    }
    int enemyHealth = 0;
    for (const Actor &enemy : view.enemies) {
        enemyHealth += enemy.hitPoints;
    }
    return enemyHealth > 2 * partyHealth;
}

int forceReserve(const CompanionView &view) {
    int reserve = 0;
    for (const Talent &talent : view.talents) {
        if (talent.role == TalentRole::Heal && talent.item == 0 && talent.forceCost > 0 &&
            (reserve == 0 || talent.forceCost < reserve)) {
            reserve = talent.forceCost;
        }
    }
    return reserve;
}

bool catchesAllies(const CompanionView &view, const glm::vec3 &point, float radius, bool excludeSelf) {
    float reach = radius + kFriendlyFireMargin;
    if (!excludeSelf && planarDistance(view.self.position, point) <= reach) {
        return true;
    }
    for (const Actor &ally : view.allies) {
        if (planarDistance(ally.position, point) <= reach) {
            return true;
        }
    }
    return false;
}

uint32_t chooseTarget(const CompanionView &view) {
    uint32_t best = 0;
    float bestScore = 0.0f;
    float holdReach = std::max(view.attackRange, kMeleeReach);
    for (const Actor &enemy : view.enemies) {
        float fromSelf = planarDistance(view.self.position, enemy.position);
        float fromLeader = planarDistance(view.leaderPosition, enemy.position);
        if (fromLeader > kChaseLimit && fromSelf > view.attackRange) {
            continue; // no chasing across the map
        }
        if (view.hold && fromSelf > holdReach) {
            continue;
        }
        if (!view.self.ranged && fromSelf > kMeleeReach &&
            pathCrossesHazard(view, view.self.position, enemy.position)) {
            continue; // would walk over a known mine
        }
        float score = -fromSelf;
        if (enemy.id == view.leaderTarget) {
            score += 30.0f;
        }
        if (enemy.healthRatio() < kWeakEnemyThreshold) {
            score += 25.0f;
        } else {
            score += (1.0f - enemy.healthRatio()) * 10.0f;
        }
        if (enemy.caster || enemy.healer) {
            score += 20.0f;
        }
        if (enemy.target == view.self.id) {
            score += 10.0f;
        }
        if (enemy.id == view.self.target) {
            score += 5.0f;
        }
        if (best == 0 || score > bestScore) {
            best = enemy.id;
            bestScore = score;
        }
    }
    return best;
}

Decision decide(const CompanionView &view, const Random &random) {
    Decision decision;
    if (view.stealthed) {
        decision.reason = "stealth";
        return decision;
    }
    std::optional<glm::vec3> escape;
    if (!view.hold) {
        for (const Hazard &hazard : view.hazards) {
            if (planarDistance(view.self.position, hazard.position) < hazard.radius) {
                escape = awayFrom(view.self.position, hazard.position,
                                  hazard.radius + 1.0f - planarDistance(view.self.position, hazard.position));
                break;
            }
        }
    }
    auto withEscape = [&escape](Decision result) {
        result.moveFirst = escape;
        return result;
    };
    bool badly = goingBadly(view);
    if (auto heal = chooseHeal(view, badly)) {
        return withEscape(*heal);
    }
    if (auto cure = chooseCure(view)) {
        return withEscape(*cure);
    }
    decision.moveFirst = escape;
    if (view.enemies.empty()) {
        decision.reason = escape ? "hazard" : "no-enemies";
        return decision;
    }
    if (view.passive) {
        decision.reason = "passive";
        return decision;
    }
    uint32_t targetId = chooseTarget(view);
    const Actor *target = findEnemy(view, targetId);
    if (!target) {
        decision.reason = "no-target";
        return decision;
    }
    int reserve = forceReserve(view);
    bool tough = toughFight(view);

    if (auto consumable = chooseConsumable(view, tough, badly)) {
        return withEscape(*consumable);
    }
    // Buffs: not at a fight's last enemy, unless that enemy makes it a tough fight.
    if (view.enemies.size() >= 2 || tough) {
        if (auto buff = chooseBuff(view, reserve)) {
            return withEscape(*buff);
        }
    }
    if (auto grenade = chooseGrenade(view, badly)) {
        return withEscape(*grenade);
    }
    if (auto area = chooseAreaPower(view, *target, reserve)) {
        return withEscape(*area);
    }
    if (auto single = chooseSingle(view, *target, reserve, random)) {
        return withEscape(*single);
    }

    std::optional<glm::vec3> move = escape ? escape : reposition(view, *target);
    for (const Talent *feat : talentsOf(view, TalentRole::AttackFeat)) {
        if (feat->rangedFeat == view.self.ranged) {
            Decision result = useOn(*feat, target->id, "feat");
            result.moveFirst = move;
            return result;
        }
    }
    decision.kind = DecisionKind::Attack;
    decision.target = target->id;
    decision.moveFirst = move;
    decision.reason = "attack";
    return decision;
}

bool isGenericCombatScript(const std::string &script) {
    std::string lower;
    for (char c : script) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    // The generic end-of-round scripts: K1 party members (k_hen_*), K2's
    // shared AI entry point, and the K1 default creature script.
    return lower == "k_hen_combend01" || lower == "k_ai_master" || lower == "k_def_combend01";
}

} // namespace companion

} // namespace game

} // namespace reone
