/*
 * Copyright (c) 2020-2023 The reone project contributors
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

#pragma once

#include "reone/gui/control/button.h"
#include "reone/gui/control/label.h"
#include "reone/gui/control/listbox.h"
#include "reone/resource/types.h"
#include "reone/scene/graph.h"
#include "reone/scene/node/model.h"

#include "../gui.h"
#include "../menutheme.h"
#include "coopmenu.h"
#include "optionspanels.h"
#include "../types.h"

namespace reone {

namespace resource {
class IAudioClips;
}

namespace game {

class Creature;

/**
 * The campaign a new or loaded game belongs to. K1 is the only campaign; the
 * K2 campaign adds its row here and a selector in MainMenu::selectedCampaign.
 */
enum class Campaign {
    K1
};

class MainMenu : public GameGUI {
public:
    MainMenu(Game &game, ServicesView &services);
    /**
     * A themed main menu in a K1 session with K2 GUI resources: the theme
     * picks the layout, stage, logo and music (design note p1_k2menus).
     */
    MainMenu(Game &game, ServicesView &services, MenuTheme theme);

    std::optional<MenuTheme> theme() const { return _theme; }
    /** The idle rotation has faded out: the host should switch themes now. */
    bool themeSwitchRequested() const { return _themeSwitchRequested; }
    /** Start dark and fade in (after an idle switch). */
    void beginThemeFadeIn() { _themeFade.beginFadeIn(); }
    /** Music clips for musicResRef(): K2's for the K2 theme, else null (the main clips). */
    resource::IAudioClips *musicClips() const;
    /** Lineup names on the stage, for the harness. */
    const std::vector<std::string> &lineupNames() const { return _lineupNames; }

    void onModuleSelected(const std::string &name);
    // Rebuild K2 from the durable options snapshot on every menu entry.
    // Also callable after a host updates the persisted presentation state.
    void refreshScene();

    const std::string &musicResRef() const { return _musicResRef; }

    bool handle(const input::Event &event) override;
    void update(float dt) override;
    void render() override;

    /** The Options button: optionsmain and its pages. */
    void openOptions();
    /** The Co-op button (in the Movies slot, between Load Game and Options): Host and Join. */
    void openCoop();
    CoopMenu *coopMenu() { return _coopMenu.get(); }
    PresentationGUI *inputPage() override;
    OptionsPanels &optionsPanels() { return _optionsPanels; }

private:
    friend class MainMenuTestAccess;
    struct Controls {
        std::shared_ptr<gui::Button> BTN_EXIT;
        std::shared_ptr<gui::Button> BTN_LOADGAME;
        std::shared_ptr<gui::Button> BTN_MOREGAMES;
        std::shared_ptr<gui::Button> BTN_MOVIES;
        std::shared_ptr<gui::Button> BTN_MUSIC;
        std::shared_ptr<gui::Button> BTN_NEWGAME;
        std::shared_ptr<gui::Button> BTN_OPTIONS;
        std::shared_ptr<gui::Button> BTN_TSLRCM;
        std::shared_ptr<gui::Button> BTN_WARP;
        std::shared_ptr<gui::Label> LBL_3DVIEW;
        std::shared_ptr<gui::Label> LBL_BW;
        std::shared_ptr<gui::Label> LBL_GAMELOGO;
        std::shared_ptr<gui::Label> LBL_LUCAS;
        std::shared_ptr<gui::Label> LBL_MENUBG;
        std::shared_ptr<gui::Label> LBL_NEWCONTENT;
        std::shared_ptr<gui::ListBox> LB_MODULES;
    };

    Controls _controls;

    std::string _musicResRef;
    std::shared_ptr<Creature> _leader;
    OptionsPanels _optionsPanels;
    std::unique_ptr<CoopMenu> _coopMenu;

    std::optional<MenuTheme> _theme;
    MenuThemeFade _themeFade;
    bool _themeSwitchRequested {false};
    std::vector<std::string> _lineupNames;

protected:
    void preload(gui::IGUI &gui) override;

private:
    void onGUILoaded() override;

    void bindControls() {
        _controls.BTN_EXIT = findControl<gui::Button>("BTN_EXIT");
        _controls.BTN_LOADGAME = findControl<gui::Button>("BTN_LOADGAME");
        _controls.BTN_MOREGAMES = findControl<gui::Button>("BTN_MOREGAMES");
        _controls.BTN_MOVIES = findControl<gui::Button>("BTN_MOVIES");
        _controls.BTN_MUSIC = findControl<gui::Button>("BTN_MUSIC");
        _controls.BTN_NEWGAME = findControl<gui::Button>("BTN_NEWGAME");
        _controls.BTN_OPTIONS = findControl<gui::Button>("BTN_OPTIONS");
        _controls.BTN_TSLRCM = findControl<gui::Button>("BTN_TSLRCM");
        _controls.BTN_WARP = findControl<gui::Button>("BTN_WARP");
        _controls.LBL_3DVIEW = findControl<gui::Label>("LBL_3DVIEW");
        _controls.LBL_BW = findControl<gui::Label>("LBL_BW");
        _controls.LBL_GAMELOGO = findControl<gui::Label>("LBL_GAMELOGO");
        _controls.LBL_LUCAS = findControl<gui::Label>("LBL_LUCAS");
        _controls.LBL_MENUBG = findControl<gui::Label>("LBL_MENUBG");
        _controls.LBL_NEWCONTENT = findControl<gui::Label>("LBL_NEWCONTENT");
        _controls.LB_MODULES = findControl<gui::ListBox>("LB_MODULES");
    }

    void configureButtons();
    void buildThemedScene();
    void addLineup(scene::ISceneGraph &sceneGraph, const glm::mat4 &placement);
    std::shared_ptr<scene::ModelSceneNode> buildLineupCharacter(scene::ISceneGraph &sceneGraph,
                                                                resource::ResourceServices &resources,
                                                                const std::string &appearance);
    resource::ResourceServices &themeSceneResources() const;
    void renderThemeFade();

    /** The campaign New Game and Load Game act on. The campaign selector goes here. */
    Campaign selectedCampaign() const;
    void startNewGame(Campaign campaign);
    void openLoadGame(Campaign campaign);
    void setup3DView();
    void setButtonColors(gui::Control &control);
    void startModuleSelection();
    void loadModuleNames();

    std::shared_ptr<scene::ModelSceneNode> getKotorModel(scene::ISceneGraph &sceneGraph);
};

} // namespace game

} // namespace reone
