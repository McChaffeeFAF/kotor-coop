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

#include "reone/system/timer.h"

#include "action.h"
#include "effect/damage.h"
#include "object/creature.h"
#include "reone/system/smallvector.h"
#include "types.h"

namespace reone {

namespace game {

class Game;
struct ServicesView;

/**
 * Combat round consists of either one or two actions. Second action is only
 * present when both combatants are creatures.
 *
 * Each creature keeps its own round in its side of the round: its clock and
 * how far it has got. A side that answers the other starts in step with it
 * and goes on from there on its own, so nothing done to one creature moves
 * the other's round (design note vrifftech_rules_triage, "Combat rounds").
 */
struct CombatRound {
    /** The length of a creature's round, in seconds. */
    static constexpr float kDuration = 3.0f;

    explicit CombatRound(const std::shared_ptr<Action> &action,
                         const std::shared_ptr<Creature> &attacker,
                         const std::shared_ptr<Object> &target,
                         bool actorQueueAssociated) {
        actions.emplace_back(
            action, attacker, target, actorQueueAssociated);
    }

    enum State {
        Pending,
        FirstAction,
        SecondAction,
        Finished
    };

    struct RoundAction {
        RoundAction(const std::shared_ptr<Action> &action,
                    const std::shared_ptr<Creature> &attacker,
                    const std::shared_ptr<Object> &target,
                    bool actorQueueAssociated) :
            action(action),
            attacker(attacker), target(target),
            actorQueueAssociated(actorQueueAssociated) {}

        std::shared_ptr<Action> action;
        RuntimeObjectRef<Creature> attacker;
        RuntimeObjectRef<Object> target;
        bool actorQueueAssociated {false};

        /** How far the creature's round has got; Finished once it has ended. */
        State state {Pending};
        /** The seconds of the creature's round gone by. */
        float time {0.0f};
        /**
         * The creature's command left the round (it was cleared, or finished
         * early) after taking its turn; the creature's round runs on to its
         * end, and its next attack waits for that end.
         */
        bool abandoned {false};
        /** The command has been let take its turn in the round. */
        mutable bool tookTurn {false};

        bool participantBindingsLive() const;
        bool remainsInActorQueue() const;
    };

    SmallVector<RoundAction, 2> actions;

    bool duel {false};

    /** The side of the round \p action takes its turn in, if any. */
    const RoundAction *sideOf(const Action &action) const;
    /** \p action may take its turn in the round now. */
    bool canExecute(Action &action) const;
    /** The round of \p action's creature has ended; true for an action not in the round. */
    bool hasEnded(const Action &action) const;
    /** The round has ended for every creature in it. */
    bool allEnded() const;
};

/**
 * Combat is used schedule attacks and form rounds. When a creature starts an
 * attack, Combat creates a new round for it. If the target attacks back, and
 * the original attack is not finished yet, the corresponding round becomes a
 * duel (a CombatRound with more than 1 attack).
 *
 * Actions may handle duel rounds differently. For example, melee duels enable
 * "cinematic" animations.
 */
class Combat {
public:
    Combat(
        Game &game,
        ServicesView &services) :
        _game(game),
        _services(services) {
    }

    /**
     * Adds an action to an existing combat round, or starts a new round, based
     * on attacker and target.
     */
    const CombatRound &addAction(const std::shared_ptr<Action> &action, Object &actor);

    void update(float dt);
    void reset() { _rounds.clear(); }
    size_t roundCount() const { return _rounds.size(); }

    /** Read-only view of the active rounds, oldest first. */
    const std::deque<std::unique_ptr<CombatRound>> &rounds() const { return _rounds; }

private:
    using RoundQueue = std::deque<std::unique_ptr<CombatRound>>;

    Game &_game;
    ServicesView &_services;

    RoundQueue _rounds;

    void updateRound(CombatRound &round, float dt);
    /**
     * The rounds of the creatures of \p sides have ended: their attackers
     * finish the round, the creatures not under a player's hand run their
     * end-of-round script, and the player's hands fight on. A side whose
     * command was given up (\p quiet) only finishes the round and lets its
     * creatures leave combat.
     */
    void finishSides(CombatRound &round, const SmallVector<size_t, 2> &sides, bool quiet);
    void continuePlayerAttack(CombatRound &round, const SmallVector<size_t, 2> &sides, const std::shared_ptr<Creature> &hand);
    void pruneInvalidRounds();
    void releaseSide(CombatRound::RoundAction &side);
    void cancelSide(CombatRound::RoundAction &side);
    /** A round the creature's given-up command took its turn in, still running out. */
    CombatRound *findRunningAbandonedRound(const Creature &attacker);

    CombatRound *findRoundForAction(
        const std::shared_ptr<Action> &action,
        const Creature &attacker);
    CombatRound *tryAppendAction(
        const std::shared_ptr<Action> &action,
        const std::shared_ptr<Creature> &attacker,
        const std::shared_ptr<Object> &target,
        bool actorQueueAssociated);
};

} // namespace game

} // namespace reone
