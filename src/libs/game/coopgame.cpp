/* Copyright (c) 2026 The reone project contributors */

// Game's co-op seams (design note coop_net): the client tick, the harness
// commands, and starting a session from the command line.

#include "reone/game/game.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>

#include <boost/algorithm/string.hpp>
#include <boost/json.hpp>

#include "reone/game/coopsession.h"
#include "reone/resource/director.h"
#include "reone/game/gui/chargen.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/gui/ingame/options.h"
#include "reone/game/gui/mainmenu.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/runner.h"
#include "reone/net/bytes.h"
#include "reone/net/fingerprint.h"
#include "reone/net/joincode.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"

namespace reone {

namespace game {

void Game::setCoopSession(std::shared_ptr<CoopSession> session) {
    _coop = std::move(session);
    restoreCoopLootFromSave();
}

void Game::restoreCoopLootFromSave() {
    if (!_coop || !_coop->isHost()) {
        return;
    }
    auto source = _services.resource.director.saveSlotDescriptor();
    if (!source) {
        return;
    }
    // A save from before the ledger existed has no such file: an empty ledger.
    auto path = source->directory / CoopLootLedger::kSaveFileName;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return;
    }
    std::string text;
    try {
        std::ifstream file(path, std::ios::binary);
        text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } catch (const std::exception &e) {
        warn("Co-op: cannot read the saved loot ledger: " + std::string(e.what()));
        return;
    }
    std::string error;
    if (!_coop->lootLedger().deserialize(text, error)) {
        warn("Co-op: ignoring the saved loot ledger: " + error);
    }
}

void Game::setCoopRecordPath(const std::filesystem::path &path) {
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    auto file = std::make_shared<std::ofstream>(path, std::ios::app);
    if (!*file) {
        warn("Co-op: cannot write the recorder file " + path.string());
        return;
    }
    _coopRecord = std::move(file);
    info("Co-op: recording state to " + path.string());
}

void Game::recordCoopState(float frameTime) {
    _coopRecordClock += frameTime;
    if (_coopRecordClock < _coopRecordNext) {
        return;
    }
    _coopRecordNext = _coopRecordClock + 1.0;
    boost::json::object line;
    line["t"] = std::round(_coopRecordClock * 10.0) / 10.0;
    line["frame"] = _frameCount;
    line["tick"] = _tickCount;
    line["screen"] = static_cast<int>(_screen);
    line["paused"] = _paused;
    line["module"] = _module ? boost::json::value(_module->name()) : boost::json::value(nullptr);
    boost::json::array party;
    for (const auto &member : _party.members()) {
        if (!member.creature) {
            continue;
        }
        const auto &p = member.creature->position();
        party.push_back(boost::json::object {{"tag", member.creature->tag()},
                                             {"pos", boost::json::array {std::round(p.x * 100) / 100, std::round(p.y * 100) / 100, std::round(p.z * 100) / 100}},
                                             {"hp", member.creature->currentHitPoints()},
                                             {"leader", member.creature == _party.getLeader()}});
    }
    line["party"] = std::move(party);
    if (_coop) {
        auto status = _coop->statusJson();
        status.erase("route");
        if (auto clients = status.if_contains("clients"); clients && clients->is_array()) {
            for (auto &client : clients->as_array()) {
                if (client.is_object()) {
                    client.as_object().erase("route");
                }
            }
        }
        line["coop"] = std::move(status);
    }
    *_coopRecord << boost::json::serialize(line) << '\n';
    _coopRecord->flush();
}

void Game::setCoopDefaults(const CoopLaunch &launch) {
    _coopDefaults = std::make_shared<CoopLaunch>(launch);
    _coopDefaults->role = CoopRole::None;
}

const CoopLaunch &Game::coopDefaults() const {
    static const CoopLaunch none;
    return _coopDefaults ? *_coopDefaults : none;
}

bool Game::startCoopHost(std::string &error, const std::string &mode) {
    if (_coop) {
        error = "a co-op session is already running";
        return false;
    }
    CoopLaunch launch = coopDefaults();
    launch.role = CoopRole::Host;
    if (!mode.empty()) {
        launch.mode = mode; // the Co-op screen's choice: Story Mode or Free Mode
    }
    if (launch.service.empty() && launch.port == 0) {
        launch.port = 27015; // no join-code service configured: direct ip:port only
    }
    return startCoopSession(*this, launch, error) != nullptr;
}

bool Game::startCoopJoin(const std::string &codeOrAddress, std::string &error) {
    if (_coop) {
        error = "a co-op session is already running";
        return false;
    }
    CoopLaunch launch = coopDefaults();
    launch.role = CoopRole::Client;
    launch.port = 0;
    launch.mode.clear(); // a joiner from the Co-op screen plays the host's mode
    if (net::looksLikeAddress(codeOrAddress)) {
        launch.address = codeOrAddress;
    } else {
        launch.code = net::normalizeJoinCode(codeOrAddress);
        if (launch.code.empty()) {
            error = "That is not a join code. Codes have 8 letters and digits, like K7QM-4TXR.";
            return false;
        }
        if (launch.service.empty()) {
            error = "No co-op service is set up (coop-server in reone.cfg), so join codes cannot be used.";
            return false;
        }
    }
    return startCoopSession(*this, launch, error) != nullptr;
}

void Game::endCoopSession() {
    if (!_coop) {
        return;
    }
    bool client = _coop->isClient();
    _coop.reset();
    info("Co-op: session closed");
    if (client && _module) {
        // The world was the host's: nothing of it is ours to keep playing.
        openMainMenu();
    }
}

void Game::tickCoopClient(float dt) {
    // No scripts, no actions, no combat, no dice: the host decides. What is
    // left is what a viewer needs: screens, module loads the host started,
    // the replicated state for this tick, and model animation.
    updateCameraType();
    processPendingScreens();
    if (!_nextModule.empty()) {
        loadNextModule();
    }
    {
        // Presentation layers the client runs itself: the conversation's own
        // fade layer (the host's global fade arrives in the snapshots).
        GameplayRandomForbidden fade;
        _dialogFade.update(dt);
    }
    _coop->clientTick(dt);
    if (!_movie && getActiveCamera()) {
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        // The host's tactical pause freezes the scene here too.
        sceneGraph.setUpdateRoots(!_paused);
        sceneGraph.updateModelRoots(dt);
    }
}

namespace {

std::string compact(const boost::json::value &value) {
    return boost::json::serialize(value);
}

float parseFloat(const ConsoleArgs &args, size_t index, const char *name) {
    auto value = args.get<float>(index);
    if (!value) {
        throw std::runtime_error(std::string("expected a number for ") + name);
    }
    return *value;
}

} // namespace

void Game::initCoopConsole() {
    _console.registerCommand("netstatus", "print the co-op session state as one line of JSON", [this](const ConsoleArgs &) {
        if (!_coop) {
            _console.printLine("{\"role\":\"none\"}");
            return;
        }
        _console.printLine(compact(_coop->statusJson()));
    });
    // The Co-op screen's buttons, for the harness (a headless engine has no screen to click).
    _console.registerCommand("coophost", "co-op: host from the menus, as the Co-op screen's Host does (a join code with coop-server)", [this](const ConsoleArgs &) {
        std::string error;
        if (!startCoopHost(error)) {
            throw std::runtime_error(error);
        }
        _console.printLine("coop hosting");
    });
    _console.registerCommand("coopjoin", "co-op: join by code or ip:port, as the Co-op screen's Connect does: coopjoin <code|ip:port>", [this](const ConsoleArgs &args) {
        auto target = args[1];
        if (!target) {
            throw std::runtime_error("Usage: coopjoin <code|ip:port>");
        }
        std::string error;
        if (!startCoopJoin(std::string(*target), error)) {
            throw std::runtime_error(error);
        }
        _console.printLine("coop joining");
    });
    _console.registerCommand("coopleave", "co-op: leave the session", [this](const ConsoleArgs &) {
        endCoopSession();
        _console.printLine("coop left");
    });
    _console.registerCommand("coopdrop", "co-op client test: cut the connection without a goodbye, as a network drop does", [this](const ConsoleArgs &) {
        if (!_coop || !_coop->isClient()) {
            throw std::runtime_error("not a co-op client");
        }
        _coop->simulateDrop();
        _console.printLine("coop dropped");
    });
    _console.registerCommand("coopscreen", "co-op: what the Co-op screen shows, as one line of JSON", [this](const ConsoleArgs &) {
        CoopMenu *menu = _mainMenu ? _mainMenu->coopMenu() : nullptr;
        if ((!menu || !menu->isOpen()) && _inGame && _inGame->optionsMenu()) {
            menu = _inGame->optionsMenu()->coopMenu();
        }
        boost::json::object screen;
        screen["open"] = menu && menu->isOpen();
        screen["text"] = menu ? menu->statusText() : std::string();
        _console.printLine(compact(screen));
    });
    _console.registerCommand("checkpoint", "write the area checkpoint now (area entry does this)", [this](const ConsoleArgs &) {
        requestAreaCheckpoint();
        _console.printLine("checkpoint requested");
    });
    _console.registerCommand("defeatchoice", "party wipe: pick a death-screen choice: defeatchoice <retry|last|load|menu>", [this](const ConsoleArgs &args) {
        auto choice = args[1];
        if (!choice) {
            throw std::runtime_error("Usage: defeatchoice <retry|last|load|menu>");
        }
        std::string name(*choice);
        if (name != "retry" && name != "last" && name != "load" && name != "menu") {
            throw std::runtime_error("defeatchoice: retry, last, load or menu");
        }
        bool done = chooseDefeatOption(name);
        _console.printLine("defeatchoice " + name + (done ? " ok" : " unavailable") +
                           (canRetryArea() ? " checkpoint=1" : " checkpoint=0"));
    });
    _console.registerCommand("coopcode", "co-op host: print the join code as one line of JSON", [this](const ConsoleArgs &) {
        if (!_coop) {
            throw std::runtime_error("no co-op session");
        }
        auto online = _coop->onlineStatus();
        boost::json::object code;
        code["online"] = online.online;
        code["state"] = online.state;
        code["code"] = online.code.empty() ? std::string() : net::displayJoinCode(online.code);
        code["error"] = online.error;
        _console.printLine(compact(code));
    });
    _console.registerCommand("coopreissue", "co-op host: a new join code (the old one stops working)", [this](const ConsoleArgs &) {
        if (!_coop || !_coop->isHost()) {
            throw std::runtime_error("not a co-op host");
        }
        _coop->reissueCode();
        _console.printLine("coop reissue requested");
    });
    _console.registerCommand("coopapprove", "co-op host: answer a join request: coopapprove <client id> [1|0]", [this](const ConsoleArgs &args) {
        if (!_coop || !_coop->isHost()) {
            throw std::runtime_error("not a co-op host");
        }
        auto id = args.get<int>(1);
        if (!id) {
            throw std::runtime_error("Usage: coopapprove <client id> [1|0]");
        }
        bool accept = args.size() < 3 || args.get<int>(2).value_or(1) != 0;
        if (!_coop->answerJoin(static_cast<uint32_t>(*id), accept)) {
            throw std::runtime_error("no pending join request from client " + std::to_string(*id));
        }
        _console.printLine(std::string("coop join ") + (accept ? "accepted" : "declined"));
    });
    _console.registerCommand("coopdump", "co-op client: print the replicated objects as one line of JSON", [this](const ConsoleArgs &) {
        if (!_coop) {
            throw std::runtime_error("no co-op session");
        }
        _console.printLine(compact(_coop->dumpJson()));
    });

    auto clientSession = [this]() -> CoopSession & {
        if (!_coop || !_coop->isClient()) {
            throw std::runtime_error("not a co-op client");
        }
        return *_coop;
    };
    auto submit = [this, clientSession](net::Command command, const std::string &what) {
        auto &session = clientSession();
        if (!session.controlledHostId()) {
            throw std::runtime_error("no companion is assigned yet");
        }
        uint32_t seq = session.submitCommand(command);
        if (!seq) {
            throw std::runtime_error("not connected to a host");
        }
        _console.printLine("coop " + what + " seq=" + std::to_string(seq) + " actor=" + std::to_string(session.controlledHostId()) +
                           " target=" + std::to_string(command.target));
    };
    auto target = [clientSession](const ConsoleArgs &args, size_t index) -> uint32_t {
        auto arg = args[index];
        if (!arg) {
            throw std::runtime_error("expected a target: a host id or a tag");
        }
        uint32_t id = clientSession().resolveHostId(std::string(*arg));
        if (!id) {
            throw std::runtime_error("no replicated object " + std::string(*arg));
        }
        return id;
    };

    _console.registerCommand("coopmove", "co-op client: walk the controlled creature to a point: coopmove x y [run=1]", [=](const ConsoleArgs &args) {
        if (args.size() < 3 || args.size() > 4) {
            throw std::runtime_error("Usage: coopmove x y [run=1]");
        }
        net::Command command;
        command.type = net::CommandType::MoveTo;
        command.point = {parseFloat(args, 1, "x"), parseFloat(args, 2, "y"), 0.0f};
        command.run = args.size() < 4 || args[3].value() != "0";
        if (auto actor = clientSession().localObjectForHost(clientSession().controlledHostId())) {
            command.point.z = actor->position().z;
        }
        submit(command, "move_to");
    });
    _console.registerCommand("coopattack", "co-op client: attack a creature: coopattack <host id|tag>", [=](const ConsoleArgs &args) {
        net::Command command;
        command.type = net::CommandType::Attack;
        command.target = target(args, 1);
        submit(command, "attack");
    });
    _console.registerCommand("coopuse", "co-op client: use an object as a click does: coopuse <host id|tag>", [=](const ConsoleArgs &args) {
        net::Command command;
        command.type = net::CommandType::UseObject;
        command.target = target(args, 1);
        submit(command, "use_object");
    });
    _console.registerCommand("coopdoor", "co-op client: open a door: coopdoor <host id|tag>", [=](const ConsoleArgs &args) {
        net::Command command;
        command.type = net::CommandType::OpenDoor;
        command.target = target(args, 1);
        submit(command, "open_door");
    });
    _console.registerCommand("coopcancel", "co-op client: clear the controlled creature's actions", [=](const ConsoleArgs &) {
        net::Command command;
        command.type = net::CommandType::Cancel;
        submit(command, "cancel");
    });
    _console.registerCommand("coopitem", "co-op client: use an item: coopitem <item tag|template> [host id|tag]", [=](const ConsoleArgs &args) {
        if (args.size() < 2) {
            throw std::runtime_error("Usage: coopitem <item> [target]");
        }
        net::Command command;
        command.type = net::CommandType::UseItem;
        command.item = std::string(*args[1]).substr(0, 32);
        command.target = args.size() > 2 ? target(args, 2) : 0;
        submit(command, "use_item");
    });
    _console.registerCommand("coopfeat", "co-op client: use a feat: coopfeat <feat id> <host id|tag>", [=](const ConsoleArgs &args) {
        auto feat = args.get<int>(1);
        if (!feat || args.size() != 3) {
            throw std::runtime_error("Usage: coopfeat <feat id> <target>");
        }
        net::Command command;
        command.type = net::CommandType::UseFeat;
        command.param = *feat;
        command.target = target(args, 2);
        submit(command, "use_feat");
    });
    _console.registerCommand("cooppower", "co-op client: use a Force power: cooppower <spell id> [host id|tag]", [=](const ConsoleArgs &args) {
        auto spell = args.get<int>(1);
        if (!spell) {
            throw std::runtime_error("Usage: cooppower <spell id> [target]");
        }
        net::Command command;
        command.type = net::CommandType::UsePower;
        command.param = *spell;
        command.target = args.size() > 2 ? target(args, 2) : 0;
        submit(command, "use_power");
    });
    _console.registerCommand("coopskill", "co-op client: use a skill on an object: coopskill <security|computeruse|repair|demolitions> <host id|tag>", [=](const ConsoleArgs &args) {
        static const std::map<std::string, int> kSkills {
            {"computeruse", 0}, {"demolitions", 1}, {"repair", 5}, {"security", 6}};
        auto name = args[1];
        auto skill = name ? kSkills.find(boost::to_lower_copy(std::string(*name))) : kSkills.end();
        if (skill == kSkills.end() || args.size() != 3) {
            throw std::runtime_error("Usage: coopskill <security|computeruse|repair|demolitions> <target>");
        }
        net::Command command;
        command.type = net::CommandType::UseSkill;
        command.param = skill->second;
        command.target = target(args, 2);
        submit(command, "use_skill");
    });
    auto anySession = [this]() -> CoopSession & {
        if (!_coop) {
            throw std::runtime_error("no co-op session");
        }
        return *_coop;
    };
    _console.registerCommand("coopready", "co-op: answer the area-exit ready check: coopready [1|0]", [=](const ConsoleArgs &args) {
        bool ready = args.size() < 2 || args[1].value() != "0";
        if (!anySession().answerReady(ready)) {
            throw std::runtime_error("no ready check is running");
        }
        _console.printLine(std::string("coop ready=") + (ready ? "1" : "0"));
    });
    _console.registerCommand("coopforce", "co-op host: move the party now, whoever is ready", [=](const ConsoleArgs &) {
        if (!anySession().forceTransition()) {
            throw std::runtime_error("no ready check is running");
        }
        _console.printLine("coop forced");
    });
    _console.registerCommand("cooppause", "co-op: pause or resume for everyone: cooppause [1|0]", [=](const ConsoleArgs &args) {
        auto &session = anySession();
        bool pause = args.size() < 2 || args[1].value() != "0";
        if (session.isClient()) {
            uint32_t seq = session.submitRequest(net::CommandType::Pause, 0, pause ? 1 : 0);
            if (!seq) {
                throw std::runtime_error("not connected to a host");
            }
            _console.printLine("coop pause seq=" + std::to_string(seq));
            return;
        }
        if (pause != isPaused() && !session.handlePauseKey()) {
            setPaused(pause);
        }
        _console.printLine(std::string("coop paused=") + (isPaused() ? "1" : "0"));
    });
    _console.registerCommand("cooptake", "co-op client: take from the open container: cooptake [all|<host item id>]", [=](const ConsoleArgs &args) {
        auto &session = clientSession();
        auto container = session.statusJson().at("viewing").as_uint64();
        if (!container) {
            throw std::runtime_error("no container is open");
        }
        int32_t item = 0;
        if (args.size() > 1 && args[1].value() != "all") {
            auto id = args.get<int>(1);
            if (!id) {
                throw std::runtime_error("Usage: cooptake [all|<host item id>]");
            }
            item = *id;
        }
        uint32_t seq = session.submitRequest(net::CommandType::TakeItem, static_cast<uint32_t>(container), item);
        _console.printLine("coop take seq=" + std::to_string(seq) + " container=" + std::to_string(container));
    });
    _console.registerCommand("coopclose", "co-op client: close the open container screen", [=](const ConsoleArgs &) {
        if (!clientSession().clientContainerClose()) {
            throw std::runtime_error("not a co-op client");
        }
        _console.printLine("coop closed");
    });
    _console.registerCommand("cooppick", "co-op client: pick a free companion: cooppick <tag>", [=](const ConsoleArgs &args) {
        if (args.size() != 2) {
            throw std::runtime_error("Usage: cooppick <tag>");
        }
        uint32_t seq = clientSession().submitRequest(net::CommandType::PickSlot, 0, 0, std::string(*args[1]));
        _console.printLine("coop pick seq=" + std::to_string(seq));
    });
    // Co-op completeness: the client's own inventory, equipment and level-up,
    // through the same calls its action bar, equipment and level-up screens make.
    auto sharedItem = [this](const std::string &which) -> std::shared_ptr<Item> {
        auto player = _party.player();
        if (!player) {
            return nullptr;
        }
        for (const auto &item : player->items()) {
            if (item && (boost::iequals(item->blueprintResRef(), which) || boost::iequals(item->tag(), which))) {
                return item;
            }
        }
        return nullptr;
    };
    _console.registerCommand("coopuseitem", "co-op client: use a shared inventory item as the action bar does: coopuseitem <template|tag> [host id|tag]", [=](const ConsoleArgs &args) {
        if (args.size() < 2) {
            throw std::runtime_error("Usage: coopuseitem <item> [target]");
        }
        auto &session = clientSession();
        auto item = sharedItem(std::string(*args[1]));
        if (!item) {
            throw std::runtime_error("no shared inventory item " + std::string(*args[1]));
        }
        uint32_t targetId = args.size() > 2 ? target(args, 2) : 0;
        session.clientUseItem(item, targetId);
        _console.printLine("coop use_item host_item=" + std::to_string(session.clientHostItemId(item->id())));
    });
    _console.registerCommand("coopequip", "co-op client: equip a shared inventory item on the companion as the equipment screen does: coopequip <template|tag|none> <slot>", [=](const ConsoleArgs &args) {
        auto slot = args.get<int>(2);
        if (args.size() != 3 || !slot) {
            throw std::runtime_error("Usage: coopequip <item|none> <slot>");
        }
        auto &session = clientSession();
        std::shared_ptr<Item> item;
        if (!boost::iequals(std::string(*args[1]), "none")) {
            item = sharedItem(std::string(*args[1]));
            if (!item) {
                throw std::runtime_error("no shared inventory item " + std::string(*args[1]));
            }
        }
        session.clientEquip(item, *slot);
        _console.printLine("coop equip slot=" + std::to_string(*slot) + " host_item=" + std::to_string(item ? session.clientHostItemId(item->id()) : 0));
    });
    _console.registerCommand("cooplevelup", "co-op client: level up the companion through the level-up screens (Recommended): cooplevelup", [=](const ConsoleArgs &) {
        auto &session = clientSession();
        auto member = std::dynamic_pointer_cast<Creature>(session.localObjectForHost(session.controlledHostId()));
        if (!member) {
            throw std::runtime_error("no companion");
        }
        if (!session.clientMayLevelUp()) {
            throw std::runtime_error("no level-up allowed now");
        }
        auto outcome = levelUpThroughScreen(member, true);
        _console.printLine("coop levelup " + (outcome.empty() ? std::string("(screens did not run)") : outcome));
    });
    _console.registerCommand("coophandover", "co-op host: hand a terminal or console to a player: coophandover <client id> <object id|tag> [1|0]", [this](const ConsoleArgs &args) {
        if (!_coop || !_coop->isHost()) {
            throw std::runtime_error("not a co-op host");
        }
        auto client = args.get<int>(1);
        if (!client || args.size() < 3) {
            throw std::runtime_error("Usage: coophandover <client id> <object id|tag> [1|0]");
        }
        std::string which(*args[2]);
        std::shared_ptr<Object> object;
        if (std::all_of(which.begin(), which.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
            object = getObjectById(static_cast<uint32_t>(std::stoul(which)));
        } else if (_module && _module->area()) {
            object = _module->area()->getObjectByTag(which);
        }
        if (!object) {
            throw std::runtime_error("no object " + which);
        }
        bool on = args.size() < 4 || args.get<int>(3).value_or(1) != 0;
        if (!_coop->handOver(static_cast<uint32_t>(*client), object->id(), on)) {
            throw std::runtime_error("no client " + std::to_string(*client));
        }
        _console.printLine("coop handover " + std::string(on ? "on " : "off ") + std::to_string(object->id()));
    });
    // Free Mode
    _console.registerCommand("coopnewguest", "co-op Free Mode client: make the guest character with quick character generation: coopnewguest [class slot 0-5] [name]", [=](const ConsoleArgs &args) {
        auto &session = clientSession();
        if (!session.clientNeedsGuest()) {
            throw std::runtime_error("no guest character is needed");
        }
        int slot = args.size() > 1 ? args.get<int>(1).value_or(2) : 2;
        std::string name = args.size() > 2 ? std::string(*args[2]) : std::string();
        if (!_charGen) {
            _charGen = tryLoadGUI<CharacterGeneration>();
        }
        if (!_charGen) {
            throw std::runtime_error("Character generation GUI is unavailable");
        }
        _charGen->finishQuickForAutomation(slot, name);
        _console.printLine("coop guest " + session.guestPath().string());
    });
    auto storeItem = [this](bool buying, const std::string &which) -> std::shared_ptr<Item> {
        auto items = buying ? _storeSession.stock() : _storeSession.sellable();
        for (const auto &item : items) {
            if (item && (boost::iequals(item->blueprintResRef(), which) || boost::iequals(item->tag(), which))) {
                return item;
            }
        }
        return nullptr;
    };
    _console.registerCommand("coopbuy", "co-op client: buy from the open store as its Buy button does: coopbuy <template|tag>", [=](const ConsoleArgs &args) {
        if (args.size() != 2) {
            throw std::runtime_error("Usage: coopbuy <item>");
        }
        clientSession();
        auto item = storeItem(true, std::string(*args[1]));
        if (!item) {
            throw std::runtime_error("the store has no " + std::string(*args[1]));
        }
        auto result = _storeSession.buy(item);
        _console.printLine(std::string("coop buy ") + StoreSession::describe(result));
    });
    _console.registerCommand("coopsell", "co-op client: sell to the open store as its Sell button does: coopsell <template|tag>", [=](const ConsoleArgs &args) {
        if (args.size() != 2) {
            throw std::runtime_error("Usage: coopsell <item>");
        }
        clientSession();
        auto item = storeItem(false, std::string(*args[1]));
        if (!item) {
            throw std::runtime_error("nothing to sell called " + std::string(*args[1]));
        }
        auto result = _storeSession.sell(item);
        _console.printLine(std::string("coop sell ") + StoreSession::describe(result));
    });
    _console.registerCommand("coopoffer", "co-op Free Mode client: answer the store or minigame prompt: coopoffer [1|0]", [=](const ConsoleArgs &args) {
        bool yes = args.size() < 2 || args.get<int>(1).value_or(1) != 0;
        if (!clientSession().clientAnswerOffer(yes)) {
            throw std::runtime_error("no prompt is open");
        }
        _console.printLine(std::string("coop offer ") + (yes ? "yes" : "no"));
    });
    _console.registerCommand("coopprompts", "co-op: show prompts in a headless run (screenshots): coopprompts [1|0]", [this](const ConsoleArgs &args) {
        if (!_coop) {
            throw std::runtime_error("no co-op session");
        }
        _coop->setForcePrompts(args.size() < 2 || args.get<int>(1).value_or(1) != 0);
        _console.printLine("coop prompts set");
    });
    _console.registerCommand("coopguestscreen", "co-op Free Mode client: open character generation for the guest character", [=](const ConsoleArgs &) {
        clientSession().clientOpenGuestCreation();
        _console.printLine("coop guest creation open");
    });
    _console.registerCommand("coopcredits", "co-op Free Mode host test seam: set a guest's credits: coopcredits <client id> <amount>", [this](const ConsoleArgs &args) {
        auto id = args.get<int>(1);
        auto amount = args.get<int>(2);
        if (!_coop || !_coop->isHost() || !id || !amount) {
            throw std::runtime_error("Usage (host): coopcredits <client id> <amount>");
        }
        if (!_coop->setGuestCredits(static_cast<uint32_t>(*id), *amount)) {
            throw std::runtime_error("no guest for client " + std::to_string(*id));
        }
        _console.printLine("coop credits " + std::to_string(*amount));
    });
    _console.registerCommand("coopresult", "co-op client: the host's answer to a command: coopresult <seq>", [=](const ConsoleArgs &args) {
        auto seq = args.get<uint32_t>(1);
        if (!seq) {
            throw std::runtime_error("Usage: coopresult <seq>");
        }
        auto *result = clientSession().commandResult(*seq);
        if (!result) {
            _console.printLine("{\"seq\":" + std::to_string(*seq) + ",\"pending\":true}");
            return;
        }
        _console.printLine("{\"seq\":" + std::to_string(result->seq) + ",\"accepted\":" + (result->accepted ? "true" : "false") +
                           ",\"reason\":\"" + net::commandRejectName(result->reason) + "\",\"hostTick\":" + std::to_string(result->hostTick) + "}");
    });
}

std::string coopBuildVersion(const std::filesystem::path &executable) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "reone-coop/p%u/%016llx", static_cast<unsigned>(net::kProtocolVersion),
                  static_cast<unsigned long long>(net::hashFile(executable)));
    return buffer;
}

std::shared_ptr<CoopSession> startCoopSession(Game &game, const CoopLaunch &launch, std::string &error) {
    if (launch.role == CoopRole::None) {
        return nullptr;
    }
    if (launch.mode != "story" && launch.mode != "free" && !(launch.role == CoopRole::Client && launch.mode.empty())) {
        error = "--coop-mode must be story or free";
        return nullptr;
    }
    CoopConfig config;
    config.role = launch.role;
    config.mode = launch.mode;
    // Story Mode: the host plus two companions. Free Mode: up to 8 players.
    config.maxClients = launch.mode == "free" ? 7 : 2;
    config.playerName = launch.playerName.empty() ? std::string(launch.role == CoopRole::Host ? "Host" : "Player") : launch.playerName;
    config.preferredTag = launch.preferredTag;
    config.hostOnlyTransitions = launch.hostOnlyTransitions;
    config.hostOnlyTrading = launch.hostOnlyTrading;
    config.noPause = launch.noPause;
    config.pauseCooldown = launch.pauseCooldown;
    config.enemyScaling = launch.enemyScaling;
    config.build = coopBuildVersion(launch.executablePath);
    // Bring your own files: only fingerprints cross the wire.
    config.k1 = net::fingerprintInstall(launch.gamePath);
    if (!launch.k2Path.empty()) {
        config.k2 = net::fingerprintInstall(launch.k2Path);
    }
    info("Co-op: build " + config.build + "; K1 " + net::describeFingerprint(config.k1) +
         (config.k2.present ? "; K2 " + net::describeFingerprint(config.k2) : std::string()));

    if (launch.role == CoopRole::Client && !game.options().game.configurationPath.empty()) {
        config.rejoinStore = game.options().game.configurationPath.parent_path() / "coop_rejoin.json";
        config.sessionKey = !launch.code.empty() ? "code:" + launch.code : "addr:" + launch.address;
    }
    config.askToJoin = launch.askToJoin < 0 ? !game.options().graphics.headless : launch.askToJoin == 1;

    net::OnlineConfig online;
    online.service = launch.service;
    online.relayOnly = launch.relayOnly;
    // The same way in again after a drop (reconnect): a code is resolved anew.
    CoopTransportFactory open;
    if (launch.role == CoopRole::Host) {
        open = [launch, online, max = config.maxClients](std::string &error) {
            return launch.service.empty() ? net::createHostTransport(launch.port, max, error)
                                          : net::createOnlineHostTransport(launch.port, max, online, error);
        };
    } else if (!launch.code.empty()) {
        if (launch.service.empty()) {
            error = "joining by code needs a join-code service (--coop-server)";
            return nullptr;
        }
        open = [code = launch.code, online](std::string &error) { return net::createCodeClientTransport(code, online, error); };
    } else {
        open = [address = launch.address](std::string &error) { return net::createClientTransport(address, error); };
    }
    auto transport = open(error);
    if (!transport) {
        return nullptr;
    }
    auto session = std::make_shared<CoopSession>(game, std::move(config), std::move(transport));
    if (launch.role == CoopRole::Client) {
        session->setReconnect(open);
    }
    game.setCoopSession(session);
    return session;
}

} // namespace game

} // namespace reone
