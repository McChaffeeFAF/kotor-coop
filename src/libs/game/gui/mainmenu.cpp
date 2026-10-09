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

#include "reone/game/gui/mainmenu.h"

#include "reone/audio/mixer.h"
#include "reone/game/di/services.h"
#include "reone/game/game.h"
#include "reone/game/party.h"
#include "reone/game/object/creature.h"
#include "reone/game/types.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/model.h"
#include "reone/gui/sceneinitializer.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/textures.h"
#include "reone/scene/di/services.h"
#include "reone/scene/graphs.h"
#include "reone/scene/node/camera.h"
#include "reone/scene/types.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/game/usersettings.h"
#include "reone/graphics/context.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/di/services.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"

#include <boost/algorithm/string.hpp>

#include <array>
#include <fstream>

using namespace reone::audio;

using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::resource;
using namespace reone::scene;

namespace reone {

namespace game {

static constexpr float kKotorModelSize = 1.4f;

MainMenu::MainMenu(Game &game, ServicesView &services) :
    GameGUI(game, services),
    _optionsPanels(game, services) {

    if (_game.isTSL()) {
        _resRef = "mainmenu8x6_p";
        _musicResRef = "mus_sion";
    } else {
        _resRef = "mainmenu16x12";
        _musicResRef = "mus_theme_cult";
    }
}

static HudPresentationServices themePresentation(Game &game, ServicesView &services, MenuTheme theme) {
    if (theme == MenuTheme::K2 && services.gui.k2Guis && services.gui.k2Resources) {
        return {GameID::TSL, *services.gui.k2Guis, *services.gui.k2Resources};
    }
    return {game.gameId(), services.gui.guis, services.resource};
}

MainMenu::MainMenu(Game &game, ServicesView &services, MenuTheme theme) :
    GameGUI(game, services, themePresentation(game, services, theme)),
    _optionsPanels(game, services),
    _theme(isTSL() ? MenuTheme::K2 : MenuTheme::K1) {

    // The theme is the whole main-menu presentation of one game: layout,
    // stage, logo and music. K2's music needs the K2 scene set.
    _resRef = isTSL() ? "mainmenu8x6_p" : "mainmenu16x12";
    _musicResRef = isTSL() && _services.gui.k2SceneResources ? "mus_sion" : "mus_theme_cult";
}

resource::IAudioClips *MainMenu::musicClips() const {
    if (_theme == MenuTheme::K2 && _services.gui.k2SceneResources) {
        return &_services.gui.k2SceneResources->audioClips;
    }
    return nullptr;
}

ResourceServices &MainMenu::themeSceneResources() const {
    if (_theme == MenuTheme::K2 && _services.gui.k2SceneResources) {
        return *_services.gui.k2SceneResources;
    }
    return _services.resource;
}

void MainMenu::preload(IGUI &gui) {
    GameGUI::preload(gui);
    gui.setResolution(800, 600);
}

void MainMenu::onGUILoaded() {
    // On a wide screen the logos keep their aspect; the stage, the buttons
    // and the module list stretch.
    for (auto tag : {"LBL_GAMELOGO", "LBL_BW", "LBL_LUCAS"}) {
        _gui->setControlKeepsAspect(tag, true);
    }
    _gui->refreshLayout();
    bindControls();

    if (_theme) {
        // K1's theme draws its starfield backdrop behind the stage; K2's
        // stage room is its own backdrop.
        if (!isTSL()) {
            loadBackground(BackgroundType::Menu);
        }
        // K2-only: the music player has no K1 content, so it is hidden, and
        // the buttons below it close the gap.
        if (_controls.BTN_MUSIC) {
            _controls.BTN_MUSIC->setVisible(false);
            int step = _controls.BTN_OPTIONS->extent().top - _controls.BTN_MUSIC->extent().top;
            for (auto &button : {_controls.BTN_OPTIONS, _controls.BTN_EXIT}) {
                auto extent = button->extent();
                extent.top -= step;
                button->setExtent(extent);
            }
        }
    } else if (!_game.isTSL()) {
        loadBackground(BackgroundType::Menu);
    }

    _controls.LB_MODULES->setVisible(false);
    _controls.LBL_NEWCONTENT->setVisible(false);
    _controls.LBL_BW->setVisible(false);
    _controls.LBL_LUCAS->setVisible(false);
    // Co-op (co-op design notes, "Menus") takes the Movies slot, between Load
    // Game and Options: this engine has no movie browser, so the slot was a
    // disabled button.
    _controls.BTN_MOVIES->setTextMessage("Co-op");
    _controls.BTN_MOVIES->setDisabled(false);
    _controls.BTN_MOVIES->setOnClick([this]() {
        openCoop();
    });
    _controls.BTN_OPTIONS->setOnClick([this]() {
        openOptions();
    });

    if (_controls.BTN_MOREGAMES) {
        _controls.BTN_MOREGAMES->setVisible(false);
    }
    if (_controls.BTN_TSLRCM) {
        _controls.BTN_TSLRCM->setVisible(false);
    }

    // Hide warp button in developer mode
    if (!_game.options().game.developer) {
        _controls.BTN_WARP->setVisible(false);
    }

    _controls.BTN_NEWGAME->setOnClick([this]() {
        startNewGame(selectedCampaign());
    });
    _controls.BTN_LOADGAME->setOnClick([this]() {
        openLoadGame(selectedCampaign());
    });
    _controls.BTN_EXIT->setOnClick([this]() {
        _game.quit();
    });
    _controls.BTN_WARP->setOnClick([this]() {
        startModuleSelection();
    });
    _controls.LB_MODULES->setOnItemClick([this](const std::string &item) {
        onModuleSelected(item);
    });

    setup3DView();
    configureButtons();
}

void MainMenu::configureButtons() {
    setButtonColors(*_controls.BTN_EXIT);
    setButtonColors(*_controls.BTN_LOADGAME);
    setButtonColors(*_controls.BTN_MOVIES);
    setButtonColors(*_controls.BTN_NEWGAME);
    setButtonColors(*_controls.BTN_OPTIONS);

    if (_controls.BTN_MUSIC) {
        setButtonColors(*_controls.BTN_MUSIC);
    }
}

Campaign MainMenu::selectedCampaign() const {
    // Campaign selector hook: when the K2 campaign is added, the choice the
    // player makes on the main menu is returned here. K1 is the only one.
    return Campaign::K1;
}

void MainMenu::startNewGame(Campaign campaign) {
    switch (campaign) {
    case Campaign::K1:
    default:
        _game.startCharacterGeneration();
        break;
    }
}

void MainMenu::openLoadGame(Campaign campaign) {
    switch (campaign) {
    case Campaign::K1:
    default:
        _game.openSaveLoad(SaveLoadMode::LoadFromMainMenu);
        break;
    }
}

void MainMenu::setButtonColors(Control &control) {
    control.setTextColor(_baseColor);
    control.setHilightColor(_hilightColor);
}

void MainMenu::setup3DView() {
    if (_theme) {
        buildThemedScene();
        return;
    }
    if (_game.isTSL()) {
        refreshScene();
        return;
    }

    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const Control::Extent &extent = _controls.LBL_3DVIEW->extent();
    float aspect = extent.width / static_cast<float>(extent.height);

    SceneInitializer(sceneGraph)
        .aspect(aspect)
        .depth(kDefaultClipPlaneNear, 10.0f)
        .modelSupplier(bind(&MainMenu::getKotorModel, this, std::placeholders::_1))
        .modelScale(kKotorModelSize)
        .cameraFromModelNode("camerahook")
        .invoke();

    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);
}

void MainMenu::refreshScene() {
    if (_theme) {
        if (_controls.LBL_3DVIEW) {
            buildThemedScene();
        }
        return;
    }
    if (!_game.isTSL() || !_controls.LBL_3DVIEW) return;

    if (_controls.LB_MODULES && _controls.LB_MODULES->isVisible()) {
        _controls.LB_MODULES->setVisible(false);
        for (auto &button : {_controls.BTN_EXIT, _controls.BTN_LOADGAME,
                 _controls.BTN_MOVIES, _controls.BTN_NEWGAME, _controls.BTN_OPTIONS,
                 _controls.BTN_MUSIC}) {
            button->setVisible(true);
        }
        _controls.BTN_WARP->setVisible(_game.options().game.developer);
        _controls.LBL_GAMELOGO->setVisible(true);
    }
    _controls.LBL_3DVIEW->setVisible(true);

    // clear() drops roots but keeps the node arena. Replace only this scene so
    // repeated returns also release obsolete emitters, cameras and listeners.
    _leader.reset();
    _services.scene.graphs.reset(kSceneMainMenu);
    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);
    const auto &state = _game.options().game.menuPresentation;
    const auto &extent = _controls.LBL_3DVIEW->extent();
    auto model = _services.resource.models.get(state.modelResRef(true));
    if (!model) return;
    auto environment = sceneGraph.newModel(*model, ModelUsage::GUI);
    if (!environment) return;
    SceneInitializer(sceneGraph)
        .aspect(extent.width / static_cast<float>(extent.height))
        .depth(0.1f, 10000.0f)
        .perspective(glm::radians(22.7259998f))
        .modelScale(1.0f)
        .prewarmEmitters()
        .modelSupplier([environment](ISceneGraph &) { return environment; })
        .invoke();
    if (auto room = _services.resource.models.get("gui3d_room")) {
        sceneGraph.clear();
        sceneGraph.addRoot(sceneGraph.newModel(*room, ModelUsage::Room));
        sceneGraph.addRoot(environment);
    }
    // mainmenu03 also has a character-local camerahook. Select the scene's
    // direct child by number, avoiding the duplicate-name lookup entirely.
    if (auto camera = sceneGraph.camera()) {
        for (const auto &child : model->rootNode()->children()) {
            if (child->name() == "camerahook") {
                environment->getNodeByNumber(child->number())->addChild(camera->get());
                break;
            }
        }
    }
    environment->playAnimation("default", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);

    if (MenuPresentation::validateSelector(state.selector) == 4 && state.leader) {
        try {
            _leader = _game.newPresentationCreature(kSceneMainMenu);
            _leader->setPresentation(*state.leader);
            auto placement = environment->getNodeByName("cutscenedummy");
            _leader->setPosition(placement ? glm::vec3(placement->absoluteTransform()[3]) : glm::vec3(0.0f, -1.0f, 0.0f));
            // The authored menu actor faces -Y, toward the camera.
            _leader->setFacing(glm::pi<float>());
            _leader->loadAppearance();
            auto node = std::static_pointer_cast<ModelSceneNode>(_leader->sceneNode());
            if (node) {
                // Resolve through the supermodel and propagate to the head,
                // without any runtime action or AI state.
                auto animation = node->model().getAnimation("evil") ? "evil" : "pause1";
                if (std::string_view(animation) != "evil") {
                    warn("Main menu leader model has no evil animation");
                }
                node->playAnimation(animation, nullptr,
                    AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
                sceneGraph.addRoot(node);
            } else {
                _leader.reset();
            }
        } catch (const std::exception &ex) {
            _leader.reset();
            warn(std::string("Could not build main menu leader: ") + ex.what());
        }
    }
}

std::shared_ptr<ModelSceneNode> MainMenu::getKotorModel(ISceneGraph &sceneGraph) {
    auto model = _services.resource.models.get("mainmenu");
    if (!model) {
        return nullptr;
    }
    return sceneGraph.newModel(*model, ModelUsage::GUI);
}

void MainMenu::openOptions() {
    _optionsPanels.open(guiResRef("optionsmain"));
}

PresentationGUI *MainMenu::inputPage() {
    if (_coopMenu && _coopMenu->isOpen()) {
        return _coopMenu->inputPage();
    }
    return this;
}

void MainMenu::openCoop() {
    if (!_coopMenu) {
        auto menu = std::make_unique<CoopMenu>(_game, _services, CoopMenu::Place::MainMenu);
        try {
            menu->init();
        } catch (const std::exception &e) {
            warn(std::string("Co-op screen did not load: ") + e.what());
            return;
        }
        menu->setHostActions([this]() { startNewGame(selectedCampaign()); },
                             [this]() { openLoadGame(selectedCampaign()); });
        _coopMenu = std::move(menu);
    }
    _coopMenu->open();
}

bool MainMenu::handle(const input::Event &event) {
    // Any input holds off the idle theme rotation.
    _themeFade.resetIdle();
    if (_optionsPanels.isOpen()) {
        _optionsPanels.handle(event);
        return true;
    }
    if (_coopMenu && _coopMenu->isOpen()) {
        return _coopMenu->handle(event);
    }
    return GameGUI::handle(event);
}

void MainMenu::update(float dt) {
    if (_theme) {
        bool idleMenu = !_optionsPanels.isOpen() && !(_coopMenu && _coopMenu->isOpen()) &&
                        !(_controls.LB_MODULES && _controls.LB_MODULES->isVisible());
        bool rotate = idleMenu && _game.menuThemeMode() == MenuThemeMode::Rotate;
        if (_themeFade.update(dt, rotate)) {
            _themeSwitchRequested = true;
        }
    }
    if (_optionsPanels.isOpen()) {
        _optionsPanels.update(dt);
        return;
    }
    if (_coopMenu && _coopMenu->isOpen()) {
        _coopMenu->update(dt);
        return;
    }
    GameGUI::update(dt);
}

void MainMenu::render() {
    if (_optionsPanels.isOpen()) {
        _optionsPanels.render();
        return;
    }
    if (_coopMenu && _coopMenu->isOpen()) {
        _coopMenu->render();
        return;
    }
    GameGUI::render();
    renderThemeFade();
}

void MainMenu::startModuleSelection() {
    loadModuleNames();

    _controls.LB_MODULES->setVisible(true);
    _controls.BTN_EXIT->setVisible(false);
    _controls.BTN_LOADGAME->setVisible(false);
    _controls.BTN_MOVIES->setVisible(false);
    _controls.BTN_NEWGAME->setVisible(false);
    _controls.BTN_OPTIONS->setVisible(false);
    _controls.BTN_WARP->setVisible(false);
    _controls.LBL_3DVIEW->setVisible(false);
    _controls.LBL_GAMELOGO->setVisible(false);

    if (_controls.BTN_MUSIC) {
        _controls.BTN_MUSIC->setVisible(false);
    }
    if (_controls.LBL_MENUBG) {
        _controls.LBL_MENUBG->setVisible(false);
    }
}

void MainMenu::loadModuleNames() {
    for (auto &module : _game.moduleNames()) {
        ListBox::Item item;
        item.tag = module;
        item.text = module;
        _controls.LB_MODULES->addItem(std::move(item));
    }
}

void MainMenu::onModuleSelected(const std::string &name) {
    _game.loadModule(name);
}

namespace {

// Idle loops tried in order; creature supermodels carry the first.
constexpr std::array<const char *, 3> kLineupAnimations {{"pause1", "pause2", "default"}};

std::vector<MenuLineupEntry> loadMenuLineup(const std::filesystem::path &settingsPath) {
    if (!settingsPath.empty()) {
        auto path = settingsPath.parent_path() / kMenuLineupFilename;
        std::ifstream input(path);
        if (input) {
            auto parsed = parseMenuLineup(input);
            if (!parsed.empty()) {
                return parsed;
            }
            warn("Main menu lineup file has no valid rows, using the default: " + path.string());
        }
    }
    return defaultMenuLineup();
}

/** Scopes the textures of nodes created on a stage to one game's resources. */
class NodeResourcesScope {
public:
    NodeResourcesScope(ISceneGraph &sceneGraph, ResourceServices *resources) :
        _sceneGraph(sceneGraph) {
        _sceneGraph.setNodeResources(resources);
    }
    ~NodeResourcesScope() { _sceneGraph.setNodeResources(nullptr); }

private:
    ISceneGraph &_sceneGraph;
};

} // namespace

void MainMenu::buildThemedScene() {
    _leader.reset();
    _lineupNames.clear();
    _services.scene.graphs.reset(kSceneMainMenu);
    auto &sceneGraph = _services.scene.graphs.get(kSceneMainMenu);

    // The stage: K2's is its empty menu stage, K1's the menu stage with its
    // own character hidden. Both carry the camera hook and placement dummy.
    auto &stageResources = themeSceneResources();
    bool k2Stage = &stageResources != &_services.resource;
    NodeResourcesScope stageScope(sceneGraph, k2Stage ? &stageResources : nullptr);
    auto model = stageResources.models.get(k2Stage ? "mainmenu05" : "mainmenu");
    if (!model) {
        return;
    }
    auto environment = sceneGraph.newModel(*model, ModelUsage::GUI);
    if (!environment) {
        return;
    }
    const auto &extent = _controls.LBL_3DVIEW->extent();
    SceneInitializer(sceneGraph)
        .aspect(extent.width / static_cast<float>(extent.height))
        .depth(0.1f, 10000.0f)
        .perspective(glm::radians(22.7259998f))
        .modelScale(1.0f)
        .prewarmEmitters()
        .modelSupplier([environment](ISceneGraph &) { return environment; })
        .invoke();
    if (k2Stage) {
        if (auto room = stageResources.models.get("gui3d_room")) {
            sceneGraph.clear();
            sceneGraph.addRoot(sceneGraph.newModel(*room, ModelUsage::Room));
            sceneGraph.addRoot(environment);
        }
    }
    if (auto camera = sceneGraph.camera()) {
        for (const auto &child : model->rootNode()->children()) {
            if (child->name() == "camerahook") {
                environment->getNodeByNumber(child->number())->addChild(camera->get());
                break;
            }
        }
    }
    environment->playAnimation("default", nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));

    glm::vec3 placement(0.0f, -1.0f, 0.0f);
    if (auto dummy = environment->getNodeByName("cutscenedummy")) {
        placement = glm::vec3(dummy->absoluteTransform()[3]);
        // The stage's own character hangs under the dummy...
        dummy->setEnabled(false);
    }
    // ...and its skinned meshes under the character's hook. Skinned meshes
    // are not children of the hook in the scene tree, so every node of the
    // hook's model subtree is disabled by name.
    std::function<void(const ModelNode &)> hideSubtree = [&](const ModelNode &node) {
        if (auto sceneNode = environment->getNodeByName(node.name())) {
            sceneNode->setEnabled(false);
        }
        for (const auto &child : node.children()) {
            hideSubtree(*child);
        }
    };
    for (const auto &child : model->rootNode()->children()) {
        auto name = boost::to_lower_copy(child->name());
        if (name != "camerahook" && boost::ends_with(name, "hook")) {
            hideSubtree(*child);
        }
    }
    sceneGraph.setNodeResources(nullptr);
    addLineup(sceneGraph, glm::translate(glm::mat4(1.0f), placement));
    _controls.LBL_3DVIEW->setSceneName(kSceneMainMenu);
}

void MainMenu::addLineup(ISceneGraph &sceneGraph, const glm::mat4 &placement) {
    // One character at a time, on the stage's own spot, rotating through the
    // lineup: overlapping several in a one-character scene reads as a mix-up.
    auto entries = loadMenuLineup(_game.userSettings().path());
    auto *k2Resources = _services.gui.k2SceneResources;
    auto last = _game.userSettings().get(kMenuLastCharacterKey);
    auto next = nextMenuCharacter(entries, k2Resources != nullptr, last.value_or(""));
    if (!next) {
        return;
    }
    const auto &entry = entries[*next];
    _game.userSettings().set(kMenuLastCharacterKey, entry.name);
    _game.userSettings().save();
    auto &resources = entry.source == MenuTheme::K2 ? *k2Resources : _services.resource;
    std::shared_ptr<ModelSceneNode> character;
    {
        NodeResourcesScope scope(sceneGraph, entry.source == MenuTheme::K2 ? k2Resources : nullptr);
        try {
            character = buildLineupCharacter(sceneGraph, resources, entry.appearance);
        } catch (const std::exception &ex) {
            warn("Main menu lineup: " + entry.name + ": " + ex.what());
        }
    }
    if (!character) {
        warn("Main menu lineup: no model for " + entry.name);
        return;
    }
    // Menu actors face -Y, toward the camera.
    character->setLocalTransform(glm::rotate(placement, glm::pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f)));
    sceneGraph.addRoot(character);
    _lineupNames.push_back(entry.name);
}

std::shared_ptr<ModelSceneNode> MainMenu::buildLineupCharacter(ISceneGraph &sceneGraph,
                                                               ResourceServices &resources,
                                                               const std::string &appearanceLabel) {
    auto appearance = resources.twoDas.get("appearance");
    if (!appearance) {
        return nullptr;
    }
    int row = appearance->indexByCellValue("label", appearanceLabel);
    if (row < 0) {
        return nullptr;
    }
    // Character models (type B) wear their body B, the character's own
    // clothes; full models carry everything in the race column.
    bool character = boost::iequals(appearance->getString(row, "modeltype"), "B");
    std::string bodyName = boost::to_lower_copy(appearance->getString(row, character ? "modelb" : "race"));
    if (bodyName.empty()) {
        bodyName = boost::to_lower_copy(appearance->getString(row, "race"));
    }
    auto bodyModel = bodyName.empty() ? nullptr : resources.models.get(bodyName);
    if (!bodyModel) {
        return nullptr;
    }
    auto body = sceneGraph.newModel(*bodyModel, ModelUsage::Creature);
    if (!body) {
        return nullptr;
    }
    std::string texName = boost::to_lower_copy(appearance->getString(row, character ? "texb" : "racetex"));
    if (!texName.empty()) {
        auto texture = resources.textures.get(character ? texName + "01" : texName, TextureUsage::MainTex);
        if (!texture && character) {
            texture = resources.textures.get(texName, TextureUsage::MainTex);
        }
        if (texture) {
            body->setMainTexture(texture.get());
        }
    }
    if (character) {
        int headRow = appearance->getInt(row, "normalhead", -1);
        auto heads = headRow >= 0 ? resources.twoDas.get("heads") : nullptr;
        std::string headName = heads ? boost::to_lower_copy(heads->getString(headRow, "head")) : "";
        if (auto headModel = headName.empty() ? nullptr : resources.models.get(headName)) {
            auto head = sceneGraph.newModel(*headModel, ModelUsage::Creature);
            body->attach("headhook", *head);
        }
    }
    for (const char *animation : kLineupAnimations) {
        if (body->model().getAnimation(animation)) {
            body->playAnimation(animation, nullptr,
                                AnimationProperties::fromFlags(AnimationFlags::loop | AnimationFlags::propagate));
            break;
        }
    }
    return body;
}

void MainMenu::renderThemeFade() {
    float alpha = _themeFade.alpha();
    if (alpha <= 0.0f) {
        return;
    }
    auto &graphics = _services.graphics;
    const auto &options = _game.options().graphics;
    graphics.uniforms.setGlobals([&](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(options.width),
                                        static_cast<float>(options.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    graphics.context.withDepthTestMode(DepthTestMode::None, [&]() {
        graphics.context.withBlendMode(BlendMode::Normal, [&]() {
            graphics.uniforms.setLocals([&](auto &locals) {
                locals.reset();
                locals.model = glm::scale(glm::mat4(1.0f), glm::vec3(options.width, options.height, 1.0f));
                locals.color = glm::vec4(0.0f, 0.0f, 0.0f, alpha);
            });
            graphics.context.useProgram(graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
            graphics.meshRegistry.get(MeshName::quad).draw(graphics.statistic);
        });
    });
}

} // namespace game

} // namespace reone
