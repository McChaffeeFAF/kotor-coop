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

#include "reone/game/traps.h"

#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

std::optional<TrapRow> loadTrapRow(resource::ITwoDAs &twoDas, int row) {
    auto traps = twoDas.get("traps");
    if (!traps || row < 0 || row >= traps->getRowCount()) {
        return std::nullopt;
    }
    TrapRow result;
    result.script = traps->getString(row, "trapscript");
    result.setDC = traps->getInt(row, "setdc");
    result.detectDCMod = traps->getInt(row, "detectdcmod");
    result.disarmDCMod = traps->getInt(row, "disarmdcmod");
    result.nameStrRef = traps->getInt(row, "trapname", -1);
    result.kitResRef = traps->getString(row, "resref");
    result.model = traps->getString(row, "model");
    result.explosionSound = traps->getString(row, "explosionsound");
    return result;
}

int trapDifficultyShift(int clientDifficulty) {
    switch (clientDifficulty) {
    case 0:
        return -5;
    case 2:
        return 5;
    default:
        return 0;
    }
}

bool trapFiresOn(int reputation, Faction trapFaction, Faction creatureFaction) {
    return reputation <= 10 && trapFaction != creatureFaction;
}

bool trapDetectRollSucceeds(int awareness, int d10, int detectDC) {
    return awareness + d10 >= detectDC;
}

TrapSkillOutcome resolveDisarm(int total, int disarmDC) {
    if (disarmDC < kUnbeatableTrapDC && total >= disarmDC) {
        return TrapSkillOutcome::Success;
    }
    return total < disarmDC - 10 ? TrapSkillOutcome::Triggered : TrapSkillOutcome::Failure;
}

TrapSkillOutcome resolveRecover(int total, int disarmDC) {
    int dc = disarmDC + 10;
    if (dc < kUnbeatableTrapDC && total >= dc) {
        return TrapSkillOutcome::Success;
    }
    return total < dc - 5 ? TrapSkillOutcome::Triggered : TrapSkillOutcome::Failure;
}

SetMineOutcome resolveSetMine(int total, int setDC, bool tookTwenty) {
    if (total >= setDC) {
        return SetMineOutcome::Success;
    }
    if (!tookTwenty && total < setDC - 10) {
        return SetMineOutcome::CriticalFailure;
    }
    return SetMineOutcome::Failure;
}

int trapSkillXP(int level, int dc) {
    return dc >= level + 20 ? level * 15 : level * 10;
}

int mineTrapRow(const Item &item) {
    const auto &properties = item.properties();
    if (properties.empty() || properties.front().propertyName != kItemPropertyTrap) {
        return -1;
    }
    return properties.front().subtype;
}

int demolitionsRoll(const Creature &creature, bool &tookTwenty) {
    tookTwenty = !creature.isInCombat();
    return tookTwenty ? 20 : randomInt(1, 20);
}

static int trapDCShift(Game &game) {
    return trapDifficultyShift(static_cast<int>(game.options().game.clientDifficulty));
}

void updateTrapDetection(Game &game, Area &area) {
    Party &party = game.party();
    std::vector<std::shared_ptr<Trigger>> traps;
    for (const auto &object : area.getObjectsByType(ObjectType::Trigger)) {
        auto trigger = std::static_pointer_cast<Trigger>(object);
        if (trigger->isTrap() && trigger->trapFields().detectable && !trigger->isTrapDetectedByParty()) {
            traps.push_back(std::move(trigger));
        }
    }
    if (traps.empty()) {
        return;
    }
    int shift = trapDCShift(game);
    float range2 = kTrapDetectRange * kTrapDetectRange;
    for (int i = 0; i < party.getSize(); ++i) {
        auto member = party.getMember(i);
        if (!member || member->isDead()) {
            continue;
        }
        for (const auto &trap : traps) {
            if (trap->isTrapDetectedByParty()) {
                continue;
            }
            if (member->getSquareDistanceTo(trap->trapCenter()) > range2) {
                continue;
            }
            int awareness = member->getSkillValue(SkillType::Awareness);
            int dc = trap->trapFields().detectDC + shift;
            if (trapDetectRollSucceeds(awareness, randomInt(1, 10), dc)) {
                trap->setTrapDetectedBy(member->id(), true);
                area.setLastTrapDetected(member->id(), trap->id());
            }
        }
    }
}

bool performTrapSkill(Game &game, ServicesView &services, Creature &actor, Trigger &trap, TrapSkill skill, float dt) {
    if (!trap.isTrap()) {
        return true;
    }
    if (!actor.navigateTo(trap.trapCenter(), true, kTrapApproachDistance, dt)) {
        return false;
    }
    actor.face(trap);

    TrapFields fields = trap.trapFields();
    switch (skill) {
    case TrapSkill::Flag:
        trap.setTrapFlagged(true);
        return true;
    case TrapSkill::Examine:
        return true;
    default:
        break;
    }
    if (!fields.disarmable) {
        return true;
    }
    int dc = fields.disarmDC + trapDCShift(game);
    bool tookTwenty = false;
    int total = demolitionsRoll(actor, tookTwenty) + actor.getSkillValue(SkillType::Demolitions);
    bool recover = skill == TrapSkill::Recover;
    TrapSkillOutcome outcome = recover ? resolveRecover(total, dc) : resolveDisarm(total, dc);
    switch (outcome) {
    case TrapSkillOutcome::Success: {
        int level = actor.attributes().getAggregateLevel();
        int amount = trapSkillXP(level, recover ? dc + 10 : dc);
        if (amount > 0) {
            game.party().awardXP(amount, XPSource::Skill);
        }
        if (recover) {
            if (auto row = loadTrapRow(services.resource.twoDas, fields.baseType)) {
                if (!row->kitResRef.empty()) {
                    auto receiver = game.party().sharedInventoryReceiver(game.getObjectById(actor.id()));
                    (receiver ? *receiver : static_cast<Object &>(actor)).addItem(row->kitResRef, 1, true);
                }
            }
        }
        trap.disarmTrap(actor.id());
        break;
    }
    case TrapSkillOutcome::Triggered:
        trap.fireTrap(actor.id());
        break;
    default:
        break;
    }
    return true;
}

std::shared_ptr<Trigger> spawnMine(Game &game, ServicesView &services, int trapRow, const glm::vec3 &position, Faction faction,
                                   uint32_t creatorId, int detectDC, int disarmDC) {
    auto row = loadTrapRow(services.resource.twoDas, trapRow);
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!row || !area) {
        return nullptr;
    }
    auto trigger = game.newTrigger();
    trigger->configureMine(trapRow, position, faction, creatorId, detectDC, disarmDC);
    area->add(trigger);
    return trigger;
}

std::shared_ptr<Trigger> setMine(Game &game, ServicesView &services, Creature &setter, Item &mine, const glm::vec3 &position) {
    int rowIndex = mineTrapRow(mine);
    auto row = loadTrapRow(services.resource.twoDas, rowIndex);
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!row || !area) {
        return nullptr;
    }
    int dc = std::max(1, row->setDC + trapDCShift(game));
    bool tookTwenty = false;
    int total = demolitionsRoll(setter, tookTwenty) + setter.getSkillValue(SkillType::Demolitions);
    SetMineOutcome outcome = resolveSetMine(total, dc, tookTwenty);
    if (outcome == SetMineOutcome::Failure) {
        return nullptr;
    }

    // One mine is used up, from whoever holds it.
    auto item = std::static_pointer_cast<Item>(game.getObjectById(mine.id()));
    if (item) {
        auto holder = game.getObjectById(item->owner());
        Object &owner = holder ? *holder : static_cast<Object &>(setter);
        bool last = false;
        if (owner.removeItem(item, last) && last) {
            game.destroyRuntimeObjectGraph(item);
        }
    }

    auto trigger = game.newTrigger();
    trigger->configureMine(
        rowIndex, position, setter.faction(), setter.id(),
        total + row->detectDCMod, total + row->disarmDCMod);
    area->add(trigger);
    trigger->setTrapDetectedBy(setter.id(), game.party().isMember(setter));
    if (outcome == SetMineOutcome::CriticalFailure) {
        trigger->fireTrap(setter.id());
    }
    return trigger;
}

} // namespace game

} // namespace reone
