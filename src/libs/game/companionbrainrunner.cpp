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

// "Smarter companions" (C13): the snapshot the brain decides on, and the
// actions its decision queues. Behaviour notes: design note c13_companions.

#include "reone/game/companionbrain.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/coopsession.h"
#include "reone/game/d20/feat.h"
#include "reone/game/d20/feats.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/spells.h"
#include "reone/game/di/services.h"
#include "reone/game/effect.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/talent.h"
#include "reone/game/traps.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

namespace companion {

namespace {

constexpr float kShortRange = 10.0f;    // an area power with a range this short is centred on the caster
constexpr float kKnownEnemyRange = 30.0f;
constexpr float kMineRadius = kMineHalfSize + 0.5f;
constexpr int kPartyStyleDefensive = 1;
constexpr int kPartyStylePassive = 2;

bool isDroid(const Creature &creature) {
    return creature.racialType() == RacialType::Droid;
}

bool wieldsRanged(const Creature &creature) {
    auto weapon = creature.getEquippedItem(InventorySlots::rightWeapon);
    return weapon && weapon->isRanged();
}

void fillActor(Actor &actor, const Creature &creature) {
    actor.id = creature.id();
    actor.position = creature.position();
    actor.hitPoints = creature.currentHitPoints();
    actor.maxHitPoints = creature.maxHitPoints();
    actor.droid = isDroid(creature);
    actor.ranged = wieldsRanged(creature);
    auto target = creature.getAttackTarget();
    actor.target = target ? target->id() : 0;
    for (const auto &effect : creature.effects()) {
        if (effect.spellId != std::numeric_limits<uint32_t>::max()) {
            actor.activeSpells.push_back(static_cast<int>(effect.spellId));
        }
    }
    std::sort(actor.activeSpells.begin(), actor.activeSpells.end());
    actor.activeSpells.erase(std::unique(actor.activeSpells.begin(), actor.activeSpells.end()), actor.activeSpells.end());
}

void classifyCaster(Actor &actor, const Creature &creature, ISpells &spells) {
    for (SpellType type : creature.attributes().spells()) {
        auto spell = spells.get(type);
        if (!spell) {
            continue;
        }
        actor.caster = true;
        if (!spell->hostile && ((spell->category >> 8) & 0x4)) {
            actor.healer = true;
        }
    }
}

std::optional<Talent> spellTalent(const Creature &creature, const Spell &spell, uint32_t item) {
    auto role = classifySpell(spell.category, spell.hostile, item != 0);
    if (!role) {
        return std::nullopt;
    }
    Talent talent;
    talent.role = *role;
    talent.type = TalentType::Spell;
    talent.id = static_cast<int>(spell.type);
    talent.item = item;
    talent.forceCost = item != 0 ? 0 : creature.forcePowerCost(spell);
    talent.exclusion = spell.exclusion;
    talent.rank = spell.maxCR;
    bool area = ((spell.category >> 12) & 0xf) == 2;
    switch (*role) {
    case TalentRole::Heal:
    case TalentRole::Buff:
        if (item != 0) {
            talent.selfOnly = true; // medpacs, shields, stims: on oneself
        } else if (area) {
            talent.onCaster = true;
            talent.radius = kAreaHealRadius;
        } else if (spell.range <= 0.0f) {
            talent.selfOnly = true;
        }
        break;
    case TalentRole::AttackArea:
        talent.onCaster = spell.range > 0.0f && spell.range <= kShortRange;
        talent.radius = kAreaPowerRadius;
        break;
    case TalentRole::Grenade:
        talent.radius = kGrenadeRadius;
        break;
    default:
        break;
    }
    return talent;
}

void collectTalents(CompanionView &view, Game &game, const Creature &creature, ServicesView &services) {
    auto &spells = services.game.spells;
    for (SpellType type : creature.attributes().spells()) {
        auto spell = spells.get(type);
        if (!spell || !creature.canAffordForcePower(*spell)) {
            continue;
        }
        if (auto talent = spellTalent(creature, *spell, 0)) {
            view.talents.push_back(*talent);
        }
    }
    std::set<int> itemSpells;
    auto offerItem = [&](const std::shared_ptr<Item> &item) {
        auto type = item ? item->activateSpell() : std::nullopt;
        if (!type || !itemSpells.insert(static_cast<int>(*type)).second) {
            return;
        }
        auto spell = spells.get(*type);
        if (!spell) {
            return;
        }
        if (auto talent = spellTalent(creature, *spell, item->id())) {
            view.talents.push_back(*talent);
        }
    };
    for (const auto &[slot, item] : creature.equipment()) {
        offerItem(item);
    }
    for (const auto &item : creature.items()) {
        offerItem(item);
    }
    // A party member's pack is the shared party inventory the player holds.
    auto owner = game.party().actualPlayer();
    if (owner && owner.get() != &creature) {
        for (const auto &item : owner->items()) {
            offerItem(item);
        }
    }
    auto &feats = services.game.feats;
    auto attackFeats = selectAttackFeats(
        creature.attributes().feats(), view.self.ranged,
        [&feats](FeatType type) { return feats.get(type); });
    for (FeatType type : attackFeats) {
        auto feat = feats.get(type);
        if (!feat || feat->category == 0) {
            continue;
        }
        Talent talent;
        talent.role = TalentRole::AttackFeat;
        talent.type = TalentType::Feat;
        talent.id = static_cast<int>(type);
        talent.rangedFeat = feat->useType == 1;
        talent.rank = feat->maxCR;
        view.talents.push_back(talent);
    }
}

void collectHazards(CompanionView &view, Game &game, const Creature &creature, Area &area) {
    for (const auto &object : area.getObjectsByType(ObjectType::Trigger)) {
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (!trigger->isTrap() || !(trigger->isTrapDetectedByParty() || trigger->isTrapDetectedBy(creature))) {
            continue;
        }
        if (game.party().isMember(trigger->trapCreatorId())) {
            continue; // the party's own mines
        }
        view.hazards.push_back(Hazard {trigger->trapCenter(), kMineRadius});
    }
    // Area-effect clouds are not simulated as objects yet; the brain takes
    // them as hazards once they are.
}

bool partyMemberId(Game &game, uint32_t id) {
    for (const auto &member : game.party().members()) {
        if (member.creature && member.creature->id() == id) {
            return true;
        }
    }
    return false;
}

} // namespace

bool usesBrain(Game &game, const Creature &creature) {
    if (!game.options().game.smartCompanions || creature.isDead()) {
        return false;
    }
    if (!isGenericCombatScript(creature.getOnEndRound())) {
        return false; // scripted and boss creatures keep their own AI
    }
    if (!partyMemberId(game, creature.id())) {
        return false; // hostile and neutral AI stays retail
    }
    auto leader = game.party().getLeader();
    if (leader && leader->id() == creature.id() && !creature.autoCombat()) {
        return false; // the player's hand
    }
    auto *coop = game.coop();
    if (coop && coop->isPlayerControlled(creature.id())) {
        return false; // a co-op player's character
    }
    return true;
}

void runCompanionBrainRound(Game &game, Creature &creature) {
    auto &services = game.services();
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    CompanionView view;
    fillActor(view.self, creature);
    view.forcePoints = creature.currentForce();
    view.stealthed = creature.hasEffect(EffectType::Invisibility) ||
                     creature.hasEffect(EffectType::ImprovedInvisibility);
    int style = game.party().aiStyle();
    view.hold = game.party().isSoloMode() || style == kPartyStyleDefensive;
    view.passive = style == kPartyStylePassive;
    view.attackRange = creature.getAttackRange();
    if (auto weapon = creature.getEquippedItem(InventorySlots::rightWeapon); weapon && weapon->isRanged()) {
        view.preferredDistance = weapon->preferredAttackDistance();
    }
    view.mayReposition = !creature.brainRepositioned();
    auto leader = game.party().getLeader();
    if (leader) {
        view.leaderPosition = leader->position();
        auto target = leader->getAttackTarget();
        view.leaderTarget = target ? target->id() : leader->getAttemptedAttackTarget();
    } else {
        view.leaderPosition = creature.position();
    }

    for (const auto &member : game.party().members()) {
        auto ally = member.creature;
        if (!ally || ally->id() == creature.id() || ally->isDead()) {
            continue;
        }
        Actor actor;
        fillActor(actor, *ally);
        view.allies.push_back(std::move(actor));
    }
    std::sort(view.allies.begin(), view.allies.end(), [](const Actor &a, const Actor &b) { return a.id < b.id; });

    // Enemies the companion or its leader sees.
    std::set<uint32_t> known;
    for (const Creature *observer : {&creature, leader.get()}) {
        if (!observer) {
            continue;
        }
        for (const auto &[id, reference] : observer->perception().seen) {
            known.insert(id);
        }
    }
    for (uint32_t id : known) {
        auto other = game.getObjectById<Creature>(id);
        if (!other || other->isDead() || !services.game.reputes.getIsEnemy(creature, *other)) {
            continue;
        }
        if (glm::distance(other->position(), creature.position()) > kKnownEnemyRange) {
            continue;
        }
        Actor actor;
        fillActor(actor, *other);
        classifyCaster(actor, *other, services.game.spells);
        view.enemies.push_back(std::move(actor));
    }
    collectTalents(view, game, creature, services);
    collectHazards(view, game, creature, *area);

    Decision decision = decide(view, [](int min, int max) {
        return gameplayRandom().uniformInt(min, max);
    });
    creature.setBrainRepositioned(decision.moveFirst.has_value());

    // Only ever printed with the option on.
    info(str(boost::format("Companion brain: %s -> %s target=%u talent=%d move=%d hp=%d/%d talents=%u allies=%u enemies=%u") %
             creature.tag() % decision.reason % decision.target %
             (decision.kind == DecisionKind::TalentOnObject ? decision.talent.id : -1) %
             (decision.moveFirst ? 1 : 0) % view.self.hitPoints % view.self.maxHitPoints %
             view.talents.size() % view.allies.size() % view.enemies.size()));
    if (decision.kind == DecisionKind::None && !decision.moveFirst) {
        return;
    }
    creature.clearAllActions();
    if (decision.moveFirst) {
        creature.addAction(game.newAction<MoveToPointAction>(*decision.moveFirst));
    }
    auto target = decision.target != 0 ? game.getObjectById(decision.target) : nullptr;
    switch (decision.kind) {
    case DecisionKind::Attack:
        if (target) {
            creature.addAction(game.newAction<AttackObjectAction>(target));
        }
        break;
    case DecisionKind::TalentOnObject:
        if (target) {
            auto talent = decision.talent.item != 0
                              ? std::make_shared<game::Talent>(decision.talent.type, decision.talent.id, 0, decision.talent.item)
                              : std::make_shared<game::Talent>(decision.talent.type, decision.talent.id);
            creature.addAction(game.newAction<UseTalentOnObjectAction>(std::move(talent), std::move(target)));
        }
        break;
    default:
        break;
    }
}

} // namespace companion

} // namespace game

} // namespace reone
