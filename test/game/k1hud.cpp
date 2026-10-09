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

/**
 * K1 HUD parity (design note k1hud_parity):
 * - the K1 layout gains the weapon-set swap, the set indicator and a K1-art
 *   auto-combat toggle, placed left of the quick bar; the K2 layout is
 *   unchanged;
 * - every new control uses K1 art only;
 * - a queued weapon switch shows K1 art in the K1 combat queue;
 * - the difficulty tables: diffsettings at the game's fixed "normal" column
 *   changes nothing, and its rules behave as K2's at the other columns.
 */

#include "../fixtures/engine.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/game.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/k1hudcontrols.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/party.h"
#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/gui.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace reone::game {

class K1HUDTestAccess {
public:
    static std::shared_ptr<IGUI> gui(HUD &hud) { return hud._gui; }
    static K1HUDControls *k1Controls(HUD &hud) { return hud._k1Controls.get(); }
    static void refreshQueue(HUD &hud) { hud.refreshActionQueueItems(); }
};

} // namespace reone::game

namespace {

ControlType controlTypeFor(const std::string &tag) {
    if (tag == "BTN_SWAPWEAPONS" || tag.rfind("TB_", 0) == 0) {
        return ControlType::ToggleButton;
    }
    bool actionIcon = tag.rfind("LBL_ACTION", 0) == 0 && tag.rfind("LBL_ACTIONDESC", 0) != 0;
    if (tag.rfind("BTN_", 0) == 0 || actionIcon || tag.rfind("LBL_TARGET", 0) == 0) {
        return ControlType::Button;
    }
    if (tag.rfind("PB_", 0) == 0) {
        return ControlType::ProgressBar;
    }
    return ControlType::Label;
}

/** Serves a HUD layout with the given tags; BTN_ACTION0 gets K1's extent. */
class LayoutGUIs : public IGUIs {
public:
    LayoutGUIs(TestEngine &engine, std::vector<std::string> tags) :
        _engine(engine), _tags(std::move(tags)) {}

    void clear() override {}

    std::shared_ptr<IGUI> get(const std::string &name, std::function<void(IGUI &)> preload) override {
        auto gui = std::make_shared<GUI>(_engine.options().graphics,
                                         _engine.sceneModule().graphs(),
                                         _engine.graphicsModule().services(),
                                         _engine.resourceModule().services());
        if (preload) {
            preload(*gui);
        }
        auto extent = Gff::Builder().field(Gff::Field::newInt("WIDTH", 800)).field(Gff::Field::newInt("HEIGHT", 600)).build();
        auto root = Gff::Builder()
                        .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                        .field(Gff::Field::newCExoString("TAG", name))
                        .field(Gff::Field::newStruct("EXTENT", extent))
                        .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
                        .build();
        gui->load(*root);
        int index = 0;
        for (const auto &tag : _tags) {
            auto type = controlTypeFor(tag);
            generated::GUI_CONTROLS definition;
            definition.CONTROLTYPE = static_cast<int>(type);
            definition.TAG = tag;
            definition.EXTENT = tag == "BTN_ACTION0"
                                    ? generated::GUI_EXTENT {60, 615, 537, 37}
                                    : generated::GUI_EXTENT {20, 10 * (index % 50), 16 * (index / 50), 20};
            definition.HILIGHT.emplace();
            auto control = std::shared_ptr<Control>(gui->newControl(type, tag));
            control->load(definition);
            gui->addControlToBack(control, IGUI::ControlCoordinates::Authored);
            ++index;
        }
        return gui;
    }

private:
    TestEngine &_engine;
    std::vector<std::string> _tags;
};

struct Session {
    TestEngine &engine;
    LayoutGUIs mainGuis;
    LayoutGUIs k2Guis;
    GUIServices guiServices;
    ServicesView services;
    GameOptions gameOptions;
    OptionsView options;
    StubConsole console;
    Game game;

    Session(TestEngine &engine, bool k2Hud) :
        engine(engine),
        mainGuis(engine, HUD::expectedTags(k2Hud)),
        k2Guis(engine, HUD::expectedTags(true)),
        guiServices(mainGuis, k2Hud ? &k2Guis : nullptr, k2Hud ? &engine.services().resource : nullptr),
        services(engine.services().game, engine.services().movie, engine.services().audio,
                 engine.services().graphics, engine.services().scene, guiServices,
                 engine.services().script, engine.services().resource, engine.services().system),
        options(gameOptions, engine.options().graphics, engine.options().audio),
        game(GameID::KotOR, "", options, services, console) {
        gameOptions.hud = k2Hud ? HudProfile::K2 : HudProfile::Default;
    }
};

bool overlaps(const Control::Extent &a, const Control::Extent &b) {
    return a.left < b.left + b.width && b.left < a.left + a.width &&
           a.top < b.top + b.height && b.top < a.top + a.height;
}

/** Art names that exist only in K2's GUI pack (design note k2_hud_plan 1.3), plus the K2 swap icon. */
const std::vector<std::string> kK2OnlyArt {
    "i_swtchwpn", "i_swtchwpn1_p", "i_swtchwpn2_p", "lbl_micombat_p", "mm_barrow_p", "uibit_bar_fp_p",
    "uibit_bar_vp_p", "uibit_brdr_16bct", "uibit_brdr_16bet", "uibit_brdr_man1p", "uibit_brdr_map_p",
    "uibit_fill_16g", "uibit_fill_2bt"};

bool isK1Art(const std::string &resRef) {
    if (resRef.empty()) {
        return true;
    }
    bool k2Suffix = resRef.size() > 2 && resRef.compare(resRef.size() - 2, 2, "_p") == 0;
    return !k2Suffix && std::find(kK2OnlyArt.begin(), kK2OnlyArt.end(), resRef) == kK2OnlyArt.end();
}

class K1HUD : public Test {
protected:
    TestEngine &engine {testEngine()};

    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
        EXPECT_CALL(static_cast<MockFonts &>(engine.services().resource.fonts), get(_)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().strings(), getText(_)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().audioClips(), get(_)).Times(AnyNumber());
    }
};

} // namespace

TEST_F(K1HUD, layout_sits_left_of_the_quick_bar_without_overlap) {
    auto slot = K1HUDControls::defaultFirstQuickSlot();
    auto layout = K1HUDControls::layoutFor(slot);

    // Swap: a quick-bar slot's size, bottom- and top-aligned with it.
    EXPECT_EQ(slot.width, layout.swap.width);
    EXPECT_EQ(slot.height, layout.swap.height);
    EXPECT_EQ(slot.top, layout.swap.top);
    EXPECT_LT(layout.swap.left + layout.swap.width, slot.left);
    EXPECT_LT(layout.indicator.left + layout.indicator.width, layout.swap.left);
    EXPECT_LT(layout.autoCombat.left + layout.autoCombat.width, layout.indicator.left);
    EXPECT_EQ(slot.top + slot.height, layout.autoCombat.top + layout.autoCombat.height);

    for (auto &extent : {layout.swap, layout.indicator, layout.autoCombat}) {
        EXPECT_FALSE(overlaps(extent, slot));
        // Right half of the canvas: anchored to the right edge, as the quick bar.
        EXPECT_GE(extent.left, 400);
        EXPECT_LE(extent.top + extent.height, 600);
    }
    EXPECT_FALSE(overlaps(layout.swap, layout.indicator));
    EXPECT_FALSE(overlaps(layout.indicator, layout.autoCombat));
    EXPECT_FALSE(overlaps(layout.activeSet, layout.stowedSet));
    // The K1 combat panel (LBL_COMBATBG*, 332..463) stays clear.
    EXPECT_FALSE(overlaps(layout.autoCombat, Control::Extent(332, 508, 131, 90)));
}

TEST_F(K1HUD, new_controls_use_k1_art_only) {
    for (const char *art : {K1HUDControls::kSlotFrame, K1HUDControls::kSlotFrameHilight, K1HUDControls::kSwapIcon,
                            K1HUDControls::kSetFrameActive, K1HUDControls::kSetFrameStowed,
                            K1HUDControls::kIndicatorFrame, K1HUDControls::kToggleOff, K1HUDControls::kToggleOn,
                            K1HUDControls::kAutoCombatIcon, K1HUDControls::kQueuedSwapIcon}) {
        EXPECT_TRUE(isK1Art(art)) << art;
    }

    Session session(engine, false);
    HUD hud(session.game, session.services);
    hud.init();
    auto gui = K1HUDTestAccess::gui(hud);
    for (auto tag : {"BTN_K1SWAPWEAPONS", "LBL_K1SWAPWEAPONS", "LBL_K1WEAPONSETS", "BTN_AUTOCOMBAT", "LBL_AUTOCOMBAT"}) {
        auto control = gui->findControl(tag);
        ASSERT_TRUE(control) << tag;
        EXPECT_TRUE(isK1Art(control->borderFillResRef())) << tag;
        EXPECT_TRUE(isK1Art(control->hilightFillResRef())) << tag;
    }
    EXPECT_EQ(K1HUDControls::kSwapIcon, gui->findControl("LBL_K1SWAPWEAPONS")->borderFillResRef());
    EXPECT_EQ(K1HUDControls::kSlotFrame, gui->findControl("BTN_K1SWAPWEAPONS")->borderFillResRef());
    // The toggle is K1 art, not the old text button.
    EXPECT_TRUE(gui->findControl("BTN_AUTOCOMBAT")->text().text.empty());
}

TEST_F(K1HUD, k1_layout_places_the_controls_from_the_authored_quick_bar) {
    Session session(engine, false);
    HUD hud(session.game, session.services);
    hud.init();
    auto gui = K1HUDTestAccess::gui(hud);
    auto layout = K1HUDControls::layoutFor(gui->findControl("BTN_ACTION0")->authoredExtent());
    auto swap = gui->findControl("BTN_K1SWAPWEAPONS");
    ASSERT_TRUE(swap);
    EXPECT_EQ(layout.swap.left, swap->authoredExtent().left);
    EXPECT_EQ(layout.swap.top, swap->authoredExtent().top);
    EXPECT_EQ(layout.autoCombat.left, gui->findControl("BTN_AUTOCOMBAT")->authoredExtent().left);
}

TEST_F(K1HUD, k2_layout_is_unchanged) {
    Session session(engine, true);
    HUD hud(session.game, session.services);
    hud.init();
    auto gui = K1HUDTestAccess::gui(hud);
    EXPECT_EQ(nullptr, K1HUDTestAccess::k1Controls(hud));
    EXPECT_FALSE(gui->findControl("BTN_K1SWAPWEAPONS"));
    EXPECT_FALSE(gui->findControl("LBL_K1WEAPONSETS"));
    EXPECT_TRUE(gui->findControl("BTN_SWAPWEAPONS"));
    auto autoCombat = gui->findControl("BTN_AUTOCOMBAT");
    ASSERT_TRUE(autoCombat);
    EXPECT_EQ("AI", autoCombat->text().text);
}

TEST_F(K1HUD, swap_and_toggle_clicks_reach_the_character) {
    Session session(engine, false);
    HUD hud(session.game, session.services);
    hud.init();
    auto *k1 = K1HUDTestAccess::k1Controls(hud);
    ASSERT_TRUE(k1);

    // No character: clicks are no-ops, not crashes; nothing shows.
    k1->swapButton()->handleClick(0, 0);
    k1->autoCombatButton()->handleClick(0, 0);
    k1->update(nullptr, true);
    EXPECT_FALSE(k1->swapButton()->isVisible());
    EXPECT_FALSE(k1->autoCombatButton()->isVisible());

    auto pc = session.game.newCreature();
    ASSERT_TRUE(session.game.party().addMember(kNpcPlayer, pc));
    session.game.party().setPlayer(pc);
    ASSERT_EQ(pc, session.game.party().getLeader());

    k1->update(pc.get(), false);
    EXPECT_TRUE(k1->swapButton()->isVisible());
    EXPECT_TRUE(k1->activeSetIcon()->isVisible());
    // The toggle shows in and out of combat, as on the K2 HUD.
    EXPECT_TRUE(k1->autoCombatButton()->isVisible());
    k1->update(pc.get(), true);
    EXPECT_TRUE(k1->autoCombatButton()->isVisible());

    // The swap button issues the player's weapon-switch request.
    k1->swapButton()->handleClick(0, 0);
    ASSERT_FALSE(pc->actions().empty());
    EXPECT_EQ(ActionType::SwitchWeapons, pc->actions().front()->type());

    // The toggle flips the character's auto-combat and its tint follows.
    EXPECT_FALSE(pc->autoCombat());
    k1->autoCombatButton()->handleClick(0, 0);
    EXPECT_TRUE(pc->autoCombat());
    k1->update(pc.get(), true);
    EXPECT_EQ(K1HUDControls::kToggleOn, k1->autoCombatButton()->borderFillResRef());
    k1->autoCombatButton()->handleClick(0, 0);
    k1->update(pc.get(), true);
    EXPECT_FALSE(pc->autoCombat());
    EXPECT_EQ(K1HUDControls::kToggleOff, k1->autoCombatButton()->borderFillResRef());
}

TEST_F(K1HUD, indicator_shows_the_in_hand_set_and_the_stowed_set) {
    // baseitems: row 8 a one-handed melee weapon, row 12 a pistol (as weaponsets.cpp).
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 12; ++i) {
        if (i == 8) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == 12) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    auto baseItems = std::shared_ptr<TwoDA>(builder.build());
    EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems")).Times(AnyNumber()).WillRepeatedly(Return(baseItems));
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto creature = game.newCreature();
    EXPECT_EQ(nullptr, K1HUDControls::setItem(*creature, false));
    EXPECT_EQ(nullptr, K1HUDControls::setItem(*creature, true));

    auto makeItem = [&game](const std::string &tag, int baseItem) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", tag))
                       .field(Gff::Field::newInt("BaseItem", baseItem))
                       .field(Gff::Field::newWord("StackSize", 1))
                       .build();
        auto item = game.newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    };
    auto sword = makeItem("sword", 8);
    auto pistol = makeItem("pistol", 12);
    ASSERT_TRUE(creature->equip(InventorySlots::rightWeapon, sword));
    ASSERT_TRUE(creature->equip(InventorySlots::leftWeapon2, pistol));

    EXPECT_EQ(sword, K1HUDControls::setItem(*creature, false));
    // An empty main hand falls back to the set's off hand.
    EXPECT_EQ(pistol, K1HUDControls::setItem(*creature, true));

    // After a swap the indicator follows the items.
    ASSERT_TRUE(creature->swapWeaponSets());
    EXPECT_EQ(pistol, K1HUDControls::setItem(*creature, false));
    EXPECT_EQ(sword, K1HUDControls::setItem(*creature, true));
}

TEST_F(K1HUD, queued_weapon_switch_shows_k1_art_in_the_k1_queue) {
    for (bool k2 : {false, true}) {
        SCOPED_TRACE(k2 ? "k2" : "k1");
        Session session(engine, k2);
        HUD hud(session.game, session.services);
        hud.init();
        auto pc = session.game.newCreature();
        ASSERT_TRUE(session.game.party().addMember(kNpcPlayer, pc));
        session.game.party().setPlayer(pc);
        pc->addAction(session.game.newAction<SwitchWeaponsAction>());

        K1HUDTestAccess::refreshQueue(hud);
        auto queue = K1HUDTestAccess::gui(hud)->findControl("LBL_QUEUE0");
        ASSERT_TRUE(queue);
        EXPECT_EQ(k2 ? "i_swtchwpn" : K1HUDControls::kQueuedSwapIcon, queue->borderFillResRef());
        if (!k2) {
            EXPECT_TRUE(isK1Art(queue->borderFillResRef()));
        }
    }
}

// Difficulty

namespace {

std::shared_ptr<TwoDA> retailDiffSettings() {
    // diffsettings.2da as K1 and K2 ship it.
    TwoDA::Builder builder;
    builder.columns({"name", "dmeasy", "easy", "normal", "hardcore", "dmplayers"})
        .row({"NoCriticalOnPC", "1", "1", "1", "", ""})
        .row({"NoAoOWithRanged", "1", "1", "1", "", ""})
        .row({"NoAoOWithPotion", "1", "1", "1", "", ""})
        .row({"MinPCDamagePercent", "100", "50", "", "", ""})
        .row({"MaxNPCDamagePercent", "25", "50", "100", "100", "200"})
        .row({"MinHP1", "1", "", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> retailDifficultyOpt() {
    TwoDA::Builder builder;
    builder.columns({"name", "desc", "multiplier"})
        .row({"42335", "Easy", "0.5"})
        .row({"42336", "Normal", "1.0"})
        .row({"42337", "Difficult", "1.5"})
        .row({"", "Default", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

/** Reads one diffsettings column, to exercise the rules as K2 would at it. */
class ColumnOptions : public IDifficultyOptions {
public:
    ColumnOptions(const DifficultyOptions &table, int column) :
        _table(table), _column(column) {}
    const DifficultyOption &get(int difficulty) const override { return _table.get(difficulty); }
    int setting(DifficultySetting setting) const override { return _table.setting(setting, _column); }

private:
    const DifficultyOptions &_table;
    int _column;
};

struct CountingRandom {
    int calls {0};
    int value {0};
    std::function<int(int)> fn() {
        return [this](int n) {
            ++calls;
            return std::min(value, n - 1);
        };
    }
};

} // namespace

TEST(K1HUDDifficulty, reads_diffsettings_at_the_fixed_normal_column) {
    NiceMock<MockTwoDAs> twoDas;
    EXPECT_CALL(twoDas, get("difficultyopt")).WillRepeatedly(Return(retailDifficultyOpt()));
    EXPECT_CALL(twoDas, get("diffsettings")).WillRepeatedly(Return(retailDiffSettings()));
    DifficultyOptions options(twoDas);
    options.init();

    EXPECT_EQ(2, kDiffSettingsColumn);
    EXPECT_EQ(1, options.setting(DifficultySetting::NoCriticalOnPC));
    EXPECT_EQ(1, options.setting(DifficultySetting::NoAoOWithPotion));
    EXPECT_EQ(0, options.setting(DifficultySetting::MinPCDamagePercent));
    EXPECT_EQ(100, options.setting(DifficultySetting::MaxNPCDamagePercent));
    EXPECT_EQ(0, options.setting(DifficultySetting::MinHP1));

    EXPECT_EQ(100, options.setting(DifficultySetting::MinPCDamagePercent, 0));
    EXPECT_EQ(25, options.setting(DifficultySetting::MaxNPCDamagePercent, 0));
    EXPECT_EQ(1, options.setting(DifficultySetting::MinHP1, 0));
    EXPECT_EQ(50, options.setting(DifficultySetting::MinPCDamagePercent, 1));
    EXPECT_EQ(200, options.setting(DifficultySetting::MaxNPCDamagePercent, 4));
    EXPECT_EQ(0, options.setting(DifficultySetting::MinHP1, 7));

    // The player option's multipliers are unchanged: 0.5 / 1.0 / 1.5.
    EXPECT_FLOAT_EQ(0.5f, options.get(0).damageMultiplier);
    EXPECT_FLOAT_EQ(1.0f, options.get(1).damageMultiplier);
    EXPECT_FLOAT_EQ(1.5f, options.get(2).damageMultiplier);
}

TEST(K1HUDDifficulty, missing_diffsettings_turns_every_rule_off) {
    NiceMock<MockTwoDAs> twoDas;
    EXPECT_CALL(twoDas, get("difficultyopt")).WillRepeatedly(Return(retailDifficultyOpt()));
    EXPECT_CALL(twoDas, get("diffsettings")).WillRepeatedly(Return(nullptr));
    DifficultyOptions options(twoDas);
    options.init();
    for (int row = 0; row < kDiffSettingsRowCount; ++row) {
        EXPECT_EQ(0, options.setting(static_cast<DifficultySetting>(row))) << row;
    }
}

TEST(K1HUDDifficulty, normal_column_leaves_every_roll_and_draws_nothing) {
    NiceMock<MockTwoDAs> twoDas;
    EXPECT_CALL(twoDas, get("difficultyopt")).WillRepeatedly(Return(retailDifficultyOpt()));
    EXPECT_CALL(twoDas, get("diffsettings")).WillRepeatedly(Return(retailDiffSettings()));
    DifficultyOptions options(twoDas);
    options.init();

    CountingRandom random;
    for (int roll = 1; roll <= 12; ++roll) {
        for (bool attackerIsPC : {false, true}) {
            for (bool targetIsPC : {false, true}) {
                EXPECT_EQ(roll, adjustDamageRollForDifficulty(options, roll, 12, attackerIsPC, targetIsPC, random.fn()));
            }
        }
        EXPECT_EQ(roll, applyMinHP1(options, roll, 5));
    }
    EXPECT_EQ(0, random.calls);
}

TEST(K1HUDDifficulty, damage_percent_rules_follow_k2_at_other_columns) {
    NiceMock<MockTwoDAs> twoDas;
    EXPECT_CALL(twoDas, get("difficultyopt")).WillRepeatedly(Return(retailDifficultyOpt()));
    EXPECT_CALL(twoDas, get("diffsettings")).WillRepeatedly(Return(retailDiffSettings()));
    DifficultyOptions table(twoDas);
    table.init();
    CountingRandom random;

    // dmeasy: a PC's hit on a non-PC always rolls the maximum.
    ColumnOptions dmeasy(table, 0);
    EXPECT_EQ(8, adjustDamageRollForDifficulty(dmeasy, 2, 8, true, false, random.fn()));
    // dmeasy: a non-PC's hit on a PC loses 75% of the dice maximum, floor 1.
    EXPECT_EQ(1, adjustDamageRollForDifficulty(dmeasy, 5, 8, false, true, random.fn()));
    EXPECT_EQ(7 - 6, adjustDamageRollForDifficulty(dmeasy, 7, 8, false, true, random.fn()));
    EXPECT_EQ(0, random.calls);
    // dmeasy MinHP1: a PC keeps 1 hit point.
    EXPECT_EQ(4, applyMinHP1(dmeasy, 9, 5));
    EXPECT_EQ(0, applyMinHP1(dmeasy, 9, 1));
    EXPECT_EQ(3, applyMinHP1(dmeasy, 3, 5));

    // easy: a PC's hit gains a random 50..99% of the maximum, capped.
    ColumnOptions easy(table, 1);
    random.value = 0;
    EXPECT_EQ(2 + 4, adjustDamageRollForDifficulty(easy, 2, 8, true, false, random.fn()));
    EXPECT_EQ(1, random.calls);
    random.value = 49;
    EXPECT_EQ(8, adjustDamageRollForDifficulty(easy, 2, 8, true, false, random.fn()));
    // easy: a non-PC's hit loses half the maximum.
    EXPECT_EQ(7 - 4, adjustDamageRollForDifficulty(easy, 7, 8, false, true, random.fn()));

    // dmplayers: a non-PC's hit gains a random 0..99% of the maximum.
    ColumnOptions dmplayers(table, 4);
    random.value = 50;
    EXPECT_EQ(3 + 4, adjustDamageRollForDifficulty(dmplayers, 3, 8, false, true, random.fn()));

    // Same-side hits are never adjusted.
    EXPECT_EQ(3, adjustDamageRollForDifficulty(dmeasy, 3, 8, true, true, random.fn()));
    EXPECT_EQ(3, adjustDamageRollForDifficulty(dmeasy, 3, 8, false, false, random.fn()));
}

TEST(K1HUDDifficulty, default_normal_difficulty_keeps_the_damage_unchanged) {
    TestEngine engine;
    engine.init();
    engine.options().game.clientDifficulty = 1;
    StubConsole console;
    Game game(GameID::KotOR, "", engine.options(), engine.services(), console);
    auto pc = game.newCreature();
    ASSERT_TRUE(game.party().addMember(kNpcPlayer, pc));
    game.party().setPlayer(pc);
    pc->setCurrentHitPoints(3);
    // Normal: no multiplier and no MinHP1 floor.
    EXPECT_EQ(9, game.scaleDamageForDifficulty(9, *pc));
    auto other = game.newCreature();
    EXPECT_EQ(9, game.scaleDamageForDifficulty(9, *other));
}
