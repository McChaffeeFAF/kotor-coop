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

// The co-op guest's workbench and lab screen without its GUI (docs/guest_workbench.md):
// what it lists from the host's view, the stand-in id mapping, the requests Assemble,
// Create and Break Down send, and the rejection text.

#include <gtest/gtest.h>

#include <algorithm>

#include "reone/game/guestbenchscreen.h"

using namespace reone;
using namespace reone::game;
using namespace reone::net;

namespace {

BenchSlot slot(uint8_t type, int16_t installed, std::vector<uint32_t> candidates = {}, bool color = false) {
    BenchSlot result;
    result.type = type;
    result.installedRow = installed;
    result.candidates = std::move(candidates);
    result.color = color;
    return result;
}

BenchView workbench() {
    BenchView view;
    view.bench = 900;
    view.kind = BenchKind::Workbench;
    view.components = 12;
    view.chemicals = 3;
    BenchItem blaster;
    blaster.id = 100;
    blaster.resref = "g_w_blstrpstl001";
    blaster.category = 2;
    blaster.slots = {slot(1, -1, {200}), slot(2, 7), slot(3, -1)};
    BenchItem saber;
    saber.id = 101;
    saber.resref = "g_w_lghtsbr01";
    saber.category = 1;
    saber.equipped = true;
    saber.modelVariation = 3;
    saber.slots = {slot(0, -1, {201}, true), slot(4, -1, {202, 203}), slot(4, 9)};
    view.items = {blaster, saber};
    view.parts = {{200, "u_scope", 1, 0}, {201, "crystal_red", 1, 0}, {202, "crystal_power", 2, 0}, {203, "crystal_power", 1, 0},
                  {100, "dup_of_item", 1, 0}};
    view.breakdown = {{300, "g_i_medeqpmnt01", 4, 2, 1}, {301, "g_w_vbroswrd01", 1, 5, 0}, {100, "g_w_blstrpstl001", 1, 3, 0}};
    view.recipes = {{"g_i_medeqpmnt01", 1, 4, true, true}, {"g_w_vbroswrd01", 0, 20, false, true},
                    {"g_i_adrnaline001", 1, 30, true, false}};
    return view;
}

} // namespace

TEST(GuestBenchScreen, a_view_opens_the_screen_and_a_closed_one_or_reset_does_not) {
    GuestBenchScreen screen;
    EXPECT_FALSE(screen.open());
    uint32_t before = screen.revision();
    screen.setView(workbench());
    EXPECT_TRUE(screen.open());
    EXPECT_GT(screen.revision(), before);
    EXPECT_TRUE(screen.upgrades());
    EXPECT_TRUE(screen.crafting());

    auto closed = workbench();
    closed.open = false;
    screen.setView(closed);
    EXPECT_FALSE(screen.open());

    screen.setView(workbench());
    screen.mapStandIn(5, 100);
    screen.reset();
    EXPECT_FALSE(screen.open());
    EXPECT_EQ(0u, screen.hostId(5));
}

TEST(GuestBenchScreen, a_workbench_with_nothing_to_make_or_break_offers_no_crafting_but_a_lab_always_does) {
    GuestBenchScreen screen;
    auto view = workbench();
    view.recipes.clear();
    view.breakdown.clear();
    screen.setView(view);
    EXPECT_FALSE(screen.crafting());
    view.kind = BenchKind::Lab;
    screen.setView(view);
    EXPECT_TRUE(screen.crafting());
    EXPECT_FALSE(screen.upgrades());
}

TEST(GuestBenchScreen, items_are_listed_by_category_in_the_views_order) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    auto all = screen.items(0);
    ASSERT_EQ(2u, all.size());
    EXPECT_EQ(100u, all[0]->id);
    EXPECT_EQ(101u, all[1]->id);
    auto ranged = screen.items(2);
    ASSERT_EQ(1u, ranged.size());
    EXPECT_EQ(100u, ranged[0]->id);
    EXPECT_TRUE(screen.items(4).empty());
    EXPECT_EQ(nullptr, screen.item(555));
    EXPECT_TRUE(screen.item(101)->equipped);
}

TEST(GuestBenchScreen, stand_ins_hold_each_host_item_once_with_its_upgrades_and_colour) {
    GuestBenchScreen screen;
    auto view = workbench();
    view.items[1].upgrades = 0x40;
    screen.setView(view);
    auto standIns = screen.standIns();
    // Two items, four parts (the part repeating item 100 is dropped), two more breakdown items.
    ASSERT_EQ(8u, standIns.size());
    EXPECT_EQ(100u, standIns[0].host);
    EXPECT_EQ("g_w_blstrpstl001", standIns[0].resref);
    EXPECT_EQ(101u, standIns[1].host);
    EXPECT_EQ(0x40u, standIns[1].upgrades);
    EXPECT_EQ(3, standIns[1].modelVariation);
    EXPECT_EQ(202u, standIns[4].host);
    EXPECT_EQ(2, standIns[4].stack);
    EXPECT_EQ(300u, standIns[6].host);
    EXPECT_EQ(4, standIns[6].stack);
    EXPECT_EQ(301u, standIns[7].host);

    screen.mapStandIn(17, 300);
    EXPECT_EQ(300u, screen.hostId(17));
    EXPECT_EQ(17u, screen.localId(300));
    EXPECT_EQ(0u, screen.hostId(18));
    screen.clearStandIns();
    EXPECT_EQ(0u, screen.localId(300));
}

TEST(GuestBenchScreen, assemble_sends_nothing_for_an_unchanged_item) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    std::vector<StagedBenchSlot> staged {{-1, 0}, {7, 0}, {-1, 0}};
    EXPECT_TRUE(screen.assemble(100, staged).empty());
}

TEST(GuestBenchScreen, assemble_installs_a_staged_part_by_its_host_id) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    std::vector<StagedBenchSlot> staged {{12, 200}, {7, 0}, {-1, 0}};
    auto requests = screen.assemble(100, staged);
    ASSERT_EQ(1u, requests.size());
    EXPECT_EQ(BenchRequest::Kind::Upgrade, requests[0].kind);
    EXPECT_EQ(100u, requests[0].item);
    EXPECT_EQ(200u, requests[0].part);
}

TEST(GuestBenchScreen, assemble_removes_an_emptied_slot_by_index) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    std::vector<StagedBenchSlot> staged {{-1, 0}, {-1, 0}, {-1, 0}};
    auto requests = screen.assemble(100, staged);
    ASSERT_EQ(1u, requests.size());
    EXPECT_EQ(BenchRequest::Kind::Remove, requests[0].kind);
    EXPECT_EQ(100u, requests[0].item);
    EXPECT_EQ(1, requests[0].slot);
}

TEST(GuestBenchScreen, assemble_swaps_a_slot_by_removing_before_any_install) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    // Slot 0 gets the scope; slot 1's row 7 becomes row 8 (a part the guest staged).
    std::vector<StagedBenchSlot> staged {{12, 200}, {8, 204}, {-1, 0}};
    auto requests = screen.assemble(100, staged);
    ASSERT_EQ(3u, requests.size());
    EXPECT_EQ(BenchRequest::Kind::Remove, requests[0].kind);
    EXPECT_EQ(1, requests[0].slot);
    EXPECT_EQ(BenchRequest::Kind::Upgrade, requests[1].kind);
    EXPECT_EQ(200u, requests[1].part);
    EXPECT_EQ(BenchRequest::Kind::Upgrade, requests[2].kind);
    EXPECT_EQ(204u, requests[2].part);
}

TEST(GuestBenchScreen, a_saber_colour_is_swapped_by_installing_a_crystal_never_removed) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    std::vector<StagedBenchSlot> staged {{-1, 201}, {-1, 0}, {9, 0}};
    auto requests = screen.assemble(101, staged);
    ASSERT_EQ(1u, requests.size());
    EXPECT_EQ(BenchRequest::Kind::Upgrade, requests[0].kind);
    EXPECT_EQ(201u, requests[0].part);
    EXPECT_EQ(0, requests[0].slot);

    std::vector<StagedBenchSlot> unchanged {{-1, 0}, {-1, 0}, {9, 0}};
    EXPECT_TRUE(screen.assemble(101, unchanged).empty());
}

TEST(GuestBenchScreen, assemble_ignores_unknown_items_extra_slots_and_labs) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    EXPECT_TRUE(screen.assemble(555, {{12, 200}}).empty());
    // Only the view's three slots count.
    auto requests = screen.assemble(100, {{-1, 0}, {7, 0}, {-1, 0}, {5, 999}});
    EXPECT_TRUE(requests.empty());
    // A staged row without a part (nothing the host could install) sends nothing for it.
    EXPECT_TRUE(screen.assemble(100, {{12, 0}, {7, 0}, {-1, 0}}).empty());
    auto lab = workbench();
    lab.kind = BenchKind::Lab;
    screen.setView(lab);
    EXPECT_TRUE(screen.assemble(100, {{12, 200}, {7, 0}, {-1, 0}}).empty());
}

TEST(GuestBenchScreen, a_lab_lists_only_chemical_recipes_and_breakdowns) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    EXPECT_EQ(3u, screen.recipes().size());
    EXPECT_EQ(3u, screen.breakdowns().size());
    auto lab = workbench();
    lab.kind = BenchKind::Lab;
    screen.setView(lab);
    auto recipes = screen.recipes();
    ASSERT_EQ(2u, recipes.size());
    EXPECT_EQ("g_i_medeqpmnt01", recipes[0].resref);
    EXPECT_EQ("g_i_adrnaline001", recipes[1].resref);
    auto breakdowns = screen.breakdowns();
    ASSERT_EQ(1u, breakdowns.size());
    EXPECT_EQ(300u, breakdowns[0].id);
    EXPECT_TRUE(screen.items(0).empty());
}

TEST(GuestBenchScreen, craft_and_break_down_requests_carry_the_recipe_and_the_host_item) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    const BenchRecipe *recipe = screen.recipe("G_I_MEDEQPMNT01", 1);
    ASSERT_NE(nullptr, recipe);
    EXPECT_EQ(nullptr, screen.recipe("g_i_medeqpmnt01", 0)) << "material is part of the recipe";
    auto craft = screen.craft(*recipe);
    EXPECT_EQ(BenchRequest::Kind::Craft, craft.kind);
    EXPECT_EQ("g_i_medeqpmnt01", craft.resref);
    EXPECT_EQ(1, craft.material);
    auto broken = screen.breakDown(301);
    EXPECT_EQ(BenchRequest::Kind::Break, broken.kind);
    EXPECT_EQ(301u, broken.item);
    ASSERT_NE(nullptr, screen.breakdown(300));
    EXPECT_EQ(2, screen.breakdown(300)->yield);
    EXPECT_EQ(nullptr, screen.breakdown(999));
}

TEST(GuestBenchScreen, only_a_tracked_requests_rejection_is_shown_with_a_readable_reason) {
    GuestBenchScreen screen;
    screen.setView(workbench());
    screen.reject(41, CommandReject::NotEnoughMaterial);
    EXPECT_TRUE(screen.rejection().empty()) << "not a bench request";
    screen.track(42);
    uint32_t before = screen.revision();
    screen.reject(42, CommandReject::NotEnoughMaterial);
    EXPECT_EQ("Not enough material", screen.rejection());
    EXPECT_GT(screen.revision(), before);
    screen.clearRejection();
    EXPECT_TRUE(screen.rejection().empty());
    screen.track(0);
    EXPECT_FALSE(screen.tracks(0));
    // The oldest tracked requests are forgotten.
    for (uint32_t seq = 100; seq < 140; ++seq) {
        screen.track(seq);
    }
    EXPECT_FALSE(screen.tracks(42));
    EXPECT_TRUE(screen.tracks(139));
}

TEST(GuestBenchScreen, every_bench_rejection_has_its_own_text) {
    EXPECT_EQ("", GuestBenchScreen::rejectionText(CommandReject::None));
    std::vector<std::string> texts;
    for (auto reason : {CommandReject::BenchClosed, CommandReject::UpgradeIncompatible, CommandReject::NotEnoughMaterial,
                        CommandReject::SkillTooLow, CommandReject::NotBreakable, CommandReject::ItemUnavailable}) {
        auto text = GuestBenchScreen::rejectionText(reason);
        EXPECT_FALSE(text.empty());
        EXPECT_EQ(texts.end(), std::find(texts.begin(), texts.end(), text));
        texts.push_back(text);
    }
    EXPECT_NE(std::string::npos, GuestBenchScreen::rejectionText(CommandReject::HostLeadsConversation).find("refused"));
}
