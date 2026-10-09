/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "reone/net/protocol.h"
#include "reone/resource/dialog.h"

namespace reone {

namespace game {

class Creature;
class Game;
class Object;

/** How far, in metres, a command may reach from the actor. */
constexpr float kCoopMaxCommandRange = 60.0f;
/** Commands a client may send per second of simulation. */
constexpr int kCoopMaxCommandsPerSecond = 15;

/** What a use of an object turns out to be; the session carries out the last two. */
enum class CoopUse {
    Normal,     // an action queued on the creature (executeCoopCommand)
    Container,  // walk there, then the client opens its own container screen
    Transition, // an area exit: the ready check
    Placeable   // a placeable with no conversation (a console): walk there and use it
};

/** What a validated command acts on. */
struct CoopCommandTargets {
    std::shared_ptr<Creature> actor;
    std::shared_ptr<Object> target;
    CoopUse use {CoopUse::Normal};
    bool story {false}; // a placeable use that changes story state (the terminal design)
};

/**
 * Does this compiled script change story state: set globals or locals,
 * change objects (doors, creatures, placeables), give or take items, XP or
 * credits, touch the party or the journal, or start a module? Read from its
 * ACTION instructions (routine names from the script table), never run.
 * Anything unknown counts as story. Cached per script.
 */
bool coopScriptChangesStory(Game &game, const std::string &script);

/** Does this compiled script open a screen (workbench, store, party selection, galaxy map, Pazaak)? */
bool coopScriptOpensScreen(Game &game, const std::string &script);

/**
 * Does any node of this dialog open a store, start Pazaak or load a
 * minigame module (a "*mg" module)? Read from the action scripts' compiled
 * code, never run. Cached per dialog.
 */
bool coopDialogOffersStoreOrMinigame(Game &game, const std::string &dialogResRef);

/**
 * Would the retail conversation reach a node isTarget accepts? Walks it as
 * Conversation does: the first active start entry, every active reply of an
 * entry (the player may pick any), then the first active entry after a
 * reply. A link is active as Conversation::isLinkActive has it (Active and
 * Active2, their Not flags and Logic); condition evaluates one Active
 * script. No action script runs.
 */
bool coopDialogReaches(
    const resource::Dialog &dialog,
    const std::function<bool(const std::string &, const resource::Dialog::EntryReplyLink::ConditionParams &)> &condition,
    const std::function<bool(const resource::Dialog::EntryReply &)> &isTarget);

/**
 * On the host, as the game stands now: would npc's conversation reach the
 * node whose action script opens a store (OfferKind::Store: OpenStore) or
 * starts Pazaak (OfferKind::Pazaak: PlayPazaak)? The Active conditions run
 * (StartingConditional, caller the NPC); the action scripts never do.
 * Other kinds are always reachable.
 */
bool coopDialogReachesOffer(Game &game, const Object &npc, net::OfferKind kind);

/**
 * Check one client command against the creature that client controls.
 *
 * The host is the authority: a client may only drive its own creature, only
 * while it is alive and in the current area, only at targets that exist in
 * that area within reach, and only with items, feats and powers that
 * creature actually has. Story state the host owns (a conversation or a
 * cutscene in progress, a load) blocks input for everyone. A stand-in
 * (standIn, design item 12) is the player's creature though it is not in
 * the party.
 */
net::CommandReject validateCoopCommand(
    Game &game,
    const net::Command &command,
    uint32_t controlledId,
    CoopCommandTargets &targets,
    bool standIn = false,
    bool ownItemsOnly = false);

/**
 * Queue a validated command as the normal action a player's input queues:
 * the same actions Module::onObjectClick and the action bar queue. Uses the
 * session carries out itself (containers, exits) are left alone.
 */
void executeCoopCommand(Game &game, const net::Command &command, const CoopCommandTargets &targets);

/** A sliding one-second window over simulation ticks. */
class CoopRateLimiter {
public:
    bool allow(uint64_t tick, int tickRate, int perSecond = kCoopMaxCommandsPerSecond);

private:
    std::deque<uint64_t> _ticks;
};

} // namespace game

} // namespace reone
