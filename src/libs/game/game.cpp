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

#include "reone/game/game.h"

#include "reone/game/coopsession.h"
#include "reone/game/savedgame.h"
#include "reone/game/savedruntime.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <numeric>

#include "reone/game/minigame.h"
#include "reone/game/object/minigameobject.h"
#include "reone/graphics/mesh.h"
#include "reone/graphics/modelnode.h"

#include "reone/audio/context.h"
#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/combat.h"
#include "reone/game/d20/classes.h"
#include "reone/game/d20/spells.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/gui/hud.h"
#include "reone/game/gui/sounds.h"
#include "reone/game/location.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/room.h"
#include "reone/game/gameclock.h"
#include "reone/game/savewidesnapshot.h"
#include "reone/game/script/routines.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/prestige.h"
#include "reone/resource/k2merge.h"
#include "reone/game/surfaces.h"
#include "reone/graphics/context.h"
#include "reone/graphics/grading.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/font.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/meshregistry.h"
#include "reone/graphics/model.h"
#include "reone/graphics/modelnode.h"
#include "reone/graphics/renderbuffer.h"
#include "reone/graphics/shaderregistry.h"
#include "reone/graphics/texture.h"
#include "reone/graphics/uniforms.h"
#include "reone/gui/gui.h"
#include "reone/movie/format/bikreader.h"
#include "reone/resource/2da.h"
#include "reone/game/object/camera.h"
#include "reone/resource/di/services.h"
#include "reone/resource/director.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/erfwriter.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/parser/gff/gvt.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/parser/gff/nfo.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/audioclips.h"
#include "reone/resource/provider/cursors.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/fonts.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/layouts.h"
#include "reone/resource/provider/lips.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/movies.h"
#include "reone/resource/provider/scripts.h"
#include "reone/resource/provider/soundsets.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/provider/walkmeshes.h"
#include "reone/resource/resources.h"
#include "reone/resource/saveworkingstate.h"
#include "reone/scene/di/services.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/graphs.h"
#include "reone/scene/interp.h"
#include "reone/scene/render/pipeline.h"
#include "reone/script/di/services.h"
#include "reone/system/frameperf.h"
#include "reone/system/binarywriter.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/fileutil.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/smallset.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/system/threadutil.h"

#include <imgui.h>

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::gui;
using namespace reone::movie;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

ModuleLoadContext resolveModuleLoadContext(
    bool initialSaveRestore,
    bool savedModuleSnapshot) {

    if (initialSaveRestore) {
        return savedModuleSnapshot
                   ? ModuleLoadContext::InitialSaveRestore
                   : ModuleLoadContext::InitialTemplateRestore;
    }
    return savedModuleSnapshot
               ? ModuleLoadContext::SavedModuleTransition
               : ModuleLoadContext::FreshModule;
}

bool restoresSavedWorld(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialSaveRestore ||
           context == ModuleLoadContext::SavedModuleTransition;
}

bool restoresSavedSession(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialTemplateRestore ||
           context == ModuleLoadContext::InitialSaveRestore;
}

bool preservesSavedPlacement(ModuleLoadContext context) {
    return context == ModuleLoadContext::InitialSaveRestore;
}

bool Game::bindEffectCreator(EffectInstance &effect) const {
    if (effect._runtimeSession &&
        *effect._runtimeSession != _runtimeSessionGeneration) {
        effect.creator.reset();
        for (auto &object : effect.objectParameterObjects) {
            object.reset();
        }
        return false;
    }
    const auto &serializedContext = effect.serializedReferenceContext;
    const bool moduleGraphReferences =
        serializedContext &&
        serializedContext->domain == SerializedIdentityDomain::ModuleGraph;
    if (moduleGraphReferences && effect._savedGraph &&
        *effect._savedGraph != _savedGraphGeneration) {
        effect.creator.reset();
        for (auto &object : effect.objectParameterObjects) {
            object.reset();
        }
        return false;
    }
    if (!effect._runtimeSession) {
        effect._runtimeSession = _runtimeSessionGeneration;
    }
    if (moduleGraphReferences && !effect._savedGraph) {
        effect._savedGraph = _savedGraphGeneration;
    }
    auto resolve = [this, &serializedContext](uint32_t id) {
        return serializedContext
                   ? resolveSerializedObjectReference(id, *serializedContext)
                   : getObjectById(id);
    };
    bool allBound = true;
    if (effect.creatorId != kSavedEffectInvalidObjectId &&
        effect.creatorId != kSavedRuntimeInvalidObjectId) {
        allBound = effect.bindCreator(resolve(effect.creatorId));
    }
    for (size_t index = 0; index < effect.objectParameters.size(); ++index) {
        uint32_t id = effect.objectParameters[index];
        if (id == kSavedEffectInvalidObjectId ||
            id == kSavedRuntimeInvalidObjectId) {
            continue;
        }
        allBound = effect.bindObjectParameter(index, resolve(id)) &&
                   allBound;
    }
    return allBound;
}

bool Game::bindSavedObjectReference(SavedObjectReference &reference) const {
    if (reference._runtimeSession && *reference._runtimeSession != _runtimeSessionGeneration) {
        reference._object.reset();
        return false;
    }
    if (reference.isInvalid()) {
        reference._object.reset();
        return false;
    }
    if (!reference._runtimeSession) {
        reference._runtimeSession = _runtimeSessionGeneration;
    }
    const auto &serializedContext = reference.serializedIdentityContext();
    const bool moduleGraphReference =
        serializedContext &&
        serializedContext->domain == SerializedIdentityDomain::ModuleGraph;
    if (moduleGraphReference && reference._savedGraph &&
        *reference._savedGraph != _savedGraphGeneration) {
        reference._object.reset();
        return false;
    }
    if (moduleGraphReference && !reference._savedGraph) {
        reference._savedGraph = _savedGraphGeneration;
    }

    auto object = serializedContext
                      ? resolveSerializedObjectReference(
                            reference.id, *serializedContext)
                      : getObjectById(reference.id);
    if (!object) {
        reference._object.reset();
        return false;
    }
    reference._object = object;
    return true;
}

std::shared_ptr<Object> Game::resolveSerializedObjectReference(
    uint32_t id,
    const SerializedIdentityContext &identityContext) const {
    switch (identityContext.domain) {
    case SerializedIdentityDomain::ModuleGraph:
        if (!_reservedSavedIdentityNamespace ||
            *_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
            return nullptr;
        }
        return getObjectBySavedId(id);
    case SerializedIdentityDomain::Template:
        // ObjectId-shaped fields in blueprints do not name instances in any
        // live serialized graph. Treating them as module references would
        // recreate the cross-domain equality assumption A1 removed.
        return nullptr;
    case SerializedIdentityDomain::DetachedRecord: {
        std::shared_ptr<Object> result;
        for (const auto &[_, candidate] : _objectById) {
            auto identity = candidate->serializedObjectIdentity();
            if (!identity || !(identity->context == identityContext) ||
                identity->id != id) {
                continue;
            }
            if (result && result.get() != candidate.get()) {
                throw ValidationException(
                    "Ambiguous detached-record object reference " +
                    identityContext.identityNamespace + ":" +
                    std::to_string(id));
            }
            result = candidate;
        }
        return result;
    }
    }
    return nullptr;
}

static constexpr char kDeveloperOverlayToggleHelp[] = "Ctrl+Shift+D";
static constexpr char kDeveloperTriggerToggleHelp[] = "Ctrl+Shift+T";
static constexpr char kDeveloperActorToggleHelp[] = "Ctrl+Shift+A";
static constexpr char kDeveloperActorLongToggleHelp[] = "Ctrl+Shift+L";
static constexpr char kDeveloperWatchToggleHelp[] = "Ctrl+Shift+W";
static constexpr float kDeveloperActorLabelDistance = 32.0f;

static std::shared_ptr<Gff> decodeSaveGff(std::optional<Resource> resource) {
    if (!resource) {
        return {};
    }
    MemoryInputStream stream(resource->data);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

static const char *screenName(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::None:
        return "None";
    case Game::Screen::MainMenu:
        return "MainMenu";
    case Game::Screen::Loading:
        return "Loading";
    case Game::Screen::CharacterGeneration:
        return "CharacterGeneration";
    case Game::Screen::InGame:
        return "InGame";
    case Game::Screen::InGameMenu:
        return "InGameMenu";
    case Game::Screen::Conversation:
        return "Conversation";
    case Game::Screen::Container:
        return "Container";
    case Game::Screen::PartySelection:
        return "PartySelection";
    case Game::Screen::SaveLoad:
        return "SaveLoad";
    case Game::Screen::GalaxyMap:
        return "GalaxyMap";
    case Game::Screen::SwoopRace:
        return "SwoopRace";
    case Game::Screen::PazaakWager:
        return "PazaakWager";
    case Game::Screen::PazaakSetup:
        return "PazaakSetup";
    case Game::Screen::PazaakBoard:
        return "PazaakBoard";
    case Game::Screen::Store:
        return "Store";
    case Game::Screen::Upgrade:
        return "Upgrade";
    case Game::Screen::Credits:
        return "Credits";
    default:
        return "Unknown";
    }
}

static pazaak::HandSelection randomPazaakHandSelection(const pazaak::SideDeck &) {
    std::array<size_t, pazaak::kSideDeckSize> indices;
    std::iota(indices.begin(), indices.end(), 0);
    for (size_t i = indices.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(indices[i], indices[swapIndex]);
    }
    return {indices[0], indices[1], indices[2], indices[3]};
}

// Developer-only KotOR II showcase hand selection. The showcase side deck is
// built in a fixed order, so rotating the four-card window across sets exposes
// every supported card family during a single match while still obeying the
// ordinary ten-card side deck and four-card hand rules.
static PazaakSession::HandSelector showcasePazaakHandSelector() {
    auto set = std::make_shared<size_t>(0);
    return [set](const pazaak::SideDeck &) {
        static const std::array<pazaak::HandSelection, 3> windows {
            pazaak::HandSelection {0, 1, 2, 3},
            pazaak::HandSelection {4, 5, 6, 7},
            pazaak::HandSelection {8, 9, 0, 1},
        };
        pazaak::HandSelection selection = windows[*set % windows.size()];
        ++*set;
        return selection;
    };
}

static pazaak::MainDeck randomPazaakMainDeck() {
    std::vector<int> cards(pazaak::MainDeck::standardOrdered().cards());
    for (size_t i = cards.size() - 1; i > 0; --i) {
        size_t swapIndex = static_cast<size_t>(randomInt(0, static_cast<int>(i)));
        std::swap(cards[i], cards[swapIndex]);
    }
    return pazaak::MainDeck(std::move(cards));
}

static std::optional<pazaak::CardDefinition> k1PazaakCardDefinition(int cardId) {
    if (cardId < 0 || cardId >= 18) {
        return std::nullopt;
    }
    int magnitude = cardId % 6 + 1;
    if (cardId < 6) {
        return pazaak::CardDefinition::fixedPositive(magnitude);
    }
    if (cardId < 12) {
        return pazaak::CardDefinition::fixedNegative(magnitude);
    }
    return pazaak::CardDefinition::signSelectable(magnitude);
}

// KotOR II owns five extra card types after the numbered ones. Their order
// follows the authored side-deck screen: Tiebreaker, Double, Flip 2&4, Flip 3&6
// and Value Change.
static std::optional<pazaak::CardDefinition> k2PazaakCardDefinition(int cardId) {
    if (cardId < 0 || cardId >= 23) {
        return std::nullopt;
    }
    if (cardId < 18) {
        return k1PazaakCardDefinition(cardId);
    }
    switch (cardId) {
    case 18:
        return pazaak::CardDefinition::tiebreaker();
    case 19:
        return pazaak::CardDefinition::doubleCard();
    case 20:
        return pazaak::CardDefinition::flipTwoFour();
    case 21:
        return pazaak::CardDefinition::flipThreeSix();
    default:
        return pazaak::CardDefinition::valueChange();
    }
}

static std::optional<pazaak::CardDefinition> parseK1PazaakDeckCard(
    const std::string &token) {

    if (token.size() != 2 ||
        (token[0] != '+' && token[0] != '-' && token[0] != '*') ||
        token[1] < '1' || token[1] > '6') {
        return std::nullopt;
    }
    int magnitude = token[1] - '0';
    switch (token[0]) {
    case '+':
        return pazaak::CardDefinition::fixedPositive(magnitude);
    case '-':
        return pazaak::CardDefinition::fixedNegative(magnitude);
    case '*':
        return pazaak::CardDefinition::signSelectable(magnitude);
    default:
        return std::nullopt;
    }
}

static std::optional<pazaak::SideDeck> loadK1PazaakOpponentDeck(
    const resource::TwoDA &decks,
    int row) {

    if (row < 0 || row >= decks.getRowCount()) {
        return std::nullopt;
    }
    std::vector<pazaak::CardDefinition> cards;
    cards.reserve(pazaak::kSideDeckSize);
    for (size_t i = 0; i < pazaak::kSideDeckSize; ++i) {
        auto card = parseK1PazaakDeckCard(
            decks.getString(row, "card" + std::to_string(i)));
        if (!card) {
            return std::nullopt;
        }
        cards.push_back(*card);
    }
    return pazaak::SideDeck {
        cards[0], cards[1], cards[2], cards[3], cards[4],
        cards[5], cards[6], cards[7], cards[8], cards[9],
    };
}

static std::optional<pazaak::CardDefinition> parseK2PazaakDeckCard(
    const std::string &token) {

    // KotOR II special-card tokens, verified from shipped card descriptions.
    if (token == "$$") {
        return pazaak::CardDefinition::doubleCard();
    }
    if (token == "F1") {
        return pazaak::CardDefinition::flipTwoFour();
    }
    if (token == "F2") {
        return pazaak::CardDefinition::flipThreeSix();
    }
    if (token == "TT") {
        return pazaak::CardDefinition::tiebreaker();
    }
    if (token == "VV") {
        return pazaak::CardDefinition::valueChange();
    }
    // Otherwise the KotOR I grammar (+N / -N / *N) applies unchanged.
    return parseK1PazaakDeckCard(token);
}

static std::optional<pazaak::SideDeck> loadK2PazaakOpponentDeck(
    const resource::TwoDA &decks,
    int row) {

    if (row < 0 || row >= decks.getRowCount()) {
        return std::nullopt;
    }
    std::vector<pazaak::CardDefinition> cards;
    cards.reserve(pazaak::kSideDeckSize);
    for (size_t i = 0; i < pazaak::kSideDeckSize; ++i) {
        auto card = parseK2PazaakDeckCard(
            decks.getString(row, "card" + std::to_string(i)));
        if (!card) {
            return std::nullopt;
        }
        cards.push_back(*card);
    }
    return pazaak::SideDeck {
        cards[0], cards[1], cards[2], cards[3], cards[4],
        cards[5], cards[6], cards[7], cards[8], cards[9],
    };
}

static const char *cameraTypeName(CameraType type) {
    switch (type) {
    case CameraType::FirstPerson:
        return "FirstPerson";
    case CameraType::ThirdPerson:
        return "ThirdPerson";
    case CameraType::Static:
        return "Static";
    case CameraType::Animated:
        return "Animated";
    case CameraType::Dialog:
        return "Dialog";
    default:
        return "Unknown";
    }
}

static const char *objectTypeName(ObjectType type) {
    switch (type) {
    case ObjectType::Creature:
        return "creature";
    case ObjectType::Item:
        return "item";
    case ObjectType::Trigger:
        return "trigger";
    case ObjectType::Door:
        return "door";
    case ObjectType::Waypoint:
        return "waypoint";
    case ObjectType::Placeable:
        return "placeable";
    case ObjectType::Store:
        return "store";
    case ObjectType::Encounter:
        return "encounter";
    case ObjectType::Sound:
        return "sound";
    case ObjectType::Module:
        return "module";
    case ObjectType::Area:
        return "area";
    case ObjectType::Room:
        return "room";
    case ObjectType::Camera:
        return "camera";
    default:
        return "object";
    }
}

static bool isDeveloperOverlayChord(const input::KeyEvent &event) {
    bool control = (event.mod & input::KeyModifiers::control) != 0;
    bool shift = (event.mod & input::KeyModifiers::shift) != 0;
    return control && shift;
}

static const char *triggerDebugStateName(Trigger::DebugState state) {
    switch (state) {
    case Trigger::DebugState::Entered:
        return "enter";
    case Trigger::DebugState::Inside:
        return "inside";
    case Trigger::DebugState::Tested:
        return "tested";
    default:
        return "default";
    }
}

static int getDebugFaction(const std::shared_ptr<Object> &object) {
    if (!object) {
        return -1;
    }
    if (auto creature = dyn_cast<Creature>(object)) {
        return static_cast<int>(creature->faction());
    }
    if (auto door = dyn_cast<Door>(object)) {
        return static_cast<int>(door->faction());
    }
    if (auto placeable = dyn_cast<Placeable>(object)) {
        return static_cast<int>(placeable->faction());
    }
    return -1;
}

void Game::init() {
    _simClock.setTickRate(_options.game.tickRate);
    _tickGate = _options.game.tickGate;
    initConsole();
    resetGalaxyMap();
    initLocalServices();
    setSceneSurfaces();
    setCursorType(CursorType::Default);
    loadUserSettings();

    _moduleNames = _services.resource.director.moduleNames();
    _saveNames = _services.resource.director.saveNames();

    playVideo("legal");
    openMainMenu();
}

void Game::initJournalNotifications() {
    _journal.setOnQuestChanged([this](const Journal::EntryChange &change) {
        submitStatusSummary(StatusSummaryCategory::Journal);
        awardPlotXPByIndex(change.plotIndex, change.xpPercentage);
    });
}

void Game::registerConsoleCommand(std::string name, std::string description, ConsoleCommandHandler handler) {
    static const std::set<std::string> cheatCommands {
        "playanim", "warp", "kill", "additem", "givexp", "givegold",
        "spawncreature", "spawncompanion", "setfaction", "setposition",
        "professionaltools", "killroom", "setability", "setskill",
        "addfeat", "removefeat", "addspell", "removespell",
        "castspellatobject", "opendoor", "closedoor"};
    bool marksCheatUsed = cheatCommands.count(name) != 0;
    _console.registerCommand(
        name, description,
        [this, handler, marksCheatUsed](const ConsoleArgs &args) {
            (this->*handler)(args);
            if (marksCheatUsed) {
                _cheatUsed = true;
            }
        });
}

void Game::initConsole() {
    registerConsoleCommand("info", "information on selected object", &Game::consoleInfo);
    registerConsoleCommand("listglobals", "list global variables", &Game::consoleListGlobals);
    registerConsoleCommand("listlocals", "list local variables", &Game::consoleListLocals);
    registerConsoleCommand("runscript", "run script", &Game::consoleRunScript);
    registerConsoleCommand("listanim", "list animations of selected object", &Game::consoleListAnim);
    registerConsoleCommand("playanim", "play animation on selected object", &Game::consolePlayAnim);
    registerConsoleCommand("warp", "warp to a module", &Game::consoleWarp);
    registerConsoleCommand("camera", "select camera (free)", &Game::consoleCamera);
    registerConsoleCommand("campos", "set free camera position", &Game::consoleCamPos);
    registerConsoleCommand("camlook", "aim free camera at a point", &Game::consoleCamLook);
    registerConsoleCommand("camstatus", "print free camera viewpoint commands", &Game::consoleCamStatus);
    registerConsoleCommand("openmenu", "open the main menu or an in-game menu tab", &Game::consoleOpenMenu);
    registerConsoleCommand("openchargen", "open a character-generation screen", &Game::consoleOpenCharacterGeneration);
    registerConsoleCommand("skipmovie", "skip the active movie", &Game::consoleSkipMovie);
    registerConsoleCommand("showbark", "show a timed HUD bark message", &Game::consoleShowBark);
    registerConsoleCommand("showpopup", "show the confirmation popup with an optional icon", &Game::consoleShowPopup);
    registerConsoleCommand("showgallerymode", "open a deterministic gameplay-mode gallery fixture", &Game::consoleShowGalleryMode);
    registerConsoleCommand("graphics", "toggle 3D scene rendering: graphics on|off", &Game::consoleGraphics);
    registerConsoleCommand("seed", "reseed the gameplay random stream: seed <number>", &Game::consoleSeed);
    registerConsoleCommand("cosmeticseed", "reseed the cosmetic (visual/audio) random stream: cosmeticseed <number>", &Game::consoleCosmeticSeed);
    registerConsoleCommand("showhud", "open the third-person gameplay HUD for a scripted capture", &Game::consoleShowHUD);
    registerConsoleCommand("showtransition", "show an area-transition banner for a scripted capture", &Game::consoleShowTransition);
    registerConsoleCommand("showloadscreen", "hold the loading screen for a scripted capture: showloadscreen [image]", &Game::consoleShowLoadScreen);
    registerConsoleCommand("opencontainer", "open the container screen on the party leader for a scripted capture", &Game::consoleOpenContainer);
    registerConsoleCommand("selectdialogoption", "select a dialog option without activating it for a scripted capture", &Game::consoleSelectDialogOption);
    registerConsoleCommand("kill", "kill selected object", &Game::consoleKill);
    registerConsoleCommand("additem", "add item to selected object", &Game::consoleAddItem);
    registerConsoleCommand("givexp", "give experience to selected creature", &Game::consoleGiveXP);
    registerConsoleCommand("givegold", "give credits to the party", &Game::consoleGiveGold);
    registerConsoleCommand("showaabb", "toggle rendering AABB", &Game::consoleShowAABB);
    registerConsoleCommand("showwalkmesh", "toggle rendering walkmesh", &Game::consoleShowWalkmesh);
    registerConsoleCommand("showtriggers", "toggle rendering triggers", &Game::consoleShowTriggers);
    registerConsoleCommand("spawncreature", "spawn a creature", &Game::consoleSpawnCreature);
    registerConsoleCommand("spawncompanion", "spawn a companion", &Game::consoleSpawnCompanion);
    registerConsoleCommand("addavailablenpc", "add an NPC to the party selection roster", &Game::consoleAddAvailableNpc);
    registerConsoleCommand("selectobjectbyid", "select an object by id", &Game::consoleSelectObjectById);
    registerConsoleCommand("selectobjectbytag", "select an object by tag", &Game::consoleSelectObjectByTag);
    registerConsoleCommand("selectleader", "select the party leader", &Game::consoleSelectLeader);
    registerConsoleCommand("setfaction", "change faction of a creature", &Game::consoleSetFaction);
    registerConsoleCommand("setposition", "change position of a creature", &Game::consoleSetPosition);
    registerConsoleCommand("professionaltools", "add various combat items to the inventory", &Game::consoleProfessionalTools);
    registerConsoleCommand("killroom", "kill all hostile creatures in a room of the selected object", &Game::consoleKillRoom);
    registerConsoleCommand("autoskipenable", "enable auto-skip for conversations", &Game::consoleAutoSkipEnable);
    registerConsoleCommand("autoskipentries", "add a sequence of entries to skip", &Game::consoleAutoSkipEntries);
    registerConsoleCommand("autoskipreplies", "add a sequence of replies to pick", &Game::consoleAutoSkipReplies);
    registerConsoleCommand("startconversation", "start a conversation with the selected object or a DLG resref", &Game::consoleStartConversation);
    // Presentation probe: the recorded sound cues, one JSON line. Kept out of
    // dumpstate, whose body must match across runs; cue choice is cosmetic.
    _console.registerCommand("soundcues", "print the recorded sound cues as JSON", [this](const ConsoleArgs &args) {
        std::string out("[");
        for (const auto &cue : _soundCues) {
            if (out.size() > 1) {
                out += ",";
            }
            out += str(boost::format("{\"tick\":%u,\"category\":\"%s\",\"resref\":\"%s\"}") % cue.tick % cue.category % cue.resRef);
        }
        out += "]";
        _console.printLine(out);
    });
    // Presentation probe: EnableVideoEffect/DisableVideoEffect from the console.
    _console.registerCommand("videoeffect", "videoeffect <row>|-1: show a videoeffects.2da row, or clear it", [this](const ConsoleArgs &args) {
        auto row = args.get<int>(1);
        if (!row || *row < 0) {
            disableVideoEffect();
        } else {
            enableVideoEffect(*row);
        }
    });
    registerConsoleCommand("cutsceneattack", "attack an object by id with a pre-determined animation and result", &Game::consoleCutsceneAttack);
    registerConsoleCommand("setability", "set ability value (strength, dexterity, etc.)", &Game::consoleSetAbility);
    registerConsoleCommand("setskill", "set skill value (computer use, repair, etc.)", &Game::consoleSetSkill);
    registerConsoleCommand("addfeat", "add feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("removefeat", "remove feat by type", &Game::consoleAddOrRemoveFeat);
    registerConsoleCommand("addspell", "add spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("removespell", "remove spell by type", &Game::consoleAddOrRemoveSpell);
    registerConsoleCommand("castspellatobject", "cast spell at object", &Game::consoleCastSpellAtObject);
    registerConsoleCommand("opendoor", "open a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("closedoor", "close a selected door object", &Game::consoleOpenCloseDoor);
    registerConsoleCommand("listgames", "list savegames", &Game::consoleListGames);
    registerConsoleCommand("loadgame", "load a savegame", &Game::consoleLoadGame);
    registerConsoleCommand("savegame", "save to a semantic slot", &Game::consoleSaveGame);
    registerConsoleCommand("startpazaak", "start a development Pazaak match", &Game::consoleStartPazaak);
    registerConsoleCommand("showpath", "show debug overlay for pathfinding", &Game::consoleShowPath);
    registerConsoleCommand("dumpstate", "print the game state as one line of JSON: dumpstate [radius]", &Game::consoleDumpState);
    registerConsoleCommand("dumpobject", "print one object as one line of JSON: dumpobject <id|tag>", &Game::consoleDumpObject);
    registerConsoleCommand("newgame", "start a quick new game: newgame [class slot 0-5] [name]", &Game::consoleNewGame);
    registerConsoleCommand("moveto", "walk a creature to a point through the pathfinder: moveto x y [z] [run=1] [@id|tag]", &Game::consoleMoveTo);
    registerConsoleCommand("movetoobject", "walk a creature to an object through the pathfinder: movetoobject id|tag [run=1] [distance] [@id|tag]", &Game::consoleMoveToObject);
    registerConsoleCommand("click", "click an object as the player does, select then act: click id|tag [now|attack]", &Game::consoleClick);
    registerConsoleCommand("instantdoors", "harness only: a transition door click leaves at once, with no walk to the door: instantdoors [1|0]", &Game::consoleInstantDoors);
    registerConsoleCommand("clearactions", "clear a creature's action queue: clearactions [@id|tag]", &Game::consoleClearActions);
    registerConsoleCommand("cancelcombat", "Cancel Combat as the HUD button does: cancelcombat [@id|tag]", &Game::consoleCancelCombat);
    registerConsoleCommand("autocombat", "toggle or set a character's auto-combat: autocombat [1|0] [@id|tag]", &Game::consoleAutoCombat);
    registerConsoleCommand("waitidle", "hold command execution until a creature's action queue is empty; prints nothing: waitidle [@id|tag] [maxframes=600]", &Game::consoleWaitIdle);
    registerConsoleCommand("idlestatus", "print whether a creature's action queue is empty: idlestatus [@id|tag]", &Game::consoleIdleStatus);
    registerConsoleCommand("closescreen", "close a container or in-game menu as its Close button does", &Game::consoleCloseScreen);
    registerConsoleCommand("pickreply", "pick the conversation reply at a list row, as clicking it does: pickreply <row>", &Game::consolePickReply);
    registerConsoleCommand("takeall", "take every item from the open container, as its OK button does", &Game::consoleTakeAll);
    registerConsoleCommand("containergive", "move one item (or one of a stack) from the leader into a placeable container, as the container Give does: containergive <item resref> <container tag>", &Game::consoleContainerGive);
    registerConsoleCommand("equipauto", "equip inventory items into empty slots: equipauto [@id|tag]", &Game::consoleEquipAuto);
    registerConsoleCommand("equipslot", "equip an inventory item into a named or numbered slot, as the equipment screen does: equipslot <item tag|template> <slot|rightweapon2|leftweapon2|...> [@id|tag]", &Game::consoleEquipSlot);
    registerConsoleCommand("swapweapons", "switch weapon sets as the HUD's swap button does: swapweapons [@id|tag]", &Game::consoleSwapWeapons);
    registerConsoleCommand("partyselect", "add NPCs on the party selection screen and press Done: partyselect available|<npc>...", &Game::consolePartySelect);
    registerConsoleCommand("switchleader", "switch the party leader as Tab does, until <tag> leads: switchleader [tag]", &Game::consoleSwitchLeader);
    registerConsoleCommand("useskill", "use a skill on a target as the target menu does: useskill <security|demolitions|computeruse|repair|treatinjury|...> <id|tag> [@actor]", &Game::consoleUseSkill);
    registerConsoleCommand("vfx", "present a visual effect row fire-and-forget, as a hit does: vfx <visualeffects row> <id|tag>", &Game::consoleVfx);
    registerConsoleCommand("giveloot", "create a droppable item on an object as CreateItemOnObject does: giveloot <id|tag> <item template> [stack]", &Game::consoleGiveLoot);
    registerConsoleCommand("tickgate", "close (1) or open (0) the tick gate; closed, the simulation runs only granted ticks: tickgate [1|0]", &Game::consoleTickGate);
    registerConsoleCommand("step", "run exactly n simulation ticks, holding command execution until they have run: step <ticks>", &Game::consoleStep);
    registerConsoleCommand("untiltick", "hold command execution until the simulation reaches a tick: untiltick <tick>", &Game::consoleUntilTick);
    registerConsoleCommand("stepidle", "run ticks until a creature's action queue is empty: stepidle [@id|tag] [maxticks=300]", &Game::consoleStepIdle);
    registerConsoleCommand("spawnfollowers", "spawn friendly co-op party characters that follow the leader: spawnfollowers <template> <count>", &Game::consoleSpawnFollowers);
    registerConsoleCommand("setvitals", "set a creature's current hit points and force points: setvitals [hp=<n>] [fp=<n>] [@id|tag]", &Game::consoleSetVitals);
    registerConsoleCommand("interp", "render interpolation between ticks: interp [status|0|1|headless 0|1|trace <frames> [@id|tag]|dump]", &Game::consoleInterp);
    registerConsoleCommand("tickstatus", "print the simulation tick, tick gate and gameplay random stream state", &Game::consoleTickStatus);
    registerConsoleCommand("usedefitem", "use a defensive item (stim, energy shield) as the action bar does: usedefitem <item tag>", &Game::consoleUseDefensiveItem);
    registerConsoleCommand("usehealitem", "use a healing item as the action bar does: usehealitem [n|item tag]", &Game::consoleUseHealItem);
    registerConsoleCommand("useattackitem", "use an attacking item (grenade) on a target as the target menu does: useattackitem <item tag> <target id|tag>", &Game::consoleUseAttackItem);
    registerConsoleCommand("k2merge", "K2 systems merge report, one JSON line per merged table: k2merge [table] | addclass <class id> [@member] | prestige [@member] | craft [status|list|create <template>|breakdown <item>|materials <c> <ch>] [@crafter]", &Game::consoleK2Merge);
    registerConsoleCommand("form", "K2 Force forms: form [list|set <spell id>|clear|grant <spell id>] [@member]", &Game::consoleForm);
    registerConsoleCommand("levelup", "take a pending level through the level-up screen, each step's Recommended pick then Accept: levelup [recommended=1] [@member]", &Game::consoleLevelUp);
    registerConsoleCommand("store", "trade through the store screen: store [status|open <tag>|list [buy|sell]|buy <item> [n]|sell <item> [n]|close]", &Game::consoleStore);
    registerConsoleCommand("upgrade", "install or remove an item upgrade through the upgrade bench, then Assemble: upgrade [list|open [item]|<item> <upgrade>|remove <item> <upgrade>]", &Game::consoleUpgrade);
    registerConsoleCommand("credits", "the end credits: credits [status|start|skip]", &Game::consoleCredits);
    registerConsoleCommand("guiclick", "click a control of the current screen as the mouse does: guiclick <tag> [clicks=1|2]", &Game::consoleGuiClick);
    registerConsoleCommand("guidump", "print named controls of the current screen: guidump <tag>...", &Game::consoleGuiDump);
    registerConsoleCommand("guishow", "show or hide a control of the current screen: guishow <tag> 0|1", &Game::consoleGuiShow);
    registerConsoleCommand("key", "press a key as the keyboard does (keymap applies): key <name> [down|up]", &Game::consoleKey);
    registerConsoleCommand("options", "open an options page over the in-game Options, as its button does: options [status|open <panel>|back]", &Game::consoleOptions);

    registerConsoleCommand("setglobal", "set a global as a script does: setglobal number|boolean <name> <value>", &Game::consoleSetGlobal);
    registerConsoleCommand("mgstate", "print the running minigame as one line of JSON", &Game::consoleMiniGameState);
    registerConsoleCommand("mginput", "set the minigame input seam: mginput [steer=-1..1] [fire=0|1] [pitch=-1..1] [yaw=-1..1] [dpitch=deg] [dyaw=deg]", &Game::consoleMiniGameInputCommand);
    registerConsoleCommand("swoopauto", "swoop autopilot: hold fire and steer for the pads: swoopauto [1|0]", &Game::consoleSwoopAuto);
    registerConsoleCommand("turretauto", "turret autopilot: aim at the nearest fighter and fire: turretauto [1|0]", &Game::consoleTurretAuto);
    registerConsoleCommand("pazaakstate", "print the Pazaak session as one line of JSON", &Game::consolePazaakState);
    registerConsoleCommand("pazaakwager", "set and confirm the Pazaak wager: pazaakwager <credits>|confirm", &Game::consolePazaakWager);
    registerConsoleCommand("pazaakdeck", "fill the side deck and confirm it: pazaakdeck [auto|confirm]", &Game::consolePazaakDeck);
    registerConsoleCommand("pazaakplay", "play a hand card: pazaakplay <hand index> [plus|minus]", &Game::consolePazaakPlay);
    registerConsoleCommand("pazaakend", "end the player's Pazaak turn", &Game::consolePazaakEnd);
    registerConsoleCommand("pazaakstand", "stand in the current Pazaak set", &Game::consolePazaakStand);
    registerConsoleCommand("pazaakforfeit", "forfeit the Pazaak match", &Game::consolePazaakForfeit);
    initCoopConsole();

    if (_options.game.developer) {
        registerConsoleCommand("minigameinfo", "print minigame metadata for current area", &Game::consoleMiniGameInfo);
        registerConsoleCommand("showimgui", "open imgui demo", &Game::consoleShowImGui);
    }
}

void Game::initLocalServices() {
    auto routines = std::make_unique<Routines>(_gameId, this, &_services);
    routines->init();
    _routines = std::move(routines);

    _scriptRunner = std::make_unique<ScriptRunner>(*_routines, _services.resource.scripts);
    initK2Routines();

    if (!_saveSeams.captureScreenshot) {
        _saveSeams.captureScreenshot = [this]() {
            return captureSaveScreenshot();
        };
    }

    _map = std::make_unique<Map>(*this, _services);
}

/**
 * K2 programs that the K2 systems merge runs (appended power rows, forms)
 * call routines by K2's numbering; they get K2's routine table. K2's ids for
 * rows of visualeffects.2da (row labels) and effecticon.2da (row indices) are
 * mapped to the merged table's on the way in (design note k2_merge).
 */
void Game::initK2Routines() {
    auto *merge = _services.resource.k2Merge;
    if (!merge) {
        return;
    }
    auto routines = std::make_unique<Routines>(GameID::TSL, this, &_services);
    routines->init();
    routines->setArgumentRemap([merge](int routine, std::vector<script::Variable> &args) {
        auto remapInt = [&](size_t index, const char *table) {
            if (index < args.size() && args[index].type == script::VariableType::Int) {
                int mapped = merge->mapK2Id(table, args[index].intValue);
                if (mapped >= 0) {
                    args[index].intValue = mapped;
                }
            }
        };
        switch (routine) {
        case 180: // EffectVisualEffect
        case 207: // EffectBeam
        case 677: // PlayVisualAreaEffect
            remapInt(0, "visualeffects");
            break;
        case 552: // SetEffectIcon
            remapInt(1, "effecticon");
            break;
        default:
            break;
        }
    });
    _k2Routines = std::move(routines);
    _scriptRunner->setK2Routines(_k2Routines.get());
}

void Game::setSceneSurfaces() {
    auto walkable = _services.game.surfaces.getWalkableSurfaces();
    auto walkcheck = _services.game.surfaces.getWalkcheckSurfaces();
    auto lineOfSight = _services.game.surfaces.getLineOfSightSurfaces();
    for (auto &name : _services.scene.graphs.sceneNames()) {
        auto &scene = _services.scene.graphs.get(name);
        scene.setWalkableSurfaces(walkable);
        scene.setWalkcheckSurfaces(walkcheck);
        scene.setLineOfSightSurfaces(lineOfSight);
    }
}

bool Game::handle(const input::Event &rawEvent) {
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->handle(rawEvent);
        return true;
    }

    // In the world, keys go through the player's bindings (keymap.2da
    // defaults): an action the game performs, or the key the existing
    // handlers expect for it (design note p1_ui, "Key bindings").
    input::Event event = rawEvent;
    bool keyEvent = event.type == input::EventType::KeyDown || event.type == input::EventType::KeyUp;
    if (keyEvent && !_movie && _screen == Screen::InGame && _cameraType == CameraType::ThirdPerson) {
        auto translation = _keymap.translate(event.key.code);
        if (translation.dispatch) {
            if (event.type == input::EventType::KeyUp) {
                return true;
            }
            if (event.key.repeat || dispatchKeyAction(*translation.dispatch)) {
                return true;
            }
        } else {
            event.key.code = translation.forward;
            if (event.key.code == input::KeyCode::Unknown) {
                return true;
            }
        }
    }

    switch (event.type) {
    case input::EventType::KeyDown:
        if (handleKeyDown(event.key)) {
            return true;
        }
        break;
    case input::EventType::MouseMotion:
        if (handleMouseMotion(event.motion)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonDown:
        if (handleMouseButtonDown(event.button)) {
            return true;
        }
        break;
    case input::EventType::MouseButtonUp:
        if (handleMouseButtonUp(event.button)) {
            return true;
        }
        break;
    default:
        break;
    }

    if (!_movie) {
        auto gui = getScreenGUI();
        if (gui && gui->handle(event)) {
            return true;
        }
        // Escape on a screen that did not take it: close it (keymap "GUI").
        if (event.type == input::EventType::KeyDown && !event.key.repeat &&
            event.key.code == input::KeyCode::Escape && handleEscape()) {
            return true;
        }
        switch (_screen) {
        case Screen::InGame: {
            if (_party.handle(event)) {
                return true;
            }
            auto camera = getActiveCamera();
            if (camera && camera->handle(event)) {
                return true;
            }
            if (_module->handle(event)) {
                return true;
            }
            break;
        }
        case Screen::SwoopRace:
            if (_swoopRace.handle(event)) {
                return true;
            }
            break;
        case Screen::Turret:
            if (_turret.handle(event)) {
                return true;
            }
            break;
        default:
            break;
        }
    }

    return false;
}

bool Game::consumeTimingDiscontinuity() {
    bool discontinuity = _timingDiscontinuity;
    _timingDiscontinuity = false;
    if (discontinuity) {
        // A load: nothing on screen continues from the last tick drawn.
        scene::interp::snapAll();
    }
    return discontinuity;
}

void Game::update(float frameTime) {
    ++_frameCount;
    // Last frame's blend is presentation only: gone before anything else runs.
    scene::interp::restore();
    float frameDt = frameTime * _gameSpeed;
    if (_coop) {
        _coop->pollNetwork();
    }
    if (_coopRecord) {
        recordCoopState(frameTime);
    }
    if (_movie) {
        // Movies play on wall time and hold the simulation: no ticks accrue.
        updateMovie(frameDt);
        if (_coop && _coop->isClient() && _movie) {
            // A co-op client keeps reading the host's state: the host's skip
            // (or its movie ending) is what ends this one.
            _coop->clientTick(frameDt);
        }
        return;
    }
    {
        frameperf::Scope musicScope(frameperf::Phase::Present);
        updateMusic(frameTime);
    }

    // Simulation: whole fixed ticks. The developer game speed scales how many
    // ticks a frame runs, never how long a tick is, so the clamp scales too.
    int speed = std::max(1, static_cast<int>(std::ceil(_gameSpeed)));
    if (_options.game.variableStep) {
        // Test hosts: one step of exactly this frame's length.
        if (_coop) {
            _coop->beforeTick();
        }
        frameperf::Scope simScope(frameperf::Phase::Sim);
        frameperf::addTicks(1);
        tick(frameDt);
        if (_coop) {
            _coop->afterTick();
        }
        checkTickWait();
        speed = 0; // no fixed-step ticks below
    }
    int due = speed == 0 ? 0 : _simClock.advance(static_cast<double>(frameTime) * _gameSpeed, std::max(1, _options.game.maxTicksPerFrame) * speed);
    if (_tickGate) {
        due = static_cast<int>(std::min<uint64_t>(static_cast<uint64_t>(due), _tickAllowance));
    }
    uint64_t ticksBefore = _tickCount;
    for (int i = 0; i < due; ++i) {
        frameperf::Scope simScope(frameperf::Phase::Sim);
        if (_tickGate) {
            --_tickAllowance;
        }
        if (_coop) {
            _coop->beforeTick();
        }
        tick(_simClock.stepSeconds());
        if (_coop) {
            _coop->afterTick();
        }
        checkTickWait();
        if (_tickGate && _tickAllowance == 0) {
            break;
        }
        if (_movie || _timingDiscontinuity) {
            // A tick blocked on a load or started a movie: the wall time this
            // frame measured is stale, so do not spend it on further ticks.
            _simClock.reset();
            scene::interp::snapAll();
            break;
        }
    }

    frameperf::addTicks(static_cast<int>(_tickCount - ticksBefore));

    // Per-frame presentation. Nothing below may change simulation state or
    // draw from the gameplay random stream.
    GameplayRandomForbidden presentation;
    frameperf::Scope presentScope(frameperf::Phase::Present);
    if (_coop) {
        _coop->present(tickAlpha());
    }
    presentInterpolation();
    updateCamera(frameDt);
    bool updModule = !_movie && _module && worldRunsOnScreen();
    if (updModule && !_paused) {
        _floatingText.update(frameDt);
    }
    auto gui = getScreenGUI();
    if (gui) {
        gui->update(frameDt);
    }
    if (_screen == Screen::MainMenu && _mainMenu && _mainMenu->themeSwitchRequested()) {
        switchMainMenuTheme();
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->update(frameDt);
    }
    updateVisualEffects(_paused ? 0.0f : frameDt);
    updateSceneGraph(frameDt);
    if (!_paused) {
        updateDrawDebug(frameDt);
    }
    if (_showImGui) {
        updateImGui(frameDt);
    }
}

bool Game::worldRunsOnScreen() const {
    switch (_screen) {
    case Screen::InGame:
    case Screen::Conversation:
    case Screen::SwoopRace:
    case Screen::Turret:
        return true;
    case Screen::InGameMenu:
    case Screen::Container:
    case Screen::Store:
    case Screen::Upgrade:
        return !_options.game.pauseInMenus;
    case Screen::Credits:
        // The end-game check that waits for the credits is a heartbeat.
        return true;
    default:
        return false;
    }
}

void Game::tick(float dt) {
    // Scene-node writes from here on are simulation; the first of each node
    // this tick keeps its previous transform for render interpolation.
    scene::interp::TickScope interpTick;
    ++_tickCount;
    if (_coop && _coop->isClient()) {
        // A co-op client does not simulate: it plays back the host's state.
        tickCoopClient(dt);
        return;
    }
    // The screen fade advances once per tick, before this tick's scripts can
    // replace its request, so every frame rate sees the same fade state.
    {
        GameplayRandomForbidden fade;
        _globalFade.update(dt);
        _dialogFade.update(dt);
    }
    updateCameraType();

    // Requests made by scripts, console handlers or UI code in the previous
    // update execute only after those call stacks have unwound. This precedes
    // deferred module transition handling, so save+transition in one script
    // deterministically captures the source module first.
    {
        frameperf::Scope perfScope(frameperf::Phase::SimSave);
        processPendingSave();
    }
    processPendingScreens();

    if (_endGameRequested) {
        _endGameRequested = false;
        info("Game: the game has ended; returning to the main menu");
        openMainMenu();
        return;
    }

    if (_partyDefeatPending) {
        _partyDefeatPending = false;
        info("Game: the party is defeated");
        if (_partyDefeatHandler) {
            _partyDefeatHandler();
        } else {
            showDeathScreen();
        }
    }
    processAutosaveOnEnter();
    processCheckpointOnEnter();

    if (_screen == Screen::PazaakBoard && _pazaakSession) {
        static constexpr float kPazaakOpponentEventDelay = 0.45f;
        if (_pazaakSession->advanceResultPresentation(dt)) {
            if (_pazaakBoard) {
                _pazaakBoard->refresh();
            }
            completePazaakIfReady();
        } else if (_pazaakSession && !_pazaakSession->presentationPending()) {
            _pazaakOpponentEventElapsed += dt;
            if (_pazaakOpponentEventElapsed >= kPazaakOpponentEventDelay) {
                _pazaakOpponentEventElapsed = 0.0f;
                PazaakOpponentEvent event = _pazaakSession->advanceOpponentEvent();
                if (event != PazaakOpponentEvent::None) {
                    if (_pazaakBoard) {
                        _pazaakBoard->refresh();
                    }
                    completePazaakIfReady();
                }
            }
        }
    }

    if (!_nextModule.empty()) {
        loadNextModule();
    }

    tickMiniGame(dt);

    bool updModule = !_movie && _module && worldRunsOnScreen();
    if (updModule && !_paused) {
        advanceWorldTime(dt);
        advancePlayedTime(dt);
        auto updatedModule = _module;
        auto generation = _runtimeSessionGeneration;
        {
            frameperf::Scope perfScope(frameperf::Phase::SimModule);
            updatedModule->update(dt);
        }
        {
            frameperf::Scope perfScope(frameperf::Phase::SimCombat);
            _combat.update(dt);
        }
        if (_module == updatedModule && _runtimeSessionGeneration == generation) {
            frameperf::Scope perfScope(frameperf::Phase::SimEvents);
            _scriptEvents.runDue();
            // Natural regeneration (design note regeneration), on the state
            // this tick's events left.
            _regeneration.update(dt);
        }
        if (_module == updatedModule && _runtimeSessionGeneration == generation) {
            settleFadeArrival();
        }
    }

    if (_screen == Screen::SwoopRace || _screen == Screen::Turret) {
        settleFadeArrival();
    }

    // The credits scroll in simulation time; the last line ends the sequence.
    if (_screen == Screen::Credits && _credits && !_movie && _credits->isRunning()) {
        if (!_credits->advance(dt)) {
            endCreditSequence();
        }
    }

    // Conversation timing: when an entry ends decides when its scripts run.
    if (_screen == Screen::Conversation && _conversation && !_movie) {
        _conversation->updateSimulation(dt);
    }

    // Model animation is simulation: creatures and doors wait for their
    // animations to finish. It advances here, at the tick rate, so an
    // animation ends on the same tick at any frame rate.
    if (!_movie && getActiveCamera()) {
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        sceneGraph.setUpdateRoots(!_paused);
        frameperf::Scope perfScope(frameperf::Phase::SimAnimation);
        sceneGraph.updateModelRoots(dt);
    }
}

void Game::setTickGate(bool closed) {
    _tickGate = closed;
    if (!closed) {
        _tickAllowance = 0;
    }
    _simClock.reset();
}

void Game::beginTickWait(std::string label, uint64_t untilTick, std::function<bool()> done) {
    _tickWait.untilTick = untilTick;
    _tickWait.done = std::move(done);
    _tickWait.finished = false;
    _tickWait.startTick = _tickCount;
    _tickWait.label = std::move(label);
    if ((_tickWait.done && _tickWait.done()) || _tickCount >= untilTick) {
        _tickWait.finished = true;
        info(str(boost::format("%s: done tick=%u ticks=0") % _tickWait.label % _tickCount));
        return;
    }
    if (_tickGate) {
        // Grant exactly the ticks the wait may take. A condition met early
        // takes the rest back in checkTickWait.
        _tickAllowance = untilTick - _tickCount;
    }
    setCommandBlocker([this]() {
        return !_tickWait.finished;
    });
}

void Game::checkTickWait() {
    if (_tickWait.finished) {
        return;
    }
    bool satisfied = _tickWait.done && _tickWait.done();
    if (!satisfied && _tickCount < _tickWait.untilTick) {
        return;
    }
    _tickWait.finished = true;
    if (_tickGate) {
        _tickAllowance = 0;
    }
    info(str(boost::format("%s: %s tick=%u ticks=%u") %
             _tickWait.label %
             (satisfied || !_tickWait.done ? "done" : "timeout") %
             _tickCount %
             (_tickCount - _tickWait.startTick)));
}

void Game::render() {
    GameplayRandomForbidden presentation;
    if (_movie) {
        _movie->render();
    } else {
        // Credits on a black background hide the world.
        bool blackCredits = _screen == Screen::Credits && _credits && !_credits->transparentBackground();
        if (!blackCredits) {
            renderScene();
        }
        if (_options.graphics.reshade && _lastRenderedSceneOutput) {
            // ReShade-friendly mode (design note graphics_preset): the GUI
            // goes inside a "reone.ui" debug group, the hook point for a
            // ReShade add-on that renders effects before the UI, and the
            // scene depth is written again after it, so whatever the GUI
            // drew to depth never reaches ReShade.
            writeSceneDepthToWindow();
            _services.graphics.context.pushDebugGroup("reone.ui");
            renderGUI();
            _services.graphics.context.popDebugGroup();
            writeSceneDepthToWindow();
        } else {
            renderGUI();
        }
    }
    // The frame is drawn: put the simulated transforms back.
    scene::interp::restore();
}

float Game::presentationAlpha() const {
    // Headless runs and a closed tick gate draw the last tick as it is, so
    // captures and harness checkpoints do not depend on the frame rate.
    if (!_options.game.interpolate || _options.game.variableStep) {
        return 1.0f;
    }
    if ((_options.graphics.headless || _tickGate) && !_interpHeadless) {
        return 1.0f;
    }
    return tickAlpha();
}

void Game::presentInterpolation() {
    float alpha = presentationAlpha();
    scene::interp::apply(alpha);
    recordInterpTrace(alpha);
    if (!(alpha < 1.0f) || _cameraType != CameraType::ThirdPerson || !_module || !_module->area()) {
        return;
    }
    // The third-person camera follows the leader as drawn this frame (also
    // when nothing blends, so it settles on the last tick's position).
    _module->area()->update3rdPersonCameraTarget();
}

bool Game::handleKeyDown(const input::KeyEvent &event) {
    if (event.repeat)
        return false;

    if (handleDeveloperKeyDown(event)) {
        return true;
    }

    switch (event.code) {
    case input::KeyCode::F4:
        if (_screen == Screen::InGame) {
            requestQuickSave();
            return true;
        }
        break;

    case input::KeyCode::U:
        // Auto-combat toggle for the leader (the HUD button does the same).
        if (_screen == Screen::InGame) {
            toggleLeaderAutoCombat();
            return true;
        }
        break;

    case input::KeyCode::Minus:
        if (_options.game.developer && _gameSpeed > 1.0f) {
            _gameSpeed = glm::max(1.0f, _gameSpeed - 1.0f);
            return true;
        }
        break;

    case input::KeyCode::Equals:
        if (_options.game.developer && _gameSpeed < 8.0f) {
            _gameSpeed = glm::min(8.0f, _gameSpeed + 1.0f);
            return true;
        }
        break;

    case input::KeyCode::V:
        if (_options.game.developer && _screen == Screen::InGame) {
            toggleInGameCameraType();
            return true;
        }
        break;

    default:
        break;
    }

    return false;
}

bool Game::handleDeveloperKeyDown(const input::KeyEvent &event) {
    if (!_options.game.developer || _screen != Screen::InGame) {
        return false;
    }
    if (!isDeveloperOverlayChord(event)) {
        return false;
    }

    switch (event.code) {
    case input::KeyCode::D:
        _developerOverlay.visible = !_developerOverlay.visible;
        return true;
    case input::KeyCode::T:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.triggers = true;
        } else {
            _developerOverlay.triggers = !_developerOverlay.triggers;
        }
        return true;
    case input::KeyCode::A:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
        } else {
            _developerOverlay.actorLabels = !_developerOverlay.actorLabels;
        }
        return true;
    case input::KeyCode::L:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else if (!_developerOverlay.actorLabels) {
            _developerOverlay.actorLabels = true;
            _developerOverlay.longActorLabels = true;
        } else {
            _developerOverlay.longActorLabels = !_developerOverlay.longActorLabels;
        }
        return true;
    case input::KeyCode::W:
        if (!_developerOverlay.visible) {
            _developerOverlay.visible = true;
            _developerOverlay.watchedValues = true;
        } else {
            _developerOverlay.watchedValues = !_developerOverlay.watchedValues;
        }
        return true;
    default:
        return false;
    }
}

bool Game::handleMouseMotion(const input::MouseMotionEvent &event) {
    _pointer->setPosition({event.x, event.y});
    return false;
}

bool Game::handleMouseButtonDown(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _pointer->setPressed(true);
    if (_movie) {
        // A queued movie flagged not skippable ignores the click.
        skipMovie();
        return true;
    }
    return areClicksBlocked();
}

bool Game::handleMouseButtonUp(const input::MouseButtonEvent &event) {
    if (event.button != input::MouseButton::Left) {
        return false;
    }
    _pointer->setPressed(false);
    return areClicksBlocked();
}

Game::PreparedDestinationModule Game::prepareDestinationModule(
    const std::string &name,
    bool initialSaveRestore,
    std::shared_ptr<const resource::SaveWorkingState> workingState) {
    PreparedDestinationModule prepared;
    prepared.resources = _services.resource.director.prepareModuleLoad(
        name, std::move(workingState));
    if (!prepared.resources || !prepared.resources->structurallyValidated()) {
        throw ValidationException("Destination module was not structurally validated");
    }
    prepared.ifo = prepared.resources->moduleIfo();
    prepared.are = prepared.resources->areaAre();
    prepared.git = prepared.resources->areaGit();
    prepared.name = prepared.resources->moduleName();
    // GIT.UseTemplates is the retail world-representation discriminator.
    // Mod_IsSaveGame describes the surrounding IFO but cannot turn a
    // template GIT into an authoritative instance graph (or vice versa).
    const bool authoritativeSavedWorld =
        prepared.git &&
        resource::generated::parseGIT(*prepared.git).UseTemplates == 0;
    prepared.context = resolveModuleLoadContext(
        initialSaveRestore, authoritativeSavedWorld);
    validatePreparedDestination(prepared);
    return prepared;
}

void Game::validatePreparedDestination(
    const PreparedDestinationModule &prepared) const {
    if (!prepared.ifo || !prepared.are || !prepared.git) {
        throw ResourceNotFoundException(
            "Prepared destination is missing IFO/ARE/GIT state");
    }
    auto ifo = resource::generated::parseIFO(*prepared.ifo);
    if (ifo.Mod_Entry_Area.empty()) {
        throw ValidationException("Mod_Entry_Area must not be empty");
    }
    (void)resource::generated::parseARE(*prepared.are);
    (void)resource::generated::parseGIT(*prepared.git);

    // Initial disk restoration may legitimately fall back to an installed
    // module when the save has no archived current-module graph (#325). Only a
    // non-template GIT owns the authoritative ModuleGraph namespace;
    // template-local numbers are not saved identities.
    if (!restoresSavedWorld(prepared.context)) {
        return;
    }

    const auto identityContext = SerializedIdentityContext::moduleGraph(
        prepared.resources->moduleName());
    std::map<uint32_t, std::string> claims;
    auto validateClaims = [&](const resource::Gff &record,
                              SerializedGraphRoot root,
                              const std::string &prefix) {
        for (auto claim : collectSerializedObjectIdClaims(
                 record, identityContext, root)) {
            if (claim.id == kSavedRuntimeModuleObjectId ||
                claim.id == std::numeric_limits<uint32_t>::max()) {
                throw ValidationException(
                    "Invalid authoritative saved ObjectId " +
                    std::to_string(claim.id) + " at " + prefix + claim.path);
            }
            auto [found, inserted] = claims.emplace(
                claim.id, prefix + claim.path);
            if (!inserted) {
                throw ValidationException(
                    "Duplicate authoritative saved ObjectId " +
                    std::to_string(claim.id) + " at " + found->second +
                    " and " + prefix + claim.path);
            }
        }
    };
    validateClaims(
        *prepared.ifo, SerializedGraphRoot::ModuleIfo, "module/");
    validateClaims(
        *prepared.git, SerializedGraphRoot::AreaGit, "area/");

    uint32_t nextObjectId = kFirstRuntimeObjectId;
    if (prepared.ifo->readDword(nextObjectId, "Mod_NextObjId0") &&
        nextObjectId != 0 && nextObjectId < kFirstRuntimeObjectId) {
        throw ValidationException("Invalid Mod_NextObjId0");
    }
    uint64_t nextEffectId = 0;
    if (prepared.ifo->readDword64(nextEffectId, "Mod_Effect_NxtId") &&
        (nextEffectId == kUnassignedEffectId ||
         nextEffectId == std::numeric_limits<EffectId>::max())) {
        throw ValidationException("Invalid Mod_Effect_NxtId");
    }
}

bool Game::loadModule(
    const std::string &name,
    std::string entry,
    bool initialSaveRestore) {
    info("Preparing module '" + name + "'");
    PreparedDestinationModule prepared;
    try {
        prepared = prepareDestinationModule(
            name,
            initialSaveRestore,
            _services.resource.director.committedSaveWorkingState());
    } catch (const std::exception &e) {
        error("Failed preparing module '" + name + "': " + e.what());
        return false;
    }
    // Object ids do not carry across modules; neither do regeneration fractions.
    _regeneration.reset();
    return loadPreparedModule(
        std::move(prepared),
        std::move(entry),
        initialSaveRestore,
        /*resourcesCommitted=*/false);
}

bool Game::loadPreparedModule(
    PreparedDestinationModule prepared,
    std::string entry,
    bool initialSaveRestore,
    bool resourcesCommitted,
    std::shared_ptr<const resource::SaveWorkingState> sourceWorkingState) {
    const std::string name = prepared.name;
    info("Loading module '" + name + "'");
    _transitionInProgress = true;
    struct TransitionGuard {
        bool &value;
        ~TransitionGuard() { value = false; }
    } transitionGuard {_transitionInProgress};

    // Restoring a save is the only load an authored script may see as such,
    // and only while it runs: the flag falls away on completion, on failure
    // and on the way out of any exception.
    _loadingFromSaveGame = initialSaveRestore;
    struct LoadFromSaveGuard {
        bool &value;
        ~LoadFromSaveGuard() { value = false; }
    } loadFromSaveGuard {_loadingFromSaveGame};

    struct PartyTransitionRuntimeState {
        struct DelayedEvent {
            SavedEventRecord event;
            bool referencesBound {false};
        };
        RuntimeObjectRef<Creature> creature;
        SavedActionQueue actions;
        std::vector<bool> actionReferencesBound;
        std::vector<DelayedEvent> delayedEvents;
    };
    std::vector<PartyTransitionRuntimeState> partyTransitionState;

    // Exit scripts are part of the source module's last observable state. The
    // resulting working state remains a candidate until the resource/runtime
    // commit below; a capture failure leaves the source graph authoritative.
    if (_module) {
        try {
            _module->area()->runOnExitScript();
        } catch (const std::exception &e) {
            error("Source module exit script failed: " + std::string(e.what()));
            return false;
        }
        sourceWorkingState = prepareCurrentModuleWorkingState();
        if (!sourceWorkingState) {
            return false;
        }

        // Retail serializes resident Party members before destroying their
        // source representations. Reone retains the Creature storage, but the
        // live action/timer objects still belong to the outgoing Area. Capture
        // their existing save-facing forms now and reconstruct them only after
        // the destination is authoritative.
        try {
            const auto &residentCreatures =
                _module->area()->getObjectsByType(ObjectType::Creature);
            std::set<const Creature *> captured;
            for (const auto &object : _party.runtimeObjects()) {
                auto creature = std::dynamic_pointer_cast<Creature>(object);
                if (!creature || !captured.insert(creature.get()).second ||
                    std::find(
                        residentCreatures.begin(), residentCreatures.end(), creature) ==
                        residentCreatures.end()) {
                    continue;
                }

                PartyTransitionRuntimeState state;
                state.creature = creature;
                // A party member's in-progress action with no save form (a talent cast)
                // is dropped rather than refusing the move, as for NPCs; the
                // AI picks a new action in the destination.
                state.actions.actions = creature->saveActionSnapshot(true);
                for (auto &action : state.actions.actions) {
                    // Bind while the source registry is authoritative. Copies of
                    // these references retain exact C4 incarnation handles, never
                    // a numeric promise that the destination may reinterpret.
                    state.actionReferencesBound.push_back(
                        action.bindObjectReferences(*this));
                }

                for (size_t index = 0; index < creature->_delayed.size(); ++index) {
                    const auto &delayed = creature->_delayed[index];
                    if (!delayed.action || delayed.action->isCompleted() ||
                        delayed.action->isCancelled()) {
                        continue;
                    }
                    auto savedAction = delayed.action->saveFacingState();
                    if (!savedAction || savedAction->actionId != 37 ||
                        savedAction->parameters.size() != 1) {
                        throw ValidationException(
                            "Party delayed action has no retail timed-event representation");
                    }
                    auto situation = std::get_if<SerializedScriptSituation>(
                        &savedAction->parameters.front().payload);
                    if (!situation) {
                        throw ValidationException(
                            "Party delayed DoCommand action lacks a script situation");
                    }

                    const double remainingSeconds = delayed.timer
                                                        ? std::max(
                                                              0.0f,
                                                              delayed.timer->remaining())
                                                        : 0.0f;
                    const uint64_t absolute = _worldTimeMilliseconds +
                        static_cast<uint64_t>(
                            std::floor(remainingSeconds * 1000.0));
                    const uint64_t millisecondsPerDay = millisecondsPerWorldDay();

                    SavedEventRecord event;
                    event.day = static_cast<uint32_t>(
                        absolute / millisecondsPerDay);
                    event.time = static_cast<uint32_t>(
                        absolute % millisecondsPerDay);
                    event.object =
                        SavedObjectReference::fromRuntimeId(creature->id());
                    event.caller =
                        SavedObjectReference::fromRuntimeId(creature->id());
                    event.eventId = static_cast<uint32_t>(SavedEventType::Timed);
                    event.payload = *situation;
                    const bool referencesBound =
                        event.bindObjectReferences(*this);
                    state.delayedEvents.push_back({
                        std::move(event), referencesBound});
                }
                partyTransitionState.push_back(std::move(state));
            }
        } catch (const std::exception &e) {
            error(
                "Unable to snapshot Party transition state: " +
                std::string(e.what()));
            return false;
        }

        // Re-entering the same module must inspect the snapshot just captured,
        // not the previously committed visit. Other destinations are
        // unaffected because the candidate changed only the source archive.
        if (boost::iequals(_module->name(), name)) {
            try {
                prepared = prepareDestinationModule(
                    name, initialSaveRestore, sourceWorkingState);
            } catch (const std::exception &e) {
                error("Failed preparing snapshotted module '" + name +
                      "': " + e.what());
                return false;
            }
        }
    }

    bool loaded = false;

    const auto sourceScreen = _screen;
    bool commitStarted = false;
    try {
        auto loadscreens = _services.resource.twoDas.get("loadscreens");
        withLoadingScreen(loadScreenImage(loadscreens.get(), name), [this,
                                            &prepared,
                                            &name,
                                            &entry,
                                            initialSaveRestore,
                                            resourcesCommitted,
                                            &sourceWorkingState,
                                            &partyTransitionState,
                                            &commitStarted,
                                            &loaded]() {
        try {
            commitStarted = true;
            _globalFade.request(GlobalFade::Direction::Out);
            // Destination structure and the source snapshot are both viable.
            // Technical teardown belongs on the commit side of that boundary:
            // a rejected destination or failed snapshot must not abort an
            // otherwise valid gameplay session.
            abortPazaak();
            endMiniGame();
            if (_screen == Screen::Conversation && _conversation) {
                _conversation->cleanupForModuleTransition();
            }

            if (_module) {
                // The source snapshot is already frozen. Module retirement
                // now owns the full Area-departure boundary itself.
                retireActiveModuleRuntime();
            }

            // Do not carry a displayed or pending batch, indicator, or GUI
            // controls across module teardown. OnLoad events below start a new
            // batch for the destination module.
            _floatingText.reset();
            _statusSummary.reset();
            if (_hud) {
                _hud->resetStatusSummaryPresentation();
            }

            if (!resourcesCommitted) {
                _services.resource.director.commitModuleLoad(
                    std::move(prepared.resources));
            }

            // Resource publication is the only fallible destination commit.
            // Adopt the frozen source snapshot only after it succeeds, so a
            // destination that never publishes does not alter working state.
            if (sourceWorkingState) {
                _services.resource.director.adoptSaveWorkingState(
                    std::move(sourceWorkingState));
            }

            loadInGameMenus();

            if (_loadScreen) {
                _loadScreen->setProgress(50);
            }
            render();

            _visualEffectPresenter.clear();
            _services.scene.graphs.get(kSceneMain).clear();

            const auto &ifo = prepared.ifo;
            ModuleLoadContext context = prepared.context;
            bool restoringSavedWorld = restoresSavedWorld(context);

            const auto identityContext =
                restoringSavedWorld
                    ? SerializedIdentityContext::moduleGraph(name)
                    : SerializedIdentityContext::templateResource(name);
            const std::string entryArea = ifo->getString("Mod_Entry_Area");
            // The module itself needs a transient runtime identity even though
            // it is not serialized. Keep that allocation clear of identities
            // explicitly owned by the saved IFO graph (notably the Area).
            if (restoringSavedWorld) {
                reserveSavedObjectIds(
                    *ifo,
                    SerializedIdentityContext::moduleGraph(name),
                    SerializedGraphRoot::ModuleIfo);
            }
            std::shared_ptr<Module> destinationModule;
            std::vector<std::shared_ptr<Object>> noObsolete;
            replaceRuntimeObjectGraph(
                noObsolete,
                [&]() {
                    destinationModule = restoringSavedWorld
                                            ? newSavedModule()
                                            : newModule();
                    if (restoringSavedWorld) {
                        registerSavedModuleReferenceTarget(
                            destinationModule,
                            SerializedIdentityContext::moduleGraph(name));
                    }
                },
                [&]() noexcept {
                    _module = std::move(destinationModule);
                });
            _fadeArrival = _globalFade.beginArrival();
            _fadeArrivalModule = _module;
            auto destination = _module;
            auto arrival = _fadeArrival;
            auto stillCurrent = [&]() {
                return destination == _module && arrival == _fadeArrival;
            };
            destination->load(
                name,
                *ifo,
                *prepared.are,
                *prepared.git,
                restoringSavedWorld);
            if (!stillCurrent()) {
                return;
            }
            if (_coop) {
                // GIT objects exist and no script has run: co-op load keys.
                _coop->onModuleStructureLoaded();
            }
            _loadedModules.insert(std::make_pair(name, destination));

            // The IFO start fields set the clock for the first fresh module
            // after a new game or a load; later modules carry it on.
            if (_clockStartPending && context == ModuleLoadContext::FreshModule) {
                const ModuleInfo &info = destination->info();
                _minutesPerHour = info.minutesPerHour == 0 ? 5 : static_cast<uint8_t>(info.minutesPerHour);
                _worldTimeMilliseconds =
                    calendarDayIndex(info.startYear, info.startMonth, info.startDay) * millisecondsPerWorldDay() +
                    static_cast<uint64_t>(std::clamp(info.startHour, 0, 23)) * _minutesPerHour * 60000u;
                _worldTimeFraction = 0.0;
            }
            _clockStartPending = false;

            // Structural construction is complete and the destination module
            // is now the authoritative script caller. Authored gameplay begins
            // here; failures from this point are terminal rather than rolled
            // back as if arbitrary NWScript mutation were transactional.
            if (!restoringSavedWorld) {
                destination->runSpawnScripts();
                if (!stillCurrent()) {
                    return;
                }
            }

            if (_party.isEmpty()) {
                loadDefaultParty();
            }

            // Retail makes restored effects, actions and events visible before
            // authored OnLoad/OnEnter scripts inspect or mutate them. Binding
            // remains explicit and graph-local; only publication moves ahead
            // of the entry hooks.
            bindSavedRuntimeState();

            for (auto &state : partyTransitionState) {
                auto creature = state.creature.resolve();
                if (!creature) continue;

                // Source references were resolved while their graph was still
                // authoritative. Publish those exact C4 incarnations without
                // looking their serialized numbers up in the destination.
                creature->_savedEffects.clear();
                creature->_savedActionQueue = std::move(state.actions);
                creature->_savedEffectReferencesBound.clear();
                creature->_savedActionReferencesBound =
                    std::move(state.actionReferencesBound);
                creature->_savedRuntimeParsed = true;
                creature->_savedRuntimePublished = false;
                creature->_loadedSaveActionSlots.clear();

                for (auto &delayed : state.delayedEvents) {
                    _module->enqueueBoundSaveEvent(
                        std::move(delayed.event), delayed.referencesBound);
                }
            }
            publishSavedRuntimeState();

            // Whether the world came from persisted state decides what gets
            // restored, not whether the module's authored entry hook runs.
            // Content relies on that hook every time it is entered, including
            // when revisiting a module whose world state is restored.
            destination->runOnLoadScript();
            if (!stillCurrent()) {
                return;
            }

            destination->loadParty(entry, preservesSavedPlacement(context));
            if (!stillCurrent()) {
                return;
            }

            info("Module '" + name + "' loaded successfully");
            deleteSaveGroupOnEnter(name);
            if (!initialSaveRestore) {
                queueAutosaveOnEnter(name);
            }
            // The area checkpoint (co-op design notes "Party wipe"): every area
            // entry, loads included, so "Retry this area" always has one.
            // Not for a co-op client, whose world is the host's.
            _checkpointOnEnterPending = !(_coop && _coop->isClient());

            if (_loadScreen) {
                _loadScreen->setProgress(100);
            }
            render();

            refreshAreaMusic();

            _globalFade.finishLoading(arrival);
            if (!isConversationActive()) {
                openInGame();
            }
            loaded = true;
            if (_module && _module->area() && _module->area()->hasMinigame()) {
                startMiniGame();
            }
        } catch (const std::exception &e) {
            error("Failed loading module '" + name + "': " + std::string(e.what()));
            // Source retirement is the commit boundary for an ordinary
            // transition, just as resetGame is for a disk load. Authored or
            // otherwise post-commit failure cannot resurrect the old world;
            // retire the partial destination and land somewhere deliberate.
            retireToMainMenu();
        }
        });
    } catch (const std::exception &e) {
        error("Module load escaped its failure boundary for '" + name +
              "': " + e.what());
        if (!commitStarted) {
            // Loading-screen preparation precedes runtime/resource commit. Its
            // presentation work is reversible without a parallel scene: the
            // authoritative source world remains the current session.
            _screen = sourceScreen;
        } else {
            // A terminal-path dependency failed while retiring the published
            // destination. Do not expose a blank or resurrected old world.
            try {
                retireToMainMenu();
            } catch (const std::exception &terminalError) {
                error("Failed entering Main Menu after load failure: " +
                      std::string(terminalError.what()));
                _screen = Screen::MainMenu;
            }
        }
        return false;
    }

    // However long that took, none of it was time the game world lived
    // through. Whoever drives the frame clock has to start a new epoch before
    // the next update, or the whole load lands on the world as one enormous
    // step.
    _timingDiscontinuity = true;
    return loaded;
}

void Game::retireActiveModuleRuntime() {
    _globalFade.invalidateModule();
    _dialogFade.resetSession();
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
    _lastRenderedSceneOutput = nullptr;
    _runtimeSessionPlayable = false;

    // This is also the failed-destination cleanup boundary. If construction
    // attached only part of the retained party, Area retirement inspects
    // actual residency and safely ignores every not-yet-attached object.
    retireActiveAreaRuntime();

    std::set<uint32_t> sessionObjectIds;
    for (const auto &object : _party.runtimeObjects()) {
        if (object) sessionObjectIds.insert(object->id());
    }

    _combat.reset();
    _module.reset();
    _loadedModules.clear();
    std::vector<std::shared_ptr<Object>> retiredObjects;
    for (const auto &[id, object] : _objectById) {
        if (sessionObjectIds.count(id) == 0) {
            retiredObjects.push_back(object);
        }
    }
    for (const auto &object : retiredObjects) {
        destroyRuntimeObjectGraph(object);
    }
    retireSavedObjectGraph();
}

void Game::retireActiveAreaRuntime() {
    auto module = _module;
    auto area = module ? module->area() : nullptr;
    if (area) {
        area->retirePartyAreaRuntime();
    }
}

void Game::retireSavedObjectGraph() {
    ++_savedGraphGeneration;
    _reservedSavedObjectIds.clear();
    _reservedSavedIdentityNamespace.reset();
    _reservedSavedObjectIdClaims.clear();
    _objectBySavedId.clear();
    _savedIdByObject.clear();
}

void Game::retireRuntimeSession() {
    // Stable-frame save execution is synchronous and cannot ordinarily overlap
    // retirement. A re-entrant retirement from an injected/service callback is
    // an invariant violation; the local request owner in processPendingSave()
    // still remains responsible for terminalization.
    if (_saveInProgress) {
        error("Runtime session retirement re-entered synchronous save execution");
    }
    if (_pendingSave) {
        auto request = std::move(*_pendingSave);
        _pendingSave.reset();
        SaveResult cancelled;
        cancelled.status = SaveStatus::Cancelled;
        cancelled.message = "Runtime session retired before save execution";
        finalizeSaveRequest(request, std::move(cancelled));
    }
    ++_runtimeSessionGeneration;
    _scriptEvents.clear();
    _globalFade.resetSession();
    _dialogFade.resetSession();
    disableVideoEffect();
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
    _runtimeSessionPlayable = false;
    _screen = Screen::None;
    _lastRenderedSceneOutput = nullptr;

    abortPazaak();
    _lastPazaakResult.reset();
    _pazaakContinuationCaller.reset();
    _pazaakDevelopmentSelectedObjectOverride.reset();
    endMiniGame();

    if (_conversation) {
        _conversation->cleanupForModuleTransition();
        _conversation = nullptr;
    }

    _services.audio.mixer.stopAll();
    _music.reset();
    _movie.reset();
    _musicResRef.clear();
    _ambient.reset();
    _ambientResRef.clear();
    _fadingAudio.clear();
    while (!_moduleTransitionMovies.empty()) {
        _moduleTransitionMovies.pop();
    }
    setCursorType(CursorType::Default);
    _cameraType = CameraType::ThirdPerson;
    _savedCameraType = CameraType::ThirdPerson;
    _paused = false;
    _relativeMouseMode = false;

    _statusSummary.reset();
    if (_hud) {
        _hud->resetStatusSummaryPresentation();
    }
    _dialogLocks.clear();
    _tutorialWindowsShown.clear();

    // Drop GUI-owned object selections, conversation participants and
    // container/party bindings before releasing the runtime graph.
    _hud.reset();
    _inGame.reset();
    _dialog.reset();
    _computer.reset();
    _container.reset();
    _partySelect.reset();

    if (_map) {
        _map->retireRuntimeSession();
    }

    // Full-session teardown can follow a partially failed destination load.
    // Retire retained Area residency before Party bindings or the owning
    // Module/Area/Pathfinder disappear. A prior failure cleanup is harmless.
    retireActiveAreaRuntime();

    _combat.reset();
    _party.retireRuntimeSession();

    _visualEffectPresenter.clear();
    _services.scene.graphs.get(kSceneMain).clear();
    _module.reset();
    _loadedModules.clear();

    // Storage retained by actions, effects or presentation holders remains
    // allocated, but every exact runtime incarnation becomes semantically dead
    // before the registry is emptied and numeric IDs may restart.
    while (!_objectById.empty()) {
        // Copy: unregistering erases the map node that owns the reference.
        auto object = _objectById.begin()->second;
        unregisterRuntimeObject(object);
    }
    retireSavedObjectGraph();
    _publishedRuntimeObjectIds.clear();
    _nextObjectId = kFirstRuntimeObjectId;
    _effectIds.reset();
    _worldTimeMilliseconds = 0;
    _minutesPerHour = 5;
    _worldTimeFraction = 0.0;
    _clockStartPending = true;

    _nextModule.clear();
    _nextEntry.clear();
    _atStableSavePoint = false;
}

void Game::resetGame() {
    retireRuntimeSession();

    _quitRequested = false;
    _endGameRequested = false;
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();
    _customTokens.clear();
    _saveResourceShadows.clear();

    _party.reset();
    _journal.reset();
    _messageLog.reset();
    _floatingText.reset();
    _cheatUsed = false;
    _playedTimeFraction = 0.0;
    _services.resource.director.onNewGame();
}

/**
 * Resolve and validate a save without disturbing the running game.
 *
 * Every read goes through the unpublished candidate rather than the director,
 * because the director still answers for the committed session: consulting it
 * here would validate the save that is already loaded. Records that the loader
 * treats as mandatory are proven now, so the failures that used to strand a
 * half-torn-down session are raised while the old one is still authoritative.
 */
Game::PreparedSaveLoad Game::prepareSaveLoad(const resource::SaveSlotDescriptor &slot) {
    PreparedSaveLoad prepared;
    prepared.session = _services.resource.director.prepareGameLoad(slot);

    prepared.saveInfo = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("savenfo", ResType::Res)));
    if (!prepared.saveInfo) {
        throw ResourceNotFoundException("saveinfo.res not found");
    }
    prepared.nfo = resource::parseNFO(*prepared.saveInfo);

    prepared.globalVars = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("globalvars", ResType::Res)));
    if (!prepared.globalVars) {
        throw ResourceNotFoundException("globalvars.res not found");
    }
    (void)resource::parseGVT(*prepared.globalVars);

    // Save-wide records remain optional where retail permits them, but any
    // record supplied by the slot is decoded while the candidate is private.
    prepared.partyTable = decodeSaveGff(
        prepared.session->findMetadata(ResourceId("partytable", ResType::Res)));
    prepared.inventory = decodeSaveGff(
        prepared.session->findWorking(ResourceId("inventory", ResType::Res)));
    prepared.destination = prepareDestinationModule(
        prepared.nfo.lastModule,
        /*initialSaveRestore=*/true,
        prepared.session->workingState());
    if (prepared.destination.context ==
        ModuleLoadContext::InitialTemplateRestore) {
        prepared.playerInfo = decodeSaveGff(
            prepared.session->findMetadata(ResourceId("pifo", ResType::Ifo)));
        if (!prepared.playerInfo ||
            prepared.playerInfo->getList("Mod_PlayerList").empty()) {
            throw ValidationException(
                "Template-world save restore requires pifo.ifo player state");
        }
        auto params = prepared.saveInfo->findStruct("AUTOSAVEPARAMS");
        if (!params) {
            throw ValidationException(
                "Template-world save restore requires AUTOSAVEPARAMS");
        }
        PreparedSaveLoad::AutosaveRestoreState autosave;
        autosave.startWaypoint = params->getString("STARTWAYPOINT");
        if (autosave.startWaypoint == "*") {
            autosave.startWaypoint.clear();
        }
        autosave.pauseDay = params->getUint("TIME_PAUSEDAY");
        autosave.pauseTime = params->getUint("TIME_PAUSETIME");
        prepared.autosave = std::move(autosave);
    }
    validatePartyLoad(prepared.partyTable.get());

    return prepared;
}

bool Game::loadGame(const resource::SaveSlotDescriptor &slot) {
    info(str(boost::format("Loading savegame '%s'") % slot.directory.filename().string()));

    // Resolve and validate the replacement before anything is given up. A
    // throw here leaves the current session and its mounts untouched, so the
    // player keeps playing instead of being left with nothing to render.
    auto prepared = prepareSaveLoad(slot);

    try {
        // Commit. The old runtime and the old mounts retire together, and only
        // then does the candidate become authoritative: no runtime ever
        // observes the other session's resources.
        resetGame();
        _services.resource.director.commitGameLoad(std::move(prepared.session));
        return restoreSaveLoad(std::move(prepared));
    } catch (const std::exception &e) {
        // Past the commit boundary the previous session no longer exists and
        // cannot be restored. Retire whatever was half-built and land on a
        // deliberate screen rather than the blank one an abandoned session
        // leaves behind.
        error("Failed restoring savegame '" +
              slot.directory.filename().string() + "': " + std::string(e.what()));
        try {
            retireToMainMenu();
        } catch (const std::exception &terminalError) {
            error("Failed entering Main Menu after save-load failure: " +
                  std::string(terminalError.what()));
            _screen = Screen::MainMenu;
        }
        return false;
    }
}

bool Game::restoreSaveLoad(PreparedSaveLoad prepared) {
    const NFO &nfo = prepared.nfo;
    _cheatUsed = nfo.cheatUsed;
    captureSaveResourceShadow({SaveResourceKind::Nfo, {}}, *prepared.saveInfo);

    // The exact plan inspected before reset now replaces the active module
    // owners. A file changing between preparation and this point is a
    // post-commit failure and follows #325's deliberate terminal policy.
    _services.resource.director.commitModuleLoad(
        std::move(prepared.destination.resources));

    // Restore the save-wide faction table before any module objects can query
    // disposition. A missing or malformed optional FAC starts from fresh base
    // data; it must never preserve relationships from the previous save.
    std::optional<IReputes::State> reputesState;
    try {
        if (auto reputesGff = decodeSaveGff(
                _services.resource.director.findSaveWorking(
                    ResourceId("repute", ResType::Fac)))) {
            captureSaveResourceShadow(
                {SaveResourceKind::FactionTable, {}}, *reputesGff);
            reputesState = _services.game.reputes.parse(*reputesGff);
        }
    } catch (const std::exception &e) {
        warn("Game: invalid repute.fac: " + std::string(e.what()));
    }
    if (!reputesState) {
        reputesState = _services.game.reputes.baseState();
    }
    _services.game.reputes.replace(std::move(*reputesState));

    // Deserialize global variables, proven present while the candidate was
    // still unpublished.
    deserializeGlobalVariables(*prepared.globalVars);

    // Deserialize party from records inspected before the old session was
    // retired. A disk restore and a saved module graph are independent: retail
    // transition autosaves restore save-wide state while constructing the
    // module world from installed templates and the player from pifo.ifo.
    const auto &ifo = prepared.destination.ifo;
    replaceCustomTokens(parseCustomTokens(*ifo));
    std::string entry;
    if (restoresSavedWorld(prepared.destination.context)) {
        captureSaveResourceShadow(
            {SaveResourceKind::ModuleIfo, prepared.destination.name}, *ifo);
        const auto moduleIdentityContext =
            SerializedIdentityContext::moduleGraph(prepared.destination.name);
        prepareSavedRuntimeNamespace(*ifo, moduleIdentityContext);

        // Detached save-wide records are deliberately not traversed here:
        // their ObjectId fields do not claim identities in the module graph.
        reserveSavedObjectIds(
            *prepared.destination.git,
            moduleIdentityContext,
            SerializedGraphRoot::AreaGit);
        deserializeParty(*ifo, prepared.partyTable, moduleIdentityContext);
    } else {
        if (!prepared.autosave || !prepared.playerInfo) {
            throw ValidationException(
                "Template-world save restore state was not prepared");
        }
        restoreWorldTime(
            *ifo, prepared.autosave->pauseDay, prepared.autosave->pauseTime);
        deserializeParty(
            *prepared.playerInfo,
            prepared.partyTable,
            SerializedIdentityContext::detachedRecord("pifo.ifo"));
        entry = prepared.autosave->startWaypoint;
    }

    // A full disk restore clears the detached NPC/PUP ActionList when it
    // respawns those representations. This is intentionally different from
    // ordinary travel, whose party member queues are restored below. The module player is loaded through the IFO or
    // pifo path and is not one of these spawned detached followers.
    auto discardDetachedRosterActions = [](const std::shared_ptr<Creature> &creature) {
        if (!creature) return;
        creature->_savedActionQueue = SavedActionQueue {};
        creature->_savedActionReferencesBound.clear();
        creature->_loadedSaveActionSlots.clear();
    };
    for (const auto &member : _party.members()) {
        if (member.creature && member.creature != _party.player() &&
            member.creature != _party.actualPlayer()) {
            discardDetachedRosterActions(member.creature);
        }
    }
    if (isTSL()) {
        for (int puppet : _party.persistedState().puppetIds) {
            discardDetachedRosterActions(
                _party.getAvailablePuppet(puppet, true));
        }
    }

    // Once the player is loaded, deserialize player's inventory.
    if (prepared.inventory) {
        captureSaveResourceShadow(
            {SaveResourceKind::Inventory, {}}, *prepared.inventory);
        deserializeInventory(*prepared.inventory);
    }

    return loadPreparedModule(
        std::move(prepared.destination),
        std::move(entry),
        /*initialSaveRestore=*/true,
        /*resourcesCommitted=*/true);
}

void Game::validatePartyLoad(const resource::Gff *partyTable) const {
    if (!partyTable) {
        return;
    }

    const auto state = parsePartyTable(*partyTable);
    const auto validNpc = [this](int npc) {
        const int count = isTSL()
                              ? static_cast<int>(Party::kK2NpcCount)
                              : static_cast<int>(Party::kK1NpcCount);
        return npc >= 0 && npc < count;
    };
    if (state.controlledNpc != -1 && !validNpc(state.controlledNpc)) {
        throw ValidationException("PartyTable controlled NPC is out of range");
    }
    // The controlled creature binds to its roster slot once the module is up;
    // a slot the table does not list as available would fail only then, past
    // the point where the running session can still be kept.
    if (state.controlledNpc != -1 && !state.npcAvailable[state.controlledNpc]) {
        throw ValidationException("PartyTable controlled NPC is not an available roster member");
    }

    std::set<int> members;
    for (int npc : state.memberIds) {
        if (npc != kNpcPlayer && !validNpc(npc)) {
            throw ValidationException("PartyTable member is out of range");
        }
        if (!members.insert(npc).second) {
            throw ValidationException("PartyTable contains a duplicate member");
        }
    }
    if (state.leader != -1 && state.leader != kNpcPlayer &&
        members.count(state.leader) == 0) {
        throw ValidationException("PartyTable leader is not an active member");
    }

    std::set<int> puppets;
    for (int puppet : state.puppetIds) {
        if (!isTSL() || puppet < 0 ||
            puppet >= static_cast<int>(Party::kMaxPuppetCount)) {
            throw ValidationException("PartyTable puppet is out of range");
        }
        if (!puppets.insert(puppet).second) {
            throw ValidationException("PartyTable contains a duplicate puppet");
        }
    }
}

std::map<int, std::string> Game::parseCustomTokens(
    const resource::Gff &ifoGff) const {

    std::map<int, std::string> result;
    for (const auto &entry : ifoGff.getList("Mod_Tokens")) {
        uint32_t token = 0;
        if (!entry->readDword(token, "Mod_TokensNumber") ||
            token > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
            continue;
        }
        result[static_cast<int>(token)] = entry->getString("Mod_TokensValue");
    }
    return result;
}

void Game::replaceCustomTokens(std::map<int, std::string> tokens) {
    _customTokens = std::move(tokens);
}

void Game::deserializeGlobalVariables(resource::Gff &gvtGff) {
    captureSaveResourceShadow({SaveResourceKind::GlobalVars, {}}, gvtGff);
    GVT gvt = resource::parseGVT(gvtGff);
    _globalStrings.clear();
    _globalBooleans.clear();
    _globalNumbers.clear();
    _globalLocations.clear();

    for (auto &[name, value] : gvt.strings) {
        setGlobalString(name, value);
    }

    for (auto &[name, value] : gvt.booleans) {
        setGlobalBoolean(name, value);
    }

    for (auto &[name, value] : gvt.numbers) {
        setGlobalNumber(name, value);
    }

    for (auto &[name, value] : gvt.locations) {
        auto &[pos, rot] = value;
        setGlobalLocation(name, std::make_shared<Location>(pos, rot));
    }
}

void Game::deserializeParty(
    resource::Gff &ifoGff,
    const std::shared_ptr<Gff> &ptGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    resetGalaxyMap();

    std::shared_ptr<Gff> pcGff;
    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (!players.empty()) {
        const int controlledNpc =
            ptGff ? parsePartyTable(*ptGff).controlledNpc : -1;
        // Retail K2 may mark the controlled module creature primary even while
        // pc.utc holds a distinct canonical player. PARTYTABLE is authoritative.
        if (controlledNpc != -1) {
            try {
                pcGff = decodeSaveGff(
                    _services.resource.director.findSaveWorking(ResourceId("pc", ResType::Utc)));
            } catch (const std::exception &e) {
                warn("Game: invalid pc.utc: " + std::string(e.what()));
            }
        }
    }

    publishPartyRuntimeState(ifoGff, ptGff, pcGff, moduleIdentityContext);
}

void Game::publishPartyRuntimeState(
    resource::Gff &ifoGff,
    const std::shared_ptr<resource::Gff> &ptGff,
    const std::shared_ptr<resource::Gff> &pcGff,
    const SerializedIdentityContext &moduleIdentityContext) {
    if (ptGff) {
        captureSaveResourceShadow({SaveResourceKind::PartyTable, {}}, *ptGff);
        deserializeGalaxyMap(*ptGff);
        uint32_t gold = 0;
        if (ptGff->readDword(gold, "PT_GOLD")) {
            _party.takeGold(_party.gold());
            _party.giveGold(gold);
        }

        uint32_t xp = 0;
        if (ptGff->readDword(xp, "PT_XP_POOL")) {
            // Populate the shared party XP pool. Members added below are synced to it.
            _party.setXP(xp);
        }

        std::vector<float> costMultipliers;
        for (const auto &entry : ptGff->getList("PT_COST_MULT_LIST")) {
            costMultipliers.push_back(entry->getFloat("PT_COST_MULT_VALUE", 1.0f));
        }
        _party.setItemCostMultipliers(std::move(costMultipliers));

        Party::PersistedState partyState = parsePartyTable(*ptGff);
        replacePartyTable(std::move(partyState));
        deserializePazaakPartyTable(*ptGff);
    }

    const auto &players = ifoGff.getList("Mod_PlayerList");
    if (players.empty()) {
        return;
    }

    auto modulePlayer = newCreature(*players.front(), moduleIdentityContext);
    modulePlayer->captureSaveRecord(
        *players.front(), moduleIdentityContext, {SaveRecordOriginKind::ModulePlayer, {}});
    if (modulePlayer->tag().empty()) {
        modulePlayer->setTag(kObjectTagPlayer);
    }

    auto actualPlayer = modulePlayer;
    const auto &partyState = _party.persistedState();
    // PT_CONTROLLED_NP, not Mod_IsPrimaryPlr, defines whether pc.utc is the
    // canonical player distinct from the currently controlled module creature.
    if (partyState.controlledNpc != -1 && pcGff) {
        const auto pcIdentityContext =
            SerializedIdentityContext::detachedRecord("pc.utc");
        actualPlayer = newCreature(*pcGff, pcIdentityContext);
        actualPlayer->captureSaveRecord(
            *pcGff, pcIdentityContext, {SaveRecordOriginKind::PrimaryPlayerUtc, {}});
    }

    _party.setPlayer(modulePlayer);
    _party.setActualPlayer(actualPlayer);
    if (partyState.controlledNpc != -1 &&
        !_party.bindRosterCreature(
            {RosterKind::Npc, partyState.controlledNpc}, modulePlayer)) {
        throw ValidationException(
            "Controlled NPC could not bind its logical roster slot");
    }

    if (ptGff) {
        deserializePartyMembers(*ptGff);
        deserializeJournal(*ptGff);
    } else {
        _party.addMember(kNpcPlayer, actualPlayer);
    }
}

Party::PersistedState Game::parsePartyTable(const resource::Gff &ptGff) const {
    Party::PersistedState state;
    state.pcName = ptGff.getString("PT_PCNAME");
    // GFF labels are capped at sixteen bytes. Retail KotOR II stores the
    // component count under this exact truncated label.
    state.itemComponent = ptGff.getUint("PT_ITEM_COMPONEN");
    if (!ptGff.has("PT_ITEM_COMPONEN")) {
        state.itemComponent = ptGff.getUint("PT_ITEM_COMPONENT");
    }
    state.itemChemical = ptGff.getUint("PT_ITEM_CHEMICAL");
    state.swoopUpgrades[0] = ptGff.getUint("PT_SWOOP1");
    state.swoopUpgrades[1] = ptGff.getUint("PT_SWOOP2");
    state.swoopUpgrades[2] = ptGff.getUint("PT_SWOOP3");
    state.playedSeconds = ptGff.getUint("PT_PLAYEDSECONDS");
    uint32_t playedMinutes = ptGff.getUint("PT_PLAYEDMINUTES");
    if (playedMinutes != 0) {
        state.playedSeconds = playedMinutes * 60;
    }
    state.controlledNpc = ptGff.getInt("PT_CONTROLLED_NP", -1);
    state.soloMode = ptGff.getBool("PT_SOLOMODE");

    const auto memberList = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), memberList.size()), 2);
    for (size_t index = 0; index < memberCount; ++index) {
        int npc = memberList[index]->getInt("PT_MEMBER_ID", -1);
        state.memberIds.push_back(npc);
        if (memberList[index]->getBool("PT_IS_LEADER")) {
            state.leader = npc;
        }
    }

    const auto puppetList = ptGff.getList("PT_PUPPETS");
    size_t puppetCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_PUPPETS"), puppetList.size()),
        Party::kMaxPuppetCount);
    for (size_t index = 0; index < puppetCount; ++index) {
        state.puppetIds.push_back(puppetList[index]->getInt("PT_PUPPET_ID", -1));
    }

    const auto availableNpcs = ptGff.getList("PT_AVAIL_NPCS");
    size_t npcCount = std::min(
        availableNpcs.size(), isTSL() ? Party::kK2NpcCount : Party::kK1NpcCount);
    for (size_t npc = 0; npc < npcCount; ++npc) {
        state.npcAvailable[npc] = availableNpcs[npc]->getBool("PT_NPC_AVAIL");
        state.npcSelectable[npc] = availableNpcs[npc]->getBool("PT_NPC_SELECT", true);
    }

    const auto influences = ptGff.getList("PT_INFLUENCE");
    for (size_t npc = 0; npc < std::min(influences.size(), Party::kMaxNpcCount); ++npc) {
        state.influence[npc] = influences[npc]->getInt("PT_NPC_INFLUENCE", -1);
    }

    const auto availablePuppets = ptGff.getList("PT_AVAIL_PUPS");
    for (size_t puppet = 0;
         puppet < std::min(availablePuppets.size(), Party::kMaxPuppetCount);
         ++puppet) {
        state.puppetAvailable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_AVAIL");
        state.puppetSelectable[puppet] =
            availablePuppets[puppet]->getBool("PT_PUP_SELECT", true);
    }

    state.aiState = ptGff.getInt("PT_AISTATE");
    state.followState = ptGff.getInt("PT_FOLLOWSTATE");
    if (auto galaxy = ptGff.findStruct("GlxyMap")) {
        state.galaxyPointCount = galaxy->getUint("GlxyMapNumPnts");
        uint32_t mask = galaxy->getUint("GlxyMapPlntMsk");
        for (size_t planet = 0; planet < Party::kGalaxyPlanetCount; ++planet) {
            state.planetAvailable[planet] = (mask & (1u << planet)) != 0;
            state.planetSelectable[planet] = (mask & (1u << (planet + 16))) != 0;
        }
        state.selectedPlanet = galaxy->getInt("GlxyMapSelPnt", -1);
    }
    state.mapDisabled = ptGff.getBool("PT_DISABLEMAP");
    state.regenerationDisabled = ptGff.getBool("PT_DISABLEREGEN");

    for (const auto &entry : ptGff.getList("PT_DLG_MSG_LIST")) {
        Party::SavedDialogMessage message;
        message.speaker = entry->getString("PT_DLG_MSG_SPKR");
        message.text = entry->getString("PT_DLG_MSG_MSG");
        state.dialogMessages.push_back(std::move(message));
    }
    for (const auto &entry : ptGff.getList("PT_FB_MSG_LIST")) {
        Party::SavedLogMessage message;
        entry->readByte(message.color, "PT_FB_MSG_COLOR");
        entry->readDword(message.type, "PT_FB_MSG_TYPE");
        message.text = entry->getString("PT_FB_MSG_MSG");
        state.feedbackMessages.push_back(std::move(message));
    }
    for (const auto &entry : ptGff.getList("PT_COM_MSG_LIST")) {
        Party::SavedLogMessage message;
        entry->readByte(message.color, "PT_COM_MSG_COOR");
        entry->readDword(message.type, "PT_COM_MSG_TYPE");
        message.text = entry->getString("PT_COM_MSG_MSG");
        state.combatMessages.push_back(std::move(message));
    }
    return state;
}

void Game::replacePartyTable(Party::PersistedState state) {
    _party.loadPersistedState(std::move(state));
}

void Game::resetGalaxyMap() {
    // K1 takes its planet count from content; K2 ignores the table and always
    // carries sixteen rows.
    auto planetary = _services.resource.twoDas.get("planetary");
    _party.galaxyMap().reset(_gameId, planetary ? planetary->getRowCount() : 0);
}

void Game::deserializeGalaxyMap(resource::Gff &ptGff) {
    _party.galaxyMap().loadFromPartyTable(ptGff);
}

void Game::deserializePazaakPartyTable(resource::Gff &ptGff) {
    const auto &pazaakCards = ptGff.getList("PT_PAZAAKCARDS");
    const auto &pazaakSide = ptGff.getList("PT_PAZSIDELIST");
    // Each title stores its own number of ownership entries, so the saved table
    // is accepted at either authored length and the card-type ID range follows
    // from it.
    size_t expectedCards = isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount;
    size_t cardTypes = expectedCards - 1;
    if (pazaakCards.size() == expectedCards &&
        pazaakSide.size() == Party::kK1PazaakSideDeckSize) {
        Party::PazaakCardCounts counts {};
        Party::PazaakSideDeck sideDeck;
        bool valid = true;
        for (size_t i = 0; i < expectedCards; ++i) {
            counts[i] = pazaakCards[i]->getInt("PT_PAZAAKCOUNT", -1);
            valid = valid && counts[i] >= 0 && counts[i] <= 255;
        }
        bool allEmpty = true;
        bool allSelected = true;
        Party::PazaakCardCounts selectedCounts {};
        for (size_t i = 0; i < sideDeck.size(); ++i) {
            sideDeck[i] = pazaakSide[i]->getInt("PT_PAZSIDECARD", -2);
            allEmpty = allEmpty && sideDeck[i] == -1;
            bool owned = sideDeck[i] >= 0 && static_cast<size_t>(sideDeck[i]) < cardTypes;
            allSelected = allSelected && owned;
            if (owned) {
                ++selectedCounts[sideDeck[i]];
            }
        }
        valid = valid && (allEmpty || allSelected);
        if (allSelected) {
            for (size_t i = 0; i < cardTypes; ++i) {
                valid = valid && selectedCounts[i] <= counts[i];
            }
        }
        if (valid) {
            _party.setPazaakData(std::move(counts), std::move(sideDeck), expectedCards);
        } else {
            warn("Game: invalid Pazaak state in PARTYTABLE.res");
        }
    } else if (!pazaakCards.empty() || !pazaakSide.empty()) {
        warn("Game: invalid Pazaak list sizes in PARTYTABLE.res");
    }

}

void Game::saveNpcState(int npc) {
    // Retail bounds the roster flat, then treats an empty slot as nothing to
    // save rather than an error.
    if (npc < 0 || npc >= static_cast<int>(Party::kMaxNpcCount)) {
        return;
    }
    auto creature = _party.getAvailableMember(npc);
    if (!creature) {
        return;
    }

    saveRosterState({RosterKind::Npc, npc}, *creature);
}

void Game::saveRosterState(
    const RosterIdentity &identity,
    const Creature &creature) {
    if (!_party.isRosterIdentityValid(identity)) {
        throw ValidationException("Roster slot is outside the title range");
    }
    const std::string prefix =
        identity.kind == RosterKind::Npc ? "availnpc" : "availpup";
    auto committed = _services.resource.director.committedSaveWorkingState();
    if (!committed) {
        committed = std::make_shared<const resource::SaveWorkingState>();
    }
    auto candidate =
        resource::SaveWorkingStateCandidate::fromCommitted(std::move(committed));
    candidate.put(
        ResourceId(prefix + std::to_string(identity.slot), ResType::Utc),
        SaveWideSnapshotBuilder::availableNpcRecord(*this, creature));
    _services.resource.director.adoptSaveWorkingState(candidate.freeze());
}

std::shared_ptr<Creature> Game::materializeRosterCreature(
    const RosterIdentity &identity) {
    if (!_party.isRosterAvailable(identity)) return nullptr;
    if (auto existing = _party.rosterCreature(identity)) return existing;

    const std::string prefix =
        identity.kind == RosterKind::Npc ? "availnpc" : "availpup";
    const std::string name = prefix + std::to_string(identity.slot);
    std::shared_ptr<Gff> record;
    try {
        record = decodeSaveGff(
            _services.resource.director.findSaveWorking(
                ResourceId(name, ResType::Utc)));
    } catch (const std::exception &e) {
        warn("Game: invalid " + name + ".utc: " + std::string(e.what()));
        return nullptr;
    }
    if (!record) {
        warn("Game: missing " + name + ".utc");
        return nullptr;
    }

    const auto context =
        SerializedIdentityContext::detachedRecord(name + ".utc");
    auto creature = newCreature(*record, context);
    try {
        creature->captureSaveRecord(
            *record,
            context,
            {identity.kind == RosterKind::Npc
                 ? SaveRecordOriginKind::AvailableNpc
                 : SaveRecordOriginKind::AvailablePuppet,
             std::to_string(identity.slot)});
        if (!_party.bindRosterCreature(identity, creature)) {
            throw ValidationException("Could not bind materialized roster creature");
        }
        // Initial restoration performs one graph-wide bind/publication after
        // every object exists. A retail-style lazy GetNPCObject call in an
        // already playable session must complete the same detached-record
        // publication for this one newly materialized object immediately.
        if (_runtimeSessionPlayable) {
            creature->resolveSavedReferences(
                [this, context](uint32_t id) {
                    return resolveSerializedObjectReference(id, context);
                });
            creature->bindSavedRuntimeState();
            creature->publishSavedRuntimeState();
        }
    } catch (...) {
        destroyRuntimeObjectGraph(creature);
        throw;
    }
    return creature;
}

bool Game::killRosterCreature(const RosterIdentity &identity) {
    auto creature = _party.rosterCreature(identity);
    if (!creature) {
        _party.clearRosterCreature(identity);
        return false;
    }
    if (_module && _module->area()) {
        _module->area()->retireCreatureAreaRuntime(creature);
    }
    destroyRuntimeObjectGraph(creature);
    return true;
}

void Game::deserializePartyMembers(resource::Gff &ptGff) {
    auto savedMembers = ptGff.getList("PT_MEMBERS");
    size_t memberCount = std::min<size_t>(
        std::min<size_t>(ptGff.getUint("PT_NUM_MEMBERS"), savedMembers.size()), 2);
    savedMembers.resize(memberCount);
    auto leader = std::find_if(savedMembers.begin(), savedMembers.end(), [](auto &member) {
        return member->getBool("PT_IS_LEADER");
    });

    auto addMember = [&](resource::Gff &memberGff) {
        int32_t npc = -1;
        if (!memberGff.readInt(npc, "PT_MEMBER_ID")) {
            warn("Game: missing PT_MEMBER_ID");
            return;
        }
        if (_party.isMember(npc)) {
            return;
        }

        auto member =
            npc == _party.persistedState().controlledNpc &&
                    _party.player() != _party.actualPlayer()
                ? _party.player()
                : _party.getAvailableMember(npc, true);
        if (!member) {
            warn("Game: NPC is not available: " + std::to_string(npc));
            return;
        }

        _party.addMember(npc, member);
    };

    // Party leader is the first runtime member. A controlled companion is the
    // module player while pc.utc remains the actual player in limbo.
    if (leader != savedMembers.end()) {
        addMember(**leader);
    }

    auto actualPlayer = _party.actualPlayer();
    // A zero-member controlled-NPC PARTYTABLE is used by retail K2 prologue
    // saves for an NPC operating alone while the canonical PC remains in
    // limbo. Non-empty lists retain the usual implicit canonical PC member.
    const bool canonicalPlayerIsActive =
        _party.persistedState().controlledNpc == -1 || !savedMembers.empty();
    if (canonicalPlayerIsActive && actualPlayer && !_party.isMember(*actualPlayer)) {
        _party.addMember(kNpcPlayer, actualPlayer);
    }

    if (_party.player() != actualPlayer &&
        !_party.isMember(_party.persistedState().controlledNpc)) {
        _party.addMember(_party.persistedState().controlledNpc, _party.player());
    }

    for (auto &savedMember : savedMembers) {
        if (leader == savedMembers.end() || savedMember != *leader) {
            addMember(*savedMember);
        }
    }
}

void Game::deserializeJournal(const resource::Gff &ptGff) {
    for (const auto &jnlEntry : ptGff.getList("JNL_Entries")) {
        std::string plotId(jnlEntry->getString("JNL_PlotID"));
        if (plotId.empty()) {
            warn("Game: missing JNL_PlotID");
            continue;
        }
        int state = jnlEntry->getInt("JNL_State");
        uint32_t date = jnlEntry->getUint("JNL_Date");
        uint32_t time = jnlEntry->getUint("JNL_Time");
        _journal.restoreEntry(std::move(plotId), state, date, time);
    }
}

void Game::deserializeInventory(resource::Gff &inventoryGff) {
    std::shared_ptr<Creature> player = _party.actualPlayer();
    if (!player) {
        return;
    }
    player->deserializeOwnedItems(
        inventoryGff,
        SerializedIdentityContext::detachedRecord("inventory.res"),
        SaveRecordOriginKind::PartyInventoryItem,
        false,
        "inventory");
}

bool Game::loadParty() {
    std::shared_ptr<Gff> ifo(_services.resource.gffs.get("module", ResType::Ifo));
    if (!ifo) {
        throw ResourceNotFoundException("module.ifo not found");
    }

    return true;
}

void Game::loadDefaultParty() {
    _party.initializeNewGameState();
    std::string member1, member2, member3;
    _party.defaultMembers(member1, member2, member3);

    if (!member1.empty()) {
        std::shared_ptr<Creature> player =
            newCreatureFromBlueprint(member1);
        player->setTag(kObjectTagPlayer);
        player->setImmortal(_options.game.immortalPlayer);
        player->setIsPC(true);
        _party.addMember(kNpcPlayer, player);
        _party.setPlayer(player);
        _party.setActualPlayer(player);
    }
    if (!member2.empty()) {
        std::shared_ptr<Creature> companion =
            newCreatureFromBlueprint(member2);
        companion->setImmortal(_options.game.immortalPlayer);
        companion->equip("g_w_dblsbr001");
        _party.addAvailableMember(0, companion);
        _party.addMember(0, companion);
    }
    if (!member3.empty()) {
        std::shared_ptr<Creature> companion =
            newCreatureFromBlueprint(member3);
        companion->setImmortal(_options.game.immortalPlayer);
        _party.addAvailableMember(1, companion);
        _party.addMember(1, companion);
    }
}

void Game::setCursorType(CursorType type) {
    _pointer->setType(type);
}

void Game::playVideo(const std::string &name) {
    _moduleTransitionMovies = std::queue<std::string>();
    startVideo(name);
}

// Stopping or switching music fades the old track (design note c04_audio).
static constexpr float kMusicFadeSeconds = 0.8f;
static constexpr float kAmbientFadeSeconds = 6.0f;

void Game::playMusic(const std::string &resRef, resource::IAudioClips *clips) {
    if (_musicResRef == resRef && _musicClips == clips) {
        return;
    }
    _musicClips = clips;
    if (_music) {
        fadeOutAudio(std::move(_music), kMusicFadeSeconds);
        _music.reset();
    }
    _musicResRef = resRef;
}

void Game::playAmbient(const std::string &resRef, int volume) {
    if (_ambientResRef == resRef) {
        if (_ambientVolume != volume) {
            _ambientVolume = volume;
            if (_ambient) {
                // Restarted by updateMusic at the new volume.
                _ambient->stop();
                _ambient.reset();
            }
        }
        return;
    }
    if (_ambient) {
        fadeOutAudio(std::move(_ambient), kAmbientFadeSeconds);
        _ambient.reset();
    }
    _ambientResRef = resRef;
    _ambientVolume = volume;
}

void Game::fadeOutAudio(std::shared_ptr<audio::AudioSource> source, float seconds) {
    if (!source) {
        return;
    }
    if (seconds <= 0.0f || !source->isPlaying()) {
        source->stop();
        return;
    }
    FadingSource fading;
    fading.from = source->gainFactor();
    fading.source = std::move(source);
    fading.total = seconds;
    fading.left = seconds;
    _fadingAudio.push_back(std::move(fading));
}

void Game::setMusicVolume(float volume) {
    _musicVolumeScale = std::clamp(volume, 0.0f, 1.0f);
    if (_music) {
        _music->setGainFactor(_musicVolumeScale);
    }
}

void Game::renderScene() {
    if (!_module || !_options.graphics.sceneRender) {
        _lastRenderedSceneOutput = nullptr;
        return;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    // Camera shake from visual effects moves only the drawn view: the offset
    // goes on for this render and comes straight off again.
    std::shared_ptr<scene::CameraSceneNode> shakenCamera;
    glm::mat4 unshaken(1.0f);
    const glm::vec3 &shake = _visualEffectPresenter.cameraShake();
    if (shake != glm::vec3(0.0f)) {
        if (Camera *camera = getActiveCamera()) {
            shakenCamera = camera->cameraSceneNode();
            if (shakenCamera) {
                unshaken = shakenCamera->localTransform();
                shakenCamera->setLocalTransform(glm::translate(shake) * unshaken);
            }
        }
    }
    captureReflectionProbeIfDue();
    updateConversationFocus();
    // The grading profile follows the module (presentation state only).
    _options.graphics.gradingProfile = graphics::gradingProfileForModule(_module->name());
    auto &output = scene.render({_options.graphics.width, _options.graphics.height});
    if (shakenCamera) {
        shakenCamera->setLocalTransform(unshaken);
    }
    _lastRenderedSceneOutput = &output;
    _services.graphics.uniforms.setLocals(std::bind(&LocalUniforms::reset, std::placeholders::_1));
    if (_videoEffect.row >= 0) {
        _services.graphics.uniforms.setLocals([this](auto &locals) {
            locals.reset();
            locals.color = glm::vec4(_videoEffect.modulation, 1.0f);
            locals.ambientColor = glm::vec4(
                _videoEffect.saturationAmount,
                _videoEffect.saturation ? 1.0f : 0.0f,
                _videoEffect.scanNoise ? 1.0f : 0.0f,
                static_cast<float>(_frameCount % 100000) / 60.0f);
        });
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::videoEffect));
    } else {
        _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::ndcTexture));
    }
    _services.graphics.context.bindTexture(output);
    _services.graphics.meshRegistry.get(MeshName::quadNDC).draw(_services.graphics.statistic);
    _services.graphics.uniforms.setLocals(std::bind(&LocalUniforms::reset, std::placeholders::_1));
}

void Game::updateConversationFocus() {
    auto &gfx = _options.graphics;
    float dt = std::min(_dofFrameTime, 0.25f);
    _dofFrameTime = 0.0f;
    if (!gfx.dof) {
        gfx.dofAmount = 0.0f;
        return;
    }
    // The focus: the dialog camera's own target when it frames the pair,
    // else the speaker's head seen from whatever camera is active.
    bool active = false;
    float focus = gfx.dofFocus;
    if (_screen == Screen::Conversation && _dialog && getActiveCamera()) {
        auto report = _dialog->presentationReport();
        if (report.dialogShot) {
            focus = glm::distance(report.eye, report.target);
            active = true;
        } else if (report.speaker) {
            auto cameraNode = getActiveCamera()->cameraSceneNode();
            if (cameraNode) {
                focus = glm::distance(cameraNode->origin(), report.speaker->position() + glm::vec3(0.0f, 0.0f, 1.6f));
                active = true;
            }
        }
    }
    // Ease in over about 0.4 s and out faster; a focus jump (a new shot) is taken at once.
    float target = active ? 1.0f : 0.0f;
    float rate = active ? 2.5f : 5.0f;
    gfx.dofAmount += (target - gfx.dofAmount) * (1.0f - std::exp(-rate * dt));
    if (std::abs(target - gfx.dofAmount) < 0.002f) {
        gfx.dofAmount = target;
    }
    if (active) {
        gfx.dofFocus = glm::clamp(focus, 0.5f, 30.0f);
    }
}

void Game::captureReflectionProbeIfDue() {
    // A static probe per area, captured on the first in-game frame there
    // (and again when the pipeline is remade, e.g. reflections switched on)
    // at the leader's eye height. Rendering only.
    if (!_options.graphics.reflections) {
        _reflectionProbeArea.clear();
        return;
    }
    if (_screen != Screen::InGame || !_module || !_module->area()) {
        return;
    }
    auto leader = _party.getLeader();
    if (!leader) {
        return;
    }
    auto &scene = _services.scene.graphs.get(kSceneMain);
    std::string key = _module->area()->name() + "|" + scene::renderPipelineKey(_options.graphics);
    if (key == _reflectionProbeArea) {
        return;
    }
    _reflectionProbeArea = key;
    scene.captureReflectionProbe(leader->position() + glm::vec3(0.0f, 0.0f, 1.6f),
                                 {_options.graphics.width, _options.graphics.height});
}

void Game::writeSceneDepthToWindow() {
    auto *depth = _services.scene.graphs.get(kSceneMain).sceneDepth();
    if (!depth) {
        return;
    }
    auto &context = _services.graphics.context;
    context.resetDrawFramebuffer();
    context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::postDepthCopy));
    context.bindTexture(*depth, TextureUnits::gBufDepth);
    context.withViewport(glm::ivec4(0, 0, _options.graphics.width, _options.graphics.height), [this, &context]() {
        context.withDepthTestMode(DepthTestMode::Always, [this, &context]() {
            context.withDepthMask(true, [this, &context]() {
                context.withColorWrites(false, [this]() {
                    _services.graphics.meshRegistry.get(MeshName::quadNDC).draw(_services.graphics.statistic);
                });
            });
        });
    });
}

void Game::toggleInGameCameraType() {
    switch (_cameraType) {
    case CameraType::FirstPerson:
        if (_party.getLeader()) {
            _cameraType = CameraType::ThirdPerson;
        }
        break;
    case CameraType::ThirdPerson: {
        _module->player().stopMovement();
        std::shared_ptr<Area> area(_module->area());
        auto thirdPerson = area->getCamera<ThirdPersonCamera>(CameraType::ThirdPerson);
        auto firstPerson = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
        firstPerson->setPosition(thirdPerson->sceneNode()->origin());
        firstPerson->setFacing(thirdPerson->facing());
        _cameraType = CameraType::FirstPerson;
        break;
    }
    default:
        break;
    }

    setRelativeMouseMode(_cameraType == CameraType::FirstPerson);

    _module->area()->updateRoomVisibility();
}

Camera *Game::getActiveCamera() const {
    if (!_module) {
        return nullptr;
    }
    std::shared_ptr<Area> area(_module->area());
    if (!area) {
        return nullptr;
    }
    return area->getCamera(_cameraType);
}

std::shared_ptr<Object> Game::getObjectById(uint32_t id) const {
    switch (id) {
    case kObjectSelf:
        throw std::invalid_argument("Invalid id: " + std::to_string(id));
    case kObjectInvalid:
        return nullptr;
    default: {
        auto it = _objectById.find(id);
        return it != _objectById.end() ? it->second : nullptr;
    }
    }
}

int Game::scaleDamageForDifficulty(
    int damage,
    const Object &target) const {
    if (damage <= 0 || !_party.isMember(target)) {
        return damage;
    }

    const DifficultyOption &difficulty =
        _services.game.difficultyOptions.get(
            static_cast<int>(_options.game.clientDifficulty));
    int scaled = static_cast<int>(
        static_cast<float>(damage) * difficulty.damageMultiplier);
    // diffsettings MinHP1 (off at the game's fixed "normal" column): damage
    // never takes a party member below 1 hit point.
    return applyMinHP1(_services.game.difficultyOptions, scaled, target.currentHitPoints());
}

void Game::setAutoCombat(Creature &creature, bool on) {
    creature.setAutoCombat(on);
    if (on) {
        creature.requestCombatHandover();
    }
}

void Game::toggleLeaderAutoCombat() {
    if (_coop && _coop->isClient()) {
        // Co-op: the host switches the player's creature's AI.
        if (auto leader = _party.getLeader()) {
            _coop->submitRequest(net::CommandType::AutoCombat, 0, leader->autoCombat() ? 0 : 1);
            leader->setAutoCombat(!leader->autoCombat()); // the button's look until the host's sheet says so
        }
        return;
    }
    if (auto leader = _party.getLeader()) {
        setAutoCombat(*leader, !leader->autoCombat());
    }
}

bool Game::isRuntimeObjectLive(const Object &object) const {
    if (!object.isRuntimeLive()) {
        return false;
    }
    auto found = _objectById.find(object.id());
    return found != _objectById.end() && found->second.get() == &object;
}

bool Game::isRuntimeObjectAttachable(const Object &object) const {
    if (isRuntimeObjectLive(object)) {
        return true;
    }
    if (object._runtimeState != Object::RuntimeState::Constructing ||
        !_stagedRuntimeObjectGraph) {
        return false;
    }
    auto found = _stagedRuntimeObjectGraph->objectById.find(object.id());
    return found != _stagedRuntimeObjectGraph->objectById.end() &&
           found->second.get() == &object;
}

Game::~Game() {
    // An object owns its queued actions and an action can own its target, so
    // an object that converses with itself (or two that target each other) is
    // a shared_ptr cycle nothing else breaks. Drop the queues, without running
    // any callback, so the registry's objects can be freed with the game.
    for (auto &[id, object] : _objectById) {
        object->_actions.clear();
        object->_delayed.clear();
        object->_executingAction.reset();
    }
}

void Game::unregisterRuntimeObject(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    // Retained storage is no longer an action owner after this boundary.
    // Retire only transient dialogue admissions: arbitrary cancellation scripts
    // must not run while an outgoing or replaced graph is being retired.
    for (const auto &action : object->actions()) {
        if (auto conversation = dyn_cast<StartConversationAction>(action)) {
            conversation->cancel(action, *object);
            action->markCancelled();
        }
    }
    object->_runtimeState = Object::RuntimeState::Retired;
    if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
        // A PartyTable binding denotes a live runtime object, not storage
        // ownership. Pointer-guarded clearing cannot disturb a replacement
        // that reused the same runtime number.
        _party.clearRosterCreature(*creature);
    }
    auto registered = _objectById.find(object->id());
    if (registered != _objectById.end() &&
        registered->second.get() == object.get()) {
        _objectById.erase(registered);
    }

    // Include the structural Module alias, which deliberately has no reverse
    // serialized identity on the Module object itself.
    for (auto it = _objectBySavedId.begin(); it != _objectBySavedId.end();) {
        if (it->second.lock().get() == object.get()) {
            it = _objectBySavedId.erase(it);
        } else {
            ++it;
        }
    }
    _savedIdByObject.erase(object.get());
}

void Game::destroyRuntimeObjectGraph(const std::shared_ptr<Object> &object) {
    if (!object) {
        return;
    }
    auto graph = collectRuntimeObjectGraph({object});
    // Children cease to exist before their owner. The pointer guard makes this
    // idempotent and protects a newer object if an explicit ID was reused.
    for (auto it = graph.rbegin(); it != graph.rend(); ++it) {
        unregisterRuntimeObject(*it);
    }
}

std::vector<std::shared_ptr<Object>> Game::collectRuntimeObjectGraph(
    const std::vector<std::shared_ptr<Object>> &roots) const {
    std::vector<std::shared_ptr<Object>> pending(roots);
    std::set<const Object *> seen;
    std::vector<std::shared_ptr<Object>> graph;
    while (!pending.empty()) {
        auto current = std::move(pending.back());
        pending.pop_back();
        if (!current || !seen.insert(current.get()).second) {
            continue;
        }
        graph.push_back(current);
        for (auto &owned : current->ownedRuntimeObjects()) {
            pending.push_back(std::move(owned));
        }
    }
    return graph;
}

void Game::discardStagedRuntimeObjects(
    const std::vector<std::shared_ptr<Object>> &objects) {
    if (!_stagedRuntimeObjectGraph) {
        throw ValidationException("No staged runtime object graph is active");
    }
    auto &staged = *_stagedRuntimeObjectGraph;
    for (const auto &object : objects) {
        if (!object) {
            continue;
        }
        auto byId = staged.objectById.find(object->id());
        if (byId != staged.objectById.end() &&
            byId->second.get() == object.get()) {
            staged.objectById.erase(byId);
        }
        for (auto it = staged.objectBySavedId.begin();
             it != staged.objectBySavedId.end();) {
            if (it->second.lock().get() == object.get()) {
                it = staged.objectBySavedId.erase(it);
            } else {
                ++it;
            }
        }
        staged.savedIdByObject.erase(object.get());
        staged.publishedRuntimeObjectIds.erase(object->id());
        staged.reservedSavedObjectIdsToRelease.erase(object->id());
        object->_runtimeState = Object::RuntimeState::Retired;
    }
    staged.candidateObjects.erase(
        std::remove_if(
            staged.candidateObjects.begin(),
            staged.candidateObjects.end(),
            [&](const auto &candidate) {
                return std::find(objects.begin(), objects.end(), candidate) !=
                       objects.end();
            }),
        staged.candidateObjects.end());
}

void Game::beginRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &obsoleteObjects) {
    if (_stagedRuntimeObjectGraph) {
        throw ValidationException("Nested runtime object graph replacement");
    }
    _stagedRuntimeObjectGraph.emplace();
    _stagedRuntimeObjectGraph->initialNextObjectId = _nextObjectId;

    _stagedRuntimeObjectGraph->obsoleteGraph =
        collectRuntimeObjectGraph(obsoleteObjects);
    for (const auto &object : _stagedRuntimeObjectGraph->obsoleteGraph) {
        _stagedRuntimeObjectGraph->replaceableObjects.insert(object.get());
    }
}

void Game::commitRuntimeObjectGraphReplacement(
    const std::vector<std::shared_ptr<Object>> &) {
    if (!_stagedRuntimeObjectGraph) {
        throw ValidationException("No runtime object graph replacement is active");
    }

    auto staged = std::move(*_stagedRuntimeObjectGraph);
    _stagedRuntimeObjectGraph.reset();

    // The complete old graph was discovered before candidate construction and
    // before the no-throw ownership publication. Nothing below walks ownership
    // or allocates graph bookkeeping after that irreversible boundary.
    for (auto it = staged.obsoleteGraph.rbegin();
         it != staged.obsoleteGraph.rend(); ++it) {
        unregisterRuntimeObject(*it);
    }
    _objectById.merge(staged.objectById);
    _publishedRuntimeObjectIds.merge(staged.publishedRuntimeObjectIds);
    _objectBySavedId.merge(staged.objectBySavedId);
    _savedIdByObject.merge(staged.savedIdByObject);
    for (const auto &object : staged.candidateObjects) {
        auto found = _objectById.find(object->id());
        if (found != _objectById.end() &&
            found->second.get() == object.get()) {
            object->_runtimeState = Object::RuntimeState::Live;
        }
    }
    for (uint32_t id : staged.reservedSavedObjectIdsToRelease) {
        _reservedSavedObjectIds.erase(id);
    }

    // All collisions are rejected while staging. A non-empty source here
    // would mean the supposedly atomic publication silently lost a node.
    if (!staged.objectById.empty() ||
        !staged.publishedRuntimeObjectIds.empty() ||
        !staged.objectBySavedId.empty() ||
        !staged.savedIdByObject.empty()) {
        std::terminate();
    }
}

void Game::abortRuntimeObjectGraphReplacement() {
    if (!_stagedRuntimeObjectGraph) {
        return;
    }
    for (const auto &object : _stagedRuntimeObjectGraph->candidateObjects) {
        object->_runtimeState = Object::RuntimeState::Retired;
    }
    _nextObjectId = _stagedRuntimeObjectGraph->initialNextObjectId;
    _stagedRuntimeObjectGraph.reset();
}

std::shared_ptr<Object> Game::getObjectBySavedId(uint32_t id) const {
    auto found = _objectBySavedId.find(id);
    if (found == _objectBySavedId.end()) {
        return nullptr;
    }
    auto object = found->second.lock();
    return object && isRuntimeObjectLive(*object) ? object : nullptr;
}

uint32_t Game::savedObjectId(const resource::Gff &gff) const {
    uint32_t id = 0;
    if (!gff.readDword(id, "ObjectId")) {
        throw ValidationException("Saved runtime object is missing ObjectId");
    }
    return id;
}

void Game::registerObject(
    const std::shared_ptr<Object> &object,
    bool allowReserved) {
    uint32_t id = object->id();
    if (id == std::numeric_limits<uint32_t>::max()) {
        throw ValidationException("Invalid saved ObjectId");
    }
    if (!allowReserved && id < kFirstRuntimeObjectId) {
        throw ValidationException("Reserved saved ObjectId: " + std::to_string(id));
    }
    if (object->_runtimeState != Object::RuntimeState::Constructing ||
        object->_runtimeIncarnation != 0) {
        throw ValidationException("Runtime object storage cannot be republished");
    }
    if (_objectById.count(id) != 0) {
        throw ValidationException("Duplicate runtime ObjectId: " + std::to_string(id));
    }
    if (_publishedRuntimeObjectIds.count(id) != 0) {
        throw ValidationException(
            "Runtime ObjectId was already published in this session: " +
            std::to_string(id));
    }
    object->_runtimeIncarnation = _nextRuntimeIncarnation++;
    if (_stagedRuntimeObjectGraph) {
        if (!_stagedRuntimeObjectGraph->publishedRuntimeObjectIds.insert(id).second) {
            throw ValidationException(
                "Runtime ObjectId was already staged in this session: " +
                std::to_string(id));
        }
        if (!_stagedRuntimeObjectGraph->objectById.emplace(id, object).second) {
            throw ValidationException(
                "Duplicate staged runtime ObjectId: " + std::to_string(id));
        }
        _stagedRuntimeObjectGraph->candidateObjects.push_back(object);
        _stagedRuntimeObjectGraph->reservedSavedObjectIdsToRelease.insert(id);
    } else {
        _publishedRuntimeObjectIds.insert(id);
        _objectById.emplace(id, object);
        object->_runtimeState = Object::RuntimeState::Live;
        _reservedSavedObjectIds.erase(id);
    }
}

void Game::registerSavedObjectIdentity(
    uint32_t id,
    const std::shared_ptr<Object> &object,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        throw ValidationException(
            "Cannot register a non-authoritative saved object identity");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (id == std::numeric_limits<uint32_t>::max() || !object) {
        throw ValidationException("Invalid saved ObjectId mapping");
    }
    auto existing = getObjectBySavedId(id);
    if (existing && existing.get() != object.get() &&
        (!_stagedRuntimeObjectGraph ||
         !_stagedRuntimeObjectGraph->replaceableObjects.count(existing.get()))) {
        throw ValidationException(
            "Duplicate authoritative saved ObjectId mapping: " +
            std::to_string(id));
    }
    if (_stagedRuntimeObjectGraph) {
        auto staged = _stagedRuntimeObjectGraph->objectBySavedId.find(id);
        if (staged != _stagedRuntimeObjectGraph->objectBySavedId.end() &&
            staged->second.lock().get() != object.get()) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId mapping: " +
                std::to_string(id));
        }
    }
    const auto &reverseMap = _stagedRuntimeObjectGraph
                                 ? _stagedRuntimeObjectGraph->savedIdByObject
                                 : _savedIdByObject;
    auto reverse = reverseMap.find(object.get());
    if (reverse != reverseMap.end() && reverse->second != id) {
        throw ValidationException("Runtime object has multiple saved ObjectIds");
    }
    auto &savedMap = _stagedRuntimeObjectGraph
                         ? _stagedRuntimeObjectGraph->objectBySavedId
                         : _objectBySavedId;
    savedMap[id] = object;
    auto &canonicalId = _stagedRuntimeObjectGraph
                            ? _stagedRuntimeObjectGraph->savedIdByObject[object.get()]
                            : _savedIdByObject[object.get()];
    canonicalId = id;
    object->assignSerializedObjectIdentity({identityContext, id});
}

void Game::registerSavedModuleReferenceTarget(
    const std::shared_ptr<Module> &module,
    const SerializedIdentityContext &identityContext) {
    if (!identityContext.hasAuthoritativeObjectIds() || !module) {
        throw ValidationException("Invalid saved structural Module target");
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace !=
               identityContext.identityNamespace) {
        throw ValidationException("Saved object identity namespace is not active");
    }
    if (_reservedSavedObjectIds.count(kSavedRuntimeModuleObjectId) != 0) {
        throw ValidationException(
            "Saved ObjectId 0 collides with the structural Module target");
    }
    auto existing = getObjectBySavedId(kSavedRuntimeModuleObjectId);
    if (existing && existing.get() != module.get()) {
        throw ValidationException("Duplicate saved structural Module target");
    }
    auto &savedMap = _stagedRuntimeObjectGraph
                         ? _stagedRuntimeObjectGraph->objectBySavedId
                         : _objectBySavedId;
    auto [found, inserted] = savedMap.emplace(
        kSavedRuntimeModuleObjectId, module);
    if (!inserted) {
        auto existing = found->second.lock();
        if (!existing || existing.get() != module.get()) {
            throw ValidationException(
                "Duplicate saved structural Module target");
        }
    }
}

std::shared_ptr<Item> Game::newItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    return newObjectFromGff<Item>(gff, identityContext, *this, _services);
}

std::shared_ptr<Item> Game::newItemFromBlueprint(const std::string &resRef) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Item> item;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            item = newItem();
            item->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return item;
}

std::shared_ptr<Item> Game::newItemClone(const Item &source) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Item> item;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            item = newItem();
            item->clone(source);
        },
        []() noexcept {});
    return item;
}

std::shared_ptr<Item> Game::newOwnedItem(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    return newObjectFromGff<Item>(gff, identityContext, *this, _services);
}

std::shared_ptr<Area> Game::newSavedArea(
    uint32_t id,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Area> area;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            area = newArea(std::move(sceneName));
            registerSavedObjectIdentity(id, area, identityContext);
        },
        []() noexcept {});
    return area;
}

std::shared_ptr<Creature> Game::newCreature(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Creature>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Creature> Game::newCreatureFromBlueprint(
    const std::string &resRef,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Creature> creature;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            creature = newCreature(std::move(sceneName));
            creature->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return creature;
}

std::shared_ptr<Placeable> Game::newPlaceable(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Placeable>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Placeable> Game::newPlaceableFromGff(
    const resource::Gff &gff,
    const std::string &identity,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Placeable> placeable;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            placeable = newPlaceable(std::move(sceneName));
            placeable->deserialize(gff, SerializedIdentityContext::templateResource(identity));
        },
        []() noexcept {});
    return placeable;
}

std::shared_ptr<Placeable> Game::newPlaceableFromBlueprint(
    const std::string &resRef,
    std::string sceneName) {
    std::vector<std::shared_ptr<Object>> noObsolete;
    std::shared_ptr<Placeable> placeable;
    replaceRuntimeObjectGraph(
        noObsolete,
        [&]() {
            placeable = newPlaceable(std::move(sceneName));
            placeable->loadFromBlueprint(resRef);
        },
        []() noexcept {});
    return placeable;
}

std::shared_ptr<Door> Game::newDoor(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Door>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Waypoint> Game::newWaypoint(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Waypoint>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Trigger> Game::newTrigger(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Trigger>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Sound> Game::newSound(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Sound>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Encounter> Game::newEncounter(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Encounter>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

std::shared_ptr<Store> Game::newStore(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    std::string sceneName) {
    return newObjectFromGff<Store>(
        gff, identityContext, std::move(sceneName), *this, _services);
}

void Game::prepareSavedRuntimeNamespace(
    const resource::Gff &ifo,
    const SerializedIdentityContext &identityContext) {
    reserveSavedObjectIds(ifo, identityContext, SerializedGraphRoot::ModuleIfo);

    uint32_t nextObjectId = kFirstRuntimeObjectId;
    if (ifo.readDword(nextObjectId, "Mod_NextObjId0") &&
        nextObjectId != 0 &&
        nextObjectId < kFirstRuntimeObjectId) {
        throw ValidationException("Invalid Mod_NextObjId0");
    }
    _nextObjectId = std::max(nextObjectId, kFirstRuntimeObjectId);

    uint64_t nextEffectId = 0;
    if (ifo.readDword64(nextEffectId, "Mod_Effect_NxtId")) {
        if (!setNextEffectId(nextEffectId)) {
            throw ValidationException("Invalid Mod_Effect_NxtId");
        }
    }

    uint64_t pauseDay = 0;
    uint64_t pauseTime = 0;
    if (ifo.has("Mod_PauseDay") || ifo.has("Mod_PauseTime")) {
        pauseDay = ifo.getUint("Mod_PauseDay");
        pauseTime = ifo.getUint("Mod_PauseTime");
    } else {
        // Read-only compatibility for saves written by the short-lived Reone
        // field-name mistake. New snapshots always emit the retail fields.
        pauseDay = ifo.getUint("Mod_CalendarDay");
        pauseTime = ifo.getUint("Mod_TimeOfDay");
    }
    restoreWorldTime(ifo, pauseDay, pauseTime);
}

void Game::restoreWorldTime(
    const resource::Gff &moduleIfo,
    uint64_t pauseDay,
    uint64_t pauseTime) {
    // Mod_MinPerHour first: it defines the day length that Mod_PauseTime is
    // measured against.
    uint32_t minutesPerHour = moduleIfo.getUint("Mod_MinPerHour");
    if (minutesPerHour > std::numeric_limits<uint8_t>::max()) {
        throw ValidationException("Invalid Mod_MinPerHour");
    }
    _minutesPerHour = minutesPerHour == 0
                          ? 5
                          : static_cast<uint8_t>(minutesPerHour);

    // Compose the canonical clock from the retail day/time pair. An oversized
    // time of day carries into later days rather than being rejected, as
    // the original game does: saves written before the day length
    // became Mod_MinPerHour-derived hold a time of day on the old fixed
    // 24-hour scale, and must still load. Both fields are Dwords, so the
    // composition cannot overflow the 64-bit clock.
    _worldTimeMilliseconds =
        pauseDay * millisecondsPerWorldDay() + pauseTime;
    _worldTimeFraction = 0.0;
}

void Game::reserveSavedObjectIds(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext,
    SerializedGraphRoot graphRoot) {
    if (!identityContext.hasAuthoritativeObjectIds()) {
        return;
    }
    if (!_reservedSavedIdentityNamespace) {
        _reservedSavedIdentityNamespace = identityContext.identityNamespace;
    } else if (*_reservedSavedIdentityNamespace != identityContext.identityNamespace) {
        throw ValidationException("Cannot mix authoritative saved object namespaces");
    }
    for (auto &claim : collectSerializedObjectIdClaims(gff, identityContext, graphRoot)) {
        if (claim.id == std::numeric_limits<uint32_t>::max()) {
            throw ValidationException("Invalid saved ObjectId");
        }
        auto [found, inserted] = _reservedSavedObjectIdClaims.emplace(claim.id, claim.path);
        if (!inserted && found->second != claim.path) {
            throw ValidationException(
                "Duplicate authoritative saved ObjectId " +
                std::to_string(claim.id) + " at " + found->second +
                " and " + claim.path);
        }
        _reservedSavedObjectIds.insert(claim.id);
    }
}

void Game::resolveSavedObjectReferences() {
    for (const auto &[_, object] : _objectById) {
        const auto identityContext = object->_savedRuntimeIdentityContext;
        object->resolveSavedReferences(
            [this, identityContext](uint32_t id) {
                return resolveSerializedObjectReference(id, identityContext);
            });
    }
}

void Game::bindSavedRuntimeState() {
    if (!_module) {
        return;
    }
    resolveSavedObjectReferences();
    for (const auto &[_, object] : _objectById) {
        object->bindSavedRuntimeState();
    }
    _module->bindSavedEventQueue();
}

void Game::publishSavedRuntimeState() {
    if (!_module) {
        return;
    }
    for (const auto &[_, object] : _objectById) {
        object->publishSavedRuntimeState();
    }
    _module->publishSavedEventQueue();
}

void Game::setTimeOfDayForward(uint32_t timeOfDay) {
    uint64_t day = worldTimeDay();
    if (timeOfDay <= worldTimeOfDay()) {
        ++day;
    }
    _worldTimeMilliseconds = day * millisecondsPerWorldDay() + timeOfDay % millisecondsPerWorldDay();
    _worldTimeFraction = 0.0;
}

void Game::advanceWorldTime(float dt) {
    if (dt <= 0.0f) {
        return;
    }
    // One second of simulation is one thousand world milliseconds.
    // Mod_MinPerHour shortens the day, it does not accelerate the clock:
    // the clock accumulates elapsed time and only derives the day boundary
    // from the day length. Scaling the rate
    // here instead made every duration measured in world time short by
    // 60 / Mod_MinPerHour.
    double gameMilliseconds =
        _worldTimeFraction + static_cast<double>(dt) * 1000.0;
    uint64_t wholeMilliseconds =
        static_cast<uint64_t>(std::floor(gameMilliseconds));
    _worldTimeFraction =
        gameMilliseconds - static_cast<double>(wholeMilliseconds);

    _worldTimeMilliseconds += wholeMilliseconds;
}

std::optional<float> Game::remainingEffectDuration(
    const EffectInstance &effect) const {
    if (effect.durationType() != DurationType::Temporary) {
        return std::nullopt;
    }
    if (effect.expiryDay == 0 && effect.expiryTime == 0) {
        return std::max(0.0f, effect.duration);
    }

    // Save-facing expiry provenance, converted once here at the restoration
    // boundary. Live effects then count down in world seconds and never
    // reconstruct a calendar day again.
    uint64_t expiry =
        static_cast<uint64_t>(effect.expiryDay) * millisecondsPerWorldDay() +
        effect.expiryTime;
    if (expiry <= _worldTimeMilliseconds) {
        return 0.0f;
    }
    double remainingSeconds =
        static_cast<double>(expiry - _worldTimeMilliseconds) / 1000.0;
    return static_cast<float>(remainingSeconds);
}

void Game::renderGUI() {
    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    bool gameplayHUD = _screen == Screen::InGame || _screen == Screen::SwoopRace || _screen == Screen::Turret;
    if (_screen == Screen::InGame) {
        if (_cameraType == CameraType::ThirdPerson) {
            renderHUD();
        }
    } else if (gameplayHUD) {
        if (auto gui = getScreenGUI()) {
            gui->render();
        }
    }
    // A conversation's node fades cover the scene under the subtitles and
    // under the screen fade.
    if (_screen == Screen::Conversation) {
        renderFade(_dialogFade);
    }
    renderFade(_globalFade);
    if (_screen == Screen::InGame && _cameraType == CameraType::ThirdPerson && _hud) {
        _hud->renderModal();
    }
    if (!gameplayHUD) {
        auto gui = getScreenGUI();
        if (gui) {
            gui->render();
        }
    }
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->render();
    }
    _pointer->render(_options.graphics, _relativeMouseMode);
    renderDeveloperOverlay();
}

void Game::recordSoundCue(const std::string &category, const std::string &resRef) {
    static constexpr size_t kMaxSoundCues = 64;
    if (_soundCues.size() >= kMaxSoundCues) {
        _soundCues.pop_front();
    }
    _soundCues.push_back(SoundCue {_tickCount, category, resRef});
    debug("Sound cue " + category + ": " + resRef, LogChannel::Audio);
}

std::shared_ptr<audio::AudioSource> Game::playSoundCue(
    const std::string &category,
    const std::string &resRef,
    std::optional<glm::vec3> position,
    audio::AudioType type,
    bool loop,
    float gain) {
    if (resRef.empty()) {
        return nullptr;
    }
    std::string lower(boost::to_lower_copy(resRef));
    recordSoundCue(category, lower);
    auto clip = _services.resource.audioClips.get(lower);
    if (!clip) {
        return nullptr;
    }
    return _services.audio.mixer.play(std::move(clip), type, gain, loop, std::move(position));
}

void Game::setVideoEffectRow(int row) {
    VideoEffect effect;
    auto table = row >= 0 ? _services.resource.twoDas.get("videoeffects") : nullptr;
    if (table && row < table->getRowCount()) {
        effect.row = row;
        effect.saturation = table->getInt(row, "enablesaturation", 0) != 0;
        // K2 splits the PC and console values; K1 has one set.
        auto number = [&](const char *k1, const char *k2, float def) {
            if (auto value = table->getFloatOpt(row, k1)) {
                return *value;
            }
            return table->getFloat(row, k2, def);
        };
        effect.modulation = glm::vec3(
            number("modulationred", "modulationred_pc", 1.0f),
            number("modulationgreen", "modulationgreen_pc", 1.0f),
            number("modulationblue", "modulationblue_pc", 1.0f));
        effect.saturationAmount = number("saturation", "saturation_pc", 0.0f);
        effect.scanNoise = table->getInt(row, "enablescannoise", 0) != 0;
    }
    _videoEffect = effect;
}

void Game::enableVideoEffect(int row) {
    _scriptVideoEffect = true;
    setVideoEffectRow(row);
}

void Game::disableVideoEffect() {
    _scriptVideoEffect = false;
    setVideoEffectRow(-1);
}

void Game::applyDialogVideoEffect(int row) {
    if (row >= 0) {
        setVideoEffectRow(row);
    } else if (!_scriptVideoEffect) {
        setVideoEffectRow(-1);
    }
}

void Game::renderFade(const GlobalFade &fade) {
    float alpha = fade.renderOpacity(tickAlpha());
    if (alpha <= 0.0f) {
        return;
    }
    auto &graphics = _services.graphics;
    auto &context = graphics.context;
    GlobalUniforms previousGlobals;
    LocalUniforms previousLocals;
    graphics.uniforms.setGlobals([&](auto &globals) {
        previousGlobals = globals;
        globals.reset();
        globals.projection = glm::ortho(0.0f, static_cast<float>(_options.graphics.width),
                                        static_cast<float>(_options.graphics.height), 0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    graphics.uniforms.setLocals([&](auto &locals) { previousLocals = locals; });
    context.withDepthTestMode(DepthTestMode::None, [&]() {
        context.withDepthMask(false, [&]() {
            context.withBlendMode(BlendMode::Normal, [&]() {
                context.withFaceCullMode(FaceCullMode::None, [&]() {
                    context.withPolygonMode(PolygonMode::Fill, [&]() {
                        // Inherit the GUI viewport, including window/drawable
                        // scaling. Logical GUI dimensions need not be pixels.
                        // Compatibility approximation: color, then blackfill.
                        for (auto color : {fade.color(), glm::vec3(0.0f)}) {
                            graphics.uniforms.setLocals([&](auto &locals) {
                                locals.reset();
                                locals.model = glm::scale(glm::mat4(1.0f),
                                                          glm::vec3(_options.graphics.width, _options.graphics.height, 1.0f));
                                locals.color = glm::vec4(color, alpha);
                            });
                            context.useProgram(graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
                            graphics.meshRegistry.get(MeshName::quad).draw(graphics.statistic);
                        }
                    });
                });
            });
        });
    });
    graphics.uniforms.setLocals([&](auto &locals) { locals = previousLocals; });
    graphics.uniforms.setGlobals([&](auto &globals) { globals = previousGlobals; });
}

void Game::settleFadeArrival() {
    if (!_fadeArrival) {
        return;
    }
    if (_fadeArrivalModule.lock() == _module && _module) {
        _globalFade.settleArrival(_fadeArrival);
    }
    _fadeArrival.reset();
    _fadeArrivalModule.reset();
}

void Game::renderDeveloperOverlay() {
    if (!_options.game.developer || !_developerOverlay.visible || !_module || _screen != Screen::InGame) {
        return;
    }
    if (!_developerFont) {
        _developerFont = _services.resource.fonts.get("fnt_console");
    }
    if (!_developerFont) {
        return;
    }

    auto camera = getActiveCamera();
    bool hasCamera = camera != nullptr;
    glm::mat4 projection(1.0f);
    glm::mat4 view(1.0f);
    if (camera) {
        projection = camera->cameraSceneNode()->camera()->projection();
        view = camera->cameraSceneNode()->camera()->view();
    }

    _services.graphics.uniforms.setGlobals([this](auto &globals) {
        globals.reset();
        globals.projection = glm::ortho(
            0.0f,
            static_cast<float>(_options.graphics.width),
            static_cast<float>(_options.graphics.height),
            0.0f, 0.0f, 100.0f);
        globals.projectionInv = glm::inverse(globals.projection);
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this]() {
        renderDeveloperBanner();
    });
    _services.graphics.context.withBlendMode(BlendMode::Normal, [this, hasCamera, &projection, &view]() {
        if (_developerOverlay.triggers && hasCamera) {
            renderDeveloperTriggerOverlay(projection, view);
        }
        if (_developerOverlay.actorLabels && hasCamera) {
            renderDeveloperActorLabels(projection, view);
        }
        if (_developerOverlay.watchedValues) {
            renderDeveloperWatchedValues();
        }
    });
}

void Game::renderDeveloperBanner() {
    std::vector<std::string> lines;
    lines.push_back("DEV OBSERVABILITY");
    lines.push_back(str(boost::format("%s overlay | %s triggers") %
                        kDeveloperOverlayToggleHelp %
                        kDeveloperTriggerToggleHelp));
    lines.push_back(str(boost::format("%s labels (%s) | %s verbose") %
                        kDeveloperActorToggleHelp %
                        (_developerOverlay.longActorLabels ? "long" : "short") %
                        kDeveloperActorLongToggleHelp));
    lines.push_back(str(boost::format("%s watch | ` console | F5 profiler") %
                        kDeveloperWatchToggleHelp));
    lines.push_back("V camera | +/- speed");

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    renderDeveloperPanel(
        lines,
        glm::vec2(0.5f * (static_cast<float>(_options.graphics.width) - maxWidth - 14.0f), 12.0f),
        glm::vec3(0.58f, 1.0f, 0.58f));
}

void Game::renderDeveloperTriggerOverlay(const glm::mat4 &projection, const glm::mat4 &view) {
    static glm::vec4 viewport(0.0f, 0.0f, 1.0f, 1.0f);
    auto area = _module ? _module->area() : nullptr;
    if (!area) {
        return;
    }

    const auto &opts = _options.graphics;
    for (const auto &object : area->getObjectsByType(ObjectType::Trigger)) {
        auto trigger = dyn_cast<Trigger>(object);
        if (!trigger) {
            continue;
        }
        const auto &geometry = trigger->geometry();
        if (geometry.empty()) {
            continue;
        }

        glm::vec3 centroid(0.0f);
        for (const auto &localPoint : geometry) {
            centroid += trigger->position() + localPoint;
        }

        // Trigger geometry now renders through the main scene pipeline; the overlay only adds labels.
        auto state = trigger->debugState();
        glm::vec4 color = trigger->debugColor();
        centroid /= static_cast<float>(geometry.size());
        glm::vec3 labelScreen = glm::project(centroid, view, projection, viewport);
        if (labelScreen.z >= 0.0f && labelScreen.z < 1.0f) {
            std::string label = str(boost::format("#%u %s") %
                                    trigger->id() %
                                    trigger->tag());
            if (!trigger->blueprintResRef().empty()) {
                label += " " + trigger->blueprintResRef();
            }
            label += str(boost::format(" [%s]") % triggerDebugStateName(state));
            glm::vec3 position(opts.width * labelScreen.x, opts.height * (1.0f - labelScreen.y), 0.0f);
            renderDeveloperText(label, position, glm::vec3(color), TextGravity::CenterBottom);
        }
    }
}

void Game::renderDeveloperActorLabels(const glm::mat4 &projection, const glm::mat4 &view) {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    if (!area || !leader) {
        return;
    }

    const auto &opts = _options.graphics;
    int rendered = 0;
    for (const auto &object : area->objects()) {
        bool supported = object->type() == ObjectType::Creature ||
                         object->type() == ObjectType::Door ||
                         object->type() == ObjectType::Placeable;
        bool inspected = object == area->hilightedObject() || object == area->selectedObject();
        if (!supported && !inspected) {
            continue;
        }

        float distance = object->getDistanceTo(*leader);
        if (!inspected && distance > kDeveloperActorLabelDistance) {
            continue;
        }

        glm::vec3 screen = area->getSelectableScreenCoords(object, projection, view);
        if (screen.z >= 1.0f) {
            continue;
        }

        int faction = getDebugFaction(object);
        bool hostile = false;
        auto creature = dyn_cast<Creature>(object);
        if (creature) {
            hostile = !creature->isDead() && _services.game.reputes.getIsEnemy(*leader, *creature);
        }

        glm::vec3 color = inspected ? glm::vec3(1.0f, 1.0f, 1.0f) : (hostile ? glm::vec3(1.0f, 0.42f, 0.36f) : glm::vec3(0.68f, 0.92f, 1.0f));
        std::string label;
        if (_developerOverlay.longActorLabels) {
            label = str(boost::format("#%u %s %s f=%d H=%d sel=%d cmd=%d vis=%d plot=%d") %
                        object->id() %
                        object->tag() %
                        object->blueprintResRef() %
                        faction %
                        static_cast<int>(hostile) %
                        static_cast<int>(object->isSelectable()) %
                        static_cast<int>(object->isCommandable()) %
                        static_cast<int>(object->visible()) %
                        static_cast<int>(object->plotFlag()));
        } else {
            label = str(boost::format("#%u %s") %
                        object->id() %
                        object->tag());
            if (!object->blueprintResRef().empty()) {
                label += " " + object->blueprintResRef();
            }
            if (hostile) {
                label += " [enemy]";
            }
            if (inspected) {
                label += str(boost::format(" [%s") % objectTypeName(object->type()));
                if (faction >= 0) {
                    label += str(boost::format(" f=%d") % faction);
                }
                label += "]";
            }
        }

        glm::vec3 position(opts.width * screen.x, opts.height * (1.0f - screen.y) - 18.0f - (rendered % 2) * 10.0f, 0.0f);
        renderDeveloperText(label, position, color, TextGravity::CenterBottom);
        if (++rendered >= 16) {
            break;
        }
    }
}

void Game::renderDeveloperWatchedValues() {
    auto area = _module ? _module->area() : nullptr;
    auto leader = _party.getLeader();
    auto selected = area ? area->selectedObject() : nullptr;
    auto hover = area ? area->hilightedObject() : nullptr;
    std::string room = leader && leader->room() ? leader->room()->name() : "-";
    glm::vec3 position = leader ? leader->position() : glm::vec3(0.0f);

    std::vector<std::string> lines;
    lines.push_back(str(boost::format("Watch (%s)") % kDeveloperWatchToggleHelp));
    lines.push_back(str(boost::format("screen=%s module=%s area=%s camera=%s") %
                        screenName(_screen) %
                        (_module ? _module->name() : "-") %
                        (area ? area->localizedName() : "-") %
                        cameraTypeName(_cameraType)));
    lines.push_back(str(boost::format("speed=%.1fx paused=%d relativeMouse=%d room=%s") %
                        _gameSpeed %
                        static_cast<int>(_paused) %
                        static_cast<int>(_relativeMouseMode) %
                        room));
    lines.push_back(str(boost::format("leader=#%u %s hp=%d/%d pos=%.2f,%.2f,%.2f") %
                        (leader ? leader->id() : 0) %
                        (leader ? leader->tag() : "-") %
                        (leader ? leader->currentHitPoints() : -1) %
                        (leader ? leader->maxHitPoints() : -1) %
                        position.x %
                        position.y %
                        position.z));
    lines.push_back(str(boost::format("selected=#%u %s/%s type=%s hp=%d/%d") %
                        (selected ? selected->id() : 0) %
                        (selected ? selected->tag() : "-") %
                        (selected ? selected->blueprintResRef() : "-") %
                        (selected ? objectTypeName(selected->type()) : "-") %
                        (selected ? selected->currentHitPoints() : -1) %
                        (selected ? selected->maxHitPoints() : -1)));
    lines.push_back(str(boost::format("hover=#%u %s/%s type=%s hp=%d/%d") %
                        (hover ? hover->id() : 0) %
                        (hover ? hover->tag() : "-") %
                        (hover ? hover->blueprintResRef() : "-") %
                        (hover ? objectTypeName(hover->type()) : "-") %
                        (hover ? hover->currentHitPoints() : -1) %
                        (hover ? hover->maxHitPoints() : -1)));

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float panelWidth = maxWidth + 14.0f;
    float panelHeight = (_developerFont->height() + 2.0f) * static_cast<float>(lines.size()) + 10.0f;
    renderDeveloperPanel(
        lines,
        glm::vec2(static_cast<float>(_options.graphics.width) - panelWidth - 4.0f, 16.0f + panelHeight),
        glm::vec3(0.92f));
}

void Game::renderDeveloperText(const std::string &text, const glm::vec3 &position, const glm::vec3 &color, TextGravity gravity) {
    if (!_developerFont) {
        return;
    }
    _developerFont->render(text, position + glm::vec3(1.0f, 1.0f, 0.0f), glm::vec3(0.0f), gravity);
    _developerFont->render(text, position, color, gravity);
}

void Game::renderDeveloperPanel(const std::vector<std::string> &lines, glm::vec2 position, glm::vec3 color) {
    if (!_developerFont || lines.empty()) {
        return;
    }

    float maxWidth = 0.0f;
    for (const auto &line : lines) {
        maxWidth = glm::max(maxWidth, _developerFont->measure(line));
    }
    float lineHeight = _developerFont->height() + 2.0f;
    glm::vec2 size(maxWidth + 14.0f, lineHeight * lines.size() + 10.0f);
    position.x = glm::clamp(position.x, 4.0f, static_cast<float>(_options.graphics.width) - size.x - 4.0f);
    position.y = glm::clamp(position.y, 4.0f, static_cast<float>(_options.graphics.height) - size.y - 4.0f);

    renderDeveloperRect(position, size, glm::vec4(0.0f, 0.0f, 0.0f, 0.58f));
    glm::vec3 textPosition(position.x + 7.0f, position.y + 5.0f, 0.0f);
    for (const auto &line : lines) {
        renderDeveloperText(line, textPosition, color, TextGravity::RightBottom);
        textPosition.y += lineHeight;
    }
}

void Game::renderDeveloperRect(glm::vec2 position, glm::vec2 size, glm::vec4 color) {
    glm::mat4 transform(1.0f);
    transform = glm::translate(transform, glm::vec3(position.x, position.y, 0.0f));
    transform = glm::scale(transform, glm::vec3(size.x, size.y, 1.0f));

    _services.graphics.uniforms.setLocals([transform, color](auto &locals) {
        locals.reset();
        locals.model = transform;
        locals.color = color;
    });
    _services.graphics.context.useProgram(_services.graphics.shaderRegistry.get(ShaderProgramId::mvpColor));
    _services.graphics.meshRegistry.get(MeshName::quad).draw(_services.graphics.statistic);
}

void Game::updateAmbientRoomScale(float dt) {
    // are Rooms/AmbientScale of the leader's room scales the ambient bed,
    // eased towards the new room's value over 1 s.
    float target = 1.0f;
    if (_module && _module->area()) {
        if (auto leader = _party.getLeader(); leader && leader->room()) {
            target = _module->area()->roomAmbientScale(leader->room()->name());
        }
    }
    float step = std::max(0.0f, dt);
    if (_ambientRoomScale < target) {
        _ambientRoomScale = std::min(target, _ambientRoomScale + step);
    } else {
        _ambientRoomScale = std::max(target, _ambientRoomScale - step);
    }
}

void Game::updateMusic(float dt) {
    updateAmbientRoomScale(dt);
    for (auto it = _fadingAudio.begin(); it != _fadingAudio.end();) {
        it->left -= dt;
        if (it->left <= 0.0f || !it->source->isPlaying()) {
            it->source->stop();
            it = _fadingAudio.erase(it);
            continue;
        }
        it->source->setGainFactor(it->from * (it->left / it->total));
        ++it;
    }
    if (_options.audio.muted) {
        // Unattended headless runs: nothing to hear, so start nothing. The
        // requested tracks stay set and play once audio is unmuted.
        return;
    }
    // Music and ambience decode off the main thread (a whole track is
    // hundreds of milliseconds of MP3 decoding) and start once ready.
    bool pending = false;
    if (!_ambientResRef.empty() && _ambientVolume > 0 && !(_ambient && _ambient->isPlaying())) {
        auto clip = _services.resource.audioClips.getInBackground(_ambientResRef, pending);
        if (!pending) {
            _ambient = _services.audio.mixer.play(
                std::move(clip), AudioType::Sound, _ambientVolume / 100.0f, /*loop=*/true);
        }
    }
    if (_ambient) {
        _ambient->setGainFactor(_ambientRoomScale);
    }
    if (_musicResRef.empty()) {
        return;
    }
    if (_music && _music->isPlaying()) {
        return;
    }
    auto clip = (_musicClips ? *_musicClips : _services.resource.audioClips).getInBackground(_musicResRef, pending);
    if (pending) {
        return;
    }
    _music = _services.audio.mixer.play(std::move(clip), AudioType::Music);
    if (_music && _musicVolumeScale != 1.0f) {
        _music->setGainFactor(_musicVolumeScale);
    }
}

void Game::loadNextModule() {
    // The party leaves the module it is in: its OnClientLeave runs while it
    // is still loaded. The script may redirect the transition.
    if (_module) {
        if (auto leader = _party.getLeader()) {
            _module->runOnClientLeaveScript(*leader);
        }
        if (_nextModule.empty()) {
            return;
        }
    }
    bool loaded = loadModule(_nextModule, _nextEntry);

    _nextModule.clear();
    _nextEntry.clear();
    if (!loaded) {
        return;
    }

    // A module whose area declares a minigame starts it once loaded (see
    // loadPreparedModule); its scripts end it with StartNewModule.
}

void Game::stopMovement() {
    // Reached with no module while one is being swapped in: loadGame resets the
    // game before the destination module is up, and the menus that call this
    // outlive that reset. There is no player to halt and no in-game camera for
    // getActiveCamera to find, so there is nothing to stop.
    if (!_module) {
        return;
    }

    auto camera = getActiveCamera();
    if (camera) {
        camera->stopMovement();
    }
    _module->player().stopMovement();
}

void Game::scheduleModuleTransition(const std::string &moduleName, const std::string &entry) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
}

void Game::scheduleModuleTransitionWithMovies(const std::string &moduleName, const std::string &entry, std::vector<std::string> movies) {
    _nextModule = moduleName;
    _nextEntry = entry;
    _moduleTransitionMovies = std::queue<std::string>();
    for (auto &movie : movies) {
        _moduleTransitionMovies.push(std::move(movie));
    }

    if (!_movie) {
        playNextModuleTransitionMovie();
    }
}

bool Game::startVideo(const std::string &name) {
    _services.audio.mixer.stopAll();
    _music.reset();
    _ambient.reset();
    _fadingAudio.clear();

    _movie = _services.resource.movies.get(name);
    _globalFade.setMovieOverride(static_cast<bool>(_movie));
    if (_coop && _movie) {
        _coop->onMovieStarted(name);
    }
    _timingDiscontinuity = true;
    if (!_movie) {
        return false;
    }

    return true;
}

bool Game::playNextModuleTransitionMovie() {
    while (!_moduleTransitionMovies.empty()) {
        auto name = std::move(_moduleTransitionMovies.front());
        _moduleTransitionMovies.pop();

        if (startVideo(name)) {
            return true;
        }
    }
    return false;
}

void Game::updateMovie(float dt) {
    _movie->update(dt);

    if (_movie->isFinished()) {
        _movie.reset();
        _globalFade.setMovieOverride(false);
        _timingDiscontinuity = true;
        if (!playNextModuleTransitionMovie()) {
            playNextQueuedMovie();
        }
    }
}

void Game::updateCameraType() {
    switch (_screen) {
    case Screen::Conversation: {
        int cameraId;
        CameraType cameraType = getConversationCamera(cameraId);
        if (cameraType == CameraType::Static) {
            _module->area()->setStaticCamera(cameraId);
        }
        _cameraType = cameraType;
        break;
    }
    case Screen::InGame:
        if (_cameraType != CameraType::FirstPerson && _cameraType != CameraType::ThirdPerson) {
            _cameraType = CameraType::ThirdPerson;
        }
        break;
    default:
        break;
    }
}

void Game::updateCamera(float dt) {
    updateCameraType();
    Camera *camera = getActiveCamera();
    if (camera) {
        camera->update(dt);

        glm::vec3 listenerPosition;
        if (_cameraType == CameraType::ThirdPerson) {
            std::shared_ptr<Creature> partyLeader(_party.getLeader());
            if (partyLeader) {
                listenerPosition = partyLeader->position() + glm::vec3 {0.0f, 0.0f, 1.7f}; // TODO: height based on appearance
            }
        } else {
            listenerPosition = camera->sceneNode()->origin();
        }
        _services.audio.context.setListenerPosition(std::move(listenerPosition));
    }
}

void Game::updateVisualEffects(float dt) {
    // Presentation: spawn, follow and retire the models of visual effects the
    // simulation started or ended since the last frame.
    if (!_module) {
        return;
    }
    VisualEffectPresenter::Context ctx;
    ctx.graph = &_services.scene.graphs.get(kSceneMain);
    ctx.mixer = &_services.audio.mixer;
    ctx.modelOf = [this](uint32_t id) -> scene::ModelSceneNode * {
        auto object = getObjectById(id);
        if (!object) {
            return nullptr;
        }
        auto node = object->sceneNode();
        return node && node->type() == scene::SceneNodeType::Model ? static_cast<scene::ModelSceneNode *>(node.get()) : nullptr;
    };
    if (Camera *camera = getActiveCamera()) {
        if (camera->sceneNode()) {
            ctx.cameraPosition = camera->sceneNode()->origin();
        }
    }
    _visualEffectPresenter.update(dt, ctx);
}

void Game::updateSceneGraph(float dt) {
    auto camera = getActiveCamera();
    if (!camera) {
        return;
    }
    auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
    sceneGraph.setActiveCamera(camera->cameraSceneNode().get());
    sceneGraph.setUpdateRoots(!_paused);
    sceneGraph.setRenderAABB(isShowAABBEnabled());
    sceneGraph.setRenderWalkmeshes(isShowWalkmeshEnabled());
    bool renderDeveloperTriggers = _options.game.developer &&
                                   _screen == Screen::InGame &&
                                   _developerOverlay.visible &&
                                   _developerOverlay.triggers;
    sceneGraph.setRenderTriggers(isShowTriggersEnabled() || renderDeveloperTriggers);
    // Model animation advanced in tick(); this is the per-frame half.
    sceneGraph.updatePresentation(dt);
    _dofFrameTime += dt;
}

bool Game::getGlobalBoolean(const std::string &name) const {
    auto it = _globalBooleans.find(name);
    return it != _globalBooleans.end() ? it->second : false;
}

int Game::getGlobalNumber(const std::string &name) const {
    auto it = _globalNumbers.find(name);
    return it != _globalNumbers.end() ? it->second : 0;
}

std::string Game::getGlobalString(const std::string &name) const {
    auto it = _globalStrings.find(name);
    return it != _globalStrings.end() ? it->second : "";
}

std::shared_ptr<Location> Game::getGlobalLocation(const std::string &name) const {
    auto it = _globalLocations.find(name);
    return it != _globalLocations.end() ? it->second : nullptr;
}

void Game::setCustomToken(int token, std::string value) {
    _customTokens[token] = std::move(value);
}

StringTokenSubject Game::stringTokenSubject() const {
    StringTokenSubject subject;
    subject.hour = static_cast<int>(worldTimeOfDay() / (60000u * (minutesPerHour() == 0 ? 5u : minutesPerHour())) % 24);
    uint32_t day = worldTimeDay();
    subject.year = static_cast<int>(day / (kClockMonthsPerYear * kClockDaysPerMonth));
    subject.month = static_cast<int>((day / kClockDaysPerMonth) % kClockMonthsPerYear) + 1;
    if (_module) {
        int dawn = _module->info().dawnHour;
        int dusk = _module->info().duskHour;
        int hour = *subject.hour;
        subject.quarterDay = (hour < dawn || dusk < hour) ? 0 : ((hour - dawn) * 3) / (dusk - dawn + 1) + 1;
    }
    subject.keyName = [this](int row, int direction) {
        auto keymapTable = _services.resource.twoDas.get("keymap");
        if (!keymapTable || row < 0) {
            return std::string();
        }
        std::string label = "action" + std::to_string(row) + (direction == 1 ? "a" : (direction == 2 ? "b" : ""));
        int index = keymapTable->indexByLabel(label);
        if (index == -1) {
            return std::string();
        }
        if (auto action = keyActionByName(keymapTable->getString(index, "name"))) {
            return keyName(_keymap.keyFor(*action));
        }
        return keymapTable->getString(index, "character");
    };
    auto player = _party.player();
    if (!player) {
        return subject;
    }
    subject.present = true;
    subject.fullName = player->name();
    subject.firstName = player->firstNameText();
    subject.lastName = player->lastNameText();
    subject.gender = static_cast<int>(player->gender());
    subject.goodEvil = player->goodEvil();
    subject.level = player->attributes().getAggregateLevel();
    auto &strings = _services.resource.strings;
    auto strRefText = [&](const std::shared_ptr<TwoDA> &table, int row, const char *column) {
        if (!table || row < 0 || row >= table->getRowCount()) {
            return std::string();
        }
        int strRef = table->getInt(row, column, -1);
        return strRef >= 0 ? strings.getText(strRef) : std::string();
    };
    auto races = _services.resource.twoDas.get("racialtypes");
    int race = static_cast<int>(player->racialType());
    subject.race = strRefText(races, race, "convername");
    subject.raceLower = strRefText(races, race, "convernamelower");
    auto classes = _services.resource.twoDas.get("classes");
    int clazz = static_cast<int>(player->attributes().getEffectiveClass());
    subject.className = strRefText(classes, clazz, "name");
    subject.classLower = strRefText(classes, clazz, "lower");
    return subject;
}

std::string Game::resolveTokens(const std::string &str, const std::map<int, std::string> &customTokens) const {
    if (str.find('<') == std::string::npos) {
        return str;
    }
    if (!_stringTokensLoaded) {
        _stringTokensLoaded = true;
        if (auto table = _services.resource.twoDas.get("stringtokens")) {
            _stringTokens.load(*table);
        }
    }
    auto &strings = _services.resource.strings;
    return _stringTokens.resolve(str, customTokens, stringTokenSubject(), [&strings](int strRef) { return strings.getText(strRef); });
}

std::string Game::substituteCustomTokens(std::string str) const {
    return resolveTokens(str, _customTokens);
}

std::string Game::substituteCustomToken(std::string str, int token, std::string value) const {
    auto customTokens = _customTokens;
    customTokens[token] = std::move(value);
    return resolveTokens(str, customTokens);
}

void Game::setGlobalBoolean(const std::string &name, bool value) {
    // K2 systems merge: the prestige classes are offered by the PC's
    // alignment at the Revan reveal, so it is recorded the first time K1's
    // reveal flag is set (design note k2_merge, "Prestige classes").
    if (value && _services.resource.k2Merge && name == prestige::kRevealGlobal &&
        !getGlobalBoolean(name) && getGlobalNumber(prestige::kRevealAlignmentGlobal) <= 0) {
        if (auto pc = _party.actualPlayer()) {
            setGlobalNumber(prestige::kRevealAlignmentGlobal, pc->goodEvil() + 1);
        }
    }
    _globalBooleans[name] = value;
}

std::shared_ptr<Creature> Game::levelUpInProgress() const {
    if (_screen != Screen::CharacterGeneration || !_charGen || _charGen->type() != CharacterGeneration::Type::LevelUp) {
        return nullptr;
    }
    return _charGen->levelUpMember();
}

prestige::Facts Game::prestigeFactsFor(const Creature &creature, bool asPlayerCharacter) const {
    prestige::Facts facts;
    facts.k2Merged = _services.resource.k2Merge != nullptr;
    auto pc = _party.actualPlayer();
    facts.playerCharacter = asPlayerCharacter || pc.get() == &creature;
    facts.revealed = getGlobalBoolean(prestige::kRevealGlobal);
    int recorded = getGlobalNumber(prestige::kRevealAlignmentGlobal);
    if (recorded > 0) {
        facts.revealGoodEvil = recorded - 1;
    }
    facts.goodEvil = creature.goodEvil();
    for (const auto &[clazz, level] : creature.attributes().classLevels()) {
        facts.classes.push_back(clazz->type());
    }
    return facts;
}

std::vector<ClassType> Game::prestigeOffersFor(const Creature &creature, bool asPlayerCharacter) const {
    if (!_services.resource.k2Merge) {
        return {};
    }
    auto offered = prestige::offeredClasses(prestigeFactsFor(creature, asPlayerCharacter));
    // Only classes the merged classes.2da defines.
    auto classes = _services.resource.twoDas.get("classes");
    offered.erase(std::remove_if(offered.begin(), offered.end(), [&classes](ClassType type) {
                      return !classes || static_cast<int>(type) >= classes->getRowCount();
                  }),
                  offered.end());
    return offered;
}

void Game::setGlobalNumber(const std::string &name, int value) {
    // Retail SetGlobalNumber stores the low byte in its signed-char table.
    // Express that conversion portably instead of relying on plain-char
    // signedness or an implementation-defined narrowing conversion.
    uint8_t raw = static_cast<uint8_t>(value);
    _globalNumbers[name] = raw <= 0x7f ? static_cast<int>(raw)
                                       : static_cast<int>(raw) - 0x100;
}

std::vector<SavedGame> Game::savedGames() const {
    return discoverSavedGames(_path);
}

void Game::setGlobalString(const std::string &name, const std::string &value) {
    _globalStrings[name] = value;
}

void Game::setGlobalLocation(const std::string &name, const std::shared_ptr<Location> &location) {
    _globalLocations[name] = location;
}

void Game::setPaused(bool paused) {
    _paused = paused;
}

void Game::blockClicksFor(float seconds) {
    if (!(seconds > 0.0f)) {
        return;
    }
    uint64_t until = _worldTimeMilliseconds + static_cast<uint64_t>(std::llround(seconds * 1000.0f));
    _clicksBlockedUntilMilliseconds = std::max(_clicksBlockedUntilMilliseconds, until);
}

void Game::setRelativeMouseMode(bool relative) {
    _relativeMouseMode = relative;
}

void Game::withLoadingScreen(const std::string &imageResRef, const std::function<void()> &block) {
    if (!_loadScreen) {
        _loadScreen = tryLoadGUI<LoadingScreen>();
    }
    if (_loadScreen) {
        _loadScreen->setImage(imageResRef);
        _loadScreen->setProgress(0);
        // A gameplay hint from loadscreenhints.2da, picked on the cosmetic
        // stream (presentation only).
        std::string hint;
        if (auto hints = _services.resource.twoDas.get("loadscreenhints"); hints && hints->getRowCount() > 0) {
            int strref = loadScreenHint(*hints, cosmeticRandomInt(0, hints->getRowCount() - 1));
            if (strref >= 0) {
                hint = _services.resource.strings.getText(strref);
            }
        }
        _loadScreen->setHint(hint);
    }
    changeScreen(Screen::Loading);
    render();
    block();
}

/**
 * Terminal destination for a load that failed after the commit boundary.
 *
 * openMainMenu retires whatever was half-built and drops the candidate mounts
 * with it, so nothing of either session survives. It gives up early when the
 * menu GUI cannot be loaded, which would leave the screen wherever the
 * abandoned session left it; record the intended destination regardless, so a
 * missing menu resource is its own visible failure rather than an engine that
 * renders nothing while still running.
 */
void Game::retireToMainMenu() {
    openMainMenu();
    if (_screen == Screen::None) {
        _screen = Screen::MainMenu;
    }
}

void Game::openMainMenu() {
    // Only a committed playable session may replace the durable menu snapshot.
    // Cold startup and recovery from a partial load must not erase it.
    if (isTSL() && _runtimeSessionPlayable) {
        MenuPresentation state;
        state.selector = MenuPresentation::validateSelector(getGlobalNumber("GBL_MAIN_SITH_LORD"));
        if (state.selector == 4) {
            if (auto leader = _party.getLeader()) state.leader = leader->presentation();
        }
        _options.game.menuPresentation = state;
        try {
            state.save(_options.game.configurationPath);
        } catch (const std::exception &ex) {
            warn(std::string("Could not persist main menu presentation: ") + ex.what());
        }
    }
    resetGame();
    if (menuThemesAvailable()) {
        // Each show of the menu picks the next theme (design note p1_k2menus).
        _menuIdleSwitches = 0;
        _mainMenu = newThemedMainMenu();
    } else if (!_mainMenu) {
        _mainMenu = tryLoadGUI<MainMenu>();
    } else {
        _mainMenu->refreshScene();
    }
    if (!_mainMenu) {
        return;
    }
    if (!_saveLoad) {
        _saveLoad = tryLoadGUI<SaveLoad>();
    }
    playMusic(_mainMenu->musicResRef(), _mainMenu->musicClips());
    playAmbient("", 0);
    changeScreen(Screen::MainMenu);
}

void Game::openInGame() {
    _runtimeSessionPlayable = static_cast<bool>(_module);
    changeScreen(Screen::InGame);
}

namespace {

// Runs a minigame's scripts through the game's script runner and forwards
// everything else to the active view (swoop or turret).
class GameMiniGameHost : public IMiniGameHost {
public:
    GameMiniGameHost(std::function<void(const std::string &, uint32_t)> run) :
        _run(std::move(run)) {
    }

    void setView(IMiniGameView *view) { _view = view; }

    void runScript(const std::string &resRef, uint32_t callerId) override {
        _run(resRef, callerId);
    }
    void playMiniGameAnimation(const MiniGameActor &actor, const std::string &name, bool looping, bool queue, bool overlay) override {
        if (_view) {
            _view->playMiniGameAnimation(actor, name, looping, queue, overlay);
        }
    }
    void removeMiniGameAnimation(const MiniGameActor &actor, const std::string &name) override {
        if (_view) {
            _view->removeMiniGameAnimation(actor, name);
        }
    }
    void playMiniGameSound(const std::string &resRef, const glm::vec3 &position) override {
        if (_view) {
            _view->playMiniGameSound(resRef, position);
        }
    }
    void onMiniGameActorDied(const MiniGameActor &actor) override {
        if (_view) {
            _view->onMiniGameActorDied(actor);
        }
    }
    void onMiniGameGunFired(const MiniGameActor &actor, size_t bankIndex) override {
        if (_view) {
            _view->onMiniGameGunFired(actor, bankIndex);
        }
    }

private:
    std::function<void(const std::string &, uint32_t)> _run;
    IMiniGameView *_view {nullptr};
};

// World-space box of an obstacle model's "hitbump" node placed at \p position.
bool obstacleHitbump(const graphics::Model &model, const glm::vec3 &position, glm::vec3 &outMin, glm::vec3 &outMax) {
    auto node = model.getNodeByNameRecursive("hitbump");
    if (!node || !node->mesh() || !node->mesh()->mesh) {
        return false;
    }
    const auto &aabb = node->mesh()->mesh->aabb();
    glm::vec3 lo(std::numeric_limits<float>::max());
    glm::vec3 hi(-std::numeric_limits<float>::max());
    glm::mat4 transform = glm::translate(position) * node->absoluteTransform();
    for (int corner = 0; corner < 8; ++corner) {
        glm::vec3 local((corner & 1) ? aabb.max().x : aabb.min().x,
                        (corner & 2) ? aabb.max().y : aabb.min().y,
                        (corner & 4) ? aabb.max().z : aabb.min().z);
        glm::vec3 world(transform * glm::vec4(local, 1.0f));
        lo = glm::min(lo, world);
        hi = glm::max(hi, world);
    }
    outMin = lo;
    outMax = hi;
    return true;
}

} // namespace

bool Game::startMiniGame() {
    if (_miniGame) {
        return false;
    }
    if (!_module || !_module->area()) {
        return false;
    }
    auto area = _module->area();
    if (!area->hasMinigame()) {
        return false;
    }
    const MinigameSpec spec = area->miniGame();
    if (spec.type != MinigameType::SwoopRace && spec.type != MinigameType::Turret) {
        return false;
    }

    auto host = std::make_unique<GameMiniGameHost>([this](const std::string &resRef, uint32_t callerId) {
        if (_scriptRunner) {
            _scriptRunner->run(resRef, callerId);
        }
    });
    auto *hostView = host.get();
    auto session = std::make_unique<MiniGameSession>(*host);

    // Tracks and obstacles from the area layout.
    auto layout = _services.resource.layouts.get(area->name());
    auto trackFor = [&](const std::string &resRef) {
        glm::vec3 placement(0.0f);
        if (layout) {
            if (auto track = layout->findTrackByName(resRef)) {
                placement = track->get().position;
            }
        }
        auto model = resRef.empty() ? nullptr : _services.resource.models.get(resRef);
        return buildMiniGameTrack(model.get(), placement);
    };
    MiniGameTrack playerTrack = trackFor(spec.player.trackResRef);
    std::vector<MiniGameTrack> enemyTracks;
    for (const auto &enemy : spec.enemies) {
        enemyTracks.push_back(trackFor(enemy.trackResRef));
    }
    std::vector<MiniGameSession::ObstaclePlacement> obstacles;
    std::vector<glm::vec3> obstaclePositions;
    for (const auto &obstacleSpec : spec.obstacles) {
        MiniGameSession::ObstaclePlacement placement;
        placement.name = obstacleSpec.name;
        if (layout) {
            if (auto obstacle = layout->findObstacleByName(obstacleSpec.name)) {
                placement.position = obstacle->get().position;
            }
        }
        if (auto model = _services.resource.models.get(obstacleSpec.name)) {
            placement.hasHitbump = obstacleHitbump(*model, placement.position, placement.hitbumpMin, placement.hitbumpMax);
        }
        obstaclePositions.push_back(placement.position);
        obstacles.push_back(std::move(placement));
    }
    session->load(spec, std::move(playerTrack), std::move(enemyTracks), std::move(obstacles));

    // Every actor is a registered object: scripts see it as OBJECT_SELF and
    // DelayCommand queues on it.
    std::vector<std::shared_ptr<Object>> objects;
    for (size_t i = 0; i < session->actors().size(); ++i) {
        auto object = newObject<MiniGameObject>(kSceneMain, *this, _services);
        object->setActorIndex(i);
        object->setTag(session->actors()[i].name);
        session->setObjectId(i, object->id());
        objects.push_back(object);
    }

    auto camera = area->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    if (camera) {
        camera->stopMovement();
    }
    _savedCameraType = _cameraType;
    _miniGameInput = MiniGameInput();

    bool started = false;
    if (spec.type == MinigameType::SwoopRace) {
        started = _swoopRace.start(*session, spec, obstaclePositions, camera);
        hostView->setView(&_swoopRace);
    } else {
        started = _turret.start(*session, spec, camera, area->name());
        hostView->setView(&_turret);
    }
    if (!started) {
        for (auto &object : objects) {
            unregisterRuntimeObject(object);
        }
        error("minigame: the " + std::string(minigameTypeName(spec.type)) + " view failed to start");
        return false;
    }

    _miniGameHost = std::move(host);
    _miniGame = std::move(session);
    _miniGameObjects = std::move(objects);
    _miniGameModule = _module->name();

    _cameraType = CameraType::FirstPerson;
    setRelativeMouseMode(spec.type == MinigameType::Turret);
    changeScreen(spec.type == MinigameType::SwoopRace ? Screen::SwoopRace : Screen::Turret);

    // The minigame owns the party now: drop what it had queued, and hide it.
    // Vanilla does not add the party to the scene in a minigame module; the
    // minigame actor represents the player.
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->clearAllActions(/*force=*/true);
        }
    }
    setPartyVisible(false);
    if (!spec.music.empty()) {
        playMusic(spec.music);
    }

    info(str(boost::format("minigame: started %s module=%s actors=%zu enemies=%d obstacles=%d")
             % minigameTypeName(spec.type) % _miniGameModule % _miniGame->actors().size()
             % _miniGame->enemyCount() % _miniGame->obstacleCount()));

    // Creation scripts last: they may already use the view.
    _miniGame->start();
    return true;
}

void Game::endMiniGame() {
    if (!_miniGame) {
        return;
    }
    info(str(boost::format("minigame: ended %s module=%s ticks=%llu scripts=%zu")
             % minigameTypeName(_miniGame->type()) % _miniGameModule
             % static_cast<unsigned long long>(_miniGame->tickCount()) % _miniGame->scriptsRun()));
    if (_swoopRace.isActive()) {
        _swoopRace.stop();
    }
    if (_turret.isActive()) {
        _turret.stop();
    }
    if (auto host = dynamic_cast<GameMiniGameHost *>(_miniGameHost.get())) {
        host->setView(nullptr);
    }
    for (auto &object : _miniGameObjects) {
        unregisterRuntimeObject(object);
    }
    _miniGameObjects.clear();
    _miniGame.reset();
    _miniGameHost.reset();
    _miniGameInput = MiniGameInput();
    _swoopAutopilot = false;
    _turretAutopilot = false;
    _miniGameModule.clear();
    _cameraType = _savedCameraType;
    setPartyVisible(true);
}

void Game::tickMiniGame(float dt) {
    if (!_miniGame) {
        return;
    }
    if (_swoopAutopilot) {
        applySwoopAutopilot();
    }
    if (_turret.isActive()) {
        if (_turretAutopilot) {
            applyTurretAutopilot();
        }
        _turret.prepare(dt);
    }
    auto session = _miniGame.get();
    session->update(dt, _miniGameInput);
    if (_miniGame.get() != session) {
        return;
    }
    // The minigame objects' own action queues: delayed commands scheduled by
    // the slot scripts run here.
    auto objects = _miniGameObjects;
    for (auto &object : objects) {
        object->update(dt);
        if (_miniGame.get() != session) {
            return;
        }
        _scriptEvents.runDue();
        if (_miniGame.get() != session) {
            return;
        }
    }
    if (_swoopRace.isActive()) {
        _swoopRace.update(dt);
    }
    if (_turret.isActive()) {
        _turret.update(dt);
    }
}

void Game::applySwoopAutopilot() {
    auto session = _miniGame.get();
    auto player = session ? session->player() : nullptr;
    if (!player) {
        return;
    }
    // Hold fire: onfire shifts up as soon as the speed allows and does
    // nothing otherwise.
    _miniGameInput.fire = true;

    // Steer for the nearest live pad ahead, unless an obstacle sits on that
    // line; then aim for the side of the obstacle away from it.
    const glm::vec3 bike = player->position;
    float speed = glm::max(session->playerSpeed(), 1.0f);
    float lookAhead = glm::clamp(speed * 1.2f, 40.0f, 400.0f);
    float targetX = bike.x;
    float bestY = std::numeric_limits<float>::max();
    for (int i = 0; i < session->enemyCount(); ++i) {
        auto pad = session->enemy(i);
        if (!pad || pad->dead || !pad->trigger) {
            continue;
        }
        float ahead = pad->position.y - bike.y;
        if (ahead > 2.0f && ahead < lookAhead && pad->position.y < bestY) {
            bestY = pad->position.y;
            targetX = pad->position.x;
        }
    }
    for (int i = 0; i < session->obstacleCount(); ++i) {
        auto obstacle = session->obstacle(i);
        if (!obstacle || !obstacle->hasHitbump) {
            continue;
        }
        float ahead = obstacle->hitbumpMin.y - bike.y;
        if (ahead < -1.0f || ahead > glm::min(lookAhead, bestY - bike.y)) {
            continue;
        }
        float margin = player->sphereRadius + 1.5f;
        float lo = obstacle->hitbumpMin.x - margin;
        float hi = obstacle->hitbumpMax.x + margin;
        if (targetX > lo && targetX < hi) {
            float centre = (obstacle->hitbumpMin.x + obstacle->hitbumpMax.x) * 0.5f;
            targetX = (bike.x < centre) ? lo : hi;
        }
    }
    // Local lateral error in the hook frame (the courses run along +Y).
    float error = targetX - bike.x;
    float lateral = session->lateralVelocity().x;
    float command = error / 4.0f - lateral / glm::max(session->movementPerSec(), 1.0f);
    _miniGameInput.steer = glm::clamp(command, -1.0f, 1.0f);
}

void Game::applyTurretAutopilot() {
    auto session = _miniGame.get();
    if (!session) {
        return;
    }
    const auto &aim = _turret.aim();
    glm::mat4 eye = _turret.eyeTransform();
    glm::vec3 eyePos(eye[3]);
    float bulletSpeed = glm::max(_turret.playerBulletSpeed(), 1.0f);
    const Turret::Enemy *best = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (const auto &enemy : _turret.enemies()) {
        if (!enemy.alive || !enemy.modelNode) {
            continue;
        }
        float distance = glm::distance(enemy.position, eyePos);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = &enemy;
        }
    }
    _miniGameInput.pitchRate = 0.0f;
    _miniGameInput.yawRate = 0.0f;
    if (!best) {
        _miniGameInput.fire = false;
        return;
    }
    // Lead the target by its motion over the bolt's flight time.
    glm::vec3 velocity = (best->position - best->previousPosition) * 30.0f;
    glm::vec3 target = best->position + velocity * (bestDistance / bulletSpeed);
    // Aim in the anchor frame: the turret's forward is Rz(yaw) * Rx(pitch) * +Y.
    glm::mat4 anchor = _turret.anchorTransform();
    glm::vec3 local(glm::inverse(anchor) * glm::vec4(target, 1.0f));
    local -= glm::vec3(glm::inverse(anchor) * glm::vec4(eyePos, 1.0f));
    float desiredYaw = std::atan2(-local.x, local.y);
    float desiredPitch = std::atan2(local.z, glm::length(glm::vec2(local.x, local.y)));
    auto wrap = [](float a) {
        while (a > glm::pi<float>()) {
            a -= glm::two_pi<float>();
        }
        while (a < -glm::pi<float>()) {
            a += glm::two_pi<float>();
        }
        return a;
    };
    float yawError = wrap(desiredYaw - aim.yaw());
    float pitchError = desiredPitch - aim.pitch();
    // At most 6 degrees per tick (180 degrees per second).
    constexpr float kMaxStep = 6.0f;
    _miniGameInput.yawDelta = glm::clamp(glm::degrees(yawError), -kMaxStep, kMaxStep);
    _miniGameInput.pitchDelta = glm::clamp(glm::degrees(pitchError), -kMaxStep, kMaxStep);
    _miniGameInput.fire = glm::abs(glm::degrees(yawError)) < 4.0f && glm::abs(glm::degrees(pitchError)) < 4.0f;
}

void Game::setPartyVisible(bool visible) {
    for (auto &member : _party.members()) {
        if (member.creature) {
            member.creature->setVisible(visible);
        }
    }
}

void Game::openInGameMenu(InGameMenuTab tab) {
    setCursorType(CursorType::Default);
    switch (tab) {
    case InGameMenuTab::Equipment:
        _inGame->openEquipment();
        break;
    case InGameMenuTab::Inventory:
        _inGame->openInventory();
        break;
    case InGameMenuTab::Character:
        _inGame->openCharacter();
        break;
    case InGameMenuTab::Abilities:
        _inGame->openAbilities();
        break;
    case InGameMenuTab::Party:
        _inGame->openPartySelection();
        break;
    case InGameMenuTab::Messages:
        _inGame->openMessages();
        break;
    case InGameMenuTab::Journal:
        _inGame->openJournal();
        break;
    case InGameMenuTab::Map:
        _inGame->openMap();
        break;
    case InGameMenuTab::Options:
        _inGame->openOptions();
        break;
    default:
        break;
    }
    changeScreen(Screen::InGameMenu);
}

void Game::openContainer(const std::shared_ptr<Object> &container) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    if (_container) {
        _container->open(container);
    }
    if (_coop) {
        _coop->onContainerOpened(*container);
    }
    changeScreen(Screen::Container);
}

void Game::openPartySelection(const PartySelectionContext &ctx) {
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _partySelect->prepare(ctx);
    changeScreen(Screen::PartySelection);
}

bool Game::isPartySelectionRealized(
    const std::vector<int> &selectedNpcs) const {
    if (!_module || !_module->area()) {
        return false;
    }
    const auto &area = *_module->area();
    std::array<bool, Party::kMaxNpcCount> requested {};
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        if (!_party.isRosterIdentityValid(identity) || requested[npc]) {
            return false;
        }
        requested[npc] = true;
    }

    std::array<bool, Party::kMaxNpcCount> realized {};
    for (const auto &member : _party.members()) {
        if (member.npc == kNpcPlayer) {
            continue;
        }
        const RosterIdentity identity {RosterKind::Npc, member.npc};
        if (!_party.isRosterIdentityValid(identity) ||
            !requested[member.npc] || realized[member.npc] ||
            !member.creature) {
            return false;
        }
        auto bound = _party.rosterCreature(identity);
        if (bound != member.creature ||
            !isRuntimeObjectLive(*bound) ||
            area.isObjectPendingDestruction(*bound) ||
            !area.isObjectResident(*bound)) {
            return false;
        }
        realized[member.npc] = true;
    }

    return realized == requested;
}

bool Game::reconcilePartySelection(
    const std::vector<int> &selectedNpcs) {
    if (!_module || !_module->area()) {
        warn("Party selection: no active Area");
        return false;
    }

    std::array<bool, Party::kMaxNpcCount> requested {};
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        if (!_party.isRosterIdentityValid(identity) || requested[npc] ||
            !_party.isRosterAvailable(identity)) {
            warn("Party selection: NPC is not available: " +
                 std::to_string(npc));
            return false;
        }
        requested[npc] = true;
    }
    if (isPartySelectionRealized(selectedNpcs)) {
        return true;
    }

    auto area = _module->area();

    // A queued representation remains live and Area-resident until the next
    // update, but it cannot satisfy a selection which must survive that
    // update. Drop only its exact roster binding so detached Party state can
    // materialize a new incarnation. The queued destruction remains aimed at
    // the obsolete object and cannot clear the replacement binding.
    for (int npc : selectedNpcs) {
        const RosterIdentity identity {RosterKind::Npc, npc};
        auto bound = _party.rosterCreature(identity);
        if (bound &&
            (!isRuntimeObjectLive(*bound) ||
             area->isObjectPendingDestruction(*bound))) {
            _party.clearRosterCreature(identity, bound.get());
        }
    }

    const auto currentMembers = _party.members();
    for (const auto &member : currentMembers) {
        if (member.npc != kNpcPlayer &&
            (member.npc < 0 ||
             member.npc >= static_cast<int>(requested.size()) ||
             !requested[member.npc])) {
            // This companion ceases to inhabit the current Area; unlike a
            // control switch, party removal is a full Area-departure boundary.
            area->retirePartyMemberAreaRuntime(member.creature);
        }
    }

    _party.clear();
    const int controlled = _party.controlledNpc();
    if (!_party.addMember(
            controlled == kNpcPlayer ? kNpcPlayer : controlled,
            _party.player())) {
        warn("Party selection: could not restore the controlled character");
        return false;
    }

    for (int npc : selectedNpcs) {
        auto member = _party.getAvailableMember(npc, true);
        if (!member || !_party.addMember(npc, member)) {
            warn("Party selection: could not realize NPC: " +
                 std::to_string(npc));
            return false;
        }
    }

    area->repositionParty();
    return isPartySelectionRealized(selectedNpcs);
}

void Game::openSaveLoad(SaveLoadMode mode) {
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _saveLoad->setMode(mode);
    _saveLoad->refresh();
    changeScreen(Screen::SaveLoad);
}

bool Game::canOpenGalaxyMapFrom(Screen screen) {
    switch (screen) {
    case Screen::None:
    case Screen::InGame:
    case Screen::InGameMenu:
    case Screen::Conversation:
        return true;
    default:
        // Every other screen owns the whole display and has somewhere of its
        // own to return to. Taking it over would strand it.
        return false;
    }
}

void Game::openGalaxyMap(int initialPlanet) {
    if (!canOpenGalaxyMapFrom(_screen)) {
        return;
    }
    if (_galaxyMap && _galaxyMap->isRunningTravelScript()) {
        // The travel script this panel dispatched must not reopen it.
        return;
    }
    if (!_galaxyMap) {
        _galaxyMap = tryLoadGUI<GalaxyMap>();
    }
    if (!_galaxyMap) {
        // A panel that will not load must not take the screen away from
        // whatever is on it.
        return;
    }
    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    // The panel decides what the routine's planet means: K2 has to record
    // where the party already is before anything can move the selection.
    _galaxyMap->prepare(initialPlanet);
    changeScreen(Screen::GalaxyMap);
}

void Game::serializePazaakPartyTable(resource::Gff &ptGff) const {
    auto replaceField = [&ptGff](resource::Gff::Field replacement) {
        auto &fields = ptGff.fields();
        auto found = std::find_if(fields.begin(), fields.end(), [&replacement](const auto &field) {
            return field.label == replacement.label;
        });
        if (found == fields.end()) {
            fields.push_back(std::move(replacement));
        } else {
            *found = std::move(replacement);
        }
    };

    replaceField(resource::Gff::Field::newDword(
        "PT_GOLD",
        static_cast<uint32_t>(std::max(0, _party.gold()))));
    if (!_party.hasValidPazaakData()) {
        return;
    }

    // Only the entries the running title actually stores are written back.
    std::vector<std::shared_ptr<resource::Gff>> cardEntries;
    const auto &savedCounts = _party.pazaakCardCounts();
    for (size_t i = 0; i < _party.pazaakCardCount(); ++i) {
        int count = savedCounts[i];
        cardEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newByte(
                    "PT_PAZAAKCOUNT",
                    static_cast<uint32_t>(count)))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZAAKCARDS",
        std::move(cardEntries)));

    std::vector<std::shared_ptr<resource::Gff>> sideEntries;
    for (int cardId : _party.pazaakSideDeck()) {
        sideEntries.push_back(
            resource::Gff::Builder()
                .field(resource::Gff::Field::newInt(
                    "PT_PAZSIDECARD",
                    cardId))
                .build());
    }
    replaceField(resource::Gff::Field::newList(
        "PT_PAZSIDELIST",
        std::move(sideEntries)));
}

bool Game::playPazaak(
    int opponentDeck,
    std::string continuationScript,
    int maximumWager,
    bool tutorialRequested,
    const std::shared_ptr<Object> &opponent) {

    if (!opponent) {
        return false;
    }

    PazaakSessionParams params;
    params.opponentDeck = opponentDeck;
    params.continuationScript = std::move(continuationScript);
    params.maximumWager = maximumWager;
    params.tutorialRequested = tutorialRequested;
    params.opponentId = opponent->id();
    params.opponentName = opponent->name().empty() ? opponent->tag() : opponent->name();

    // A native match always plays with the cards the player actually owns, read
    // from PARTYTABLE.res. Only the developer command uses temporary cards.
    if (!_party.hasValidPazaakData()) {
        error("Unable to start Pazaak: PARTYTABLE.res has no valid Pazaak data");
        return false;
    }
    int cardTypes = static_cast<int>(
        (isTSL() ? Party::kK2PazaakCardCount : Party::kK1PazaakCardCount) - 1);
    std::array<std::optional<size_t>, Party::kMaxPazaakCardCount> collectionIndex;
    const auto &counts = _party.pazaakCardCounts();
    size_t ownedCards = 0;
    for (int cardId = 0; cardId < cardTypes; ++cardId) {
        if (counts[cardId] == 0) {
            continue;
        }
        auto definition = isTSL() ? k2PazaakCardDefinition(cardId)
                                  : k1PazaakCardDefinition(cardId);
        if (!definition) {
            error("Unable to start Pazaak: invalid player collection card ID");
            return false;
        }
        collectionIndex[cardId] = params.collection.size();
        params.collection.push_back(
            {*definition, static_cast<size_t>(counts[cardId]), cardId});
        ownedCards += static_cast<size_t>(counts[cardId]);
    }
    if (ownedCards < pazaak::kSideDeckSize) {
        error("Unable to start Pazaak: player owns fewer than ten side-deck cards");
        return false;
    }

    const auto &savedSideDeck = _party.pazaakSideDeck();
    if (std::all_of(savedSideDeck.begin(), savedSideDeck.end(), [](int id) {
            return id >= 0;
        })) {
        for (int cardId : savedSideDeck) {
            if (cardId >= cardTypes || !collectionIndex[cardId]) {
                error("Unable to start Pazaak: saved side deck is not owned");
                return false;
            }
            params.initialChosenCards.push_back(*collectionIndex[cardId]);
        }
    }

    if (_pazaakOpponentDeckOverride) {
        params.opponentSideDeck = _pazaakOpponentDeckOverride;
    } else {
        try {
            auto decks = _services.resource.twoDas.get("pazaakdecks");
            if (!decks) {
                error("Unable to start Pazaak: pazaakdecks.2da is missing");
                return false;
            }
            params.opponentSideDeck = isTSL()
                                          ? loadK2PazaakOpponentDeck(*decks, opponentDeck)
                                          : loadK1PazaakOpponentDeck(*decks, opponentDeck);
        } catch (const std::exception &e) {
            error("Unable to read pazaakdecks.2da: " + std::string(e.what()));
            return false;
        }
        if (!params.opponentSideDeck) {
            error("Unable to start Pazaak: invalid opponent deck row in pazaakdecks.2da");
            return false;
        }
    }
    return startPazaakFlow(std::move(params), opponent, false);
}

bool Game::startDevelopmentPazaak(std::string opponentName, int maximumWager) {
    PazaakSessionParams params;
    params.opponentDeck = 0;
    params.maximumWager = maximumWager;
    params.opponentName = opponentName.empty() ? "Pazaak Opponent" : std::move(opponentName);
    // The developer route never touches save-owned cards or credits: it uses a
    // temporary, title-appropriate collection and opponent deck only.
    if (isTSL()) {
        params.collection = PazaakSession::k2DefaultCollection();
        params.opponentSideDeck = PazaakSession::temporaryK2OpponentSideDeck();
        // Deterministic showcase deck covering every KotOR II family. The first
        // four entries become the opening hand: a Value Change card, a
        // sign-selectable card, a fixed card and a non-switchable special.
        params.initialChosenCards = PazaakSession::k2ShowcaseChosenCards();
        _pazaakShowcaseHands = true;
    } else {
        params.collection = PazaakSession::temporaryK1TestCollection();
        params.opponentSideDeck = PazaakSession::temporaryK1OpponentSideDeck();
    }
    return startPazaakFlow(std::move(params), nullptr, true);
}

bool Game::startPazaakFlow(
    PazaakSessionParams params,
    const std::shared_ptr<Object> &continuationCaller,
    bool developmentLaunch) {

    if (_pazaakSession) {
        return false;
    }

    _pazaakOriginScreen = _screen;
    _pazaakContinuationCaller = continuationCaller;
    _pazaakDevelopmentLaunch = developmentLaunch;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakOpponentEventElapsed = 0.0f;
    params.availableCredits = _party.gold();
    params.paceAutomaticDraws = _pazaakPaceAutomaticDraws;
    if (auto player = _party.player()) {
        params.playerName = player->name();
    }

    auto playerSelector = _pazaakPlayerHandSelector
                              ? _pazaakPlayerHandSelector
                              : (_pazaakShowcaseHands
                                     ? showcasePazaakHandSelector()
                                     : PazaakSession::HandSelector(randomPazaakHandSelection));
    auto opponentSelector = _pazaakOpponentHandSelector
                                ? _pazaakOpponentHandSelector
                                : PazaakSession::HandSelector(randomPazaakHandSelection);
    auto mainDeckFactory = _pazaakMainDeckFactory
                               ? _pazaakMainDeckFactory
                               : PazaakSession::MainDeckFactory(randomPazaakMainDeck);
    pazaak::Participant firstParticipant =
        randomInt(0, 1) == 0 ? pazaak::Participant::One : pazaak::Participant::Two;
    auto firstParticipantSelector = _pazaakFirstParticipantSelector
                                        ? _pazaakFirstParticipantSelector
                                        : PazaakSession::FirstParticipantSelector(
                                              [firstParticipant](size_t setIndex) {
                                                  bool useInitial = setIndex % 2 == 0;
                                                  if (useInitial) {
                                                      return firstParticipant;
                                                  }
                                                  return firstParticipant == pazaak::Participant::One
                                                             ? pazaak::Participant::Two
                                                             : pazaak::Participant::One;
                                              });

    try {
        _pazaakSession = std::make_unique<PazaakSession>(
            std::move(params),
            std::move(playerSelector),
            std::move(opponentSelector),
            std::move(mainDeckFactory),
            std::move(firstParticipantSelector));
    } catch (const std::exception &e) {
        error("Unable to create Pazaak session: " + std::string(e.what()));
        releasePazaakFlow(true);
        return false;
    }

    if (!loadPazaakGUIs()) {
        releasePazaakFlow(true);
        return false;
    }

    if (_module && _module->area()) {
        stopMovement();
    }
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    if (_pazaakSession->screen() == PazaakFlowScreen::Wager) {
        if (_pazaakWager) {
            _pazaakWager->refresh();
        }
        changeScreen(Screen::PazaakWager);
    } else {
        showPazaakSetup();
    }
    return true;
}

bool Game::loadPazaakGUIs() {
    if (_pazaakGuiLoadOverride) {
        _pazaakGUIsReady = _pazaakGuiLoadOverride();
        return _pazaakGUIsReady;
    }

    _pazaakWager = tryLoadGUI<PazaakWagerGUI>();
    _pazaakSetup = tryLoadGUI<PazaakSetupGUI>();
    _pazaakBoard = tryLoadGUI<PazaakBoardGUI>();
    _pazaakGUIsReady = _pazaakWager && _pazaakSetup && _pazaakBoard;
    if (!_pazaakGUIsReady) {
        _pazaakWager.reset();
        _pazaakSetup.reset();
        _pazaakBoard.reset();
    }
    return _pazaakGUIsReady;
}

void Game::showPazaakSetup() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Setup) {
        return;
    }
    if (_pazaakSetup) {
        _pazaakSetup->refresh();
    }
    changeScreen(Screen::PazaakSetup);
}

void Game::showPazaakBoard() {
    if (!_pazaakSession ||
        !_pazaakGUIsReady ||
        _pazaakSession->screen() != PazaakFlowScreen::Board ||
        !_pazaakSession->match()) {
        return;
    }
    if (!_pazaakDevelopmentLaunch && !_pazaakSelectionPersisted) {
        Party::PazaakSideDeck selected;
        const auto &collection = _pazaakSession->collection();
        const auto &chosen = _pazaakSession->chosenCards();
        if (chosen.size() != selected.size()) {
            error("Unable to persist Pazaak side deck: selection is incomplete");
            return;
        }
        for (size_t i = 0; i < chosen.size(); ++i) {
            if (chosen[i] >= collection.size() ||
                collection[chosen[i]].persistentId < 0) {
                error("Unable to persist Pazaak side deck: invalid collection mapping");
                return;
            }
            selected[i] = collection[chosen[i]].persistentId;
        }
        _party.setPazaakSideDeck(std::move(selected));
        _pazaakSelectionPersisted = true;
    }
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    changeScreen(Screen::PazaakBoard);
    completePazaakIfReady();
}

void Game::cancelPazaak() {
    if (!_pazaakSession || _pazaakSession->screen() == PazaakFlowScreen::Board) {
        return;
    }
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    releasePazaakFlow(true);
    if (developmentLaunch) {
        _console.printLine("pazaak: development match cancelled");
    }
}

void Game::abortPazaak() {
    if (!_pazaakSession) {
        return;
    }
    releasePazaakFlow(true);
}

void Game::completePazaakIfReady() {
    if (!_pazaakSession ||
        !_pazaakSession->completedResult() ||
        _pazaakSession->presentationPending()) {
        return;
    }
    finishPazaak(*_pazaakSession->completedResult());
}

void Game::finishPazaak(PazaakCompletedResult result) {
    if (!_pazaakSession) {
        return;
    }

    std::string continuation(_pazaakSession->continuationScript());
    uint32_t opponentId = _pazaakSession->opponentId();
    std::shared_ptr<Object> continuationCaller(
        _pazaakContinuationCaller.resolve());
    bool developmentLaunch = _pazaakDevelopmentLaunch;
    int wager = _pazaakSession->wager();
    bool callerValid = continuationCaller &&
                       continuationCaller->id() == opponentId &&
                       getObjectById(opponentId) == continuationCaller;
    _lastPazaakResult = result;

    if (_coop && _coop->isClient()) {
        // Co-op Free Mode: a guest's own game; its credits are the host's to settle.
        _coop->clientPazaakFinished(result == PazaakCompletedResult::PlayerWon, wager);
    }
    if (!developmentLaunch && !_pazaakSettlementApplied) {
        if (result == PazaakCompletedResult::PlayerWon) {
            _party.giveGold(wager);
        } else {
            _party.takeGold(wager);
        }
        _pazaakSettlementApplied = true;
    }

    // Release ownership before external script execution so re-entrant or
    // repeated completion cannot invoke the continuation twice.
    releasePazaakFlow(true);

    if (developmentLaunch) {
        switch (result) {
        case PazaakCompletedResult::PlayerWon:
            _console.printLine("pazaak: development match completed - player won");
            break;
        case PazaakCompletedResult::OpponentWon:
            _console.printLine("pazaak: development match completed - opponent won");
            break;
        case PazaakCompletedResult::PlayerForfeited:
            _console.printLine("pazaak: development match completed - player forfeited");
            break;
        }
    }

    if (continuation.empty()) {
        return;
    }
    if (!callerValid) {
        error("Pazaak continuation skipped because its caller is no longer valid");
        return;
    }
    if (_pazaakContinuationOverride) {
        _pazaakContinuationOverride(continuation, opponentId);
    } else if (_scriptRunner) {
        _scriptRunner->run(continuation, opponentId);
    }
}

Game::Screen Game::safePazaakOriginScreen() const {
    switch (_pazaakOriginScreen) {
    case Screen::PazaakWager:
    case Screen::PazaakSetup:
    case Screen::PazaakBoard:
        return _module ? Screen::InGame : Screen::None;
    case Screen::Conversation:
        // The dialogue may finish after its action script opens Pazaak.
        // Returning to the world cannot strand an ended conversation GUI.
        return _module ? Screen::InGame : Screen::None;
    default:
        return _pazaakOriginScreen;
    }
}

void Game::releasePazaakFlow(bool restoreOrigin) {
    Screen restore = safePazaakOriginScreen();
    _pazaakSession.reset();
    _pazaakWager.reset();
    _pazaakSetup.reset();
    _pazaakBoard.reset();
    _pazaakGUIsReady = false;
    _pazaakContinuationCaller.reset();
    _pazaakDevelopmentLaunch = false;
    _pazaakSelectionPersisted = false;
    _pazaakSettlementApplied = false;
    _pazaakShowcaseHands = false;
    _pazaakOpponentEventElapsed = 0.0f;
    if (restoreOrigin) {
        changeScreen(restore);
    }
    _pazaakOriginScreen = Screen::None;
}

void Game::openLevelUp() {
    openLevelUp(nullptr);
}

void Game::openLevelUp(const std::shared_ptr<Creature> &member) {
    if (_coop && _coop->isClient() && !_coop->clientMayLevelUp()) {
        // Co-op: only the companion's own level, within the host's level.
        return;
    }
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }

    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    _charGen->startLevelUp(member);
    changeScreen(Screen::CharacterGeneration);
}

void Game::notifyLevelUpPending(const Creature &creature) {
    if (!_party.isMember(creature)) {
        return;
    }
    _services.audio.mixer.play(_services.game.guiSounds.getOnLevelUpNotify(), AudioType::Sound);
}

void Game::startCharacterGeneration() {
    resetGame();
    if (!_charGen) {
        _charGen = tryLoadGUI<CharacterGeneration>();
    }
    if (!_charGen) {
        return;
    }
    withLoadingScreen(_charGen->loadScreenResRef(), [this]() {
        _loadScreen->setProgress(100);
        render();
        playMusic(_charGen->musicResRef());
        playAmbient("", 0);
        changeScreen(Screen::CharacterGeneration);
    });
}

void Game::startDialog(const std::shared_ptr<Object> &owner, const std::string &resRef,
                       GlobalFade::DialogTicket admission) {
    if (_captureHUDPresentation) {
        return;
    }
    if (admission && !_globalFade.isCurrentDialog(admission)) {
        return;
    }
    std::shared_ptr<resource::Dialog> dialog;
    try {
        if (_services.resource.gffs.get(resRef, ResType::Dlg)) {
            dialog = _services.resource.dialogs.get(resRef);
        }
    } catch (const std::exception &e) {
        warn("Game: conversation load failed: " + resRef + ": " + e.what());
        _globalFade.finishDialog(admission);
        return;
    }
    if (!dialog) {
        warn("Game: conversation not found or invalid: " + resRef);
        _globalFade.finishDialog(admission);
        return;
    }

    _lastConversation = resRef;
    bool computerConversation = dialog->conversationType == ConversationType::Computer;
    auto conversation = computerConversation ? _computer.get() : static_cast<Conversation *>(_dialog.get());
    if (!conversation) {
        _globalFade.finishDialog(admission);
        return;
    }

    stopMovement();
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    changeScreen(Screen::Conversation);

    _conversation = conversation;
    _conversation->setAutoSkip(&_conversationAutoSkip);
    _conversation->start(dialog, owner, std::move(admission));
}

void Game::resumeConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->resume();
}

void Game::pauseConversation() {
    if (!_conversation || !isConversationActive()) {
        return;
    }
    _conversation->pause();
}

void Game::loadInGameMenus() {
    if (!_hud) {
        _hud = tryLoadGUI<HUD>();
    }
    if (!_inGame) {
        _inGame = tryLoadGUI<InGameMenu>();
    }
    if (!_dialog) {
        _dialog = tryLoadGUI<DialogGUI>();
    }
    if (!_computer) {
        _computer = tryLoadGUI<ComputerGUI>();
    }
    if (!_container) {
        _container = tryLoadGUI<ContainerGUI>();
    }
    if (!_partySelect) {
        _partySelect = tryLoadGUI<PartySelection>();
    }
}

void Game::changeScreen(Screen screen) {
    auto gui = getScreenGUI();
    if (gui) {
        gui->clearSelection();
    }
    if (_confirmPopup) {
        _confirmPopup->hide();
    }
    _screen = screen;
}

GameGUI *Game::getScreenGUI() const {
    switch (_screen) {
    case Screen::MainMenu:
        return _mainMenu.get();
    case Screen::Loading:
        return _loadScreen.get();
    case Screen::CharacterGeneration:
        return _charGen.get();
    case Screen::InGame:
        return _cameraType == game::CameraType::ThirdPerson ? _hud.get() : nullptr;
    case Screen::InGameMenu:
        return _inGame.get();
    case Screen::Conversation:
        return _conversation;
    case Screen::Container:
        return _container.get();
    case Screen::PartySelection:
        return _partySelect.get();
    case Screen::SaveLoad:
        return _saveLoad.get();
    case Screen::GalaxyMap:
        return _galaxyMap.get();
    case Screen::SwoopRace:
        return nullptr; // race skeleton has no HUD yet
    case Screen::PazaakWager:
        return _pazaakWager.get();
    case Screen::PazaakSetup:
        return _pazaakSetup.get();
    case Screen::PazaakBoard:
        return _pazaakBoard.get();
    case Screen::Turret:
        return nullptr; // the turret HUD is part of the player model set
    case Screen::Store:
        return _store.get();
    case Screen::Upgrade:
        return _upgrade.get();
    case Screen::Credits:
        return _credits.get();
    default:
        return nullptr;
    }
}

void Game::setBarkBubbleText(std::string text, float duration) {
    if (_coop && _coop->isHost()) {
        // Co-op: the host's barks show on every player's screen.
        _coop->onBark(text, duration);
    }
    _hud->barkBubble().setBarkText(text, duration);
}

void Game::submitStatusSummary(
    StatusSummaryCategory category,
    int amount,
    std::vector<std::string> items) {

    // Suppression and the Status Summary preference belong at this single
    // submission boundary when those vanilla behaviours are implemented.
    _statusSummary.submit(category, amount, std::move(items));
    if (_hud) {
        _hud->activateStatusSummaryIndicator(category);
    }
}

void Game::submitAlignmentShift(int shift) {
    if (shift == 0) {
        return;
    }
    _statusSummary.submitAlignment(shift);
    if (_hud) {
        _hud->activateStatusSummaryIndicator(
            shift > 0 ? StatusSummaryCategory::LightSideShift : StatusSummaryCategory::DarkSideShift);
    }
}

int Game::getPlotXP(const std::string &plotName) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable) {
        return 0;
    }
    for (int row = 0; row < plotTable->getRowCount(); ++row) {
        if (boost::iequals(plotTable->getString(row, "label"), plotName)) {
            return plotTable->getInt(row, "xp");
        }
    }
    return 0;
}

int Game::getPlotXPByIndex(int plotIndex) {
    std::shared_ptr<TwoDA> plotTable(_services.resource.twoDas.get("plot"));
    if (!plotTable || plotIndex < 0 || plotIndex >= plotTable->getRowCount()) {
        return 0;
    }
    return plotTable->getInt(plotIndex, "xp");
}

void Game::awardPlotXP(const std::string &plotName, int percentage) {
    if (plotName.empty() || percentage == 0) {
        return;
    }
    int baseXP = getPlotXP(plotName);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>((static_cast<int64_t>(baseXP) * percentage) / 100);
    _party.awardXP(amount, XPSource::Plot);
}

void Game::awardPlotXPByIndex(int plotIndex, float fraction) {
    if (plotIndex < 0 || fraction == 0.0f) {
        return;
    }
    int baseXP = getPlotXPByIndex(plotIndex);
    if (baseXP == 0) {
        return;
    }
    int amount = static_cast<int>(baseXP * fraction);
    _party.awardXP(amount, XPSource::Plot);
}

bool Game::setDialogPlaceableCamera(int cameraId) {
    if (!isConversationActive() || !_conversation || !_module || !_module->area()) {
        return false;
    }
    bool found = false;
    for (const auto &object : _module->area()->objects()) {
        if (object->type() != ObjectType::Camera) {
            continue;
        }
        if (static_cast<const Camera &>(*object).cameraId() == cameraId) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    _conversation->setPlaceableCamera(cameraId);
    return true;
}

void Game::resetDialogState() {
    if (isConversationActive()) {
        return;
    }
    _globalFade.releaseDialogHold();
}

void Game::refreshAreaMusic() {
    if (_module && _module->area()) {
        const AreaMusic &music = _module->area()->musicState();
        playMusic(music.current());
        playAmbient(music.currentAmbient(), music.currentAmbientVolume());
    }
}

void Game::showMessageBox(const std::string &text) {
    if (text.empty()) {
        return;
    }
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (_confirmPopup) {
        _confirmPopup->show(text);
    }
}

bool Game::showTutorialWindow(int window) {
    if (!_tutorialWindowsEnabled || window < 0 || wasTutorialWindowShown(window)) {
        return false;
    }
    std::shared_ptr<TwoDA> table(_services.resource.twoDas.get("tutorial"));
    if (!table || window >= table->getRowCount()) {
        return false;
    }
    int strref = table->getInt(window, "message0", -1);
    if (strref < 0) {
        return false;
    }
    std::string text(_services.resource.strings.getText(strref));
    if (text.empty()) {
        return false;
    }
    _tutorialWindowsShown.insert(window);
    info(str(boost::format("Tutorial window %d: %s") % window % table->getString(window, "label")));
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (_confirmPopup) {
        _confirmPopup->show(text);
    }
    return true;
}

int Game::subScreenId() const {
    if (_screen != Screen::InGameMenu || !_inGame) {
        return 0;
    }
    // SUBSCREEN_ID_* in nwscript.nss.
    switch (_inGame->activeTab()) {
    case InGameMenuTab::Equipment:
        return 1;
    case InGameMenuTab::Inventory:
        return 2;
    case InGameMenuTab::Character:
        return 3;
    case InGameMenuTab::Abilities:
        return 4;
    case InGameMenuTab::Map:
        return 5;
    case InGameMenuTab::Journal:
        return 6;
    case InGameMenuTab::Options:
        return 7;
    case InGameMenuTab::Messages:
        return 8;
    default:
        return 0;
    }
}

void Game::onModuleSelected(const std::string &module) {
    _mainMenu->onModuleSelected(module);
}

void Game::renderHUD() {
    _hud->render();
}

CameraType Game::getConversationCamera(int &cameraId) const {
    return _conversation->getCamera(cameraId);
}

void Game::updateImGui(float dt) {
    ImGui::ShowDemoWindow(&_showImGui);
}

std::shared_ptr<Object> Game::getConsoleTargetObject() {
    auto object = getConsoleArea()->selectedObject();
    if (!object) {
        object = party().getLeader();
    }
    if (object) {
        return object;
    }
    throw std::runtime_error("No object is selected");
}

std::shared_ptr<Creature> Game::getConsoleTargetCreature() {
    if (auto object = getConsoleArea()->selectedObject()) {
        if (auto creature = dyn_cast<Creature>(object)) {
            return creature;
        }
        throw std::runtime_error("Selected object must be a creature");
    }

    return getConsoleLeader();
}

std::shared_ptr<Creature> Game::getConsoleLeader() {
    if (std::shared_ptr<Creature> leader = _party.getLeader()) {
        return leader;
    }
    throw std::runtime_error("No party leader");
}

std::shared_ptr<Area> Game::getConsoleArea() {
    std::shared_ptr<Module> mod = module();
    if (!mod) {
        throw std::runtime_error("Module is not loaded");
    }

    if (std::shared_ptr<Area> area = mod->area()) {
        return area;
    }
    throw std::runtime_error("Area is not loaded");
}

static void consoleCheckUsage(const ConsoleArgs &args,
                              size_t minArgs, size_t maxArgs,
                              std::string_view usage) {
    size_t numArgs = args.size() - 1;
    if (numArgs < minArgs || numArgs > maxArgs) {
        throw std::runtime_error(str(boost::format("Usage: %s %s") % args[0].value() % usage));
    }
}

void Game::consoleInfo(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    glm::vec3 position(object->position());

    std::stringstream ss;
    ss << std::setprecision(2) << std::fixed
       << "id=" << object->id()
       << " "
       << "tag=\"" << object->tag() << "\""
       << " "
       << "tpl=\"" << object->blueprintResRef() << "\""
       << " "
       << "pos=[" << position.x << ", " << position.y << ", " << position.z << "]";

    if (auto creature = dyn_cast<Creature>(object)) {
        ss << " "
           << "app=" << creature->appearance()
           << " "
           << "fac=" << static_cast<int>(creature->faction());
    } else if (auto placeable = dyn_cast<Placeable>(object)) {
        ss << " "
           << "app=" << placeable->appearance();
    }

    _console.printLine(ss.str());
}

void Game::consoleListGlobals(const ConsoleArgs &args) {
    auto &strings = globalStrings();
    for (auto &var : strings) {
        _console.printLine(var.first + " = " + var.second);
    }

    auto &booleans = globalBooleans();
    for (auto &var : booleans) {
        _console.printLine(var.first + " = " + (var.second ? "true" : "false"));
    }

    auto &numbers = globalNumbers();
    for (auto &var : numbers) {
        _console.printLine(var.first + " = " + std::to_string(var.second));
    }

    auto &locations = globalLocations();
    for (auto &var : locations) {
        _console.printLine(str(boost::format("%s = (%.04f, %.04f, %.04f, %.04f") %
                               var.first %
                               var.second->position().x %
                               var.second->position().y %
                               var.second->position().z %
                               var.second->facing()));
    }
}

void Game::consoleListLocals(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();

    auto &booleans = object->localBooleans();
    for (auto &var : booleans) {
        _console.printLine(std::to_string(var.first) + " -> " + (var.second ? "true" : "false"));
    }

    auto &numbers = object->localNumbers();
    for (auto &var : numbers) {
        _console.printLine(std::to_string(var.first) + " -> " + std::to_string(var.second));
    }
}

void Game::consoleListAnim(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    auto substr = args[1];

    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    std::vector<std::string> anims(model->model().getAnimationNames());
    sort(anims.begin(), anims.end());

    for (auto &anim : anims) {
        if (!substr || boost::contains(anim, substr.value())) {
            _console.printLine(anim);
        }
    }
}

void Game::consolePlayAnim(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "anim_name");
    std::string anim(args[1].value());

    auto object = getConsoleTargetObject();
    auto model = std::static_pointer_cast<ModelSceneNode>(object->sceneNode());
    model->playAnimation(anim, nullptr, AnimationProperties::fromFlags(AnimationFlags::loop));
}

void Game::consoleKill(const ConsoleArgs &args) {
    auto object = getConsoleTargetObject();
    if (args.size() > 1 && args[1] && *args[1] == "force") {
        // Harness: past Min1HP and immortality too (a party wipe where the
        // story keeps the party at 1 HP, as on the Endar Spire).
        object->setMinOneHP(false);
        if (auto creature = std::dynamic_pointer_cast<Creature>(object)) {
            creature->setImmortal(false);
        }
    }
    auto effect = newEffect<DamageEffect>(
        100000,
        DamageType::Universal,
        DamagePower::Normal);
    object->applyEffect(std::move(effect), DurationType::Instant);
}

void Game::consoleAddItem(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "item_tpl [size]");
    auto object = getConsoleTargetObject();
    int stackSize = args.get<int>(2).value_or(1);
    auto receiver = _party.sharedInventoryReceiver(object);
    receiver->addItem(std::string(args[1].value()), stackSize);
}

void Game::consoleGiveXP(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    auto creature = getConsoleTargetCreature();
    int amount = args.get<int>(1).value();
    if (_party.isMember(*creature)) {
        _party.awardXP(amount, XPSource::Console);
    } else {
        creature->giveXP(amount);
    }
}

void Game::consoleGiveGold(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "amount");
    _party.giveGold(args.get<int>(1).value());
    _console.printLine(str(boost::format("party gold: %d") % _party.gold()));
}

// The free camera is the first-person camera flown off the player: WASD/QZ
// move it, the mouse aims it. These commands exist so a viewpoint found by
// hand can be replayed exactly from a commands file - camstatus prints the
// line to paste - and the same commands drive other builds of the engine,
// which keeps captures comparable between them.
void Game::consoleCamera(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "free");
    if (args[1].value() != "free") {
        throw std::runtime_error("Unknown camera: " + std::string(args[1].value()));
    }
    if (_screen != Screen::InGame) {
        throw std::runtime_error("The free camera needs the in-game screen");
    }
    if (_cameraType != CameraType::FirstPerson) {
        toggleInGameCameraType();
    }
}

void Game::consoleCamPos(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setPosition({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamLook(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    camera->setLookAt({args.get<float>(1).value(), args.get<float>(2).value(), args.get<float>(3).value()});
}

void Game::consoleCamStatus(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto camera = getConsoleArea()->getCamera<FirstPersonCamera>(CameraType::FirstPerson);
    glm::vec3 pos = camera->position();
    glm::vec3 forward(-glm::sin(camera->facing()) * glm::cos(camera->pitch()),
                      glm::cos(camera->facing()) * glm::cos(camera->pitch()),
                      glm::sin(camera->pitch()));
    glm::vec3 target = pos + forward;
    std::string result = str(boost::format("camera free; campos %.6f %.6f %.6f; camlook %.6f %.6f %.6f") %
                             pos.x % pos.y % pos.z % target.x % target.y % target.z);
    _console.printLine(result);
    info(result);
}

void Game::consoleWarp(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "module");
    // Gallery states share an engine process for speed. A warp is their scene
    // boundary, so fixture-only HUD state must not bleed into the next image.
    _captureHUDPresentation = false;
    if (_hud) {
        _hud->clearCapturePresentation();
    }
    loadModule(std::string(args[1].value()));
}

void Game::consoleOpenMenu(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "main|save|load|equipment|equipment-items|inventory|character|abilities|party|partyselect|messages|journal|map|options");

    std::string_view name(args[1].value());
    if (boost::iequals(name, "main")) {
        openMainMenu();
        return;
    }
    if (boost::iequals(name, "partyselect")) {
        // The stand-alone screen, as ShowPartySelectionGUI opens it.
        openPartySelection(PartySelectionContext());
        return;
    }
    if (boost::iequals(name, "save") || boost::iequals(name, "load")) {
        bool save = boost::iequals(name, "save");
        openSaveLoad(save ? SaveLoadMode::Save
                          : (_module ? SaveLoadMode::LoadFromInGame : SaveLoadMode::LoadFromMainMenu));
        return;
    }
    if (boost::iequals(name, "equipment-items")) {
        setCursorType(CursorType::Default);
        _inGame->openEquipmentItems();
        changeScreen(Screen::InGameMenu);
        return;
    }

    static const std::array<std::pair<std::string_view, InGameMenuTab>, 9> kTabs {{
        {"equipment", InGameMenuTab::Equipment},
        {"inventory", InGameMenuTab::Inventory},
        {"character", InGameMenuTab::Character},
        {"abilities", InGameMenuTab::Abilities},
        {"party", InGameMenuTab::Party},
        {"messages", InGameMenuTab::Messages},
        {"journal", InGameMenuTab::Journal},
        {"map", InGameMenuTab::Map},
        {"options", InGameMenuTab::Options},
    }};
    for (const auto &[tabName, tab] : kTabs) {
        if (boost::iequals(name, tabName)) {
            openInGameMenu(tab);
            return;
        }
    }
    throw std::runtime_error("Unknown in-game menu tab: " + std::string(name));
}

static std::string joinConsoleArgs(const ConsoleArgs &args, size_t first) {
    std::string result;
    for (size_t i = first; i < args.size(); ++i) {
        if (!result.empty()) {
            result += ' ';
        }
        result += args[i].value();
    }
    return result;
}

void Game::consoleOpenCharacterGeneration(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "class|quick-or-custom|quick|portrait|name|custom|abilities|skills|feats|powers|level-up|prestige [select]");

    std::string_view screen(args[1].value());
    bool selectFirst = args.size() > 2 && boost::iequals(std::string(args[2].value()), "select");
    if (boost::iequals(screen, "class")) {
        startCharacterGeneration();
        if (_charGen) {
            _charGen->openClassSelection();
        }
        return;
    }

    if (!_module || !_party.getLeader()) {
        throw std::runtime_error("Character-generation capture screens require a loaded party");
    }
    openLevelUp();
    if (!_charGen) {
        throw std::runtime_error("Character generation GUI is unavailable");
    }
    if (boost::iequals(screen, "level-up")) {
        _charGen->openLevelUp();
        return;
    }
    if (boost::iequals(screen, "prestige")) {
        // The K2 prestige step the level-up opened on (when it is offered).
        if (!_charGen->isPrestigeScreenOpen()) {
            throw std::runtime_error("No prestige class is offered to the party leader");
        }
        return;
    }

    Character character(_charGen->character());
    ClassType captureClass = boost::iequals(screen, "powers")
                                 ? ClassType::JediConsular
                                 : (isTSL() ? ClassType::JediGuardian : ClassType::Soldier);
    std::shared_ptr<CreatureClass> clazz(_services.game.classes.get(captureClass));
    if (!clazz) {
        throw std::runtime_error("Starting class is unavailable");
    }
    character.attributes = clazz->defaultAttributes();
    _charGen->setCharacter(std::move(character));

    if (boost::iequals(screen, "quick")) {
        _charGen->startQuick();
        return;
    }
    _charGen->startCustom();
    if (boost::iequals(screen, "quick-or-custom")) {
        _charGen->openQuickOrCustom();
        return;
    }
    if (boost::iequals(screen, "portrait")) {
        _charGen->openPortraitSelection();
        return;
    }
    if (boost::iequals(screen, "name")) {
        _charGen->openNameEntry();
        return;
    }
    if (boost::iequals(screen, "custom")) {
        _charGen->openCustom();
        return;
    }
    if (boost::iequals(screen, "abilities")) {
        _charGen->openAbilities();
        return;
    }
    if (boost::iequals(screen, "skills")) {
        _charGen->openSkills();
        if (selectFirst) {
            _charGen->skills().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "feats")) {
        _charGen->openFeats();
        if (selectFirst) {
            _charGen->feats().selectFirstEntryForCapture();
        }
        return;
    }
    if (boost::iequals(screen, "powers")) {
        _charGen->openPowers();
        if (selectFirst) {
            _charGen->powers().selectFirstEntryForCapture();
        }
        return;
    }
    throw std::runtime_error("Unknown character-generation screen: " + std::string(screen));
}

void Game::consoleShowBark(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "seconds message ...");
    auto duration = args.get<float>(1);
    if (!duration || *duration <= 0.0f) {
        throw std::invalid_argument("showbark duration must be positive");
    }
    if (!_hud) {
        throw std::runtime_error("HUD is unavailable; load a module first");
    }
    setBarkBubbleText(joinConsoleArgs(args, 2), *duration);
}

void Game::consoleSkipMovie(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    if (!_movie) {
        return;
    }
    // End the movie as finishing it does. Leaving the movie override set froze
    // the global fade at whatever it was when the movie started, so a skipped
    // opening left the whole session behind an opaque fade.
    _movie.reset();
    _globalFade.setMovieOverride(false);
    _timingDiscontinuity = true;
    if (!playNextModuleTransitionMovie()) {
        playNextQueuedMovie();
    }
}

void Game::consoleShowPopup(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 1024, "icon|none message ...");
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (!_confirmPopup) {
        throw std::runtime_error("Confirmation popup GUI is unavailable");
    }

    std::shared_ptr<Texture> icon;
    std::string_view iconResRef(args[1].value());
    if (!boost::iequals(iconResRef, "none")) {
        icon = _services.resource.textures.get(std::string(iconResRef), TextureUsage::GUI);
    }
    _confirmPopup->show(joinConsoleArgs(args, 2), std::move(icon));
}

void Game::consoleSeed(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    setRandomSeed(static_cast<uint32_t>(args.get<int>(1).value()));
}

void Game::consoleCosmeticSeed(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    setCosmeticRandomSeed(static_cast<uint32_t>(args.get<int>(1).value()));
}

void Game::consoleGraphics(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "on|off");
    auto mode = std::string(args[1].value());
    if (boost::iequals(mode, "on")) {
        _options.graphics.sceneRender = true;
    } else if (boost::iequals(mode, "off")) {
        _options.graphics.sceneRender = false;
    } else {
        throw std::runtime_error("Expected on or off");
    }
}

void Game::consoleOpenContainer(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    auto leader = getConsoleLeader();
    if (!leader || !_container) {
        throw std::runtime_error("Container fixture requires a loaded module");
    }
    openContainer(leader);
}

void Game::consoleShowHUD(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[combat]");
    if (!_module || !_hud) {
        throw std::runtime_error("HUD capture fixture requires a loaded module");
    }
    bool combat = args.size() > 1 && boost::iequals(std::string(args[1].value()), "combat");
    if (args.size() > 1 && !combat) {
        throw std::runtime_error("Unknown HUD capture presentation: " + std::string(args[1].value()));
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(combat);
}

void Game::consoleShowTransition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "destination ...");
    if (!_module || !_hud) {
        throw std::runtime_error("Area-transition capture fixture requires a loaded module");
    }
    _captureHUDPresentation = true;
    _cameraType = CameraType::ThirdPerson;
    openInGame();
    _hud->showCapturePresentation(false);
    _hud->showTransitionCapturePresentation(joinConsoleArgs(args, 1));
}

void Game::consoleSelectDialogOption(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "index");
    if (_screen != Screen::Conversation || _conversation != _dialog.get()) {
        throw std::runtime_error("Dialog selection fixture requires an active character conversation");
    }
    _dialog->selectReplyForCapture(args.get<int>(1).value());
}

void Game::consoleShowGalleryMode(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "swoop|pazaak wager|setup|board");

    std::string_view mode(args[1].value());
    if (boost::iequals(mode, "swoop")) {
        if (!_swoopRace.isActive()) {
            startMiniGame();
        }
        if (!_swoopRace.isActive()) {
            throw std::runtime_error("Swoop gallery fixture requires a loaded swoop minigame module");
        }
        return;
    }

    if (!boost::iequals(mode, "pazaak") || args.size() != 3) {
        throw std::runtime_error("Unknown gallery mode; expected swoop or pazaak wager|setup|board");
    }
    if (!_module) {
        throw std::runtime_error("Pazaak gallery fixture requires a loaded module");
    }

    std::string_view screen(args[2].value());
    bool showWager = boost::iequals(screen, "wager");
    bool showSetup = boost::iequals(screen, "setup");
    bool showBoard = boost::iequals(screen, "board");
    if (!showWager && !showSetup && !showBoard) {
        throw std::runtime_error("Unknown Pazaak gallery screen: " + std::string(screen));
    }

    abortPazaak();
    if (!startDevelopmentPazaak("Gallery Opponent", showWager ? 100 : 0)) {
        throw std::runtime_error("Unable to start Pazaak gallery fixture");
    }
    if (showWager || showSetup) {
        return;
    }

    for (size_t collectionIndex = 0;
         collectionIndex < _pazaakSession->collection().size() &&
         _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize;
         ++collectionIndex) {
        while (_pazaakSession->remainingCopies(collectionIndex) > 0 &&
               _pazaakSession->chosenCards().size() < pazaak::kSideDeckSize) {
            _pazaakSession->selectCard(collectionIndex);
        }
    }
    if (!_pazaakSession->confirmSetup()) {
        throw std::runtime_error("Unable to prepare Pazaak board gallery fixture");
    }
    showPazaakBoard();
}

void Game::consoleRunScript(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1024, "resref [kind:value ...]");

    std::string resRef(args[1].value());
    std::vector<script::Argument> vars;
    for (size_t i = 2; i < args.size(); ++i) {
        vars.push_back(script::Argument::fromString(std::string(args[i].value())));
    }

    int result = scriptRunner().run(resRef, vars);
    _console.printLine(str(boost::format("%s -> %d") % resRef % result));
}

void Game::consoleShowAABB(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowAABB(show);
}

void Game::consoleShowWalkmesh(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowWalkmesh(show);
}

void Game::consoleShowTriggers(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowTriggers(show);
}

void Game::consoleSpawnCreature(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 2, "res [id]");

    std::string res(args[1].value());
    std::optional<uint32_t> id = args.get<uint32_t>(2);

    auto area = getConsoleArea();
    auto leader = getConsoleLeader();

    std::shared_ptr<Creature> creature;
    if (auto explicitId = args.get<uint32_t>(2)) {
        if (getObjectById(*explicitId)) {
            throw std::runtime_error("Object already exists");
        }
        std::vector<std::shared_ptr<Object>> noObsolete;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                creature = newObjectAtId<Creature>(
                    *explicitId, false, kSceneMain, *this, _services);
                creature->loadFromBlueprint(res);
            },
            []() noexcept {});
    } else {
        creature = newCreatureFromBlueprint(res);
    }
    creature->setPosition(leader->position());
    creature->setFacing(leader->getFacing());
    creature->setFaction(Faction::Neutral);

    area->landObject(*creature);
    area->add(creature);
    creature->runSpawnScript();
}

void Game::consoleSpawnCompanion(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 3, "res npcindex [id]");

    std::string res(args[1].value());
    int npc = args.get<int>(2).value();
    std::optional<uint32_t> id = args.get<uint32_t>(3);

    auto leader = getConsoleLeader();
    auto area = getConsoleArea();

    std::shared_ptr<Creature> companion;
    if (id) {
        if (getObjectById(id.value())) {
            throw std::runtime_error("Object already exists");
        }
        std::vector<std::shared_ptr<Object>> noObsolete;
        replaceRuntimeObjectGraph(
            noObsolete,
            [&]() {
                companion = newObjectAtId<Creature>(
                    id.value(), false, kSceneMain, *this, _services);
                companion->loadFromBlueprint(res);
            },
            []() noexcept {});
    } else {
        companion = newCreatureFromBlueprint(res);
    }
    companion->setPosition(leader->position());
    companion->setFacing(leader->getFacing());
    companion->setFaction(leader->faction());

    area->landObject(*companion);
    area->add(companion);
    companion->runSpawnScript();
    _party.addAvailableMember(npc, companion);
    _party.addMember(npc, companion);
}

void Game::consoleAddAvailableNpc(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "npcindex blueprint");

    int npc = args.get<int>(1).value();
    std::string blueprint(args[2].value());

    if (!_party.addAvailableMember(npc, blueprint)) {
        throw std::runtime_error("NPC is already available: " + std::to_string(npc));
    }
}

void Game::consoleSelectObjectById(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "id");
    int id = args.get<int>(1).value();

    std::shared_ptr<Object> object = getObjectById(id);
    if (!object) {
        throw std::runtime_error("Object not found");
    }

    getConsoleArea()->selectObject(object, /*force=*/true);
}

void Game::consoleSelectObjectByTag(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "tag");
    std::string_view tag = args[1].value();

    auto area = getConsoleArea();
    for (auto &object : area->objects()) {
        if (object->tag() == tag) {
            area->selectObject(object, /*force=*/true);
            return;
        }
    }

    throw std::runtime_error("Object not found");
}

void Game::consoleSelectLeader(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    getConsoleArea()->selectObject(getConsoleLeader());
}

void Game::consoleSetFaction(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "number");
    Faction faction = args.getEnum<Faction>(1).value();
    getConsoleTargetCreature()->setFaction(faction);
}

void Game::consoleSetPosition(const ConsoleArgs &args) {
    consoleCheckUsage(args, 3, 3, "x y z");

    glm::vec3 pos(args.get<float>(1).value(),
                  args.get<float>(2).value(),
                  args.get<float>(3).value());

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    std::shared_ptr<Area> area = getConsoleArea();

    creature->setPosition(pos);
    area->determineObjectRoom(*creature);

    auto leader = party().getLeader();
    if (&*creature == &*leader) {
        area->onPartyLeaderMoved(/*roomChanged=*/true);
    }
}

void Game::consoleProfessionalTools(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::vector<std::pair<std::string, int>> items = {
        // Ranged weapons
        {"g_w_blstrcrbn001", 1},
        {"g_w_blstrpstl001", 2},
        {"g_w_blstrrfl001", 1},
        {"g_w_bowcstr001", 1},
        {"g_w_dsrptpstl001", 2},
        {"g_w_dsrptrfl001", 1},
        {"g_w_ionblstr02", 2},
        {"g_w_ionrfl01", 1},
        {"g_w_rptnblstr01", 1},
        {"g_w_sonicpstl01", 2},
        {"g_w_sonicrfl01", 1},

        // Melee weapons
        {"g_w_dblsbr001", 1},
        {"g_w_dblswrd001", 1},
        {"g_w_gaffi001", 1},
        {"g_w_lghtsbr01", 2},
        {"g_w_lngswrd01", 2},
        {"g_w_stunbaton01", 1},
        {"g_w_waraxe001", 1},

        // Grenades
        {"g_w_adhsvgren001", 10},
        {"g_w_cryobgren001", 10},
        {"g_w_firegren001", 10},
        {"g_w_flashgren001", 10},
        {"g_w_fraggren01", 10},
        {"g_w_iongren01", 10},
        {"g_w_poisngren01", 10},
        {"g_w_sonicgren01", 10},
        {"g_w_stungren01", 10},
        {"g_w_thermldet01", 10},

        // Mines
        {"g_i_trapkit001", 10},
        {"g_i_trapkit004", 10},
        {"g_i_trapkit007", 10},
        {"g_i_trapkit010", 10},

        // Consumables
        {"g_i_frarmbnds01", 10},
        {"g_i_medeqpmnt01", 10},
        {"g_i_medeqpmnt04", 10},
        {"g_i_adrnaline001", 10},
        {"g_i_adrnaline002", 10},
        {"g_i_adrnaline003", 10},
    };

    std::shared_ptr<Creature> creature = getConsoleTargetCreature();
    if (!creature) {
        return;
    }
    for (auto &kv : items) {
        creature->addItem(kv.first, kv.second);
    }
}

void Game::consoleKillRoom(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    std::shared_ptr<Creature> target = getConsoleTargetCreature();
    Room *room = target->room();
    if (!room) {
        throw std::runtime_error("No room found for the selected creature");
    }

    auto leader = party().getLeader();
    bool killEnemies = &*target == &*leader;

    SmallSet<Creature *, 16> targets;
    for (Object *object : room->tenants()) {
        Creature *creature = dyn_cast<Creature>(object);
        if (!creature || creature->isDead()) {
            continue;
        }

        if (killEnemies) {
            // Kill enemies of the leader
            if (_services.game.reputes.getIsEnemy(*target, *creature)) {
                targets.insert(creature);
            }
        } else {
            // Kill all creatures with the same faction as the selected target
            if (target->faction() == creature->faction()) {
                targets.insert(creature);
            }
        }
    }

    for (Creature *creature : targets) {
        creature->damage(std::numeric_limits<int>::max(), nullptr);
    }
}

void Game::consoleAutoSkipEnable(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    _conversationAutoSkip.enabled = args.get<int>(1).value();
}

void Game::consoleAutoSkipEntries(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "1|0 ...");

    auto &entries = _conversationAutoSkip.entries;
    entries = std::queue<bool>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        entries.push(args.get<int>(i).value());
    }
}

void Game::consoleAutoSkipReplies(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "number|? ...");

    auto &replies = _conversationAutoSkip.replies;
    replies = std::queue<std::optional<int>>();

    if (args.size() <= 1) {
        return;
    }

    for (size_t i = 1; i < args.size(); ++i) {
        int val = args.get<int>(i).value();
        if (!val) {
            replies.push(std::optional<int>());
            continue;
        }
        replies.push(val - 1);
    }
}

void Game::consoleStartConversation(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[dlg_resref]");

    auto leader = getConsoleLeader();
    _captureHUDPresentation = false;
    auto resRef = args[1];
    if (resRef) {
        startDialog(leader, std::string(resRef.value()));
        return;
    }

    auto target = getConsoleTargetObject();

    auto action = newAction<StartConversationAction>(target, "");
    leader->addAction(std::move(action));
}

void Game::consoleCutsceneAttack(const ConsoleArgs &args) {
    consoleCheckUsage(args, 4, 4, "target_id animation_id result damage");

    std::shared_ptr<Creature> actor = getConsoleTargetCreature();

    std::shared_ptr<Object> target = getObjectById(args.get<uint32_t>(1).value());
    if (!target) {
        throw std::runtime_error("Target not found");
    }

    int anim = args.get<int>(2).value();
    AttackResultType result = args.getEnum<AttackResultType>(3).value();
    int damage = args.get<int>(4).value();

    auto action = newAction<CutsceneAttackAction>(
        std::move(target), anim, result, damage);
    actor->addAction(std::move(action));
}

void Game::consoleSetAbility(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "ability value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<Ability> ability = args.getEnum<Ability>(1);
    if (!ability) {
        throw std::runtime_error("Invalid ability: must be a number");
    }
    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setAbilityScore(ability.value(), value.value());
    if (ability.value() == Ability::Constitution) {
        actor->recalculatePermanentVitality();
    }
}

void Game::consoleSetSkill(const ConsoleArgs &args) {
    consoleCheckUsage(args, 2, 2, "skill value");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SkillType> skill = args.getEnum<SkillType>(1);
    if (!skill) {
        throw std::runtime_error("Invalid skill: must be a number");
    }

    std::optional<int> value = args.get<int>(2);
    if (!value) {
        throw std::runtime_error("Invalid value");
    }
    actor->attributes().setSkillRank(skill.value(), value.value());
}

void Game::consoleAddOrRemoveFeat(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "feat");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<FeatType> feat = args.getEnum<FeatType>(1);
    if (!feat) {
        throw std::runtime_error("Invalid feat: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addfeat") {
        attrs.addFeat(feat.value());
    } else {
        attrs.removeFeat(feat.value());
    }
    actor->recalculatePermanentVitality();
}

void Game::consoleAddOrRemoveSpell(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "spell");
    std::shared_ptr<Creature> actor = getConsoleTargetCreature();
    std::optional<SpellType> spell = args.getEnum<SpellType>(1);
    if (!spell) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    CreatureAttributes &attrs = actor->attributes();
    if (args[0].value() == "addspell") {
        attrs.addSpell(spell.value());
    } else {
        attrs.removeSpell(spell.value());
    }
}

void Game::consoleCastSpellAtObject(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 3, "spell ischeat item");

    auto leader = getConsoleLeader();
    auto target = getConsoleTargetObject();

    std::optional<SpellType> spellType = args.getEnum<SpellType>(1);
    if (!spellType) {
        throw std::runtime_error("Invalid spell: must be a number");
    }

    std::shared_ptr<Spell> spell = _services.game.spells.get(spellType.value());
    if (!spell) {
        throw std::runtime_error("Unknown spell");
    }

    bool cheat = args.get<int>(2).value_or(false);
    std::optional<std::string_view> spellItem = args[3];
    std::optional<std::shared_ptr<Item>> item;
    if (spellItem) {
        for (const std::shared_ptr<Item> &inventoryItem : leader->items()) {
            if (inventoryItem->tag() == spellItem.value()) {
                item = inventoryItem;
                break;
            }
        }
        if (!cheat && !item) {
            throw std::runtime_error("Item is not in the inventory");
        }
    }

    auto action = newAction<CastSpellAtObjectAction>(
        spell, std::move(target), std::move(item), cheat);

    leader->addAction(std::move(action));
}

void Game::consoleOpenCloseDoor(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1, "[triggerer_id]");

    auto target = dyn_cast<Door>(getConsoleTargetObject());
    if (!target) {
        throw std::runtime_error("Selected object must be a door");
    }

    auto triggerer_id = args.get<uint32_t>(1);
    std::shared_ptr<Object> triggerer;
    if (triggerer_id) {
        if (uint32_t id = triggerer_id.value()) {
            triggerer = getObjectById(id);
        }
    } else {
        triggerer = getConsoleLeader();
    }

    if (args[0].value() == "opendoor") {
        target->open();
        if (triggerer) {
            target->onOpen(triggerer->id());
        }
    } else {
        target->close();
        // There is no Door::onClose yet
    }
}

void Game::consoleListGames(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");

    // Indices must address the same list consoleLoadGame indexes.
    std::stringstream ss;
    unsigned index = 0;
    const char *newline = "";
    for (const auto &save : savedGames()) {
        ss << newline << "[" << index++ << "] "
           << save.descriptor.directory.filename().string();
        newline = "\n";
    }
    _console.printLine(ss.str());
}

void Game::consoleLoadGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "save_id");
    size_t id = *args.get<size_t>(1);
    auto saves = savedGames();
    if (id >= saves.size()) {
        throw std::runtime_error("Invalid savegame id");
    }
    loadGame(saves[id].descriptor);
}

void Game::consoleSaveGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, std::numeric_limits<size_t>::max(), "slot [name]");
    auto slot = args.get<uint32_t>(1);
    if (!slot) {
        throw std::runtime_error("Invalid save slot");
    }
    std::string name;
    for (size_t i = 2; i < args.size(); ++i) {
        if (!name.empty()) {
            name += " ";
        }
        name += std::string(*args[i]);
    }
    auto result = requestSave(
        {SaveKind::Developer, *slot, std::move(name), true});
    _console.printLine(result.message);
}

void Game::consoleStartPazaak(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 0, "");
    if (!_options.game.developer) {
        _console.printLine("pazaak: developer mode required");
        return;
    }
    if (_pazaakSession) {
        _console.printLine("pazaak: already active");
        return;
    }
    if (!_module || _screen != Screen::InGame) {
        _console.printLine("pazaak: no active in-game module");
        return;
    }

    std::shared_ptr<Object> selected(
        _pazaakDevelopmentSelectedObjectOverride.resolve());
    if (!selected) {
        if (auto area = _module->area()) {
            selected = area->selectedObject();
        }
    }

    std::string opponentName("Pazaak Opponent");
    if (selected && selected->type() == ObjectType::Creature) {
        if (!selected->name().empty()) {
            opponentName = selected->name();
        } else if (!selected->tag().empty()) {
            opponentName = selected->tag();
        }
    }

    if (!startDevelopmentPazaak(opponentName)) {
        _console.printLine("pazaak: development launch failed");
        return;
    }
    _console.printLine("pazaak: development match started against " + opponentName);
}

void Game::consoleMiniGameInfo(const ConsoleArgs &args) {
    auto area = getConsoleArea();
    if (!area->hasMinigame()) {
        _console.printLine("minigame: none");
        return;
    }
    const auto &mg = area->miniGame();
    _console.printLine(str(boost::format("minigame: type=%s track=%s models=%zu enemies=%zu obstacles=%zu movePerSec=%.1f lateralAccel=%.1f inertia=%d fov=%.1f")
                           % minigameTypeName(mg.type) % mg.player.trackResRef % mg.player.models.size()
                           % mg.enemies.size() % mg.obstacles.size() % mg.movementPerSec % mg.lateralAccel
                           % static_cast<int>(mg.useInertia) % mg.cameraViewAngle));
    for (size_t slot = 0; slot < kMiniGameSlotCount; ++slot) {
        const auto &script = mg.player.slots[slot];
        if (!script.empty()) {
            _console.printLine(str(boost::format("  player %s=%s") % miniGameSlotName(static_cast<MiniGameSlot>(slot)) % script));
        }
    }
    _console.printLine(str(boost::format("  running=%d") % static_cast<int>(_miniGame != nullptr)));
}

void Game::consoleShowImGui(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    _showImGui = show;
}

void Game::consoleNewGame(const ConsoleArgs &args) {
    consoleCheckUsage(args, 0, 1024, "[class slot 0-5] [name ...]");
    // Slot 2 is the male Soldier in KotOR and the male Jedi Guardian in TSL.
    int slot = 2;
    if (args.size() > 1) {
        auto parsed = args.get<int>(1);
        if (!parsed) {
            throw std::runtime_error("Usage: newgame [class slot 0-5] [name ...]");
        }
        slot = *parsed;
    }
    std::string name = args.size() > 2 ? joinConsoleArgs(args, 2) : std::string();
    startCharacterGeneration();
    if (!_charGen) {
        throw std::runtime_error("Character generation GUI is unavailable");
    }
    _charGen->finishQuickForAutomation(slot, std::move(name));
}

void Game::consoleShowPath(const ConsoleArgs &args) {
    consoleCheckUsage(args, 1, 1, "1|0");
    bool show = args.get<int>(1).value();
    setShowPath(show);
}

bool Game::menuThemesAvailable() const {
    return _gameId == resource::GameID::KotOR && hudPresentationId() == resource::GameID::TSL;
}

MenuThemeMode Game::menuThemeMode() const {
    auto value = _userSettings.get(kMenuThemeKey);
    auto mode = value ? parseMenuThemeMode(*value) : std::nullopt;
    return mode.value_or(MenuThemeMode::Rotate);
}

std::unique_ptr<MainMenu> Game::newThemedMainMenu(bool advanceTheme) {
    auto last = _userSettings.get(kMenuLastThemeKey);
    auto lastTheme = last ? parseMenuTheme(*last) : std::nullopt;
    auto theme = nextMenuTheme(menuThemeMode(), menuThemesAvailable(), lastTheme);
    if (!advanceTheme && lastTheme && nextMenuTheme(menuThemeMode(), menuThemesAvailable(), theme) == *lastTheme) {
        // Only the character rotates: keep the theme shown now.
        theme = *lastTheme;
    }
    _userSettings.set(kMenuLastThemeKey, menuThemeName(theme));
    _userSettings.save();
    auto menu = std::make_unique<MainMenu>(*this, _services, theme);
    try {
        menu->init();
    } catch (const std::exception &e) {
        error(str(boost::format("Error loading GUI: %s") % std::string(e.what())));
        return nullptr;
    }
    return menu;
}

void Game::switchMainMenuTheme() {
    // Idle: the next character every switch, the next theme every few.
    bool advanceTheme = ++_menuIdleSwitches % MenuThemeFade::kSwitchesPerTheme == 0;
    auto previousMusic = _mainMenu ? _mainMenu->musicResRef() : std::string();
    auto menu = newThemedMainMenu(advanceTheme);
    if (!menu) {
        return;
    }
    menu->beginThemeFadeIn();
    _mainMenu = std::move(menu);
    if (_mainMenu->musicResRef() != previousMusic) {
        playMusic(_mainMenu->musicResRef(), _mainMenu->musicClips());
    }
}

resource::GameID Game::hudPresentationId() const {
    switch (_options.game.hud) {
    case HudProfile::K1:
        // A K1 HUD in a K2 session has no resource set; the engine rejects it.
        return _gameId;
    case HudProfile::K2:
        if (_gameId == resource::GameID::TSL || _services.gui.k2Guis) {
            return resource::GameID::TSL;
        }
        return _gameId;
    case HudProfile::Default:
    default:
        return _gameId;
    }
}

HudPresentationServices Game::hudPresentation() const {
    auto id = hudPresentationId();
    bool k2Set = id == resource::GameID::TSL && _gameId != resource::GameID::TSL &&
                 _services.gui.k2Guis && _services.gui.k2Resources;
    if (k2Set) {
        return {id, *_services.gui.k2Guis, *_services.gui.k2Resources};
    }
    return {id, _services.gui.guis, _services.resource};
}

} // namespace game

} // namespace reone

namespace reone {

namespace game {

// Party death (design note p1_game "Death screen").
static constexpr int kStrRefPartyKilled = 42351;
static constexpr int kStrRefNoSavedGames = 42491;
static constexpr int kStrRefMainMenu = 49466;
static constexpr int kStrRefLoadLastSave = 49467;
static constexpr int kStrRefLoadGameList = 49468;

static std::string textOr(resource::IStrings &strings, int strRef, const char *fallback) {
    std::string text = strings.getText(strRef);
    return text.empty() ? std::string(fallback) : text;
}

void Game::showDeathScreen() {
    if (!_confirmPopup) {
        _confirmPopup = tryLoadGUI<ConfirmPopup>();
    }
    if (!_confirmPopup) {
        warn("Game: no popup for the death screen; returning to the main menu");
        openMainMenu();
        return;
    }
    auto &strings = _services.resource.strings;
    // K1's line adds "Return to Main Menu." on a second line; the buttons
    // say what happens now.
    std::string message = textOr(strings, kStrRefPartyKilled, "Your entire party has been killed.");
    if (auto newline = message.find('\n'); newline != std::string::npos) {
        message.resize(newline);
    }
    setRelativeMouseMode(false);
    setCursorType(CursorType::Default);
    std::vector<ConfirmPopup::Choice> choices;
    // Party wipe (co-op design notes): the area checkpoint first.
    if (canRetryArea()) {
        choices.push_back({"Retry this area", [this]() { chooseDefeatOption("retry"); }});
    }
    choices.push_back({textOr(strings, kStrRefLoadLastSave, "Load Last Saved Game"), [this]() { chooseDefeatOption("last"); }});
    choices.push_back({textOr(strings, kStrRefLoadGameList, "Go to Load Game List"), [this]() { chooseDefeatOption("load"); }});
    choices.push_back({textOr(strings, kStrRefMainMenu, "Main Menu"), [this]() { chooseDefeatOption("menu"); }});
    _confirmPopup->showChoices(message, std::move(choices));
}

namespace {

const SavedGame *areaCheckpoint(const std::vector<SavedGame> &saves) {
    for (const auto &save : saves) {
        if (save.slot == kAreaCheckpointSlot) {
            return &save;
        }
    }
    return nullptr;
}

} // namespace

bool Game::canRetryArea() const {
    if (!_module) {
        return false;
    }
    auto saves = discoverSavedGames(_path);
    return areaCheckpoint(saves) != nullptr;
}

bool Game::retryArea() {
    if (!_module) {
        return false;
    }
    auto saves = discoverSavedGames(_path);
    auto save = areaCheckpoint(saves);
    if (!save) {
        return false;
    }
    info("Game: retrying from the area checkpoint (" + save->metadata.lastModule + ")");
    return loadGame(save->descriptor);
}

bool Game::chooseDefeatOption(const std::string &choice) {
    if (_confirmPopup && _confirmPopup->isVisible()) {
        _confirmPopup->hide();
    }
    auto &strings = _services.resource.strings;
    if (choice == "retry" || choice == "last") {
        bool loaded = choice == "retry" ? retryArea() : loadLastSave();
        if (!loaded && _confirmPopup) {
            _confirmPopup->showChoices(
                choice == "retry" ? std::string("There is no area checkpoint yet.")
                                  : textOr(strings, kStrRefNoSavedGames, "You have no Saved Games."),
                {{"OK", [this]() { showDeathScreen(); }}});
        }
        return loaded;
    }
    if (choice == "load") {
        if (_coop && _coop->isHost()) {
            // Keep the session: the load list over the game, and the
            // clients follow whatever loads.
            openSaveLoad(SaveLoadMode::LoadFromInGame);
            return true;
        }
        openMainMenu();
        if (_saveLoad) {
            openSaveLoad(SaveLoadMode::LoadFromMainMenu);
        }
        return true;
    }
    if (choice == "menu") {
        endCoopSession();
        openMainMenu();
        return true;
    }
    return false;
}

bool Game::loadLastSave() {
    auto saves = discoverSavedGames(_path);
    const SavedGame *latest = nullptr;
    std::filesystem::file_time_type latestTime;
    for (const auto &save : saves) {
        std::error_code ec;
        auto time = std::filesystem::last_write_time(save.descriptor.directory, ec);
        if (ec) {
            continue;
        }
        if (save.slot == kAreaCheckpointSlot) {
            continue; // "Load last save" is the player's saves; Retry is the checkpoint
        }
        if (!latest || time > latestTime || (time == latestTime && save.slot < latest->slot)) {
            latest = &save;
            latestTime = time;
        }
    }
    if (!latest) {
        return false;
    }
    info("Game: loading the last save, slot " + std::to_string(latest->slot));
    return loadGame(latest->descriptor);
}

// Autosave on module entry (modulesave.2da; design note c11_data "Autosave").
static constexpr auto kAutosaveInterval = std::chrono::minutes(15);

AutosaveOnEnter parseAutosaveOnEnter(const std::optional<std::string> &cell) {
    if (!cell) {
        return AutosaveOnEnter::Missing;
    }
    std::string value = boost::to_lower_copy(*cell);
    if (value == "force") {
        return AutosaveOnEnter::Force;
    }
    if (value == "no") {
        return AutosaveOnEnter::No;
    }
    if (value == "yes") {
        return AutosaveOnEnter::Yes;
    }
    return AutosaveOnEnter::Missing;
}

bool shouldAutosaveOnEnter(AutosaveOnEnter mode, bool autosaveOption, bool intervalElapsed) {
    switch (mode) {
    case AutosaveOnEnter::Force:
        return true;
    case AutosaveOnEnter::No:
        return false;
    case AutosaveOnEnter::Yes:
        return autosaveOption && intervalElapsed;
    case AutosaveOnEnter::Missing:
    default:
        return intervalElapsed;
    }
}

void Game::queueAutosaveOnEnter(const std::string &moduleName) {
    auto table = _services.resource.twoDas.get("modulesave");
    std::optional<std::string> cell;
    if (table) {
        int row = table->indexByLabel(boost::to_lower_copy(moduleName));
        if (row != -1) {
            cell = table->getStringOpt(row, "autosaveonenter");
        }
    }
    auto now = std::chrono::steady_clock::now();
    if (!_lastSaveClock) {
        // The interval counts from the start of the session.
        _lastSaveClock = now;
    }
    bool elapsed = now - *_lastSaveClock >= kAutosaveInterval;
    // The AutoSave game option defaults to on; there is no option screen
    // toggle for it yet.
    _autosaveOnEnterPending = shouldAutosaveOnEnter(parseAutosaveOnEnter(cell), true, elapsed);
    if (_autosaveOnEnterPending) {
        info("Game: autosave queued on entering '" + moduleName + "'");
    }
}

std::vector<std::string> modulesDeletedOnEnter(const resource::TwoDA &modulesave, const std::string &moduleName) {
    std::vector<std::string> result;
    std::string entered = boost::to_lower_copy(moduleName);
    int row = modulesave.indexByLabel(entered);
    if (row == -1) {
        return result;
    }
    auto group = modulesave.getStringOpt(row, "deletesavegrouponenter");
    if (!group || group->empty()) {
        return result;
    }
    for (int i = 0; i < modulesave.getRowCount(); ++i) {
        auto savegroup = modulesave.getStringOpt(i, "savegroup");
        if (!savegroup || *savegroup != *group) {
            continue;
        }
        std::string name = boost::to_lower_copy(
            modulesave.getString(i, "modulename", modulesave.rows()[i].label));
        if (!name.empty() && name != entered) {
            result.push_back(std::move(name));
        }
    }
    return result;
}

std::string loadScreenImage(const resource::TwoDA *loadscreens, const std::string &moduleName) {
    std::string name = boost::to_lower_copy(moduleName);
    if (loadscreens) {
        int row = loadscreens->indexByLabel(name);
        if (row != -1) {
            std::string image = boost::to_lower_copy(loadscreens->getString(row, "bmpresref"));
            if (!image.empty()) {
                return image;
            }
        }
    }
    return "load_" + name;
}

int loadScreenHint(const resource::TwoDA &hints, int row) {
    if (row < 0 || row >= hints.getRowCount()) {
        return -1;
    }
    return hints.getInt(row, "gameplayhint", -1);
}

void Game::deleteSaveGroupOnEnter(const std::string &moduleName) {
    auto table = _services.resource.twoDas.get("modulesave");
    if (!table) {
        return;
    }
    auto modules = modulesDeletedOnEnter(*table, moduleName);
    auto committed = _services.resource.director.committedSaveWorkingState();
    if (modules.empty() || !committed) {
        return;
    }
    auto candidate = resource::SaveWorkingStateCandidate::fromCommitted(committed);
    bool changed = false;
    for (const auto &name : modules) {
        for (auto type : {ResType::Sav, ResType::Rsv}) {
            ResourceId id(name, type);
            if (candidate.contains(id)) {
                candidate.erase(id);
                changed = true;
            }
        }
    }
    if (changed) {
        info("Game: dropped the cached states of save group entered from '" + moduleName + "'");
        _services.resource.director.adoptSaveWorkingState(candidate.freeze());
    }
}

void Game::processAutosaveOnEnter() {
    if (!_autosaveOnEnterPending) {
        return;
    }
    if (saveEligibility(false) != SaveEligibilityReason::None || _nextModule.size() > 0) {
        return;
    }
    _autosaveOnEnterPending = false;
    auto result = requestAutoSave();
    if (result.status == SaveStatus::Accepted) {
        _lastSaveClock = std::chrono::steady_clock::now();
    }
}

void Game::processCheckpointOnEnter() {
    // After the retail autosave (one save request at a time), never instead
    // of it. It does not count as a save for the autosave interval.
    if (!_checkpointOnEnterPending || _autosaveOnEnterPending || _pendingSave || _saveInProgress) {
        return;
    }
    if (saveEligibility(false) != SaveEligibilityReason::None || _nextModule.size() > 0) {
        return;
    }
    _checkpointOnEnterPending = false;
    requestSave({SaveKind::Checkpoint, kAreaCheckpointSlot, "Area checkpoint", false});
}

} // namespace game

} // namespace reone
