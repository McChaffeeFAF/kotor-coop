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

#include <cmath>
#include <cstdio>

#include "reone/system/portablemath.h"
#include "reone/game/d20/forms.h"
#include "reone/resource/k2merge.h"
#include "reone/game/d20/k2feats.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/interactobject.h"
#include "reone/game/action/unlockobject.h"
#include "reone/game/action/useskill.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/barkstring.h"
#include "reone/game/action/docommand.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/visual.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/event.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/creaturestates.h"
#include "reone/game/gameclock.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/reputes.h"
#include "reone/game/shapequery.h"
#include "reone/game/script/routine/argutil.h"
#include "reone/game/script/routine/patternmatch.h"
#include "reone/game/script/routine/context.h"
#include "reone/game/script/routine/objectutil.h"
#include "reone/game/script/routines.h"
#include "reone/game/talent.h"
#include "reone/game/talents.h"
#include "reone/game/party.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/object/item.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/object/area.h"
#include "reone/game/object/module.h"
#include "reone/game/journal.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/game/object/sound.h"
#include "reone/game/traps.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/gff.h"
#include "reone/resource/strings.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/routine/exception/argument.h"
#include "reone/script/routine/exception/notimplemented.h"
#include "reone/script/variable.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

#define R_VOID script::VariableType::Void
#define R_INT script::VariableType::Int
#define R_FLOAT script::VariableType::Float
#define R_OBJECT script::VariableType::Object
#define R_STRING script::VariableType::String
#define R_EFFECT script::VariableType::Effect
#define R_EVENT script::VariableType::Event
#define R_LOCATION script::VariableType::Location
#define R_TALENT script::VariableType::Talent
#define R_VECTOR script::VariableType::Vector
#define R_ACTION script::VariableType::Action

#ifdef _DEBUG
static constexpr bool kShipBuild = false;
#else
static constexpr bool kShipBuild = true;
#endif

using namespace reone::script;
using namespace reone::scene;

namespace reone {

namespace game {

// dyn_cast that tolerates a null object (an OBJECT_INVALID argument).
// The calling object, null when the routine runs without one (tests, console).
static std::shared_ptr<Object> callerOrNull(const RoutineContext &ctx) {
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    return caller ? ctx.game.getObjectById(caller->objectId) : nullptr;
}

static std::shared_ptr<Creature> asCreature(const std::shared_ptr<Object> &object) {
    return object ? dyn_cast<Creature>(object) : nullptr;
}

static Variable Random(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMaxInteger = getInt(args, 0);
    if (nMaxInteger <= 0) {
        return Variable::ofInt(0);
    }

    // Transform

    // Execute
    return Variable::ofInt(randomInt(0, nMaxInteger - 1));
}

static Variable PrintString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    info(sString);
    return Variable::ofNull();
}

static Variable PrintFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);
    auto nWidth = getIntOrElse(args, 1, 18);
    auto nDecimals = getIntOrElse(args, 2, 9);

    // Transform

    // Execute
    // Debug output only: width and decimals as documented, to the log like PrintString.
    char text[128];
    std::snprintf(text, sizeof(text), "%*.*f", std::clamp(nWidth, 0, 64), std::clamp(nDecimals, 0, 32), fFloat);
    info(text);
    return Variable::ofNull();
}

static Variable FloatToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);
    auto nWidth = getIntOrElse(args, 1, 18);
    auto nDecimals = getIntOrElse(args, 2, 9);

    // Transform
    // nwscript.nss: the width is 0 to 18 and the decimals 0 to 9; the number is
    // right-justified in that width, and keeps its decimal point when there are
    // no decimals (printf "%#*.*f"): the swoop dashboard builds "meter" +
    // FloatToString(x, 1, 0) and drops the last character to name its
    // animation, which only gives "meter4" when the result is "4.".
    int width = glm::clamp(nWidth, 0, 18);
    int decimals = glm::clamp(nDecimals, 0, 9);

    // Execute
    char text[64];
    std::snprintf(text, sizeof(text), "%#*.*f", width, decimals, std::isfinite(fFloat) ? fFloat : 0.0f);
    return Variable::ofString(text);
}

static Variable PrintInteger(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    info(std::to_string(nInteger));
    return Variable::ofNull();
}

static Variable PrintObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    info("Object " + std::to_string(oObject ? oObject->id() : kObjectInvalid));
    return Variable::ofNull();
}

static Variable AssignCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oActionSubject = getObject(args, 0, ctx);
    auto aActionToAssign = getAction(args, 1);

    // Transform

    // Execute
    // An assigned command runs in place as the subject, it is not itself
    // queued. What it contains decides what reaches the queue: an Action
    // routine inside it queues on the subject, anything else takes effect now.
    //
    // Queueing the command starves it behind whatever the subject is already
    // doing, and scripts rely on the difference. 103PER rolls the player
    // mid-run with AssignCommand(oPC, PlayOverlayAnimation(...)) while an
    // ActionMoveToLocation is still in flight, then queues a head turn behind
    // that move - a sequence that only reads correctly if the overlay lands
    // immediately and the head turn waits.
    runCommandAsActor(*aActionToAssign, *oActionSubject);
    return Variable::ofNull();
}

static Variable DelayCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fSeconds = getFloat(args, 0);
    auto aActionToDelay = getAction(args, 1);

    // Transform

    // Execute
    auto commandAction = ctx.game.newAction<DoCommandAction>(std::move(aActionToDelay));
    getCaller(ctx)->delayAction(std::move(commandAction), fSeconds);
    return Variable::ofNull();
}

static Variable ExecuteScript(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sScript = getString(args, 0);
    auto oTarget = getObject(args, 1, ctx);
    auto nScriptVar = getIntOrElse(args, 2, -1);

    // Transform

    // Execute
    std::vector<script::Argument> scriptArgs;
    scriptArgs.emplace_back(script::ArgKind::Caller, Variable::ofObject(oTarget->id()));
    scriptArgs.emplace_back(script::ArgKind::ScriptVar, Variable::ofInt(nScriptVar));

    // Copy all arguments from the parent execution context. This essentially
    // extends lifetime of arguments to the callee script. For example, onNotice
    // scripts call ExecuteScript for k_ai_master, which in turn needs
    // GetLastPerception and other perception arguments.
    for (const Argument &origArg : ctx.execution.args) {
        scriptArgs.emplace_back(origArg);
    }

    // A K2 program (the K2 systems merge) runs K2's script of that name.
    if (ctx.execution.routines && ctx.execution.routines != &ctx.game.routines() &&
        !resource::isK2ScriptName(sScript)) {
        sScript = std::string(resource::kK2ScriptPrefix) + sScript;
    }

    ctx.game.scriptRunner().run(sScript, scriptArgs);

    return Variable::ofNull();
}

static Variable ClearAllActions(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    getCaller(ctx)->clearAllActions();
    return Variable::ofNull();
}

static Variable SetFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fDirection = getFloat(args, 0);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    caller->setFacing(glm::radians(fDirection));
    return Variable::ofNull();
}

static Variable SwitchPlayerCharacter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    Party &party = ctx.game.party();

    // The canonical PC is not a roster entry. Retail parks it while another
    // actor stands in, and -1 asks for it back; the available roster only ever
    // holds companions.
    std::shared_ptr<Creature> creature;
    if (nNPC == kNpcPlayer) {
        if (party.controlledNpc() == kNpcPlayer) {
            return Variable::ofInt(1);
        }
        creature = party.actualPlayer();
    } else {
        if (party.controlledNpc() == nNPC) {
            return Variable::ofInt(1);
        }
        creature = party.getAvailableMember(nNPC, true);
    }
    if (!creature) {
        warn("Party: NPC not found: " + std::to_string(nNPC));
        return Variable::ofInt(0);
    }

    auto currentLeader = party.getLeader();
    glm::vec3 position(currentLeader ? currentLeader->position() : creature->position());
    float facing = currentLeader ? currentLeader->getFacing() : creature->getFacing();

    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    party.setControlledMember(nNPC, creature);

    if (area) {
        // Retail changes the controlled object inside the existing Area. Do
        // not invoke module-boundary retirement merely to place the new PC.
        area->placeControlledCreature(creature, position, facing);
        area->onPartyLeaderMoved(true);
        area->update3rdPersonCameraFacing();
    }
    return Variable::ofInt(1);
}

static Variable SetTime(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nHour = getInt(args, 0);
    auto nMinute = getInt(args, 1);
    auto nSecond = getInt(args, 2);
    auto nMillisecond = getInt(args, 3);

    // Execute
    // Forward only: an earlier time of day is reached on the next day.
    int minutesPerHour = ctx.game.minutesPerHour() == 0 ? 5 : ctx.game.minutesPerHour();
    uint32_t hour = static_cast<uint32_t>(std::clamp(nHour, 0, 23));
    uint32_t minute = static_cast<uint32_t>(std::clamp(nMinute, 0, minutesPerHour - 1));
    uint32_t second = static_cast<uint32_t>(std::clamp(nSecond, 0, 59));
    uint32_t millisecond = static_cast<uint32_t>(std::clamp(nMillisecond, 0, 999));
    ctx.game.setTimeOfDayForward(
        hour * static_cast<uint32_t>(minutesPerHour) * 60000u + minute * 60000u + second * 1000u + millisecond);
    return Variable::ofNull();
}

static Variable SetPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute: handing the lead to the player character stands a downed one up first (and
    // leaves it raiseable), whether or not it already leads.
    auto &party = ctx.game.party();
    if (nNPC == kNpcPlayer) {
        for (int i = 0; i < static_cast<int>(party.getSize()); ++i) {
            auto member = party.getMember(i);
            if (member && member->isPC() && member->isTemporarilyDead()) {
                member->setRaiseable(true);
                if (!member->resurrect(0)) {
                    member->setCurrentHitPoints(1); // down but not dead: back on its feet
                }
            }
        }
    }
    return Variable::ofInt(party.setPartyLeader(nNPC) ? 1 : 0);
}

static Variable SetAreaUnescapable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bUnescapable = getInt(args, 0);

    // Transform
    auto unescapable = static_cast<bool>(bUnescapable);

    // Execute
    ctx.game.module()->area()->setUnescapable(unescapable);
    return Variable::ofNull();
}

static Variable GetAreaUnescapable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool unescapable = ctx.game.module()->area()->isUnescapable();
    return Variable::ofInt(static_cast<int>(unescapable));
}

// Game time. The day has 24 hours of Mod_MinPerHour real minutes each:
// GetTimeHour is the hour of the day, GetTimeMinute the minute within the
// hour (0 .. MinPerHour - 1), then the second and millisecond within that
// minute. The swoop race timers compute elapsed time from exactly these.
static uint32_t millisecondsPerGameHour(const RoutineContext &ctx) {
    uint32_t minutes = ctx.game.minutesPerHour() == 0 ? 5u : ctx.game.minutesPerHour();
    return minutes * 60u * 1000u;
}

// Day, night, dawn and dusk from the module's Mod_DawnHour/Mod_DuskHour.
static DayState currentDayState(const RoutineContext &ctx) {
    int hour = static_cast<int>(ctx.game.worldTimeOfDay() / millisecondsPerGameHour(ctx));
    auto module = ctx.game.module();
    if (!module) {
        return dayStateAt(hour, 0, 0);
    }
    return dayStateAt(hour, module->info().dawnHour, module->info().duskHour);
}

static Variable GetTimeHour(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(ctx.game.worldTimeOfDay() / millisecondsPerGameHour(ctx)));
}

static Variable GetTimeMinute(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    uint32_t withinHour = ctx.game.worldTimeOfDay() % millisecondsPerGameHour(ctx);
    return Variable::ofInt(static_cast<int>(withinHour / 60000u));
}

static Variable GetTimeSecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>((ctx.game.worldTimeOfDay() % 60000u) / 1000u));
}

static Variable GetTimeMillisecond(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(ctx.game.worldTimeOfDay() % 1000u));
}

static Variable GetArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto area = ctx.game.module()->area();
    return Variable::ofObject(getObjectIdOrInvalid(area));
}

static Variable GetEnteringObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: what the caller keeps (so a delayed or executed script still reads it), else the
    // event's own argument.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    if (caller && caller->lastEnteredId() != kObjectInvalid) {
        return Variable::ofObject(caller->lastEnteredId());
    }
    if (const Variable *entering = ctx.execution.findArg(ArgKind::EnteringObject)) {
        return *entering;
    }

    return Variable::ofObject(kObjectInvalid);
}

static Variable GetExitingObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: as GetEnteringObject, for who last left.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto caller = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    if (caller && caller->lastLeftId() != kObjectInvalid) {
        return Variable::ofObject(caller->lastLeftId());
    }
    if (const Variable *exiting = ctx.execution.findArg(ArgKind::ExitingObject)) {
        return *exiting;
    }

    return Variable::ofObject(kObjectInvalid);
}

static Variable GetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofVector(oTarget->position());
}

static Variable GetFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    float facing = glm::degrees(oTarget->getFacing());
    return Variable::ofFloat(facing);
}

static Variable GetItemPossessor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    // The object whose inventory or equipment holds the item (design note c11_data);
    // an item on the ground or in nobody's hands has no possessor.
    if (!item || item->owner() == 0 || item->owner() == kObjectInvalid) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto possessor = ctx.game.getObjectById(item->owner());
    return Variable::ofObject(getObjectIdOrInvalid(possessor));
}

static Variable GetItemPossessedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto sItemTag = getString(args, 1);

    // Transform
    auto itemTag = boost::to_lower_copy(sItemTag);

    // Execute
    if (itemTag.empty()) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto item = oObject->getItemByTag(itemTag);
    if (!item) {
        if (auto creature = dyn_cast<Creature>(oObject)) {
            for (auto &[slot, equippedItem] : creature->equipment()) {
                if (equippedItem->tag() == itemTag) {
                    item = equippedItem;
                }
            }
        }
    }

    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable CreateItemOnObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sItemTemplate = getString(args, 0);
    auto oTarget = getObjectOrCaller(args, 1, ctx);
    auto nStackSize = getIntOrElse(args, 2, 1);
    auto nHideMessage = getIntOrElse(args, 3, 0);

    // Transform
    auto itemTemplate = boost::to_lower_copy(sItemTemplate);

    // Execute
    if (itemTemplate.empty()) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto receiver = ctx.game.party().sharedInventoryReceiver(oTarget);
    auto item = receiver->addItem(itemTemplate, nStackSize, true);
    // A created item has no previous possessor.
    raisePartyItemEvents(ctx.game, itemTemplate, item, nullptr, receiver.get());
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable GetLastAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttackee = getObjectOrNull(args, 0, ctx);
    if (args.empty() || args[0].objectId == kObjectSelf) {
        oAttackee = getCaller(ctx);
    }

    // Execute
    if (!oAttackee) {
        return Variable::ofObject(kObjectInvalid);
    }
    // Inside the attackee's own attack/damage/death event, the event's
    // attacker. Anywhere else, the attackee's remembered last attacker.
    const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
    bool isCaller = callerArg && callerArg->objectId == oAttackee->id();
    if (isCaller) {
        if (const Variable *attacker = ctx.execution.findArg(ArgKind::LastAttacker)) {
            return *attacker;
        }
    }
    return Variable::ofObject(oAttackee->getLastHostileActor());
}

static Variable GetNearestCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFirstCriteriaType = getInt(args, 0);
    auto nFirstCriteriaValue = getInt(args, 1);
    auto oTarget = getObjectOrCaller(args, 2, ctx);
    auto nNth = getIntOrElse(args, 3, 1);
    auto nSecondCriteriaType = getIntOrElse(args, 4, -1);
    auto nSecondCriteriaValue = getIntOrElse(args, 5, -1);
    auto nThirdCriteriaType = getIntOrElse(args, 6, -1);
    auto nThirdCriteriaValue = getIntOrElse(args, 7, -1);

    // Transform
    auto firstCriteriaType = static_cast<CreatureType>(nFirstCriteriaType);
    auto secondCriteriaType = static_cast<CreatureType>(nSecondCriteriaType);
    auto thirdCriteriaType = static_cast<CreatureType>(nThirdCriteriaType);

    // Execute
    Area::SearchCriteriaList criterias;
    criterias.push_back(std::make_pair(firstCriteriaType, nFirstCriteriaValue));
    if (secondCriteriaType != CreatureType::Invalid) {
        criterias.push_back(std::make_pair(secondCriteriaType, nSecondCriteriaValue));
    }
    if (thirdCriteriaType != CreatureType::Invalid) {
        criterias.push_back(std::make_pair(thirdCriteriaType, nThirdCriteriaValue));
    }
    auto creature = ctx.game.module()->area()->getNearestCreature(oTarget, criterias, nNth - 1);
    return Variable::ofObject(getObjectIdOrInvalid(creature));
}

static Variable GetDistanceToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    return Variable::ofFloat(caller->getDistanceTo(*oObject));
}

static Variable GetIsObjectValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    bool valid;
    try {
        auto oObject = getObject(args, 0, ctx);
        valid = oObject && !oObject->isDead();
    } catch (const RoutineArgumentException &ignored) {
        valid = false;
    }
    return Variable::ofInt(static_cast<bool>(valid));
}

static Variable SetCameraFacing(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto fDirection = getFloat(args, 0);
    (void)fDirection;

    // Execute
    // intentionally stubbed: the chase camera follows the party leader; a script cannot turn it
    return Variable::ofNull();
}

static Variable PlaySound(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sSoundName = getString(args, 0);

    // Transform
    auto caller = getCaller(ctx);

    // Execute
    // A one-shot sound at the calling object. The module and areas have no
    // place in the world, so a sound played from their scripts is silent.
    if (sSoundName.empty() || caller->type() == ObjectType::Module || caller->type() == ObjectType::Area) {
        return Variable::ofNull();
    }
    auto clip = ctx.services.resource.audioClips.get(boost::to_lower_copy(sSoundName));
    if (!clip) {
        return Variable::ofNull();
    }
    glm::vec3 position(caller->position());
    if (caller->type() == ObjectType::Creature) {
        position.z += 1.7f; // head height, as creature sound sets
    }
    ctx.services.audio.mixer.play(std::move(clip), audio::AudioType::Sound, 1.0f, false, position);
    // A minigame's scripts run on the co-op host only: its guests hear this through the state.
    ctx.game.recordMiniGameSound(boost::to_lower_copy(sSoundName));
    return Variable::ofNull();
}

static Variable GetSpellTargetObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Set by CastSpellAtObjectAction for the impact script it runs.
    if (const Variable *target = ctx.execution.findArg(ArgKind::SpellTargetObject)) {
        return *target;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetCurrentHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    int hitPoints = oObject->currentHitPoints();
    return Variable::ofInt(hitPoints);
}

static Variable GetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    int hitPoints = oObject->maxHitPoints();
    return Variable::ofInt(hitPoints);
}

static Variable GetLastItemEquipped(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The item most recently put on by any creature; invalid once it has gone from the world.
    if (auto module = ctx.game.module()) {
        uint32_t id = module->lastItemEquipped();
        if (id != script::kObjectInvalid && ctx.game.getObjectById(id)) {
            return Variable::ofObject(id);
        }
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetSubScreenID(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.subScreenId());
}

static Variable CancelCombat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCreature = getObjectOrNull(args, 0, ctx);

    // Execute
    if (auto creature = asCreature(oidCreature)) {
        creature->cancelCombat();
    }
    return Variable::ofNull();
}

static Variable GetCurrentForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Execute: 0 for anything but a creature (design note c03_stats).
    auto creature = asCreature(oObject);
    return Variable::ofInt(creature ? creature->currentForce() : 0);
}

static Variable GetMaxForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = asCreature(oObject);
    return Variable::ofInt(creature ? creature->maxForcePoints() : 0);
}

static Variable PauseGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bPause = getInt(args, 0);

    // Execute
    ctx.game.setPaused(bPause != 0);
    return Variable::ofNull();
}

static Variable SetPlayerRestrictMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bRestrict = getInt(args, 0);

    // Transform
    auto restrict = static_cast<bool>(bRestrict);

    // Execute
    ctx.game.module()->player().setRestrictMode(restrict);
    return Variable::ofNull();
}

static Variable GetStringLength(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(sString.length()));
}

static Variable GetStringUpperCase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofString(boost::to_upper_copy(sString, std::locale::classic()));
}

static Variable GetStringLowerCase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofString(boost::to_lower_copy(sString, std::locale::classic()));
}

static Variable GetStringRight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nCount = getInt(args, 1);

    // Transform

    // Execute
    std::string right;
    if (sString.size() >= nCount) {
        right = sString.substr(sString.length() - nCount, nCount);
    }
    return Variable::ofString(std::move(right));
}

static Variable GetStringLeft(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nCount = getInt(args, 1);

    // Transform

    // Execute
    std::string left;
    if (sString.size() >= nCount) {
        left = sString.substr(0, nCount);
    }
    return Variable::ofString(std::move(left));
}

static Variable InsertString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sDestination = getString(args, 0);
    auto sString = getString(args, 1);
    auto nPosition = getInt(args, 2);

    // Execute: inserted before the character at nPosition (0-based); a position
    // before the start inserts at the start, one past the end appends.
    auto position = static_cast<size_t>(std::clamp(nPosition, 0, static_cast<int>(sDestination.size())));
    return Variable::ofString(sDestination.substr(0, position) + sString + sDestination.substr(position));
}

static Variable GetSubString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nStart = getInt(args, 1);
    auto nCount = getInt(args, 2);

    // Transform

    // Execute
    // nCount is a character count, not an end position. Out-of-range requests
    // yield the empty string the declaration documents for an error, the way
    // GetStringLeft and GetStringRight already handle theirs. Widened so that
    // extreme arguments cannot overflow the range check.
    std::string substring;
    if (nStart >= 0 && nCount >= 0 &&
        static_cast<int64_t>(nStart) + nCount <= static_cast<int64_t>(sString.size())) {
        substring = sString.substr(nStart, nCount);
    }
    return Variable::ofString(std::move(substring));
}

static Variable FindSubString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto sSubString = getString(args, 1);

    // Transform

    // Execute
    size_t pos = sString.find(sSubString);
    return Variable::ofInt(pos != std::string::npos ? static_cast<int>(pos) : -1);
}

static Variable fabs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(std::fabs(fValue));
}

static Variable cos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(portable::cosf(fValue));
}

static Variable sin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(portable::sinf(fValue));
}

static Variable tan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(portable::sinf(fValue) / portable::cosf(fValue));
}

static Variable acos(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    // Zero outside [-1, 1], as documented, rather than NaN.
    return Variable::ofFloat(fValue > 1.0f || fValue < -1.0f ? 0.0f : portable::atan2f(std::sqrt(1.0f - fValue * fValue), fValue));
}

static Variable asin(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    // Zero outside [-1, 1], as documented, rather than NaN.
    return Variable::ofFloat(fValue > 1.0f || fValue < -1.0f ? 0.0f : portable::atan2f(fValue, std::sqrt(1.0f - fValue * fValue)));
}

static Variable atan(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(portable::atan2f(fValue, 1.0f));
}

static Variable log(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    // The natural logarithm; zero for fValue <= 0, as documented.
    return Variable::ofFloat(fValue <= 0.0f ? 0.0f : std::log(fValue));
}

static Variable pow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);
    auto fExponent = getFloat(args, 1);

    // Transform

    // Execute
    // Zero where the result would be undefined (0 to a negative power), as documented.
    if (fValue == 0.0f && fExponent < 0.0f) {
        return Variable::ofFloat(0.0f);
    }
    return Variable::ofFloat(std::pow(fValue, fExponent));
}

static Variable sqrt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fValue = getFloat(args, 0);

    // Transform

    // Execute
    // Zero for a negative argument, as documented, rather than NaN.
    return Variable::ofFloat(fValue < 0.0f ? 0.0f : std::sqrt(fValue));
}

static Variable abs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nValue = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(std::abs(nValue));
}

static Variable GetPlayerRestrictMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    bool restrict = ctx.game.module()->player().isRestrictMode();
    return Variable::ofInt(static_cast<int>(restrict));
}

static Variable GetCasterLevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Execute: KotOR has no separate caster class level; a creature casts at its
    // character level. 0 for anything but a creature.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature ? creature->attributes().getAggregateLevel() : 0);
}

static Variable GetFirstEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofEffect(creature->getFirstEffect());
}

static Variable GetNextEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofEffect(creature->getNextEffect());
}

static Variable RemoveEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto eEffect = getEffect(args, 1);

    // Transform

    // Execute
    if (!oCreature || !eEffect) {
        return Variable::ofNull();
    }
    // A value read back from a save carries the applied instance's id; a
    // live value is the applied payload itself.
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(eEffect);
        saved && saved->instance().hasStableId()) {
        oCreature->removeEffectsById(saved->instance().id);
    } else {
        oCreature->removeEffect(eEffect);
    }
    return Variable::ofNull();
}

static Variable GetIsEffectValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    bool valid;
    try {
        auto eEffect = getEffect(args, 0);
        valid = static_cast<bool>(eEffect);
    } catch (const RoutineArgumentException &ignored) {
        valid = false;
    }
    return Variable::ofInt(static_cast<int>(valid));
}

// The instance an effect value belongs to once applied, if it is applied to
// anything in the current area. An effect value that was never applied is in
// its un-applied form: instant, magical.
static const EffectInstance *findAppliedInstance(const Effect &effect, const RoutineContext &ctx) {
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return nullptr;
    }
    for (const auto &object : area->objects()) {
        for (const EffectInstance &instance : object->effects()) {
            if (instance.effect.get() == &effect) {
                return &instance;
            }
        }
    }
    return nullptr;
}

static Variable GetEffectDurationType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute
    DurationType type = DurationType::Instant;
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(eEffect)) {
        type = saved->instance().durationType();
    } else if (const EffectInstance *instance = findAppliedInstance(*eEffect, ctx)) {
        type = instance->durationType();
    }
    return Variable::ofInt(static_cast<int>(type));
}

static Variable GetEffectSubType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute: SUBTYPE_MAGICAL (8), SUBTYPE_SUPERNATURAL (16) or SUBTYPE_EXTRAORDINARY (24).
    uint16_t subType = eEffect->semanticSubType();
    if (const EffectInstance *instance = findAppliedInstance(*eEffect, ctx)) {
        subType = instance->semanticSubType();
    }
    return Variable::ofInt(subType);
}

static Variable GetEffectCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Transform

    // Execute
    std::shared_ptr<Object> creator;
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(eEffect)) {
        creator = saved->instance().boundCreator();
    } else if (eEffect) {
        creator = eEffect->saveFacingCreator();
    }
    return Variable::ofObject(creator ? creator->id() : script::kObjectInvalid);
}

static Variable IntToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofString(std::to_string(nInteger));
}

struct ObjectsInArea : public EngineType {
    std::vector<uint32_t> objects;
    uint32_t area {script::kObjectInvalid};
    int objectFilter {0};
};

static std::shared_ptr<Area> getAreaOrCurrent(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    auto module = ctx.game.module();
    auto currentArea = module ? module->area() : nullptr;
    if (index < 0 || index >= args.size()) {
        return currentArea;
    }
    if (args[index].type != VariableType::Object) {
        throw RoutineArgumentException("Expected object argument");
    }
    if (args[index].objectId == script::kObjectInvalid) {
        return currentArea;
    }

    return dyn_cast<Area>(getObject(args, index, ctx));
}

static bool matchesObjectFilter(const Object &object, int objectFilter) {
    return static_cast<int>(object.type()) & objectFilter;
}

static Variable GetFirstObjectInArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrCurrent(args, 0, ctx);
    auto nObjectFilter = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    if (!oArea) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    SmallVector<uint32_t, 16> foundObjects;
    for (auto it = oArea->objects().rbegin(), end = oArea->objects().rend(); it != end; ++it) {
        const std::shared_ptr<Object> &object = *it;
        if (!matchesObjectFilter(*object, nObjectFilter)) {
            continue;
        }
        foundObjects.push_back(object->id());
    }

    if (foundObjects.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    uint32_t firstObject = foundObjects.back();
    foundObjects.resize(foundObjects.size() - 1);
    if (foundObjects.empty()) {
        return Variable::ofObject(firstObject);
    }

    auto objectsArg = std::make_shared<ObjectsInArea>();
    objectsArg->objects.reserve(foundObjects.size());
    for (uint32_t object : foundObjects) {
        objectsArg->objects.push_back(object);
    }
    objectsArg->area = oArea->id();
    objectsArg->objectFilter = nObjectFilter;

    ctx.execution.args.push_back(
        Argument(ArgKind::ObjectsInArea, Variable::ofCustom(objectsArg)));

    return Variable::ofObject(firstObject);
}

static Variable GetNextObjectInArea(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getAreaOrCurrent(args, 0, ctx);
    auto nObjectFilter = getIntOrElse(args, 1, 1);

    // Transform

    // Execute
    if (!oArea) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    auto &scriptArgs = ctx.execution.args;
    for (auto it = scriptArgs.rbegin(), end = scriptArgs.rend(); it != end; ++it) {
        if (it->kind != ArgKind::ObjectsInArea) {
            continue;
        }
        assert(it->var.type == VariableType::Custom);

        auto objects = std::static_pointer_cast<ObjectsInArea>(it->var.engineType);
        if (objects->area != oArea->id() || objects->objectFilter != nObjectFilter) {
            continue;
        }

        if (objects->objects.empty()) {
            return Variable::ofObject(script::kObjectInvalid);
        }

        uint32_t nextObject = objects->objects.back();
        objects->objects.pop_back();
        return Variable::ofObject(nextObject);
    }

    return Variable::ofObject(script::kObjectInvalid);
}

static Variable d2(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 2);
    }
    return Variable::ofInt(total);
}

static Variable d3(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 3);
    }
    return Variable::ofInt(total);
}

static Variable d4(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 4);
    }
    return Variable::ofInt(total);
}

static Variable d6(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 6);
    }
    return Variable::ofInt(total);
}

static Variable d8(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 8);
    }
    return Variable::ofInt(total);
}

static Variable d10(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 10);
    }
    return Variable::ofInt(total);
}

static Variable d12(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 12);
    }
    return Variable::ofInt(total);
}

static Variable d20(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 20);
    }
    return Variable::ofInt(total);
}

static Variable d100(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumDice = getIntOrElse(args, 0, 1);

    // Transform
    auto numDice = std::max(1, nNumDice);

    // Execute
    int total = 0;
    for (int i = 0; i < numDice; ++i) {
        total += reone::randomInt(1, 100);
    }
    return Variable::ofInt(total);
}

static Variable VectorMagnitude(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(glm::length(vVector));
}

static Variable GetMetaMagicFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: KotOR has no metamagic, so no cast ever carries a metamagic feat
    // (METAMAGIC_NONE).
    return Variable::ofInt(0);
}

static Variable GetObjectType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oTarget->type()));
}

static Variable GetRacialType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->racialType()));
}

static Variable FortitudeSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nDC = getInt(args, 1);
    auto nSaveType = getIntOrElse(args, 2, 0);
    auto oSaveVersus = getObjectOrCaller(args, 3, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto saveType = static_cast<SavingThrowType>(nSaveType);

    // Execute: 1 if the save succeeds, 0 if it fails. Immunity to the save
    // type (return value 2) is not modelled yet.
    return Variable::ofInt(creature->rollSavingThrowVersus(1, nDC, saveType, asCreature(oSaveVersus).get()) ? 1 : 0);
}

static Variable ReflexSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nDC = getInt(args, 1);
    auto nSaveType = getIntOrElse(args, 2, 0);
    auto oSaveVersus = getObjectOrCaller(args, 3, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto saveType = static_cast<SavingThrowType>(nSaveType);

    // Execute: 1 if the save succeeds, 0 if it fails. Immunity to the save
    // type (return value 2) is not modelled yet.
    return Variable::ofInt(creature->rollSavingThrowVersus(2, nDC, saveType, asCreature(oSaveVersus).get()) ? 1 : 0);
}

static Variable WillSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nDC = getInt(args, 1);
    auto nSaveType = getIntOrElse(args, 2, 0);
    auto oSaveVersus = getObjectOrCaller(args, 3, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto saveType = static_cast<SavingThrowType>(nSaveType);

    // Execute: 1 if the save succeeds, 0 if it fails. Immunity to the save
    // type (return value 2) is not modelled yet.
    return Variable::ofInt(creature->rollSavingThrowVersus(3, nDC, saveType, asCreature(oSaveVersus).get()) ? 1 : 0);
}

// K2 behaviour (design note k2_merge): the caster's save DC for the power it
// is casting: 5 + its character level + its Wisdom and Charisma modifiers,
// +5 for the Force Scream powers, +2/+3/+4 for Force Focus / Advanced /
// Mastery. Outside a cast, 14.
static Variable GetSpellSaveDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    static constexpr int kNoCastDC = 14;
    auto caller = asCreature(ctx.execution.findArg(ArgKind::Caller)
                                 ? ctx.game.getObjectById(ctx.execution.findArg(ArgKind::Caller)->objectId)
                                 : nullptr);
    auto spell = ctx.execution.findArg(ArgKind::SpellId);
    if (!caller || !spell) {
        return Variable::ofInt(kNoCastDC);
    }
    const auto &attributes = caller->attributes();
    int dc = 5 + attributes.getAggregateLevel() +
             caller->getEffectiveAbilityModifier(Ability::Wisdom) +
             caller->getEffectiveAbilityModifier(Ability::Charisma);
    int spellId = spell->intValue;
    if (spellId >= 159 && spellId <= 161) {
        dc += 5;
    }
    if (caller->hasEffectiveFeat(static_cast<FeatType>(90))) {
        dc += 4;
    } else if (caller->hasEffectiveFeat(static_cast<FeatType>(89))) {
        dc += 3;
    } else if (caller->hasEffectiveFeat(static_cast<FeatType>(88))) {
        dc += 2;
    }
    return Variable::ofInt(dc);
}

static Variable MagicalEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute: the value is marked in place and handed back, so every copy of
    // the script variable sees the new subtype.
    eEffect->setSemanticSubType(8);
    return Variable::ofEffect(eEffect);
}

static Variable SupernaturalEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute: the value is marked in place and handed back, so every copy of
    // the script variable sees the new subtype.
    eEffect->setSemanticSubType(16);
    return Variable::ofEffect(eEffect);
}

static Variable ExtraordinaryEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute: the value is marked in place and handed back, so every copy of
    // the script variable sees the new subtype.
    eEffect->setSemanticSubType(24);
    return Variable::ofEffect(eEffect);
}

static Variable GetAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nForFutureUse = getIntOrElse(args, 1, 0);
    (void)nForFutureUse;

    // Execute: the creature's armour class (its defense), 0 for anything else.
    auto creature = asCreature(oObject);
    return Variable::ofInt(creature ? creature->getDefense() : 0);
}

static Variable RoundsToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nRounds = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(nRounds / 6.0f);
}

static Variable HoursToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nHours = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(nHours * 3600);
}

static Variable TurnsToSeconds(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nTurns = getInt(args, 0);

    // Transform

    // Execute
    // A turn is always 60 seconds, as a round is always 6.
    return Variable::ofFloat(nTurns * 60.0f);
}

static Variable SoundObjectSetFixedVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fFixedVariance = getFloat(args, 1);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    if (sound) {
        sound->setFixedVariance(fFixedVariance);
    }
    return Variable::ofNull();
}

static Variable GetGoodEvilValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature ? creature->goodEvil() : -1);
}

static Variable GetPartyMemberCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().getSize());
}

static Variable GetAlignmentGoodEvil(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Execute (K2 behaviour, design note k2_merge): the band of the
    // creature's GoodEvil: below 41 ALIGNMENT_DARK_SIDE, 60 and above
    // ALIGNMENT_LIGHT_SIDE, otherwise ALIGNMENT_NEUTRAL; -1 for anything
    // that is not a creature.
    auto creature = asCreature(oCreature);
    if (!creature) {
        return Variable::ofInt(-1);
    }
    return Variable::ofInt(prestige::alignmentConstant(prestige::alignmentBand(creature->goodEvil())));
}

// Get{First,Next}ObjectInShape. Behaviour notes: design note c09_ai_combat,
// "Objects in a shape". The matching rules live in game/shapequery.h so the
// tests can pin them without a scene.

// The iteration a GetFirstObjectInShape started: every area object in
// ascending x (ties by id), and the index of the object last returned. Next
// tests the objects after it against its own arguments. There is one per
// script: a nested First (a helper scanning around each candidate) restarts
// it, and the outer Next then carries on after the inner scan's last result.
struct ObjectsInShape : public EngineType {
    std::vector<uint32_t> objects;
    size_t next {0}; // first index Next looks at: one past the last result
};

static bool checkShapeLineOfSight(const glm::vec3 &from, const Object &object, ISceneGraph &sceneGraph) {
    const float kLineOfSightHeight = 1.7f;
    glm::vec3 origin(from);
    origin.z += kLineOfSightHeight;
    glm::vec3 dest(object.position());
    dest.z += kLineOfSightHeight;
    if (glm::distance2(origin, dest) < 1e-6f) {
        return true;
    }
    Collision collision;
    if (sceneGraph.testLineOfSight(origin, dest, collision)) {
        return collision.user == &object ||
               glm::distance2(origin, dest) <= glm::distance2(origin, collision.intersection);
    }
    return true;
}

static ShapeQuery makeShapeQuery(const std::vector<Variable> &args, const RoutineContext &ctx) {
    ShapeQuery query;
    query.shape = getInt(args, 0);
    query.size = getFloat(args, 1);
    auto lTarget = getLocationArgument(args, 2);
    query.lineOfSight = getIntOrElse(args, 3, 0) != 0;
    query.objectFilter = getIntOrElse(args, 4, static_cast<int>(ObjectType::Creature));
    query.origin = getVectorOrElse(args, 5, glm::vec3(0.0f, 0.0f, 0.0f));
    query.target = lTarget ? lTarget->position() : glm::vec3(0.0f);
    // The spell shapes start at the caster, whatever vOrigin says.
    if (query.shape == static_cast<int>(Shape::SpellCylinder) ||
        query.shape == static_cast<int>(Shape::SpellCone)) {
        const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
        if (callerArg) {
            if (auto caller = ctx.game.getObjectById(callerArg->objectId)) {
                query.origin = caller->position();
            }
        }
    }
    prepareShapeQuery(query);
    return query;
}

// The first object at or after cursor that the query accepts; the cursor is
// left on it (or at the end).
static uint32_t nextObjectInShape(ObjectsInShape &state, const ShapeQuery &query, Area &area, const RoutineContext &ctx) {
    // Past the search range the scan stops without moving the cursor.
    for (size_t index = state.next; index < state.objects.size(); ++index) {
        auto object = ctx.game.getObjectById(state.objects[index]);
        if (!object) {
            continue;
        }
        const glm::vec3 &position = object->position();
        if (position.x > query.maxX) {
            break;
        }
        if (position.x < query.minX) {
            continue;
        }
        if (!shapeFilterAccepts(query.objectFilter, static_cast<int>(object->type()))) {
            continue;
        }
        if (!shapeContains(query, position)) {
            continue;
        }
        if (query.lineOfSight &&
            !checkShapeLineOfSight(shapeLineOfSightOrigin(query), *object, area.graph())) {
            continue;
        }
        state.next = index + 1;
        return object->id();
    }
    return script::kObjectInvalid;
}

static Variable
GetFirstObjectInShape(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto query = makeShapeQuery(args, ctx);

    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    auto state = std::make_shared<ObjectsInShape>();
    std::vector<std::pair<float, uint32_t>> ordered;
    ordered.reserve(area->objects().size());
    for (const std::shared_ptr<Object> &object : area->objects()) {
        ordered.emplace_back(object->position().x, object->id());
    }
    std::sort(ordered.begin(), ordered.end());
    state->objects.reserve(ordered.size());
    for (const auto &entry : ordered) {
        state->objects.push_back(entry.second);
    }
    uint32_t first = nextObjectInShape(*state, query, *area, ctx);

    // Replace any earlier iteration: there is one per script, as the area
    // has one in the original.
    auto &scriptArgs = ctx.execution.args;
    scriptArgs.erase(
        std::remove_if(scriptArgs.begin(), scriptArgs.end(), [](const Argument &arg) {
            return arg.kind == ArgKind::ObjectsInShape;
        }),
        scriptArgs.end());
    scriptArgs.push_back(Argument(ArgKind::ObjectsInShape, Variable::ofCustom(state)));

    return Variable::ofObject(first);
}

static Variable GetNextObjectInShape(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto query = makeShapeQuery(args, ctx);

    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    for (auto &arg : ctx.execution.args) {
        if (arg.kind != ArgKind::ObjectsInShape) {
            continue;
        }
        auto state = std::static_pointer_cast<ObjectsInShape>(arg.var.engineType);
        return Variable::ofObject(nextObjectInShape(*state, query, *area, ctx));
    }
    // No previous GetFirstObjectInShape.
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable SignalEvent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto evToRun = getEvent(args, 1);

    // Execute
    // K2 behaviour: the event is queued, due now, and runs on the next drain
    // of the script event queue, after the signalling script has finished.
    debug(str(boost::format("Event signalled: %s %s") % oObject->tag() % evToRun->number()), LogChannel::Script);
    uint32_t caller = kObjectInvalid;
    if (const Variable *self = ctx.execution.findArg(ArgKind::Caller)) {
        if (self->objectId != kObjectSelf && ctx.game.getObjectById(self->objectId)) {
            caller = self->objectId;
        }
    }
    ctx.game.scriptEvents().signal(oObject->id(), caller, evToRun);

    return Variable::ofNull();
}

static Variable EventUserDefined(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nUserDefinedEventNumber = getInt(args, 0);

    // Transform

    // Execute
    auto event = ctx.game.newEvent(nUserDefinedEventNumber);
    return Variable::ofEvent(std::move(event));
}

static Variable VectorNormalize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    return Variable::ofVector(glm::normalize(vVector));
}

static Variable GetItemStackSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    return Variable::ofInt(item->stackSize());
}

static Variable GetAbilityScore(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nAbilityType = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);
    auto ability = static_cast<Ability>(nAbilityType);

    // Execute
    return Variable::ofInt(creature->getEffectiveAbilityScore(ability));
}

static Variable GetIsDead(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->isDead()));
}

static Variable PrintVector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);
    auto bPrepend = getInt(args, 1);

    // Transform

    // Execute
    char text[160];
    std::snprintf(text, sizeof(text), "%s(%f, %f, %f)", bPrepend ? "PRINTVECTOR:" : "", vVector.x, vVector.y, vVector.z);
    info(text);
    return Variable::ofNull();
}

static Variable Vector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto x = getFloatOrElse(args, 0, 0.0f);
    auto y = getFloatOrElse(args, 1, 0.0f);
    auto z = getFloatOrElse(args, 2, 0.0f);

    // Transform

    // Execute
    return Variable::ofVector(glm::vec3(x, y, z));
}

static Variable SetFacingPoint(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vTarget = getVector(args, 0);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    caller->face(vTarget);
    return Variable::ofNull();
}

static Variable AngleToVector(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fAngle = getFloat(args, 0);

    // Transform

    // Execute
    auto vector = glm::vec3(portable::cosf(fAngle), portable::sinf(fAngle), 0.0f);
    return Variable::ofVector(std::move(vector));
}

static Variable VectorToAngle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vVector = getVector(args, 0);

    // Transform

    // Execute
    // Degrees in [0, 360), counter-clockwise from the +X axis (east), the
    // inverse of the facing GetFacing reports.
    float angle = glm::degrees(portable::atan2f(vVector.y, vVector.x));
    if (angle < 0.0f) {
        angle += 360.0f;
    }
    return Variable::ofFloat(angle);
}

// A touch attack roll: d20 plus the attacker's attack bonus against the target's
// armour class without its body armour. A natural 1 misses and a natural 20
// hits; a hit on a natural 20 confirmed by a second roll is critical (2).
// Returns 0 miss, 1 hit, 2 critical hit. It does not use up the attacker's round.
static int touchAttack(const Creature &attacker, const Object &target, bool ranged) {
    (void)ranged;
    int defense = 10;
    if (auto *creature = dyn_cast<Creature>(&target)) {
        defense = creature->getDefense();
        if (auto armor = creature->getEquippedItem(InventorySlots::body)) {
            defense -= armor->baseDefense();
        }
    }
    int roll = randomInt(1, 20);
    int bonus = attacker.getAttackBonus();
    if (roll == 1) {
        return 0;
    }
    if (roll == 20) {
        return randomInt(1, 20) + bonus >= defense ? 2 : 1;
    }
    return roll + bonus >= defense ? 1 : 0;
}

static Variable TouchAttackMelee(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto bDisplayFeedback = getIntOrElse(args, 1, 1);
    (void)bDisplayFeedback;

    // Execute: 0 miss, 1 hit, 2 critical hit; 0 when the caller or target is missing.
    auto attacker = asCreature(callerOrNull(ctx));
    return Variable::ofInt(attacker && oTarget ? touchAttack(*attacker, *oTarget, false) : 0);
}

static Variable TouchAttackRanged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto bDisplayFeedback = getIntOrElse(args, 1, 1);
    (void)bDisplayFeedback;

    // Execute: 0 miss, 1 hit, 2 critical hit; 0 when the caller or target is missing.
    auto attacker = asCreature(callerOrNull(ctx));
    return Variable::ofInt(attacker && oTarget ? touchAttack(*attacker, *oTarget, true) : 0);
}

static Variable SetItemStackSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto nStackSize = getInt(args, 1);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    item->setStackSize(nStackSize);
    return Variable::ofNull();
}

static Variable GetDistanceBetween(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectA = getObject(args, 0, ctx);
    auto oObjectB = getObject(args, 1, ctx);

    // Transform

    // Execute
    return Variable::ofFloat(oObjectA->getDistanceTo(*oObjectB));
}

static Variable SetReturnStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bShow = getInt(args, 0);
    auto srStringRef = getIntOrElse(args, 1, 0);
    auto srReturnQueryStrRef = getIntOrElse(args, 2, 0);

    // Transform

    // Execute
    if (auto module = ctx.game.module()) {
        Module::ReturnOption option;
        option.show = bShow != 0;
        option.strref = srStringRef;
        option.queryStrref = srReturnQueryStrRef;
        module->setReturnOption(option);
    }
    return Variable::ofNull();
}

static Variable GetItemInSlot(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInventorySlot = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    auto item = creature->getEquippedItem(nInventorySlot);
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable SetGlobalString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto sValue = getString(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalString(sIdentifier, sValue);
    return Variable::ofNull();
}

static Variable SetCommandable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bCommandable = getInt(args, 0);
    auto oTarget = getObjectOrCaller(args, 1, ctx);

    // Transform
    bool commandable = static_cast<bool>(bCommandable);

    // Execute
    oTarget->setCommandable(commandable);
    return Variable::ofNull();
}

static Variable GetCommandable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oTarget->isCommandable()));
}

static Variable GetHitDice(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->attributes().getAggregateLevel());
}

static Variable GetTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(oObject->tag());
}

static Variable ResistForce(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSource = getObjectOrNull(args, 0, ctx);
    auto oTarget = getObjectOrNull(args, 1, ctx);

    // Transform
    auto source = oSource ? dyn_cast<Creature>(oSource.get()) : nullptr;
    auto target = oTarget ? dyn_cast<Creature>(oTarget.get()) : nullptr;
    if (!source || !target) {
        return Variable::ofInt(0);
    }

    // Execute: the source rolls d20 + its level against the target's Force
    // resistance; the power is resisted (TRUE) when the roll falls short.
    // No resistance, no roll (design note c08b_effects).
    int resistance = target->getForceResistance();
    if (resistance <= 0) {
        return Variable::ofInt(0);
    }
    int roll = randomInt(1, 20) + source->attributes().getAggregateLevel();
    return Variable::ofInt(roll < resistance ? 1 : 0);
}

static Variable GetEffectType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(eEffect->type()));
}

static Variable GetFactionEqual(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFirstObject = getObject(args, 0, ctx);
    auto oSecondObject = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto firstObject = checkCreature(oFirstObject);
    auto secondObject = checkCreature(oSecondObject);

    // Execute
    return Variable::ofInt(static_cast<int>(firstObject->faction() == secondObject->faction()));
}

static Variable ChangeFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectToChangeFaction = getObject(args, 0, ctx);
    auto oMemberOfFactionToJoin = getObject(args, 1, ctx);

    // Transform
    auto target = checkCreature(oObjectToChangeFaction);
    auto source = checkCreature(oMemberOfFactionToJoin);

    // Execute
    target->setFaction(source->faction());
    return Variable::ofNull();
}

static Variable GetIsListening(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oObject);

    // Execute
    return Variable::ofInt(creature->isListening());
}

static Variable SetListening(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto bValue = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oObject);

    // Execute
    creature->setIsListening(bValue);
    return Variable::ofNull();
}

static Variable SetListenPattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto sPattern = getString(args, 1);
    auto nNumber = getIntOrElse(args, 2, 0);

    // Transform
    auto creature = checkCreature(oObject);

    // Execute
    ctx.game.module()->area()->messageBus().addListener(creature, sPattern, nNumber);
    return Variable::ofNull();
}

// The substrings of the latest TestStringAgainstPattern, for GetMatchedSubstring.
static std::vector<std::string> g_matchedSubstrings;

static Variable TestStringAgainstPattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sPattern = getString(args, 0);
    auto sStringToTest = getString(args, 1);

    // Execute
    auto result = matchPattern(sPattern, sStringToTest);
    g_matchedSubstrings = result.matched ? std::move(result.substrings) : std::vector<std::string> {};
    return Variable::ofInt(result.matched ? 1 : 0);
}

static Variable GetMatchedSubstring(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nString = getInt(args, 0);

    // Execute: "" for a number the latest match has no substring for.
    bool valid = nString >= 0 && nString < static_cast<int>(g_matchedSubstrings.size());
    return Variable::ofString(valid ? g_matchedSubstrings[nString] : std::string());
}

static Variable GetMatchedSubstringsCount(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(g_matchedSubstrings.size()));
}

static std::optional<Faction> getObjectFaction(const Object &object);
static std::vector<std::shared_ptr<Creature>> collectFactionMembers(Faction faction, bool pcOnly, const RoutineContext &ctx);

// Picks the faction member that ranks first by `better` (strict, so ties keep
// the earlier member in area order). With bMustBeVisible the members must be
// visible to the calling creature.
static Variable pickFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx,
                                  const std::function<bool(const Creature &, const Creature &)> &better) {
    // Load
    auto oFactionMember = getObjectOrNull(args, 0, ctx);
    auto bMustBeVisible = getIntOrElse(args, 1, 1);

    // Transform
    auto faction = oFactionMember ? getObjectFaction(*oFactionMember) : std::nullopt;
    if (!faction) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    auto caller = asCreature(callerOrNull(ctx));

    // Execute
    std::shared_ptr<Creature> best;
    for (const auto &member : collectFactionMembers(*faction, false, ctx)) {
        if (bMustBeVisible && caller && member != caller && !caller->perception().sees(member->id())) {
            continue;
        }
        if (!best || better(*member, *best)) {
            best = member;
        }
    }
    return Variable::ofObject(best ? best->id() : script::kObjectInvalid);
}

static int damageTaken(const Creature &c) {
    return c.maxHitPoints() - c.currentHitPoints();
}

static Variable GetFactionWeakestMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return a.challengeRating() < b.challengeRating(); });
}

static Variable GetFactionStrongestMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return a.challengeRating() > b.challengeRating(); });
}

static Variable GetFactionMostDamagedMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return damageTaken(a) > damageTaken(b); });
}

static Variable GetFactionLeastDamagedMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return damageTaken(a) < damageTaken(b); });
}

static Variable GetFactionGold(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFactionMember = getObjectOrNull(args, 0, ctx);

    // Transform
    auto faction = oFactionMember ? getObjectFaction(*oFactionMember) : std::nullopt;

    // Execute: the party's shared credits for the player's faction; creatures
    // of other factions carry no gold of their own.
    return Variable::ofInt(faction && *faction == Faction::Player ? ctx.game.party().gold() : 0);
}

static Variable GetFactionAverageReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSourceFactionMember = getObjectOrNull(args, 0, ctx);
    auto oTarget = getObjectOrNull(args, 1, ctx);

    // Transform
    auto source = asCreature(oSourceFactionMember);
    if (!source) {
        return Variable::ofInt(-1);
    }
    auto target = asCreature(oTarget);
    if (!target) {
        return Variable::ofInt(0);
    }

    // Execute: the members' average regard for the target. Our factions carry
    // no personal reputation, so every member regards it as the faction does.
    int reputation = ctx.services.game.reputes.getReputation(source->faction(), target->faction());
    return Variable::ofInt(std::clamp(reputation, 0, 100));
}

static std::optional<Faction> getObjectFaction(const Object &object);
static std::vector<std::shared_ptr<Creature>> collectFactionMembers(Faction faction, bool pcOnly, const RoutineContext &ctx);

static Variable factionAverage(const std::vector<Variable> &args, const RoutineContext &ctx, int (*value)(const Creature &)) {
    // Load
    auto oFactionMember = getObjectOrNull(args, 0, ctx);

    // Transform
    auto faction = oFactionMember ? getObjectFaction(*oFactionMember) : std::nullopt;
    if (!faction) {
        return Variable::ofInt(-1);
    }

    // Execute: the whole-number average over the faction's members, -1 for a
    // faction without any.
    auto members = collectFactionMembers(*faction, false, ctx);
    if (members.empty()) {
        return Variable::ofInt(-1);
    }
    int total = 0;
    for (const auto &member : members) {
        total += value(*member);
    }
    return Variable::ofInt(total / static_cast<int>(members.size()));
}

static Variable GetFactionAverageGoodEvilAlignment(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return factionAverage(args, ctx, [](const Creature &c) { return c.goodEvil(); });
}

static Variable SoundObjectGetFixedVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    // A non-sound object answers 0.
    return Variable::ofFloat(sound ? sound->fixedVariance() : 0);
}

static Variable GetFactionAverageLevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return factionAverage(args, ctx, [](const Creature &c) { return c.attributes().getAggregateLevel(); });
}

static Variable GetFactionAverageXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return factionAverage(args, ctx, [](const Creature &c) { return c.xp(); });
}

static Variable GetFactionMostFrequentClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFactionMember = getObjectOrNull(args, 0, ctx);

    // Transform
    auto faction = oFactionMember ? getObjectFaction(*oFactionMember) : std::nullopt;
    if (!faction) {
        return Variable::ofInt(-1);
    }

    // Execute: each member counts for its current (latest) class; ties go to
    // the lower class number, -1 for a faction without members.
    std::map<int, int> counts;
    for (const auto &member : collectFactionMembers(*faction, false, ctx)) {
        ++counts[static_cast<int>(member->attributes().getEffectiveClass())];
    }
    int best = -1;
    int bestCount = 0;
    for (const auto &[clazz, count] : counts) {
        if (count > bestCount) {
            best = clazz;
            bestCount = count;
        }
    }
    return Variable::ofInt(best);
}

static Variable GetFactionWorstAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return a.getDefense() < b.getDefense(); });
}

static Variable GetFactionBestAC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return pickFactionMember(args, ctx, [](const Creature &a, const Creature &b) { return a.getDefense() > b.getDefense(); });
}

static Variable GetGlobalString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofString(ctx.game.getGlobalString(sIdentifier));
}

static Variable GetListenPatternNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *number = ctx.execution.findArg(ArgKind::ListenPatternNumber)) {
        return *number;
    }

    return Variable::ofInt(-1);
}

static Variable GetWaypointByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sWaypointTag = getString(args, 0);

    // Transform
    auto waypointTag = boost::to_lower_copy(sWaypointTag);

    // Execute
    std::shared_ptr<Object> waypoint;
    for (auto &object : ctx.game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        if (object->tag() == waypointTag) {
            waypoint = object;
            break;
        }
    }
    return Variable::ofObject(getObjectIdOrInvalid(waypoint));
}

static Variable GetTransitionTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTransition = getObjectOrNull(args, 0, ctx);

    // Execute: the waypoint a door or trigger leads to, named by its LinkedTo
    // tag. A transition into another module has no target in this area.
    std::string tag;
    if (auto door = oTransition ? dyn_cast<Door>(oTransition) : nullptr) {
        if (door->linkedToModule().empty()) {
            tag = door->linkedTo();
        }
    } else if (auto trigger = oTransition ? dyn_cast<Trigger>(oTransition) : nullptr) {
        if (trigger->linkedToModule().empty()) {
            tag = trigger->linkedTo();
        }
    }
    if (tag.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    tag = boost::to_lower_copy(tag);
    for (auto &object : ctx.game.module()->area()->getObjectsByType(ObjectType::Waypoint)) {
        if (object->tag() == tag) {
            return Variable::ofObject(object->id());
        }
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetObjectByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTag = getString(args, 0);
    auto nNth = getIntOrElse(args, 1, 0);

    // Transform
    auto tag = boost::to_lower_copy(sTag);

    // Execute
    std::shared_ptr<Object> object;
    if (!tag.empty()) {
        object = ctx.game.module()->area()->getObjectByTag(tag, nNth);
    } else {
        object = ctx.game.party().player();
    }
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable AdjustAlignment(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSubject = getObjectOrNull(args, 0, ctx);
    auto nAlignment = getInt(args, 1);
    auto nShift = getInt(args, 2);
    // TSL appends bDontModifyNPCs; it governs TSL companion influence, which
    // K1 content has none of, so it defaults and changes nothing here.
    auto bDontModifyNPCs = getIntOrElse(args, 3, 0);
    (void)bDontModifyNPCs;

    // Transform
    auto creature = asCreature(oSubject);
    if (!creature) {
        return Variable::ofNull();
    }

    // Execute (design note c03_stats, "AdjustAlignment")
    int delta = 0;
    switch (static_cast<Alignment>(nAlignment)) {
    case Alignment::LightSide:
        delta = nShift;
        break;
    case Alignment::DarkSide:
        delta = -nShift;
        break;
    case Alignment::Neutral: {
        // Towards 50 by nShift, and not beyond it.
        int value = creature->goodEvil();
        if (value <= 50) {
            delta = std::min(nShift, 50 - value);
        } else {
            delta = -std::min(nShift, value - 50);
        }
        break;
    }
    default:
        break;
    }
    creature->adjustGoodEvil(delta);
    // The player's shifts feed the status summary and the HUD flash
    // (design note p2_ui, "Alignment feedback").
    if (delta != 0 && creature == ctx.game.party().player()) {
        ctx.game.submitAlignmentShift(delta);
    }
    return Variable::ofNull();
}

static Variable SetAreaTransitionBMP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto nPredefinedAreaTransition = getInt(args, 0);
    auto sCustomAreaTransitionBMP = getStringOrElse(args, 1, "");
    (void)nPredefinedAreaTransition;
    (void)sCustomAreaTransitionBMP;

    // Execute
    // intentionally stubbed: the loading-screen picture comes from module data; the engine models no per-transition override
    return Variable::ofNull();
}

// The faction member naming a reputation source need not be a creature --
// modules commonly place an inert object purely to stand in for its faction.
static std::optional<Faction> getObjectFaction(const Object &object) {
    switch (object.type()) {
    case ObjectType::Creature:
        return static_cast<const Creature &>(object).faction();
    case ObjectType::Door:
        return static_cast<const Door &>(object).faction();
    case ObjectType::Placeable:
        return static_cast<const Placeable &>(object).faction();
    default:
        return std::nullopt;
    }
}

// Faction members are the creatures of the current area that share a faction.
// Dead creatures count; pcOnly narrows them to the player and the party.
struct FactionMembers : public EngineType {
    std::vector<uint32_t> objects; // still to hand out, next one last
    Faction faction {Faction::Invalid};
    bool pcOnly {false};
};

static std::vector<std::shared_ptr<Creature>> collectFactionMembers(Faction faction, bool pcOnly, const RoutineContext &ctx) {
    std::vector<std::shared_ptr<Creature>> members;
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return members;
    }
    for (const auto &object : area->objects()) {
        auto creature = dyn_cast<Creature>(object);
        if (!creature || creature->faction() != faction) {
            continue;
        }
        if (pcOnly && creature != ctx.game.party().player() && !ctx.game.party().isMember(*creature)) {
            continue;
        }
        members.push_back(std::move(creature));
    }
    return members;
}

static Variable GetReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSource = getObjectOrNull(args, 0, ctx);
    auto oTarget = getObjectOrNull(args, 1, ctx);

    // Transform
    auto sourceFaction = oSource ? getObjectFaction(*oSource) : std::nullopt;
    if (!sourceFaction) {
        return Variable::ofInt(-1);
    }
    auto target = asCreature(oTarget);
    if (!target) {
        return Variable::ofInt(0);
    }

    // Execute: how the source's faction regards the target's, 0 to 100.
    int reputation = ctx.services.game.reputes.getReputation(*sourceFaction, target->faction());
    return Variable::ofInt(std::clamp(reputation, 0, 100));
}

static Variable AdjustReputation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto oSourceFactionMember = getObject(args, 1, ctx);
    auto nAdjustment = getInt(args, 2);

    // Transform
    auto target = dyn_cast<Creature>(oTarget);
    if (!target) {
        return Variable::ofNull();
    }
    auto sourceFaction = getObjectFaction(*oSourceFactionMember);
    if (!sourceFaction) {
        return Variable::ofNull();
    }

    // Execute
    ctx.services.game.reputes.adjustReputation(*sourceFaction, target->faction(), nAdjustment);
    return Variable::ofNull();
}

static Variable GetModuleFileName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The resref of the module being played (design note c11_data).
    auto module = ctx.game.module();
    return Variable::ofString(module ? module->name() : std::string());
}

static Variable GetGoingToBeAttackedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: the first living creature of the area (in area order) that is
    // about to attack the target, i.e. whose attempted attack target it is.
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (oTarget && area) {
        for (const auto &object : area->objects()) {
            auto creature = dyn_cast<Creature>(object);
            if (creature && !creature->isDead() && creature->id() != oTarget->id() &&
                creature->getAttemptedAttackTarget() == oTarget->id()) {
                return Variable::ofObject(creature->id());
            }
        }
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofLocation(ctx.game.newLocation(oObject->position(), oObject->getFacing()));
}

static Variable CreateLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPosition = getVector(args, 0);
    auto fOrientation = getFloat(args, 1);

    // Transform
    auto orientation = glm::radians(fOrientation);

    // Execute
    return Variable::ofLocation(ctx.game.newLocation(std::move(vPosition), orientation));
}

static Variable ApplyEffectAtLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDurationType = getInt(args, 0);
    auto eEffect = getEffect(args, 1);
    auto lLocation = getLocationArgument(args, 2);
    auto fDuration = getFloatOrElse(args, 3, 0.0f);

    // Transform
    auto duration = static_cast<DurationType>(nDurationType);

    // Only visual effects have a location.
    if (eEffect->type() != EffectType::Visual) {
        return Variable::ofNull();
    }
    auto effect = std::static_pointer_cast<VisualEffect>(eEffect);
    effect->setLocation(lLocation->position());

    // Visual effects without duration make no sense - adjust duration and type
    // to at least match the effect.
    fDuration = std::max(fDuration, effect->duration());
    if (duration == DurationType::Instant && fDuration > 0.0f) {
        duration = DurationType::Temporary;
    }

    // Execute
    std::shared_ptr<Area> area = ctx.game.module()->area();
    area->applyEffect(effect, duration, fDuration);

    return Variable::ofNull();
}

static Variable GetIsPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute. K1: the player-controlled character is whoever leads the
    // party, so a companion the player has switched to counts too. Retail
    // trigger scripts depend on it: tar02_openbase (k_ptar_openbase) opens
    // the Sith base for GetIsPC(entering) with tag "t3m4", and
    // k_ptar_sthchk_en tests GetIsPC with T3-M4 in the party. The player
    // character stays a PC while a companion leads.
    auto &party = ctx.game.party();
    bool pc = creature && (creature == party.player() || creature == party.getLeader());
    return Variable::ofInt(static_cast<int>(pc));
}

static Variable FeetToMeters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFeet = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(fFeet * 0.3048f);
}

static Variable YardsToMeters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fYards = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(fYards * 0.9144f);
}

static Variable ApplyEffectToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDurationType = getInt(args, 0);
    auto eEffect = getEffect(args, 1);
    auto oTarget = getObject(args, 2, ctx);
    auto fDuration = getFloatOrElse(args, 3, 0.0f);

    // Transform
    auto durationType = static_cast<DurationType>(nDurationType);

    // Execute
    // A resisted or fizzled power marks the caster's last Force power as
    // unsuccessful (GetWasForcePowerSuccessful).
    if (eEffect->type() == EffectType::ForceResisted || eEffect->type() == EffectType::ForceFizzle) {
        if (auto caster = asCreature(getCaller(ctx))) {
            caster->setLastForcePowerFailed();
        }
    }
    oTarget->applyEffect(eEffect, durationType, fDuration);
    return Variable::ofNull();
}

static Variable SpeakString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sStringToSpeak = getString(args, 0);
    auto nTalkVolume = getIntOrElse(args, 1, 0);

    // Transform
    auto object = getCaller(ctx);

    if (nTalkVolume < 0 || nTalkVolume > (int32_t)TalkVolume::Last) {
        throw RoutineArgumentException(str(boost::format("Invalid talk volume: %d") % nTalkVolume));
    }

    auto volume = static_cast<TalkVolume>(nTalkVolume);

    // Execute
    ctx.game.module()->area()->messageBus().addMessage(object->id(), sStringToSpeak, volume);
    return Variable::ofNull();
}

static Variable GetSpellTargetLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *location = ctx.execution.findArg(ArgKind::SpellLocation)) {
        return *location;
    }
    return Variable::ofLocation(nullptr);
}

static Variable GetPositionFromLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 0);

    // Transform

    // Execute
    return Variable::ofVector(lLocation->position());
}

static Variable GetFacingFromLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocation = getLocationArgument(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(glm::degrees(lLocation->facing()));
}

static Variable GetNearestCreatureToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFirstCriteriaType = getInt(args, 0);
    auto nFirstCriteriaValue = getInt(args, 1);
    auto lLocation = getLocationArgument(args, 2);
    auto nNth = getIntOrElse(args, 3, 1);
    auto nSecondCriteriaType = getIntOrElse(args, 4, -1);
    auto nSecondCriteriaValue = getIntOrElse(args, 5, -1);
    auto nThirdCriteriaType = getIntOrElse(args, 6, -1);
    auto nThirdCriteriaValue = getIntOrElse(args, 7, -1);

    // Transform
    auto firstCriteriaType = static_cast<CreatureType>(nFirstCriteriaType);
    auto secondCriteriaType = static_cast<CreatureType>(nSecondCriteriaType);
    auto thirdCriteriaType = static_cast<CreatureType>(nThirdCriteriaType);

    // Execute
    Area::SearchCriteriaList criterias;
    criterias.push_back(std::make_pair(firstCriteriaType, nFirstCriteriaValue));
    if (secondCriteriaType != CreatureType::Invalid) {
        criterias.push_back(std::make_pair(secondCriteriaType, nSecondCriteriaValue));
    }
    if (thirdCriteriaType != CreatureType::Invalid) {
        criterias.push_back(std::make_pair(thirdCriteriaType, nThirdCriteriaValue));
    }
    auto creature = ctx.game.module()->area()->getNearestCreatureToLocation(*lLocation, criterias, nNth - 1);
    return Variable::ofObject(getObjectIdOrInvalid(creature));
}

static Variable GetNearestObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getIntOrElse(args, 0, 32767);
    auto oTarget = getObjectOrCaller(args, 1, ctx);
    auto nNth = getIntOrElse(args, 2, 1);

    // Transform
    auto objectType = static_cast<ObjectType>(nObjectType);

    // Execute
    auto object = ctx.game.module()->area()->getNearestObject(oTarget->position(), nNth - 1, [&objectType](auto &object) {
        return object->type() == objectType;
    });
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable GetNearestObjectToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getInt(args, 0);
    auto lLocation = getLocationArgument(args, 1);
    auto nNth = getIntOrElse(args, 2, 1);

    // Transform
    auto objectType = static_cast<ObjectType>(nObjectType);

    // Execute
    auto object = ctx.game.module()->area()->getNearestObject(lLocation->position(), nNth - 1, [&objectType](auto &object) {
        return object->type() == objectType;
    });
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable GetNearestObjectByTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTag = getString(args, 0);
    auto oTarget = getObjectOrCaller(args, 1, ctx);
    auto nNth = getIntOrElse(args, 2, 1);

    // Transform
    auto tag = boost::to_lower_copy(sTag);

    // Execute
    auto object = ctx.game.module()->area()->getNearestObject(oTarget->position(), nNth - 1, [&tag](auto &object) {
        return object->tag() == tag;
    });
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable IntToFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofFloat(static_cast<float>(nInteger));
}

static Variable FloatToInt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fFloat = getFloat(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(fFloat));
}

static Variable StringToInt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sNumber = getString(args, 0);

    // Transform

    // Execute
    int intValue = 0;
    if (!sNumber.empty()) {
        intValue = stoi(sNumber);
    }
    return Variable::ofInt(intValue);
}

static Variable StringToFloat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sNumber = getString(args, 0);

    // Transform

    // Execute
    // Like StringToInt: what does not read as a number is zero.
    std::istringstream stream(sNumber);
    stream.imbue(std::locale::classic());
    float value = 0.0f;
    if (!(stream >> value)) {
        value = 0.0f;
    }
    return Variable::ofFloat(value);
}

static Variable GetIsEnemy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto oSource = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto target = checkCreature(oTarget);
    auto source = checkCreature(oSource);

    // Execute
    // oTarget is an enemy of oSource when oSource's faction regards it as one.
    bool enemy = ctx.services.game.reputes.getIsEnemy(*source, *target);
    return Variable::ofInt(static_cast<int>(enemy));
}

static Variable GetIsFriend(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto oSource = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto target = checkCreature(oTarget);
    auto source = checkCreature(oSource);

    // Execute
    bool isFriend = ctx.services.game.reputes.getIsFriend(*source, *target);
    return Variable::ofInt(static_cast<int>(isFriend));
}

static Variable GetIsNeutral(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto oSource = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto target = checkCreature(oTarget);
    auto source = checkCreature(oSource);

    // Execute
    bool neutral = ctx.services.game.reputes.getIsNeutral(*source, *target);
    return Variable::ofInt(static_cast<int>(neutral));
}

static Variable GetPCSpeaker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Co-op Free Mode: a terminal the host handed to a guest speaks as the
    // guest's character, so its skills make the checks.
    if (auto *coop = ctx.game.coop(); coop && coop->pcSpeaker()) {
        if (auto speaker = ctx.game.getObjectById(coop->pcSpeaker())) {
            return Variable::ofObject(speaker->id());
        }
    }
    auto player = ctx.game.party().player();
    return Variable::ofObject(getObjectIdOrInvalid(player));
}

static Variable GetStringByStrRef(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofString(ctx.services.resource.strings.getText(nStrRef));
}

static Variable DestroyObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDestroy = getObject(args, 0, ctx);
    auto fDelay = getFloatOrElse(args, 1, 0.0f);
    auto bNoFade = getIntOrElse(args, 2, 0);
    auto fDelayUntilFade = getFloatOrElse(args, 3, 0.0f);
    auto nHideFeedback = getIntOrElse(args, 4, 0);

    // Transform

    // Execute
    if (auto item = dyn_cast<Item>(oDestroy)) {
        uint32_t ownerId = item->owner();
        if (ownerId != 0 && ownerId != kObjectInvalid) {
            if (auto owner = ctx.game.getObjectById(ownerId)) {
                if (auto creature = dyn_cast<Creature>(owner);
                    creature && item->isEquipped()) {
                    creature->takeEquippedItem(item);
                }
                owner->removeItemStack(item);
            }
        }
    }
    ctx.game.module()->area()->destroyObject(*oDestroy);
    return Variable::ofNull();
}

static Variable GetModule(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(getObjectIdOrInvalid(ctx.game.module()));
}

static Variable CreateObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nObjectType = getInt(args, 0);
    auto sTemplate = getString(args, 1);
    auto lLocation = getLocationArgument(args, 2);
    auto bUseAppearAnimation = getIntOrElse(args, 3, 0);

    // Transform
    auto objectType = static_cast<ObjectType>(nObjectType);
    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    auto object = ctx.game.module()->area()->createObject(objectType, tmplt, lLocation);
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable EventSpellCastAt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCaster = getObjectOrNull(args, 0, ctx);
    auto nSpell = getInt(args, 1);
    auto bHarmful = getIntOrElse(args, 2, 1);

    // Execute
    std::vector<std::shared_ptr<Object>> objects {oCaster};
    auto event = ctx.game.newEvent(
        0,
        std::vector<int32_t> {nSpell, bHarmful ? 1 : 0},
        std::vector<float> {},
        std::vector<std::string> {},
        std::move(objects));
    event->setKind(EventKind::SpellCastAt);
    return Variable::ofEvent(std::move(event));
}

static Variable GetLastSpellCaster(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *caster = ctx.execution.findArg(ArgKind::LastSpellCaster)) {
        return *caster;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *spell = ctx.execution.findArg(ArgKind::LastSpell)) {
        return *spell;
    }
    return Variable::ofInt(-1);
}

static Variable GetUserDefinedEventNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *eventNum = ctx.execution.findArg(ArgKind::UserDefinedEventNumber)) {
        return *eventNum;
    }

    return Variable::ofInt(-1);
}

static Variable GetSpellId(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *spell = ctx.execution.findArg(ArgKind::SpellId)) {
        return *spell;
    }

    return Variable::ofInt(-1);
}

static Variable RandomName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // intentionally stubbed: no name tables ship with the game data for it to draw on; gives an empty name
    return Variable::ofString("");
}

static Variable GetLoadFromSaveGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(ctx.game.isLoadingFromSaveGame()));
}

static Variable GetName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(oObject->name());
}

static Variable GetLastSpeaker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *speaker = ctx.execution.findArg(ArgKind::LastSpeaker)) {
        return *speaker;
    }

    return Variable::ofObject(kObjectInvalid);
}

static Variable BeginConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sResRef = getStringOrElse(args, 0, "");
    auto oObjectToDialog = getObjectOrNull(args, 1, ctx);

    // Execute
    // OnDialog context: the caller owns the dialog; the other party is
    // oObjectToDialog or the last speaker.
    auto caller = getCaller(ctx);
    std::string resRef = sResRef.empty() ? caller->conversation() : sResRef;
    if (resRef.empty() || ctx.game.isConversationActive()) {
        return Variable::ofInt(0);
    }
    std::shared_ptr<Object> other = oObjectToDialog;
    if (!other) {
        if (const Variable *speaker = ctx.execution.findArg(ArgKind::LastSpeaker)) {
            other = ctx.game.getObjectById(speaker->objectId);
        }
    }
    debug(str(boost::format("BeginConversation: %s with %s") % resRef % (other ? other->tag() : std::string("(none)"))), LogChannel::Conversation);
    ctx.game.startDialog(caller, resRef);
    return Variable::ofInt(ctx.game.isConversationActive() ? 1 : 0);
}

static Variable GetLastPerceived(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *percieved = ctx.execution.findArg(ArgKind::LastPerceived)) {
        return *percieved;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastPerceptionHeard(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *heard = ctx.execution.findArg(ArgKind::LastPerceptionHeard)) {
        return *heard;
    }
    return Variable::ofInt(0);
}

static Variable GetLastPerceptionInaudible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *inaudible = ctx.execution.findArg(ArgKind::LastPerceptionInaudible)) {
        return *inaudible;
    }
    return Variable::ofInt(0);
}

static Variable GetLastPerceptionSeen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *seen = ctx.execution.findArg(ArgKind::LastPerceptionSeen)) {
        return *seen;
    }
    return Variable::ofInt(0);
}

static Variable GetLastClosedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *lastClosed = ctx.execution.findArg(ArgKind::LastClosedBy)) {
        return *lastClosed;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastPerceptionVanished(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *vanished = ctx.execution.findArg(ArgKind::LastPerceptionVanished)) {
        return *vanished;
    }
    return Variable::ofInt(0);
}

static Variable GetFirstInPersistentObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPersistentObject = getObjectOrCaller(args, 0, ctx);
    auto nResidentObjectType = getIntOrElse(args, 1, 1);
    auto nPersistentZone = getIntOrElse(args, 2, 0);

    // Transform
    // PERSISTENT_ZONE_FOLLOW "is no longer used" (nwscript.nss): every zone
    // reads as the active one.
    (void)nPersistentZone;
    auto trigger = dyn_cast<Trigger>(oPersistentObject);

    // Execute
    auto &scriptArgs = ctx.execution.args;
    if (oPersistentObject) {
        uint32_t ownerId = oPersistentObject->id();
        scriptArgs.erase(
            std::remove_if(scriptArgs.begin(), scriptArgs.end(), [&](const Argument &arg) {
                if (arg.kind != ArgKind::ObjectsInArea) {
                    return false;
                }
                auto objects = std::static_pointer_cast<ObjectsInArea>(arg.var.engineType);
                return objects->area == ownerId && objects->objectFilter == nResidentObjectType;
            }),
            scriptArgs.end());
    }
    if (!trigger) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    // Residents in object id order, so every run walks them the same way.
    // Stored reversed, so the next one is popped from the back.
    std::vector<uint32_t> residents;
    for (const auto &tenant : trigger->tenants()) {
        if (matchesObjectFilter(*tenant, nResidentObjectType)) {
            residents.push_back(tenant->id());
        }
    }
    std::sort(residents.begin(), residents.end(), std::greater<uint32_t>());
    if (residents.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    uint32_t first = residents.back();
    residents.pop_back();

    auto objectsArg = std::make_shared<ObjectsInArea>();
    objectsArg->objects = std::move(residents);
    objectsArg->area = trigger->id();
    objectsArg->objectFilter = nResidentObjectType;
    scriptArgs.push_back(Argument(ArgKind::ObjectsInArea, Variable::ofCustom(objectsArg)));

    return Variable::ofObject(first);
}

static Variable GetNextInPersistentObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPersistentObject = getObjectOrCaller(args, 0, ctx);
    auto nResidentObjectType = getIntOrElse(args, 1, 1);
    auto nPersistentZone = getIntOrElse(args, 2, 0);

    // Transform
    (void)nPersistentZone;

    // Execute
    if (!oPersistentObject) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    auto &scriptArgs = ctx.execution.args;
    for (auto it = scriptArgs.rbegin(), end = scriptArgs.rend(); it != end; ++it) {
        if (it->kind != ArgKind::ObjectsInArea) {
            continue;
        }
        auto objects = std::static_pointer_cast<ObjectsInArea>(it->var.engineType);
        if (objects->area != oPersistentObject->id() || objects->objectFilter != nResidentObjectType) {
            continue;
        }
        while (!objects->objects.empty()) {
            uint32_t next = objects->objects.back();
            objects->objects.pop_back();
            // A resident destroyed mid-iteration is skipped.
            if (ctx.game.getObjectById(next)) {
                return Variable::ofObject(next);
            }
        }
        return Variable::ofObject(script::kObjectInvalid);
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetAreaOfEffectCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oAreaOfEffectObject = getObjectOrCaller(args, 0, ctx);
    (void)oAreaOfEffectObject;

    // Execute
    // intentionally stubbed: areas of effect keep no record of their caster
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable ShowLevelUpGUI(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the screen opens on the next tick when the player character
    // has a level pending (design note gui_screens, "Level-up").
    return Variable::ofInt(ctx.game.requestLevelUpScreen() ? 1 : 0);
}

static Variable SetItemNonEquippable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto bNonEquippable = getInt(args, 1);

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute
    // Does nothing for a non-item. A worn item stays on (design note c11_data).
    if (item) {
        item->setNonEquippable(bNonEquippable != 0);
    }
    return Variable::ofNull();
}

static Variable GetButtonMashCheck(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the flag SetButtonMashCheck stored on the module; off until set.
    auto module = ctx.game.module();
    return Variable::ofInt(module ? module->scriptSetting("ButtonMashCheck") : 0);
}

static Variable SetButtonMashCheck(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCheck = getInt(args, 0);

    // Execute: stored for GetButtonMashCheck; no screen reads it yet.
    if (auto module = ctx.game.module()) {
        module->setScriptSetting("ButtonMashCheck", nCheck != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable GiveItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto oGiveTo = getObject(args, 1, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    // The script's caller is the giver; a caller-less run disturbs as no one.
    const Variable *caller = ctx.execution.findArg(ArgKind::Caller);
    transferItemAndSignalDisturbed(ctx.game, item, *oGiveTo, caller ? caller->objectId : kObjectInvalid);
    return Variable::ofNull();
}

static Variable ObjectToString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofString(str(boost::format("%x") % oObject->id()));
}

static Variable GetIsImmune(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nImmunityType = getInt(args, 1);
    auto oVersus = getObjectOrNull(args, 2, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oCreature);
    auto versus = oVersus ? dyn_cast<Creature>(oVersus) : nullptr;

    // Execute
    bool immune = creature && creature->hasEffectImmunity(
                                  static_cast<ImmunityType>(nImmunityType),
                                  versus.get());
    return Variable::ofInt(static_cast<int>(immune));
}

static std::shared_ptr<Encounter> encounterArgument(const std::vector<Variable> &args, int index, const RoutineContext &ctx) {
    std::shared_ptr<Object> object;
    if (static_cast<int>(args.size()) <= index || args[index].objectId == kObjectSelf) {
        const Variable *callerArg = ctx.execution.findArg(ArgKind::Caller);
        object = callerArg ? ctx.game.getObjectById(callerArg->objectId) : nullptr;
    } else {
        object = getObjectOrNull(args, index, ctx);
    }
    return object ? dyn_cast<Encounter>(object) : nullptr;
}

static Variable GetEncounterActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto encounter = encounterArgument(args, 0, ctx);

    // Execute
    return Variable::ofInt(encounter && encounter->isActive() ? 1 : 0);
}

static Variable SetEncounterActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNewValue = getInt(args, 0);
    auto encounter = encounterArgument(args, 1, ctx);

    // Execute
    // Anything but an encounter (K1 scripts pass creatures and triggers too)
    // is left alone.
    if (encounter) {
        encounter->setActive(nNewValue != 0);
    }
    return Variable::ofNull();
}

static Variable GetEncounterSpawnsMax(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oEncounter = getObjectOrCaller(args, 0, ctx);

    // Execute: how many times the encounter may spawn (its Respawns; -1 for no limit).
    auto encounter = dyn_cast<Encounter>(oEncounter);
    return Variable::ofInt(encounter ? encounter->maxSpawns() : 0);
}

static Variable SetEncounterSpawnsMax(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpawnsMax = getInt(args, 0);
    auto oEncounter = getObjectOrCaller(args, 1, ctx);

    // Execute
    if (auto encounter = dyn_cast<Encounter>(oEncounter)) {
        encounter->setMaxSpawns(nSpawnsMax);
    }
    return Variable::ofNull();
}

static Variable GetEncounterSpawnsCurrent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oEncounter = getObjectOrCaller(args, 0, ctx);

    // Execute: how many times the encounter has spawned (CurrentSpawns).
    auto encounter = dyn_cast<Encounter>(oEncounter);
    return Variable::ofInt(encounter ? encounter->currentSpawns() : 0);
}

static Variable SetEncounterSpawnsCurrent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpawnsCurrent = getInt(args, 0);
    auto oEncounter = getObjectOrCaller(args, 1, ctx);

    // Execute
    if (auto encounter = dyn_cast<Encounter>(oEncounter)) {
        encounter->setCurrentSpawns(nSpawnsCurrent);
    }
    return Variable::ofNull();
}

// An object the module stored when it last handled an event of some kind;
// OBJECT_INVALID with no module or before any such event.
static Variable moduleEventObject(const RoutineContext &ctx, uint32_t ModuleEventRecord::*field) {
    auto module = ctx.game.module();
    if (!module) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(module->eventRecord().*field);
}

static Variable GetModuleItemAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The item of the module's last item-acquired event.
    return moduleEventObject(ctx, &ModuleEventRecord::itemAcquired);
}

static Variable GetModuleItemAcquiredFrom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Where that item came from; OBJECT_INVALID for a created item.
    return moduleEventObject(ctx, &ModuleEventRecord::itemAcquiredFrom);
}

static Variable SetCustomToken(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCustomTokenNumber = getInt(args, 0);
    auto sTokenValue = getString(args, 1);

    // Transform

    // Execute
    ctx.game.setCustomToken(nCustomTokenNumber, std::move(sTokenValue));
    return Variable::ofNull();
}

static Variable GetHasFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto feat = static_cast<FeatType>(nFeat);
    auto creature = checkCreature(oCreature);

    // Execute
    bool hasFeat = creature->hasEffectiveFeat(feat);
    return Variable::ofInt(static_cast<int>(hasFeat));
}

static Variable GetHasSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto skill = static_cast<SkillType>(nSkill);
    auto creature = checkCreature(oCreature);

    // Execute
    bool hasSkill = creature->attributes().hasSkill(skill);
    return Variable::ofInt(static_cast<int>(hasSkill));
}

static Variable GetObjectSeen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto oSource = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto source = checkCreature(oSource);

    // Execute
    bool seen = source->perception().sees(oTarget->id());
    return Variable::ofInt(static_cast<int>(seen));
}

static Variable GetObjectHeard(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);
    auto oSource = getObjectOrCaller(args, 1, ctx);

    // Execute: whether the source creature currently hears the target.
    auto source = asCreature(oSource);
    return Variable::ofInt(source && oTarget && source->perception().hears(oTarget->id()) ? 1 : 0);
}

static Variable GetLastPlayerDied(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The party member of the module's last player-death event.
    return moduleEventObject(ctx, &ModuleEventRecord::lastPlayerDied);
}

static Variable GetModuleItemLost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The item of the module's last item-lost event.
    return moduleEventObject(ctx, &ModuleEventRecord::itemLost);
}

static Variable GetModuleItemLostBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The party member that lost it.
    return moduleEventObject(ctx, &ModuleEventRecord::itemLostBy);
}

static Variable EventConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: signalled to a creature, it runs the creature's OnDialogue script.
    auto event = ctx.game.newEvent(0);
    event->setKind(EventKind::Conversation);
    return Variable::ofEvent(std::move(event));
}

static Variable SetEncounterDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEncounterDifficulty = getInt(args, 0);
    auto oEncounter = getObjectOrCaller(args, 1, ctx);

    // Execute
    if (auto encounter = dyn_cast<Encounter>(oEncounter)) {
        encounter->setDifficulty(nEncounterDifficulty);
    }
    return Variable::ofNull();
}

static Variable GetEncounterDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oEncounter = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto encounter = dyn_cast<Encounter>(oEncounter);
    return Variable::ofInt(encounter ? encounter->difficulty() : 0);
}

static Variable GetDistanceBetweenLocations(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocationA = getLocationArgument(args, 0);
    auto lLocationB = getLocationArgument(args, 1);

    // Transform

    // Execute
    // A location carries no area, so every pair is measured as one area.
    return Variable::ofFloat(glm::distance(lLocationA->position(), lLocationB->position()));
}

static Variable GetReflexAdjustedDamage(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDamage = getInt(args, 0);
    auto oTarget = getObject(args, 1, ctx);
    auto nDC = getInt(args, 2);
    auto nSaveType = getIntOrElse(args, 3, 0);
    auto oSaveVersus = getObjectOrCaller(args, 4, ctx);

    // Transform
    auto creature = asCreature(oTarget);

    // Execute (K2 behaviour, design note k2_merge): the target makes a
    // Reflex save against nDC. A success halves the damage, or avoids it all
    // with Evasion (a K2 feat); a failure takes it all. A target that is not
    // a creature makes no save.
    if (!creature) {
        return Variable::ofInt(nDamage);
    }
    bool saved = creature->rollSavingThrowVersus(2, nDC, static_cast<SavingThrowType>(nSaveType), asCreature(oSaveVersus).get());
    bool evasion = k2feats::hasEvasion([&creature](FeatType feat) { return creature->hasEffectiveFeat(feat); });
    return Variable::ofInt(k2feats::reflexAdjustedDamage(nDamage, saved, evasion));
}

static Variable PlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAnimation = getInt(args, 0);
    auto fSpeed = getFloatOrElse(args, 1, 1.0f);
    auto fSeconds = getFloatOrElse(args, 2, 0.0f);

    // Transform
    auto animation = static_cast<AnimationType>(nAnimation);

    // Execute
    AnimationProperties properties;
    properties.speed = fSpeed;
    auto caller = getCaller(ctx);
    caller->playAnimation(animation, std::move(properties));
    return Variable::ofNull();
}

// A talent argument that may be the invalid talent a failed search returns.
static std::shared_ptr<Talent> getTalentOrNull(const std::vector<Variable> &args, int index) {
    if (index >= static_cast<int>(args.size()) || args[index].type != VariableType::Talent) {
        return nullptr;
    }
    auto talent = std::static_pointer_cast<Talent>(args[index].engineType);
    return talent && talent->type() != TalentType::Invalid ? talent : nullptr;
}

static Variable creatureTalentResult(
    const RoutineContext &ctx,
    const std::shared_ptr<Object> &object,
    const TalentQuery &query) {

    auto creature = asCreature(object);
    std::optional<TalentChoice> choice;
    if (creature) {
        choice = findCreatureTalent(
            *creature, ctx.services.game.spells, ctx.services.game.feats, query,
            [](int min, int max) { return randomInt(min, max); });
    }
    if (Logger::instance.isChannelEnabled(LogChannel::Script)) {
        debug(str(boost::format("Talent search: %s %s 0x%04x -> %s %d") %
                  (object ? object->tag() : std::string("(invalid)")) %
                  (query.mode == TalentSearchMode::Best ? "best" : "random") % query.category %
                  (choice ? (choice->type == TalentType::Feat ? "feat" : (choice->item ? "item" : "force")) : "none") %
                  (choice ? choice->id : -1)),
              LogChannel::Script);
    }
    if (!choice) {
        return Variable::ofTalent(ctx.game.newTalent(TalentType::Invalid, -1));
    }
    uint32_t item = choice->item ? choice->item->id() : 0x7f000000;
    return Variable::ofTalent(ctx.game.newTalent(choice->type, choice->id, 0, item));
}

static Variable TalentSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Spell, nSpell));
}

static Variable TalentFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Feat, nFeat));
}

static Variable TalentSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);

    // Execute
    return Variable::ofTalent(ctx.game.newTalent(TalentType::Skill, nSkill));
}

static Variable GetHasSpellEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oObject = getObjectOrCaller(args, 1, ctx);

    // Execute
    if (nSpell < 0 || !asCreature(oObject)) {
        return Variable::ofInt(0);
    }
    for (const EffectInstance &applied : oObject->effects()) {
        if (applied.spellId == static_cast<uint32_t>(nSpell)) {
            return Variable::ofInt(1);
        }
    }
    return Variable::ofInt(0);
}

static Variable GetEffectSpellId(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eSpellEffect = getEffect(args, 0);

    uint32_t spellId = eSpellEffect->saveFacingInstance().spellId;
    return Variable::ofInt(static_cast<int32_t>(spellId));
}

static Variable GetCreatureHasTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto tTalent = getTalentOrNull(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Execute
    auto creature = asCreature(oCreature);
    if (!tTalent || !creature) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(creatureHasTalent(*creature, tTalent->type(), tTalent->value()) ? 1 : 0);
}

static Variable GetCreatureTalentRandom(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCategory = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);
    auto nInclusion = getIntOrElse(args, 2, 0);

    // Execute
    TalentQuery query;
    query.category = static_cast<uint32_t>(nCategory);
    query.mode = TalentSearchMode::Random;
    query.inclusion = static_cast<uint32_t>(nInclusion);
    return creatureTalentResult(ctx, oCreature, query);
}

static Variable GetCreatureTalentBest(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCategory = getInt(args, 0);
    auto nCRMax = getInt(args, 1);
    auto oCreature = getObjectOrCaller(args, 2, ctx);
    auto nInclusion = getIntOrElse(args, 3, 0);
    auto nExcludeType = getIntOrElse(args, 4, -1);
    auto nExcludeId = getIntOrElse(args, 5, -1);

    // Execute
    TalentQuery query;
    query.category = static_cast<uint32_t>(nCategory);
    query.mode = TalentSearchMode::Best;
    query.crMax = nCRMax;
    query.inclusion = static_cast<uint32_t>(nInclusion);
    query.excludeType = nExcludeType;
    query.excludeId = nExcludeId;
    return creatureTalentResult(ctx, oCreature, query);
}

static Variable GetGoldPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oGold = getObjectOrNull(args, 0, ctx);

    // Execute: a pile of credits is an item whose stack size is its value; 0 for anything else.
    auto item = dyn_cast<Item>(oGold);
    return Variable::ofInt(item ? item->stackSize() : 0);
}

static Variable GetIsPlayableRacialType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = std::dynamic_pointer_cast<Creature>(oCreature);

    // Execute
    // racialtypes.2da playerrace marks the races a player character may be.
    if (!creature) {
        return Variable::ofInt(0);
    }
    auto racialTypes = ctx.services.resource.twoDas.get("racialtypes");
    int row = static_cast<int>(creature->racialType());
    bool playable = racialTypes &&
                    row >= 0 && row < racialTypes->getRowCount() &&
                    racialTypes->getInt(row, "playerrace", 0) != 0;
    return Variable::ofInt(playable ? 1 : 0);
}

static Variable JumpToLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lDestination = getLocationArgument(args, 0);

    // Transform

    // Execute
    auto action = ctx.game.newAction<JumpToLocationAction>(std::move(lDestination));
    getCaller(ctx)->addActionOnTop(std::move(action));
    return Variable::ofNull();
}

static Variable GetSkillRank(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);
    auto oTarget = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto skill = static_cast<SkillType>(nSkill);
    auto target = checkCreature(oTarget);

    // Execute
    return Variable::ofInt(target->getSkillValue(skill));
}

static Variable GetAttackTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    auto target = creature->getAttackTarget();
    return Variable::ofObject(getObjectIdOrInvalid(target));
}

static Variable GetLastAttackType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    (void)oCreature;

    // Execute
    // intentionally stubbed: the engine keeps no attack type (special attack) per attack; answers 0
    return Variable::ofInt(0);
}

static Variable GetLastAttackMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    (void)oCreature;

    // Execute
    // intentionally stubbed: the engine keeps no attack mode per attack; answers 0
    return Variable::ofInt(0);
}

static Variable GetDistanceBetween2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectA = getObject(args, 0, ctx);
    auto oObjectB = getObject(args, 1, ctx);

    // Transform

    // Execute
    float distance = oObjectA->getDistanceTo(glm::vec2(oObjectB->position()));
    return Variable::ofFloat(distance);
}

static Variable GetIsInCombat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    auto bOnlyCountReal = getIntOrElse(args, 1, 0);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    if (!creature->isInCombat()) {
        return Variable::ofInt(0);
    }
    // K2's bOnlyCountReal: only a creature that is fighting someone (it has
    // a target, or something hostile acted on it) counts, not one merely
    // left in combat mode.
    if (bOnlyCountReal) {
        bool real = creature->getAttackTarget() != nullptr ||
                    creature->getAttemptedAttackTarget() != kObjectInvalid ||
                    creature->getAttemptedSpellTarget() != kObjectInvalid ||
                    creature->getLastHostileActor() != kObjectInvalid;
        return Variable::ofInt(real ? 1 : 0);
    }
    return Variable::ofInt(1);
}

static Variable GetLastAssociateCommand(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oAssociate = getObjectOrCaller(args, 0, ctx);
    (void)oAssociate;

    // Execute
    // intentionally stubbed: KotOR has no associate command set (companions are not commanded by shout); answers 0
    return Variable::ofInt(0);
}

static Variable GiveGoldToCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nGP = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    if (creature && ctx.game.party().isMember(*creature)) {
        ctx.game.party().giveGold(nGP);
        // The party's credits row ("Credits Received: <CUSTOM0>").
        if (nGP > 0) {
            ctx.game.submitStatusSummary(StatusSummaryCategory::Credits, nGP);
        }
    } else {
        creature->giveGold(nGP);
    }
    return Variable::ofNull();
}

static Variable SetIsDestroyable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bDestroyable = getInt(args, 0);
    auto bRaiseable = getIntOrElse(args, 1, 1);
    auto bSelectableWhenDead = getIntOrElse(args, 2, 0);

    // Transform

    // Execute
    if (auto creature = asCreature(getCaller(ctx))) {
        creature->setDestroyable(bDestroyable != 0, bRaiseable != 0, bSelectableWhenDead != 0);
    }
    return Variable::ofNull();
}

static Variable SetLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto bLocked = getInt(args, 1);

    // Transform
    bool locked = static_cast<bool>(bLocked);

    // Execute
    // Doors and placeables: tar03_box's puzzle, the tar_m09aa wall puzzle and
    // the Star Forge armour container lock and unlock placeables.
    if (oTarget && oTarget->type() == ObjectType::Placeable) {
        std::static_pointer_cast<Placeable>(oTarget)->setLocked(locked);
        return Variable::ofNull();
    }
    auto target = checkDoor(oTarget);
    target->setLocked(locked);
    return Variable::ofNull();
}

static Variable GetLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Execute
    if (oTarget && oTarget->type() == ObjectType::Placeable) {
        return Variable::ofInt(static_cast<int>(std::static_pointer_cast<Placeable>(oTarget)->isLocked()));
    }
    auto target = checkDoor(oTarget);
    return Variable::ofInt(static_cast<int>(target->isLocked()));
}

static Variable GetClickingObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *object = ctx.execution.findArg(ArgKind::ClickingObject)) {
        return *object;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable SetAssociateListenPatterns(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oTarget = getObjectOrCaller(args, 0, ctx);
    (void)oTarget;

    // Execute
    // intentionally stubbed: KotOR has no associate shouts for the patterns to listen to
    return Variable::ofNull();
}

static Variable GetLastWeaponUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Execute: the weapon of the creature's latest attack, invalid when it
    // attacked unarmed or has not attacked.
    auto creature = asCreature(oCreature);
    return Variable::ofObject(creature ? creature->lastWeaponUsed() : script::kObjectInvalid);
}

static Variable GetLastUsedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *usedBy = ctx.execution.findArg(ArgKind::LastUsedBy)) {
        return *usedBy;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetAbilityModifier(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAbility = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto ability = static_cast<Ability>(nAbility);

    // Execute
    return Variable::ofInt(creature->getEffectiveAbilityModifier(ability));
}

static Variable GetIdentified(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(checkItem(oItem)->isIdentified()));
}

static Variable SetIdentified(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto bIdentified = getInt(args, 1);

    // Transform

    // Execute
    checkItem(oItem)->setIdentified(static_cast<bool>(bIdentified));
    return Variable::ofNull();
}

static Variable GetDistanceBetweenLocations2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto lLocationA = getLocationArgument(args, 0);
    auto lLocationB = getLocationArgument(args, 1);

    // Transform

    // Execute
    return Variable::ofFloat(glm::distance(glm::vec2(lLocationA->position()), glm::vec2(lLocationB->position())));
}

static Variable GetDistanceToObject2D(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    auto caller = getCaller(ctx);
    float result = caller->getDistanceTo(glm::vec2(oObject->position()));
    return Variable::ofFloat(result);
}

static Variable GetBlockingDoor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Captured when the blocked event was raised, so a continuation of that run
    // reports the door the event was about rather than whatever obstructs the
    // creature by the time the continuation asks. Validity is left to
    // GetIsObjectValid, as for any other object a script holds on to.
    if (const Variable *blockingDoor = ctx.execution.findArg(ArgKind::BlockingDoor)) {
        return *blockingDoor;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetIsDoorActionPossible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTargetDoor = getObject(args, 0, ctx);
    auto nDoorAction = getInt(args, 1);

    // Transform
    auto targetDoor = checkDoor(oTargetDoor);
    auto doorAction = static_cast<DoorAction>(nDoorAction);

    // Execute
    bool possible = false;
    switch (doorAction) {
    case DoorAction::Open:
        possible = !targetDoor->isLocked();
        break;
    case DoorAction::Bash: {
        auto caller = std::dynamic_pointer_cast<Creature>(getCaller(ctx));
        possible = caller && canBashDoor(*targetDoor, *caller, ctx.services.game.reputes);
        break;
    }
    default:
        // No shipped K1 or K2 content performs UNLOCK, IGNORE or KNOCK, so
        // there is no behaviour to reproduce. Report them as impossible rather
        // than guessing at semantics.
        debug(str(boost::format("Unsupported door action: %d") % nDoorAction), LogChannel::Script);
        break;
    }

    return Variable::ofInt(static_cast<int>(possible));
}

static Variable DoDoorAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTargetDoor = getObject(args, 0, ctx);
    auto nDoorAction = getInt(args, 1);

    // Transform
    auto targetDoor = checkDoor(oTargetDoor);
    auto doorAction = static_cast<DoorAction>(nDoorAction);
    auto caller = getCaller(ctx);

    // Execute
    switch (doorAction) {
    case DoorAction::Open:
        // Opening is immediate: the routine returns void and the door state,
        // animation, walkmesh swap and OnOpen event are the same ones a door
        // opened through OpenDoorAction goes through. Nothing is queued, so a
        // movement action that was blocked by this door stays in place and
        // resumes once the door stops obstructing.
        if (!targetDoor->isLocked()) {
            targetDoor->open(caller.get());
            targetDoor->onOpen(caller->id());
        }
        break;
    case DoorAction::Bash: {
        // Bashing is a sustained attack rather than an immediate state change,
        // so it goes on top of the queue: whatever the creature was doing stays
        // behind it instead of being discarded.
        auto creature = std::dynamic_pointer_cast<Creature>(caller);
        if (creature && canBashDoor(*targetDoor, *creature, ctx.services.game.reputes)) {
            creature->addActionOnTop(ctx.game.newAction<AttackObjectAction>(targetDoor));
        }
        break;
    }
    default:
        debug(str(boost::format("Unsupported door action: %d") % nDoorAction), LogChannel::Script);
        break;
    }

    return Variable::ofNull();
}

static Variable GetFirstItemInInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    auto item = oTarget->getFirstItem();
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable GetNextItemInInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    auto item = oTarget->getNextItem();
    return Variable::ofObject(getObjectIdOrInvalid(item));
}

static Variable GetClassByPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassPosition = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    auto clazz = creature->attributes().getClassByPosition(nClassPosition);
    return Variable::ofInt(static_cast<int>(clazz));
}

static Variable GetLevelByPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassPosition = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    int level = creature->attributes().getLevelByPosition(nClassPosition);
    return Variable::ofInt(level);
}

static Variable GetLevelByClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassType = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto classType = static_cast<ClassType>(nClassType);

    // Execute
    int level = creature->attributes().getClassLevel(classType);
    return Variable::ofInt(level);
}

static Variable GetDamageDealtByType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDamageType = getInt(args, 0);

    // Execute: the part of the caller's last damage event of this one type
    // (k39_ud_terentate asks for DAMAGE_TYPE_SONIC).
    auto caller = asCreature(getCaller(ctx));
    return Variable::ofInt(caller ? caller->lastDamageOfType(nDamageType) : 0);
}

static Variable GetTotalDamageDealt(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto caller = asCreature(getCaller(ctx));
    return Variable::ofInt(caller ? caller->lastDamageTotal() : 0);
}

static Variable GetLastDamager(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto object = getCaller(ctx);
    return Variable::ofObject(object->getLastDamager());
}

// Trap fields of a trapped trigger, door or placeable (design note c11_data).
static std::optional<TrapFields> getTrapFields(const std::shared_ptr<Object> &object) {
    if (auto trigger = std::dynamic_pointer_cast<Trigger>(object)) {
        if (trigger->isTrapTrigger()) {
            return trigger->trapFields();
        }
        return std::nullopt;
    }
    if (auto door = std::dynamic_pointer_cast<Door>(object)) {
        return door->trapFields();
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(object)) {
        return placeable->trapFields();
    }
    return std::nullopt;
}

static Variable GetLastDisarmed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The creature that disarmed the trap whose OnDisarm is running.
    auto trap = std::dynamic_pointer_cast<Trigger>(getCaller(ctx));
    return Variable::ofObject(trap ? trap->lastDisarmedBy() : kObjectInvalid);
}

static Variable GetLastDisturbed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *lastDisturbed = ctx.execution.findArg(ArgKind::LastDisturbed)) {
        return *lastDisturbed;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetLastLocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: who last locked the calling door or placeable.
    auto caller = getCaller(ctx);
    return Variable::ofObject(caller ? caller->lastLockedBy() : script::kObjectInvalid);
}

static Variable GetLastUnlocked(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: who last unlocked the calling door or placeable.
    auto caller = getCaller(ctx);
    return Variable::ofObject(caller ? caller->lastUnlockedBy() : script::kObjectInvalid);
}

static Variable GetInventoryDisturbType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *disturbType = ctx.execution.findArg(ArgKind::InventoryDisturbType)) {
        return *disturbType;
    }
    return Variable::ofInt(0);
}

static Variable GetInventoryDisturbItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *disturbItem = ctx.execution.findArg(ArgKind::InventoryDisturbItem)) {
        return *disturbItem;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable ShowUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto oCharacter = getObjectOrNull(args, 1, ctx);
    auto nDisableItemCreation = getIntOrElse(args, 2, 0);
    auto nDisableUpgrade = getIntOrElse(args, 3, 0);
    auto sOverride2DA = getStringOrElse(args, 4, "");

    // Transform
    auto item = std::dynamic_pointer_cast<Item>(oItem);

    // Execute: the bench opens on the next tick, straight on the item when a
    // valid one is given. The K2-only arguments (character, disabled item
    // creation/upgrade, override table) have no K1 caller.
    (void)oCharacter;
    (void)nDisableItemCreation;
    (void)nDisableUpgrade;
    (void)sOverride2DA;
    ctx.game.requestUpgradeScreen(item);
    return Variable::ofNull();
}

static Variable VersusAlignmentEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);
    auto nLawChaos = getIntOrElse(args, 1, 0);
    auto nGoodEvil = getIntOrElse(args, 2, 0);

    if (eEffect && nLawChaos >= 0 && nLawChaos <= 3) {
        eEffect->setVersusAlignment(nLawChaos, nGoodEvil);
    }
    return Variable::ofEffect(std::move(eEffect));
}

static Variable VersusRacialTypeEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);
    auto nRacialType = getInt(args, 1);

    if (eEffect &&
        nRacialType >= static_cast<int>(RacialType::Unknown) &&
        nRacialType <= static_cast<int>(RacialType::All)) {
        eEffect->setVersusRacialType(nRacialType);
    }
    return Variable::ofEffect(std::move(eEffect));
}

static Variable VersusTrapEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);

    // Execute
    // K1 content never calls it; the effect is returned unchanged.
    return Variable::ofEffect(std::move(eEffect));
}

static Variable GetGender(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->gender()));
}

static Variable GetIsTalentValid(const std::vector<Variable> &args, const RoutineContext &ctx) {
    bool valid;
    try {
        auto tTalent = getTalent(args, 0);
        valid = static_cast<bool>(tTalent);
    } catch (const RoutineArgumentException &ignored) {
        valid = false;
    }
    return Variable::ofInt(static_cast<int>(valid));
}

static Variable GetAttemptedAttackTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = checkCreature(getCaller(ctx));
    return Variable::ofObject(caller->getAttemptedAttackTarget());
}

static Variable GetTypeFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto tTalent = getTalentOrNull(args, 0);
    // K2 behaviour: an invalid talent's type is -1.
    if (!tTalent || tTalent->type() == TalentType::Invalid) {
        return Variable::ofInt(-1);
    }
    return Variable::ofInt(static_cast<int>(tTalent->type()));
}

static Variable GetIdFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto tTalent = getTalentOrNull(args, 0);
    return Variable::ofInt(tTalent ? tTalent->value() : -1);
}

static Variable PlayPazaak(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nOpponentPazaakDeck = getInt(args, 0);
    auto sEndScript = getString(args, 1);
    auto nMaxWager = getInt(args, 2);
    auto bShowTutorial = getIntOrElse(args, 3, 0);
    auto oOpponent = getObjectOrNull(args, 4, ctx);
    if (!oOpponent) {
        // K1's authored k_act_mispaz call omits the optional opponent. In
        // NWScript that argument defaults to OBJECT_INVALID; the script caller
        // is the only authored identity/continuation context available.
        oOpponent = getCaller(ctx);
    }

    auto endScript = boost::to_lower_copy(sEndScript);

    // Execute
    ctx.game.playPazaak(
        nOpponentPazaakDeck,
        std::move(endScript),
        nMaxWager,
        bShowTutorial != 0,
        oOpponent);
    return Variable::ofNull();
}

static Variable GetLastPazaakResult(const std::vector<Variable> &args, const RoutineContext &ctx) {
    const auto &result = ctx.game.lastPazaakResult();
    if (!result) {
        return Variable::ofInt(0);
    }
    // Shipped K1 scripts consume 1 as a player win and 0 as a player loss.
    // Forfeit follows the loss path.
    return Variable::ofInt(
        *result == PazaakCompletedResult::PlayerWon ? 1 : 0);
}

static Variable DisplayFeedBackText(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nTextConstant = getInt(args, 1);

    // Execute: feedback text is presentation only (feedbacktext.2da); the
    // message log does not show it yet, and nothing in the game state moves.
    debug(str(boost::format("DisplayFeedBackText: %d") % nTextConstant), LogChannel::Script);
    return Variable::ofNull();
}

static Variable AddJournalQuestEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);
    auto nState = getInt(args, 1);
    auto bAllowOverrideHigher = getIntOrElse(args, 2, 0);

    // Transform
    auto allowOverrideHigher = static_cast<bool>(bAllowOverrideHigher);

    // Execute
    ctx.game.journal().addEntry(szPlotID, nState, allowOverrideHigher);
    return Variable::ofNull();
}

static Variable RemoveJournalQuestEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    ctx.game.journal().removeEntry(szPlotID);
    return Variable::ofNull();
}

static Variable GetJournalEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    return Variable::ofInt(ctx.game.journal().getEntryState(szPlotID));
}

static Variable PlayRumblePattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPattern = getInt(args, 0);

    // Transform

    // Execute
    // Controller rumble. We drive no rumble device; retail hands the pattern
    // to the input layer, and no K1 script reads the result.
    return Variable::ofInt(0);
}

static Variable StopRumblePattern(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPattern = getInt(args, 0);
    (void)nPattern;

    // Execute
    // intentionally stubbed: the engine has no controller rumble to stop; reports no pattern was playing
    return Variable::ofInt(0);
}

static Variable SendMessageToPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlayer = getObjectOrNull(args, 0, ctx);
    auto szMessage = getString(args, 1);

    // Execute: a line of feedback in the console for the player; other objects get nothing.
    if (asCreature(oPlayer) && !szMessage.empty()) {
        ctx.game.console().printLine(szMessage);
    }
    return Variable::ofNull();
}

static Variable GetAttemptedSpellTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto caller = asCreature(getCaller(ctx));
    return Variable::ofObject(caller ? caller->getAttemptedSpellTarget() : kObjectInvalid);
}

static Variable GetLastOpenedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *opened = ctx.execution.findArg(ArgKind::LastOpenedBy)) {
        return *opened;
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetHasSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);
    auto spell = static_cast<SpellType>(nSpell);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->attributes().hasSpell(spell)));
}

static Variable OpenStore(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oStore = getObjectOrNull(args, 0, ctx);
    auto oPC = getObjectOrNull(args, 1, ctx);
    auto nBonusMarkUp = getIntOrElse(args, 2, 0);
    auto nBonusMarkDown = getIntOrElse(args, 3, 0);

    // Transform
    auto store = std::dynamic_pointer_cast<Store>(oStore);
    auto customer = std::dynamic_pointer_cast<Creature>(oPC);

    // Execute: the store screen opens on the next tick, once the calling
    // conversation has ended (design note gui_screens, "Store").
    if (store) {
        ctx.game.requestOpenStore(store, customer, nBonusMarkUp, nBonusMarkDown);
    }
    return Variable::ofNull();
}

static Variable GetFirstFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMemberOfFaction = getObjectOrNull(args, 0, ctx);
    auto bPCOnly = getIntOrElse(args, 1, 1);

    // Transform
    auto faction = oMemberOfFaction ? getObjectFaction(*oMemberOfFaction) : std::nullopt;
    if (!faction) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    // Execute
    auto members = collectFactionMembers(*faction, bPCOnly != 0, ctx);
    if (members.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    auto state = std::make_shared<FactionMembers>();
    state->faction = *faction;
    state->pcOnly = bPCOnly != 0;
    for (auto it = members.rbegin(); it != members.rend(); ++it) {
        state->objects.push_back((*it)->id());
    }
    uint32_t first = state->objects.back();
    state->objects.pop_back();
    ctx.execution.args.push_back(Argument(ArgKind::FactionMembers, Variable::ofCustom(state)));
    return Variable::ofObject(first);
}

static Variable GetNextFactionMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMemberOfFaction = getObjectOrNull(args, 0, ctx);
    auto bPCOnly = getIntOrElse(args, 1, 1);

    // Transform
    auto faction = oMemberOfFaction ? getObjectFaction(*oMemberOfFaction) : std::nullopt;
    if (!faction) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    // Execute: continues the latest cycle started for this faction and filter.
    auto &scriptArgs = ctx.execution.args;
    for (auto it = scriptArgs.rbegin(), end = scriptArgs.rend(); it != end; ++it) {
        if (it->kind != ArgKind::FactionMembers) {
            continue;
        }
        auto state = std::static_pointer_cast<FactionMembers>(it->var.engineType);
        if (state->faction != *faction || state->pcOnly != (bPCOnly != 0)) {
            continue;
        }
        if (state->objects.empty()) {
            break;
        }
        uint32_t next = state->objects.back();
        state->objects.pop_back();
        return Variable::ofObject(next);
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetJournalQuestExperience(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);

    // Execute
    return Variable::ofInt(ctx.game.getPlotXP(szPlotID));
}

static Variable JumpToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oToJumpTo = getObject(args, 0, ctx);
    auto nWalkStraightLineToPoint = getIntOrElse(args, 1, 1);

    // Transform
    auto walkStraightLineToPoint = static_cast<bool>(nWalkStraightLineToPoint);

    // Execute
    auto action = ctx.game.newAction<JumpToObjectAction>(std::move(oToJumpTo), walkStraightLineToPoint);
    getCaller(ctx)->addActionOnTop(std::move(action));
    return Variable::ofNull();
}

static Variable SetMapPinEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMapPin = getObjectOrNull(args, 0, ctx);
    auto nEnabled = getInt(args, 1);

    // Transform
    auto waypoint = std::dynamic_pointer_cast<Waypoint>(oMapPin);

    // Execute: a map pin is a waypoint's map note.
    if (waypoint) {
        waypoint->setMapNoteEnabled(nEnabled != 0);
    }
    return Variable::ofNull();
}

static Variable PopUpGUIPanel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oPC = getObject(args, 0, ctx);
    auto nGUIPanel = getInt(args, 1);
    (void)oPC;
    (void)nGUIPanel;

    // Execute
    // intentionally stubbed: the GUI_PANEL_* ids are not documented, so no panel can be chosen
    return Variable::ofNull();
}

static Variable AddMultiClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nClassType = getInt(args, 0);
    auto oSource = getObjectOrNull(args, 1, ctx);

    // Transform
    auto creature = asCreature(oSource);
    if (!creature || nClassType < 0) {
        return Variable::ofNull();
    }

    // Execute: k_pdan_makejedi (danm13) makes the PC a Jedi this way; the new
    // class arrives at level 0 and the pending level-up goes into it.
    creature->addMultiClass(static_cast<ClassType>(nClassType));
    return Variable::ofNull();
}

// The immunity that stops an effect of this type landing, as the effects'
// own application checks decide it; None for effects no immunity stops.
static ImmunityType immunityAgainst(EffectType type) {
    switch (type) {
    case EffectType::Stunned:
    case EffectType::DroidStun:
        return ImmunityType::Stun;
    case EffectType::Paralyze:
        return ImmunityType::Paralysis;
    case EffectType::Sleep:
        return ImmunityType::Sleep;
    case EffectType::Entangle:
        return ImmunityType::Entangle;
    case EffectType::Knockdown:
    case EffectType::ForcePushed:
        return ImmunityType::Knockdown;
    case EffectType::Frightened:
    case EffectType::Horrified:
        return ImmunityType::Fear;
    case EffectType::Confused:
        return ImmunityType::Confused;
    case EffectType::Poison:
        return ImmunityType::Poison;
    case EffectType::Disease:
        return ImmunityType::Disease;
    case EffectType::Slow:
        return ImmunityType::Slow;
    case EffectType::Charmed:
        return ImmunityType::Charm;
    case EffectType::Dominated:
        return ImmunityType::Dominate;
    case EffectType::Dazed:
        return ImmunityType::Dazed;
    case EffectType::Blindness:
        return ImmunityType::Blindness;
    case EffectType::Deaf:
        return ImmunityType::Deafness;
    case EffectType::Silence:
        return ImmunityType::Silence;
    case EffectType::AbilityDecrease:
        return ImmunityType::AbilityDecrease;
    case EffectType::AttackDecrease:
        return ImmunityType::AttackDecrease;
    case EffectType::DamageDecrease:
        return ImmunityType::DamageDecrease;
    case EffectType::DamageImmunityDecrease:
        return ImmunityType::DamageImmunityDecrease;
    case EffectType::ACDecrease:
        return ImmunityType::AcDecrease;
    case EffectType::MovementSpeedDecrease:
        return ImmunityType::MovementSpeedDecrease;
    case EffectType::SavingThrowDecrease:
        return ImmunityType::SavingThrowDecrease;
    case EffectType::ForceResistanceDecrease:
        return ImmunityType::ForceResistanceDecrease;
    case EffectType::SkillDecrease:
        return ImmunityType::SkillDecrease;
    case EffectType::NegativeLevel:
        return ImmunityType::NegativeLevel;
    default:
        return ImmunityType::None;
    }
}

static bool isLinkImmune(const Creature &target, const Effect &effect, const Creature *versus) {
    if (effect.type() == EffectType::LinkEffects) {
        const auto &link = static_cast<const LinkEffectsEffect &>(effect);
        return (link.childEffect() && isLinkImmune(target, *link.childEffect(), versus)) ||
               (link.parentEffect() && isLinkImmune(target, *link.parentEffect(), versus));
    }
    auto immunity = immunityAgainst(effect.type());
    return immunity != ImmunityType::None && target.hasEffectImmunity(immunity, versus);
}

static Variable GetIsLinkImmune(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto eEffect = getEffect(args, 1);

    // Transform
    auto target = dyn_cast<Creature>(oTarget);
    std::shared_ptr<Creature> versus;
    if (const Variable *caller = ctx.execution.findArg(script::ArgKind::Caller)) {
        versus = dyn_cast<Creature>(ctx.game.getObjectById(caller->objectId));
    }

    // Execute: TRUE when an immunity of the target stops any effect in the
    // link (grenade scripts test this before applying their stun/entangle).
    bool immune = target && eEffect && isLinkImmune(*target, *eEffect, versus.get());
    return Variable::ofInt(static_cast<int>(immune));
}

static Variable GiveXPToCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nXpAmount = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    if (ctx.game.party().isMember(*creature)) {
        ctx.game.party().awardXP(nXpAmount, XPSource::Plot);
    } else {
        creature->giveXP(nXpAmount);
    }
    return Variable::ofNull();
}

static Variable SetXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nXpAmount = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    if (ctx.game.party().isMember(*creature)) {
        ctx.game.party().setXP(nXpAmount);
    } else {
        creature->setXP(nXpAmount);
    }
    return Variable::ofNull();
}

static Variable GetXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(creature->xp());
}

static Variable IntToHexString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nInteger = getInt(args, 0);

    // Execute: "0x" and eight lower-case digits, e.g. 123 -> "0x0000007b"
    // (negative numbers show their two's complement bits).
    return Variable::ofString(str(boost::format("0x%08x") % static_cast<uint32_t>(nInteger)));
}

static Variable GetBaseItemType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);

    // Transform
    auto item = checkItem(oItem);

    // Execute
    return Variable::ofInt(item->baseItemType());
}

static Variable GetItemHasItemProperty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);
    auto nProperty = getInt(args, 1);

    // Execute: whether an active property of that type (ITEM_PROPERTY_*) is on the item.
    auto item = dyn_cast<Item>(oItem);
    if (item) {
        for (const auto &property : item->properties()) {
            if (item->isPropertyActive(property) && property.propertyName == nProperty) {
                return Variable::ofInt(1);
            }
        }
    }
    return Variable::ofInt(0);
}

static Variable GetItemACValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Execute: the item's base defense plus its unconditional AC bonus
    // properties (those not tied to an alignment, damage type or race); 0 for
    // anything but an item.
    auto item = dyn_cast<Item>(oItem);
    if (!item) {
        return Variable::ofInt(0);
    }
    int value = item->baseDefense();
    auto costs = ctx.services.resource.twoDas.get("iprp_bonuscost");
    for (const auto &property : item->properties()) {
        if (item->isPropertyActive(property) && property.propertyName == static_cast<int>(ItemProperty::AcBonus) && costs) {
            value += costs->getInt(property.costValue, "value", 0);
        }
    }
    return Variable::ofInt(value);
}

static Variable ExploreAreaForPlayer(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObjectOrNull(args, 0, ctx);
    auto oPlayer = getObjectOrNull(args, 1, ctx);
    (void)oPlayer; // one explored map for the party

    // Execute: marks the whole map of that area explored.
    if (auto area = dyn_cast<Area>(oArea)) {
        area->mapExploration().revealAll();
    }
    return Variable::ofNull();
}

static Variable GetIsDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(currentDayState(ctx).day ? 1 : 0);
}

static Variable GetIsNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(currentDayState(ctx).night ? 1 : 0);
}

static Variable GetIsDawn(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(currentDayState(ctx).dawn ? 1 : 0);
}

static Variable GetIsDusk(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(currentDayState(ctx).dusk ? 1 : 0);
}

static Variable GetIsEncounterCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = dyn_cast<Creature>(oCreature);

    // Execute
    // True for a creature an encounter (UTE) spawned.
    return Variable::ofInt(creature && creature->encounterId() != kObjectInvalid ? 1 : 0);
}

static Variable GetLastPlayerDying(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The party member of the module's last player-dying event.
    return moduleEventObject(ctx, &ModuleEventRecord::lastPlayerDying);
}

static Variable GetStartingLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the module's entry position and facing (Mod_Entry_X/Y/Z and direction).
    auto module = ctx.game.module();
    if (!module) {
        return Variable::ofLocation(std::make_shared<Location>(glm::vec3(0.0f), 0.0f));
    }
    const auto &info = module->info();
    return Variable::ofLocation(std::make_shared<Location>(info.entryPosition, info.entryFacing));
}

static Variable ChangeToStandardFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreatureToChange = getObjectOrNull(args, 0, ctx);
    auto nStandardFaction = getInt(args, 1);

    // Execute: only a standard (NPC) faction is taken, and a player character keeps its own;
    // anything else is ignored rather than stopping the script.
    auto creature = dyn_cast<Creature>(oCreatureToChange);
    if (!creature || nStandardFaction <= (int)Faction::Invalid || nStandardFaction >= (int)Faction::Last ||
        creature->isPC()) {
        return Variable::ofNull();
    }
    creature->setFaction((Faction)nStandardFaction);
    return Variable::ofNull();
}

static Variable SoundObjectPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = checkSound(oSound);

    // Execute
    sound->setActive(true);
    return Variable::ofNull();
}

static Variable SoundObjectStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = checkSound(oSound);

    // Execute
    sound->setActive(false);
    return Variable::ofNull();
}

static Variable SoundObjectSetVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    if (sound) {
        sound->setVolume(nVolume);
    }
    return Variable::ofNull();
}

static Variable SoundObjectSetPosition(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObjectOrNull(args, 0, ctx);
    auto vPosition = getVector(args, 1);

    // Execute
    if (auto sound = dyn_cast<Sound>(oSound)) {
        sound->setPosition(vPosition);
    }
    return Variable::ofNull();
}

static Variable SpeakOneLinerConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sDialogResRef = getStringOrElse(args, 0, "");
    auto oTokenTarget = getObjectOrNull(args, 1, ctx);
    (void)oTokenTarget;

    // Execute: the first line of the dialog as a bark; nothing for a missing dialog or an empty line.
    if (sDialogResRef.empty()) {
        return Variable::ofNull();
    }
    std::shared_ptr<resource::Dialog> dialog;
    try {
        dialog = ctx.services.resource.dialogs.get(sDialogResRef);
    } catch (const std::exception &) {
        dialog.reset();
    }
    if (!dialog || dialog->startEntries.empty()) {
        return Variable::ofNull();
    }
    int index = dialog->startEntries.front().index;
    if (index < 0 || index >= static_cast<int>(dialog->entries.size())) {
        return Variable::ofNull();
    }
    const std::string &text = dialog->entries[index].text;
    if (!text.empty()) {
        ctx.game.setBarkBubbleText(text, barkDuration(text));
    }
    return Variable::ofNull();
}

static Variable GetGold(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oTarget);

    // Execute
    // Credits are a single party-shared pool in KOTOR; party members (incl. the
    // PC speaker / leader) report the party total.
    if (creature && ctx.game.party().isMember(*creature)) {
        return Variable::ofInt(ctx.game.party().gold());
    }
    return Variable::ofInt(creature->gold());
}

static Variable GetLastRespawnButtonPresser(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The player of the module's last respawn-button event.
    return moduleEventObject(ctx, &ModuleEventRecord::lastRespawnButtonPresser);
}

static Variable SetLightsaberPowered(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto bOverride = getInt(args, 1);
    auto bPowered = getIntOrElse(args, 2, 1);
    auto bShowTransition = getIntOrElse(args, 3, 0);

    // Transform

    // Execute
    if (auto creature = asCreature(oCreature)) {
        creature->setLightsaberPowerOverride(bOverride != 0, bPowered != 0, bShowTransition != 0);
    }
    return Variable::ofNull();
}

static Variable GetIsWeaponEffective(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oVersus = getObjectOrNull(args, 0, ctx);
    auto bOffHand = getIntOrElse(args, 1, 0);
    (void)bOffHand;

    // Execute: no KotOR weapon is ever barred from hurting a target (there is no
    // damage-immune-to-nonmagic rule), so a weapon is effective against any live target.
    return Variable::ofInt(oVersus && !oVersus->isDead() ? 1 : 0);
}

static Variable GetLastSpellHarmful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    if (const Variable *harmful = ctx.execution.findArg(ArgKind::LastSpellHarmful)) {
        return *harmful;
    }
    return Variable::ofInt(0);
}

static Variable EventActivateItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObject(args, 0, ctx);
    auto lTarget = getLocationArgument(args, 1);
    auto oTarget = getObjectOrNull(args, 2, ctx);

    // Execute: signalled to a creature, it runs the module's OnActivateItem script
    // with that creature as the activator (the target location is not carried).
    (void)lTarget;
    std::vector<std::shared_ptr<Object>> objects {oItem, oTarget};
    auto event = ctx.game.newEvent(0, std::vector<int32_t> {}, std::vector<float> {}, std::vector<std::string> {}, std::move(objects));
    event->setKind(EventKind::ActivateItem);
    return Variable::ofEvent(std::move(event));
}

static Variable MusicBackgroundPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setBackgroundPlaying(true);
        if (auto module = ctx.game.module(); module && module->area() == area) {
            ctx.game.refreshAreaMusic();
        }
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setBackgroundPlaying(false);
        if (auto module = ctx.game.module(); module && module->area() == area) {
            ctx.game.refreshAreaMusic();
        }
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundSetDelay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nDelay = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setDelay(nDelay);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundChangeDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nTrack = getInt(args, 1);
    auto nStreamingMusic = getIntOrElse(args, 2, 0);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    // The third argument selects streamed music in K2; K1 music always
    // streams, so it changes nothing.
    static_cast<void>(nStreamingMusic);
    if (area) {
        area->changeMusic(Area::MusicSlot::Day, nTrack);
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundChangeNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nTrack = getInt(args, 1);
    auto nStreamingMusic = getIntOrElse(args, 2, 0);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    // Heard in night areas only (K1 has none). The third argument selects
    // streamed music in K2; K1 music always streams.
    static_cast<void>(nStreamingMusic);
    if (area) {
        area->changeMusic(Area::MusicSlot::Night, nTrack);
    }
    return Variable::ofNull();
}

static Variable MusicBattlePlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setBattlePlaying(true);
        if (auto module = ctx.game.module(); module && module->area() == area) {
            ctx.game.refreshAreaMusic();
        }
    }
    return Variable::ofNull();
}

static Variable MusicBattleStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setBattlePlaying(false);
        if (auto module = ctx.game.module(); module && module->area() == area) {
            ctx.game.refreshAreaMusic();
        }
    }
    return Variable::ofNull();
}

static Variable MusicBattleChange(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->changeMusic(Area::MusicSlot::Battle, nTrack);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundPlay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setAmbientPlaying(true);
        area->refreshMusic();
    }
    return Variable::ofNull();
}

static Variable AmbientSoundStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setAmbientPlaying(false);
        area->refreshMusic();
    }
    return Variable::ofNull();
}

static Variable AmbientSoundChangeDay(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->changeAmbient(true, nTrack);
    }
    return Variable::ofNull();
}

static Variable AmbientSoundChangeNight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nTrack = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->changeAmbient(false, nTrack);
    }
    return Variable::ofNull();
}

static Variable GetLastKiller(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = asCreature(getCaller(ctx));
    return Variable::ofObject(caller ? caller->lastKiller() : kObjectInvalid);
}

static Variable GetSpellCastItem(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Set by CastSpellAtObjectAction for the impact script of a spell cast from an item.
    if (const Variable *item = ctx.execution.findArg(ArgKind::SpellCastItem)) {
        return *item;
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetItemActivated(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Event data of the module's OnActivateItem script.
    if (const Variable *item = ctx.execution.findArg(ArgKind::ItemActivated)) {
        return *item;
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetItemActivator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *activator = ctx.execution.findArg(ArgKind::ItemActivator)) {
        return *activator;
    }
    return Variable::ofObject(script::kObjectInvalid);
}

static Variable GetItemActivatedTargetLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The module keeps the last activation; before any, the origin.
    auto module = ctx.game.module();
    if (!module) {
        return Variable::ofLocation(ctx.game.newLocation(glm::vec3(0.0f), 0.0f));
    }
    const ItemActivation &activation = module->itemActivation();
    return Variable::ofLocation(ctx.game.newLocation(activation.targetPosition, activation.targetFacing));
}

static Variable GetItemActivatedTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto module = ctx.game.module();
    return Variable::ofObject(module ? module->itemActivation().target : script::kObjectInvalid);
}

static Variable GetIsOpen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oObject->isOpen()));
}

static Variable TakeGoldFromCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAmount = getInt(args, 0);
    auto oCreatureToTakeFrom = getObject(args, 1, ctx);
    auto bDestroy = getIntOrElse(args, 2, 0);

    // Transform
    auto creatureToTakeFrom = checkCreature(oCreatureToTakeFrom);
    bool destroy = static_cast<bool>(bDestroy);

    // Execute
    if (creatureToTakeFrom) {
        if (ctx.game.party().isMember(*creatureToTakeFrom)) {
            ctx.game.party().takeGold(nAmount);
        } else {
            creatureToTakeFrom->takeGold(nAmount);
        }
    }
    if (!destroy) {
        auto caller = checkCreature(getCaller(ctx));
        if (caller) {
            if (ctx.game.party().isMember(*caller)) {
                ctx.game.party().giveGold(nAmount);
            } else {
                caller->giveGold(nAmount);
            }
        }
    }
    return Variable::ofNull();
}

static Variable GetIsInConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    // Execute
    return Variable::ofInt(oObject->isInConversation());
}

static Variable GetPlotFlag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    bool plotFlag = oTarget->plotFlag();
    return Variable::ofInt(static_cast<int>(plotFlag));
}

static Variable SetPlotFlag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto nPlotFlag = getInt(args, 1);

    // Transform
    bool plotFlag = static_cast<bool>(nPlotFlag);

    // Execute
    oTarget->setPlotFlag(plotFlag);
    return Variable::ofNull();
}

static Variable SetDialogPlaceableCamera(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCameraId = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.setDialogPlaceableCamera(nCameraId);
    return Variable::ofNull();
}

static Variable GetSoloMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool solo = ctx.game.party().isSoloMode();
    return Variable::ofInt(static_cast<int>(solo));
}

static Variable GetMaxStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    return Variable::ofInt(area ? area->maxStealthXP() : 0);
}

static Variable SetMaxStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMax = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.module()->area()->setMaxStealthXP(nMax);
    return Variable::ofNull();
}

static Variable GetCurrentStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    int xp = ctx.game.module()->area()->currentStealthXP();
    return Variable::ofInt(xp);
}

static Variable GetNumStackedItems(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    if (args.empty() || args[0].type != VariableType::Object) {
        return Variable::ofInt(-1);
    }

    // Transform
    uint32_t objectId = args[0].objectId;
    if (objectId == kObjectSelf) {
        if (const Variable *caller = ctx.execution.findArg(ArgKind::Caller)) {
            objectId = caller->objectId;
        } else {
            objectId = kObjectInvalid;
        }
    }
    auto object = ctx.game.getObjectById(objectId);
    if (!object || object->type() != ObjectType::Item) {
        return Variable::ofInt(-1);
    }
    auto item = std::static_pointer_cast<Item>(object);

    // Execute
    return Variable::ofInt(item->stackSize());
}

static Variable SurrenderToEnemies(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute

    // K2 behaviour: the caller and every creature it is hostile to within
    // 250 m stop fighting (design note c08_effects).
    if (auto caller = dyn_cast<Creature>(getCaller(ctx))) {
        caller->surrender(/*retainOwnEffects=*/false);
    }
    return Variable::ofNull();
}

static Variable SetCurrentStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCurrent = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.module()->area()->setCurrentStealthXP(nCurrent);
    return Variable::ofNull();
}

static Variable GetCreatureSize(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Execute: CREATURE_SIZE_* (appearance.2da sizecategory); 0 (invalid)
    // for anything that is not a creature.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature ? static_cast<int>(creature->size()) : 0);
}

static Variable AwardStealthXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);

    // Transform

    // Execute
    // Only a creature target counts. The area's pool goes to the party once.
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (asCreature(oTarget) && area) {
        int amount = area->awardStealthXP();
        if (amount > 0) {
            ctx.game.party().awardXP(amount, XPSource::Stealth);
        }
    }
    return Variable::ofNull();
}

static Variable GetStealthXPEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    return Variable::ofInt(area && area->isStealthXPEnabled() ? 1 : 0);
}

static Variable SetStealthXPEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bEnabled = getInt(args, 0);

    // Transform
    bool enabled = static_cast<bool>(bEnabled);

    // Execute
    ctx.game.module()->area()->setStealthXPEnabled(enabled);
    return Variable::ofNull();
}

static Variable GetLastTrapDetected(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto area = ctx.game.module() ? ctx.game.module()->area() : nullptr;
    if (!area || !oTarget) {
        return Variable::ofObject(kObjectInvalid);
    }
    uint32_t trapId = area->lastTrapDetected(oTarget->id());
    return Variable::ofObject(getObjectIdOrInvalid(ctx.game.getObjectById(trapId)));
}

static Variable GetNearestTrapToObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);
    auto nTrapDetected = getIntOrElse(args, 1, 1);

    // Execute
    auto area = ctx.game.module() ? ctx.game.module()->area() : nullptr;
    if (!area || !oTarget) {
        return Variable::ofObject(kObjectInvalid);
    }
    std::shared_ptr<Trigger> nearest;
    float nearestDistance2 = std::numeric_limits<float>::max();
    for (const auto &object : area->getObjectsByType(ObjectType::Trigger)) {
        auto trap = std::static_pointer_cast<Trigger>(object);
        if (!trap->isTrap() || (nTrapDetected && !trap->isTrapDetectedBy(*oTarget))) {
            continue;
        }
        float distance2 = oTarget->getSquareDistanceTo(trap->trapCenter());
        if (distance2 < nearestDistance2) {
            nearestDistance2 = distance2;
            nearest = trap;
        }
    }
    return Variable::ofObject(getObjectIdOrInvalid(nearest));
}

static Variable GetAttemptedMovementTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // The object the caller is currently walking to, if any.
    auto caller = getCaller(ctx);
    if (!caller->actions().empty()) {
        if (auto move = dyn_cast<MoveToObjectAction>(caller->actions().front().get())) {
            if (move->target() && ctx.game.isRuntimeObjectLive(*move->target())) {
                return Variable::ofObject(move->target()->id());
            }
        }
    }
    return Variable::ofObject(kObjectInvalid);
}

static Variable GetBlockingCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = asCreature(oTarget);
    return Variable::ofObject(creature ? creature->blockingCreature() : kObjectInvalid);
}

static Variable GetFortitudeSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: the creature's base saving throw bonus, 0 for anything else.
    auto creature = asCreature(oTarget);
    return Variable::ofInt(creature ? creature->getSavingThrow(1) : 0);
}

static Variable GetWillSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: the creature's base saving throw bonus, 0 for anything else.
    auto creature = asCreature(oTarget);
    return Variable::ofInt(creature ? creature->getSavingThrow(3) : 0);
}

static Variable GetReflexSavingThrow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: the creature's base saving throw bonus, 0 for anything else.
    auto creature = asCreature(oTarget);
    return Variable::ofInt(creature ? creature->getSavingThrow(2) : 0);
}

static Variable GetChallengeRating(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute
    auto creature = asCreature(oCreature);
    return Variable::ofFloat(creature ? creature->challengeRating() : 0.0f);
}

static Variable GetFoundEnemyCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = asCreature(oTarget);
    return Variable::ofObject(creature ? creature->foundEnemyCreature() : kObjectInvalid);
}

static Variable GetMovementRate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute: MOVEMENT_SPEED_* (immobile 1, very slow 2, slow 3, normal 4, fast 5,
    // very fast 6) from the creature's speed factor; 4 for anything but a creature.
    auto creature = asCreature(oCreature);
    if (!creature) {
        return Variable::ofInt(4);
    }
    float factor = creature->getMovementSpeedFactor();
    int rate = factor <= 0.0f ? 1 : factor < 0.5f ? 2 : factor < 1.0f ? 3 : factor <= 1.0f ? 4 : factor <= 1.5f ? 5 : 6;
    return Variable::ofInt(rate);
}

static Variable GetSubRace(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->subrace()));
}

static Variable GetStealthXPDecrement(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    return Variable::ofInt(area ? area->stealthXPDecrement() : 0);
}

static Variable SetStealthXPDecrement(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nDecrement = getInt(args, 0);

    // Execute
    ctx.game.module()->area()->setStealthXPDecrement(nDecrement);
    return Variable::ofNull();
}

static Variable DuplicateHeadAppearance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCreatureToChange = getObject(args, 0, ctx);
    auto oidCreatureToMatch = getObject(args, 1, ctx);

    // Transform
    auto target = dyn_cast<Creature>(oidCreatureToChange);
    auto source = dyn_cast<Creature>(oidCreatureToMatch);

    // Execute
    // The first creature shows the second creature's head (heads.2da row).
    if (target && source) {
        int head = source->headRow();
        if (head >= 0) {
            target->setHeadOverride(head);
        }
    }
    return Variable::ofNull();
}

static Variable CutsceneAttack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto nAnimation = getInt(args, 1);
    auto nAttackResult = getInt(args, 2);
    auto nDamage = getInt(args, 3);

    // Transform
    auto attackResult = static_cast<AttackResultType>(nAttackResult);

    // Execute
    auto caller = checkCreature(getCaller(ctx));

    auto action = ctx.game.newAction<CutsceneAttackAction>(
        std::move(oTarget), nAnimation, attackResult, nDamage);
    caller->addAction(std::move(action));
    return Variable::ofNull();
}

static Variable SetCameraMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oPlayer = getObject(args, 0, ctx);
    auto nCameraMode = getInt(args, 1);
    (void)oPlayer;
    (void)nCameraMode;

    // Execute
    // intentionally stubbed: camera modes are chosen by the engine's own camera logic; a script cannot force one
    return Variable::ofNull();
}

static Variable SetLockOrientationInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    if (oObject && oObject->type() == ObjectType::Creature) {
        DialogLocks::set(ctx.game.dialogLocks().orientation, oObject->id(), nValue != 0);
    }
    return Variable::ofNull();
}

static Variable SetLockHeadFollowInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    if (oObject && oObject->type() == ObjectType::Creature) {
        DialogLocks::set(ctx.game.dialogLocks().headFollow, oObject->id(), nValue != 0);
    }
    return Variable::ofNull();
}

static Variable CutsceneMove(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto vPosition = getVector(args, 1);
    auto nRun = getInt(args, 2);

    // Execute: the creature walks or runs to the point, as ActionMoveToLocation does.
    if (auto creature = asCreature(oObject)) {
        auto action = ctx.game.newAction<MoveToLocationAction>(std::make_shared<Location>(vPosition, 0.0f), nRun != 0);
        creature->addAction(std::move(action));
    }
    return Variable::ofNull();
}

static Variable EnableVideoEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEffectType = getInt(args, 0);

    // Execute: presentation only (videoeffects.2da row), sticky until disabled.
    ctx.game.enableVideoEffect(nEffectType);
    return Variable::ofNull();
}

static Variable StartNewModule(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sModuleName = getString(args, 0);
    auto sWayPoint = getStringOrElse(args, 1, "");
    auto sMovie1 = getStringOrElse(args, 2, "");
    auto sMovie2 = getStringOrElse(args, 3, "");
    auto sMovie3 = getStringOrElse(args, 4, "");
    auto sMovie4 = getStringOrElse(args, 5, "");
    auto sMovie5 = getStringOrElse(args, 6, "");
    auto sMovie6 = getStringOrElse(args, 7, "");

    // Transform
    auto moduleName = boost::to_lower_copy(sModuleName);
    auto waypoint = boost::to_lower_copy(sWayPoint);
    std::vector<std::string> movies;
    auto addMovie = [&movies](const std::string &movie) {
        if (!movie.empty()) {
            movies.push_back(boost::to_lower_copy(movie));
        }
    };
    addMovie(sMovie1);
    addMovie(sMovie2);
    addMovie(sMovie3);
    addMovie(sMovie4);
    addMovie(sMovie5);
    addMovie(sMovie6);

    // Execute
    if (movies.empty()) {
        ctx.game.scheduleModuleTransition(moduleName, waypoint);
    } else {
        ctx.game.scheduleModuleTransitionWithMovies(moduleName, waypoint, std::move(movies));
    }
    return Variable::ofNull();
}

static Variable DisableVideoEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.disableVideoEffect();
    return Variable::ofNull();
}

static Variable GetWeaponRanged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oItem = getObjectOrNull(args, 0, ctx);

    // Execute
    auto item = oItem ? dyn_cast<Item>(oItem) : nullptr;
    return Variable::ofInt(item && item->isRanged() ? 1 : 0);
}

static Variable DoSinglePlayerAutoSave(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.requestAutoSave();
    return Variable::ofNull();
}

static Variable GetGameDifficulty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The player's difficulty option (difficultyopt.2da row: 0 Easy, 1 Normal,
    // 2 Difficult), as the game returns it.
    return Variable::ofInt(static_cast<int>(ctx.game.options().game.clientDifficulty));
}

static Variable GetUserActionsPending(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = checkCreature(getCaller(ctx));
    return Variable::ofInt(static_cast<int>(caller->hasUserActionsPending()));
}

static Variable RevealMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vPoint = getVectorOrElse(args, 0, glm::vec3(0.0f, 0.0f, 0.0f));
    auto nRadius = getIntOrElse(args, 1, -1);

    // Transform

    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (area) {
        if (nRadius < 0) {
            area->mapExploration().revealAll();
        } else {
            area->mapExploration().reveal(area->worldToMapPoint(glm::vec2(vPoint)), nRadius);
        }
    }
    return Variable::ofNull();
}

static Variable SetTutorialWindowsEnabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bEnabled = getInt(args, 0);

    // Execute
    ctx.game.setTutorialWindowsEnabled(bEnabled != 0);
    return Variable::ofNull();
}

static Variable ShowTutorialWindow(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nWindow = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.showTutorialWindow(nWindow);
    return Variable::ofNull();
}

static Variable StartCreditSequence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bTransparentBackground = getInt(args, 0);

    // Execute
    ctx.game.startCreditSequence(bTransparentBackground != 0);
    return Variable::ofNull();
}

static Variable IsCreditSequenceInProgress(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.isCreditSequenceInProgress() ? 1 : 0);
}

// Actions that first walk the actor up to a target, then act on it.
static bool isApproachAction(ActionType type) {
    switch (type) {
    case ActionType::OpenDoor:
    case ActionType::CloseDoor:
    case ActionType::OpenLock:
    case ActionType::UseObject:
    case ActionType::PickUpItem:
    case ActionType::DisableTrap:
    case ActionType::RecoverTrap:
    case ActionType::FlagTrap:
    case ActionType::ExamineTrap:
    case ActionType::OpenContainer:
    case ActionType::UseSkill:
    case ActionType::InteractObject:
    case ActionType::StartConversation:
        return true;
    default:
        return false;
    }
}

static Variable GetCurrentAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Transform

    // Execute
    auto action = oObject->getCurrentAction();
    if (!action) {
        return Variable::ofInt(static_cast<int>(ActionType::QueueEmpty));
    }
    // Retail walks a creature to its target as a move-to-point step at the
    // front of its queue, so while it is still on the way the current action
    // reads ACTION_MOVETOPOINT. The party heartbeat relies on this: it leaves a
    // companion that is walking somewhere alone and resets anyone else to
    // follow the leader, which otherwise drops an order the player gave (a
    // companion sent to pick a lock).
    if (isApproachAction(action->type())) {
        if (auto creature = dyn_cast<Creature>(oObject.get());
            creature && creature->movementType() != Creature::MovementType::None) {
            return Variable::ofInt(static_cast<int>(ActionType::MoveToPoint));
        }
    }
    return Variable::ofInt(static_cast<int>(action->type()));
}

static Variable GetDifficultyModifier(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The difficultyopt.2da multiplier of the player's difficulty option.
    return Variable::ofFloat(ctx.services.game.difficultyOptions
                                 .get(static_cast<int>(ctx.game.options().game.clientDifficulty))
                                 .damageMultiplier);
}

static Variable GetAppearanceType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Execute: the appearance.2da row; 0 when not a creature.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature ? creature->appearance() : 0);
}

static Variable FloatingTextStrRefOnCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRefToDisplay = getInt(args, 0);
    auto oCreatureToFloatAbove = getObjectOrNull(args, 1, ctx);
    auto bBroadcastToFaction = getIntOrElse(args, 2, 1);
    (void)bBroadcastToFaction; // one local player: nobody else to broadcast to

    // Execute
    if (asCreature(oCreatureToFloatAbove)) {
        ctx.game.floatingText().addMessage(*oCreatureToFloatAbove, ctx.services.resource.strings.getText(nStrRefToDisplay));
    }
    return Variable::ofNull();
}

static Variable FloatingTextStringOnCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sStringToDisplay = getString(args, 0);
    auto oCreatureToFloatAbove = getObjectOrNull(args, 1, ctx);
    auto bBroadcastToFaction = getIntOrElse(args, 2, 1);
    (void)bBroadcastToFaction; // one local player: nobody else to broadcast to

    // Execute
    if (asCreature(oCreatureToFloatAbove)) {
        ctx.game.floatingText().addMessage(*oCreatureToFloatAbove, sStringToDisplay);
    }
    return Variable::ofNull();
}

static Variable GetTrapDisarmable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields && fields->disarmable ? 1 : 0);
}

static Variable GetTrapDetectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields && fields->detectable ? 1 : 0);
}

static Variable GetTrapDetectedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);
    auto oCreature = getObjectOrNull(args, 1, ctx);

    // Execute
    auto trap = std::dynamic_pointer_cast<Trigger>(oTrapObject);
    return Variable::ofInt(trap && oCreature && trap->isTrapDetectedBy(*oCreature) ? 1 : 0);
}

static Variable GetTrapFlagged(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto trap = std::dynamic_pointer_cast<Trigger>(oTrapObject);
    return Variable::ofInt(trap && trap->isTrapFlagged() ? 1 : 0);
}

static Variable GetTrapBaseType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields ? fields->baseType : -1);
}

static Variable GetTrapOneShot(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields && fields->oneShot ? 1 : 0);
}

static Variable GetTrapCreator(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto trap = std::dynamic_pointer_cast<Trigger>(oTrapObject);
    if (!trap) {
        return Variable::ofObject(kObjectInvalid);
    }
    return Variable::ofObject(getObjectIdOrInvalid(ctx.game.getObjectById(trap->trapCreatorId())));
}

static Variable GetTrapKeyTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofString(fields ? fields->keyTag : "");
}

static Variable GetTrapDisarmDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields ? fields->disarmDC : 0);
}

static Variable GetTrapDetectDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrapObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oTrapObject);
    return Variable::ofInt(fields ? fields->detectDC : 0);
}

static Variable GetLockKeyRequired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (auto door = std::dynamic_pointer_cast<Door>(oObject)) {
        return Variable::ofInt(static_cast<int>(door->isKeyRequired()));
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(oObject)) {
        return Variable::ofInt(static_cast<int>(placeable->isKeyRequired()));
    }
    return Variable::ofInt(0);
}

static Variable GetLockKeyTag(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (auto door = std::dynamic_pointer_cast<Door>(oObject)) {
        return Variable::ofString(door->keyName());
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(oObject)) {
        return Variable::ofString(placeable->keyName());
    }
    return Variable::ofString("");
}

static Variable GetLockLockable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (auto door = std::dynamic_pointer_cast<Door>(oObject)) {
        return Variable::ofInt(static_cast<int>(door->isLockable()));
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(oObject)) {
        return Variable::ofInt(static_cast<int>(placeable->isLockable()));
    }
    return Variable::ofInt(0);
}

static Variable GetLockUnlockDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (auto door = std::dynamic_pointer_cast<Door>(oObject)) {
        return Variable::ofInt(door->openLockDC());
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(oObject)) {
        return Variable::ofInt(placeable->openLockDC());
    }
    return Variable::ofInt(0);
}

static Variable GetLockLockDC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    if (auto door = std::dynamic_pointer_cast<Door>(oObject)) {
        return Variable::ofInt(door->closeLockDC());
    }
    if (auto placeable = std::dynamic_pointer_cast<Placeable>(oObject)) {
        return Variable::ofInt(placeable->closeLockDC());
    }
    return Variable::ofInt(0);
}

static Variable GetPCLevellingUp(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the member the level-up screen is open for, invalid otherwise.
    auto member = ctx.game.levelUpInProgress();
    return Variable::ofObject(member ? member->id() : script::kObjectInvalid);
}

static Variable GetHasFeatEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);
    auto oObject = getObjectOrCaller(args, 1, ctx);

    // Execute: whether the creature is in the combat mode that feat starts.
    auto creature = asCreature(oObject);
    return Variable::ofInt(creature && static_cast<int>(creature->activeCombatFeat()) == nFeat ? 1 : 0);
}

static Variable SetPlaceableIllumination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrCaller(args, 0, ctx);
    auto bIlluminate = getIntOrElse(args, 1, 1);

    // Execute: switches the placeable's light state on or off.
    if (auto placeable = dyn_cast<Placeable>(oPlaceable)) {
        placeable->setIlluminated(bIlluminate != 0);
    }
    return Variable::ofNull();
}

static Variable GetPlaceableIllumination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto placeable = dyn_cast<Placeable>(oPlaceable);
    return Variable::ofInt(placeable && placeable->isIlluminated() ? 1 : 0);
}

static Variable GetIsPlaceableObjectActionPossible(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrNull(args, 0, ctx);
    auto nPlaceableAction = getInt(args, 1);

    // Execute: PLACEABLE_ACTION_USE (0) when it can be used, UNLOCK (1) and KNOCK (3)
    // when it is locked, BASH (2) when it is locked and can be damaged.
    auto placeable = dyn_cast<Placeable>(oPlaceable);
    if (!placeable || placeable->isDead()) {
        return Variable::ofInt(0);
    }
    bool possible = false;
    switch (nPlaceableAction) {
    case 0:
        possible = placeable->isUsable() && !placeable->isLocked();
        break;
    case 1:
    case 3:
        possible = placeable->isUsable() && placeable->isLocked();
        break;
    case 2:
        possible = placeable->isLocked() && !placeable->plotFlag() && !placeable->isNotBlastable();
        break;
    default:
        break;
    }
    return Variable::ofInt(possible ? 1 : 0);
}

static Variable DoPlaceableObjectAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObjectOrNull(args, 0, ctx);
    auto nPlaceableAction = getInt(args, 1);

    // Execute: the caller queues the action on the placeable: USE (0) interacts with
    // it, UNLOCK (1) picks the lock then uses it, BASH (2) attacks it, KNOCK (3)
    // unlocks it outright.
    auto placeable = dyn_cast<Placeable>(oPlaceable);
    auto caller = asCreature(getCaller(ctx));
    if (!placeable || !caller) {
        return Variable::ofNull();
    }
    switch (nPlaceableAction) {
    case 0:
        caller->addAction(ctx.game.newAction<InteractObjectAction>(placeable));
        break;
    case 1:
        caller->addAction(ctx.game.newAction<UseSkillAction>(SkillType::Security, oPlaceable, 0, nullptr));
        caller->addAction(ctx.game.newAction<InteractObjectAction>(placeable));
        break;
    case 2:
        caller->addAction(ctx.game.newAction<AttackObjectAction>(oPlaceable, false));
        break;
    case 3:
        caller->addAction(ctx.game.newAction<UnlockObjectAction>(oPlaceable));
        break;
    default:
        break;
    }
    return Variable::ofNull();
}

static Variable GetFirstPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto player = ctx.game.party().player();
    return Variable::ofObject(getObjectIdOrInvalid(player));
}

static Variable GetNextPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofObject(kObjectInvalid);
}

static Variable SetTrapDetectedBy(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrap = getObjectOrNull(args, 0, ctx);
    auto oDetector = getObjectOrNull(args, 1, ctx);

    // Execute
    auto trap = std::dynamic_pointer_cast<Trigger>(oTrap);
    if (!trap || !oDetector) {
        return Variable::ofInt(0);
    }
    trap->setTrapDetectedBy(oDetector->id(), ctx.game.party().isMember(*oDetector));
    return Variable::ofInt(1);
}

static Variable GetIsTrapped(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute
    auto fields = getTrapFields(oObject);
    return Variable::ofInt(fields && fields->trapped ? 1 : 0);
}

static Variable SetEffectIcon(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto eEffect = getEffect(args, 0);
    auto nIcon = getInt(args, 1);

    // Transform

    // Execute
    // The icon (an effecticon.2da row) travels with the effect value; the
    // portrait/character-sheet display of it is not implemented yet.
    if (eEffect) {
        eEffect->setEffectIcon(nIcon);
    }
    return Variable::ofEffect(eEffect);
}

static Variable FaceObjectAwayFromObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFacer = getObject(args, 0, ctx);
    auto oObjectToFaceAwayFrom = getObject(args, 1, ctx);

    // Transform

    // Execute
    oFacer->faceAwayFrom(*oObjectToFaceAwayFrom);
    return Variable::ofNull();
}

static Variable PopUpDeathGUIPanel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oPC = getObject(args, 0, ctx);
    (void)oPC;

    // Execute
    // intentionally stubbed: the death panel is the engine's own game-over flow, with no per-call button set
    return Variable::ofNull();
}

static Variable SetTrapDisabled(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTrap = getObjectOrNull(args, 0, ctx);

    // Execute
    if (auto trap = std::dynamic_pointer_cast<Trigger>(oTrap)) {
        trap->disableTrap();
    } else if (auto door = std::dynamic_pointer_cast<Door>(oTrap)) {
        door->disableTrap();
    } else if (auto placeable = std::dynamic_pointer_cast<Placeable>(oTrap)) {
        placeable->disableTrap();
    }
    return Variable::ofNull();
}

static Variable GetLastHostileActor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oVictim = getObjectOrCaller(args, 0, ctx);

    // Execute
    uint32_t actorId = oVictim->getLastHostileActor();
    if (actorId != script::kObjectInvalid) {
        auto actor = ctx.game.getObjectById<Creature>(actorId);
        if (!actor || actor->isDead() || actor->isTemporarilyDead()) {
            actorId = script::kObjectInvalid;
            oVictim->setLastHostileActor(actorId);
        }
    }
    return Variable::ofObject(actorId);
}

static Variable ExportAllCharacters(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // intentionally stubbed: exports party characters to the multiplayer vault, which this engine has none of
    return Variable::ofNull();
}

static Variable MusicBackgroundGetDayTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    return Variable::ofInt(area ? area->musicState().day().row : 0);
}

static Variable MusicBackgroundGetNightTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    return Variable::ofInt(area ? area->musicState().night().row : 0);
}

static Variable WriteTimestampedLogEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sLogEntry = getString(args, 0);

    // Execute: the engine log stamps every line itself.
    info(sLogEntry, LogChannel::Script);
    return Variable::ofNull();
}

static Variable GetModuleName(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The module's authored Mod_Name, not its resource name: shipped scripts
    // compare the result case-sensitively and take substrings of it. Scripts do
    // not run without a module, so the empty string here is only defensive.
    auto module = ctx.game.module();
    return Variable::ofString(module ? module->localizedName() : "");
}

static Variable GetFactionLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oFactionMember = getObjectOrNull(args, 0, ctx);

    // Transform
    auto faction = oFactionMember ? getObjectFaction(*oFactionMember) : std::nullopt;
    if (!faction) {
        return Variable::ofObject(script::kObjectInvalid);
    }

    // Execute: the party leader for the player's faction, else the first
    // member in area order.
    if (*faction == Faction::Player) {
        if (auto leader = ctx.game.party().getLeader()) {
            return Variable::ofObject(leader->id());
        }
    }
    auto members = collectFactionMembers(*faction, false, ctx);
    return Variable::ofObject(members.empty() ? script::kObjectInvalid : members.front()->id());
}

static Variable EndGame(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nShowEndGameGui = getIntOrElse(args, 0, 1);

    // Transform

    // Execute
    // Ends the running game and returns to the start screen. K1 has no death
    // screen to show, so the flag changes nothing.
    ctx.game.requestEndGame();
    return Variable::ofNull();
}

static Variable GetRunScriptVar(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    if (const Variable *var = ctx.execution.findArg(ArgKind::ScriptVar)) {
        return *var;
    }
    return Variable::ofInt(-1);
}

static Variable GetCreatureMovmentType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCreature = getObject(args, 0, ctx);

    // Execute: the creature's MOVEMENT_SPEED_* rate (its template's
    // MovementRate); MOVEMENT_SPEED_IMMOBILE (1) when not a creature.
    static constexpr int kMovementSpeedImmobile = 1;
    auto creature = asCreature(oidCreature);
    return Variable::ofInt(creature ? creature->walkRate() : kMovementSpeedImmobile);
}

static Variable AmbientSoundSetDayVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setAmbientDayVolume(nVolume);
        area->refreshMusic();
    }
    return Variable::ofNull();
}

static Variable AmbientSoundSetNightVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto nVolume = getInt(args, 1);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    if (area) {
        area->musicState().setAmbientNightVolume(nVolume);
        area->refreshMusic();
    }
    return Variable::ofNull();
}

static Variable MusicBackgroundGetBattleTrack(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);

    // Transform
    auto area = dyn_cast<Area>(oArea);

    // Execute
    return Variable::ofInt(area ? area->musicState().battleTrack().row : 0);
}

static Variable GetHasInventory(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrCaller(args, 0, ctx);

    // Execute: creatures, stores, and placeables authored with an inventory.
    bool has = false;
    if (oObject) {
        if (auto placeable = dyn_cast<Placeable>(oObject)) {
            has = placeable->hasInventory();
        } else {
            has = oObject->type() == ObjectType::Creature || oObject->type() == ObjectType::Store;
        }
    }
    return Variable::ofInt(has ? 1 : 0);
}

static Variable GetStrRefSoundDuration(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);

    // Execute: the length in seconds of the sound a string reference names; 0 when it has none.
    auto sound = ctx.services.resource.strings.getSound(nStrRef);
    if (sound.empty()) {
        return Variable::ofFloat(0.0f);
    }
    auto clip = ctx.services.resource.audioClips.get(sound);
    return Variable::ofFloat(clip ? clip->duration() : 0.0f);
}

// AddToParty/RemoveFromParty act on the party of networked players, not on
// the companion party (design note c03_stats, "Party routines"). A creature
// counts only if a player controls it: here, the one local player character.
static bool isPlayerControlled(const std::shared_ptr<Creature> &creature, const RoutineContext &ctx) {
    return creature && creature == ctx.game.party().actualPlayer();
}

static Variable AddToParty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPC = getObjectOrNull(args, 0, ctx);
    auto oPartyLeader = getObjectOrNull(args, 1, ctx);

    // Transform
    auto pc = asCreature(oPC);
    auto leader = asCreature(oPartyLeader);

    // Execute: joining takes two different players. With one local player
    // (Janice's droids on Taris are creatures, not players) the companion
    // party stays as it is.
    if (isPlayerControlled(pc, ctx) && isPlayerControlled(leader, ctx) && pc != leader) {
        debug("AddToParty: both players already share the local party");
    }
    return Variable::ofNull();
}

static Variable RemoveFromParty(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPC = getObjectOrNull(args, 0, ctx);

    // Transform
    auto pc = asCreature(oPC);

    // Execute: the lone local player leaves a one-player party, which changes
    // no companion membership (k_psta_partyleav destroys the companions
    // itself).
    if (isPlayerControlled(pc, ctx)) {
        debug("RemoveFromParty: the local player remains the only player");
    }
    return Variable::ofNull();
}

static Variable AddPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oCreature = getObject(args, 1, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    bool added = ctx.game.party().addMember(nNPC, creature);
    return Variable::ofInt(static_cast<int>(added));
}

static Variable RemovePartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool removed = ctx.game.party().removeMember(nNPC);
    return Variable::ofInt(static_cast<int>(removed));
}

static Variable IsObjectPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    bool member = ctx.game.party().isMember(*creature);
    return Variable::ofInt(static_cast<int>(member));
}

static Variable GetPartyMemberByIndex(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Transform

    // Execute
    auto member = ctx.game.party().getMember(nIndex);
    // Auto-combat (P1 item 10): the generic AI skips party member 0, so while
    // it decides the leader's round, index 0 reads as the next member.
    if (nIndex == 0 && member && ctx.game.autoCombatScriptCaller() == member->id()) {
        member = ctx.game.party().getMember(1);
    }
    return Variable::ofObject(getObjectIdOrInvalid(member));
}

static Variable GetGlobalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    bool value = ctx.game.getGlobalBoolean(sIdentifier);
    return Variable::ofInt(static_cast<int>(value));
}

static Variable SetGlobalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalBoolean(sIdentifier, nValue);
    return Variable::ofNull();
}

static Variable GetGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    int value = ctx.game.getGlobalNumber(sIdentifier);
    return Variable::ofInt(value);
}

static Variable SetGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nValue = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalNumber(sIdentifier, nValue);
    return Variable::ofNull();
}

static Variable AurPostString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sString = getString(args, 0);
    auto nX = getInt(args, 1);
    auto nY = getInt(args, 2);
    auto fLife = getFloat(args, 3);

    // Execute
    // A debug overlay in retail. Reone has no free-positioned screen text, so
    // the text goes to the log and the console, where the harness sees it.
    std::string line = str(boost::format("AurPostString(%d,%d,%.1f): %s") % nX % nY % fLife % sString);
    info(line, LogChannel::Script);
    ctx.game.console().printLine(line);
    return Variable::ofNull();
}

static Variable AddJournalWorldEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);
    auto szEntry = getString(args, 1);
    auto szTitle = getStringOrElse(args, 2, "World Entry");

    // Execute: kept in the journal's world notes, replacing the entry with that index.
    Journal::WorldEntry entry;
    entry.index = nIndex;
    entry.text = szEntry;
    entry.title = szTitle;
    ctx.game.journal().addWorldEntry(std::move(entry));
    return Variable::ofNull();
}

static Variable AddJournalWorldEntryStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto strref = getInt(args, 0);
    auto strrefTitle = getInt(args, 1);

    // Execute: the strings are resolved when added; the entry's index is its text's string reference.
    Journal::WorldEntry entry;
    entry.index = strref;
    entry.strref = strref;
    entry.titleStrref = strrefTitle;
    entry.text = ctx.services.resource.strings.getText(strref);
    entry.title = ctx.services.resource.strings.getText(strrefTitle);
    ctx.game.journal().addWorldEntry(std::move(entry));
    return Variable::ofNull();
}

static Variable BarkString(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto strRef = getInt(args, 1);
    // TSL only: bubble position, not modelled.
    auto nBarkX = getIntOrElse(args, 2, -1);
    auto nBarkY = getIntOrElse(args, 3, -1);

    // Execute
    std::string text = ctx.services.resource.strings.getText(strRef);
    if (text.empty()) {
        return Variable::ofNull();
    }
    float duration = barkDuration(text);
    info(str(boost::format("BarkString: %s: %s") % (oCreature ? oCreature->tag() : std::string("(general)")) % text));
    ctx.game.setBarkBubbleText(text, duration);
    return Variable::ofNull();
}

static Variable DeleteJournalWorldAllEntries(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.journal().clearWorldEntries();
    return Variable::ofNull();
}

static Variable DeleteJournalWorldEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Execute
    ctx.game.journal().removeWorldEntry(nIndex);
    return Variable::ofNull();
}

static Variable DeleteJournalWorldEntryStrref(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto strref = getInt(args, 0);

    // Execute
    ctx.game.journal().removeWorldEntryByStrref(strref);
    return Variable::ofNull();
}

static Variable PlayVisualAreaEffect(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nEffectID = getInt(args, 0);
    auto lTarget = getLocationArgument(args, 1);

    // Transform
    auto module = ctx.game.module();

    // Execute
    // The visual effect plays once at the location, like an instant visual
    // effect applied at a location.
    if (module && module->area()) {
        auto effect = ctx.game.newEffect<VisualEffect>(nEffectID, false, ctx.services);
        effect->setLocation(lTarget->position());
        float duration = effect->duration();
        module->area()->applyEffect(
            effect,
            duration > 0.0f ? DurationType::Temporary : DurationType::Instant,
            duration);
    }
    return Variable::ofNull();
}

static Variable SetJournalQuestEntryPicture(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto szPlotID = getString(args, 0);
    auto oObject = getObjectOrNull(args, 1, ctx);
    auto nPictureIndex = getInt(args, 2);
    auto bAllPartyMemebers = getIntOrElse(args, 3, 1);
    auto bAllPlayers = getIntOrElse(args, 4, 0);
    (void)oObject;
    (void)bAllPartyMemebers;
    (void)bAllPlayers; // one journal for the whole party

    // Execute: remembers the picture index for the quest; the journal screens show no pictures.
    ctx.game.journal().setEntryPicture(szPlotID, nPictureIndex);
    return Variable::ofNull();
}

static Variable GetLocalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);

    // Transform

    // Execute
    bool value = oObject->getLocalBoolean(nIndex);
    return Variable::ofInt(static_cast<int>(value));
}

static Variable SetLocalBoolean(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);
    auto nValue = getInt(args, 2);

    // Transform

    // Execute
    oObject->setLocalBoolean(nIndex, nValue);
    return Variable::ofNull();
}

static Variable GetLocalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);

    // Transform

    // Execute
    return Variable::ofInt(oObject->getLocalNumber(nIndex));
}

static Variable SetLocalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nIndex = getInt(args, 1);
    auto nValue = getInt(args, 2);

    // Transform

    // Execute
    oObject->setLocalNumber(nIndex, nValue);
    return Variable::ofNull();
}

static Variable SoundObjectGetPitchVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    // A non-sound object answers 0.
    return Variable::ofFloat(sound ? sound->pitchVariation() : 0);
}

static Variable SoundObjectSetPitchVariance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fVariance = getFloat(args, 1);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    if (sound) {
        sound->setPitchVariation(fVariance);
    }
    return Variable::ofNull();
}

static Variable SoundObjectGetVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    // A non-sound object answers 0.
    return Variable::ofInt(sound ? sound->volume() : 0);
}

static Variable GetGlobalLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);

    // Transform

    // Execute
    return Variable::ofLocation(ctx.game.getGlobalLocation(sIdentifier));
}

static Variable SetGlobalLocation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto lValue = getLocationArgument(args, 1);

    // Transform

    // Execute
    ctx.game.setGlobalLocation(sIdentifier, lValue);
    return Variable::ofNull();
}

static Variable AddAvailableNPCByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oCreature = getObject(args, 1, ctx);

    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Npc, nNPC}, creature));
}

static Variable RemoveAvailableNPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool removed = ctx.game.party().removeAvailableMember(nNPC);
    return Variable::ofInt(static_cast<int>(removed));
}

static Variable IsAvailableCreature(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool available = ctx.game.party().isMemberAvailable(nNPC);
    return Variable::ofInt(static_cast<int>(available));
}

static Variable AddAvailableNPCByTemplate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto sTemplate = getString(args, 1);

    // Transform
    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    bool added = ctx.game.party().addAvailableMember(nNPC, tmplt);
    return Variable::ofInt(static_cast<int>(added));
}

static Variable SpawnAvailableNPC(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto lPosition = getLocationArgument(args, 1);

    // Execute
    auto member = ctx.game.party().getAvailableMember(nNPC, true);
    if (!member) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return Variable::ofObject(kObjectInvalid);
    }
    member->setPosition(lPosition->position());
    member->setFacing(lPosition->facing());
    area->landObject(*member);
    const bool alreadyResident = std::find(
        area->objects().begin(), area->objects().end(), member) !=
        area->objects().end();
    if (!alreadyResident) {
        area->add(member);
        member->runSpawnScript();
    }
    return Variable::ofObject(member->id());
}

static Variable IsNPCPartyMember(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    bool member = ctx.game.party().isMember(nNPC);
    return Variable::ofInt(static_cast<int>(member));
}

static Variable GetIsConversationActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    bool active = ctx.game.isConversationActive();
    return Variable::ofInt(active);
}

static Variable GetPartyAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    return Variable::ofInt(ctx.game.party().aiStyle());
}

static Variable GetNPCAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->aiStyle()));
}

static Variable SetPartyAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStyle = getInt(args, 0);

    // Execute
    ctx.game.party().setAIStyle(nStyle);
    return Variable::ofNull();
}

static Variable SetNPCAIStyle(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nStyle = getInt(args, 1);

    // Transform
    auto creature = checkCreature(oCreature);
    auto style = static_cast<NPCAIStyle>(nStyle);

    // Execute
    creature->setAIStyle(style);
    return Variable::ofNull();
}

static Variable SetNPCSelectability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nSelectability = getInt(args, 1);

    // Execute
    ctx.game.party().setRosterSelectable(
        {RosterKind::Npc, nNPC}, nSelectability != 0);
    return Variable::ofNull();
}

static Variable GetNPCSelectability(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Execute
    const RosterIdentity identity {RosterKind::Npc, nNPC};
    if (!ctx.game.party().isRosterAvailable(identity)) {
        return Variable::ofInt(-1);
    }
    return Variable::ofInt(ctx.game.party().isRosterSelectable(identity));
}

static Variable ClearAllEffects(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto caller = getCaller(ctx);
    caller->clearAllEffects();
    return Variable::ofNull();
}

static Variable GetLastConversation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the resource name of the dialog started most recently.
    return Variable::ofString(ctx.game.lastConversation());
}

static Variable ShowPartySelectionGUI(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sExitScript = getStringOrElse(args, 0, "");
    auto nForceNPC1 = getIntOrElse(args, 1, -1);
    auto nForceNPC2 = getIntOrElse(args, 2, -1);
    auto nAllowCancel = getIntOrElse(args, 3, 0);

    // Transform
    auto exitScript = boost::to_lower_copy(sExitScript);

    // Execute
    PartySelectionContext partyCtx;
    partyCtx.exitScript = exitScript;
    partyCtx.forceNpc1 = nForceNPC1;
    partyCtx.forceNpc2 = nForceNPC2;
    ctx.game.openPartySelection(partyCtx);
    return Variable::ofNull();
}

static Variable GetStandardFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oObject);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->faction()));
}

static Variable GivePlotXP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sPlotName = getString(args, 0);
    auto nPercentage = getInt(args, 1);

    // Execute
    ctx.game.awardPlotXP(sPlotName, nPercentage);
    return Variable::ofNull();
}

static Variable GetMinOneHP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Transform

    // Execute
    return Variable::ofInt(static_cast<int>(oObject->isMinOneHP()));
}

static Variable SetMinOneHP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nMinOneHP = getInt(args, 1);

    // Transform
    auto minOneHP = static_cast<bool>(nMinOneHP);

    // Execute
    oObject->setMinOneHP(minOneHP);
    return Variable::ofNull();
}

static Variable SetGlobalFadeIn(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fWait = getFloatOrElse(args, 0, 0.0f);
    auto fLength = getFloatOrElse(args, 1, 0.0f);
    auto fR = getFloatOrElse(args, 2, 0.0f);
    auto fG = getFloatOrElse(args, 3, 0.0f);
    auto fB = getFloatOrElse(args, 4, 0.0f);

    // Transform

    // Execute
    ctx.game.globalFade().request(GlobalFade::Direction::In, fWait, fLength,
                                  {fR, fG, fB}, GlobalFade::Source::Script);
    return Variable::ofNull();
}

static Variable SetGlobalFadeOut(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fWait = getFloatOrElse(args, 0, 0.0f);
    auto fLength = getFloatOrElse(args, 1, 0.0f);
    auto fR = getFloatOrElse(args, 2, 0.0f);
    auto fG = getFloatOrElse(args, 3, 0.0f);
    auto fB = getFloatOrElse(args, 4, 0.0f);

    // Transform

    // Execute
    ctx.game.globalFade().request(GlobalFade::Direction::Out, fWait, fLength,
                                  {fR, fG, fB}, GlobalFade::Source::Script);
    return Variable::ofNull();
}

static Variable GetLastHostileTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto attacker = checkCreature(oAttacker);

    // Execute
    return Variable::ofObject(attacker->getLastHostileTarget());
}

static Variable GetLastAttackAction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Execute: the action of the attacker's last round, -1 when it had none; 0 for a non-creature.
    auto attacker = dyn_cast<Creature>(oAttacker);
    if (!attacker) {
        return Variable::ofInt(0);
    }
    auto action = attacker->getLastAttackAction();
    return Variable::ofInt(action == ActionType::QueueEmpty ? -1 : static_cast<int>(action));
}

static Variable GetLastForcePowerUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto attacker = asCreature(oAttacker);
    return Variable::ofInt(attacker ? attacker->getLastForcePower() : -1);
}

static Variable GetLastCombatFeatUsed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto attacker = checkCreature(oAttacker);

    // Execute
    return Variable::ofInt(static_cast<int>(attacker->getLastCombatFeat()));
}

static Variable GetLastAttackResult(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto attacker = checkCreature(oAttacker);

    // Execute
    return Variable::ofInt(static_cast<int>(attacker->getLastAttackResult()));
}

static Variable GetWasForcePowerSuccessful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAttacker = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto attacker = asCreature(oAttacker);
    return Variable::ofInt(attacker && attacker->wasLastForcePowerSuccessful() ? 1 : 0);
}


// GetFirstAttacker / GetNextAttacker walk this per-victim list of the creatures
// whose attempted attack target is the victim, in area order.
static std::map<uint32_t, std::vector<uint32_t>> g_pendingAttackers;

static Variable GetFirstAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto &pending = g_pendingAttackers[oCreature ? oCreature->id() : script::kObjectInvalid];
    pending.clear();
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (oCreature && area) {
        for (const auto &object : area->objects()) {
            auto creature = dyn_cast<Creature>(object);
            if (creature && !creature->isDead() && creature->id() != oCreature->id() &&
                creature->getAttemptedAttackTarget() == oCreature->id()) {
                pending.push_back(creature->id());
            }
        }
    }
    std::reverse(pending.begin(), pending.end());
    if (pending.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    uint32_t first = pending.back();
    pending.pop_back();
    return Variable::ofObject(first);
}

static Variable GetNextAttacker(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto found = g_pendingAttackers.find(oCreature ? oCreature->id() : script::kObjectInvalid);
    if (found == g_pendingAttackers.end() || found->second.empty()) {
        return Variable::ofObject(script::kObjectInvalid);
    }
    uint32_t next = found->second.back();
    found->second.pop_back();
    return Variable::ofObject(next);
}

static Variable SetFormation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oAnchor = getObject(args, 0, ctx);
    auto oCreature = getObject(args, 1, ctx);
    auto nFormationPattern = getInt(args, 2);
    auto nPosition = getInt(args, 3);

    // Execute
    // The creature walks to its formations.2da slot around the anchor.
    auto slot = formationSlot(ctx.services.resource.twoDas, nFormationPattern, nPosition,
                              oAnchor->position(), oAnchor->getFacing());
    if (slot) {
        auto destination = std::make_shared<Location>(*slot, oAnchor->getFacing());
        oCreature->addAction(ctx.game.newAction<MoveToLocationAction>(std::move(destination), true));
    }
    return Variable::ofNull();
}

static Variable SetForcePowerUnsuccessful(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nResult = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Execute: the reason (FORCE_POWER_RESULT_*) is kept on the creature; the
    // engine shows no failure feedback of its own to attach it to.
    if (oCreature) {
        oCreature->setScriptSetting("ForcePowerUnsuccessful", nResult);
    }
    return Variable::ofNull();
}

static Variable GetIsDebilitated(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Transform
    auto creature = checkCreature(oCreature);

    // Execute
    return Variable::ofInt(static_cast<int>(creature->isDebilitated()));
}

static Variable PlayMovie(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sMovie = getString(args, 0);
    auto nStreamingMusic = getIntOrElse(args, 1, 0);

    // Transform
    auto movie = boost::to_lower_copy(sMovie);

    // Execute
    ctx.game.playVideo(movie);
    return Variable::ofNull();
}

static Variable SaveNPCState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.saveNpcState(nNPC);

    return Variable::ofNull();
}

static Variable GetCategoryFromTalent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto tTalent = getTalentOrNull(args, 0);
    if (!tTalent) {
        return Variable::ofInt(0);
    }
    uint32_t category = getTalentCategory(
        ctx.services.game.spells, ctx.services.game.feats, tTalent->type(), tTalent->value());
    return Variable::ofInt(static_cast<int>(category));
}

// Every creature in the current area of faction `from` joins faction `to`;
// a surrender also clears its actions and takes it out of combat.
static void changeAreaFaction(int from, int to, bool surrender, const RoutineContext &ctx) {
    if (to <= static_cast<int>(Faction::Invalid) || to >= static_cast<int>(Faction::Last)) {
        return;
    }
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    std::vector<std::shared_ptr<Creature>> members;
    for (const auto &object : area->objects()) {
        auto creature = asCreature(object);
        if (creature && static_cast<int>(creature->faction()) == from) {
            members.push_back(std::move(creature));
        }
    }
    for (const auto &creature : members) {
        creature->setFaction(static_cast<Faction>(to));
        if (surrender) {
            creature->clearAllActions();
            creature->cancelCombat();
        }
    }
}

static Variable SurrenderByFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFactionFrom = getInt(args, 0);
    auto nFactionTo = getInt(args, 1);

    // Execute
    changeAreaFaction(nFactionFrom, nFactionTo, /*surrender=*/true, ctx);
    return Variable::ofNull();
}

static Variable ChangeFactionByFaction(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFactionFrom = getInt(args, 0);
    auto nFactionTo = getInt(args, 1);

    // Execute
    changeAreaFaction(nFactionFrom, nFactionTo, /*surrender=*/false, ctx);
    return Variable::ofNull();
}

static Variable PlayRoomAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sRoom = getString(args, 0);
    auto nAnimation = getInt(args, 1);

    // Transform
    auto module = ctx.game.module();

    // Execute
    // Loops the room model's "scriptloopNN" animation in the current area.
    // Room models step in the fixed tick with every other model root.
    if (module && module->area()) {
        module->area()->playRoomAnimation(sRoom, nAnimation);
    }
    return Variable::ofNull();
}

static Variable ShowGalaxyMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    ctx.game.openGalaxyMap(nPlanet);
    return {};
}

static Variable SetPlanetSelectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);
    auto bSelectable = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.party().galaxyMap().setSelectable(nPlanet, bSelectable != 0);
    return {};
}

static Variable GetPlanetSelectable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().selectable(nPlanet) ? 1 : 0);
}

static Variable SetPlanetAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);
    auto bAvailable = getInt(args, 1);

    // Transform

    // Execute
    ctx.game.party().galaxyMap().setAvailable(nPlanet, bAvailable != 0);
    return {};
}

static Variable GetPlanetAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPlanet = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().available(nPlanet) ? 1 : 0);
}

static Variable GetSelectedPlanet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.party().galaxyMap().selectedPlanet());
}

static Variable SoundObjectFadeAndStop(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oSound = getObject(args, 0, ctx);
    auto fSeconds = getFloat(args, 1);

    // Transform
    auto sound = dyn_cast<Sound>(oSound);

    // Execute
    if (sound) {
        sound->fadeAndStop(fSeconds);
    }
    return Variable::ofNull();
}

static Variable SetAreaFogColor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oArea = getObject(args, 0, ctx);
    auto fRed = getFloat(args, 1);
    auto fGreen = getFloat(args, 2);
    auto fBlue = getFloat(args, 3);

    // Execute: colour components run 0.0 to 1.0.
    if (auto area = dyn_cast<Area>(oArea)) {
        area->setFogColor(glm::clamp(glm::vec3(fRed, fGreen, fBlue), 0.0f, 1.0f));
    }
    return Variable::ofNull();
}

static Variable ChangeItemCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sItem = getString(args, 0);
    auto fCostMultiplier = getFloat(args, 1);

    // Transform
    auto resRef = boost::to_lower_copy(sItem);

    // Execute
    // sItem is an item template: its base item's price multiplier is set,
    // for every item of that base item (K2 behaviour, design note c11_data).
    std::shared_ptr<resource::Gff> uti;
    try {
        uti = ctx.services.resource.gffs.get(resRef, resource::ResType::Uti);
    } catch (const std::exception &) {
        uti.reset();
    }
    if (uti) {
        ctx.game.party().setItemCostMultiplier(uti->getInt("BaseItem", -1), fCostMultiplier);
    }
    return Variable::ofNull();
}

static Variable GetIsLiveContentAvailable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPkg = getInt(args, 0);

    // Transform

    // Execute
    // The PC release has no downloadable content packages (design note c11_data).
    return Variable::ofInt(0);
}

static Variable ResetDialogState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // Back to "no dialog": a world fade-in held for a conversation that the
    // OnDialog script chose not to start is released.
    ctx.game.resetDialogState();
    return Variable::ofNull();
}

static Variable SetGoodEvilValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto nAlignment = getInt(args, 1);

    // Execute: 0 (dark) to 100 (light); the creature clamps anything outside.
    if (auto creature = asCreature(oCreature)) {
        creature->setGoodEvil(nAlignment);
    }
    return Variable::ofNull();
}

static Variable GetIsPoisoned(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);

    // Execute: TRUE while a poison effect is on the object.
    return Variable::ofInt(oObject && oObject->hasEffect(EffectType::Poison) ? 1 : 0);
}

static Variable GetSpellTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = asCreature(oCreature);
    return Variable::ofObject(creature ? creature->getAttemptedSpellTarget() : kObjectInvalid);
}

static Variable SetSoloMode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto bActivate = getInt(args, 0);

    // Transform
    auto activate = static_cast<bool>(bActivate);

    // Execute
    ctx.game.party().setSoloMode(activate);
    return Variable::ofNull();
}

static Variable CancelPostDialogCharacterSwitch(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // The PC keeps control after the conversation (K2 behaviour,
    // design note c11_data). Harmless when no switch back is pending.
    ctx.game.party().setPostDialogSwitchBack(0);
    return Variable::ofNull();
}

static Variable SetMaxHitPoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObject(args, 0, ctx);
    auto nMaxHP = getInt(args, 1);

    // Transform

    // Execute
    oObject->setMaxHitPoints(nMaxHP);
    return Variable::ofNull();
}

static Variable NoClicksFor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fDuration = getFloat(args, 0);

    // Execute
    ctx.game.blockClicksFor(fDuration);
    return Variable::ofNull();
}

static Variable HoldWorldFadeInForDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.globalFade().holdForDialog();
    return Variable::ofNull();
}

static Variable ShipBuild(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(static_cast<int>(kShipBuild));
}

static Variable SurrenderRetainBuffs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // As SurrenderToEnemies, keeping the effects the creatures made themselves.
    if (auto caller = dyn_cast<Creature>(getCaller(ctx))) {
        caller->surrender(/*retainOwnEffects=*/true);
    }
    return Variable::ofNull();
}

static Variable SuppressStatusSummaryEntry(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNumEntries = getIntOrElse(args, 0, 1);
    (void)nNumEntries;

    // Execute
    // intentionally stubbed: the engine shows no status-summary list whose entries could be hidden
    return Variable::ofNull();
}

static Variable GetCheatCode(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nCode = getInt(args, 0);

    // Transform

    // Execute
    return Variable::ofInt(0);
}

static Variable SetMusicVolume(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto fVolume = getFloatOrElse(args, 0, 1.0f);

    // Execute
    // Clamped to 0.0-1.0; scales the music from now on.
    ctx.game.setMusicVolume(fVolume);
    return Variable::ofNull();
}

static Variable CreateItemOnFloor(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sTemplate = getString(args, 0);
    auto lLocation = getLocationArgument(args, 1);
    auto bUseAppearAnimation = getIntOrElse(args, 2, 0);

    // Transform
    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    // An item lying in the current area at the location (design note c11_data).
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto object = area->createObject(ObjectType::Item, tmplt, lLocation);
    return Variable::ofObject(getObjectIdOrInvalid(object));
}

static Variable SetAvailableNPCId(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oidNPC = getObjectOrNull(args, 1, ctx);

    // Execute
    const RosterIdentity identity {RosterKind::Npc, nNPC};
    if (!oidNPC) {
        if (args[1].objectId == kObjectInvalid &&
            ctx.game.party().isRosterAvailable(identity)) {
            ctx.game.party().clearRosterCreature(identity);
        }
    } else {
        ctx.game.party().bindRosterCreature(identity, checkCreature(oidNPC));
    }
    return Variable::ofNull();
}

static Variable GetScriptParameter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nIndex = getInt(args, 0);

    // Transform
    const Variable *param = nullptr;
    switch (nIndex) {
    case 1:
        param = ctx.execution.findArg(ArgKind::ScriptParam1);
        break;
    case 2:
        param = ctx.execution.findArg(ArgKind::ScriptParam2);
        break;
    case 3:
        param = ctx.execution.findArg(ArgKind::ScriptParam3);
        break;
    case 4:
        param = ctx.execution.findArg(ArgKind::ScriptParam4);
        break;
    case 5:
        param = ctx.execution.findArg(ArgKind::ScriptParam5);
        break;
    default:
        break;
    }

    // Execute
    return Variable::ofInt(param ? param->intValue : 0);
}

static Variable SetFadeUntilScript(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    ctx.game.globalFade().lockUntilScript();
    return Variable::ofNull();
}

static Variable GetItemComponent(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the party's pieces of crafting components.
    return Variable::ofInt(static_cast<int>(ctx.game.party().persistedState().itemComponent));
}

static Variable GetItemComponentPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the credit value one piece stands for; the workbench counts one
    // piece per 20 credits of an item's cost (crafting::materialValue).
    return Variable::ofInt(20);
}

static Variable ShowChemicalUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCharacter = getObject(args, 0, ctx);
    (void)oCharacter;

    // Execute
    // intentionally stubbed: the chemical workbench screen is not wired to scripts
    return Variable::ofNull();
}

static Variable GetChemicals(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: the party's pieces of chemicals.
    return Variable::ofInt(static_cast<int>(ctx.game.party().persistedState().itemChemical));
}

static Variable GetChemicalPieceValue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: as for components, one piece per 20 credits.
    return Variable::ofInt(20);
}

// K2: the Force points the power being cast cost its caster (the cost rule of
// design note c03_stats). 0 outside a cast, and for a cast that costs nothing.
static Variable GetSpellForcePointCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto callerArg = ctx.execution.findArg(ArgKind::Caller);
    auto spellArg = ctx.execution.findArg(ArgKind::SpellId);
    if (!callerArg || !spellArg) {
        return Variable::ofInt(0);
    }
    auto caller = asCreature(ctx.game.getObjectById(callerArg->objectId));
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(spellArg->intValue));
    if (!caller || !spell) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(caller->forcePowerCost(*spell));
}

static Variable GetFeatAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Execute (K2): the creature owns the feat, whether or not it has uses
    // left today.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature && creature->attributes().hasFeat(static_cast<FeatType>(nFeat)) ? 1 : 0);
}

static Variable GetSpellAcquired(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oCreature = getObjectOrCaller(args, 1, ctx);

    // Execute (K2): the creature knows the power, even if it cannot afford
    // it right now.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature && creature->attributes().hasSpell(static_cast<SpellType>(nSpell)) ? 1 : 0);
}

static Variable ShowSwoopUpgradeScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // intentionally stubbed: the swoop upgrade screen is not wired to scripts
    return Variable::ofNull();
}

static Variable GrantFeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFeat = getInt(args, 0);
    auto oCreature = getObjectOrNull(args, 1, ctx);

    // Execute: adds the feat to the creature outright, as a level-up pick would.
    if (auto creature = asCreature(oCreature)) {
        creature->attributes().addFeat(static_cast<FeatType>(nFeat));
    }
    return Variable::ofNull();
}

static Variable GrantSpell(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpell = getInt(args, 0);
    auto oCreature = getObjectOrNull(args, 1, ctx);

    // Execute: adds the Force power to the creature outright.
    if (auto creature = asCreature(oCreature)) {
        creature->attributes().addSpell(static_cast<SpellType>(nSpell));
    }
    return Variable::ofNull();
}

static Variable SpawnMine(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nMineType = getInt(args, 0);
    auto lPoint = getLocationArgument(args, 1);
    auto nDetectDCBase = getInt(args, 2);
    auto nDisarmDCBase = getInt(args, 3);
    auto oCreator = getObject(args, 4, ctx);

    // Execute: lays the mine of that traps.2da row at the location with the given DCs.
    auto creator = asCreature(oCreator);
    spawnMine(ctx.game, ctx.services, nMineType, lPoint->position(), creator ? creator->faction() : Faction::Hostile1,
              oCreator ? oCreator->id() : script::kObjectInvalid, nDetectDCBase, nDisarmDCBase);
    return Variable::ofNull();
}

static Variable SetFakeCombatState(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nEnable = getInt(args, 1);

    // Execute: remembered on the object; nothing draws the combat stance from it yet.
    if (oObject) {
        oObject->setScriptSetting("FakeCombatState", nEnable != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable GetOwnerDemolitionsSkill(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute: the Demolitions skill of the creature that laid a mine or trap;
    // of the creature itself when given one; 0 when there is no such creature.
    std::shared_ptr<Creature> owner = asCreature(oObject);
    if (auto trigger = dyn_cast<Trigger>(oObject)) {
        owner = asCreature(ctx.game.getObjectById(trigger->trapCreatorId()));
    }
    return Variable::ofInt(owner ? owner->attributes().demolitions() : 0);
}

static Variable SetOrientOnClick(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrCaller(args, 0, ctx);
    auto nState = getIntOrElse(args, 1, 1);

    // Execute: remembered on the creature; clicking it does not turn the player to face it yet.
    if (oCreature) {
        oCreature->setScriptSetting("OrientOnClick", nState != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable GetInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Execute: the companion's influence over the player, 0-100; 0 for an unknown companion.
    return Variable::ofInt(ctx.game.party().influence(nNPC));
}

static Variable SetInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nInfluence = getInt(args, 1);

    // Execute: clamped to 0-100.
    ctx.game.party().setInfluence(nNPC, nInfluence);
    return Variable::ofNull();
}

static Variable ModifyInfluence(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto nModifier = getInt(args, 1);

    // Execute: adds to the current influence, the total clamped to 0-100.
    ctx.game.party().setInfluence(nNPC, ctx.game.party().influence(nNPC) + nModifier);
    return Variable::ofNull();
}

static Variable GetRacialSubType(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObjectOrNull(args, 0, ctx);

    // Execute: the creature's subrace (the value GetSubRace reports); 0 for anything else.
    auto creature = asCreature(oTarget);
    return Variable::ofInt(creature ? static_cast<int>(creature->subrace()) : 0);
}

// Number globals are signed 8-bit values. IncrementGlobalNumber and
// DecrementGlobalNumber are documented as failing with a warning when the
// resulting amount would leave that domain, so a rejected call leaves the stored
// value alone: it neither wraps nor clamps to the bound.
static constexpr int kMinGlobalNumber = -128;
static constexpr int kMaxGlobalNumber = 127;

// Add a signed delta to a named number global, rejecting a result outside the
// domain. Widened to 64-bit so that an extreme amount cannot overflow before the
// bounds check decides the call.
static void applyGlobalNumberDelta(
    const RoutineContext &ctx,
    const std::string &identifier,
    int64_t delta) {

    int64_t result = static_cast<int64_t>(ctx.game.getGlobalNumber(identifier)) + delta;
    if (result < kMinGlobalNumber || result > kMaxGlobalNumber) {
        debug(str(boost::format("Global number out of range: %s %d") % identifier % result),
              LogChannel::Script);
        return;
    }
    ctx.game.setGlobalNumber(identifier, static_cast<int>(result));
}

static Variable IncrementGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nAmount = getInt(args, 1);

    // Transform

    // Execute
    applyGlobalNumberDelta(ctx, sIdentifier, nAmount);
    return Variable::ofNull();
}

static Variable DecrementGlobalNumber(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sIdentifier = getString(args, 0);
    auto nAmount = getInt(args, 1);

    // Transform

    // Execute
    // Negated as 64-bit: the amount may be the most negative int, which has no
    // positive counterpart in its own width.
    applyGlobalNumberDelta(ctx, sIdentifier, -static_cast<int64_t>(nAmount));
    return Variable::ofNull();
}

static Variable SetBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto nBonusFP = getInt(args, 1);

    // Execute: the creature's bonus Force points, never below 0. They are kept
    // for GetBonusForcePoints and do not raise the maximum.
    if (oCreature) {
        oCreature->setScriptSetting("BonusForcePoints", std::max(0, nBonusFP));
    }
    return Variable::ofNull();
}

static Variable AddBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto nBonusFP = getInt(args, 1);

    // Execute
    if (oCreature) {
        oCreature->setScriptSetting("BonusForcePoints", std::max(0, oCreature->scriptSetting("BonusForcePoints") + nBonusFP));
    }
    return Variable::ofNull();
}

static Variable GetBonusForcePoints(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute: declared void in the game's script header yet read as a number by scripts, so the value is returned.
    return Variable::ofInt(oCreature ? oCreature->scriptSetting("BonusForcePoints") : 0);
}

static Variable IsMoviePlaying(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    return Variable::ofInt(ctx.game.isMoviePlaying() ? 1 : 0);
}

static Variable QueueMovie(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto sMovie = getString(args, 0);
    auto nSkippable = getIntOrElse(args, 1, 1);

    // Execute
    ctx.game.queueMovie(boost::to_lower_copy(sMovie), nSkippable != 0);
    return Variable::ofNull();
}

static Variable PlayMovieQueue(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nAllowSkips = getIntOrElse(args, 0, 1);

    // Execute
    ctx.game.playMovieQueue(nAllowSkips != 0);
    return Variable::ofNull();
}

static Variable YavinHackDoorClose(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oDoor = getObjectOrNull(args, 0, ctx);

    // Transform
    auto door = std::dynamic_pointer_cast<Door>(oDoor);

    // Execute
    // Snaps the door shut with no close action or events (K2 behaviour,
    // design note c11_data); anything that is not a door is ignored.
    if (door) {
        door->snapClosed();
    }
    return Variable::ofNull();
}

static Variable IsStealthed(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCreature = getObject(args, 0, ctx);
    (void)oCreature;

    // Execute
    // intentionally stubbed: the engine has no stealth stance for a creature to be in; always FALSE
    return Variable::ofInt(0);
}

static Variable IsMeditating(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCreature = getObject(args, 0, ctx);
    (void)oCreature;

    // Execute
    // intentionally stubbed: the engine has no meditation stance for a creature to be in; always FALSE
    return Variable::ofInt(0);
}

static Variable IsInTotalDefense(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oCreature = getObject(args, 0, ctx);
    (void)oCreature;

    // Execute
    // intentionally stubbed: the engine has no total-defense stance for a creature to be in; always FALSE
    return Variable::ofInt(0);
}

static Variable SetHealTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidHealer = getObjectOrNull(args, 0, ctx);
    auto oidTarget = getObjectOrNull(args, 1, ctx);

    // Execute: remembers whom the healer is set to heal; no healer AI reads it yet.
    if (oidHealer) {
        oidHealer->setScriptSetting("HealTarget", static_cast<int>(oidTarget ? oidTarget->id() : script::kObjectInvalid));
    }
    return Variable::ofNull();
}

static Variable GetHealTarget(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidHealer = getObjectOrNull(args, 0, ctx);

    // Execute: the object SetHealTarget set, invalid when it is gone or was never set.
    uint32_t id = oidHealer ? static_cast<uint32_t>(oidHealer->scriptSetting("HealTarget", static_cast<int>(script::kObjectInvalid)))
                            : script::kObjectInvalid;
    return Variable::ofObject(ctx.game.getObjectById(id) ? id : script::kObjectInvalid);
}

static Variable GetRandomDestination(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto rangeLimit = getInt(args, 1);

    // Execute: a walkable point within rangeLimit of the creature (a few tries),
    // the creature's own position when none is found.
    auto creature = asCreature(oCreature);
    if (!creature) {
        return Variable::ofVector(glm::vec3(0.0f));
    }
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (area && rangeLimit > 0) {
        for (int attempt = 0; attempt < 8; ++attempt) {
            float angle = randomFloat(0.0f, 6.28318531f);
            float distance = randomFloat(0.0f, static_cast<float>(rangeLimit));
            glm::vec3 candidate(creature->position() + glm::vec3(portable::cosf(angle) * distance, portable::sinf(angle) * distance, 0.0f));
            if (auto landed = area->walkablePointAt(candidate)) {
                return Variable::ofVector(*landed);
            }
        }
    }
    return Variable::ofVector(creature->position());
}

static Variable IsFormActive(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nFormID = getInt(args, 1);

    // Execute (K2): the creature's active form is nFormID, and for a
    // lightsaber form it wields a lightsaber.
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature && forms::isInEffect(creature->activeForm(), nFormID, creature->wieldsLightsaber()) ? 1 : 0);
}

static Variable GetSpellFormMask(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpellID = getInt(args, 0);

    // Execute (K2): spells.2da formmask, the forms a power works with.
    auto spells = ctx.services.resource.twoDas.get("spells");
    if (!spells || nSpellID < 0 || nSpellID >= spells->getRowCount()) {
        return Variable::ofInt(0);
    }
    return Variable::ofInt(static_cast<int>(spells->getHexInt(nSpellID, "formmask", 0)));
}

static Variable GetSpellBaseForcePointCost(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSpellID = getInt(args, 0);

    // Execute (K2): spells.2da forcepoints, before any modifier.
    auto spell = ctx.services.game.spells.get(static_cast<SpellType>(nSpellID));
    return Variable::ofInt(spell ? spell->forcePoints : 0);
}

static Variable SetKeepStealthInDialog(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStealthState = getInt(args, 0);

    // Execute: kept on the module; the engine has no stealth stance for a conversation to drop.
    if (auto module = ctx.game.module()) {
        module->setScriptSetting("KeepStealthInDialog", nStealthState);
    }
    return Variable::ofNull();
}

static Variable HasLineOfSight(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto vSource = getVector(args, 0);
    auto vTarget = getVector(args, 1);
    auto oSource = getObjectOrNull(args, 2, ctx);
    auto oTarget = getObjectOrNull(args, 3, ctx);

    // Execute: whether anything but the two given objects blocks the line between the points.
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    return Variable::ofInt(!area || area->hasLineOfSightBetween(vSource, vTarget, oSource.get(), oTarget.get()) ? 1 : 0);
}

static Variable ShowDemoScreen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto sTexture = getString(args, 0);
    auto nTimeout = getInt(args, 1);
    auto nDisplayString = getInt(args, 2);
    auto nDisplayX = getInt(args, 3);
    auto nDisplayY = getInt(args, 4);
    (void)sTexture;
    (void)nTimeout;
    (void)nDisplayString;
    (void)nDisplayX;
    (void)nDisplayY;

    // Execute
    // intentionally stubbed: there is no demo/attract screen; reports it was not shown (0)
    return Variable::ofInt(0);
}

static Variable ForceHeartbeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute: runs the object's OnHeartbeat script now, off the regular beat.
    if (oCreature && !oCreature->getOnHeartbeat().empty()) {
        ctx.game.scriptRunner().run(oCreature->getOnHeartbeat(), oCreature->id());
    }
    return Variable::ofNull();
}

static Variable IsRunning(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute
    auto creature = asCreature(oCreature);
    return Variable::ofInt(creature && creature->movementType() == Creature::MovementType::Run ? 1 : 0);
}

static Variable SetForfeitConditions(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nForfeitFlags = getInt(args, 0);

    // Execute: kept on the module; the engine's duels do not check forfeit rules yet.
    if (auto module = ctx.game.module()) {
        module->setScriptSetting("ForfeitConditions", nForfeitFlags);
    }
    return Variable::ofNull();
}

static Variable GetLastForfeitViolation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // intentionally stubbed: no duel checks the forfeit conditions, so none is ever violated; answers 0
    return Variable::ofInt(0);
}

static Variable ModifyReflexSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aObject = getObjectOrNull(args, 0, ctx);
    auto aModValue = getInt(args, 1);

    // Execute: changes the creature's base reflex save (never below 0); it adds to the saves its classes grant.
    if (auto creature = asCreature(aObject)) {
        creature->attributes().modifyBaseSavingThrow(1, aModValue);
    }
    return Variable::ofNull();
}

static Variable ModifyFortitudeSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aObject = getObjectOrNull(args, 0, ctx);
    auto aModValue = getInt(args, 1);

    // Execute: changes the creature's base fortitude save (never below 0); it adds to the saves its classes grant.
    if (auto creature = asCreature(aObject)) {
        creature->attributes().modifyBaseSavingThrow(0, aModValue);
    }
    return Variable::ofNull();
}

static Variable ModifyWillSavingThrowBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aObject = getObjectOrNull(args, 0, ctx);
    auto aModValue = getInt(args, 1);

    // Execute: changes the creature's base will save (never below 0); it adds to the saves its classes grant.
    if (auto creature = asCreature(aObject)) {
        creature->attributes().modifyBaseSavingThrow(2, aModValue);
    }
    return Variable::ofNull();
}

static Variable GetScriptStringParameter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    const Variable *param = ctx.execution.findArg(ArgKind::ScriptStringParam);
    return Variable::ofString(param ? param->strValue : "");
}

static Variable GetObjectPersonalSpace(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto aObject = getObjectOrNull(args, 0, ctx);

    // Execute: a creature's personal space (appearance.2da) in metres; 0 for anything else.
    auto creature = asCreature(aObject);
    return Variable::ofFloat(creature ? creature->creaturePersonalSpace() : 0.0f);
}

static Variable AdjustCreatureAttributes(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nAttribute = getInt(args, 1);
    auto nAmount = getInt(args, 2);

    // Execute: changes an ability score (ABILITY_*) by nAmount, keeping it between 1 and 255.
    auto creature = asCreature(oObject);
    if (creature && nAttribute >= 0 && nAttribute < 6) {
        auto ability = static_cast<Ability>(nAttribute);
        creature->attributes().setAbilityScore(ability, std::clamp(creature->attributes().getAbilityScore(ability) + nAmount, 1, 255));
    }
    return Variable::ofNull();
}

static Variable SetCreatureAILevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nPriority = getInt(args, 1);

    // Execute: kept on the creature; the AI does not consult a script-set level yet.
    if (oObject) {
        oObject->setScriptSetting("AILevel", nPriority);
    }
    return Variable::ofNull();
}

static Variable ResetCreatureAILevel(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);

    // Execute: back to no script-set level (-1).
    if (oObject) {
        oObject->setScriptSetting("AILevel", -1);
    }
    return Variable::ofNull();
}

static Variable AddAvailablePUPByTemplate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto sTemplate = getString(args, 1);

    auto tmplt = boost::to_lower_copy(sTemplate);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Puppet, nPUP}, tmplt));
}

static Variable AddAvailablePUPByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oPuppet = getObject(args, 1, ctx);

    auto creature = checkCreature(oPuppet);

    // Execute
    return Variable::ofInt(ctx.game.party().addAvailableRosterRecord(
        {RosterKind::Puppet, nPUP}, creature));
}

static Variable AssignPUP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto nNPC = getInt(args, 1);

    // Execute
    return Variable::ofInt(ctx.game.party().assignPuppet(nPUP, nNPC));
}

static Variable SpawnAvailablePUP(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto lLocation = getLocationArgument(args, 1);

    // Execute
    auto puppet = ctx.game.party().getAvailablePuppet(nPUP, true);
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!puppet || !area) {
        return Variable::ofObject(kObjectInvalid);
    }
    puppet->setPuppet(true);
    puppet->setPosition(lLocation->position());
    puppet->setFacing(lLocation->facing());
    area->landObject(*puppet);
    const bool alreadyResident = std::find(
        area->objects().begin(), area->objects().end(), puppet) !=
        area->objects().end();
    if (!alreadyResident) {
        area->add(puppet);
        puppet->runSpawnScript();
    }
    return Variable::ofObject(puppet->id());
}

static Variable AddPartyPuppet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oidCreature = getObject(args, 1, ctx);

    auto creature = checkCreature(oidCreature);

    // Execute
    return Variable::ofInt(ctx.game.party().addPuppet(nPUP, creature));
}

static Variable GetPUPOwner(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPUP = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = dyn_cast<Creature>(oPUP);
    auto identity = creature
                        ? ctx.game.party().rosterIdentity(*creature)
                        : std::nullopt;
    if (!identity || identity->kind != RosterKind::Puppet) {
        return Variable::ofObject(kObjectInvalid);
    }
    auto owner = ctx.game.party().puppetOwner(identity->slot);
    return Variable::ofObject(getObjectIdOrInvalid(owner));
}

static Variable GetIsPuppet(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPUP = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto creature = dyn_cast<Creature>(oPUP);
    return Variable::ofInt(creature && creature->isPuppet());
}

static Variable GetIsPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCharacter = getObjectOrCaller(args, 0, ctx);

    // Execute
    auto leader = ctx.game.party().getLeader();
    return Variable::ofInt(oCharacter && leader && leader->id() == oCharacter->id() ? 1 : 0);
}

static Variable GetPartyLeader(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto leader = ctx.game.party().getLeader();
    return Variable::ofObject(getObjectIdOrInvalid(leader));
}

static Variable RemoveNPCFromPartyToBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);

    // Transform
    if (nNPC < 0 || nNPC >= static_cast<int>(Party::kK2NpcCount)) {
        return Variable::ofInt(0);
    }

    // Execute
    Party &party = ctx.game.party();
    const RosterIdentity identity {RosterKind::Npc, nNPC};
    auto creature = party.rosterCreature(identity);
    if (!creature) {
        return Variable::ofInt(0);
    }
    const bool wasControlled = party.controlledNpc() == nNPC;
    auto canonicalPlayer = party.actualPlayer();
    if (wasControlled && canonicalPlayer.get() == creature.get()) {
        // A malformed controlled-NPC topology has no canonical PC to resume.
        // Fail closed instead of retiring the session's only controlled actor.
        return Variable::ofInt(0);
    }

    if (party.isRosterAvailable(identity) &&
        !party.removeMemberToBase(nNPC)) {
        return Variable::ofInt(0);
    }
    const bool killed = ctx.game.killRosterCreature(identity);
    if (killed && wasControlled && canonicalPlayer &&
        !party.isMember(*canonicalPlayer)) {
        party.addMember(kNpcPlayer, canonicalPlayer);
    }
    return Variable::ofInt(static_cast<int>(killed));
}

static Variable CreatureFlourishWeapon(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oObject = getObject(args, 0, ctx);
    (void)oObject;

    // Execute
    // intentionally stubbed: the flourish animation is not documented and no animation set names it
    return Variable::ofNull();
}

static Variable ChangeObjectAppearance(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObjectToChange = getObjectOrNull(args, 0, ctx);
    auto nAppearance = getInt(args, 1);

    // Execute: a creature takes the appearance.2da row and rebuilds its model; other objects keep theirs.
    if (auto creature = asCreature(oObjectToChange)) {
        creature->setAppearance(nAppearance);
        creature->loadAppearance();
    }
    return Variable::ofNull();
}

static Variable GetIsXBox(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute: this engine runs on PCs only.
    return Variable::ofInt(0);
}

static Variable PlayOverlayAnimation(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oTarget = getObject(args, 0, ctx);
    auto nAnimation = getInt(args, 1);

    // Transform
    auto target = checkCreature(oTarget);
    auto animation = static_cast<AnimationType>(nAnimation);

    // Execute
    // As nwscript describes it: plays on the creature even while it is moving,
    // and places no action on the queue.
    target->playOverlayAnimation(animation);
    return Variable::ofNull();
}

static Variable UnlockAllSongs(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    // intentionally stubbed: the engine keeps no list of unlocked songs to fill
    return Variable::ofNull();
}

static Variable DisableMap(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Execute: the party's PT_DISABLEMAP flag.
    ctx.game.party().setMapDisabled(nFlag != 0);
    return Variable::ofNull();
}

static Variable DetonateMine(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oMine = getObjectOrNull(args, 0, ctx);

    // Execute: sets the mine off with no victim, so its trap script runs once; a one-shot mine is spent.
    if (auto trigger = dyn_cast<Trigger>(oMine)) {
        trigger->fireTrap(script::kObjectInvalid);
    }
    return Variable::ofNull();
}

static Variable DisableHealthRegen(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Execute: PT_DISABLEREGEN (design note regeneration)
    ctx.game.party().setRegenerationDisabled(nFlag != 0);
    return Variable::ofNull();
}

static Variable SetCurrentForm(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto nFormID = getInt(args, 1);

    // Execute (K2): a form the creature knows becomes its active form.
    auto creature = asCreature(oCreature);
    if (creature && forms::isForm(nFormID) && creature->attributes().hasSpell(static_cast<SpellType>(nFormID))) {
        creature->setActiveForm(nFormID);
    }
    return Variable::ofNull();
}

static Variable SetDisableTransit(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getIntOrElse(args, 0, 0);

    // Execute: kept on the module; the engine's area transitions do not consult it yet.
    if (auto module = ctx.game.module()) {
        module->setScriptSetting("DisableTransit", nFlag != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable SetInputClass(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto nClass = getInt(args, 0);
    (void)nClass;

    // Execute
    // intentionally stubbed: the engine has one input scheme (no controller or mouse input classes to switch between)
    return Variable::ofNull();
}

static Variable SetForceAlwaysUpdate(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nFlag = getInt(args, 1);

    // Execute: kept on the object; the engine updates every object of the area every tick anyway.
    if (oObject) {
        oObject->setScriptSetting("ForceAlwaysUpdate", nFlag != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable EnableRain(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nFlag = getInt(args, 0);

    // Execute: kept on the area; the renderer draws no rain yet.
    auto module = ctx.game.module();
    if (auto area = module ? module->area() : nullptr) {
        area->setScriptSetting("RainEnabled", nFlag != 0 ? 1 : 0);
    }
    return Variable::ofNull();
}

static Variable DisplayMessageBox(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nStrRef = getInt(args, 0);
    auto sIcon = getStringOrElse(args, 1, "");
    (void)sIcon; // the popup has no icon slot

    // Execute: the string in a popup the player confirms.
    ctx.game.showMessageBox(ctx.services.resource.strings.getText(nStrRef));
    return Variable::ofNull();
}

static Variable DisplayDatapad(const std::vector<Variable> &args, const RoutineContext &ctx) {
    auto oDatapad = getObject(args, 0, ctx);
    (void)oDatapad;

    // Execute
    // intentionally stubbed: the engine has no datapad reader screen
    return Variable::ofNull();
}

static Variable RemoveHeartbeat(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oPlaceable = getObject(args, 0, ctx);

    // Transform

    // Execute
    // Only the heartbeat script is dropped, and it stays dropped: no routine
    // can assign an event script back. Area heartbeat dispatch reads a copy of
    // the resref before running it, so a heartbeat that removes its own script
    // still runs to completion.
    oPlaceable->clearOnHeartbeat();
    return Variable::ofNull();
}

static Variable RemoveEffectByID(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);
    auto nEffectID = getInt(args, 1);

    // Execute: removes the effects carrying that exposed effect id (the id an
    // effect value read back from a save or the effect list reports).
    if (oCreature && nEffectID > 0) {
        oCreature->removeEffectsById(static_cast<EffectId>(nEffectID));
    }
    return Variable::ofNull();
}

static Variable RemoveEffectByExactMatch(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObject(args, 0, ctx);
    auto eEffect = getEffect(args, 1);

    // Execute: an effect value is the applied instance itself, so the exact
    // match is that instance; same as RemoveEffect.
    if (!oCreature || !eEffect) {
        return Variable::ofNull();
    }
    if (auto saved = std::dynamic_pointer_cast<SavedEffectValue>(eEffect);
        saved && saved->instance().hasStableId()) {
        oCreature->removeEffectsById(saved->instance().id);
    } else {
        oCreature->removeEffect(eEffect);
    }
    return Variable::ofNull();
}

static Variable AdjustCreatureSkills(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto nSkill = getInt(args, 1);
    auto nAmount = getInt(args, 2);

    // Execute: changes the ranks in a skill (SKILL_*) by nAmount, never below 0.
    auto creature = asCreature(oObject);
    if (creature && nSkill >= 0 && nSkill < 8) {
        auto skill = static_cast<SkillType>(nSkill);
        creature->attributes().setSkillRank(skill, std::max(0, creature->attributes().getSkillRank(skill) + nAmount));
    }
    return Variable::ofNull();
}

static Variable GetSkillRankBase(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nSkill = getInt(args, 0);
    auto oObject = getObjectOrCaller(args, 1, ctx);

    // Execute: the ranks the creature has bought, without ability modifier or effects; 0 for anything else.
    auto creature = asCreature(oObject);
    return Variable::ofInt(creature ? creature->attributes().getSkillRank(static_cast<SkillType>(nSkill)) : 0);
}

static Variable EnableRendering(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oObject = getObjectOrNull(args, 0, ctx);
    auto bEnable = getInt(args, 1);

    // Execute: shows or hides the object's model.
    if (oObject) {
        oObject->setVisible(bEnable != 0);
    }
    return Variable::ofNull();
}

static Variable GetCombatActionsPending(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oCreature = getObjectOrNull(args, 0, ctx);

    // Execute: how many attack and spell actions wait in the creature's queue, the one running included.
    int pending = 0;
    if (oCreature) {
        for (const auto &action : oCreature->actions()) {
            if (action->type() == ActionType::AttackObject || action->type() == ActionType::CastSpell ||
                action->type() == ActionType::ItemCastSpell) {
                ++pending;
            }
        }
    }
    return Variable::ofInt(pending);
}

static Variable SaveNPCByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nNPC = getInt(args, 0);
    auto oidCharacter = getObject(args, 1, ctx);

    auto creature = checkCreature(oidCharacter);

    // Execute
    creature->clearAllActions();
    ctx.game.saveRosterState({RosterKind::Npc, nNPC}, *creature);
    return Variable::ofNull();
}

static Variable SavePUPByObject(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto nPUP = getInt(args, 0);
    auto oidPuppet = getObject(args, 1, ctx);

    auto creature = checkCreature(oidPuppet);

    // Execute
    creature->clearAllActions();
    ctx.game.saveRosterState({RosterKind::Puppet, nPUP}, *creature);
    return Variable::ofNull();
}

static Variable GetIsPlayerMadeCharacter(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Load
    auto oidCharacter = getObjectOrNull(args, 0, ctx);

    // Execute: the player character, the one the player built; companions are authored.
    auto player = ctx.game.party().player();
    return Variable::ofInt(oidCharacter && player && player->id() == oidCharacter->id() ? 1 : 0);
}

static Variable RebuildPartyTable(const std::vector<Variable> &args, const RoutineContext &ctx) {
    // Execute
    auto module = ctx.game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) return Variable::ofNull();

    // Retail K2 deliberately authors this association in routine 876. It is
    // the one place where these content Tags mean "bind this object to this
    // PartyTable slot"; they are not general engine identities.
    static const std::array<const char *, Party::kK2NpcCount> kNpcTags {
        "atton", "baodur", "mand", "g0t0", "handmaiden", "hk47",
        "kreia", "mira", "t3m4", "visasmarr", "hanharr", "disciple"};
    auto findCreature = [&area](const std::string &tag) {
        for (const auto &object : area->objects()) {
            if (object->type() == ObjectType::Creature &&
                boost::iequals(object->tag(), tag)) {
                return std::static_pointer_cast<Creature>(object);
            }
        }
        return std::shared_ptr<Creature> {};
    };
    auto &party = ctx.game.party();
    for (size_t slot = 0; slot < kNpcTags.size(); ++slot) {
        if (!party.isMemberAvailable(static_cast<int>(slot))) continue;
        if (auto creature = findCreature(kNpcTags[slot])) {
            party.bindRosterCreature(
                {RosterKind::Npc, static_cast<int>(slot)}, creature);
        }
    }
    if (party.isRosterAvailable({RosterKind::Puppet, 0})) {
        if (auto remote = findCreature("remote")) {
            party.bindRosterCreature({RosterKind::Puppet, 0}, remote);
        }
    }
    return Variable::ofNull();
}

void Routines::registerMainKotorRoutines() {
    insert(0, "Random", R_INT, {R_INT}, &Random);
    insert(1, "PrintString", R_VOID, {R_STRING}, &PrintString);
    insert(2, "PrintFloat", R_VOID, {R_FLOAT, R_INT, R_INT}, &PrintFloat);
    insert(3, "FloatToString", R_STRING, {R_FLOAT, R_INT, R_INT}, &FloatToString);
    insert(4, "PrintInteger", R_VOID, {R_INT}, &PrintInteger);
    insert(5, "PrintObject", R_VOID, {R_OBJECT}, &PrintObject);
    insert(6, "AssignCommand", R_VOID, {R_OBJECT, R_ACTION}, &AssignCommand);
    insert(7, "DelayCommand", R_VOID, {R_FLOAT, R_ACTION}, &DelayCommand);
    insert(8, "ExecuteScript", R_VOID, {R_STRING, R_OBJECT, R_INT}, &ExecuteScript);
    insert(9, "ClearAllActions", R_VOID, {}, &ClearAllActions);
    insert(10, "SetFacing", R_VOID, {R_FLOAT}, &SetFacing);
    insert(11, "SwitchPlayerCharacter", R_INT, {R_INT}, &SwitchPlayerCharacter);
    insert(12, "SetTime", R_VOID, {R_INT, R_INT, R_INT, R_INT}, &SetTime);
    insert(13, "SetPartyLeader", R_INT, {R_INT}, &SetPartyLeader);
    insert(14, "SetAreaUnescapable", R_VOID, {R_INT}, &SetAreaUnescapable);
    insert(15, "GetAreaUnescapable", R_INT, {}, &GetAreaUnescapable);
    insert(16, "GetTimeHour", R_INT, {}, &GetTimeHour);
    insert(17, "GetTimeMinute", R_INT, {}, &GetTimeMinute);
    insert(18, "GetTimeSecond", R_INT, {}, &GetTimeSecond);
    insert(19, "GetTimeMillisecond", R_INT, {}, &GetTimeMillisecond);
    insert(24, "GetArea", R_OBJECT, {R_OBJECT}, &GetArea);
    insert(25, "GetEnteringObject", R_OBJECT, {}, &GetEnteringObject);
    insert(26, "GetExitingObject", R_OBJECT, {}, &GetExitingObject);
    insert(27, "GetPosition", R_VECTOR, {R_OBJECT}, &GetPosition);
    insert(28, "GetFacing", R_FLOAT, {R_OBJECT}, &GetFacing);
    insert(29, "GetItemPossessor", R_OBJECT, {R_OBJECT}, &GetItemPossessor);
    insert(30, "GetItemPossessedBy", R_OBJECT, {R_OBJECT, R_STRING}, &GetItemPossessedBy);
    insert(31, "CreateItemOnObject", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &CreateItemOnObject);
    insert(36, "GetLastAttacker", R_OBJECT, {R_OBJECT}, &GetLastAttacker);
    insert(38, "GetNearestCreature", R_OBJECT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreature);
    insert(41, "GetDistanceToObject", R_FLOAT, {R_OBJECT}, &GetDistanceToObject);
    insert(42, "GetIsObjectValid", R_INT, {R_OBJECT}, &GetIsObjectValid);
    insert(45, "SetCameraFacing", R_VOID, {R_FLOAT}, &SetCameraFacing);
    insert(46, "PlaySound", R_VOID, {R_STRING}, &PlaySound);
    insert(47, "GetSpellTargetObject", R_OBJECT, {}, &GetSpellTargetObject);
    insert(49, "GetCurrentHitPoints", R_INT, {R_OBJECT}, &GetCurrentHitPoints);
    insert(50, "GetMaxHitPoints", R_INT, {R_OBJECT}, &GetMaxHitPoints);
    insert(52, "GetLastItemEquipped", R_OBJECT, {}, &GetLastItemEquipped);
    insert(53, "GetSubScreenID", R_INT, {}, &GetSubScreenID);
    insert(54, "CancelCombat", R_VOID, {R_OBJECT}, &CancelCombat);
    insert(55, "GetCurrentForcePoints", R_INT, {R_OBJECT}, &GetCurrentForcePoints);
    insert(56, "GetMaxForcePoints", R_INT, {R_OBJECT}, &GetMaxForcePoints);
    insert(57, "PauseGame", R_VOID, {R_INT}, &PauseGame);
    insert(58, "SetPlayerRestrictMode", R_VOID, {R_INT}, &SetPlayerRestrictMode);
    insert(59, "GetStringLength", R_INT, {R_STRING}, &GetStringLength);
    insert(60, "GetStringUpperCase", R_STRING, {R_STRING}, &GetStringUpperCase);
    insert(61, "GetStringLowerCase", R_STRING, {R_STRING}, &GetStringLowerCase);
    insert(62, "GetStringRight", R_STRING, {R_STRING, R_INT}, &GetStringRight);
    insert(63, "GetStringLeft", R_STRING, {R_STRING, R_INT}, &GetStringLeft);
    insert(64, "InsertString", R_STRING, {R_STRING, R_STRING, R_INT}, &InsertString);
    insert(65, "GetSubString", R_STRING, {R_STRING, R_INT, R_INT}, &GetSubString);
    insert(66, "FindSubString", R_INT, {R_STRING, R_STRING}, &FindSubString);
    insert(67, "fabs", R_FLOAT, {R_FLOAT}, &fabs);
    insert(68, "cos", R_FLOAT, {R_FLOAT}, &cos);
    insert(69, "sin", R_FLOAT, {R_FLOAT}, &sin);
    insert(70, "tan", R_FLOAT, {R_FLOAT}, &tan);
    insert(71, "acos", R_FLOAT, {R_FLOAT}, &acos);
    insert(72, "asin", R_FLOAT, {R_FLOAT}, &asin);
    insert(73, "atan", R_FLOAT, {R_FLOAT}, &atan);
    insert(74, "log", R_FLOAT, {R_FLOAT}, &log);
    insert(75, "pow", R_FLOAT, {R_FLOAT, R_FLOAT}, &pow);
    insert(76, "sqrt", R_FLOAT, {R_FLOAT}, &sqrt);
    insert(77, "abs", R_INT, {R_INT}, &abs);
    insert(83, "GetPlayerRestrictMode", R_INT, {R_OBJECT}, &GetPlayerRestrictMode);
    insert(84, "GetCasterLevel", R_INT, {R_OBJECT}, &GetCasterLevel);
    insert(85, "GetFirstEffect", R_EFFECT, {R_OBJECT}, &GetFirstEffect);
    insert(86, "GetNextEffect", R_EFFECT, {R_OBJECT}, &GetNextEffect);
    insert(87, "RemoveEffect", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffect);
    insert(88, "GetIsEffectValid", R_INT, {R_EFFECT}, &GetIsEffectValid);
    insert(89, "GetEffectDurationType", R_INT, {R_EFFECT}, &GetEffectDurationType);
    insert(90, "GetEffectSubType", R_INT, {R_EFFECT}, &GetEffectSubType);
    insert(91, "GetEffectCreator", R_OBJECT, {R_EFFECT}, &GetEffectCreator);
    insert(92, "IntToString", R_STRING, {R_INT}, &IntToString);
    insert(93, "GetFirstObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstObjectInArea);
    insert(94, "GetNextObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetNextObjectInArea);
    insert(95, "d2", R_INT, {R_INT}, &d2);
    insert(96, "d3", R_INT, {R_INT}, &d3);
    insert(97, "d4", R_INT, {R_INT}, &d4);
    insert(98, "d6", R_INT, {R_INT}, &d6);
    insert(99, "d8", R_INT, {R_INT}, &d8);
    insert(100, "d10", R_INT, {R_INT}, &d10);
    insert(101, "d12", R_INT, {R_INT}, &d12);
    insert(102, "d20", R_INT, {R_INT}, &d20);
    insert(103, "d100", R_INT, {R_INT}, &d100);
    insert(104, "VectorMagnitude", R_FLOAT, {R_VECTOR}, &VectorMagnitude);
    insert(105, "GetMetaMagicFeat", R_INT, {}, &GetMetaMagicFeat);
    insert(106, "GetObjectType", R_INT, {R_OBJECT}, &GetObjectType);
    insert(107, "GetRacialType", R_INT, {R_OBJECT}, &GetRacialType);
    insert(108, "FortitudeSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &FortitudeSave);
    insert(109, "ReflexSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &ReflexSave);
    insert(110, "WillSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &WillSave);
    insert(111, "GetSpellSaveDC", R_INT, {}, &GetSpellSaveDC);
    insert(112, "MagicalEffect", R_EFFECT, {R_EFFECT}, &MagicalEffect);
    insert(113, "SupernaturalEffect", R_EFFECT, {R_EFFECT}, &SupernaturalEffect);
    insert(114, "ExtraordinaryEffect", R_EFFECT, {R_EFFECT}, &ExtraordinaryEffect);
    insert(116, "GetAC", R_INT, {R_OBJECT, R_INT}, &GetAC);
    insert(121, "RoundsToSeconds", R_FLOAT, {R_INT}, &RoundsToSeconds);
    insert(122, "HoursToSeconds", R_FLOAT, {R_INT}, &HoursToSeconds);
    insert(123, "TurnsToSeconds", R_FLOAT, {R_INT}, &TurnsToSeconds);
    insert(124, "SoundObjectSetFixedVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetFixedVariance);
    insert(125, "GetGoodEvilValue", R_INT, {R_OBJECT}, &GetGoodEvilValue);
    insert(126, "GetPartyMemberCount", R_INT, {}, &GetPartyMemberCount);
    insert(127, "GetAlignmentGoodEvil", R_INT, {R_OBJECT}, &GetAlignmentGoodEvil);
    insert(128, "GetFirstObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetFirstObjectInShape);
    insert(129, "GetNextObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetNextObjectInShape);
    insert(131, "SignalEvent", R_VOID, {R_OBJECT, R_EVENT}, &SignalEvent);
    insert(132, "EventUserDefined", R_EVENT, {R_INT}, &EventUserDefined);
    insert(137, "VectorNormalize", R_VECTOR, {R_VECTOR}, &VectorNormalize);
    insert(138, "GetItemStackSize", R_INT, {R_OBJECT}, &GetItemStackSize);
    insert(139, "GetAbilityScore", R_INT, {R_OBJECT, R_INT}, &GetAbilityScore);
    insert(140, "GetIsDead", R_INT, {R_OBJECT}, &GetIsDead);
    insert(141, "PrintVector", R_VOID, {R_VECTOR, R_INT}, &PrintVector);
    insert(142, "Vector", R_VECTOR, {R_FLOAT, R_FLOAT, R_FLOAT}, &Vector);
    insert(143, "SetFacingPoint", R_VOID, {R_VECTOR}, &SetFacingPoint);
    insert(144, "AngleToVector", R_VECTOR, {R_FLOAT}, &AngleToVector);
    insert(145, "VectorToAngle", R_FLOAT, {R_VECTOR}, &VectorToAngle);
    insert(146, "TouchAttackMelee", R_INT, {R_OBJECT, R_INT}, &TouchAttackMelee);
    insert(147, "TouchAttackRanged", R_INT, {R_OBJECT, R_INT}, &TouchAttackRanged);
    insert(150, "SetItemStackSize", R_VOID, {R_OBJECT, R_INT}, &SetItemStackSize);
    insert(151, "GetDistanceBetween", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween);
    insert(152, "SetReturnStrref", R_VOID, {R_INT, R_INT, R_INT}, &SetReturnStrref);
    insert(155, "GetItemInSlot", R_OBJECT, {R_INT, R_OBJECT}, &GetItemInSlot);
    insert(160, "SetGlobalString", R_VOID, {R_STRING, R_STRING}, &SetGlobalString);
    insert(162, "SetCommandable", R_VOID, {R_INT, R_OBJECT}, &SetCommandable);
    insert(163, "GetCommandable", R_INT, {R_OBJECT}, &GetCommandable);
    insert(166, "GetHitDice", R_INT, {R_OBJECT}, &GetHitDice);
    insert(168, "GetTag", R_STRING, {R_OBJECT}, &GetTag);
    insert(169, "ResistForce", R_INT, {R_OBJECT, R_OBJECT}, &ResistForce);
    insert(170, "GetEffectType", R_INT, {R_EFFECT}, &GetEffectType);
    insert(172, "GetFactionEqual", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionEqual);
    insert(173, "ChangeFaction", R_VOID, {R_OBJECT, R_OBJECT}, &ChangeFaction);
    insert(174, "GetIsListening", R_INT, {R_OBJECT}, &GetIsListening);
    insert(175, "SetListening", R_VOID, {R_OBJECT, R_INT}, &SetListening);
    insert(176, "SetListenPattern", R_VOID, {R_OBJECT, R_STRING, R_INT}, &SetListenPattern);
    insert(177, "TestStringAgainstPattern", R_INT, {R_STRING, R_STRING}, &TestStringAgainstPattern);
    insert(178, "GetMatchedSubstring", R_STRING, {R_INT}, &GetMatchedSubstring);
    insert(179, "GetMatchedSubstringsCount", R_INT, {}, &GetMatchedSubstringsCount);
    insert(181, "GetFactionWeakestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWeakestMember);
    insert(182, "GetFactionStrongestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionStrongestMember);
    insert(183, "GetFactionMostDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionMostDamagedMember);
    insert(184, "GetFactionLeastDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionLeastDamagedMember);
    insert(185, "GetFactionGold", R_INT, {R_OBJECT}, &GetFactionGold);
    insert(186, "GetFactionAverageReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionAverageReputation);
    insert(187, "GetFactionAverageGoodEvilAlignment", R_INT, {R_OBJECT}, &GetFactionAverageGoodEvilAlignment);
    insert(188, "SoundObjectGetFixedVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetFixedVariance);
    insert(189, "GetFactionAverageLevel", R_INT, {R_OBJECT}, &GetFactionAverageLevel);
    insert(190, "GetFactionAverageXP", R_INT, {R_OBJECT}, &GetFactionAverageXP);
    insert(191, "GetFactionMostFrequentClass", R_INT, {R_OBJECT}, &GetFactionMostFrequentClass);
    insert(192, "GetFactionWorstAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWorstAC);
    insert(193, "GetFactionBestAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionBestAC);
    insert(194, "GetGlobalString", R_STRING, {R_STRING}, &GetGlobalString);
    insert(195, "GetListenPatternNumber", R_INT, {}, &GetListenPatternNumber);
    insert(197, "GetWaypointByTag", R_OBJECT, {R_STRING}, &GetWaypointByTag);
    insert(198, "GetTransitionTarget", R_OBJECT, {R_OBJECT}, &GetTransitionTarget);
    insert(200, "GetObjectByTag", R_OBJECT, {R_STRING, R_INT}, &GetObjectByTag);
    insert(201, "AdjustAlignment", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustAlignment);
    insert(203, "SetAreaTransitionBMP", R_VOID, {R_INT, R_STRING}, &SetAreaTransitionBMP);
    insert(208, "GetReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetReputation);
    insert(209, "AdjustReputation", R_VOID, {R_OBJECT, R_OBJECT, R_INT}, &AdjustReputation);
    insert(210, "GetModuleFileName", R_STRING, {}, &GetModuleFileName);
    insert(211, "GetGoingToBeAttackedBy", R_OBJECT, {R_OBJECT}, &GetGoingToBeAttackedBy);
    insert(213, "GetLocation", R_LOCATION, {R_OBJECT}, &GetLocation);
    insert(215, "Location", R_LOCATION, {R_VECTOR, R_FLOAT}, &CreateLocation);
    insert(216, "ApplyEffectAtLocation", R_VOID, {R_INT, R_EFFECT, R_LOCATION, R_FLOAT}, &ApplyEffectAtLocation);
    insert(217, "GetIsPC", R_INT, {R_OBJECT}, &GetIsPC);
    insert(218, "FeetToMeters", R_FLOAT, {R_FLOAT}, &FeetToMeters);
    insert(219, "YardsToMeters", R_FLOAT, {R_FLOAT}, &YardsToMeters);
    insert(220, "ApplyEffectToObject", R_VOID, {R_INT, R_EFFECT, R_OBJECT, R_FLOAT}, &ApplyEffectToObject);
    insert(221, "SpeakString", R_VOID, {R_STRING, R_INT}, &SpeakString);
    insert(222, "GetSpellTargetLocation", R_LOCATION, {}, &GetSpellTargetLocation);
    insert(223, "GetPositionFromLocation", R_VECTOR, {R_LOCATION}, &GetPositionFromLocation);
    insert(225, "GetFacingFromLocation", R_FLOAT, {R_LOCATION}, &GetFacingFromLocation);
    insert(226, "GetNearestCreatureToLocation", R_OBJECT, {R_INT, R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreatureToLocation);
    insert(227, "GetNearestObject", R_OBJECT, {R_INT, R_OBJECT, R_INT}, &GetNearestObject);
    insert(228, "GetNearestObjectToLocation", R_OBJECT, {R_INT, R_LOCATION, R_INT}, &GetNearestObjectToLocation);
    insert(229, "GetNearestObjectByTag", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &GetNearestObjectByTag);
    insert(230, "IntToFloat", R_FLOAT, {R_INT}, &IntToFloat);
    insert(231, "FloatToInt", R_INT, {R_FLOAT}, &FloatToInt);
    insert(232, "StringToInt", R_INT, {R_STRING}, &StringToInt);
    insert(233, "StringToFloat", R_FLOAT, {R_STRING}, &StringToFloat);
    insert(235, "GetIsEnemy", R_INT, {R_OBJECT, R_OBJECT}, &GetIsEnemy);
    insert(236, "GetIsFriend", R_INT, {R_OBJECT, R_OBJECT}, &GetIsFriend);
    insert(237, "GetIsNeutral", R_INT, {R_OBJECT, R_OBJECT}, &GetIsNeutral);
    insert(238, "GetPCSpeaker", R_OBJECT, {}, &GetPCSpeaker);
    insert(239, "GetStringByStrRef", R_STRING, {R_INT}, &GetStringByStrRef);
    insert(241, "DestroyObject", R_VOID, {R_OBJECT, R_FLOAT, R_INT, R_FLOAT}, &DestroyObject);
    insert(242, "GetModule", R_OBJECT, {}, &GetModule);
    insert(243, "CreateObject", R_OBJECT, {R_INT, R_STRING, R_LOCATION, R_INT}, &CreateObject);
    insert(244, "EventSpellCastAt", R_EVENT, {R_OBJECT, R_INT, R_INT}, &EventSpellCastAt);
    insert(245, "GetLastSpellCaster", R_OBJECT, {}, &GetLastSpellCaster);
    insert(246, "GetLastSpell", R_INT, {}, &GetLastSpell);
    insert(247, "GetUserDefinedEventNumber", R_INT, {}, &GetUserDefinedEventNumber);
    insert(248, "GetSpellId", R_INT, {}, &GetSpellId);
    insert(249, "RandomName", R_STRING, {}, &RandomName);
    insert(251, "GetLoadFromSaveGame", R_INT, {}, &GetLoadFromSaveGame);
    insert(253, "GetName", R_STRING, {R_OBJECT}, &GetName);
    insert(254, "GetLastSpeaker", R_OBJECT, {}, &GetLastSpeaker);
    insert(255, "BeginConversation", R_INT, {R_STRING, R_OBJECT}, &BeginConversation);
    insert(256, "GetLastPerceived", R_OBJECT, {}, &GetLastPerceived);
    insert(257, "GetLastPerceptionHeard", R_INT, {}, &GetLastPerceptionHeard);
    insert(258, "GetLastPerceptionInaudible", R_INT, {}, &GetLastPerceptionInaudible);
    insert(259, "GetLastPerceptionSeen", R_INT, {}, &GetLastPerceptionSeen);
    insert(260, "GetLastClosedBy", R_OBJECT, {}, &GetLastClosedBy);
    insert(261, "GetLastPerceptionVanished", R_INT, {}, &GetLastPerceptionVanished);
    insert(262, "GetFirstInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetFirstInPersistentObject);
    insert(263, "GetNextInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetNextInPersistentObject);
    insert(264, "GetAreaOfEffectCreator", R_OBJECT, {R_OBJECT}, &GetAreaOfEffectCreator);
    insert(265, "ShowLevelUpGUI", R_INT, {}, &ShowLevelUpGUI);
    insert(266, "SetItemNonEquippable", R_VOID, {R_OBJECT, R_INT}, &SetItemNonEquippable);
    insert(267, "GetButtonMashCheck", R_INT, {}, &GetButtonMashCheck);
    insert(268, "SetButtonMashCheck", R_VOID, {R_INT}, &SetButtonMashCheck);
    insert(271, "GiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &GiveItem);
    insert(272, "ObjectToString", R_STRING, {R_OBJECT}, &ObjectToString);
    insert(274, "GetIsImmune", R_INT, {R_OBJECT, R_INT, R_OBJECT}, &GetIsImmune);
    insert(276, "GetEncounterActive", R_INT, {R_OBJECT}, &GetEncounterActive);
    insert(277, "SetEncounterActive", R_VOID, {R_INT, R_OBJECT}, &SetEncounterActive);
    insert(278, "GetEncounterSpawnsMax", R_INT, {R_OBJECT}, &GetEncounterSpawnsMax);
    insert(279, "SetEncounterSpawnsMax", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsMax);
    insert(280, "GetEncounterSpawnsCurrent", R_INT, {R_OBJECT}, &GetEncounterSpawnsCurrent);
    insert(281, "SetEncounterSpawnsCurrent", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsCurrent);
    insert(282, "GetModuleItemAcquired", R_OBJECT, {}, &GetModuleItemAcquired);
    insert(283, "GetModuleItemAcquiredFrom", R_OBJECT, {}, &GetModuleItemAcquiredFrom);
    insert(284, "SetCustomToken", R_VOID, {R_INT, R_STRING}, &SetCustomToken);
    insert(285, "GetHasFeat", R_INT, {R_INT, R_OBJECT}, &GetHasFeat);
    insert(286, "GetHasSkill", R_INT, {R_INT, R_OBJECT}, &GetHasSkill);
    insert(289, "GetObjectSeen", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectSeen);
    insert(290, "GetObjectHeard", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectHeard);
    insert(291, "GetLastPlayerDied", R_OBJECT, {}, &GetLastPlayerDied);
    insert(292, "GetModuleItemLost", R_OBJECT, {}, &GetModuleItemLost);
    insert(293, "GetModuleItemLostBy", R_OBJECT, {}, &GetModuleItemLostBy);
    insert(295, "EventConversation", R_EVENT, {}, &EventConversation);
    insert(296, "SetEncounterDifficulty", R_VOID, {R_INT, R_OBJECT}, &SetEncounterDifficulty);
    insert(297, "GetEncounterDifficulty", R_INT, {R_OBJECT}, &GetEncounterDifficulty);
    insert(298, "GetDistanceBetweenLocations", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations);
    insert(299, "GetReflexAdjustedDamage", R_INT, {R_INT, R_OBJECT, R_INT, R_INT, R_OBJECT}, &GetReflexAdjustedDamage);
    insert(300, "PlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &PlayAnimation);
    insert(301, "TalentSpell", R_TALENT, {R_INT}, &TalentSpell);
    insert(302, "TalentFeat", R_TALENT, {R_INT}, &TalentFeat);
    insert(303, "TalentSkill", R_TALENT, {R_INT}, &TalentSkill);
    insert(304, "GetHasSpellEffect", R_INT, {R_INT, R_OBJECT}, &GetHasSpellEffect);
    insert(305, "GetEffectSpellId", R_INT, {R_EFFECT}, &GetEffectSpellId);
    insert(306, "GetCreatureHasTalent", R_INT, {R_TALENT, R_OBJECT}, &GetCreatureHasTalent);
    insert(307, "GetCreatureTalentRandom", R_TALENT, {R_INT, R_OBJECT, R_INT}, &GetCreatureTalentRandom);
    insert(308, "GetCreatureTalentBest", R_TALENT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT}, &GetCreatureTalentBest);
    insert(311, "GetGoldPieceValue", R_INT, {R_OBJECT}, &GetGoldPieceValue);
    insert(312, "GetIsPlayableRacialType", R_INT, {R_OBJECT}, &GetIsPlayableRacialType);
    insert(313, "JumpToLocation", R_VOID, {R_LOCATION}, &JumpToLocation);
    insert(315, "GetSkillRank", R_INT, {R_INT, R_OBJECT}, &GetSkillRank);
    insert(316, "GetAttackTarget", R_OBJECT, {R_OBJECT}, &GetAttackTarget);
    insert(317, "GetLastAttackType", R_INT, {R_OBJECT}, &GetLastAttackType);
    insert(318, "GetLastAttackMode", R_INT, {R_OBJECT}, &GetLastAttackMode);
    insert(319, "GetDistanceBetween2D", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween2D);
    insert(320, "GetIsInCombat", R_INT, {R_OBJECT}, &GetIsInCombat);
    insert(321, "GetLastAssociateCommand", R_INT, {R_OBJECT}, &GetLastAssociateCommand);
    insert(322, "GiveGoldToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveGoldToCreature);
    insert(323, "SetIsDestroyable", R_VOID, {R_INT, R_INT, R_INT}, &SetIsDestroyable);
    insert(324, "SetLocked", R_VOID, {R_OBJECT, R_INT}, &SetLocked);
    insert(325, "GetLocked", R_INT, {R_OBJECT}, &GetLocked);
    insert(326, "GetClickingObject", R_OBJECT, {}, &GetClickingObject);
    insert(327, "SetAssociateListenPatterns", R_VOID, {R_OBJECT}, &SetAssociateListenPatterns);
    insert(328, "GetLastWeaponUsed", R_OBJECT, {R_OBJECT}, &GetLastWeaponUsed);
    insert(330, "GetLastUsedBy", R_OBJECT, {}, &GetLastUsedBy);
    insert(331, "GetAbilityModifier", R_INT, {R_INT, R_OBJECT}, &GetAbilityModifier);
    insert(332, "GetIdentified", R_INT, {R_OBJECT}, &GetIdentified);
    insert(333, "SetIdentified", R_VOID, {R_OBJECT, R_INT}, &SetIdentified);
    insert(334, "GetDistanceBetweenLocations2D", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations2D);
    insert(335, "GetDistanceToObject2D", R_FLOAT, {R_OBJECT}, &GetDistanceToObject2D);
    insert(336, "GetBlockingDoor", R_OBJECT, {}, &GetBlockingDoor);
    insert(337, "GetIsDoorActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsDoorActionPossible);
    insert(338, "DoDoorAction", R_VOID, {R_OBJECT, R_INT}, &DoDoorAction);
    insert(339, "GetFirstItemInInventory", R_OBJECT, {R_OBJECT}, &GetFirstItemInInventory);
    insert(340, "GetNextItemInInventory", R_OBJECT, {R_OBJECT}, &GetNextItemInInventory);
    insert(341, "GetClassByPosition", R_INT, {R_INT, R_OBJECT}, &GetClassByPosition);
    insert(342, "GetLevelByPosition", R_INT, {R_INT, R_OBJECT}, &GetLevelByPosition);
    insert(343, "GetLevelByClass", R_INT, {R_INT, R_OBJECT}, &GetLevelByClass);
    insert(344, "GetDamageDealtByType", R_INT, {R_INT}, &GetDamageDealtByType);
    insert(345, "GetTotalDamageDealt", R_INT, {}, &GetTotalDamageDealt);
    insert(346, "GetLastDamager", R_OBJECT, {}, &GetLastDamager);
    insert(347, "GetLastDisarmed", R_OBJECT, {}, &GetLastDisarmed);
    insert(348, "GetLastDisturbed", R_OBJECT, {}, &GetLastDisturbed);
    insert(349, "GetLastLocked", R_OBJECT, {}, &GetLastLocked);
    insert(350, "GetLastUnlocked", R_OBJECT, {}, &GetLastUnlocked);
    insert(352, "GetInventoryDisturbType", R_INT, {}, &GetInventoryDisturbType);
    insert(353, "GetInventoryDisturbItem", R_OBJECT, {}, &GetInventoryDisturbItem);
    insert(354, "ShowUpgradeScreen", R_VOID, {R_OBJECT}, &ShowUpgradeScreen);
    insert(355, "VersusAlignmentEffect", R_EFFECT, {R_EFFECT, R_INT, R_INT}, &VersusAlignmentEffect);
    insert(356, "VersusRacialTypeEffect", R_EFFECT, {R_EFFECT, R_INT}, &VersusRacialTypeEffect);
    insert(357, "VersusTrapEffect", R_EFFECT, {R_EFFECT}, &VersusTrapEffect);
    insert(358, "GetGender", R_INT, {R_OBJECT}, &GetGender);
    insert(359, "GetIsTalentValid", R_INT, {R_TALENT}, &GetIsTalentValid);
    insert(361, "GetAttemptedAttackTarget", R_OBJECT, {}, &GetAttemptedAttackTarget);
    insert(362, "GetTypeFromTalent", R_INT, {R_TALENT}, &GetTypeFromTalent);
    insert(363, "GetIdFromTalent", R_INT, {R_TALENT}, &GetIdFromTalent);
    insert(364, "PlayPazaak", R_VOID, {R_INT, R_STRING, R_INT, R_INT, R_OBJECT}, &PlayPazaak);
    insert(365, "GetLastPazaakResult", R_INT, {}, &GetLastPazaakResult);
    insert(366, "DisplayFeedBackText", R_VOID, {R_OBJECT, R_INT}, &DisplayFeedBackText);
    insert(367, "AddJournalQuestEntry", R_VOID, {R_STRING, R_INT, R_INT}, &AddJournalQuestEntry);
    insert(368, "RemoveJournalQuestEntry", R_VOID, {R_STRING}, &RemoveJournalQuestEntry);
    insert(369, "GetJournalEntry", R_INT, {R_STRING}, &GetJournalEntry);
    insert(370, "PlayRumblePattern", R_INT, {R_INT}, &PlayRumblePattern);
    insert(371, "StopRumblePattern", R_INT, {R_INT}, &StopRumblePattern);
    insert(374, "SendMessageToPC", R_VOID, {R_OBJECT, R_STRING}, &SendMessageToPC);
    insert(375, "GetAttemptedSpellTarget", R_OBJECT, {}, &GetAttemptedSpellTarget);
    insert(376, "GetLastOpenedBy", R_OBJECT, {}, &GetLastOpenedBy);
    insert(377, "GetHasSpell", R_INT, {R_INT, R_OBJECT}, &GetHasSpell);
    insert(378, "OpenStore", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &OpenStore);
    insert(380, "GetFirstFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstFactionMember);
    insert(381, "GetNextFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetNextFactionMember);
    insert(384, "GetJournalQuestExperience", R_INT, {R_STRING}, &GetJournalQuestExperience);
    insert(385, "JumpToObject", R_VOID, {R_OBJECT, R_INT}, &JumpToObject);
    insert(386, "SetMapPinEnabled", R_VOID, {R_OBJECT, R_INT}, &SetMapPinEnabled);
    insert(388, "PopUpGUIPanel", R_VOID, {R_OBJECT, R_INT}, &PopUpGUIPanel);
    insert(389, "AddMultiClass", R_VOID, {R_INT, R_OBJECT}, &AddMultiClass);
    insert(390, "GetIsLinkImmune", R_INT, {R_OBJECT, R_EFFECT}, &GetIsLinkImmune);
    insert(393, "GiveXPToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveXPToCreature);
    insert(394, "SetXP", R_VOID, {R_OBJECT, R_INT}, &SetXP);
    insert(395, "GetXP", R_INT, {R_OBJECT}, &GetXP);
    insert(396, "IntToHexString", R_STRING, {R_INT}, &IntToHexString);
    insert(397, "GetBaseItemType", R_INT, {R_OBJECT}, &GetBaseItemType);
    insert(398, "GetItemHasItemProperty", R_INT, {R_OBJECT, R_INT}, &GetItemHasItemProperty);
    insert(401, "GetItemACValue", R_INT, {R_OBJECT}, &GetItemACValue);
    insert(403, "ExploreAreaForPlayer", R_VOID, {R_OBJECT, R_OBJECT}, &ExploreAreaForPlayer);
    insert(405, "GetIsDay", R_INT, {}, &GetIsDay);
    insert(406, "GetIsNight", R_INT, {}, &GetIsNight);
    insert(407, "GetIsDawn", R_INT, {}, &GetIsDawn);
    insert(408, "GetIsDusk", R_INT, {}, &GetIsDusk);
    insert(409, "GetIsEncounterCreature", R_INT, {R_OBJECT}, &GetIsEncounterCreature);
    insert(410, "GetLastPlayerDying", R_OBJECT, {}, &GetLastPlayerDying);
    insert(411, "GetStartingLocation", R_LOCATION, {}, &GetStartingLocation);
    insert(412, "ChangeToStandardFaction", R_VOID, {R_OBJECT, R_INT}, &ChangeToStandardFaction);
    insert(413, "SoundObjectPlay", R_VOID, {R_OBJECT}, &SoundObjectPlay);
    insert(414, "SoundObjectStop", R_VOID, {R_OBJECT}, &SoundObjectStop);
    insert(415, "SoundObjectSetVolume", R_VOID, {R_OBJECT, R_INT}, &SoundObjectSetVolume);
    insert(416, "SoundObjectSetPosition", R_VOID, {R_OBJECT, R_VECTOR}, &SoundObjectSetPosition);
    insert(417, "SpeakOneLinerConversation", R_VOID, {R_STRING, R_OBJECT}, &SpeakOneLinerConversation);
    insert(418, "GetGold", R_INT, {R_OBJECT}, &GetGold);
    insert(419, "GetLastRespawnButtonPresser", R_OBJECT, {}, &GetLastRespawnButtonPresser);
    insert(421, "SetLightsaberPowered", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &SetLightsaberPowered);
    insert(422, "GetIsWeaponEffective", R_INT, {R_OBJECT, R_INT}, &GetIsWeaponEffective);
    insert(423, "GetLastSpellHarmful", R_INT, {}, &GetLastSpellHarmful);
    insert(424, "EventActivateItem", R_EVENT, {R_OBJECT, R_LOCATION, R_OBJECT}, &EventActivateItem);
    insert(425, "MusicBackgroundPlay", R_VOID, {R_OBJECT}, &MusicBackgroundPlay);
    insert(426, "MusicBackgroundStop", R_VOID, {R_OBJECT}, &MusicBackgroundStop);
    insert(427, "MusicBackgroundSetDelay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundSetDelay);
    insert(428, "MusicBackgroundChangeDay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundChangeDay);
    insert(429, "MusicBackgroundChangeNight", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundChangeNight);
    insert(430, "MusicBattlePlay", R_VOID, {R_OBJECT}, &MusicBattlePlay);
    insert(431, "MusicBattleStop", R_VOID, {R_OBJECT}, &MusicBattleStop);
    insert(432, "MusicBattleChange", R_VOID, {R_OBJECT, R_INT}, &MusicBattleChange);
    insert(433, "AmbientSoundPlay", R_VOID, {R_OBJECT}, &AmbientSoundPlay);
    insert(434, "AmbientSoundStop", R_VOID, {R_OBJECT}, &AmbientSoundStop);
    insert(435, "AmbientSoundChangeDay", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeDay);
    insert(436, "AmbientSoundChangeNight", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeNight);
    insert(437, "GetLastKiller", R_OBJECT, {}, &GetLastKiller);
    insert(438, "GetSpellCastItem", R_OBJECT, {}, &GetSpellCastItem);
    insert(439, "GetItemActivated", R_OBJECT, {}, &GetItemActivated);
    insert(440, "GetItemActivator", R_OBJECT, {}, &GetItemActivator);
    insert(441, "GetItemActivatedTargetLocation", R_LOCATION, {}, &GetItemActivatedTargetLocation);
    insert(442, "GetItemActivatedTarget", R_OBJECT, {}, &GetItemActivatedTarget);
    insert(443, "GetIsOpen", R_INT, {R_OBJECT}, &GetIsOpen);
    insert(444, "TakeGoldFromCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &TakeGoldFromCreature);
    insert(445, "GetIsInConversation", R_INT, {R_OBJECT}, &GetIsInConversation);
    insert(455, "GetPlotFlag", R_INT, {R_OBJECT}, &GetPlotFlag);
    insert(456, "SetPlotFlag", R_VOID, {R_OBJECT, R_INT}, &SetPlotFlag);
    insert(461, "SetDialogPlaceableCamera", R_VOID, {R_INT}, &SetDialogPlaceableCamera);
    insert(462, "GetSoloMode", R_INT, {}, &GetSoloMode);
    insert(464, "GetMaxStealthXP", R_INT, {}, &GetMaxStealthXP);
    insert(468, "SetMaxStealthXP", R_VOID, {R_INT}, &SetMaxStealthXP);
    insert(474, "GetCurrentStealthXP", R_INT, {}, &GetCurrentStealthXP);
    insert(475, "GetNumStackedItems", R_INT, {R_OBJECT}, &GetNumStackedItems);
    insert(476, "SurrenderToEnemies", R_VOID, {}, &SurrenderToEnemies);
    insert(478, "SetCurrentStealthXP", R_VOID, {R_INT}, &SetCurrentStealthXP);
    insert(479, "GetCreatureSize", R_INT, {R_OBJECT}, &GetCreatureSize);
    insert(480, "AwardStealthXP", R_VOID, {R_OBJECT}, &AwardStealthXP);
    insert(481, "GetStealthXPEnabled", R_INT, {}, &GetStealthXPEnabled);
    insert(482, "SetStealthXPEnabled", R_VOID, {R_INT}, &SetStealthXPEnabled);
    insert(486, "GetLastTrapDetected", R_OBJECT, {R_OBJECT}, &GetLastTrapDetected);
    insert(488, "GetNearestTrapToObject", R_OBJECT, {R_OBJECT, R_INT}, &GetNearestTrapToObject);
    insert(489, "GetAttemptedMovementTarget", R_OBJECT, {}, &GetAttemptedMovementTarget);
    insert(490, "GetBlockingCreature", R_OBJECT, {R_OBJECT}, &GetBlockingCreature);
    insert(491, "GetFortitudeSavingThrow", R_INT, {R_OBJECT}, &GetFortitudeSavingThrow);
    insert(492, "GetWillSavingThrow", R_INT, {R_OBJECT}, &GetWillSavingThrow);
    insert(493, "GetReflexSavingThrow", R_INT, {R_OBJECT}, &GetReflexSavingThrow);
    insert(494, "GetChallengeRating", R_FLOAT, {R_OBJECT}, &GetChallengeRating);
    insert(495, "GetFoundEnemyCreature", R_OBJECT, {R_OBJECT}, &GetFoundEnemyCreature);
    insert(496, "GetMovementRate", R_INT, {R_OBJECT}, &GetMovementRate);
    insert(497, "GetSubRace", R_INT, {R_OBJECT}, &GetSubRace);
    insert(498, "GetStealthXPDecrement", R_INT, {}, &GetStealthXPDecrement);
    insert(499, "SetStealthXPDecrement", R_VOID, {R_INT}, &SetStealthXPDecrement);
    insert(500, "DuplicateHeadAppearance", R_VOID, {R_OBJECT, R_OBJECT}, &DuplicateHeadAppearance);
    insert(503, "CutsceneAttack", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &CutsceneAttack);
    insert(504, "SetCameraMode", R_VOID, {R_OBJECT, R_INT}, &SetCameraMode);
    insert(505, "SetLockOrientationInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockOrientationInDialog);
    insert(506, "SetLockHeadFollowInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockHeadFollowInDialog);
    insert(507, "CutsceneMove", R_VOID, {R_OBJECT, R_VECTOR, R_INT}, &CutsceneMove);
    insert(508, "EnableVideoEffect", R_VOID, {R_INT}, &EnableVideoEffect);
    insert(509, "StartNewModule", R_VOID, {R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING}, &StartNewModule);
    insert(510, "DisableVideoEffect", R_VOID, {}, &DisableVideoEffect);
    insert(511, "GetWeaponRanged", R_INT, {R_OBJECT}, &GetWeaponRanged);
    insert(512, "DoSinglePlayerAutoSave", R_VOID, {}, &DoSinglePlayerAutoSave);
    insert(513, "GetGameDifficulty", R_INT, {}, &GetGameDifficulty);
    insert(514, "GetUserActionsPending", R_INT, {}, &GetUserActionsPending);
    insert(515, "RevealMap", R_VOID, {R_VECTOR, R_INT}, &RevealMap);
    insert(516, "SetTutorialWindowsEnabled", R_VOID, {R_INT}, &SetTutorialWindowsEnabled);
    insert(517, "ShowTutorialWindow", R_VOID, {R_INT}, &ShowTutorialWindow);
    insert(518, "StartCreditSequence", R_VOID, {R_INT}, &StartCreditSequence);
    insert(519, "IsCreditSequenceInProgress", R_INT, {}, &IsCreditSequenceInProgress);
    insert(522, "GetCurrentAction", R_INT, {R_OBJECT}, &GetCurrentAction);
    insert(523, "GetDifficultyModifier", R_FLOAT, {}, &GetDifficultyModifier);
    insert(524, "GetAppearanceType", R_INT, {R_OBJECT}, &GetAppearanceType);
    insert(525, "FloatingTextStrRefOnCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &FloatingTextStrRefOnCreature);
    insert(526, "FloatingTextStringOnCreature", R_VOID, {R_STRING, R_OBJECT, R_INT}, &FloatingTextStringOnCreature);
    insert(527, "GetTrapDisarmable", R_INT, {R_OBJECT}, &GetTrapDisarmable);
    insert(528, "GetTrapDetectable", R_INT, {R_OBJECT}, &GetTrapDetectable);
    insert(529, "GetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &GetTrapDetectedBy);
    insert(530, "GetTrapFlagged", R_INT, {R_OBJECT}, &GetTrapFlagged);
    insert(531, "GetTrapBaseType", R_INT, {R_OBJECT}, &GetTrapBaseType);
    insert(532, "GetTrapOneShot", R_INT, {R_OBJECT}, &GetTrapOneShot);
    insert(533, "GetTrapCreator", R_OBJECT, {R_OBJECT}, &GetTrapCreator);
    insert(534, "GetTrapKeyTag", R_STRING, {R_OBJECT}, &GetTrapKeyTag);
    insert(535, "GetTrapDisarmDC", R_INT, {R_OBJECT}, &GetTrapDisarmDC);
    insert(536, "GetTrapDetectDC", R_INT, {R_OBJECT}, &GetTrapDetectDC);
    insert(537, "GetLockKeyRequired", R_INT, {R_OBJECT}, &GetLockKeyRequired);
    insert(538, "GetLockKeyTag", R_STRING, {R_OBJECT}, &GetLockKeyTag);
    insert(539, "GetLockLockable", R_INT, {R_OBJECT}, &GetLockLockable);
    insert(540, "GetLockUnlockDC", R_INT, {R_OBJECT}, &GetLockUnlockDC);
    insert(541, "GetLockLockDC", R_INT, {R_OBJECT}, &GetLockLockDC);
    insert(542, "GetPCLevellingUp", R_OBJECT, {}, &GetPCLevellingUp);
    insert(543, "GetHasFeatEffect", R_INT, {R_INT, R_OBJECT}, &GetHasFeatEffect);
    insert(544, "SetPlaceableIllumination", R_VOID, {R_OBJECT, R_INT}, &SetPlaceableIllumination);
    insert(545, "GetPlaceableIllumination", R_INT, {R_OBJECT}, &GetPlaceableIllumination);
    insert(546, "GetIsPlaceableObjectActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsPlaceableObjectActionPossible);
    insert(547, "DoPlaceableObjectAction", R_VOID, {R_OBJECT, R_INT}, &DoPlaceableObjectAction);
    insert(548, "GetFirstPC", R_OBJECT, {}, &GetFirstPC);
    insert(549, "GetNextPC", R_OBJECT, {}, &GetNextPC);
    insert(550, "SetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &SetTrapDetectedBy);
    insert(551, "GetIsTrapped", R_INT, {R_OBJECT}, &GetIsTrapped);
    insert(552, "SetEffectIcon", R_EFFECT, {R_EFFECT, R_INT}, &SetEffectIcon);
    insert(553, "FaceObjectAwayFromObject", R_VOID, {R_OBJECT, R_OBJECT}, &FaceObjectAwayFromObject);
    insert(554, "PopUpDeathGUIPanel", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT, R_STRING}, &PopUpDeathGUIPanel);
    insert(555, "SetTrapDisabled", R_VOID, {R_OBJECT}, &SetTrapDisabled);
    insert(556, "GetLastHostileActor", R_OBJECT, {R_OBJECT}, &GetLastHostileActor);
    insert(557, "ExportAllCharacters", R_VOID, {}, &ExportAllCharacters);
    insert(558, "MusicBackgroundGetDayTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetDayTrack);
    insert(559, "MusicBackgroundGetNightTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetNightTrack);
    insert(560, "WriteTimestampedLogEntry", R_VOID, {R_STRING}, &WriteTimestampedLogEntry);
    insert(561, "GetModuleName", R_STRING, {}, &GetModuleName);
    insert(562, "GetFactionLeader", R_OBJECT, {R_OBJECT}, &GetFactionLeader);
    insert(564, "EndGame", R_VOID, {R_INT}, &EndGame);
    insert(565, "GetRunScriptVar", R_INT, {}, &GetRunScriptVar);
    insert(566, "GetCreatureMovmentType", R_INT, {R_OBJECT}, &GetCreatureMovmentType);
    insert(567, "AmbientSoundSetDayVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetDayVolume);
    insert(568, "AmbientSoundSetNightVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetNightVolume);
    insert(569, "MusicBackgroundGetBattleTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetBattleTrack);
    insert(570, "GetHasInventory", R_INT, {R_OBJECT}, &GetHasInventory);
    insert(571, "GetStrRefSoundDuration", R_FLOAT, {R_INT}, &GetStrRefSoundDuration);
    insert(572, "AddToParty", R_VOID, {R_OBJECT, R_OBJECT}, &AddToParty);
    insert(573, "RemoveFromParty", R_VOID, {R_OBJECT}, &RemoveFromParty);
    insert(574, "AddPartyMember", R_INT, {R_INT, R_OBJECT}, &AddPartyMember);
    insert(575, "RemovePartyMember", R_INT, {R_INT}, &RemovePartyMember);
    insert(576, "IsObjectPartyMember", R_INT, {R_OBJECT}, &IsObjectPartyMember);
    insert(577, "GetPartyMemberByIndex", R_OBJECT, {R_INT}, &GetPartyMemberByIndex);
    insert(578, "GetGlobalBoolean", R_INT, {R_STRING}, &GetGlobalBoolean);
    insert(579, "SetGlobalBoolean", R_VOID, {R_STRING, R_INT}, &SetGlobalBoolean);
    insert(580, "GetGlobalNumber", R_INT, {R_STRING}, &GetGlobalNumber);
    insert(581, "SetGlobalNumber", R_VOID, {R_STRING, R_INT}, &SetGlobalNumber);
    insert(582, "AurPostString", R_VOID, {R_STRING, R_INT, R_INT, R_FLOAT}, &AurPostString);
    insert(669, "AddJournalWorldEntry", R_VOID, {R_INT, R_STRING, R_STRING}, &AddJournalWorldEntry);
    insert(670, "AddJournalWorldEntryStrref", R_VOID, {R_INT, R_INT}, &AddJournalWorldEntryStrref);
    insert(671, "BarkString", R_VOID, {R_OBJECT, R_INT}, &BarkString);
    insert(672, "DeleteJournalWorldAllEntries", R_VOID, {}, &DeleteJournalWorldAllEntries);
    insert(673, "DeleteJournalWorldEntry", R_VOID, {R_INT}, &DeleteJournalWorldEntry);
    insert(674, "DeleteJournalWorldEntryStrref", R_VOID, {R_INT}, &DeleteJournalWorldEntryStrref);
    insert(677, "PlayVisualAreaEffect", R_VOID, {R_INT, R_LOCATION}, &PlayVisualAreaEffect);
    insert(678, "SetJournalQuestEntryPicture", R_VOID, {R_STRING, R_OBJECT, R_INT, R_INT, R_INT}, &SetJournalQuestEntryPicture);
    insert(679, "GetLocalBoolean", R_INT, {R_OBJECT, R_INT}, &GetLocalBoolean);
    insert(680, "SetLocalBoolean", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalBoolean);
    insert(681, "GetLocalNumber", R_INT, {R_OBJECT, R_INT}, &GetLocalNumber);
    insert(682, "SetLocalNumber", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalNumber);
    insert(689, "SoundObjectGetPitchVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetPitchVariance);
    insert(690, "SoundObjectSetPitchVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetPitchVariance);
    insert(691, "SoundObjectGetVolume", R_INT, {R_OBJECT}, &SoundObjectGetVolume);
    insert(692, "GetGlobalLocation", R_LOCATION, {R_STRING}, &GetGlobalLocation);
    insert(693, "SetGlobalLocation", R_VOID, {R_STRING, R_LOCATION}, &SetGlobalLocation);
    insert(694, "AddAvailableNPCByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailableNPCByObject);
    insert(695, "RemoveAvailableNPC", R_INT, {R_INT}, &RemoveAvailableNPC);
    insert(696, "IsAvailableCreature", R_INT, {R_INT}, &IsAvailableCreature);
    insert(697, "AddAvailableNPCByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailableNPCByTemplate);
    insert(698, "SpawnAvailableNPC", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailableNPC);
    insert(699, "IsNPCPartyMember", R_INT, {R_INT}, &IsNPCPartyMember);
    insert(701, "GetIsConversationActive", R_INT, {}, &GetIsConversationActive);
    insert(704, "GetPartyAIStyle", R_INT, {}, &GetPartyAIStyle);
    insert(705, "GetNPCAIStyle", R_INT, {R_OBJECT}, &GetNPCAIStyle);
    insert(706, "SetPartyAIStyle", R_VOID, {R_INT}, &SetPartyAIStyle);
    insert(707, "SetNPCAIStyle", R_VOID, {R_OBJECT, R_INT}, &SetNPCAIStyle);
    insert(708, "SetNPCSelectability", R_VOID, {R_INT, R_INT}, &SetNPCSelectability);
    insert(709, "GetNPCSelectability", R_INT, {R_INT}, &GetNPCSelectability);
    insert(710, "ClearAllEffects", R_VOID, {}, &ClearAllEffects);
    insert(711, "GetLastConversation", R_STRING, {}, &GetLastConversation);
    insert(712, "ShowPartySelectionGUI", R_VOID, {R_STRING, R_INT, R_INT}, &ShowPartySelectionGUI);
    insert(713, "GetStandardFaction", R_INT, {R_OBJECT}, &GetStandardFaction);
    insert(714, "GivePlotXP", R_VOID, {R_STRING, R_INT}, &GivePlotXP);
    insert(715, "GetMinOneHP", R_INT, {R_OBJECT}, &GetMinOneHP);
    insert(716, "SetMinOneHP", R_VOID, {R_OBJECT, R_INT}, &SetMinOneHP);
    insert(719, "SetGlobalFadeIn", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeIn);
    insert(720, "SetGlobalFadeOut", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeOut);
    insert(721, "GetLastHostileTarget", R_OBJECT, {R_OBJECT}, &GetLastHostileTarget);
    insert(722, "GetLastAttackAction", R_INT, {R_OBJECT}, &GetLastAttackAction);
    insert(723, "GetLastForcePowerUsed", R_INT, {R_OBJECT}, &GetLastForcePowerUsed);
    insert(724, "GetLastCombatFeatUsed", R_INT, {R_OBJECT}, &GetLastCombatFeatUsed);
    insert(725, "GetLastAttackResult", R_INT, {R_OBJECT}, &GetLastAttackResult);
    insert(726, "GetWasForcePowerSuccessful", R_INT, {R_OBJECT}, &GetWasForcePowerSuccessful);
    insert(727, "GetFirstAttacker", R_OBJECT, {R_OBJECT}, &GetFirstAttacker);
    insert(728, "GetNextAttacker", R_OBJECT, {R_OBJECT}, &GetNextAttacker);
    insert(729, "SetFormation", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &SetFormation);
    insert(731, "SetForcePowerUnsuccessful", R_VOID, {R_INT, R_OBJECT}, &SetForcePowerUnsuccessful);
    insert(732, "GetIsDebilitated", R_INT, {R_OBJECT}, &GetIsDebilitated);
    insert(733, "PlayMovie", R_VOID, {R_STRING}, &PlayMovie);
    insert(734, "SaveNPCState", R_VOID, {R_INT}, &SaveNPCState);
    insert(735, "GetCategoryFromTalent", R_INT, {R_TALENT}, &GetCategoryFromTalent);
    insert(736, "SurrenderByFaction", R_VOID, {R_INT, R_INT}, &SurrenderByFaction);
    insert(737, "ChangeFactionByFaction", R_VOID, {R_INT, R_INT}, &ChangeFactionByFaction);
    insert(738, "PlayRoomAnimation", R_VOID, {R_STRING, R_INT}, &PlayRoomAnimation);
    insert(739, "ShowGalaxyMap", R_VOID, {R_INT}, &ShowGalaxyMap);
    insert(740, "SetPlanetSelectable", R_VOID, {R_INT, R_INT}, &SetPlanetSelectable);
    insert(741, "GetPlanetSelectable", R_INT, {R_INT}, &GetPlanetSelectable);
    insert(742, "SetPlanetAvailable", R_VOID, {R_INT, R_INT}, &SetPlanetAvailable);
    insert(743, "GetPlanetAvailable", R_INT, {R_INT}, &GetPlanetAvailable);
    insert(744, "GetSelectedPlanet", R_INT, {}, &GetSelectedPlanet);
    insert(745, "SoundObjectFadeAndStop", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectFadeAndStop);
    insert(746, "SetAreaFogColor", R_VOID, {R_OBJECT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetAreaFogColor);
    insert(747, "ChangeItemCost", R_VOID, {R_STRING, R_FLOAT}, &ChangeItemCost);
    insert(748, "GetIsLiveContentAvailable", R_INT, {R_INT}, &GetIsLiveContentAvailable);
    insert(749, "ResetDialogState", R_VOID, {}, &ResetDialogState);
    insert(750, "SetGoodEvilValue", R_VOID, {R_OBJECT, R_INT}, &SetGoodEvilValue);
    insert(751, "GetIsPoisoned", R_INT, {R_OBJECT}, &GetIsPoisoned);
    insert(752, "GetSpellTarget", R_OBJECT, {R_OBJECT}, &GetSpellTarget);
    insert(753, "SetSoloMode", R_VOID, {R_INT}, &SetSoloMode);
    insert(757, "CancelPostDialogCharacterSwitch", R_VOID, {}, &CancelPostDialogCharacterSwitch);
    insert(758, "SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SetMaxHitPoints);
    insert(759, "NoClicksFor", R_VOID, {R_FLOAT}, &NoClicksFor);
    insert(760, "HoldWorldFadeInForDialog", R_VOID, {}, &HoldWorldFadeInForDialog);
    insert(761, "ShipBuild", R_INT, {}, &ShipBuild);
    insert(762, "SurrenderRetainBuffs", R_VOID, {}, &SurrenderRetainBuffs);
    insert(763, "SuppressStatusSummaryEntry", R_VOID, {R_INT}, &SuppressStatusSummaryEntry);
    insert(764, "GetCheatCode", R_INT, {R_INT}, &GetCheatCode);
    insert(765, "SetMusicVolume", R_VOID, {R_FLOAT}, &SetMusicVolume);
    insert(766, "CreateItemOnFloor", R_OBJECT, {R_STRING, R_LOCATION, R_INT}, &CreateItemOnFloor);
    insert(767, "SetAvailableNPCId", R_VOID, {R_INT, R_OBJECT}, &SetAvailableNPCId);
    insert(768, "IsMoviePlaying", R_INT, {}, &IsMoviePlaying);
    insert(769, "QueueMovie", R_VOID, {R_STRING, R_INT}, &QueueMovie);
    insert(770, "PlayMovieQueue", R_VOID, {R_INT}, &PlayMovieQueue);
    insert(771, "YavinHackCloseDoor", R_VOID, {R_OBJECT}, &YavinHackDoorClose);
}

void Routines::registerMainTslRoutines() {
    insert(0, "Random", R_INT, {R_INT}, &Random);
    insert(1, "PrintString", R_VOID, {R_STRING}, &PrintString);
    insert(2, "PrintFloat", R_VOID, {R_FLOAT, R_INT, R_INT}, &PrintFloat);
    insert(3, "FloatToString", R_STRING, {R_FLOAT, R_INT, R_INT}, &FloatToString);
    insert(4, "PrintInteger", R_VOID, {R_INT}, &PrintInteger);
    insert(5, "PrintObject", R_VOID, {R_OBJECT}, &PrintObject);
    insert(6, "AssignCommand", R_VOID, {R_OBJECT, R_ACTION}, &AssignCommand);
    insert(7, "DelayCommand", R_VOID, {R_FLOAT, R_ACTION}, &DelayCommand);
    insert(8, "ExecuteScript", R_VOID, {R_STRING, R_OBJECT, R_INT}, &ExecuteScript);
    insert(9, "ClearAllActions", R_VOID, {}, &ClearAllActions);
    insert(10, "SetFacing", R_VOID, {R_FLOAT}, &SetFacing);
    insert(11, "SwitchPlayerCharacter", R_INT, {R_INT}, &SwitchPlayerCharacter);
    insert(12, "SetTime", R_VOID, {R_INT, R_INT, R_INT, R_INT}, &SetTime);
    insert(13, "SetPartyLeader", R_INT, {R_INT}, &SetPartyLeader);
    insert(14, "SetAreaUnescapable", R_VOID, {R_INT}, &SetAreaUnescapable);
    insert(15, "GetAreaUnescapable", R_INT, {}, &GetAreaUnescapable);
    insert(16, "GetTimeHour", R_INT, {}, &GetTimeHour);
    insert(17, "GetTimeMinute", R_INT, {}, &GetTimeMinute);
    insert(18, "GetTimeSecond", R_INT, {}, &GetTimeSecond);
    insert(19, "GetTimeMillisecond", R_INT, {}, &GetTimeMillisecond);
    insert(24, "GetArea", R_OBJECT, {R_OBJECT}, &GetArea);
    insert(25, "GetEnteringObject", R_OBJECT, {}, &GetEnteringObject);
    insert(26, "GetExitingObject", R_OBJECT, {}, &GetExitingObject);
    insert(27, "GetPosition", R_VECTOR, {R_OBJECT}, &GetPosition);
    insert(28, "GetFacing", R_FLOAT, {R_OBJECT}, &GetFacing);
    insert(29, "GetItemPossessor", R_OBJECT, {R_OBJECT}, &GetItemPossessor);
    insert(30, "GetItemPossessedBy", R_OBJECT, {R_OBJECT, R_STRING}, &GetItemPossessedBy);
    insert(31, "CreateItemOnObject", R_OBJECT, {R_STRING, R_OBJECT, R_INT, R_INT}, &CreateItemOnObject);
    insert(36, "GetLastAttacker", R_OBJECT, {R_OBJECT}, &GetLastAttacker);
    insert(38, "GetNearestCreature", R_OBJECT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreature);
    insert(41, "GetDistanceToObject", R_FLOAT, {R_OBJECT}, &GetDistanceToObject);
    insert(42, "GetIsObjectValid", R_INT, {R_OBJECT}, &GetIsObjectValid);
    insert(45, "SetCameraFacing", R_VOID, {R_FLOAT}, &SetCameraFacing);
    insert(46, "PlaySound", R_VOID, {R_STRING}, &PlaySound);
    insert(47, "GetSpellTargetObject", R_OBJECT, {}, &GetSpellTargetObject);
    insert(49, "GetCurrentHitPoints", R_INT, {R_OBJECT}, &GetCurrentHitPoints);
    insert(50, "GetMaxHitPoints", R_INT, {R_OBJECT}, &GetMaxHitPoints);
    insert(52, "GetLastItemEquipped", R_OBJECT, {}, &GetLastItemEquipped);
    insert(53, "GetSubScreenID", R_INT, {}, &GetSubScreenID);
    insert(54, "CancelCombat", R_VOID, {R_OBJECT}, &CancelCombat);
    insert(55, "GetCurrentForcePoints", R_INT, {R_OBJECT}, &GetCurrentForcePoints);
    insert(56, "GetMaxForcePoints", R_INT, {R_OBJECT}, &GetMaxForcePoints);
    insert(57, "PauseGame", R_VOID, {R_INT}, &PauseGame);
    insert(58, "SetPlayerRestrictMode", R_VOID, {R_INT}, &SetPlayerRestrictMode);
    insert(59, "GetStringLength", R_INT, {R_STRING}, &GetStringLength);
    insert(60, "GetStringUpperCase", R_STRING, {R_STRING}, &GetStringUpperCase);
    insert(61, "GetStringLowerCase", R_STRING, {R_STRING}, &GetStringLowerCase);
    insert(62, "GetStringRight", R_STRING, {R_STRING, R_INT}, &GetStringRight);
    insert(63, "GetStringLeft", R_STRING, {R_STRING, R_INT}, &GetStringLeft);
    insert(64, "InsertString", R_STRING, {R_STRING, R_STRING, R_INT}, &InsertString);
    insert(65, "GetSubString", R_STRING, {R_STRING, R_INT, R_INT}, &GetSubString);
    insert(66, "FindSubString", R_INT, {R_STRING, R_STRING}, &FindSubString);
    insert(67, "fabs", R_FLOAT, {R_FLOAT}, &fabs);
    insert(68, "cos", R_FLOAT, {R_FLOAT}, &cos);
    insert(69, "sin", R_FLOAT, {R_FLOAT}, &sin);
    insert(70, "tan", R_FLOAT, {R_FLOAT}, &tan);
    insert(71, "acos", R_FLOAT, {R_FLOAT}, &acos);
    insert(72, "asin", R_FLOAT, {R_FLOAT}, &asin);
    insert(73, "atan", R_FLOAT, {R_FLOAT}, &atan);
    insert(74, "log", R_FLOAT, {R_FLOAT}, &log);
    insert(75, "pow", R_FLOAT, {R_FLOAT, R_FLOAT}, &pow);
    insert(76, "sqrt", R_FLOAT, {R_FLOAT}, &sqrt);
    insert(77, "abs", R_INT, {R_INT}, &abs);
    insert(83, "GetPlayerRestrictMode", R_INT, {R_OBJECT}, &GetPlayerRestrictMode);
    insert(84, "GetCasterLevel", R_INT, {R_OBJECT}, &GetCasterLevel);
    insert(85, "GetFirstEffect", R_EFFECT, {R_OBJECT}, &GetFirstEffect);
    insert(86, "GetNextEffect", R_EFFECT, {R_OBJECT}, &GetNextEffect);
    insert(87, "RemoveEffect", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffect);
    insert(88, "GetIsEffectValid", R_INT, {R_EFFECT}, &GetIsEffectValid);
    insert(89, "GetEffectDurationType", R_INT, {R_EFFECT}, &GetEffectDurationType);
    insert(90, "GetEffectSubType", R_INT, {R_EFFECT}, &GetEffectSubType);
    insert(91, "GetEffectCreator", R_OBJECT, {R_EFFECT}, &GetEffectCreator);
    insert(92, "IntToString", R_STRING, {R_INT}, &IntToString);
    insert(93, "GetFirstObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstObjectInArea);
    insert(94, "GetNextObjectInArea", R_OBJECT, {R_OBJECT, R_INT}, &GetNextObjectInArea);
    insert(95, "d2", R_INT, {R_INT}, &d2);
    insert(96, "d3", R_INT, {R_INT}, &d3);
    insert(97, "d4", R_INT, {R_INT}, &d4);
    insert(98, "d6", R_INT, {R_INT}, &d6);
    insert(99, "d8", R_INT, {R_INT}, &d8);
    insert(100, "d10", R_INT, {R_INT}, &d10);
    insert(101, "d12", R_INT, {R_INT}, &d12);
    insert(102, "d20", R_INT, {R_INT}, &d20);
    insert(103, "d100", R_INT, {R_INT}, &d100);
    insert(104, "VectorMagnitude", R_FLOAT, {R_VECTOR}, &VectorMagnitude);
    insert(105, "GetMetaMagicFeat", R_INT, {}, &GetMetaMagicFeat);
    insert(106, "GetObjectType", R_INT, {R_OBJECT}, &GetObjectType);
    insert(107, "GetRacialType", R_INT, {R_OBJECT}, &GetRacialType);
    insert(108, "FortitudeSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &FortitudeSave);
    insert(109, "ReflexSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &ReflexSave);
    insert(110, "WillSave", R_INT, {R_OBJECT, R_INT, R_INT, R_OBJECT}, &WillSave);
    insert(111, "GetSpellSaveDC", R_INT, {}, &GetSpellSaveDC);
    insert(112, "MagicalEffect", R_EFFECT, {R_EFFECT}, &MagicalEffect);
    insert(113, "SupernaturalEffect", R_EFFECT, {R_EFFECT}, &SupernaturalEffect);
    insert(114, "ExtraordinaryEffect", R_EFFECT, {R_EFFECT}, &ExtraordinaryEffect);
    insert(116, "GetAC", R_INT, {R_OBJECT, R_INT}, &GetAC);
    insert(121, "RoundsToSeconds", R_FLOAT, {R_INT}, &RoundsToSeconds);
    insert(122, "HoursToSeconds", R_FLOAT, {R_INT}, &HoursToSeconds);
    insert(123, "TurnsToSeconds", R_FLOAT, {R_INT}, &TurnsToSeconds);
    insert(124, "SoundObjectSetFixedVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetFixedVariance);
    insert(125, "GetGoodEvilValue", R_INT, {R_OBJECT}, &GetGoodEvilValue);
    insert(126, "GetPartyMemberCount", R_INT, {}, &GetPartyMemberCount);
    insert(127, "GetAlignmentGoodEvil", R_INT, {R_OBJECT}, &GetAlignmentGoodEvil);
    insert(128, "GetFirstObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetFirstObjectInShape);
    insert(129, "GetNextObjectInShape", R_OBJECT, {R_INT, R_FLOAT, R_LOCATION, R_INT, R_INT, R_VECTOR}, &GetNextObjectInShape);
    insert(131, "SignalEvent", R_VOID, {R_OBJECT, R_EVENT}, &SignalEvent);
    insert(132, "EventUserDefined", R_EVENT, {R_INT}, &EventUserDefined);
    insert(137, "VectorNormalize", R_VECTOR, {R_VECTOR}, &VectorNormalize);
    insert(138, "GetItemStackSize", R_INT, {R_OBJECT}, &GetItemStackSize);
    insert(139, "GetAbilityScore", R_INT, {R_OBJECT, R_INT}, &GetAbilityScore);
    insert(140, "GetIsDead", R_INT, {R_OBJECT}, &GetIsDead);
    insert(141, "PrintVector", R_VOID, {R_VECTOR, R_INT}, &PrintVector);
    insert(142, "Vector", R_VECTOR, {R_FLOAT, R_FLOAT, R_FLOAT}, &Vector);
    insert(143, "SetFacingPoint", R_VOID, {R_VECTOR}, &SetFacingPoint);
    insert(144, "AngleToVector", R_VECTOR, {R_FLOAT}, &AngleToVector);
    insert(145, "VectorToAngle", R_FLOAT, {R_VECTOR}, &VectorToAngle);
    insert(146, "TouchAttackMelee", R_INT, {R_OBJECT, R_INT}, &TouchAttackMelee);
    insert(147, "TouchAttackRanged", R_INT, {R_OBJECT, R_INT}, &TouchAttackRanged);
    insert(150, "SetItemStackSize", R_VOID, {R_OBJECT, R_INT}, &SetItemStackSize);
    insert(151, "GetDistanceBetween", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween);
    insert(152, "SetReturnStrref", R_VOID, {R_INT, R_INT, R_INT}, &SetReturnStrref);
    insert(155, "GetItemInSlot", R_OBJECT, {R_INT, R_OBJECT}, &GetItemInSlot);
    insert(160, "SetGlobalString", R_VOID, {R_STRING, R_STRING}, &SetGlobalString);
    insert(162, "SetCommandable", R_VOID, {R_INT, R_OBJECT}, &SetCommandable);
    insert(163, "GetCommandable", R_INT, {R_OBJECT}, &GetCommandable);
    insert(166, "GetHitDice", R_INT, {R_OBJECT}, &GetHitDice);
    insert(168, "GetTag", R_STRING, {R_OBJECT}, &GetTag);
    insert(169, "ResistForce", R_INT, {R_OBJECT, R_OBJECT}, &ResistForce);
    insert(170, "GetEffectType", R_INT, {R_EFFECT}, &GetEffectType);
    insert(172, "GetFactionEqual", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionEqual);
    insert(173, "ChangeFaction", R_VOID, {R_OBJECT, R_OBJECT}, &ChangeFaction);
    insert(174, "GetIsListening", R_INT, {R_OBJECT}, &GetIsListening);
    insert(175, "SetListening", R_VOID, {R_OBJECT, R_INT}, &SetListening);
    insert(176, "SetListenPattern", R_VOID, {R_OBJECT, R_STRING, R_INT}, &SetListenPattern);
    insert(177, "TestStringAgainstPattern", R_INT, {R_STRING, R_STRING}, &TestStringAgainstPattern);
    insert(178, "GetMatchedSubstring", R_STRING, {R_INT}, &GetMatchedSubstring);
    insert(179, "GetMatchedSubstringsCount", R_INT, {}, &GetMatchedSubstringsCount);
    insert(181, "GetFactionWeakestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWeakestMember);
    insert(182, "GetFactionStrongestMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionStrongestMember);
    insert(183, "GetFactionMostDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionMostDamagedMember);
    insert(184, "GetFactionLeastDamagedMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionLeastDamagedMember);
    insert(185, "GetFactionGold", R_INT, {R_OBJECT}, &GetFactionGold);
    insert(186, "GetFactionAverageReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetFactionAverageReputation);
    insert(187, "GetFactionAverageGoodEvilAlignment", R_INT, {R_OBJECT}, &GetFactionAverageGoodEvilAlignment);
    insert(188, "SoundObjectGetFixedVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetFixedVariance);
    insert(189, "GetFactionAverageLevel", R_INT, {R_OBJECT}, &GetFactionAverageLevel);
    insert(190, "GetFactionAverageXP", R_INT, {R_OBJECT}, &GetFactionAverageXP);
    insert(191, "GetFactionMostFrequentClass", R_INT, {R_OBJECT}, &GetFactionMostFrequentClass);
    insert(192, "GetFactionWorstAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionWorstAC);
    insert(193, "GetFactionBestAC", R_OBJECT, {R_OBJECT, R_INT}, &GetFactionBestAC);
    insert(194, "GetGlobalString", R_STRING, {R_STRING}, &GetGlobalString);
    insert(195, "GetListenPatternNumber", R_INT, {}, &GetListenPatternNumber);
    insert(197, "GetWaypointByTag", R_OBJECT, {R_STRING}, &GetWaypointByTag);
    insert(198, "GetTransitionTarget", R_OBJECT, {R_OBJECT}, &GetTransitionTarget);
    insert(200, "GetObjectByTag", R_OBJECT, {R_STRING, R_INT}, &GetObjectByTag);
    insert(201, "AdjustAlignment", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &AdjustAlignment);
    insert(203, "SetAreaTransitionBMP", R_VOID, {R_INT, R_STRING}, &SetAreaTransitionBMP);
    insert(208, "GetReputation", R_INT, {R_OBJECT, R_OBJECT}, &GetReputation);
    insert(209, "AdjustReputation", R_VOID, {R_OBJECT, R_OBJECT, R_INT}, &AdjustReputation);
    insert(210, "GetModuleFileName", R_STRING, {}, &GetModuleFileName);
    insert(211, "GetGoingToBeAttackedBy", R_OBJECT, {R_OBJECT}, &GetGoingToBeAttackedBy);
    insert(213, "GetLocation", R_LOCATION, {R_OBJECT}, &GetLocation);
    insert(215, "Location", R_LOCATION, {R_VECTOR, R_FLOAT}, &CreateLocation);
    insert(216, "ApplyEffectAtLocation", R_VOID, {R_INT, R_EFFECT, R_LOCATION, R_FLOAT}, &ApplyEffectAtLocation);
    insert(217, "GetIsPC", R_INT, {R_OBJECT}, &GetIsPC);
    insert(218, "FeetToMeters", R_FLOAT, {R_FLOAT}, &FeetToMeters);
    insert(219, "YardsToMeters", R_FLOAT, {R_FLOAT}, &YardsToMeters);
    insert(220, "ApplyEffectToObject", R_VOID, {R_INT, R_EFFECT, R_OBJECT, R_FLOAT}, &ApplyEffectToObject);
    insert(221, "SpeakString", R_VOID, {R_STRING, R_INT}, &SpeakString);
    insert(222, "GetSpellTargetLocation", R_LOCATION, {}, &GetSpellTargetLocation);
    insert(223, "GetPositionFromLocation", R_VECTOR, {R_LOCATION}, &GetPositionFromLocation);
    insert(225, "GetFacingFromLocation", R_FLOAT, {R_LOCATION}, &GetFacingFromLocation);
    insert(226, "GetNearestCreatureToLocation", R_OBJECT, {R_INT, R_INT, R_LOCATION, R_INT, R_INT, R_INT, R_INT, R_INT}, &GetNearestCreatureToLocation);
    insert(227, "GetNearestObject", R_OBJECT, {R_INT, R_OBJECT, R_INT}, &GetNearestObject);
    insert(228, "GetNearestObjectToLocation", R_OBJECT, {R_INT, R_LOCATION, R_INT}, &GetNearestObjectToLocation);
    insert(229, "GetNearestObjectByTag", R_OBJECT, {R_STRING, R_OBJECT, R_INT}, &GetNearestObjectByTag);
    insert(230, "IntToFloat", R_FLOAT, {R_INT}, &IntToFloat);
    insert(231, "FloatToInt", R_INT, {R_FLOAT}, &FloatToInt);
    insert(232, "StringToInt", R_INT, {R_STRING}, &StringToInt);
    insert(233, "StringToFloat", R_FLOAT, {R_STRING}, &StringToFloat);
    insert(235, "GetIsEnemy", R_INT, {R_OBJECT, R_OBJECT}, &GetIsEnemy);
    insert(236, "GetIsFriend", R_INT, {R_OBJECT, R_OBJECT}, &GetIsFriend);
    insert(237, "GetIsNeutral", R_INT, {R_OBJECT, R_OBJECT}, &GetIsNeutral);
    insert(238, "GetPCSpeaker", R_OBJECT, {}, &GetPCSpeaker);
    insert(239, "GetStringByStrRef", R_STRING, {R_INT}, &GetStringByStrRef);
    insert(241, "DestroyObject", R_VOID, {R_OBJECT, R_FLOAT, R_INT, R_FLOAT, R_INT}, &DestroyObject);
    insert(242, "GetModule", R_OBJECT, {}, &GetModule);
    insert(243, "CreateObject", R_OBJECT, {R_INT, R_STRING, R_LOCATION, R_INT}, &CreateObject);
    insert(244, "EventSpellCastAt", R_EVENT, {R_OBJECT, R_INT, R_INT}, &EventSpellCastAt);
    insert(245, "GetLastSpellCaster", R_OBJECT, {}, &GetLastSpellCaster);
    insert(246, "GetLastSpell", R_INT, {}, &GetLastSpell);
    insert(247, "GetUserDefinedEventNumber", R_INT, {}, &GetUserDefinedEventNumber);
    insert(248, "GetSpellId", R_INT, {}, &GetSpellId);
    insert(249, "RandomName", R_STRING, {}, &RandomName);
    insert(251, "GetLoadFromSaveGame", R_INT, {}, &GetLoadFromSaveGame);
    insert(253, "GetName", R_STRING, {R_OBJECT}, &GetName);
    insert(254, "GetLastSpeaker", R_OBJECT, {}, &GetLastSpeaker);
    insert(255, "BeginConversation", R_INT, {R_STRING, R_OBJECT}, &BeginConversation);
    insert(256, "GetLastPerceived", R_OBJECT, {}, &GetLastPerceived);
    insert(257, "GetLastPerceptionHeard", R_INT, {}, &GetLastPerceptionHeard);
    insert(258, "GetLastPerceptionInaudible", R_INT, {}, &GetLastPerceptionInaudible);
    insert(259, "GetLastPerceptionSeen", R_INT, {}, &GetLastPerceptionSeen);
    insert(260, "GetLastClosedBy", R_OBJECT, {}, &GetLastClosedBy);
    insert(261, "GetLastPerceptionVanished", R_INT, {}, &GetLastPerceptionVanished);
    insert(262, "GetFirstInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetFirstInPersistentObject);
    insert(263, "GetNextInPersistentObject", R_OBJECT, {R_OBJECT, R_INT, R_INT}, &GetNextInPersistentObject);
    insert(264, "GetAreaOfEffectCreator", R_OBJECT, {R_OBJECT}, &GetAreaOfEffectCreator);
    insert(265, "ShowLevelUpGUI", R_INT, {}, &ShowLevelUpGUI);
    insert(266, "SetItemNonEquippable", R_VOID, {R_OBJECT, R_INT}, &SetItemNonEquippable);
    insert(267, "GetButtonMashCheck", R_INT, {}, &GetButtonMashCheck);
    insert(268, "SetButtonMashCheck", R_VOID, {R_INT}, &SetButtonMashCheck);
    insert(271, "GiveItem", R_VOID, {R_OBJECT, R_OBJECT}, &GiveItem);
    insert(272, "ObjectToString", R_STRING, {R_OBJECT}, &ObjectToString);
    insert(274, "GetIsImmune", R_INT, {R_OBJECT, R_INT, R_OBJECT}, &GetIsImmune);
    insert(276, "GetEncounterActive", R_INT, {R_OBJECT}, &GetEncounterActive);
    insert(277, "SetEncounterActive", R_VOID, {R_INT, R_OBJECT}, &SetEncounterActive);
    insert(278, "GetEncounterSpawnsMax", R_INT, {R_OBJECT}, &GetEncounterSpawnsMax);
    insert(279, "SetEncounterSpawnsMax", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsMax);
    insert(280, "GetEncounterSpawnsCurrent", R_INT, {R_OBJECT}, &GetEncounterSpawnsCurrent);
    insert(281, "SetEncounterSpawnsCurrent", R_VOID, {R_INT, R_OBJECT}, &SetEncounterSpawnsCurrent);
    insert(282, "GetModuleItemAcquired", R_OBJECT, {}, &GetModuleItemAcquired);
    insert(283, "GetModuleItemAcquiredFrom", R_OBJECT, {}, &GetModuleItemAcquiredFrom);
    insert(284, "SetCustomToken", R_VOID, {R_INT, R_STRING}, &SetCustomToken);
    insert(285, "GetHasFeat", R_INT, {R_INT, R_OBJECT}, &GetHasFeat);
    insert(286, "GetHasSkill", R_INT, {R_INT, R_OBJECT}, &GetHasSkill);
    insert(289, "GetObjectSeen", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectSeen);
    insert(290, "GetObjectHeard", R_INT, {R_OBJECT, R_OBJECT}, &GetObjectHeard);
    insert(291, "GetLastPlayerDied", R_OBJECT, {}, &GetLastPlayerDied);
    insert(292, "GetModuleItemLost", R_OBJECT, {}, &GetModuleItemLost);
    insert(293, "GetModuleItemLostBy", R_OBJECT, {}, &GetModuleItemLostBy);
    insert(295, "EventConversation", R_EVENT, {}, &EventConversation);
    insert(296, "SetEncounterDifficulty", R_VOID, {R_INT, R_OBJECT}, &SetEncounterDifficulty);
    insert(297, "GetEncounterDifficulty", R_INT, {R_OBJECT}, &GetEncounterDifficulty);
    insert(298, "GetDistanceBetweenLocations", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations);
    insert(299, "GetReflexAdjustedDamage", R_INT, {R_INT, R_OBJECT, R_INT, R_INT, R_OBJECT}, &GetReflexAdjustedDamage);
    insert(300, "PlayAnimation", R_VOID, {R_INT, R_FLOAT, R_FLOAT}, &PlayAnimation);
    insert(301, "TalentSpell", R_TALENT, {R_INT}, &TalentSpell);
    insert(302, "TalentFeat", R_TALENT, {R_INT}, &TalentFeat);
    insert(303, "TalentSkill", R_TALENT, {R_INT}, &TalentSkill);
    insert(304, "GetHasSpellEffect", R_INT, {R_INT, R_OBJECT}, &GetHasSpellEffect);
    insert(305, "GetEffectSpellId", R_INT, {R_EFFECT}, &GetEffectSpellId);
    insert(306, "GetCreatureHasTalent", R_INT, {R_TALENT, R_OBJECT}, &GetCreatureHasTalent);
    insert(307, "GetCreatureTalentRandom", R_TALENT, {R_INT, R_OBJECT, R_INT}, &GetCreatureTalentRandom);
    insert(308, "GetCreatureTalentBest", R_TALENT, {R_INT, R_INT, R_OBJECT, R_INT, R_INT, R_INT}, &GetCreatureTalentBest);
    insert(311, "GetGoldPieceValue", R_INT, {R_OBJECT}, &GetGoldPieceValue);
    insert(312, "GetIsPlayableRacialType", R_INT, {R_OBJECT}, &GetIsPlayableRacialType);
    insert(313, "JumpToLocation", R_VOID, {R_LOCATION}, &JumpToLocation);
    insert(315, "GetSkillRank", R_INT, {R_INT, R_OBJECT}, &GetSkillRank);
    insert(316, "GetAttackTarget", R_OBJECT, {R_OBJECT}, &GetAttackTarget);
    insert(317, "GetLastAttackType", R_INT, {R_OBJECT}, &GetLastAttackType);
    insert(318, "GetLastAttackMode", R_INT, {R_OBJECT}, &GetLastAttackMode);
    insert(319, "GetDistanceBetween2D", R_FLOAT, {R_OBJECT, R_OBJECT}, &GetDistanceBetween2D);
    insert(320, "GetIsInCombat", R_INT, {R_OBJECT, R_INT}, &GetIsInCombat);
    insert(321, "GetLastAssociateCommand", R_INT, {R_OBJECT}, &GetLastAssociateCommand);
    insert(322, "GiveGoldToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveGoldToCreature);
    insert(323, "SetIsDestroyable", R_VOID, {R_INT, R_INT, R_INT}, &SetIsDestroyable);
    insert(324, "SetLocked", R_VOID, {R_OBJECT, R_INT}, &SetLocked);
    insert(325, "GetLocked", R_INT, {R_OBJECT}, &GetLocked);
    insert(326, "GetClickingObject", R_OBJECT, {}, &GetClickingObject);
    insert(327, "SetAssociateListenPatterns", R_VOID, {R_OBJECT}, &SetAssociateListenPatterns);
    insert(328, "GetLastWeaponUsed", R_OBJECT, {R_OBJECT}, &GetLastWeaponUsed);
    insert(330, "GetLastUsedBy", R_OBJECT, {}, &GetLastUsedBy);
    insert(331, "GetAbilityModifier", R_INT, {R_INT, R_OBJECT}, &GetAbilityModifier);
    insert(332, "GetIdentified", R_INT, {R_OBJECT}, &GetIdentified);
    insert(333, "SetIdentified", R_VOID, {R_OBJECT, R_INT}, &SetIdentified);
    insert(334, "GetDistanceBetweenLocations2D", R_FLOAT, {R_LOCATION, R_LOCATION}, &GetDistanceBetweenLocations2D);
    insert(335, "GetDistanceToObject2D", R_FLOAT, {R_OBJECT}, &GetDistanceToObject2D);
    insert(336, "GetBlockingDoor", R_OBJECT, {}, &GetBlockingDoor);
    insert(337, "GetIsDoorActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsDoorActionPossible);
    insert(338, "DoDoorAction", R_VOID, {R_OBJECT, R_INT}, &DoDoorAction);
    insert(339, "GetFirstItemInInventory", R_OBJECT, {R_OBJECT}, &GetFirstItemInInventory);
    insert(340, "GetNextItemInInventory", R_OBJECT, {R_OBJECT}, &GetNextItemInInventory);
    insert(341, "GetClassByPosition", R_INT, {R_INT, R_OBJECT}, &GetClassByPosition);
    insert(342, "GetLevelByPosition", R_INT, {R_INT, R_OBJECT}, &GetLevelByPosition);
    insert(343, "GetLevelByClass", R_INT, {R_INT, R_OBJECT}, &GetLevelByClass);
    insert(344, "GetDamageDealtByType", R_INT, {R_INT}, &GetDamageDealtByType);
    insert(345, "GetTotalDamageDealt", R_INT, {}, &GetTotalDamageDealt);
    insert(346, "GetLastDamager", R_OBJECT, {}, &GetLastDamager);
    insert(347, "GetLastDisarmed", R_OBJECT, {}, &GetLastDisarmed);
    insert(348, "GetLastDisturbed", R_OBJECT, {}, &GetLastDisturbed);
    insert(349, "GetLastLocked", R_OBJECT, {}, &GetLastLocked);
    insert(350, "GetLastUnlocked", R_OBJECT, {}, &GetLastUnlocked);
    insert(352, "GetInventoryDisturbType", R_INT, {}, &GetInventoryDisturbType);
    insert(353, "GetInventoryDisturbItem", R_OBJECT, {}, &GetInventoryDisturbItem);
    insert(354, "ShowUpgradeScreen", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT, R_STRING}, &ShowUpgradeScreen);
    insert(355, "VersusAlignmentEffect", R_EFFECT, {R_EFFECT, R_INT, R_INT}, &VersusAlignmentEffect);
    insert(356, "VersusRacialTypeEffect", R_EFFECT, {R_EFFECT, R_INT}, &VersusRacialTypeEffect);
    insert(357, "VersusTrapEffect", R_EFFECT, {R_EFFECT}, &VersusTrapEffect);
    insert(358, "GetGender", R_INT, {R_OBJECT}, &GetGender);
    insert(359, "GetIsTalentValid", R_INT, {R_TALENT}, &GetIsTalentValid);
    insert(361, "GetAttemptedAttackTarget", R_OBJECT, {}, &GetAttemptedAttackTarget);
    insert(362, "GetTypeFromTalent", R_INT, {R_TALENT}, &GetTypeFromTalent);
    insert(363, "GetIdFromTalent", R_INT, {R_TALENT}, &GetIdFromTalent);
    insert(364, "PlayPazaak", R_VOID, {R_INT, R_STRING, R_INT, R_INT, R_OBJECT}, &PlayPazaak);
    insert(365, "GetLastPazaakResult", R_INT, {}, &GetLastPazaakResult);
    insert(366, "DisplayFeedBackText", R_VOID, {R_OBJECT, R_INT}, &DisplayFeedBackText);
    insert(367, "AddJournalQuestEntry", R_VOID, {R_STRING, R_INT, R_INT}, &AddJournalQuestEntry);
    insert(368, "RemoveJournalQuestEntry", R_VOID, {R_STRING}, &RemoveJournalQuestEntry);
    insert(369, "GetJournalEntry", R_INT, {R_STRING}, &GetJournalEntry);
    insert(370, "PlayRumblePattern", R_INT, {R_INT}, &PlayRumblePattern);
    insert(371, "StopRumblePattern", R_INT, {R_INT}, &StopRumblePattern);
    insert(374, "SendMessageToPC", R_VOID, {R_OBJECT, R_STRING}, &SendMessageToPC);
    insert(375, "GetAttemptedSpellTarget", R_OBJECT, {}, &GetAttemptedSpellTarget);
    insert(376, "GetLastOpenedBy", R_OBJECT, {}, &GetLastOpenedBy);
    insert(377, "GetHasSpell", R_INT, {R_INT, R_OBJECT}, &GetHasSpell);
    insert(378, "OpenStore", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &OpenStore);
    insert(380, "GetFirstFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetFirstFactionMember);
    insert(381, "GetNextFactionMember", R_OBJECT, {R_OBJECT, R_INT}, &GetNextFactionMember);
    insert(384, "GetJournalQuestExperience", R_INT, {R_STRING}, &GetJournalQuestExperience);
    insert(385, "JumpToObject", R_VOID, {R_OBJECT, R_INT}, &JumpToObject);
    insert(386, "SetMapPinEnabled", R_VOID, {R_OBJECT, R_INT}, &SetMapPinEnabled);
    insert(388, "PopUpGUIPanel", R_VOID, {R_OBJECT, R_INT}, &PopUpGUIPanel);
    insert(389, "AddMultiClass", R_VOID, {R_INT, R_OBJECT}, &AddMultiClass);
    insert(390, "GetIsLinkImmune", R_INT, {R_OBJECT, R_EFFECT}, &GetIsLinkImmune);
    insert(393, "GiveXPToCreature", R_VOID, {R_OBJECT, R_INT}, &GiveXPToCreature);
    insert(394, "SetXP", R_VOID, {R_OBJECT, R_INT}, &SetXP);
    insert(395, "GetXP", R_INT, {R_OBJECT}, &GetXP);
    insert(396, "IntToHexString", R_STRING, {R_INT}, &IntToHexString);
    insert(397, "GetBaseItemType", R_INT, {R_OBJECT}, &GetBaseItemType);
    insert(398, "GetItemHasItemProperty", R_INT, {R_OBJECT, R_INT}, &GetItemHasItemProperty);
    insert(401, "GetItemACValue", R_INT, {R_OBJECT}, &GetItemACValue);
    insert(403, "ExploreAreaForPlayer", R_VOID, {R_OBJECT, R_OBJECT}, &ExploreAreaForPlayer);
    insert(405, "GetIsDay", R_INT, {}, &GetIsDay);
    insert(406, "GetIsNight", R_INT, {}, &GetIsNight);
    insert(407, "GetIsDawn", R_INT, {}, &GetIsDawn);
    insert(408, "GetIsDusk", R_INT, {}, &GetIsDusk);
    insert(409, "GetIsEncounterCreature", R_INT, {R_OBJECT}, &GetIsEncounterCreature);
    insert(410, "GetLastPlayerDying", R_OBJECT, {}, &GetLastPlayerDying);
    insert(411, "GetStartingLocation", R_LOCATION, {}, &GetStartingLocation);
    insert(412, "ChangeToStandardFaction", R_VOID, {R_OBJECT, R_INT}, &ChangeToStandardFaction);
    insert(413, "SoundObjectPlay", R_VOID, {R_OBJECT}, &SoundObjectPlay);
    insert(414, "SoundObjectStop", R_VOID, {R_OBJECT}, &SoundObjectStop);
    insert(415, "SoundObjectSetVolume", R_VOID, {R_OBJECT, R_INT}, &SoundObjectSetVolume);
    insert(416, "SoundObjectSetPosition", R_VOID, {R_OBJECT, R_VECTOR}, &SoundObjectSetPosition);
    insert(417, "SpeakOneLinerConversation", R_VOID, {R_STRING, R_OBJECT}, &SpeakOneLinerConversation);
    insert(418, "GetGold", R_INT, {R_OBJECT}, &GetGold);
    insert(419, "GetLastRespawnButtonPresser", R_OBJECT, {}, &GetLastRespawnButtonPresser);
    insert(421, "SetLightsaberPowered", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &SetLightsaberPowered);
    insert(422, "GetIsWeaponEffective", R_INT, {R_OBJECT, R_INT}, &GetIsWeaponEffective);
    insert(423, "GetLastSpellHarmful", R_INT, {}, &GetLastSpellHarmful);
    insert(424, "EventActivateItem", R_EVENT, {R_OBJECT, R_LOCATION, R_OBJECT}, &EventActivateItem);
    insert(425, "MusicBackgroundPlay", R_VOID, {R_OBJECT}, &MusicBackgroundPlay);
    insert(426, "MusicBackgroundStop", R_VOID, {R_OBJECT}, &MusicBackgroundStop);
    insert(427, "MusicBackgroundSetDelay", R_VOID, {R_OBJECT, R_INT}, &MusicBackgroundSetDelay);
    insert(428, "MusicBackgroundChangeDay", R_VOID, {R_OBJECT, R_INT, R_INT}, &MusicBackgroundChangeDay);
    insert(429, "MusicBackgroundChangeNight", R_VOID, {R_OBJECT, R_INT, R_INT}, &MusicBackgroundChangeNight);
    insert(430, "MusicBattlePlay", R_VOID, {R_OBJECT}, &MusicBattlePlay);
    insert(431, "MusicBattleStop", R_VOID, {R_OBJECT}, &MusicBattleStop);
    insert(432, "MusicBattleChange", R_VOID, {R_OBJECT, R_INT}, &MusicBattleChange);
    insert(433, "AmbientSoundPlay", R_VOID, {R_OBJECT}, &AmbientSoundPlay);
    insert(434, "AmbientSoundStop", R_VOID, {R_OBJECT}, &AmbientSoundStop);
    insert(435, "AmbientSoundChangeDay", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeDay);
    insert(436, "AmbientSoundChangeNight", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundChangeNight);
    insert(437, "GetLastKiller", R_OBJECT, {}, &GetLastKiller);
    insert(438, "GetSpellCastItem", R_OBJECT, {}, &GetSpellCastItem);
    insert(439, "GetItemActivated", R_OBJECT, {}, &GetItemActivated);
    insert(440, "GetItemActivator", R_OBJECT, {}, &GetItemActivator);
    insert(441, "GetItemActivatedTargetLocation", R_LOCATION, {}, &GetItemActivatedTargetLocation);
    insert(442, "GetItemActivatedTarget", R_OBJECT, {}, &GetItemActivatedTarget);
    insert(443, "GetIsOpen", R_INT, {R_OBJECT}, &GetIsOpen);
    insert(444, "TakeGoldFromCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &TakeGoldFromCreature);
    insert(445, "GetIsInConversation", R_INT, {R_OBJECT}, &GetIsInConversation);
    insert(455, "GetPlotFlag", R_INT, {R_OBJECT}, &GetPlotFlag);
    insert(456, "SetPlotFlag", R_VOID, {R_OBJECT, R_INT}, &SetPlotFlag);
    insert(461, "SetDialogPlaceableCamera", R_VOID, {R_INT}, &SetDialogPlaceableCamera);
    insert(462, "GetSoloMode", R_INT, {}, &GetSoloMode);
    insert(464, "GetMaxStealthXP", R_INT, {}, &GetMaxStealthXP);
    insert(468, "SetMaxStealthXP", R_VOID, {R_INT}, &SetMaxStealthXP);
    insert(474, "GetCurrentStealthXP", R_INT, {}, &GetCurrentStealthXP);
    insert(475, "GetNumStackedItems", R_INT, {R_OBJECT}, &GetNumStackedItems);
    insert(476, "SurrenderToEnemies", R_VOID, {}, &SurrenderToEnemies);
    insert(478, "SetCurrentStealthXP", R_VOID, {R_INT}, &SetCurrentStealthXP);
    insert(479, "GetCreatureSize", R_INT, {R_OBJECT}, &GetCreatureSize);
    insert(480, "AwardStealthXP", R_VOID, {R_OBJECT}, &AwardStealthXP);
    insert(481, "GetStealthXPEnabled", R_INT, {}, &GetStealthXPEnabled);
    insert(482, "SetStealthXPEnabled", R_VOID, {R_INT}, &SetStealthXPEnabled);
    insert(486, "GetLastTrapDetected", R_OBJECT, {R_OBJECT}, &GetLastTrapDetected);
    insert(488, "GetNearestTrapToObject", R_OBJECT, {R_OBJECT, R_INT}, &GetNearestTrapToObject);
    insert(489, "GetAttemptedMovementTarget", R_OBJECT, {}, &GetAttemptedMovementTarget);
    insert(490, "GetBlockingCreature", R_OBJECT, {R_OBJECT}, &GetBlockingCreature);
    insert(491, "GetFortitudeSavingThrow", R_INT, {R_OBJECT}, &GetFortitudeSavingThrow);
    insert(492, "GetWillSavingThrow", R_INT, {R_OBJECT}, &GetWillSavingThrow);
    insert(493, "GetReflexSavingThrow", R_INT, {R_OBJECT}, &GetReflexSavingThrow);
    insert(494, "GetChallengeRating", R_FLOAT, {R_OBJECT}, &GetChallengeRating);
    insert(495, "GetFoundEnemyCreature", R_OBJECT, {R_OBJECT}, &GetFoundEnemyCreature);
    insert(496, "GetMovementRate", R_INT, {R_OBJECT}, &GetMovementRate);
    insert(497, "GetSubRace", R_INT, {R_OBJECT}, &GetSubRace);
    insert(498, "GetStealthXPDecrement", R_INT, {}, &GetStealthXPDecrement);
    insert(499, "SetStealthXPDecrement", R_VOID, {R_INT}, &SetStealthXPDecrement);
    insert(500, "DuplicateHeadAppearance", R_VOID, {R_OBJECT, R_OBJECT}, &DuplicateHeadAppearance);
    insert(503, "CutsceneAttack", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &CutsceneAttack);
    insert(504, "SetCameraMode", R_VOID, {R_OBJECT, R_INT}, &SetCameraMode);
    insert(505, "SetLockOrientationInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockOrientationInDialog);
    insert(506, "SetLockHeadFollowInDialog", R_VOID, {R_OBJECT, R_INT}, &SetLockHeadFollowInDialog);
    insert(507, "CutsceneMove", R_VOID, {R_OBJECT, R_VECTOR, R_INT}, &CutsceneMove);
    insert(508, "EnableVideoEffect", R_VOID, {R_INT}, &EnableVideoEffect);
    insert(509, "StartNewModule", R_VOID, {R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING, R_STRING}, &StartNewModule);
    insert(510, "DisableVideoEffect", R_VOID, {}, &DisableVideoEffect);
    insert(511, "GetWeaponRanged", R_INT, {R_OBJECT}, &GetWeaponRanged);
    insert(512, "DoSinglePlayerAutoSave", R_VOID, {}, &DoSinglePlayerAutoSave);
    insert(513, "GetGameDifficulty", R_INT, {}, &GetGameDifficulty);
    insert(514, "GetUserActionsPending", R_INT, {}, &GetUserActionsPending);
    insert(515, "RevealMap", R_VOID, {R_VECTOR, R_INT}, &RevealMap);
    insert(516, "SetTutorialWindowsEnabled", R_VOID, {R_INT}, &SetTutorialWindowsEnabled);
    insert(517, "ShowTutorialWindow", R_VOID, {R_INT}, &ShowTutorialWindow);
    insert(518, "StartCreditSequence", R_VOID, {R_INT}, &StartCreditSequence);
    insert(519, "IsCreditSequenceInProgress", R_INT, {}, &IsCreditSequenceInProgress);
    insert(522, "GetCurrentAction", R_INT, {R_OBJECT}, &GetCurrentAction);
    insert(523, "GetDifficultyModifier", R_FLOAT, {}, &GetDifficultyModifier);
    insert(524, "GetAppearanceType", R_INT, {R_OBJECT}, &GetAppearanceType);
    insert(525, "FloatingTextStrRefOnCreature", R_VOID, {R_INT, R_OBJECT, R_INT}, &FloatingTextStrRefOnCreature);
    insert(526, "FloatingTextStringOnCreature", R_VOID, {R_STRING, R_OBJECT, R_INT}, &FloatingTextStringOnCreature);
    insert(527, "GetTrapDisarmable", R_INT, {R_OBJECT}, &GetTrapDisarmable);
    insert(528, "GetTrapDetectable", R_INT, {R_OBJECT}, &GetTrapDetectable);
    insert(529, "GetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &GetTrapDetectedBy);
    insert(530, "GetTrapFlagged", R_INT, {R_OBJECT}, &GetTrapFlagged);
    insert(531, "GetTrapBaseType", R_INT, {R_OBJECT}, &GetTrapBaseType);
    insert(532, "GetTrapOneShot", R_INT, {R_OBJECT}, &GetTrapOneShot);
    insert(533, "GetTrapCreator", R_OBJECT, {R_OBJECT}, &GetTrapCreator);
    insert(534, "GetTrapKeyTag", R_STRING, {R_OBJECT}, &GetTrapKeyTag);
    insert(535, "GetTrapDisarmDC", R_INT, {R_OBJECT}, &GetTrapDisarmDC);
    insert(536, "GetTrapDetectDC", R_INT, {R_OBJECT}, &GetTrapDetectDC);
    insert(537, "GetLockKeyRequired", R_INT, {R_OBJECT}, &GetLockKeyRequired);
    insert(538, "GetLockKeyTag", R_STRING, {R_OBJECT}, &GetLockKeyTag);
    insert(539, "GetLockLockable", R_INT, {R_OBJECT}, &GetLockLockable);
    insert(540, "GetLockUnlockDC", R_INT, {R_OBJECT}, &GetLockUnlockDC);
    insert(541, "GetLockLockDC", R_INT, {R_OBJECT}, &GetLockLockDC);
    insert(542, "GetPCLevellingUp", R_OBJECT, {}, &GetPCLevellingUp);
    insert(543, "GetHasFeatEffect", R_INT, {R_INT, R_OBJECT}, &GetHasFeatEffect);
    insert(544, "SetPlaceableIllumination", R_VOID, {R_OBJECT, R_INT}, &SetPlaceableIllumination);
    insert(545, "GetPlaceableIllumination", R_INT, {R_OBJECT}, &GetPlaceableIllumination);
    insert(546, "GetIsPlaceableObjectActionPossible", R_INT, {R_OBJECT, R_INT}, &GetIsPlaceableObjectActionPossible);
    insert(547, "DoPlaceableObjectAction", R_VOID, {R_OBJECT, R_INT}, &DoPlaceableObjectAction);
    insert(548, "GetFirstPC", R_OBJECT, {}, &GetFirstPC);
    insert(549, "GetNextPC", R_OBJECT, {}, &GetNextPC);
    insert(550, "SetTrapDetectedBy", R_INT, {R_OBJECT, R_OBJECT}, &SetTrapDetectedBy);
    insert(551, "GetIsTrapped", R_INT, {R_OBJECT}, &GetIsTrapped);
    insert(552, "SetEffectIcon", R_EFFECT, {R_EFFECT, R_INT}, &SetEffectIcon);
    insert(553, "FaceObjectAwayFromObject", R_VOID, {R_OBJECT, R_OBJECT}, &FaceObjectAwayFromObject);
    insert(554, "PopUpDeathGUIPanel", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT, R_STRING}, &PopUpDeathGUIPanel);
    insert(555, "SetTrapDisabled", R_VOID, {R_OBJECT}, &SetTrapDisabled);
    insert(556, "GetLastHostileActor", R_OBJECT, {R_OBJECT}, &GetLastHostileActor);
    insert(557, "ExportAllCharacters", R_VOID, {}, &ExportAllCharacters);
    insert(558, "MusicBackgroundGetDayTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetDayTrack);
    insert(559, "MusicBackgroundGetNightTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetNightTrack);
    insert(560, "WriteTimestampedLogEntry", R_VOID, {R_STRING}, &WriteTimestampedLogEntry);
    insert(561, "GetModuleName", R_STRING, {}, &GetModuleName);
    insert(562, "GetFactionLeader", R_OBJECT, {R_OBJECT}, &GetFactionLeader);
    insert(564, "EndGame", R_VOID, {R_INT}, &EndGame);
    insert(565, "GetRunScriptVar", R_INT, {}, &GetRunScriptVar);
    insert(566, "GetCreatureMovmentType", R_INT, {R_OBJECT}, &GetCreatureMovmentType);
    insert(567, "AmbientSoundSetDayVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetDayVolume);
    insert(568, "AmbientSoundSetNightVolume", R_VOID, {R_OBJECT, R_INT}, &AmbientSoundSetNightVolume);
    insert(569, "MusicBackgroundGetBattleTrack", R_INT, {R_OBJECT}, &MusicBackgroundGetBattleTrack);
    insert(570, "GetHasInventory", R_INT, {R_OBJECT}, &GetHasInventory);
    insert(571, "GetStrRefSoundDuration", R_FLOAT, {R_INT}, &GetStrRefSoundDuration);
    insert(572, "AddToParty", R_VOID, {R_OBJECT, R_OBJECT}, &AddToParty);
    insert(573, "RemoveFromParty", R_VOID, {R_OBJECT}, &RemoveFromParty);
    insert(574, "AddPartyMember", R_INT, {R_INT, R_OBJECT}, &AddPartyMember);
    insert(575, "RemovePartyMember", R_INT, {R_INT}, &RemovePartyMember);
    insert(576, "IsObjectPartyMember", R_INT, {R_OBJECT}, &IsObjectPartyMember);
    insert(577, "GetPartyMemberByIndex", R_OBJECT, {R_INT}, &GetPartyMemberByIndex);
    insert(578, "GetGlobalBoolean", R_INT, {R_STRING}, &GetGlobalBoolean);
    insert(579, "SetGlobalBoolean", R_VOID, {R_STRING, R_INT}, &SetGlobalBoolean);
    insert(580, "GetGlobalNumber", R_INT, {R_STRING}, &GetGlobalNumber);
    insert(581, "SetGlobalNumber", R_VOID, {R_STRING, R_INT}, &SetGlobalNumber);
    insert(582, "AurPostString", R_VOID, {R_STRING, R_INT, R_INT, R_FLOAT}, &AurPostString);
    insert(669, "AddJournalWorldEntry", R_VOID, {R_INT, R_STRING, R_STRING}, &AddJournalWorldEntry);
    insert(670, "AddJournalWorldEntryStrref", R_VOID, {R_INT, R_INT}, &AddJournalWorldEntryStrref);
    insert(671, "BarkString", R_VOID, {R_OBJECT, R_INT, R_INT, R_INT}, &BarkString);
    insert(672, "DeleteJournalWorldAllEntries", R_VOID, {}, &DeleteJournalWorldAllEntries);
    insert(673, "DeleteJournalWorldEntry", R_VOID, {R_INT}, &DeleteJournalWorldEntry);
    insert(674, "DeleteJournalWorldEntryStrref", R_VOID, {R_INT}, &DeleteJournalWorldEntryStrref);
    insert(677, "PlayVisualAreaEffect", R_VOID, {R_INT, R_LOCATION}, &PlayVisualAreaEffect);
    insert(678, "SetJournalQuestEntryPicture", R_VOID, {R_STRING, R_OBJECT, R_INT, R_INT, R_INT}, &SetJournalQuestEntryPicture);
    insert(679, "GetLocalBoolean", R_INT, {R_OBJECT, R_INT}, &GetLocalBoolean);
    insert(680, "SetLocalBoolean", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalBoolean);
    insert(681, "GetLocalNumber", R_INT, {R_OBJECT, R_INT}, &GetLocalNumber);
    insert(682, "SetLocalNumber", R_VOID, {R_OBJECT, R_INT, R_INT}, &SetLocalNumber);
    insert(689, "SoundObjectGetPitchVariance", R_FLOAT, {R_OBJECT}, &SoundObjectGetPitchVariance);
    insert(690, "SoundObjectSetPitchVariance", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectSetPitchVariance);
    insert(691, "SoundObjectGetVolume", R_INT, {R_OBJECT}, &SoundObjectGetVolume);
    insert(692, "GetGlobalLocation", R_LOCATION, {R_STRING}, &GetGlobalLocation);
    insert(693, "SetGlobalLocation", R_VOID, {R_STRING, R_LOCATION}, &SetGlobalLocation);
    insert(694, "AddAvailableNPCByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailableNPCByObject);
    insert(695, "RemoveAvailableNPC", R_INT, {R_INT}, &RemoveAvailableNPC);
    insert(696, "IsAvailableCreature", R_INT, {R_INT}, &IsAvailableCreature);
    insert(697, "AddAvailableNPCByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailableNPCByTemplate);
    insert(698, "SpawnAvailableNPC", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailableNPC);
    insert(699, "IsNPCPartyMember", R_INT, {R_INT}, &IsNPCPartyMember);
    insert(701, "GetIsConversationActive", R_INT, {}, &GetIsConversationActive);
    insert(704, "GetPartyAIStyle", R_INT, {}, &GetPartyAIStyle);
    insert(705, "GetNPCAIStyle", R_INT, {R_OBJECT}, &GetNPCAIStyle);
    insert(706, "SetPartyAIStyle", R_VOID, {R_INT}, &SetPartyAIStyle);
    insert(707, "SetNPCAIStyle", R_VOID, {R_OBJECT, R_INT}, &SetNPCAIStyle);
    insert(708, "SetNPCSelectability", R_VOID, {R_INT, R_INT}, &SetNPCSelectability);
    insert(709, "GetNPCSelectability", R_INT, {R_INT}, &GetNPCSelectability);
    insert(710, "ClearAllEffects", R_VOID, {}, &ClearAllEffects);
    insert(711, "GetLastConversation", R_STRING, {}, &GetLastConversation);
    insert(712, "ShowPartySelectionGUI", R_VOID, {R_STRING, R_INT, R_INT, R_INT}, &ShowPartySelectionGUI);
    insert(713, "GetStandardFaction", R_INT, {R_OBJECT}, &GetStandardFaction);
    insert(714, "GivePlotXP", R_VOID, {R_STRING, R_INT}, &GivePlotXP);
    insert(715, "GetMinOneHP", R_INT, {R_OBJECT}, &GetMinOneHP);
    insert(716, "SetMinOneHP", R_VOID, {R_OBJECT, R_INT}, &SetMinOneHP);
    insert(719, "SetGlobalFadeIn", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeIn);
    insert(720, "SetGlobalFadeOut", R_VOID, {R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetGlobalFadeOut);
    insert(721, "GetLastHostileTarget", R_OBJECT, {R_OBJECT}, &GetLastHostileTarget);
    insert(722, "GetLastAttackAction", R_INT, {R_OBJECT}, &GetLastAttackAction);
    insert(723, "GetLastForcePowerUsed", R_INT, {R_OBJECT}, &GetLastForcePowerUsed);
    insert(724, "GetLastCombatFeatUsed", R_INT, {R_OBJECT}, &GetLastCombatFeatUsed);
    insert(725, "GetLastAttackResult", R_INT, {R_OBJECT}, &GetLastAttackResult);
    insert(726, "GetWasForcePowerSuccessful", R_INT, {R_OBJECT}, &GetWasForcePowerSuccessful);
    insert(727, "GetFirstAttacker", R_OBJECT, {R_OBJECT}, &GetFirstAttacker);
    insert(728, "GetNextAttacker", R_OBJECT, {R_OBJECT}, &GetNextAttacker);
    insert(729, "SetFormation", R_VOID, {R_OBJECT, R_OBJECT, R_INT, R_INT}, &SetFormation);
    insert(731, "SetForcePowerUnsuccessful", R_VOID, {R_INT, R_OBJECT}, &SetForcePowerUnsuccessful);
    insert(732, "GetIsDebilitated", R_INT, {R_OBJECT}, &GetIsDebilitated);
    insert(733, "PlayMovie", R_VOID, {R_STRING, R_INT}, &PlayMovie);
    insert(734, "SaveNPCState", R_VOID, {R_INT}, &SaveNPCState);
    insert(735, "GetCategoryFromTalent", R_INT, {R_TALENT}, &GetCategoryFromTalent);
    insert(736, "SurrenderByFaction", R_VOID, {R_INT, R_INT}, &SurrenderByFaction);
    insert(737, "ChangeFactionByFaction", R_VOID, {R_INT, R_INT}, &ChangeFactionByFaction);
    insert(738, "PlayRoomAnimation", R_VOID, {R_STRING, R_INT}, &PlayRoomAnimation);
    insert(739, "ShowGalaxyMap", R_VOID, {R_INT}, &ShowGalaxyMap);
    insert(740, "SetPlanetSelectable", R_VOID, {R_INT, R_INT}, &SetPlanetSelectable);
    insert(741, "GetPlanetSelectable", R_INT, {R_INT}, &GetPlanetSelectable);
    insert(742, "SetPlanetAvailable", R_VOID, {R_INT, R_INT}, &SetPlanetAvailable);
    insert(743, "GetPlanetAvailable", R_INT, {R_INT}, &GetPlanetAvailable);
    insert(744, "GetSelectedPlanet", R_INT, {}, &GetSelectedPlanet);
    insert(745, "SoundObjectFadeAndStop", R_VOID, {R_OBJECT, R_FLOAT}, &SoundObjectFadeAndStop);
    insert(746, "SetAreaFogColor", R_VOID, {R_OBJECT, R_FLOAT, R_FLOAT, R_FLOAT}, &SetAreaFogColor);
    insert(747, "ChangeItemCost", R_VOID, {R_STRING, R_FLOAT}, &ChangeItemCost);
    insert(748, "GetIsLiveContentAvailable", R_INT, {R_INT}, &GetIsLiveContentAvailable);
    insert(749, "ResetDialogState", R_VOID, {}, &ResetDialogState);
    insert(750, "SetGoodEvilValue", R_VOID, {R_OBJECT, R_INT}, &SetGoodEvilValue);
    insert(751, "GetIsPoisoned", R_INT, {R_OBJECT}, &GetIsPoisoned);
    insert(752, "GetSpellTarget", R_OBJECT, {R_OBJECT}, &GetSpellTarget);
    insert(753, "SetSoloMode", R_VOID, {R_INT}, &SetSoloMode);
    insert(757, "CancelPostDialogCharacterSwitch", R_VOID, {}, &CancelPostDialogCharacterSwitch);
    insert(758, "SetMaxHitPoints", R_VOID, {R_OBJECT, R_INT}, &SetMaxHitPoints);
    insert(759, "NoClicksFor", R_VOID, {R_FLOAT}, &NoClicksFor);
    insert(760, "HoldWorldFadeInForDialog", R_VOID, {}, &HoldWorldFadeInForDialog);
    insert(761, "ShipBuild", R_INT, {}, &ShipBuild);
    insert(762, "SurrenderRetainBuffs", R_VOID, {}, &SurrenderRetainBuffs);
    insert(763, "SuppressStatusSummaryEntry", R_VOID, {R_INT}, &SuppressStatusSummaryEntry);
    insert(764, "GetCheatCode", R_INT, {R_INT}, &GetCheatCode);
    insert(765, "SetMusicVolume", R_VOID, {R_FLOAT}, &SetMusicVolume);
    insert(766, "CreateItemOnFloor", R_OBJECT, {R_STRING, R_LOCATION, R_INT}, &CreateItemOnFloor);
    insert(767, "SetAvailableNPCId", R_VOID, {R_INT, R_OBJECT}, &SetAvailableNPCId);
    insert(768, "GetScriptParameter", R_INT, {R_INT}, &GetScriptParameter);
    insert(769, "SetFadeUntilScript", R_VOID, {}, &SetFadeUntilScript);
    insert(771, "GetItemComponent", R_INT, {}, &GetItemComponent);
    insert(772, "GetItemComponentPieceValue", R_INT, {}, &GetItemComponentPieceValue);
    insert(773, "ShowChemicalUpgradeScreen", R_VOID, {R_OBJECT}, &ShowChemicalUpgradeScreen);
    insert(774, "GetChemicals", R_INT, {}, &GetChemicals);
    insert(775, "GetChemicalPieceValue", R_INT, {}, &GetChemicalPieceValue);
    insert(776, "GetSpellForcePointCost", R_INT, {}, &GetSpellForcePointCost);
    insert(783, "GetFeatAcquired", R_INT, {R_INT, R_OBJECT}, &GetFeatAcquired);
    insert(784, "GetSpellAcquired", R_INT, {R_INT, R_OBJECT}, &GetSpellAcquired);
    insert(785, "ShowSwoopUpgradeScreen", R_VOID, {}, &ShowSwoopUpgradeScreen);
    insert(786, "GrantFeat", R_VOID, {R_INT, R_OBJECT}, &GrantFeat);
    insert(787, "GrantSpell", R_VOID, {R_INT, R_OBJECT}, &GrantSpell);
    insert(788, "SpawnMine", R_VOID, {R_INT, R_LOCATION, R_INT, R_INT, R_OBJECT}, &SpawnMine);
    insert(791, "SetFakeCombatState", R_VOID, {R_OBJECT, R_INT}, &SetFakeCombatState);
    insert(793, "GetOwnerDemolitionsSkill", R_INT, {R_OBJECT}, &GetOwnerDemolitionsSkill);
    insert(794, "SetOrientOnClick", R_VOID, {R_OBJECT, R_INT}, &SetOrientOnClick);
    insert(795, "GetInfluence", R_INT, {R_INT}, &GetInfluence);
    insert(796, "SetInfluence", R_VOID, {R_INT, R_INT}, &SetInfluence);
    insert(797, "ModifyInfluence", R_VOID, {R_INT, R_INT}, &ModifyInfluence);
    insert(798, "GetRacialSubType", R_INT, {R_OBJECT}, &GetRacialSubType);
    insert(799, "IncrementGlobalNumber", R_VOID, {R_STRING, R_INT}, &IncrementGlobalNumber);
    insert(800, "DecrementGlobalNumber", R_VOID, {R_STRING, R_INT}, &DecrementGlobalNumber);
    insert(801, "SetBonusForcePoints", R_VOID, {R_OBJECT, R_INT}, &SetBonusForcePoints);
    insert(802, "AddBonusForcePoints", R_VOID, {R_OBJECT, R_INT}, &AddBonusForcePoints);
    insert(803, "GetBonusForcePoints", R_VOID, {R_OBJECT}, &GetBonusForcePoints);
    insert(805, "IsMoviePlaying", R_INT, {}, &IsMoviePlaying);
    insert(806, "QueueMovie", R_VOID, {R_STRING, R_INT}, &QueueMovie);
    insert(807, "PlayMovieQueue", R_VOID, {R_INT}, &PlayMovieQueue);
    insert(808, "YavinHackDoorClose", R_VOID, {R_OBJECT}, &YavinHackDoorClose);
    insert(810, "IsStealthed", R_INT, {R_OBJECT}, &IsStealthed);
    insert(811, "IsMeditating", R_INT, {R_OBJECT}, &IsMeditating);
    insert(812, "IsInTotalDefense", R_INT, {R_OBJECT}, &IsInTotalDefense);
    insert(813, "SetHealTarget", R_VOID, {R_OBJECT, R_OBJECT}, &SetHealTarget);
    insert(814, "GetHealTarget", R_OBJECT, {R_OBJECT}, &GetHealTarget);
    insert(815, "GetRandomDestination", R_VECTOR, {R_OBJECT, R_INT}, &GetRandomDestination);
    insert(816, "IsFormActive", R_INT, {R_OBJECT, R_INT}, &IsFormActive);
    insert(817, "GetSpellFormMask", R_INT, {R_INT}, &GetSpellFormMask);
    insert(818, "GetSpellBaseForcePointCost", R_INT, {R_INT}, &GetSpellBaseForcePointCost);
    insert(819, "SetKeepStealthInDialog", R_VOID, {R_INT}, &SetKeepStealthInDialog);
    insert(820, "HasLineOfSight", R_INT, {R_VECTOR, R_VECTOR, R_OBJECT, R_OBJECT}, &HasLineOfSight);
    insert(821, "ShowDemoScreen", R_INT, {R_STRING, R_INT, R_INT, R_INT, R_INT}, &ShowDemoScreen);
    insert(822, "ForceHeartbeat", R_VOID, {R_OBJECT}, &ForceHeartbeat);
    insert(824, "IsRunning", R_INT, {R_OBJECT}, &IsRunning);
    insert(826, "SetForfeitConditions", R_VOID, {R_INT}, &SetForfeitConditions);
    insert(827, "GetLastForfeitViolation", R_INT, {}, &GetLastForfeitViolation);
    insert(828, "ModifyReflexSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyReflexSavingThrowBase);
    insert(829, "ModifyFortitudeSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyFortitudeSavingThrowBase);
    insert(830, "ModifyWillSavingThrowBase", R_VOID, {R_OBJECT, R_INT}, &ModifyWillSavingThrowBase);
    insert(831, "GetScriptStringParameter", R_STRING, {}, &GetScriptStringParameter);
    insert(832, "GetObjectPersonalSpace", R_FLOAT, {R_OBJECT}, &GetObjectPersonalSpace);
    insert(833, "AdjustCreatureAttributes", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustCreatureAttributes);
    insert(834, "SetCreatureAILevel", R_VOID, {R_OBJECT, R_INT}, &SetCreatureAILevel);
    insert(835, "ResetCreatureAILevel", R_VOID, {R_OBJECT}, &ResetCreatureAILevel);
    insert(836, "AddAvailablePUPByTemplate", R_INT, {R_INT, R_STRING}, &AddAvailablePUPByTemplate);
    insert(837, "AddAvailablePUPByObject", R_INT, {R_INT, R_OBJECT}, &AddAvailablePUPByObject);
    insert(838, "AssignPUP", R_INT, {R_INT, R_INT}, &AssignPUP);
    insert(839, "SpawnAvailablePUP", R_OBJECT, {R_INT, R_LOCATION}, &SpawnAvailablePUP);
    insert(840, "AddPartyPuppet", R_INT, {R_INT, R_OBJECT}, &AddPartyPuppet);
    insert(841, "GetPUPOwner", R_OBJECT, {R_OBJECT}, &GetPUPOwner);
    insert(842, "GetIsPuppet", R_INT, {R_OBJECT}, &GetIsPuppet);
    insert(844, "GetIsPartyLeader", R_INT, {R_OBJECT}, &GetIsPartyLeader);
    insert(845, "GetPartyLeader", R_OBJECT, {}, &GetPartyLeader);
    insert(846, "RemoveNPCFromPartyToBase", R_INT, {R_INT}, &RemoveNPCFromPartyToBase);
    insert(847, "CreatureFlourishWeapon", R_VOID, {R_OBJECT}, &CreatureFlourishWeapon);
    insert(850, "ChangeObjectAppearance", R_VOID, {R_OBJECT, R_INT}, &ChangeObjectAppearance);
    insert(851, "GetIsXBox", R_INT, {}, &GetIsXBox);
    insert(854, "PlayOverlayAnimation", R_VOID, {R_OBJECT, R_INT}, &PlayOverlayAnimation);
    insert(855, "UnlockAllSongs", R_VOID, {}, &UnlockAllSongs);
    insert(856, "DisableMap", R_VOID, {R_INT}, &DisableMap);
    insert(857, "DetonateMine", R_VOID, {R_OBJECT}, &DetonateMine);
    insert(858, "DisableHealthRegen", R_VOID, {R_INT}, &DisableHealthRegen);
    insert(859, "SetCurrentForm", R_VOID, {R_OBJECT, R_INT}, &SetCurrentForm);
    insert(860, "SetDisableTransit", R_VOID, {R_INT}, &SetDisableTransit);
    insert(861, "SetInputClass", R_VOID, {R_INT}, &SetInputClass);
    insert(862, "SetForceAlwaysUpdate", R_VOID, {R_OBJECT, R_INT}, &SetForceAlwaysUpdate);
    insert(863, "EnableRain", R_VOID, {R_INT}, &EnableRain);
    insert(864, "DisplayMessageBox", R_VOID, {R_INT, R_STRING}, &DisplayMessageBox);
    insert(865, "DisplayDatapad", R_VOID, {R_OBJECT}, &DisplayDatapad);
    insert(866, "RemoveHeartbeat", R_VOID, {R_OBJECT}, &RemoveHeartbeat);
    insert(867, "RemoveEffectByID", R_VOID, {R_OBJECT, R_INT}, &RemoveEffectByID);
    insert(868, "RemoveEffectByExactMatch", R_VOID, {R_OBJECT, R_EFFECT}, &RemoveEffectByExactMatch);
    insert(869, "AdjustCreatureSkills", R_VOID, {R_OBJECT, R_INT, R_INT}, &AdjustCreatureSkills);
    insert(870, "GetSkillRankBase", R_INT, {R_INT, R_OBJECT}, &GetSkillRankBase);
    insert(871, "EnableRendering", R_VOID, {R_OBJECT, R_INT}, &EnableRendering);
    insert(872, "GetCombatActionsPending", R_INT, {R_OBJECT}, &GetCombatActionsPending);
    insert(873, "SaveNPCByObject", R_VOID, {R_INT, R_OBJECT}, &SaveNPCByObject);
    insert(874, "SavePUPByObject", R_VOID, {R_INT, R_OBJECT}, &SavePUPByObject);
    insert(875, "GetIsPlayerMadeCharacter", R_INT, {R_OBJECT}, &GetIsPlayerMadeCharacter);
    insert(876, "RebuildPartyTable", R_VOID, {}, &RebuildPartyTable);
}

} // namespace game

} // namespace reone
