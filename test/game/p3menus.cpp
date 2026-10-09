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

// P3 menus: K2-profile screens in a K1 session (design note p3_menus).

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui/optionspanels.h"
#include "reone/game/gui/partyselect.h"
#include "reone/gui/control/slider.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"

#include <set>

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace {

/** Serves each requested GUI with the given tags at the given extents. */
class LayoutGUIs : public IGUIs {
public:
    struct Entry {
        std::string tag;
        ControlType type;
        Control::Extent extent;
    };

    LayoutGUIs(TestEngine &engine, std::map<std::string, std::vector<Entry>> layouts) :
        _engine(engine), _layouts(std::move(layouts)) {}

    void clear() override {}

    std::shared_ptr<IGUI> get(const std::string &name, std::function<void(IGUI &)> preload) override {
        auto layout = _layouts.find(name);
        if (layout == _layouts.end()) {
            return nullptr;
        }
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
        for (const auto &entry : layout->second) {
            generated::GUI_CONTROLS definition;
            definition.CONTROLTYPE = static_cast<int>(entry.type);
            definition.TAG = entry.tag;
            definition.EXTENT.LEFT = entry.extent.left;
            definition.EXTENT.TOP = entry.extent.top;
            definition.EXTENT.WIDTH = entry.extent.width;
            definition.EXTENT.HEIGHT = entry.extent.height;
            definition.HILIGHT.emplace();
            auto control = std::shared_ptr<Control>(gui->newControl(entry.type, entry.tag));
            control->load(definition);
            gui->addControlToBack(control, IGUI::ControlCoordinates::Authored);
        }
        return gui;
    }

private:
    TestEngine &_engine;
    std::map<std::string, std::vector<Entry>> _layouts;
};

/** partyselect_p's controls (positions are not under test here). */
std::vector<LayoutGUIs::Entry> partySelectLayout() {
    std::vector<LayoutGUIs::Entry> entries;
    for (int i = 0; i < 12; ++i) {
        Control::Extent seat {10 + 80 * (i % 4), 10 + 80 * (i / 4), 80, 80};
        entries.push_back({"BTN_NPC" + std::to_string(i), ControlType::ToggleButton, seat});
        entries.push_back({"LBL_CHAR" + std::to_string(i), ControlType::Label, seat});
        entries.push_back({"LBL_NA" + std::to_string(i), ControlType::Label, seat});
    }
    for (auto tag : {"BTN_ACCEPT", "BTN_DONE", "BTN_BACK"}) {
        entries.push_back({tag, ControlType::Button, {10, 500, 100, 28}});
    }
    for (auto tag : {"LBL_3D", "LBL_BAR1", "LBL_BAR2", "LBL_BAR3", "LBL_BAR4", "LBL_BAR5", "LBL_BEVEL_L",
                     "LBL_BEVEL_M", "LBL_COUNT", "LBL_NAMEBACK", "LBL_NPC_LEVEL", "LBL_NPC_NAME", "LBL_TITLE"}) {
        entries.push_back({tag, ControlType::Label, {420, 134, 280, 20}});
    }
    return entries;
}

struct Session {
    LayoutGUIs mainGuis;
    LayoutGUIs k2Guis;
    GUIServices guiServices;
    ServicesView services;
    GameOptions gameOptions;
    OptionsView options;
    StubConsole console;
    Game game;

    Session(TestEngine &engine, GameID gameId, HudProfile hud, bool withK2Set) :
        mainGuis(engine, {{"partyselect_p", partySelectLayout()}}),
        k2Guis(engine, {{"partyselect_p", partySelectLayout()}}),
        guiServices(mainGuis, withK2Set ? &k2Guis : nullptr, withK2Set ? &engine.services().resource : nullptr),
        services(engine.services().game, engine.services().movie, engine.services().audio,
                 engine.services().graphics, engine.services().scene, guiServices,
                 engine.services().script, engine.services().resource, engine.services().system),
        options(gameOptions, engine.options().graphics, engine.options().audio),
        game(gameId, "", options, services, console) {
        gameOptions.hud = hud;
    }
};

class P3Menus : public Test {
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

// Item 23: a K1 options file served through the K2 loader read its K1
// strrefs from K2's TLK ("Gender Required" on the Sound button).
TEST_F(P3Menus, options_panels_follow_the_menu_profile) {
    Session k2(engine, GameID::KotOR, HudProfile::K2, true);
    OptionsPanels k2Panels(k2.game, k2.services);
    EXPECT_EQ("optionsmain_p", k2Panels.panelResRef("optionsmain"));
    EXPECT_EQ("optgameplay_p", k2Panels.panelResRef("optgameplay_p"));
    EXPECT_EQ("optkeyentry_p", k2Panels.panelResRef("optkeyentry"));

    Session k1(engine, GameID::KotOR, HudProfile::K1, false);
    OptionsPanels k1Panels(k1.game, k1.services);
    EXPECT_EQ("optionsmain", k1Panels.panelResRef("optionsmain"));
}

// Item 26: K1's nine companions sit where K2's ring seats their counterparts.
TEST_F(P3Menus, k1_companions_take_their_counterparts_seats) {
    Session session(engine, GameID::KotOR, HudProfile::K2, true);
    PartySelection screen(session.game, session.services);
    screen.init();
    screen.prepare(PartySelectionContext());

    EXPECT_EQ(2, screen.slotOf(1)); // Canderous: Mandalore's seat
    EXPECT_EQ(0, screen.slotOf(2)); // Carth: Atton's
    EXPECT_EQ(5, screen.slotOf(3)); // HK-47: HK-47's
    EXPECT_EQ(8, screen.slotOf(7)); // T3-M4: T3-M4's
    std::set<int> seats;
    for (int npc = 0; npc < kKotorNpcCount; ++npc) {
        int seat = screen.slotOf(npc);
        EXPECT_GE(seat, 0);
        EXPECT_LE(seat, 9); // the ten distinct places of K2's ring
        EXPECT_EQ(npc, screen.npcOfSlot(seat));
        seats.insert(seat);
    }
    EXPECT_EQ(9u, seats.size());
    EXPECT_EQ(-1, screen.npcOfSlot(3)); // G0-T0's seat
    EXPECT_EQ(-1, screen.npcOfSlot(10));
    EXPECT_EQ(-1, screen.npcOfSlot(11));
    EXPECT_FALSE(screen.controlByTag("BTN_NPC3")->isVisible());
    EXPECT_FALSE(screen.controlByTag("BTN_NPC10")->isVisible());
    EXPECT_FALSE(screen.controlByTag("BTN_NPC11")->isVisible());
    EXPECT_TRUE(screen.controlByTag("BTN_NPC9")->isVisible()); // Juhani: Visas's seat

    // The stand-alone screen keeps its own title; the count is a number of
    // free slots, and with nobody available nothing is selected or shown.
    EXPECT_TRUE(screen.controlByTag("LBL_TITLE")->isVisible());
    EXPECT_THAT(screen.controlByTag("LBL_COUNT")->text().text, EndsWith("2"));
    EXPECT_EQ(-1, screen.selectedNpc());
    EXPECT_FALSE(screen.controlByTag("LBL_NPC_NAME")->isVisible());
}

TEST_F(P3Menus, k2_session_keeps_its_own_seats) {
    Session session(engine, GameID::TSL, HudProfile::Default, false);
    PartySelection screen(session.game, session.services, true);
    screen.init();
    screen.prepare(PartySelectionContext());
    for (int npc = 0; npc < kTSLNpcCount; ++npc) {
        EXPECT_EQ(npc, screen.slotOf(npc));
    }
    // Inside the in-game menu the host draws the title.
    EXPECT_FALSE(screen.controlByTag("LBL_TITLE")->isVisible());
}

// Item 27: the alignment bar is vertical; its marker was sized from the track
// height as if horizontal and drew as a bar across the screen.
TEST_F(P3Menus, vertical_slider_moves_along_its_height) {
    GUI gui(engine.options().graphics, engine.sceneModule().graphs(), engine.graphicsModule().services(),
            engine.resourceModule().services());
    auto control = gui.newControl(ControlType::Slider, "SLD_ALIGN");
    auto &slider = static_cast<Slider &>(*control);
    slider.setExtent({0, 0, 20, 200});
    EXPECT_TRUE(slider.isVertical());
    // The top is the maximum (light), the bottom the minimum (dark).
    EXPECT_FLOAT_EQ(1.0f, slider.valueAt(10, 0));
    EXPECT_FLOAT_EQ(0.0f, slider.valueAt(10, 200));
    EXPECT_NEAR(0.5f, slider.valueAt(10, 100), 0.01f);

    slider.setExtent({0, 0, 200, 20});
    EXPECT_FALSE(slider.isVertical());
    EXPECT_FLOAT_EQ(slider.valueAt(0), slider.valueAt(0, 15));
}

// Item 25: K2 menus fill a wide screen (Widescreen mode).
TEST_F(P3Menus, widescreen_stretches_frames_and_keeps_pictures) {
    auto &graphics = engine.options().graphics;
    auto saved = std::make_pair(graphics.width, graphics.height);
    GUI gui(graphics, engine.sceneModule().graphs(), engine.graphicsModule().services(),
            engine.resourceModule().services());
    gui.setResolution(800, 600);
    gui.setScaling(IGUI::ScalingMode::Widescreen);
    auto extent = Gff::Builder().field(Gff::Field::newInt("WIDTH", 800)).field(Gff::Field::newInt("HEIGHT", 600)).build();
    auto root = Gff::Builder()
                    .field(Gff::Field::newInt("CONTROLTYPE", static_cast<int>(ControlType::Panel)))
                    .field(Gff::Field::newCExoString("TAG", "ROOT"))
                    .field(Gff::Field::newStruct("EXTENT", extent))
                    .field(Gff::Field::newStruct("BORDER", Gff::Builder().build()))
                    .build();
    graphics.width = 1920;
    graphics.height = 1080;
    gui.load(*root);
    auto add = [&](const char *tag, ControlType type, Control::Extent authored) {
        generated::GUI_CONTROLS definition;
        definition.CONTROLTYPE = static_cast<int>(type);
        definition.TAG = tag;
        definition.EXTENT.LEFT = authored.left;
        definition.EXTENT.TOP = authored.top;
        definition.EXTENT.WIDTH = authored.width;
        definition.EXTENT.HEIGHT = authored.height;
        auto control = std::shared_ptr<Control>(gui.newControl(type, tag));
        control->load(definition);
        gui.addControlToBack(control, IGUI::ControlCoordinates::Authored);
        return control;
    };
    auto bar = add("LBL_BAR1", ControlType::Label, {61, 105, 678, 14});
    auto portrait = add("LBL_CHAR0", ControlType::Label, {118, 135, 80, 80});
    auto alignment = add("SLD_ALIGN", ControlType::Slider, {96, 190, 64, 256});
    auto logo = add("LBL_GAMELOGO", ControlType::Label, {310, 0, 330, 330});

    // The root fills the screen; a strip stretches on each axis.
    EXPECT_NEAR(1920, gui.rootControl().extent().width, 1);
    EXPECT_NEAR(1080, gui.rootControl().extent().height, 1);
    EXPECT_NEAR(146, bar->extent().left, 1);
    EXPECT_NEAR(1627, bar->extent().width, 1);
    EXPECT_NEAR(189, bar->extent().top, 1);
    // A portrait keeps its aspect at the limiting-axis size, on its place.
    EXPECT_NEAR(144, portrait->extent().width, 1);
    EXPECT_NEAR(144, portrait->extent().height, 1);
    EXPECT_NEAR(307, portrait->extent().left, 1);
    EXPECT_NEAR(243, portrait->extent().top, 1);
    // So does a vertical track.
    EXPECT_NEAR(115, alignment->extent().width, 1);
    EXPECT_NEAR(460, alignment->extent().height, 1);
    // A large picture stretches unless its screen says otherwise.
    EXPECT_NEAR(792, logo->extent().width, 1);
    gui.setControlKeepsAspect("LBL_GAMELOGO", true);
    gui.refreshLayout();
    EXPECT_NEAR(594, logo->extent().width, 1);
    EXPECT_NEAR(594, logo->extent().height, 1);

    // At the authored aspect the layout is the uniform scaled one.
    graphics.width = 1600;
    graphics.height = 1200;
    gui.refreshLayout();
    EXPECT_NEAR(236, portrait->extent().left, 1);
    EXPECT_NEAR(160, portrait->extent().width, 1);
    EXPECT_NEAR(1356, bar->extent().width, 1);

    graphics.width = saved.first;
    graphics.height = saved.second;
}
