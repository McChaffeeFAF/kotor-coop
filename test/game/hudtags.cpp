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
 * The HUD's tag contract, for both layouts and both presentations: the HUD
 * binds every tag its layout authors, and any tag a layout lacks is skipped,
 * including the mismatched cases a K2 HUD in a K1 session could produce.
 */

#include "../fixtures/engine.h"
#include "reone/game/game.h"
#include "reone/game/gui/hud.h"
#include "reone/gui/control/togglebutton.h"
#include "reone/gui/gui.h"
#include "reone/resource/gff.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace reone;
using namespace reone::game;
using namespace reone::gui;
using namespace reone::resource;
using namespace testing;

namespace reone::game {

class HUDTagTestAccess {
public:
    static std::shared_ptr<IGUI> gui(HUD &hud) { return hud._gui; }
    static void updateParty(HUD &hud) { hud.updatePartyPresentation(); }
    static void toggleCombat(HUD &hud, bool enabled) { hud.toggleCombat(enabled); }
    static void refreshQueue(HUD &hud) { hud.refreshActionQueueItems(); }
    static bool presentsTSL(HUD &hud) { return hud.isTSL(); }
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

/** Serves each requested GUI with exactly the tags it was given, no more. */
class TagGUIs : public IGUIs {
public:
    TagGUIs(TestEngine &engine, std::map<std::string, std::vector<std::string>> layouts) :
        _engine(engine), _layouts(std::move(layouts)) {}

    void clear() override { loaded.clear(); }

    std::shared_ptr<IGUI> get(const std::string &name, std::function<void(IGUI &)> preload) override {
        requested.push_back(name);
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
        auto layout = _layouts.find(name);
        if (layout != _layouts.end()) {
            int index = 0;
            for (const auto &tag : layout->second) {
                auto type = controlTypeFor(tag);
                generated::GUI_CONTROLS definition;
                definition.CONTROLTYPE = static_cast<int>(type);
                definition.TAG = tag;
                definition.EXTENT = {20, 10 * (index % 50), 16 * (index / 50), 20};
                definition.HILIGHT.emplace();
                auto control = std::shared_ptr<Control>(gui->newControl(type, tag));
                control->load(definition);
                gui->addControlToBack(control, IGUI::ControlCoordinates::Authored);
                ++index;
            }
        }
        loaded[name] = gui;
        return gui;
    }

    std::vector<std::string> requested;
    std::map<std::string, std::shared_ptr<IGUI>> loaded;

private:
    TestEngine &_engine;
    std::map<std::string, std::vector<std::string>> _layouts;
};

struct Session {
    TestEngine &engine;
    TagGUIs mainGuis;
    TagGUIs k2Guis;
    GUIServices guiServices;
    ServicesView services;
    GameOptions gameOptions;
    OptionsView options;
    StubConsole console;
    Game game;

    Session(TestEngine &engine, GameID gameId, HudProfile hud, bool withK2Set,
            const std::vector<std::string> &tags) :
        engine(engine),
        mainGuis(engine, {{"mipc28x6", tags}, {"mipc28x6_p", tags}}),
        k2Guis(engine, {{"mipc28x6_p", tags}}),
        guiServices(mainGuis, withK2Set ? &k2Guis : nullptr, withK2Set ? &engine.services().resource : nullptr),
        services(engine.services().game, engine.services().movie, engine.services().audio,
                 engine.services().graphics, engine.services().scene, guiServices,
                 engine.services().script, engine.services().resource, engine.services().system),
        options(gameOptions, engine.options().graphics, engine.options().audio),
        game(gameId, "", options, services, console) {
        gameOptions.hud = hud;
    }
};

std::vector<std::string> without(std::vector<std::string> tags, std::initializer_list<const char *> removed) {
    for (auto tag : removed) {
        tags.erase(std::remove(tags.begin(), tags.end(), std::string(tag)), tags.end());
    }
    return tags;
}

class HUDTags : public Test {
protected:
    TestEngine &engine {testEngine()};

    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().textures(), get(_, _)).Times(AnyNumber());
        EXPECT_CALL(static_cast<MockFonts &>(engine.services().resource.fonts), get(_)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().strings(), getText(_)).Times(AnyNumber());
        EXPECT_CALL(engine.resourceModule().audioClips(), get(_)).Times(AnyNumber());
    }

    /** Inits a HUD and runs every per-frame path that touches bound controls. */
    static void exercise(HUD &hud) {
        hud.init();
        HUDTagTestAccess::updateParty(hud);
        HUDTagTestAccess::toggleCombat(hud, true);
        HUDTagTestAccess::refreshQueue(hud);
        HUDTagTestAccess::toggleCombat(hud, false);
        hud.activateStatusSummaryIndicator(StatusSummaryCategory::Journal);
        hud.resetStatusSummaryPresentation();
    }

    static void expectBound(IGUI &gui, const std::vector<std::string> &tags) {
        for (const auto &tag : tags) {
            EXPECT_TRUE(gui.findControl(tag)) << tag;
        }
    }
};

} // namespace

TEST_F(HUDTags, tag_lists_differ_by_the_documented_controls) {
    auto k1 = HUD::expectedTags(false);
    auto k2 = HUD::expectedTags(true);
    auto has = [](const std::vector<std::string> &tags, const char *tag) {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    };
    for (auto tag : {"BTN_SWAPWEAPONS", "BTN_ACTION4", "BTN_ACTION5", "LBL_ACTION5", "BTN_ACTIONUP5", "BTN_ACTIONDOWN4"}) {
        EXPECT_TRUE(has(k2, tag)) << tag;
        EXPECT_FALSE(has(k1, tag)) << tag;
    }
    for (auto tag : {"LBL_COMBATBG1", "LBL_COMBATBG2", "LBL_LVLUPBG1", "LBL_LVLUPBG3", "LBL_MOULDING2"}) {
        EXPECT_TRUE(has(k1, tag)) << tag;
        EXPECT_FALSE(has(k2, tag)) << tag;
    }
}

TEST_F(HUDTags, k1_hud_binds_the_k1_layout) {
    auto tags = HUD::expectedTags(false);
    Session session(engine, GameID::KotOR, HudProfile::Default, false, tags);
    HUD hud(session.game, session.services);
    exercise(hud);
    EXPECT_FALSE(HUDTagTestAccess::presentsTSL(hud));
    ASSERT_EQ("mipc28x6", session.mainGuis.requested.front());
    expectBound(*HUDTagTestAccess::gui(hud), tags);
}

TEST_F(HUDTags, k2_hud_in_k1_session_binds_the_k2_layout_from_the_k2_set) {
    auto tags = HUD::expectedTags(true);
    Session session(engine, GameID::KotOR, HudProfile::K2, true, tags);
    HUD hud(session.game, session.services);
    exercise(hud);
    EXPECT_TRUE(HUDTagTestAccess::presentsTSL(hud));
    // Game rules stay K1.
    EXPECT_FALSE(session.game.isTSL());
    ASSERT_FALSE(session.k2Guis.requested.empty());
    EXPECT_EQ("mipc28x6_p", session.k2Guis.requested.front());
    // The HUD family never loads through the main loader in this profile.
    EXPECT_TRUE(session.mainGuis.requested.empty());
    auto gui = HUDTagTestAccess::gui(hud);
    expectBound(*gui, tags);
    auto swap = std::static_pointer_cast<ToggleButton>(gui->findControl("BTN_SWAPWEAPONS"));
    ASSERT_TRUE(swap);
    EXPECT_TRUE(swap->isVisible());
    // No leader: the click is a no-op, not a crash.
    swap->handleClick(0, 0);
}

TEST_F(HUDTags, k2_profile_without_k2_set_stays_k1) {
    auto tags = HUD::expectedTags(false);
    Session session(engine, GameID::KotOR, HudProfile::K2, false, tags);
    HUD hud(session.game, session.services);
    exercise(hud);
    EXPECT_FALSE(HUDTagTestAccess::presentsTSL(hud));
    EXPECT_EQ("mipc28x6", session.mainGuis.requested.front());
}

TEST_F(HUDTags, mismatched_layouts_and_missing_controls_never_crash) {
    const std::vector<std::vector<std::string>> layouts {
        HUD::expectedTags(true),                  // K2 layout read on the K1 code path
        HUD::expectedTags(false),                 // K1 layout read on the K2 code path
        {},                                       // nothing but the root
        without(HUD::expectedTags(true), {"LBL_MAPBORDER", "LBL_MAPVIEW", "BTN_CLEARALL", "PB_VIT1", "LBL_QUEUE2"})};
    for (const auto &tags : layouts) {
        for (bool k2 : {false, true}) {
            SCOPED_TRACE(std::to_string(tags.size()) + (k2 ? " k2" : " k1"));
            Session session(engine, GameID::KotOR, k2 ? HudProfile::K2 : HudProfile::Default, k2, tags);
            HUD hud(session.game, session.services);
            exercise(hud);
            hud.clearCapturePresentation();
        }
    }
}
