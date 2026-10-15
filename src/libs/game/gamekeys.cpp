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

// Player settings, key bindings and selection cycling (design note p1_ui).

#include "reone/game/game.h"
#include "reone/game/action/switchweapons.h"

#include "reone/game/coopsession.h"
#include "reone/graphics/preset.h"
#include "reone/resource/provider/textures.h"

#include "reone/game/gui/hud.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/resource/2da.h"
#include "reone/resource/provider/2das.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/system/logutil.h"

#include <algorithm>

namespace reone {

namespace game {

void Game::loadUserSettings() {
    // Defaults come from the running game's keymap.2da.
    std::vector<std::pair<std::string, std::string>> rows;
    try {
        if (auto table = _services.resource.twoDas.get("keymap")) {
            for (int row = 0; row < table->getRowCount(); ++row) {
                if (table->getInt(row, "disabled", 0) != 0 || table->getInt(row, "icpc", 0) == 0) {
                    continue;
                }
                rows.emplace_back(table->getString(row, "name"), table->getString(row, "character"));
            }
        }
    } catch (const std::exception &e) {
        warn(std::string("Keymap: keymap.2da did not load: ") + e.what());
    }
    _keymap.loadDefaults(rows);

    _launchOptions.audio = _options.audio;
    _launchOptions.difficulty = _options.game.clientDifficulty;
    _launchOptions.grass = _options.graphics.grass;
    _launchOptions.shadows = _options.graphics.shadows;
    _launchOptions.vsync = _options.graphics.vsync;
    _launchOptions.smartCompanions = _options.game.smartCompanions;
    _launchOptions.graphics = _options.graphics;

    // Our own file next to reone.cfg, in the build folder; never the install.
    if (!_options.game.configurationPath.empty()) {
        _userSettings.load(_options.game.configurationPath.parent_path() / UserSettings::kFilename);
    }
    applyUserSettings(false);
}

void Game::applyUserSettings(bool save) {
    auto &s = _userSettings;
    const auto &launch = _launchOptions;
    auto volume = [&s](const char *key, int launchValue) {
        return std::clamp(s.getInt(key, launchValue), 0, 100);
    };
    _options.audio.musicVolume = volume("audio.music", launch.audio.musicVolume);
    _options.audio.voiceVolume = volume("audio.voice", launch.audio.voiceVolume);
    _options.audio.soundVolume = volume("audio.sound", launch.audio.soundVolume);
    _options.audio.movieVolume = volume("audio.movie", launch.audio.movieVolume);

    _options.game.clientDifficulty = static_cast<uint8_t>(
        std::clamp(s.getInt("gameplay.difficulty", launch.difficulty), 0, 2));

    _options.graphics.grass = s.getBool("optgraphics.CB_GRASS", launch.grass);
    _options.graphics.shadows = s.getBool("optgraphics.CB_SHADOWS", launch.shadows);
    _options.graphics.vsync = s.getBool("optgraphicsadv.CB_VSYNC", launch.vsync);
    _options.game.smartCompanions = s.getBool(kSmartCompanionsSetting, launch.smartCompanions);

    // Enhanced preset features (Options > Graphics > Advanced); unset keys
    // keep the launch values, so with no settings the look is unchanged.
    auto &gfx = _options.graphics;
    const auto &launchGfx = launch.graphics;
    int previousAnisotropy = gfx.anisotropicFiltering;
    gfx.bloom = s.getBool(kGraphicsBloomSetting, launchGfx.bloom);
    gfx.gtao = s.getBool(kGraphicsAOSetting, launchGfx.gtao);
    gfx.softShadows = s.getBool(kGraphicsSoftShadowsSetting, launchGfx.softShadows);
    gfx.sharpen = s.getBool(kGraphicsSharpenSetting, launchGfx.sharpen);
    for (const auto &toggle : graphics::graphicsFeatureToggles()) {
        gfx.*toggle.field = s.getBool(graphicsFeatureSetting(toggle.settingTag), launchGfx.*toggle.field);
    }
    gfx.shadowResolution = s.getBool(kGraphicsHiResShadowsSetting, launchGfx.shadowResolution > 2048) ? std::max(4096, launchGfx.shadowResolution) : std::min(2048, launchGfx.shadowResolution);
    int aa = std::clamp(s.getInt(kGraphicsAntiAliasingSetting, static_cast<int>(graphics::antiAliasing(launchGfx))), 0, 3);
    graphics::setAntiAliasing(gfx, static_cast<graphics::AntiAliasing>(aa));
    gfx.anisotropicFiltering = std::clamp(s.getInt(kGraphicsAnisotropySetting, launchGfx.anisotropicFiltering), 0, 4);
    if (gfx.anisotropicFiltering != previousAnisotropy) {
        _services.resource.textures.applyAnisotropy();
    }

    _selectionRadius = std::clamp(s.getFloat("select.radius", kDefaultSelectionRadius),
                                  kMinSelectionRadius, kMaxSelectionRadius);
    _fixedActionBar = s.getBool("hud.fixedActionBar", true);

    _keymap.resetToDefaults();
    _keymap.applySettings(s);
    _padBindings.resetToDefaults();
    _padBindings.applySettings(s);
    _padInput.setDeadZone(s.getFloat("pad.deadzone", 0.25f));

    if (save) {
        _userSettings.save();
    }
}

void Game::cycleSelection(bool previous) {
    if (!_module) {
        return;
    }
    auto area = _module->area();
    auto camera = getActiveCamera();
    auto leader = _party.getLeader();
    if (!area || !camera || !leader) {
        return;
    }
    glm::mat4 projection(camera->cameraSceneNode()->camera()->projection());
    glm::mat4 view(camera->cameraSceneNode()->camera()->view());
    glm::vec3 cameraPos(camera->sceneNode()->origin());
    float radius2 = _selectionRadius * _selectionRadius;

    // On-screen selectable objects in range, left to right.
    std::vector<std::pair<float, std::shared_ptr<Object>>> candidates;
    for (const auto &object : area->objects()) {
        if (!object || object == leader || !object->isSelectable()) {
            continue;
        }
        if (_party.isMember(*object)) {
            continue;
        }
        glm::vec3 position(object->getSelectablePosition());
        if (glm::distance2(cameraPos, position) > radius2) {
            continue;
        }
        glm::vec3 screen = area->getSelectableScreenCoords(object, projection, view);
        if (screen.z >= 1.0f || screen.x < 0.0f || screen.x > 1.0f || screen.y < 0.0f || screen.y > 1.0f) {
            continue;
        }
        candidates.emplace_back(screen.x, object);
    }
    if (candidates.empty()) {
        return;
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](auto &a, auto &b) {
        return a.first < b.first;
    });
    auto selected = area->selectedObject();
    int index = -1;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].second == selected) {
            index = static_cast<int>(i);
            break;
        }
    }
    int count = static_cast<int>(candidates.size());
    int next;
    if (index < 0) {
        next = previous ? count - 1 : 0;
    } else {
        next = (index + (previous ? count - 1 : 1)) % count;
    }
    area->selectObject(candidates[next].second);
}

void Game::togglePauseAsKey() {
    // Co-op: anyone pauses for everyone, shown with who paused.
    if (_coop && _coop->handlePauseKey()) {
        return;
    }
    setPaused(!isPaused());
}

void Game::toggleSoloModeAsKey() {
    if (_coop && _coop->isClient()) {
        return; // co-op: the party's follow mode is the host's
    }
    _party.setSoloMode(!_party.isSoloMode());
}

bool Game::dispatchKeyAction(KeyAction action) {
    auto leader = _party.getLeader();
    switch (action) {
    case KeyAction::GameMenu:
        return handleEscape();
    case KeyAction::SelectPrev:
    case KeyAction::SelectNext:
        cycleSelection(action == KeyAction::SelectPrev);
        return true;
    case KeyAction::PartyActive:
        if (_options.game.developer) {
            return false; // developer builds keep V for the camera toggle
        }
        toggleSoloModeAsKey();
        return true;
    case KeyAction::Freelook:
        toggleInGameCameraType();
        return true;
    case KeyAction::Equip:
        openInGameMenu(InGameMenuTab::Equipment);
        return true;
    case KeyAction::Inventory:
        openInGameMenu(InGameMenuTab::Inventory);
        return true;
    case KeyAction::Character:
        openInGameMenu(InGameMenuTab::Character);
        return true;
    case KeyAction::SkillsAndFeats:
        openInGameMenu(InGameMenuTab::Abilities);
        return true;
    case KeyAction::Messages:
        openInGameMenu(InGameMenuTab::Messages);
        return true;
    case KeyAction::Quests:
        openInGameMenu(InGameMenuTab::Journal);
        return true;
    case KeyAction::Map:
        openInGameMenu(InGameMenuTab::Map);
        return true;
    case KeyAction::Options:
        openInGameMenu(InGameMenuTab::Options);
        return true;
    case KeyAction::PauseKey:
        togglePauseAsKey();
        return true;
    case KeyAction::TargetLeftAct:
    case KeyAction::TargetMiddleAct:
    case KeyAction::TargetRightAct:
        if (_hud) {
            _hud->activateTargetAction(static_cast<int>(action) - static_cast<int>(KeyAction::TargetLeftAct));
        }
        return true;
    case KeyAction::PersonalPowerAct:
    case KeyAction::PersonalMedicalAct:
    case KeyAction::PersonalOtherAct:
    case KeyAction::PersonalMinesAct:
        if (_hud) {
            _hud->activatePersonalAction(static_cast<int>(action) - static_cast<int>(KeyAction::PersonalPowerAct));
        }
        return true;
    case KeyAction::DefaultAction:
        if (_module) {
            if (auto selected = _module->area()->selectedObject()) {
                _module->onObjectClick(selected);
            }
        }
        return true;
    case KeyAction::CancelCombat:
        if (_coop && _coop->isClient()) {
            _coop->submitRequest(net::CommandType::Cancel, 0, 1);
        } else if (leader) {
            leader->cancelCombat();
        }
        return true;
    case KeyAction::SwitchWeapons:
        // As the HUD's swap button (both layouts).
        if (leader) {
            requestWeaponSwitch(*this, *leader);
        }
        return true;
    case KeyAction::ClearOneAction:
        if (_coop && _coop->isClient()) {
            _coop->submitRequest(net::CommandType::Cancel, 0, 2);
        } else if (leader) {
            leader->cancelLastQueuedAction();
        }
        return true;
    default:
        // Bound but not acted on yet (Quickload, Stealth): design note p1_ui.
        return false;
    }
}

bool Game::handleEscape() {
    switch (_screen) {
    case Screen::InGame:
        // keymap.2da "GUI": the game menu, on its options page.
        openInGameMenu(InGameMenuTab::Options);
        return true;
    case Screen::InGameMenu:
        openInGame();
        return true;
    case Screen::MainMenu:
    case Screen::Loading:
    case Screen::Conversation:
    case Screen::None:
        return false;
    default:
        if (auto gui = getScreenGUI()) {
            return gui->activateCancel();
        }
        return false;
    }
}

} // namespace game

} // namespace reone
