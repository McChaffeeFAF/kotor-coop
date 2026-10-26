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

// The co-op control policy: a table of creature states against every query.

#include <vector>

#include <gtest/gtest.h>

#include "reone/game/coopcontrol_policy.h"

using namespace reone::game;
using namespace reone::game::coopcontrol;

namespace {

WorldFacts host() {
    WorldFacts w;
    w.coopHost = true;
    w.hostOnlyScenes = true;
    w.smartCompanions = true;
    return w;
}

WorldFacts singlePlayer() {
    WorldFacts w;
    w.smartCompanions = true;
    return w;
}

WorldFacts client() {
    WorldFacts w;
    w.coopClient = true;
    w.smartCompanions = true;
    return w;
}

CreatureFacts aiCompanion() {
    CreatureFacts c;
    c.partyMember = true;
    c.genericRoundScript = true;
    return c;
}

CreatureFacts hostLeader() {
    CreatureFacts c = aiCompanion();
    c.hostLeader = true;
    c.hostPlayer = true;
    return c;
}

CreatureFacts guestCompanion() {
    CreatureFacts c = aiCompanion();
    c.guestControlled = true;
    c.guestClient = 1;
    return c;
}

CreatureFacts hostile() {
    CreatureFacts c;
    c.genericRoundScript = true;
    return c;
}

struct Row {
    const char *name;
    CreatureFacts creature;
    WorldFacts world;
    Controller controller;
    bool brain;
    bool roundScript;
    bool autoInCombat;
    bool autoOutOfCombat;
    bool dropsFollow;
    bool reactions;
    bool follow;
    bool idleEngage;
    bool input;
    bool lead;
    bool pcToDoor;
    bool pcToCreature;
    ScriptActor actorUnnamed;
};

std::vector<Row> table() {
    std::vector<Row> rows;
    auto add = [&](Row row) { rows.push_back(row); };

    add({"ai companion", aiCompanion(), host(), Controller::AI,
         true, true, true, true, false, true, true, true, true, true, false, false, ScriptActor::Self});

    add({"host leader", hostLeader(), host(), Controller::HostPlayer,
         false, false, true, false, false, true, false, false, true, true, true, true, ScriptActor::Self});

    CreatureFacts autoLeader = hostLeader();
    autoLeader.autoCombat = true;
    add({"host leader on auto-combat", autoLeader, host(), Controller::HostPlayer,
         true, false, true, false, false, true, false, false, true, true, true, true, ScriptActor::Self});

    // The host switched to a companion: the PC is still a PC, but neither leads nor is a hand.
    CreatureFacts pcNotLeading = aiCompanion();
    pcNotLeading.hostPlayer = true;
    add({"host pc, not leading", pcNotLeading, host(), Controller::AI,
         true, true, true, true, false, true, true, true, true, true, true, true, ScriptActor::Self});

    add({"guest companion", guestCompanion(), host(), Controller::Guest,
         false, false, true, false, true, false, false, true, true, false, true, false, ScriptActor::HostCharacter});

    CreatureFacts autoGuest = guestCompanion();
    autoGuest.autoCombat = true;
    add({"guest on auto-combat", autoGuest, host(), Controller::Guest,
         false, false, true, false, true, false, false, true, true, false, true, false, ScriptActor::HostCharacter});

    CreatureFacts heldGuest = guestCompanion();
    heldGuest.scriptSequence = true;
    add({"guest held by a script", heldGuest, host(), Controller::Script,
         false, false, true, false, true, false, false, true, false, false, true, false, ScriptActor::HostCharacter});

    CreatureFacts expiredGuest = heldGuest;
    expiredGuest.scriptHoldExpired = true;
    add({"guest, script hold expired", expiredGuest, host(), Controller::Guest,
         false, false, true, false, true, false, false, true, true, false, true, false, ScriptActor::HostCharacter});

    WorldFacts noClicks = host();
    noClicks.clicksBlocked = true;
    add({"guest under NoClicksFor", guestCompanion(), noClicks, Controller::Script,
         false, false, true, false, true, false, false, true, false, false, true, false, ScriptActor::HostCharacter});

    add({"ai companion under NoClicksFor", aiCompanion(), noClicks, Controller::AI,
         true, true, true, true, false, true, true, true, false, true, false, false, ScriptActor::Self});

    CreatureFacts deadCompanion = aiCompanion();
    deadCompanion.dead = true;
    add({"dead companion", deadCompanion, host(), Controller::AI,
         false, true, true, true, false, true, false, true, true, true, false, false, ScriptActor::Self});

    add({"hostile", hostile(), host(), Controller::AI,
         false, true, true, true, false, true, false, false, true, true, false, false, ScriptActor::Self});

    CreatureFacts bossCompanion = aiCompanion();
    bossCompanion.genericRoundScript = false;
    add({"companion on its own round script", bossCompanion, host(), Controller::AI,
         false, true, true, true, false, true, true, true, true, true, false, false, ScriptActor::Self});

    CreatureFacts puppet;
    puppet.puppet = true;
    add({"puppet", puppet, host(), Controller::Puppet,
         false, true, true, true, false, true, false, false, true, true, false, false, ScriptActor::Self});

    WorldFacts noBrain = host();
    noBrain.smartCompanions = false;
    add({"ai companion, smarter companions off", aiCompanion(), noBrain, Controller::AI,
         false, true, true, true, false, true, true, true, true, true, false, false, ScriptActor::Self});

    add({"single player leader", hostLeader(), singlePlayer(), Controller::HostPlayer,
         false, false, true, false, false, true, false, false, true, true, true, true, ScriptActor::Self});

    // A client never leads on the host's party; its own view has no guest facts.
    add({"client's party member", aiCompanion(), client(), Controller::AI,
         true, true, true, true, false, true, true, true, true, false, false, false, ScriptActor::Self});

    return rows;
}

} // namespace

TEST(CoopControlPolicy, table) {
    for (const auto &row : table()) {
        SCOPED_TRACE(row.name);
        const auto &c = row.creature;
        const auto &w = row.world;
        EXPECT_EQ(row.controller, controllerOf(c, w)) << controllerName(controllerOf(c, w));
        EXPECT_EQ(row.brain, mayBrainAct(c, w)) << "mayBrainAct";
        EXPECT_EQ(row.roundScript, mayRunRoundScript(c)) << "mayRunRoundScript";
        EXPECT_EQ(row.autoInCombat, mayAutoCombatAct(c, true)) << "mayAutoCombatAct in combat";
        EXPECT_EQ(row.autoOutOfCombat, mayAutoCombatAct(c, false)) << "mayAutoCombatAct out of combat";
        EXPECT_EQ(row.dropsFollow, autoRoundDropsFollow(c)) << "autoRoundDropsFollow";
        EXPECT_EQ(row.reactions, mayRunReactionScripts(c)) << "mayRunReactionScripts";
        EXPECT_EQ(row.follow, mayFollowLeader(c)) << "mayFollowLeader";
        EXPECT_EQ(row.idleEngage, mayIdleEngage(c)) << "mayIdleEngage";
        EXPECT_EQ(row.input, playerInputAllowed(c, w)) << "playerInputAllowed";
        EXPECT_EQ(!row.input, scriptHoldsCreature(c, w)) << "scriptHoldsCreature";
        EXPECT_EQ(row.lead, mayLeadParty(c, w)) << "mayLeadParty";
        EXPECT_EQ(row.pcToDoor, scriptSeesAsPC(c, w, ScriptKind::Door)) << "GetIsPC from a door";
        EXPECT_EQ(row.pcToDoor, scriptSeesAsPC(c, w, ScriptKind::Trigger)) << "GetIsPC from a trigger";
        EXPECT_EQ(row.pcToDoor, scriptSeesAsPC(c, w, ScriptKind::Placeable)) << "GetIsPC from a placeable";
        EXPECT_EQ(row.pcToCreature, scriptSeesAsPC(c, w, ScriptKind::Creature)) << "GetIsPC from a creature";
        EXPECT_EQ(row.actorUnnamed, scriptActorFor(c, w, false)) << "scriptActorFor, tag not named";
        EXPECT_EQ(ScriptActor::Self, scriptActorFor(c, w, true)) << "scriptActorFor, tag named";
        EXPECT_EQ(c.guestControlled, idleEngageAsksRoundScript(c) == false) << "idleEngageAsksRoundScript";
    }
}

TEST(CoopControlPolicy, scenes) {
    auto w = host();
    auto guest = guestCompanion();
    EXPECT_FALSE(sceneStartAllowed(guest, w, true, false)) << "walked in by its player: waits for the host";
    EXPECT_TRUE(sceneStartAllowed(guest, w, true, true)) << "a script moved it there: the scene runs";
    EXPECT_TRUE(sceneStartAllowed(guest, w, false, false)) << "no scene in the script";
    EXPECT_TRUE(sceneStartAllowed(hostLeader(), w, true, false)) << "the host's own character";
    EXPECT_TRUE(sceneStartAllowed(aiCompanion(), w, true, false)) << "an AI companion";

    auto off = w;
    off.hostOnlyScenes = false;
    EXPECT_FALSE(sceneRuleApplies(guest, off));
    EXPECT_TRUE(sceneStartAllowed(guest, off, true, false)) << "option off";

    auto dedicated = w;
    dedicated.dedicated = true;
    EXPECT_TRUE(sceneStartAllowed(guest, dedicated, true, false)) << "a dedicated host has no character";

    EXPECT_TRUE(sceneStartAllowed(guest, singlePlayer(), true, false));
    EXPECT_TRUE(sceneRuleApplies(guest, w));
}

TEST(CoopControlPolicy, steering) {
    FrontAction autoSwing;
    autoSwing.present = true;
    autoSwing.movementRestricted = true;
    EXPECT_TRUE(steeringOverridesAction(autoSwing)) << "an attack the player did not order";

    auto ordered = autoSwing;
    ordered.userAction = true;
    EXPECT_FALSE(steeringOverridesAction(ordered)) << "the player's own order runs";

    auto knocked = autoSwing;
    knocked.knockedDown = true;
    EXPECT_FALSE(steeringOverridesAction(knocked));

    auto pose = autoSwing;
    pose.heldPose = true;
    EXPECT_FALSE(steeringOverridesAction(pose));

    auto free = autoSwing;
    free.movementRestricted = false;
    EXPECT_FALSE(steeringOverridesAction(free)) << "nothing holds it: it just walks";

    EXPECT_FALSE(steeringOverridesAction(FrontAction())) << "an empty queue";
}

TEST(CoopControlPolicy, deterministic) {
    // Pure functions: the same facts give the same answers, every time.
    auto rows = table();
    for (int pass = 0; pass < 3; ++pass) {
        for (const auto &row : rows) {
            EXPECT_EQ(controllerOf(row.creature, row.world), controllerOf(row.creature, row.world));
            EXPECT_EQ(mayBrainAct(row.creature, row.world), row.brain);
        }
    }
}

/*
 * Fixed bug: the Endar Spire's first-room door (end_m01aa, end_door01).
 *
 * Seen (development notes (not included): host log, save "000003 - Game2"):
 * a guest controls end_trask; it clicks the door twice, both open_door
 * commands are accepted, Trask walks to the door and nothing happens.
 *
 * Read with the policy: after Trask's talk the door is locked and Trask asks
 * to be switched to. In single player the player switches to Trask (Tab), so
 * Trask is the party leader when he reaches the door. The door's
 * OnFailToOpen opens it only when party member 0 is end_trask; it does not
 * look at the clicking object. Everything up to that point works:
 *   - playerInputAllowed(Trask) is true, so the command is accepted;
 *   - scriptActorFor(Trask, names "end_trask") is Self, so the clicker is Trask;
 *   - but scriptSeesAsLeader(Trask, Door) was false: party member 0 was still
 *     the host's PC, so the script takes its other branch (the door's
 *     "access code required" line to the host's PC) and the door stays shut.
 *
 * The fix this test pins: to a door, trigger or placeable script fired by a
 * guest's character, that character reads as the leader too (as it already
 * reads as a PC), and GetPartyMemberByIndex(0) consults scriptSeesAsLeader
 * with the event's clicker/entering object (scriptPartyIndex swaps 0 and its index).
 */
TEST(CoopControlPolicy, endarSpireDoorGuestTraskReadsAsLeader) {
    auto trask = guestCompanion();
    auto w = host();
    ASSERT_TRUE(playerInputAllowed(trask, w));
    ASSERT_EQ(ScriptActor::Self, scriptActorFor(trask, w, true));
    EXPECT_TRUE(scriptSeesAsLeader(trask, w, ScriptKind::Door));
    EXPECT_FALSE(scriptSeesAsLeader(trask, w, ScriptKind::Creature)) << "creature scripts keep one leader";
}

TEST(CoopControlPolicy, leaderToScripts) {
    auto w = host();
    EXPECT_TRUE(scriptSeesAsLeader(hostLeader(), w, ScriptKind::Door));
    EXPECT_TRUE(scriptSeesAsLeader(hostLeader(), w, ScriptKind::Creature));
    for (auto kind : {ScriptKind::Door, ScriptKind::Trigger, ScriptKind::Placeable}) {
        EXPECT_TRUE(scriptSeesAsLeader(guestCompanion(), w, kind));
    }
    EXPECT_FALSE(scriptSeesAsLeader(guestCompanion(), w, ScriptKind::Creature));
    EXPECT_FALSE(scriptSeesAsLeader(guestCompanion(), w, ScriptKind::Other));
    EXPECT_FALSE(scriptSeesAsLeader(aiCompanion(), w, ScriptKind::Door)) << "an AI companion never leads";

    auto standIn = guestCompanion();
    standIn.partyMember = false;
    EXPECT_FALSE(scriptSeesAsLeader(standIn, w, ScriptKind::Door)) << "a stand-in has no party index";

    EXPECT_FALSE(scriptSeesAsLeader(guestCompanion(), singlePlayer(), ScriptKind::Door));
    EXPECT_FALSE(scriptSeesAsLeader(guestCompanion(), client(), ScriptKind::Door));
}

TEST(CoopControlPolicy, scriptPartyIndexSwapsTheEventLeaderWithZero) {
    // [PC, Trask]: Trask (1) clicked.
    EXPECT_EQ(1, scriptPartyIndex(0, 1));
    EXPECT_EQ(0, scriptPartyIndex(1, 1)) << "the host's PC is not lost";
    EXPECT_EQ(2, scriptPartyIndex(2, 1));
    // [PC, A, Guest]: the guest (2) clicked; A stays at 1.
    EXPECT_EQ(2, scriptPartyIndex(0, 2));
    EXPECT_EQ(1, scriptPartyIndex(1, 2));
    EXPECT_EQ(0, scriptPartyIndex(2, 2));
    // No event leader, or the leader itself: unchanged.
    for (int i = -1; i < 4; ++i) {
        EXPECT_EQ(i, scriptPartyIndex(i, -1));
        EXPECT_EQ(i, scriptPartyIndex(i, 0));
    }
    // A permutation: every member read exactly once (no one counted twice).
    for (int leader = 1; leader < 3; ++leader) {
        std::vector<int> seen(3, 0);
        for (int i = 0; i < 3; ++i) {
            ++seen[scriptPartyIndex(i, leader)];
        }
        EXPECT_EQ(std::vector<int>({1, 1, 1}), seen);
    }
}
