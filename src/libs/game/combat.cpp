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

#include "reone/game/combat.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/coopsession.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/object/door.h"
#include "reone/game/object/placeable.h"
#include "reone/game/reputes.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/smallset.h"

using namespace reone::graphics;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kRoundDuration = 3.0f;
static constexpr float kDeactivateDelay = 8.0f;
static constexpr float kMaxAttackRange = 20.0f;
static constexpr float kAttackTargetSearchPadding = 2.0f;
// Round time is a float sum of tick lengths: 90 ticks of 1/30 s come out a
// hair under 3 s. Reaching a boundary within this counts as reaching it, so a
// round is exactly 90 ticks and its reply starts on tick 45, not a tick later.
static constexpr float kRoundTimeEpsilon = 1e-4f;

static bool isUnavailableAttackTarget(const Creature &creature) {
    return creature.isDead() || creature.isTemporarilyDead();
}

bool CombatRound::canExecute(Action &action) const {
    State requiredState[] = {CombatRound::FirstAction, CombatRound::SecondAction};
    assert(actions.size() <= 2 && "no more than 2 actors in a round");
    for (unsigned i = 0; i < actions.size(); ++i) {
        if (actions[i].action.get() == &action) {
            return actions[i].participantBindingsLive() &&
                   actions[i].remainsInActorQueue() &&
                   action.runtimeDependenciesLive() &&
                   !action.isCompleted() &&
                   !action.isCancelled() &&
                   state == requiredState[i];
        }
    }
    return false;
}

bool CombatRound::RoundAction::participantBindingsLive() const {
    return attacker.resolve() &&
           (target.empty() || target.resolve());
}

bool CombatRound::RoundAction::remainsInActorQueue() const {
    if (!actorQueueAssociated) {
        return true;
    }
    auto actor = attacker.resolve();
    return actor && std::find(
                        actor->actions().begin(),
                        actor->actions().end(),
                        action) != actor->actions().end();
}

static std::shared_ptr<Object> getTarget(Action &action) {
    if (auto *attack = dyn_cast<AttackObjectAction>(&action)) {
        return attack->target();
    }
    if (auto *feat = dyn_cast<UseFeatAction>(&action)) {
        return feat->target();
    }
    // A scripted CutsceneAttack names its target too, so the opponent's
    // reply joins the same round as a duel instead of opening a round of its
    // own (P1 item 14).
    if (auto *cutscene = dyn_cast<CutsceneAttackAction>(&action)) {
        return cutscene->target();
    }

    return nullptr;
}

static FeatType getCombatFeat(Action &action) {
    if (auto *feat = dyn_cast<UseFeatAction>(&action)) {
        return isPhysicalAttackFeat(feat->feat())
                   ? feat->feat()
                   : FeatType::Invalid;
    }
    return FeatType::Invalid;
}

static void recordCombatAction(
    Object &actor,
    const std::shared_ptr<Object> &target,
    Action &action) {

    if (!target || !isHostileAction(action)) {
        return;
    }

    target->setLastHostileActor(actor.id());
    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->beginCombatAttack(target, getCombatFeat(action));
    }
}

static bool isRoundPastFirstAttack(float time) {
    return time + kRoundTimeEpsilon >= 0.5f * kRoundDuration;
}

static float getAttackTargetSearchRange(const Creature &attacker) {
    return std::min(attacker.getAttackRange(), kMaxAttackRange) + kAttackTargetSearchPadding;
}

static std::shared_ptr<Creature> findNearestEnemy(
    Area &area,
    const Creature &attacker,
    const Creature &observer,
    float maxDistance,
    const IReputes &reputes) {

    std::shared_ptr<Creature> nearest;
    float nearestDistance = maxDistance;

    for (const std::shared_ptr<Object> &object : area.getObjectsByType(ObjectType::Creature)) {
        auto candidate = std::static_pointer_cast<Creature>(object);
        if (candidate->id() == attacker.id() ||
            candidate->id() == observer.id() ||
            isUnavailableAttackTarget(*candidate)) {
            continue;
        }

        bool isEnemy = observer.id() == attacker.id()
                           ? reputes.getIsEnemy(attacker, *candidate)
                           : reputes.getIsEnemy(*candidate, attacker);
        if (!isEnemy || !observer.perception().sees(candidate->id())) {
            continue;
        }

        float distance = attacker.getDistanceTo(*candidate) -
                         observer.creaturePersonalSpace() -
                         candidate->creaturePersonalSpace();
        if (distance >= nearestDistance || !area.isObjectSeen(observer, *candidate)) {
            continue;
        }

        nearest = std::move(candidate);
        nearestDistance = distance;
    }

    return nearest;
}

CombatRound *Combat::findRoundForAction(
    const std::shared_ptr<Action> &action, const Creature &attacker) {

    for (auto &round : _rounds) {
        for (CombatRound::RoundAction &roundAction : round->actions) {
            auto boundAttacker = roundAction.attacker.resolve();
            if (boundAttacker.get() == &attacker &&
                roundAction.action == action) {
                return round.get();
            }
        }
    }
    return nullptr;
}

// If there is an incomplete combat round where attacker and target roles
// are reversed, append to that round.
CombatRound *Combat::tryAppendAction(
    const std::shared_ptr<Action> &action,
    const std::shared_ptr<Creature> &attacker,
    const std::shared_ptr<Object> &target,
    bool actorQueueAssociated) {

    for (auto &round : _rounds) {
        if (round->state == CombatRound::Finished) {
            // Finished rounds may not be retired until all actions are
            // completed. Do not consider them for new rounds.
            continue;
        }

        bool isReversed = false;
        for (CombatRound::RoundAction &roundAction : round->actions) {
            if (roundAction.attacker.resolve().get() == target.get() &&
                roundAction.target.resolve().get() == attacker.get()) {
                isReversed = true;
                break;
            }
        }

        if (!isReversed || round->actions.size() > 1) {
            continue;
        }

        // Found a round to append.
        round->actions.emplace_back(
            action, attacker, target, actorQueueAssociated);
        round->duel = true;
        return round.get();
    }
    return nullptr;
}

const CombatRound &Combat::addAction(const std::shared_ptr<Action> &action, Object &actor) {
    auto attacker = _game.getObjectById<Creature>(actor.id());
    if (!attacker || attacker.get() != &actor) {
        throw std::logic_error(
            "Combat action actor is not the live registered Creature");
    }
    bool actorQueueAssociated = std::find(
                                    attacker->actions().begin(),
                                    attacker->actions().end(),
                                    action) != attacker->actions().end();

    // If attacker has already started a combat round, return it.
    if (CombatRound *round = findRoundForAction(action, *attacker)) {
        return *round;
    }

    // Find an existing round where target and attacker roles are reversed, and
    // append the action to this round.
    std::shared_ptr<Object> target = getTarget(*action);
    if (target) {
        if (CombatRound *round = tryAppendAction(
                action, attacker, target, actorQueueAssociated)) {
            recordCombatAction(actor, target, *action);
            debug(str(boost::format("Append attack: %s -> %s") % actor.tag() % target->tag()), LogChannel::Combat);
            return *round;
        }
    }

    // Otherwise, start a new combat round
    _rounds.emplace_back(std::make_unique<CombatRound>(
        action, attacker, target, actorQueueAssociated));
    CombatRound &newRound = *_rounds.back();

    // Two creatures fighting each other duel from the round's first swing. The
    // opponent's reply is queued by its end-of-round script a moment after
    // this round opens, so waiting for it to append would play every round's
    // lead attack as a plain swing with no parry (P2 item 6). An opponent
    // whose attack target is this attacker replies in this round.
    if (auto *opponent = target ? dyn_cast<Creature>(target.get()) : nullptr) {
        if (!isUnavailableAttackTarget(*opponent) && opponent->getAttackTarget().get() == attacker.get()) {
            newRound.duel = true;
        }
    }

    if (target) {
        recordCombatAction(actor, target, *action);
        debug(str(boost::format("Start round: %s -> %s") % actor.tag() % target->tag()), LogChannel::Combat);
    } else {
        debug(str(boost::format("Start round: %s") % actor.tag()), LogChannel::Combat);
    }

    if (auto *creature = dyn_cast<Creature>(&actor)) {
        creature->activateCombat();
    }
    if (target && isa<Creature>(target)) {
        cast<Creature>(target)->activateCombat();
    }

    return newRound;
}

void Combat::update(float dt) {
    pruneInvalidRounds();

    for (auto &round : _rounds) {
        updateRound(*round, dt);
    }

    pruneInvalidRounds();
}

static bool isActionFinished(const CombatRound::RoundAction &action) {
    return action.action->isCompleted() || action.action->isCancelled();
}

void Combat::cancelRound(CombatRound &round) {
    for (auto &roundAction : round.actions) {
        if (roundAction.action->isCompleted()) {
            continue;
        }
        if (roundAction.action->isCancelled()) {
            roundAction.action->complete();
            continue;
        }
        auto attacker = roundAction.attacker.resolve();
        if (attacker) {
            roundAction.action->cancel(roundAction.action, *attacker);
        } else {
            roundAction.action->complete();
        }
        roundAction.action->markCancelled();
        if (!roundAction.action->isCompleted()) {
            roundAction.action->complete();
        }
    }
}

// A round dropped before it finished (its actions were cleared: a leader
// switch, Cancel Combat, a script's ClearAllActions) must still let its
// participants leave combat, as a finished round does. Otherwise they stay in
// combat mode with nothing to end it (playtest 1, item 11).
void Combat::releaseParticipants(CombatRound &round) {
    for (auto &roundAction : round.actions) {
        if (auto attacker = roundAction.attacker.resolve()) {
            attacker->deactivateCombat(kDeactivateDelay);
        }
        if (auto target = roundAction.target.resolve()) {
            if (auto *creature = dyn_cast<Creature>(target.get())) {
                creature->deactivateCombat(kDeactivateDelay);
            }
        }
    }
}

void Combat::pruneInvalidRounds() {
    for (auto it = _rounds.begin(); it != _rounds.end();) {
        CombatRound &round = **it;
        bool bindingsLive = std::all_of(
            round.actions.begin(),
            round.actions.end(),
            [](const CombatRound::RoundAction &action) {
                return action.participantBindingsLive() &&
                       action.remainsInActorQueue() &&
                       action.action->runtimeDependenciesLive();
            });
        bool anyActionFinished = std::any_of(
            round.actions.begin(), round.actions.end(), isActionFinished);
        bool actionsFinished = std::all_of(
            round.actions.begin(), round.actions.end(), isActionFinished);

        if (!bindingsLive ||
            (round.state != CombatRound::Finished && anyActionFinished)) {
            cancelRound(round);
            releaseParticipants(round);
            it = _rounds.erase(it);
        } else if (round.state == CombatRound::Finished && actionsFinished) {
            it = _rounds.erase(it);
        } else {
            ++it;
        }
    }
}

void Combat::updateRound(CombatRound &round, float dt) {
    round.time += dt;

    switch (round.state) {
    case CombatRound::Pending: {
        round.state = CombatRound::FirstAction;
        return;
    }
    case CombatRound::FirstAction: {
        if (isRoundPastFirstAttack(round.time)) {
            round.state = CombatRound::SecondAction;
        }
        return;
    }
    case CombatRound::SecondAction: {
        if (round.time + kRoundTimeEpsilon >= kRoundDuration) {
            round.state = CombatRound::Finished;
            finishRound(round);
        }
        return;
    }
    case CombatRound::Finished:
        return;
    }
}

void Combat::finishRound(CombatRound &round) {
    SmallSet<Object *, 4> seenObjects;
    SmallVector<RuntimeObjectRef<Object>, 4> objects;
    SmallSet<Creature *, 2> seenAttackers;
    SmallVector<RuntimeObjectRef<Creature>, 2> attackers;
    for (CombatRound::RoundAction &action : round.actions) {
        auto attacker = action.attacker.resolve();
        auto target = action.target.resolve();
        if (!attacker || (!action.target.empty() && !target)) {
            continue;
        }
        if (seenObjects.insert(attacker.get()).second) {
            objects.emplace_back(attacker);
        }
        if (target && seenObjects.insert(target.get()).second) {
            objects.emplace_back(target);
        }
        if (isHostileAction(*action.action) &&
            seenAttackers.insert(attacker.get()).second) {
            attackers.emplace_back(attacker);
        }

        if (Logger::instance.isChannelEnabled(LogChannel::Combat)) {
            debug(str(boost::format("Finish round: %s") % attacker->tag()), LogChannel::Combat);
        }
    }

    for (const auto &reference : attackers) {
        if (auto attacker = reference.resolve()) {
            attacker->finishCombatRound();
        }
    }

    std::shared_ptr<Creature> leader = _game.party().getLeader();
    for (const auto &reference : objects) {
        std::shared_ptr<Object> object = reference.resolve();
        if (object && isa<Creature>(object)) {
            auto &participant = cast<Creature>(*object);
            // A co-op client's creature is its player's leader: no generic
            // OnEndRound orders (k_ai_master would queue attacks straight
            // back after Cancel Combat), only auto-combat when switched on.
            auto *coop = _game.coop();
            bool playerHand = (leader && participant.id() == leader->id()) ||
                              (coop && coop->isPlayerControlled(participant.id()));
            if (!playerHand) {
                participant.runEndRoundScript();
            } else if (participant.autoCombat() && !participant.hasUserActionsPending()) {
                // Auto-combat: the generic AI decides the leader's next round
                // once the player's own queued actions have run.
                participant.runAutoCombatRound();
            }
            participant.deactivateCombat(kDeactivateDelay);
        }
    }

    // The player's hands keep fighting: the host's leader and every creature
    // a co-op client controls take no OnEndRound orders, so the round's end
    // carries their attack on.
    SmallVector<std::shared_ptr<Creature>, 4> hands;
    if (leader) {
        hands.push_back(leader);
    }
    if (auto *coop = _game.coop()) {
        for (const auto &reference : objects) {
            auto object = reference.resolve();
            auto creature = object ? dyn_cast<Creature>(object) : nullptr;
            if (creature && creature != leader && coop->isPlayerControlled(creature->id())) {
                hands.push_back(std::move(creature));
            }
        }
    }
    for (const auto &hand : hands) {
        continuePlayerAttack(round, hand);
    }
}

void Combat::continuePlayerAttack(CombatRound &round, const std::shared_ptr<Creature> &hand) {
    if (!hand || !_game.isRuntimeObjectLive(*hand) ||
        isUnavailableAttackTarget(*hand) || hand->autoCombat() || hand->isCombatHeld()) {
        return;
    }

    for (CombatRound::RoundAction &action : round.actions) {
        if (action.attacker.resolve().get() != hand.get()) {
            continue;
        }

        if (hand->hasUserActionsPending(action.action.get())) {
            continue;
        }

        std::shared_ptr<Module> module = _game.module();

        std::shared_ptr<Object> target = action.target.resolve();
        auto door = target ? dyn_cast<Door>(target) : nullptr;
        auto placeable = target ? dyn_cast<Placeable>(target) : nullptr;
        if (door || placeable) {
            // Bashing: the hand goes on hitting a locked door or container
            // until it breaks or the player gives another order.
            bool bashable = door ? canBashDoor(*door, *hand, _services.game.reputes)
                                 : canBashPlaceable(*placeable, *hand, _services.game.reputes);
            if (isa<AttackObjectAction>(action.action.get()) && hand->getCurrentAction() == action.action && bashable) {
                hand->addAction(_game.newAction<AttackObjectAction>(std::move(target)));
            }
            continue;
        }
        auto targetCreature = target ? dyn_cast<Creature>(target) : nullptr;
        bool targetUnavailable = !targetCreature || isUnavailableAttackTarget(*targetCreature);
        if (!targetUnavailable) {
            if (hand->getCurrentAction() != action.action) {
                continue;
            }
            if (!_services.game.reputes.getIsEnemy(*targetCreature, *hand)) {
                continue;
            }

            hand->addAction(_game.newAction<AttackObjectAction>(std::move(target)));
            continue;
        }

        if (!isa<AttackObjectAction>(action.action.get()) &&
            !isa<UseFeatAction>(action.action.get())) {
            continue;
        }

        std::shared_ptr<Creature> enemy;
        std::shared_ptr<Area> area = module ? module->area() : nullptr;
        if (area) {
            float searchRange = getAttackTargetSearchRange(*hand);
            if (targetCreature) {
                enemy = findNearestEnemy(
                    *area,
                    *hand,
                    *targetCreature,
                    searchRange,
                    _services.game.reputes);
            }
            if (!enemy) {
                enemy = findNearestEnemy(
                    *area,
                    *hand,
                    *hand,
                    searchRange,
                    _services.game.reputes);
            }
        }
        if (enemy) {
            hand->addAction(_game.newAction<AttackObjectAction>(std::move(enemy)));
        } else {
            hand->deactivateCombat(0.0f);
        }
    }
}

} // namespace game

} // namespace reone
