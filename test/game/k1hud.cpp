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
 * - pass 2 (design note k1hud_parity_2): Force powers on the quick bar and the
 *   target menu, live pause / solo toggles, portrait status indicators and
 *   the leader's effect icons on both layouts, the K1 equipment screen's
 *   weapon-set toggle and the weapon-swap key;
 * - the difficulty tables: diffsettings at the game's fixed "normal" column
 *   changes nothing, and its rules behave as K2's at the other columns.
 */

#include "../fixtures/engine.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/d20/forcepowers.h"
#include "reone/game/d20/spell.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/d20/attributes.h"
#include "reone/game/d20/feat.h"
#include "reone/game/gui/ingame/abilities.h"
#include "reone/game/gui/ingame/character.h"
#include "reone/game/gui/ingame/equip.h"
#include "reone/game/gui/ingame/journal.h"
#include "reone/game/gui/ingame/messages.h"
#include "reone/game/messagelog.h"
#include "reone/game/keymap.h"
#include "reone/gui/control/togglebutton.h"
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
    static void updateToggles(HUD &hud) { hud.updateModeToggles(); }
    static void updateParty(HUD &hud) { hud.updatePartyPresentation(); }
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

// Pass 2

namespace {

std::shared_ptr<Spell> makeSpell(SpellType type, int userType, uint32_t category, bool hostile) {
    auto spell = std::make_shared<Spell>();
    spell->type = type;
    spell->userType = userType;
    spell->category = category;
    spell->hostile = hostile;
    return spell;
}

} // namespace

TEST(K1HUDPass2, force_powers_split_between_quick_bar_and_target_menu) {
    // spells.2da: 8 Speed Burst (personal), 28 Heal (personal), 46 Stun
    // (hostile), 6 Affect Mind (no category: conversation only), and a K2
    // form (usertype 6); the last two never reach the HUD's power lists.
    NiceMock<MockSpells> spells;
    std::map<int, std::shared_ptr<Spell>> table {
        {8, makeSpell(static_cast<SpellType>(8), kForcePowerUserType, 0x1808, false)},
        {28, makeSpell(static_cast<SpellType>(28), kForcePowerUserType, 0x2441, false)},
        {46, makeSpell(static_cast<SpellType>(46), kForcePowerUserType, 0x1201, true)},
        {6, makeSpell(static_cast<SpellType>(6), kForcePowerUserType, 0, false)},
        {260, makeSpell(static_cast<SpellType>(260), 6, 0x1000, false)}};
    ON_CALL(spells, get(_)).WillByDefault([&table](SpellType type) -> std::shared_ptr<Spell> {
        auto it = table.find(static_cast<int>(type));
        return it == table.end() ? nullptr : it->second;
    });
    std::set<SpellType> known;
    for (int row : {6, 8, 28, 46, 260, 999}) {
        known.insert(static_cast<SpellType>(row));
    }

    auto personal = hudForcePowers(known, spells, false);
    ASSERT_EQ(2u, personal.size());
    EXPECT_EQ(static_cast<SpellType>(8), personal[0]->type);
    EXPECT_EQ(static_cast<SpellType>(28), personal[1]->type);
    auto hostile = hudForcePowers(known, spells, true);
    ASSERT_EQ(1u, hostile.size());
    EXPECT_EQ(static_cast<SpellType>(46), hostile[0]->type);
    EXPECT_FALSE(isHUDForcePower(*table[6]));
    EXPECT_FALSE(isHUDForcePower(*table[260]));
}

TEST_F(K1HUD, pause_and_solo_toggles_show_and_follow_the_game_on_both_layouts) {
    for (bool k2 : {false, true}) {
        SCOPED_TRACE(k2 ? "k2" : "k1");
        Session session(engine, k2);
        HUD hud(session.game, session.services);
        hud.init();
        auto gui = K1HUDTestAccess::gui(hud);
        auto pause = std::dynamic_pointer_cast<ToggleButton>(gui->findControl("TB_PAUSE"));
        auto solo = std::dynamic_pointer_cast<ToggleButton>(gui->findControl("TB_SOLO"));
        auto stealth = gui->findControl("TB_STEALTH");
        ASSERT_TRUE(pause && solo && stealth);

        K1HUDTestAccess::updateToggles(hud);
        EXPECT_FALSE(pause->isVisible()) << "no character, no toggles";

        auto pc = session.game.newCreature();
        ASSERT_TRUE(session.game.party().addMember(kNpcPlayer, pc));
        session.game.party().setPlayer(pc);
        K1HUDTestAccess::updateToggles(hud);
        EXPECT_TRUE(pause->isVisible());
        EXPECT_TRUE(solo->isVisible());
        // No stealth mode in the build.
        EXPECT_FALSE(stealth->isVisible());
        if (!k2) {
            EXPECT_TRUE(gui->findControl("LBL_MOULDING1")->isVisible());
        }

        EXPECT_FALSE(pause->isOn());
        pause->handleClick(0, 0);
        EXPECT_TRUE(session.game.isPaused());
        K1HUDTestAccess::updateToggles(hud);
        EXPECT_TRUE(pause->isOn());
        pause->handleClick(0, 0);
        EXPECT_FALSE(session.game.isPaused());

        solo->handleClick(0, 0);
        EXPECT_TRUE(session.game.party().isSoloMode());
        K1HUDTestAccess::updateToggles(hud);
        EXPECT_TRUE(solo->isOn());
        // The PartyActive key reaches the same state.
        session.game.toggleSoloModeAsKey();
        EXPECT_FALSE(session.game.party().isSoloMode());
        K1HUDTestAccess::updateToggles(hud);
        EXPECT_FALSE(solo->isOn());
    }
}

TEST_F(K1HUD, portrait_status_shows_a_downed_member_on_both_layouts) {
    for (bool k2 : {false, true}) {
        SCOPED_TRACE(k2 ? "k2" : "k1");
        Session session(engine, k2);
        HUD hud(session.game, session.services);
        hud.init();
        auto gui = K1HUDTestAccess::gui(hud);
        auto pc = session.game.newCreature();
        ASSERT_TRUE(session.game.party().addMember(kNpcPlayer, pc));
        session.game.party().setPlayer(pc);
        pc->setMaxHitPoints(20);
        pc->setCurrentHitPoints(20);

        auto status = HUD::memberStatus(engine.services().resource.twoDas, *pc);
        EXPECT_FALSE(status.down);
        EXPECT_FALSE(status.debilitated);
        EXPECT_FALSE(status.helped);
        EXPECT_FALSE(status.hindered);
        K1HUDTestAccess::updateParty(hud);
        EXPECT_FALSE(gui->findControl("LBL_DISABLE1")->isVisible());
        ASSERT_TRUE(gui->findControl("LBL_EFFECTICON0"));
        EXPECT_FALSE(gui->findControl("LBL_EFFECTICON0")->isVisible());

        // Stunned: cannot act.
        pc->applyEffect(std::make_shared<StunnedEffect>(), DurationType::Temporary, 60.0f);
        EXPECT_TRUE(HUD::memberStatus(engine.services().resource.twoDas, *pc).debilitated);
        K1HUDTestAccess::updateParty(hud);
        EXPECT_TRUE(gui->findControl("LBL_DEBILATATED1")->isVisible());
        EXPECT_FALSE(gui->findControl("LBL_DISABLE1")->isVisible());

        // Down: the downed overlay replaces the stunned one.
        pc->setCurrentHitPoints(0);
        EXPECT_TRUE(HUD::memberStatus(engine.services().resource.twoDas, *pc).down);
        K1HUDTestAccess::updateParty(hud);
        EXPECT_TRUE(gui->findControl("LBL_DISABLE1")->isVisible());
        EXPECT_FALSE(gui->findControl("LBL_DEBILATATED1")->isVisible());
        EXPECT_FALSE(gui->findControl("LBL_DISABLE2")->isVisible()) << "no second member";
    }
}

TEST(K1HUDPass2, effect_icon_rows_clear_the_toggles_and_portraits) {
    // Authored extents of mipc28x6 (K1) and mipc28x6_p (K2).
    struct Case {
        bool tsl;
        Control::Extent portrait;
        std::vector<Control::Extent> keepClear;
    };
    std::vector<Case> cases {
        {false, {4, 534, 75, 61},
         {{6, 465, 35, 35}, {6, 500, 35, 35}, {37, 500, 35, 35}, {4, 534, 75, 61}, {79, 560, 48, 35},
          {127, 560, 48, 35}, {332, 508, 131, 90}}},
        {true, {704, 520, 78, 78},
         {{548, 562, 36, 36}, {584, 562, 36, 36}, {620, 562, 36, 36}, {656, 562, 36, 36}, {704, 520, 78, 78},
          {677, 520, 39, 78}, {750, 482, 36, 36}, {737, 482, 18, 36}, {310, 498, 180, 90}}}};
    for (const auto &c : cases) {
        SCOPED_TRACE(c.tsl ? "k2" : "k1");
        auto row = HUD::effectIconRowFor(c.portrait, c.tsl);
        for (int i = 0; i < HUD::kEffectIconCount; ++i) {
            Control::Extent icon {row.left + i * row.step, row.top, row.size, row.size};
            EXPECT_GE(icon.left, 0);
            EXPECT_LE(icon.left + icon.width, 800);
            for (const auto &other : c.keepClear) {
                EXPECT_FALSE(overlaps(icon, other)) << i << " vs " << other.left << "," << other.top;
            }
        }
    }
}

TEST(K1HUDPass2, k1_equipment_second_set_has_its_own_weapon_slots) {
    using Slot = Equipment::Slot;
    auto left = Equipment::k1SecondSetExtent(Slot::WeapL2);
    auto right = Equipment::k1SecondSetExtent(Slot::WeapR2);
    // K1 equip layout (640x480): the pair sits under the portrait (55..159 x
    // 155..260) and above the vitality and defense blocks (from y 304); the
    // slot art keeps an 8 px transparent margin inside its control.
    auto core = [](Control::Extent e) { return Control::Extent(e.left + 8, e.top + 8, e.width - 16, e.height - 16); };
    for (auto extent : {left, right}) {
        EXPECT_FALSE(overlaps(core(extent), Control::Extent(55, 155, 104, 105)));
        EXPECT_FALSE(overlaps(core(extent), Control::Extent(55, 304, 104, 92)));
        EXPECT_GE(extent.left, 40);
        EXPECT_LE(extent.left + extent.width, 175);
    }
    EXPECT_FALSE(overlaps(core(left), core(right)));
    EXPECT_LT(left.left, right.left); // left hand on the left
}

TEST(K1HUDPass2, weapon_swap_key_defaults_to_h_and_is_free) {
    Keymap keymap;
    EXPECT_EQ(input::KeyCode::H, keymap.keyFor(KeyAction::SwitchWeapons));
    auto action = keymap.actionFor(input::KeyCode::H);
    ASSERT_TRUE(action);
    EXPECT_EQ(KeyAction::SwitchWeapons, *action);
    EXPECT_EQ(std::string("SwitchWeaps"), keyActionInfo(KeyAction::SwitchWeapons).name);
}

// Pass 2 follow-up: screens missing on both sides

TEST(K1HUDScreens, abilities_powers_and_feats_tabs_list_the_leaders_own) {
    NiceMock<MockSpells> spells;
    NiceMock<MockFeats> feats;
    auto speed = makeSpell(static_cast<SpellType>(8), kForcePowerUserType, 0x1808, false);
    speed->name = "Burst of Speed";
    auto affect = makeSpell(static_cast<SpellType>(6), kForcePowerUserType, 0, false);
    affect->name = "Affect Mind";
    auto grenade = makeSpell(static_cast<SpellType>(100), 2, 0x10, true); // an item ability
    grenade->name = "Frag Grenade";
    ON_CALL(spells, get(_)).WillByDefault([&](SpellType type) -> std::shared_ptr<Spell> {
        switch (static_cast<int>(type)) {
        case 8: return speed;
        case 6: return affect;
        case 100: return grenade;
        default: return nullptr;
        }
    });
    auto power = std::make_shared<Feat>();
    power->name = "Power Attack";
    power->description = "A strong blow.";
    auto hidden = std::make_shared<Feat>(); // no name: never listed
    ON_CALL(feats, get(_)).WillByDefault([&](FeatType type) -> std::shared_ptr<Feat> {
        return static_cast<int>(type) == 28 ? power : (static_cast<int>(type) == 29 ? hidden : nullptr);
    });

    CreatureAttributes attributes;
    attributes.addSpell(static_cast<SpellType>(8));
    attributes.addSpell(static_cast<SpellType>(6));
    attributes.addSpell(static_cast<SpellType>(100));
    attributes.addFeat(static_cast<FeatType>(28));
    attributes.addFeat(static_cast<FeatType>(29));

    auto powers = AbilitiesMenu::entriesFor(AbilitiesMenu::Tab::Powers, attributes, spells, feats);
    ASSERT_EQ(2u, powers.size()) << "Force powers only, conversation powers included";
    EXPECT_EQ("p6", powers[0].tag);
    EXPECT_EQ("Affect Mind", powers[0].name);
    EXPECT_EQ("p8", powers[1].tag);
    auto featRows = AbilitiesMenu::entriesFor(AbilitiesMenu::Tab::Feats, attributes, spells, feats);
    ASSERT_EQ(1u, featRows.size());
    EXPECT_EQ("f28", featRows[0].tag);
    EXPECT_EQ("A strong blow.", featRows[0].description);
    EXPECT_TRUE(AbilitiesMenu::entriesFor(AbilitiesMenu::Tab::Skills, attributes, spells, feats).empty());
}

TEST(K1HUDScreens, message_log_keeps_dialog_and_splits_feedback) {
    MessageLog log;
    log.addDialog("Trask Ulgo", "We have to get to the bridge!");
    log.addDialog("Harness", "Let's go.");
    log.addDialog("", "(A door opens.)");
    log.addDialog("Nobody", ""); // empty lines are not logged
    log.addDialog("Nobody", " \n");
    log.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Combat, "Sith Soldier attacks Harness: hit");
    log.add(MessageLog::kFeedbackMessageType, MessageLog::Style::Normal, "You have failed to unlock it.");
    ASSERT_EQ(5u, log.entries().size());
    EXPECT_EQ("Trask Ulgo: We have to get to the bridge!", log.entries()[0].text);
    EXPECT_EQ("(A door opens.)", log.entries()[2].text);

    auto split = MessagesMenu::split(log.entries());
    EXPECT_EQ(3u, split.dialog.size());
    EXPECT_EQ(2u, split.feedback.size()) << "K1's feedback page lists combat too";
    ASSERT_EQ(1u, split.combat.size());
    EXPECT_EQ(MessageLog::Style::Combat, split.combat[0]->style);
    ASSERT_EQ(1u, split.general.size());
    EXPECT_EQ("You have failed to unlock it.", split.general[0]->text);
}

TEST(K1HUDScreens, journal_sorts_and_splits_active_from_completed) {
    using Row = JournalMenu::Row;
    using Filter = JournalMenu::Filter;
    std::vector<Row> rows {
        {"q_b", "Bravo", 2, 3, 0, false},
        {"q_a", "alpha", 1, 5, 1, false},
        {"q_c", "Charlie", 0, 3, 2, false},
        {"q_done", "Done", 0, 1, 3, true}};
    auto tags = [](const std::vector<Row> &sorted) {
        std::vector<std::string> result;
        for (const auto &row : sorted) {
            result.push_back(row.plotId);
        }
        return result;
    };
    EXPECT_EQ((std::vector<std::string> {"q_b", "q_a", "q_c"}), tags(JournalMenu::arrange(rows, Filter::Time, false)));
    EXPECT_EQ((std::vector<std::string> {"q_a", "q_b", "q_c"}), tags(JournalMenu::arrange(rows, Filter::Name, false)));
    EXPECT_EQ((std::vector<std::string> {"q_c", "q_a", "q_b"}), tags(JournalMenu::arrange(rows, Filter::Priority, false)));
    // Planet ties keep the order received.
    EXPECT_EQ((std::vector<std::string> {"q_b", "q_c", "q_a"}), tags(JournalMenu::arrange(rows, Filter::Planet, false)));
    EXPECT_EQ((std::vector<std::string> {"q_done"}), tags(JournalMenu::arrange(rows, Filter::Time, true)));

    // K1's BTN_SORT cycles back to the start.
    Filter filter = Filter::Time;
    for (Filter expected : {Filter::Name, Filter::Priority, Filter::Planet, Filter::Time}) {
        filter = JournalMenu::nextK1Sort(filter);
        EXPECT_EQ(expected, filter);
    }
}

TEST(K1HUDScreens, character_scripts_button_cycles_the_party_ai_style) {
    EXPECT_EQ(1, CharacterMenu::nextPartyAIStyle(0));
    EXPECT_EQ(2, CharacterMenu::nextPartyAIStyle(1));
    EXPECT_EQ(0, CharacterMenu::nextPartyAIStyle(2));
    EXPECT_EQ(0, CharacterMenu::nextPartyAIStyle(7)) << "an unknown style resets";
    EXPECT_EQ(1096, CharacterMenu::partyAIStyleStrRef(0));
    EXPECT_EQ(1097, CharacterMenu::partyAIStyleStrRef(1));
    EXPECT_EQ(1098, CharacterMenu::partyAIStyleStrRef(2));
    // character_p: the added button sits on the footer row, clear of Exit.
    auto extent = CharacterMenu::k2ScriptsButtonExtent();
    EXPECT_FALSE(overlaps(extent, Control::Extent(75, 516, 120, 28)));
    EXPECT_LE(extent.left + extent.width, 800);
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
