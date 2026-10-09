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

#include <glm/geometric.hpp>

namespace reone {

namespace game {

namespace companion {

namespace {

constexpr uint32_t kExcludesDroids = 0x02;
constexpr uint32_t kExcludesOrganics = 0x01;

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

bool usableOn(const Talent &talent, const Actor &target) {
    if (target.droid && (talent.exclusion & kExcludesDroids)) {
        return false;
    }
    if (!target.droid && (talent.exclusion & kExcludesOrganics)) {
        return false;
    }
    return true;
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

int enemiesWithin(const CompanionView &view, const glm::vec3 &point, float radius) {
    int count = 0;
    for (const Actor &enemy : view.enemies) {
        if (planarDistance(point, enemy.position) <= radius) {
            ++count;
        }
    }
    return count;
}

/** Talents of a role, strongest first; equal ranks keep their id order. */
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
        if (a->id != b->id) {
            return a->id < b->id;
        }
        return a->item < b->item;
    });
    return result;
}

bool affordable(const CompanionView &view, const Talent &talent, int reserve) {
    return talent.forceCost <= 0 || view.forcePoints >= talent.forceCost + reserve;
}

Decision useOn(const Talent &talent, uint32_t target, std::string reason) {
    Decision decision;
    decision.kind = DecisionKind::TalentOnObject;
    decision.talent = talent;
    decision.target = target;
    decision.reason = std::move(reason);
    return decision;
}

std::optional<Decision> chooseHeal(const CompanionView &view) {
    std::vector<const Actor *> hurt;
    if (view.self.hitPoints > 0 && view.self.healthRatio() < kHealThreshold) {
        hurt.push_back(&view.self);
    }
    for (const Actor &ally : view.allies) {
        if (ally.hitPoints > 0 && ally.healthRatio() < kHealThreshold) {
            hurt.push_back(&ally);
        }
    }
    if (hurt.empty()) {
        return std::nullopt;
    }
    std::stable_sort(hurt.begin(), hurt.end(), [](const Actor *a, const Actor *b) {
        if (a->healthRatio() != b->healthRatio()) {
            return a->healthRatio() < b->healthRatio();
        }
        return a->id < b->id;
    });
    auto heals = talentsOf(view, TalentRole::Heal);
    // Force heals first, for the most hurt party member.
    for (const Actor *patient : hurt) {
        for (const Talent *heal : heals) {
            if (heal->item != 0 || !affordable(view, *heal, 0) || !usableOn(*heal, *patient)) {
                continue;
            }
            bool isSelf = patient->id == view.self.id;
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
        break; // only the most hurt decides which Force heal to use
    }
    // Medpacs and repair kits: on oneself, when badly hurt.
    if (view.self.healthRatio() < kItemHealThreshold) {
        for (const Talent *heal : heals) {
            if (heal->item != 0 && usableOn(*heal, view.self)) {
                return useOn(*heal, view.self.id, "medpac");
            }
        }
    }
    return std::nullopt;
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
            return std::nullopt; // droid utilities: left to the retail AI
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
    if (auto heal = chooseHeal(view)) {
        heal->moveFirst = escape;
        return *heal;
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

    // Buffs: not at a fight's last enemy, and never one already running.
    if (view.enemies.size() >= 2) {
        for (const Talent *buff : talentsOf(view, TalentRole::Buff)) {
            if (!buff->selfOnly && !buff->onCaster) {
                continue; // single-target buffs on others: left to the player
            }
            if (!affordable(view, *buff, reserve) || !usableOn(*buff, view.self) ||
                view.self.hasSpellEffect(buff->id)) {
                continue;
            }
            Decision result = useOn(*buff, view.self.id, "buff");
            result.moveFirst = escape;
            return result;
        }
    }

    // Area attacks, only where no party member stands in the blast.
    for (TalentRole role : {TalentRole::Grenade, TalentRole::AttackArea}) {
        for (const Talent *area : talentsOf(view, role)) {
            if (!affordable(view, *area, reserve)) {
                continue;
            }
            float radius = area->radius > 0.0f ? area->radius : (role == TalentRole::Grenade ? kGrenadeRadius : kAreaPowerRadius);
            glm::vec3 centre = area->onCaster ? view.self.position : target->position;
            if (enemiesWithin(view, centre, radius) < 2) {
                continue;
            }
            if (catchesAllies(view, centre, radius, area->onCaster)) {
                continue;
            }
            // Where a power's shape is unknown, it must also spare the
            // allies around the caster.
            if (role == TalentRole::AttackArea && !area->onCaster &&
                catchesAllies(view, view.self.position, radius, true)) {
                continue;
            }
            Decision result = useOn(*area, target->id, role == TalentRole::Grenade ? "grenade" : "area-power");
            result.moveFirst = escape;
            return result;
        }
    }

    // Single-target powers: half the rounds, never on a nearly dead enemy or
    // one already under the same power.
    std::vector<const Talent *> singles;
    for (const Talent *power : talentsOf(view, TalentRole::AttackSingle)) {
        if (affordable(view, *power, reserve) && !target->hasSpellEffect(power->id) &&
            target->healthRatio() >= kWeakEnemyThreshold && usableOn(*power, *target)) {
            singles.push_back(power);
        }
    }
    if (!singles.empty() && random(0, 1) == 0) {
        size_t best = 0;
        while (best + 1 < singles.size() && singles[best + 1]->rank == singles[0]->rank) {
            ++best;
        }
        int index = best == 0 ? 0 : std::clamp(random(0, static_cast<int>(best)), 0, static_cast<int>(best));
        Decision result = useOn(*singles[index], target->id, "power");
        result.moveFirst = escape;
        return result;
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
