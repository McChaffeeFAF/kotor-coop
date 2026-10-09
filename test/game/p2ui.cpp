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

// Playtest 1b UI fixes (batch P2-UI, design note p2_ui).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"

#include "reone/game/game.h"
#include "reone/game/mapexploration.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/statussummary.h"
#include "reone/gui/control.h"
#include "reone/net/protocol.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class P2UITest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Creature> addPlayer() {
        auto pc = addCreature("player");
        _game->party().addMember(kNpcPlayer, pc);
        _game->party().setPlayer(pc);
        pc->adjustGoodEvil(50);
        return pc;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(args, execution);
    }

    void adjust(const std::shared_ptr<Creature> &creature, int alignment, int shift) {
        call("AdjustAlignment", {Variable::ofObject(creature->id()), Variable::ofInt(alignment), Variable::ofInt(shift)});
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

constexpr int kLightSide = 2;
constexpr int kDarkSide = 3;
constexpr int kNeutral = 1;

} // namespace

// --- Alignment feedback (item 20) -----------------------------------------------

TEST(P2UIStatusSummary, light_and_dark_gains_in_one_batch_net_out) {
    StatusSummaryAccumulator summary;
    summary.submitAlignment(5);
    summary.submitAlignment(-2);
    EXPECT_TRUE(summary.pending().entry(StatusSummaryCategory::LightSideShift).active);
    EXPECT_FALSE(summary.pending().entry(StatusSummaryCategory::DarkSideShift).active);

    summary.submitAlignment(-6);
    EXPECT_FALSE(summary.pending().entry(StatusSummaryCategory::LightSideShift).active);
    EXPECT_TRUE(summary.pending().entry(StatusSummaryCategory::DarkSideShift).active);

    // Equal gains show neither, and leave nothing to present.
    summary.submitAlignment(3);
    EXPECT_FALSE(summary.pending().entry(StatusSummaryCategory::LightSideShift).active);
    EXPECT_FALSE(summary.pending().entry(StatusSummaryCategory::DarkSideShift).active);
    EXPECT_TRUE(summary.pending().empty());
    EXPECT_FALSE(summary.beginPresentation());
}

TEST(P2UIStatusSummary, a_new_batch_starts_its_alignment_totals_afresh) {
    StatusSummaryAccumulator summary;
    summary.submitAlignment(-4);
    ASSERT_TRUE(summary.beginPresentation());
    EXPECT_TRUE(summary.displayed()->entry(StatusSummaryCategory::DarkSideShift).active);
    summary.submitAlignment(1);
    EXPECT_TRUE(summary.pending().entry(StatusSummaryCategory::LightSideShift).active);
    EXPECT_FALSE(summary.pending().entry(StatusSummaryCategory::DarkSideShift).active);
}

TEST_F(P2UITest, adjust_alignment_on_the_player_feeds_the_status_summary) {
    auto pc = addPlayer();
    adjust(pc, kLightSide, 4);
    EXPECT_TRUE(_game->statusSummary().pending().entry(StatusSummaryCategory::LightSideShift).active);
    adjust(pc, kDarkSide, 10);
    EXPECT_TRUE(_game->statusSummary().pending().entry(StatusSummaryCategory::DarkSideShift).active);
    EXPECT_FALSE(_game->statusSummary().pending().entry(StatusSummaryCategory::LightSideShift).active);
}

TEST_F(P2UITest, neutral_shift_counts_towards_the_side_it_moves) {
    auto pc = addPlayer();
    pc->adjustGoodEvil(30); // 80
    adjust(pc, kNeutral, 10);
    EXPECT_EQ(70, pc->goodEvil());
    EXPECT_TRUE(_game->statusSummary().pending().entry(StatusSummaryCategory::DarkSideShift).active);
}

TEST_F(P2UITest, other_creatures_shifts_and_no_ops_show_nothing) {
    addPlayer();
    auto npc = addCreature("carth");
    npc->adjustGoodEvil(50);
    adjust(npc, kLightSide, 5);
    EXPECT_EQ(55, npc->goodEvil());
    EXPECT_TRUE(_game->statusSummary().pending().empty());

    auto pc = _game->party().player();
    adjust(pc, 0, 30); // ALIGNMENT_ALL
    adjust(pc, kNeutral, 10); // already at 50
    EXPECT_TRUE(_game->statusSummary().pending().empty());
}

// --- XP rows and floating XP (items 7, 8) ------------------------------------------

TEST_F(P2UITest, stealth_xp_reaches_the_summary_with_its_amount) {
    addPlayer();
    int given = _game->party().awardXP(37, XPSource::Stealth);
    EXPECT_EQ(37, given);
    const auto &entry = _game->statusSummary().pending().entry(StatusSummaryCategory::StealthXP);
    EXPECT_TRUE(entry.active);
    EXPECT_EQ(37, entry.amount);
}

TEST_F(P2UITest, award_xp_returns_what_the_pool_got) {
    addPlayer();
    EXPECT_EQ(120, _game->party().awardXP(120, XPSource::Combat));
    EXPECT_EQ(120, _game->party().xp());
    EXPECT_EQ(0, _game->party().awardXP(0, XPSource::Combat));
}

TEST_F(P2UITest, custom_token_takes_the_row_value) {
    EXPECT_EQ("Stealth XP Received: 37", _game->substituteCustomToken("Stealth XP Received: <CUSTOM0>", 0, "37"));
    EXPECT_EQ("Credits Received: 250", _game->substituteCustomToken("Credits Received: <CUSTOM0>", 0, "250"));
}

// --- Upgrade bench text (item 12) ---------------------------------------------------

TEST(P2UIText, bottom_centre_is_the_authored_alignment_value) {
    // K1 upgradesel authors Lightsaber/Ranged with 34 (centre | bottom).
    EXPECT_EQ(34, static_cast<int>(gui::Control::TextAlign::CenterBottom));
    EXPECT_EQ(10, static_cast<int>(gui::Control::TextAlign::CenterTop));
}

// --- Map markers (item 16) -------------------------------------------------------------

TEST(P2UIMapMarkers, party_and_player_flags_do_not_collide_with_the_others) {
    using namespace reone::net;
    uint8_t all = kFlagVisible | kFlagSelectable | kFlagInCombat | kFlagInConversation;
    EXPECT_EQ(0, all & kFlagPartyMember);
    EXPECT_EQ(0, all & kFlagPlayer);
    EXPECT_NE(kFlagPartyMember, kFlagPlayer);
}
