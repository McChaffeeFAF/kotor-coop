/* Copyright (c) 2026 The reone project contributors */

// Stealth Mode rules (design note stealth). The game-side behaviour
// (perception, the request path, co-op, saving) is tested in object.cpp's
// StealthMode suite, next to the visibility fixture it shares.

#include <gtest/gtest.h>

#include "reone/game/stealth.h"
#include "reone/net/protocol.h"

using namespace reone;
using namespace reone::game;

TEST(StealthRules, a_stealth_unit_is_known_by_its_base_item_label_or_item_type) {
    EXPECT_TRUE(stealth::isStealthUnitBaseItem("Stealth_Unit", 44));
    EXPECT_TRUE(stealth::isStealthUnitBaseItem("stealth_unit", 0)); // label wins, any case
    EXPECT_FALSE(stealth::isStealthUnitBaseItem("Belt", 21));        // a plain belt (row 47)
    EXPECT_FALSE(stealth::isStealthUnitBaseItem("Belt", 44));        // the label decides when present
    EXPECT_TRUE(stealth::isStealthUnitBaseItem("", 44));             // no label: the item type
    EXPECT_FALSE(stealth::isStealthUnitBaseItem("", 21));
}

TEST(StealthRules, entry_needs_the_unit_and_paid_points_and_no_combat) {
    stealth::EntryInputs ok;
    ok.stealthUnitEquipped = true;
    ok.stealthRanks = 1;
    EXPECT_EQ(stealth::Refusal::None, stealth::entryRefusal(ok));

    auto noUnit = ok;
    noUnit.stealthUnitEquipped = false;
    EXPECT_EQ(stealth::Refusal::Requirements, stealth::entryRefusal(noUnit));

    auto noRanks = ok;
    noRanks.stealthRanks = 0;
    EXPECT_EQ(stealth::Refusal::Requirements, stealth::entryRefusal(noRanks));

    auto fighting = ok;
    fighting.inCombat = true;
    EXPECT_EQ(stealth::Refusal::InCombat, stealth::entryRefusal(fighting));
    fighting.stealthUnitEquipped = false; // combat is named first
    EXPECT_EQ(stealth::Refusal::InCombat, stealth::entryRefusal(fighting));

    auto dead = ok;
    dead.alive = false;
    EXPECT_EQ(stealth::Refusal::Unavailable, stealth::entryRefusal(dead));
}

TEST(StealthRules, the_leader_with_followers_needs_solo_mode) {
    stealth::EntryInputs inputs;
    inputs.stealthUnitEquipped = true;
    inputs.stealthRanks = 4;
    inputs.leaderWithFollowers = true;
    EXPECT_EQ(stealth::Refusal::NeedSolo, stealth::entryRefusal(inputs));
    inputs.soloMode = true;
    EXPECT_EQ(stealth::Refusal::None, stealth::entryRefusal(inputs));
    inputs.soloMode = false;
    inputs.leaderWithFollowers = false; // alone, or a co-op guest's character
    EXPECT_EQ(stealth::Refusal::None, stealth::entryRefusal(inputs));
    // The unit is asked for before solo mode is offered.
    inputs.leaderWithFollowers = true;
    inputs.stealthUnitEquipped = false;
    EXPECT_EQ(stealth::Refusal::Requirements, stealth::entryRefusal(inputs));
}

TEST(StealthRules, refusals_name_the_game_s_own_lines) {
    EXPECT_EQ(1452, stealth::refusalStrRef(stealth::Refusal::InCombat));
    EXPECT_EQ(38062, stealth::refusalStrRef(stealth::Refusal::Requirements));
    EXPECT_EQ(37890, stealth::refusalStrRef(stealth::Refusal::NeedSolo));
    EXPECT_EQ(-1, stealth::refusalStrRef(stealth::Refusal::None));
    EXPECT_EQ(-1, stealth::refusalStrRef(stealth::Refusal::Unavailable));
}

TEST(StealthRules, running_is_disabled_unless_stealth_run) {
    EXPECT_TRUE(stealth::mayRun(false, false));
    EXPECT_FALSE(stealth::mayRun(true, false));
    EXPECT_TRUE(stealth::mayRun(true, true)); // K2 FEAT_STEALTH_RUN
}

TEST(StealthRules, awareness_modifier_counts_distance_running_rear_and_combat) {
    stealth::AwarenessInputs inputs;
    inputs.awarenessSkill = 6;
    EXPECT_EQ(6, stealth::awarenessModifier(inputs));
    inputs.distance = 2.9f;
    EXPECT_EQ(6, stealth::awarenessModifier(inputs));
    inputs.distance = 9.0f; // -1 per 3 m
    EXPECT_EQ(3, stealth::awarenessModifier(inputs));
    inputs.observerRunning = true; // TLK 250: -5
    EXPECT_EQ(-2, stealth::awarenessModifier(inputs));
    inputs.targetBehind = true;
    EXPECT_EQ(-7, stealth::awarenessModifier(inputs));
    inputs.observerInCombat = true;
    EXPECT_EQ(-12, stealth::awarenessModifier(inputs));
}

TEST(StealthRules, behind_is_the_rear_half_circle_of_the_facing) {
    // Facing 0 looks along +Y.
    EXPECT_FALSE(stealth::isBehind(0.0f, 0.0f, 5.0f));
    EXPECT_TRUE(stealth::isBehind(0.0f, 0.0f, -5.0f));
    EXPECT_TRUE(stealth::isBehind(0.0f, 1.0f, -5.0f));
    // A quarter turn left (pi/2) looks along -X.
    EXPECT_FALSE(stealth::isBehind(1.5707964f, -4.0f, 0.0f));
    EXPECT_TRUE(stealth::isBehind(1.5707964f, 4.0f, 0.0f));
}

TEST(StealthRules, detection_is_awareness_beating_stealth_and_a_tie_stays_hidden) {
    auto beaten = stealth::check(15, 3, 10, 6); // 18 vs 16
    EXPECT_TRUE(beaten.detected);
    EXPECT_EQ(18, beaten.awarenessTotal);
    EXPECT_EQ(16, beaten.stealthTotal);
    auto tie = stealth::check(13, 3, 10, 6); // 16 vs 16
    EXPECT_FALSE(tie.detected);
    auto highRoll = stealth::check(20, -10, 1, 4); // 10 vs 5: a poor observer can still roll well
    EXPECT_TRUE(highRoll.detected);
    auto missed = stealth::check(2, 1, 1, 4); // 3 vs 5
    EXPECT_FALSE(missed.detected);
}

TEST(StealthRules, a_detection_lowers_the_pool_by_the_decrement_while_enabled) {
    EXPECT_EQ(0, stealth::poolAfterDetection(300, 300, true));     // the Endar Spire: one detection takes it all
    EXPECT_EQ(375, stealth::poolAfterDetection(500, 125, true));   // Taris: a quarter each
    EXPECT_EQ(0, stealth::poolAfterDetection(100, 125, true));     // never below zero
    EXPECT_EQ(500, stealth::poolAfterDetection(500, 125, false));  // disabled: untouched
    EXPECT_EQ(500, stealth::poolAfterDetection(500, -5, true));    // a negative decrement does nothing
}

TEST(StealthRules, protocol_carries_the_stealth_command_flag_and_rejects) {
    EXPECT_GE(net::kProtocolVersion, 9) << "older peers reject the Stealth command and flag";
    EXPECT_EQ(33, static_cast<int>(net::CommandType::Stealth));
    EXPECT_STREQ("stealth", net::commandTypeName(net::CommandType::Stealth));
    EXPECT_STREQ("stealth_unavailable", net::commandRejectName(net::CommandReject::StealthUnavailable));
    EXPECT_STREQ("stealth_in_combat", net::commandRejectName(net::CommandReject::StealthInCombat));
    EXPECT_EQ(0x40, static_cast<int>(net::kFlagStealthed));
}
