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

#include <cstdint>

namespace reone {

namespace game {

class Game;
class Creature;

/**
 * The co-op control policy: who drives a creature right now, and what that
 * allows. Every rule that decides between the party AI, the host's hand, a
 * guest's hand and a story script lives here, as pure functions over two
 * small fact structs; the callers (the companion brain, auto-combat, idle
 * engage and follow, steering, command validation, the script routines and
 * the door/trigger/placeable script hooks) only gather facts and ask.
 *
 * Without a co-op host session every guest fact is false and each query
 * gives the single-player answer.
 */
namespace coopcontrol {

/** Who drives a creature. */
enum class Controller {
    AI,         // the party AI or the creature's own scripts
    HostPlayer, // the host's hand: the party leader on the host
    Guest,      // a co-op client's hand (CreatureFacts::guestClient names it when known)
    Script,     // a player's hand held by a story script sequence (or NoClicksFor)
    Puppet      // a TSL puppet: follows its owner, no hand of its own
};

const char *controllerName(Controller controller);

/** What the policy needs to know about one creature. */
struct CreatureFacts {
    bool dead {false};
    bool partyMember {false};     // a member of the host's companion party
    bool hostLeader {false};      // the host's party leader
    bool hostPlayer {false};      // the host's own player character (Party::player)
    bool guestControlled {false}; // a welcomed co-op client controls it (host side only)
    uint32_t guestClient {0};     // that client's id, when the caller knows it (informational)
    bool puppet {false};
    bool autoCombat {false};

    // Smarter companions: its OnEndRound is the generic party AI script.
    bool genericRoundScript {false};

    // The script hold: a story script's action sequence is queued on it, and
    // whether the front script action has held it past kCoopScriptHoldSeconds.
    bool scriptSequence {false};
    bool scriptHoldExpired {false};
};

/** What the policy needs to know about the session and the game. */
struct WorldFacts {
    bool coopHost {false};
    bool coopClient {false};
    bool dedicated {false};
    bool hostOnlyScenes {false};
    bool smartCompanions {false};
    bool clicksBlocked {false}; // NoClicksFor is running
};

/** The object whose script asks (the Caller of a GetIsPC). */
enum class ScriptKind {
    Creature, // the AI, perception, conversations: one PC, as in single player
    Door,
    Trigger,
    Placeable,
    Other
};

// Who drives

Controller controllerOf(const CreatureFacts &creature, const WorldFacts &world);

/** The host's leader or a guest's character: the party AI gives it no orders. */
bool isPlayerHand(const CreatureFacts &creature);

// Party AI

/** Smarter Companions: the brain decides this creature's round (C13). */
bool mayBrainAct(const CreatureFacts &creature, const WorldFacts &world);

/** The generic OnEndRound runs for it at the round's end (not a player's hand). */
bool mayRunRoundScript(const CreatureFacts &creature);

/**
 * Auto-combat on a player's hand fights only while there is a fight; out of
 * combat the hand stays its player's. Anyone else's round is the AI's anyway.
 */
bool mayAutoCombatAct(const CreatureFacts &creature, bool fighting);

/** The follow and random walks an auto-combat round queues are dropped (a guest goes where its player sends it). */
bool autoRoundDropsFollow(const CreatureFacts &creature);

/** OnAttacked, OnDamaged and OnNotice run for it (no party AI orders on a guest's hand). */
bool mayRunReactionScripts(const CreatureFacts &creature);

/** Restarts its follow of the leader between heartbeats (never a guest's character). */
bool mayFollowLeader(const CreatureFacts &creature);

/** Joins a fight it idles in (party members and guests' characters, not the host's leader). */
bool mayIdleEngage(const CreatureFacts &creature);

/** An idle engage asks its own round script first (an AI companion), not just a plain attack (a guest). */
bool idleEngageAsksRoundScript(const CreatureFacts &creature);

// Player input

/** A story script holds the creature: a sequence it must finish, or NoClicksFor. */
bool scriptHoldsCreature(const CreatureFacts &creature, const WorldFacts &world);

/** Its player's commands and clicks are taken (false while a script holds it). */
bool playerInputAllowed(const CreatureFacts &creature, const WorldFacts &world);

/** The host's player may switch to it and lead with it. */
bool mayLeadParty(const CreatureFacts &creature, const WorldFacts &world);

/** The front of a steered creature's queue, as steering sees it. */
struct FrontAction {
    bool present {false};
    bool userAction {false};       // a player ordered it
    bool movementRestricted {false};
    bool knockedDown {false};
    bool heldPose {false};
};

/**
 * A guest's movement keys cancel what holds the creature mid-swing: an attack
 * the player did not order (the round's automatic continuation, an idle
 * engage). Not a knockdown, a held pose or the player's own order.
 */
bool steeringOverridesAction(const FrontAction &front);

// Scripts

/** GetIsPC: the host's PC and leader always; a guest's character to door, trigger and placeable scripts. */
bool scriptSeesAsPC(const CreatureFacts &creature, const WorldFacts &world, ScriptKind caller);

/**
 * A script of this kind reads the creature as the party's leader
 * (GetPartyMemberByIndex(0), GetPartyLeader, IsPartyLeader): the host's
 * leader always; a guest's party member to a door, trigger or placeable
 * script it fired by its own act (a click, an open, a use; not walking
 * into a trigger), as the
 * companion a single player switched to would lead. Creature scripts keep
 * the one leader. The Endar Spire's first door opens for party member 0 being
 * end_trask.
 */
bool scriptSeesAsLeader(const CreatureFacts &creature, const WorldFacts &world, ScriptKind caller);

/**
 * The party index a script reads when the member at eventLeaderIndex leads
 * for it (scriptSeesAsLeader): indices 0 and eventLeaderIndex swap, as a
 * single player's portrait click swaps them; the rest stay. No lasting party
 * change. eventLeaderIndex <= 0: no swap.
 */
int scriptPartyIndex(int requested, int eventLeaderIndex);

enum class ScriptActor {
    Self,         // the creature itself is passed
    HostCharacter // the host's player character is passed in its place
};

/**
 * The object a door, trigger or placeable script gets for a creature: a
 * guest's character reads as the PC, unless the script names its tag (the
 * script is about that creature itself).
 */
ScriptActor scriptActorFor(const CreatureFacts &creature, const WorldFacts &world, bool scriptNamesTag);

/** hostOnlyScenes applies to this creature at all (cheap; check before scanning the script). */
bool sceneRuleApplies(const CreatureFacts &creature, const WorldFacts &world);

/**
 * A scene (a conversation or cutscene) the creature's entering starts may run
 * now. False only for a guest's character walking in on its own input with
 * hostOnlyScenes on; a script that moved it there runs the scene.
 */
bool sceneStartAllowed(const CreatureFacts &creature, const WorldFacts &world, bool scriptStartsScene, bool movedByScript);

// Fact gathering (game-bound adapters)

WorldFacts observeWorld(Game &game);

/** The cheap facts: party, leader, guest, puppet, auto-combat. */
CreatureFacts observe(Game &game, const Creature &creature);

/** Adds the script-hold facts (the action queue walk). */
void observeScriptHold(Game &game, const Creature &creature, CreatureFacts &facts);

} // namespace coopcontrol

} // namespace game

} // namespace reone
