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

#include "reone/game/coopcontrol_policy.h"

#include "reone/game/action.h"
#include "reone/game/coopcommand.h"
#include "reone/game/coopsession.h"
#include "reone/game/game.h"
#include "reone/game/object/creature.h"
#include "reone/game/party.h"

namespace reone {

namespace game {

namespace coopcontrol {

const char *controllerName(Controller controller) {
    switch (controller) {
    case Controller::AI:
        return "ai";
    case Controller::HostPlayer:
        return "host";
    case Controller::Guest:
        return "guest";
    case Controller::Script:
        return "script";
    case Controller::Puppet:
        return "puppet";
    }
    return "?";
}

Controller controllerOf(const CreatureFacts &creature, const WorldFacts &world) {
    if (isPlayerHand(creature) && scriptHoldsCreature(creature, world)) {
        return Controller::Script;
    }
    if (creature.guestControlled) {
        return Controller::Guest;
    }
    if (creature.hostLeader) {
        return Controller::HostPlayer;
    }
    if (creature.puppet) {
        return Controller::Puppet;
    }
    return Controller::AI;
}

bool isPlayerHand(const CreatureFacts &creature) {
    return creature.hostLeader || creature.guestControlled;
}

bool mayBrainAct(const CreatureFacts &creature, const WorldFacts &world) {
    if (!world.smartCompanions || creature.dead) {
        return false;
    }
    if (!creature.genericRoundScript) {
        return false; // scripted and boss creatures keep their own AI
    }
    if (!creature.partyMember) {
        return false; // hostile and neutral AI stays retail
    }
    if (creature.hostLeader && !creature.autoCombat) {
        return false; // the player's hand
    }
    if (creature.guestControlled) {
        return false; // a co-op player's character
    }
    return true;
}

bool mayRunRoundScript(const CreatureFacts &creature) {
    return !isPlayerHand(creature);
}

bool mayAutoCombatAct(const CreatureFacts &creature, bool fighting) {
    return !isPlayerHand(creature) || fighting;
}

bool autoRoundDropsFollow(const CreatureFacts &creature) {
    return creature.guestControlled;
}

bool mayRunReactionScripts(const CreatureFacts &creature) {
    return !creature.guestControlled;
}

bool mayFollowLeader(const CreatureFacts &creature) {
    return !creature.hostLeader && !creature.dead && creature.partyMember && !creature.guestControlled;
}

bool mayIdleEngage(const CreatureFacts &creature) {
    return !creature.hostLeader && (creature.guestControlled || creature.partyMember);
}

bool idleEngageAsksRoundScript(const CreatureFacts &creature) {
    return !creature.guestControlled;
}

bool scriptHoldsCreature(const CreatureFacts &creature, const WorldFacts &world) {
    if (world.clicksBlocked) {
        return true; // NoClicksFor: no player clicks, the host's or a guest's
    }
    return creature.scriptSequence && !creature.scriptHoldExpired;
}

bool playerInputAllowed(const CreatureFacts &creature, const WorldFacts &world) {
    return !scriptHoldsCreature(creature, world);
}

bool mayLeadParty(const CreatureFacts &creature, const WorldFacts &world) {
    if (world.coopClient) {
        return false;
    }
    return !(world.coopHost && creature.guestControlled);
}

bool steeringOverridesAction(const FrontAction &front) {
    return front.present && front.movementRestricted && !front.userAction && !front.knockedDown && !front.heldPose;
}

bool scriptSeesAsPC(const CreatureFacts &creature, const WorldFacts &world, ScriptKind caller) {
    if (creature.hostPlayer || creature.hostLeader) {
        return true;
    }
    // A companion a guest controls is that player's character to a door,
    // trigger or placeable script, as a companion the host switches to is.
    if (world.coopHost && creature.guestControlled) {
        return caller == ScriptKind::Door || caller == ScriptKind::Trigger || caller == ScriptKind::Placeable;
    }
    return false;
}

bool scriptSeesAsLeader(const CreatureFacts &creature, const WorldFacts &world, ScriptKind caller) {
    if (creature.hostLeader) {
        return true;
    }
    if (world.coopHost && creature.guestControlled && creature.partyMember) {
        return caller == ScriptKind::Door || caller == ScriptKind::Trigger || caller == ScriptKind::Placeable;
    }
    return false;
}

int scriptPartyIndex(int requested, int eventLeaderIndex) {
    if (eventLeaderIndex <= 0) {
        return requested;
    }
    if (requested == 0) {
        return eventLeaderIndex;
    }
    if (requested == eventLeaderIndex) {
        return 0;
    }
    return requested;
}

ScriptActor scriptActorFor(const CreatureFacts &creature, const WorldFacts &world, bool scriptNamesTag) {
    if (!world.coopHost || !creature.guestControlled || scriptNamesTag) {
        return ScriptActor::Self;
    }
    return ScriptActor::HostCharacter;
}

bool sceneRuleApplies(const CreatureFacts &creature, const WorldFacts &world) {
    // A dedicated host has no character of its own: its story driver starts scenes as before.
    return world.coopHost && world.hostOnlyScenes && !world.dedicated && creature.guestControlled;
}

bool sceneStartAllowed(const CreatureFacts &creature, const WorldFacts &world, bool scriptStartsScene, bool movedByScript) {
    if (!sceneRuleApplies(creature, world) || !scriptStartsScene) {
        return true;
    }
    // Only what a player starts by its own input waits for the host.
    return movedByScript;
}

// Fact gathering

WorldFacts observeWorld(Game &game) {
    WorldFacts world;
    if (auto *coop = game.coop()) {
        world.coopHost = coop->isHost();
        world.coopClient = coop->isClient();
        world.dedicated = coop->dedicated();
    }
    world.smartCompanions = game.options().game.smartCompanions;
    world.clicksBlocked = game.areClicksBlocked();
    return world;
}

CreatureFacts observe(Game &game, const Creature &creature) {
    CreatureFacts facts;
    auto &party = game.party();
    facts.dead = creature.isDead();
    facts.partyMember = party.isMember(creature);
    auto leader = party.getLeader();
    facts.hostLeader = leader && leader.get() == &creature;
    auto player = party.player();
    facts.hostPlayer = player && player.get() == &creature;
    auto *coop = game.coop();
    facts.guestControlled = coop && coop->isPlayerControlled(creature.id());
    facts.puppet = creature.isPuppet();
    facts.autoCombat = creature.autoCombat();
    return facts;
}

void observeScriptHold(Game &game, const Creature &creature, CreatureFacts &facts) {
    facts.scriptSequence = creature.hasScriptSequence();
    facts.scriptHoldExpired = false;
    if (!facts.scriptSequence) {
        return;
    }
    auto front = creature.getCurrentAction();
    if (front && front->isScriptAction() && front->startTick()) {
        auto limit = static_cast<uint64_t>(kCoopScriptHoldSeconds * static_cast<float>(game.tickRate()));
        facts.scriptHoldExpired = game.tickCount() > front->startTick() + limit;
    }
}

} // namespace coopcontrol

} // namespace game

} // namespace reone
