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

// Playtest 1 gameplay fixes (batch P1-Game, design note p1_game).

#include <gtest/gtest.h>

#include "reone/game/game.h"
#include "reone/game/mapexploration.h"
#include "reone/game/stringtokens.h"

using namespace reone;
using namespace reone::game;

namespace {

StringTokenRow row(int action, std::array<int, 4> strRefs = {-1, -1, -1, -1}, int def = -1) {
    StringTokenRow r;
    r.actionCode = action;
    r.strRefs = strRefs;
    r.defaultStrRef = def;
    return r;
}

StringTokens k1Tokens() {
    StringTokens tokens;
    tokens.add("FullName", row(2));
    tokens.add("PlayerName", row(23));
    tokens.add("FirstName", row(3));
    tokens.add("LastName", row(4));
    tokens.add("he/she", row(1, {32086, 32088, -1, -1}));
    tokens.add("Sir/Madam", row(1, {32105, 32107, -1, -1}));
    tokens.add("Level", row(10));
    tokens.add("Class", row(8));
    tokens.add("race", row(6));
    tokens.add("Good/Evil", row(14, {1, 2, 3, -1}));
    tokens.add("GameTime", row(16));
    tokens.add("abutton", row(24, {-1, -1, -1, -1}, 16));
    return tokens;
}

std::string tlk(int strRef) {
    switch (strRef) {
    case 32086:
        return "he";
    case 32088:
        return "she";
    case 32105:
        return "Sir";
    case 32107:
        return "Madam";
    case 1:
        return "good";
    case 2:
        return "neutral";
    case 3:
        return "evil";
    default:
        return " padded ";
    }
}

StringTokenSubject player() {
    StringTokenSubject s;
    s.present = true;
    s.fullName = "Revan Doe";
    s.firstName = "Revan";
    s.lastName = "Doe";
    s.className = "Scout";
    s.raceLower = "human";
    s.level = 3;
    s.gender = 1;
    s.goodEvil = 50;
    s.hour = 13;
    return s;
}

} // namespace

TEST(P1GameTokens, PlayerNamesResolve) {
    auto tokens = k1Tokens();
    EXPECT_EQ("I'm Revan Doe. Revan/Doe/Revan Doe",
              tokens.resolve("I'm <FullName>. <FirstName>/<LastName>/<PlayerName>", {}, player(), tlk));
}

TEST(P1GameTokens, GenderedWordsFollowGender) {
    auto tokens = k1Tokens();
    auto subject = player();
    EXPECT_EQ("Madam, she", tokens.resolve("<Sir/Madam>, <he/she>", {}, subject, tlk));
    subject.gender = 0;
    EXPECT_EQ("Sir, he", tokens.resolve("<Sir/Madam>, <he/she>", {}, subject, tlk));
}

TEST(P1GameTokens, StatsAlignmentAndTime) {
    auto tokens = k1Tokens();
    auto subject = player();
    EXPECT_EQ("3 Scout human neutral 1", tokens.resolve("<Level> <Class> <race> <Good/Evil> <GameTime>", {}, subject, tlk));
    subject.goodEvil = 60;
    EXPECT_EQ("good", tokens.resolve("<Good/Evil>", {}, subject, tlk));
    subject.goodEvil = 40;
    EXPECT_EQ("evil", tokens.resolve("<Good/Evil>", {}, subject, tlk));
}

TEST(P1GameTokens, CustomEscapesAndUnknown) {
    auto tokens = k1Tokens();
    std::map<int, std::string> custom {{0, "150"}, {32, "Carth"}};
    EXPECT_EQ("150 XP, Carth", tokens.resolve("<CUSTOM0> XP, <CUSTOM32>", custom, player(), tlk));
    EXPECT_EQ(std::string(kUnrecognizedToken), tokens.resolve("<CUSTOM7>", custom, player(), tlk));
    EXPECT_EQ(std::string(kUnrecognizedToken), tokens.resolve("<snore>", custom, player(), tlk));
    // Token names are case sensitive.
    EXPECT_EQ(std::string(kUnrecognizedToken), tokens.resolve("<fullname>", custom, player(), tlk));
    EXPECT_EQ("a < b", tokens.resolve("a << b", custom, player(), tlk));
    EXPECT_EQ("no tokens", tokens.resolve("no tokens", custom, player(), tlk));
    // Xbox button glyphs carry no PC text.
    EXPECT_EQ("Press  now", tokens.resolve("Press <abutton> now", custom, player(), tlk));
}

TEST(P1GameTokens, NoPlayerUsesDefaultString) {
    auto tokens = k1Tokens();
    tokens.add("FullName", row(2, {-1, -1, -1, -1}, 5));
    StringTokenSubject none;
    // The default string is trimmed of surrounding spaces.
    EXPECT_EQ("[padded]", tokens.resolve("[<FullName>]", {}, none, tlk));
}

TEST(P1GameAutosave, ModuleSaveRules) {
    EXPECT_EQ(AutosaveOnEnter::Force, parseAutosaveOnEnter(std::string("force")));
    EXPECT_EQ(AutosaveOnEnter::Yes, parseAutosaveOnEnter(std::string("YES")));
    EXPECT_EQ(AutosaveOnEnter::No, parseAutosaveOnEnter(std::string("no")));
    EXPECT_EQ(AutosaveOnEnter::Missing, parseAutosaveOnEnter(std::nullopt));

    // force always saves; no never does.
    EXPECT_TRUE(shouldAutosaveOnEnter(AutosaveOnEnter::Force, false, false));
    EXPECT_FALSE(shouldAutosaveOnEnter(AutosaveOnEnter::No, true, true));
    // yes waits 15 minutes and needs the AutoSave option.
    EXPECT_FALSE(shouldAutosaveOnEnter(AutosaveOnEnter::Yes, true, false));
    EXPECT_FALSE(shouldAutosaveOnEnter(AutosaveOnEnter::Yes, false, true));
    EXPECT_TRUE(shouldAutosaveOnEnter(AutosaveOnEnter::Yes, true, true));
    // A module with no row waits 15 minutes only.
    EXPECT_TRUE(shouldAutosaveOnEnter(AutosaveOnEnter::Missing, false, true));
    EXPECT_FALSE(shouldAutosaveOnEnter(AutosaveOnEnter::Missing, true, false));
}

TEST(P1GameMapFog, PartyRevealIsADiamondOfOne) {
    MapExploration grid;
    grid.reset(89, 44);
    grid.reveal(glm::vec2(0.5f, 0.5f), MapExploration::kPartyRevealRadius);
    EXPECT_EQ(5, grid.exploredCount());
    EXPECT_TRUE(grid.isExplored(44, 22));
    EXPECT_TRUE(grid.isExplored(45, 22));
    EXPECT_TRUE(grid.isExplored(44, 21));
    EXPECT_FALSE(grid.isExplored(45, 23));
}

TEST(P1GameMapFog, PackedGridRoundTrips) {
    MapExploration grid;
    grid.reset(89, 44);
    grid.reveal(glm::vec2(0.1f, 0.9f), 3);
    grid.reveal(glm::vec2(0.8f, 0.2f), 1);
    auto words = grid.packed();
    EXPECT_EQ(89u * 44u / 32u + 1u, words.size());

    MapExploration restored;
    restored.reset(89, 44);
    ASSERT_TRUE(restored.unpack(89, 44, words));
    EXPECT_EQ(grid.exploredCount(), restored.exploredCount());
    for (int row = 0; row < 44; ++row) {
        for (int column = 0; column < 89; ++column) {
            EXPECT_EQ(grid.isExplored(column, row), restored.isExplored(column, row));
        }
    }
    // A grid of another size is refused.
    EXPECT_FALSE(restored.unpack(40, 20, words));
}
