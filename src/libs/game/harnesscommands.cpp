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

// Console commands an automated harness uses to move and interact the way a
// player does. Every command queues the same actions the mouse and the
// scripting layer queue, so pathfinding and interaction behave as in play.

#include "reone/game/hitvisuals.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/visual.h"
#include "reone/game/visualeffects.h"
#include "reone/game/game.h"
#include "reone/game/gui/chargen.h"
#include "reone/resource/k2merge.h"
#include "reone/game/d20/prestige.h"
#include "reone/game/crafting.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/forms.h"
#include "reone/game/gui/pazaak.h"
#include "reone/gui/control/listbox.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <map>
#include <set>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#include "reone/game/equipmentoperation.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/equipitem.h"
#include "reone/game/action/follow.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/useskill.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/system/randomutil.h"
#include "reone/game/gui/container.h"
#include "reone/game/gui/partyselect.h"
#include "reone/game/gui/store.h"
#include "reone/game/gui/upgrade.h"
#include "reone/game/gui/ingame.h"
#include "reone/game/gui/ingame/options.h"
#include "reone/game/upgradebench.h"
#include "reone/game/coopsession.h"
#include "reone/resource/provider/2das.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"
#include "reone/scene/interp.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

namespace {

std::string describeActionType(ActionType type) {
    switch (type) {
    case ActionType::MoveToPoint:
        return "MoveToPointAction";
    case ActionType::PickUpItem:
        return "PickUpItemAction";
    case ActionType::AttackObject:
        return "AttackObjectAction";
    case ActionType::CastSpell:
        return "CastSpellAction";
    case ActionType::OpenDoor:
        return "OpenDoorAction";
    case ActionType::CloseDoor:
        return "CloseDoorAction";
    case ActionType::OpenLock:
        return "OpenLockAction";
    case ActionType::UseObject:
        return "UseObjectAction";
    case ActionType::Follow:
        return "FollowAction";
    case ActionType::Wait:
        return "WaitAction";
    case ActionType::FollowLeader:
        return "FollowLeaderAction";
    case ActionType::FollowOwner:
        return "FollowOwnerAction";
    case ActionType::DoCommand:
        return "DoCommandAction";
    case ActionType::StartConversation:
        return "StartConversationAction";
    case ActionType::PauseConversation:
        return "PauseConversationAction";
    case ActionType::ResumeConversation:
        return "ResumeConversationAction";
    case ActionType::MoveToObject:
        return "MoveToObjectAction";
    case ActionType::OpenContainer:
        return "OpenContainerAction";
    case ActionType::JumpToObject:
        return "JumpToObjectAction";
    case ActionType::JumpToLocation:
        return "JumpToLocationAction";
    case ActionType::RandomWalk:
        return "RandomWalkAction";
    case ActionType::MoveToLocation:
        return "MoveToLocationAction";
    case ActionType::MoveAwayFromObject:
        return "MoveAwayFromObjectAction";
    case ActionType::EquipItem:
        return "EquipItemAction";
    case ActionType::UnequipItem:
        return "UnequipItemAction";
    case ActionType::PlayAnimation:
        return "PlayAnimationAction";
    case ActionType::CastSpellAtObject:
        return "CastSpellAtObjectAction";
    case ActionType::UseFeat:
        return "UseFeatAction";
    case ActionType::UseSkill:
        return "UseSkillAction";
    case ActionType::UseTalentOnObject:
        return "UseTalentOnObjectAction";
    case ActionType::InteractObject:
        return "InteractObjectAction";
    case ActionType::CutsceneAttack:
        return "CutsceneAttackAction";
    default:
        return str(boost::format("ActionType(0x%x)") % static_cast<int>(type));
    }
}

std::string describeObjectType(ObjectType type) {
    switch (type) {
    case ObjectType::Creature:
        return "creature";
    case ObjectType::Door:
        return "door";
    case ObjectType::Placeable:
        return "placeable";
    case ObjectType::Item:
        return "item";
    case ObjectType::Trigger:
        return "trigger";
    case ObjectType::Waypoint:
        return "waypoint";
    default:
        return "object";
    }
}

std::string formatVec3(const glm::vec3 &v) {
    return str(boost::format("[%.2f, %.2f, %.2f]") % v.x % v.y % v.z);
}

// A completed action stays at the front of the queue until the object's next
// update removes it, so a queue holding only completed actions is idle.
std::shared_ptr<Action> firstPendingAction(const Object &object) {
    for (const auto &action : object.actions()) {
        if (!action->isCompleted()) {
            return action;
        }
    }
    return nullptr;
}

bool parseBoolArg(const std::string &value, const char *name) {
    if (value == "1" || value == "true") {
        return true;
    }
    if (value == "0" || value == "false") {
        return false;
    }
    throw std::runtime_error(str(boost::format("%s must be 0 or 1, got '%s'") % name % value));
}

float parseFloatArg(const std::string &value, const char *name) {
    float result = 0.0f;
    const char *end = value.data() + value.size();
    auto parsed = std::from_chars(value.data(), end, result);
    if (parsed.ec != std::errc {} || parsed.ptr != end || !std::isfinite(result)) {
        throw std::runtime_error(str(boost::format("%s must be a number, got '%s'") % name % value));
    }
    return result;
}

int parsePositiveIntArg(const std::string &value, const char *name) {
    int result = 0;
    const char *end = value.data() + value.size();
    auto parsed = std::from_chars(value.data(), end, result);
    if (parsed.ec != std::errc {} || parsed.ptr != end || result < 1) {
        throw std::runtime_error(str(boost::format("%s must be a positive integer, got '%s'") % name % value));
    }
    return result;
}

/**
 * Harness command arguments after the command name: positional values,
 * key=value options and an optional @id|tag actor selector, in any order.
 */
struct HarnessArgs {
    std::vector<std::string> positional;
    std::map<std::string, std::string> named;
    std::optional<std::string> actor;

    explicit HarnessArgs(const ConsoleArgs &args) {
        for (size_t i = 1; i < args.size(); ++i) {
            std::string token(args[i].value());
            if (token.size() > 1 && token.front() == '@') {
                actor = token.substr(1);
                continue;
            }
            auto eq = token.find('=');
            if (eq != std::string::npos && eq > 0 && std::isalpha(static_cast<unsigned char>(token.front()))) {
                named[token.substr(0, eq)] = token.substr(eq + 1);
                continue;
            }
            positional.push_back(std::move(token));
        }
    }

    void requireKnown(std::initializer_list<const char *> keys) const {
        for (const auto &entry : named) {
            bool known = std::any_of(keys.begin(), keys.end(), [&entry](const char *key) { return entry.first == key; });
            if (!known) {
                throw std::runtime_error("Unknown option: " + entry.first);
            }
        }
    }

    // Positional index \p index, overridden by option \p key when both appear.
    std::optional<std::string> value(size_t index, const char *key) const {
        if (auto it = named.find(key); it != named.end()) {
            return it->second;
        }
        if (index < positional.size()) {
            return positional[index];
        }
        return std::nullopt;
    }
};

} // namespace

bool Game::pollCommandBlocker() {
    if (!_commandBlocker) {
        return false;
    }
    if (_commandBlocker()) {
        return true;
    }
    _commandBlocker = nullptr;
    return false;
}

std::shared_ptr<Object> Game::findConsoleObject(std::string_view idOrTag) {
    bool numeric = !idOrTag.empty() &&
                   std::all_of(idOrTag.begin(), idOrTag.end(), [](char c) {
                       return std::isdigit(static_cast<unsigned char>(c)) != 0;
                   });
    if (numeric) {
        uint32_t id = 0;
        std::from_chars(idOrTag.data(), idOrTag.data() + idOrTag.size(), id);
        if (auto object = getObjectById(id)) {
            return object;
        }
        throw std::runtime_error("Object not found: id " + std::string(idOrTag));
    }
    auto area = getConsoleArea();
    for (auto &object : area->objects()) {
        if (object->tag() == idOrTag) {
            return object;
        }
    }
    for (auto &object : area->objects()) {
        if (boost::iequals(object->tag(), idOrTag)) {
            return object;
        }
    }
    throw std::runtime_error("Object not found: tag " + std::string(idOrTag));
}

std::shared_ptr<Creature> Game::findConsoleActor(const std::optional<std::string> &idOrTag) {
    if (!idOrTag) {
        return getConsoleLeader();
    }
    auto creature = dyn_cast<Creature>(findConsoleObject(*idOrTag));
    if (!creature) {
        throw std::runtime_error("Actor must be a creature: " + *idOrTag);
    }
    return creature;
}

void Game::consoleMoveTo(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"z", "run"});
    if (parsed.positional.size() < 2 || parsed.positional.size() > 4) {
        throw std::runtime_error("Usage: moveto x y [z] [run=1] [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    auto area = getConsoleArea();

    glm::vec3 dest(parseFloatArg(parsed.positional[0], "x"),
                   parseFloatArg(parsed.positional[1], "y"),
                   actor->position().z);
    auto zArg = parsed.value(2, "z");
    auto runArg = parsed.value(3, "run");
    bool run = runArg ? parseBoolArg(*runArg, "run") : true;

    std::string zSource = "arg";
    if (zArg) {
        dest.z = parseFloatArg(*zArg, "z");
    } else {
        // Resolve the floor under (x, y): the surface nearest below the
        // actor's own level first, then the topmost surface at that point.
        auto &sceneGraph = _services.scene.graphs.get(kSceneMain);
        scene::Collision collision;
        glm::vec3 nearProbe(dest.x, dest.y, actor->position().z + 1.0f);
        glm::vec3 topProbe(dest.x, dest.y, actor->position().z + scene::kElevationTestZ);
        if (sceneGraph.testElevation(nearProbe, collision) || sceneGraph.testElevation(topProbe, collision)) {
            dest.z = collision.intersection.z;
            zSource = "walkmesh";
        } else if (auto z = walkmeshElevationAt(area->pathfinder().uni, glm::vec2(dest), actor->position().z)) {
            // The scene graph holds only the rooms near the camera; the
            // pathfinder's walkmesh has them all (a far room on another level).
            dest.z = *z;
            zSource = "uniwalk";
        } else {
            zSource = "offmesh";
        }
    }

    glm::vec3 from(actor->position());
    // Queue as a player click does: drop what the creature was doing, then
    // act. MoveToLocation is the action ActionMoveToLocation queues; it walks
    // through Creature::navigateTo, and has the run flag MoveToPoint lacks.
    actor->clearAllActions();
    actor->addAction(newAction<MoveToLocationAction>(
        std::make_shared<Location>(dest, actor->getFacing()), run));

    std::string line = str(boost::format("moveto queued id=%u tag=%s from=%s to=%s run=%d z=%s dist=%.2f") %
                           actor->id() % actor->tag() % formatVec3(from) % formatVec3(dest) %
                           (run ? 1 : 0) % zSource % glm::distance(glm::vec2(from), glm::vec2(dest)));
    _console.printLine(line);
    info(line);
}

void Game::consoleMoveToObject(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"run", "distance"});
    if (parsed.positional.empty() || parsed.positional.size() > 3) {
        throw std::runtime_error("Usage: movetoobject id|tag [run=1] [distance] [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    auto target = findConsoleObject(parsed.positional[0]);
    auto runArg = parsed.value(1, "run");
    auto distanceArg = parsed.value(2, "distance");
    bool run = runArg ? parseBoolArg(*runArg, "run") : true;
    float distance = distanceArg ? parseFloatArg(*distanceArg, "distance") : 1.0f;

    glm::vec3 from(actor->position());
    actor->clearAllActions();
    actor->addAction(newAction<MoveToObjectAction>(target, run, distance));

    std::string line = str(boost::format("movetoobject queued id=%u tag=%s target=%u tag=%s from=%s to=%s run=%d distance=%.2f") %
                           actor->id() % actor->tag() % target->id() % target->tag() % formatVec3(from) %
                           formatVec3(target->position()) % (run ? 1 : 0) % distance);
    _console.printLine(line);
    info(line);
}

void Game::consoleClick(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    bool now = parsed.positional.size() == 2 && parsed.positional[1] == "now";
    bool attack = parsed.positional.size() == 2 && parsed.positional[1] == "attack";
    if (parsed.positional.empty() || parsed.positional.size() > 2 || parsed.actor ||
        (parsed.positional.size() == 2 && !now && !attack)) {
        throw std::runtime_error("Usage: click id|tag [now|attack]");
    }
    auto mod = module();
    if (!mod || !mod->area()) {
        throw std::runtime_error("Module is not loaded");
    }
    auto object = findConsoleObject(parsed.positional[0]);
    auto leader = getConsoleLeader();
    std::string prefix = str(boost::format("click %s %s id=%u") % describeObjectType(object->type()) % object->tag() % object->id());

    auto report = [this](const std::string &line) {
        _console.printLine(line);
        info(line);
    };
    if (!object->isSelectable()) {
        // The mouse cannot pick this object, so neither can a player.
        report(prefix + " -> not selectable");
        return;
    }

    if (attack) {
        // The target menu's attack entry (bash, for a door or placeable), as
        // SelectionOverlay queues it when the player picks it.
        auto actions = mod->getContextActions(object);
        bool offered = std::any_of(actions.begin(), actions.end(), [](const ContextAction &action) {
            return action.type == ActionType::AttackObject;
        });
        if (!offered) {
            report(prefix + " -> attack not offered");
            return;
        }
        auto action = newAction<AttackObjectAction>(object);
        action->setUserAction(true);
        leader->addAction(std::move(action));
        report(prefix + " -> AttackObjectAction");
        return;
    }

    auto lastQueued = leader->actions().empty() ? nullptr : leader->actions().back();
    bool acted = mod->clickObject(object);
    if (!acted && now) {
        acted = mod->clickObject(object);
    }

    std::string outcome;
    if (!acted) {
        outcome = "selected";
    } else if (!_nextModule.empty()) {
        outcome = "ModuleTransition " + _nextModule;
    } else {
        auto queued = leader->actions().empty() ? nullptr : leader->actions().back();
        outcome = (queued && queued != lastQueued) ? describeActionType(queued->type()) : "no action queued";
    }
    report(prefix + " -> " + outcome);
}

void Game::consoleCancelCombat(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: cancelcombat [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    actor->cancelCombatNow();
    std::string line = str(boost::format("cancelcombat id=%u tag=%s inCombat=%d actions=%u") %
                           actor->id() % actor->tag() % actor->isInCombat() % actor->actions().size());
    _console.printLine(line);
    info(line);
}

void Game::consoleAutoCombat(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() > 1) {
        throw std::runtime_error("Usage: autocombat [1|0] [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    bool on = parsed.positional.empty() ? !actor->autoCombat() : parsed.positional[0] != "0";
    setAutoCombat(*actor, on);
    std::string line = str(boost::format("autocombat id=%u tag=%s on=%d") % actor->id() % actor->tag() % on);
    _console.printLine(line);
    info(line);
}

void Game::consoleClearActions(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: clearactions [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    size_t before = actor->actions().size();
    // Same call a player click makes before queueing its own action.
    actor->clearAllActions();
    size_t remaining = actor->actions().size();
    std::string line = str(boost::format("clearactions id=%u tag=%s cleared=%u remaining=%u") %
                           actor->id() % actor->tag() % (before - remaining) % remaining);
    _console.printLine(line);
    info(line);
}

void Game::consoleWaitIdle(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"maxframes"});
    if (parsed.positional.size() > 1) {
        throw std::runtime_error("Usage: waitidle [@id|tag] [maxframes=600]");
    }
    auto maxArg = parsed.value(0, "maxframes");
    int maxFrames = maxArg ? parsePositiveIntArg(*maxArg, "maxframes") : 600;
    auto actor = findConsoleActor(parsed.actor);
    uint32_t actorId = actor->id();

    // A blocking condition only: it prints nothing to the console, because a
    // line printed frames later belongs to no command. idlestatus reports the
    // outcome. The log still records it.
    _waitIdleFrames = 0;
    if (!firstPendingAction(*actor)) {
        info("waitidle: idle frames=0");
        return;
    }
    // Polled once per frame by the scripted-command runner, which runs no
    // further commands until this returns false.
    setCommandBlocker([this, actorId, maxFrames]() {
        ++_waitIdleFrames;
        auto creature = getObjectById<Creature>(actorId);
        auto pending = creature ? firstPendingAction(*creature) : nullptr;
        if (!pending) {
            info(str(boost::format("waitidle: idle frames=%d") % _waitIdleFrames));
            return false;
        }
        if (_waitIdleFrames >= maxFrames) {
            info(str(boost::format("waitidle: timeout frames=%d front=%s") % _waitIdleFrames % describeActionType(pending->type())));
            return false;
        }
        return true;
    });
}

void Game::consoleIdleStatus(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: idlestatus [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    auto front = firstPendingAction(*actor);
    std::string line;
    if (!front) {
        line = str(boost::format("idle frames_waited=%d") % _waitIdleFrames);
    } else {
        size_t pending = std::count_if(actor->actions().begin(), actor->actions().end(), [](const auto &action) {
            return !action->isCompleted();
        });
        line = str(boost::format("busy front=%s queue=%u frames_waited=%d") %
                   describeActionType(front->type()) % pending % _waitIdleFrames);
    }
    _console.printLine(line);
    info(line);
}

void Game::consoleCloseScreen(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty() || parsed.actor) {
        throw std::runtime_error("Usage: closescreen");
    }
    // A status summary over the HUD is acknowledged as its OK button does.
    if (_screen == Screen::InGame && _hud && _hud->dismissStatusSummary()) {
        _console.printLine("closescreen: status summary acknowledged");
        return;
    }
    // Leaves a store, upgrade bench or the credits the way their own
    // Close/Back/skip does.
    if (_screen == Screen::Store) {
        closeStore();
        _console.printLine("closescreen: back in game");
        return;
    }
    if (_screen == Screen::Upgrade) {
        closeUpgradeScreen();
        _console.printLine("closescreen: back in game");
        return;
    }
    if (_screen == Screen::CharacterGeneration && _charGen &&
        _charGen->type() == CharacterGeneration::Type::LevelUp) {
        // The level-up screen's Back button.
        _charGen->cancel();
        _console.printLine("closescreen: back in game");
        return;
    }
    if (_screen == Screen::Credits) {
        endCreditSequence();
        _console.printLine("closescreen: credits ended");
        return;
    }
    // Leaves a container or in-game menu the way its Close button does.
    if (_screen != Screen::Container && _screen != Screen::InGameMenu) {
        _console.printLine("closescreen: nothing to close");
        return;
    }
    openInGame();
    _console.printLine("closescreen: back in game");
}

void Game::consolePickReply(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 1 || parsed.actor) {
        throw std::runtime_error("Usage: pickreply <row>");
    }
    if (_screen != Screen::Conversation || !_conversation) {
        throw std::runtime_error("pickreply: no conversation is active");
    }
    int row = std::stoi(std::string(parsed.positional[0]));
    if (!_conversation->pickReplyAtRow(row)) {
        throw std::runtime_error("pickreply: no reply at row " + std::to_string(row));
    }
    _console.printLine("pickreply row=" + std::to_string(row));
}

void Game::consoleTakeAll(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty() || parsed.actor) {
        throw std::runtime_error("Usage: takeall");
    }
    if (_screen != Screen::Container) {
        throw std::runtime_error("takeall: no container is open");
    }
    _container->takeAll();
    _console.printLine("takeall: done");
}

void Game::consoleContainerGive(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 2 || parsed.actor) {
        throw std::runtime_error("Usage: containergive <item resref> <container tag>");
    }
    auto leader = _party.getLeader();
    if (!leader) {
        throw std::runtime_error("containergive: no party leader");
    }
    const std::string &resRef = parsed.positional[0];
    std::shared_ptr<Item> item;
    for (auto &candidate : leader->items()) {
        if (boost::iequals(candidate->blueprintResRef(), resRef) || boost::iequals(candidate->tag(), resRef)) {
            item = candidate;
            break;
        }
    }
    if (!item) {
        throw std::runtime_error("containergive: " + leader->tag() + " holds no item " + resRef);
    }
    std::shared_ptr<Object> container;
    for (auto &object : getConsoleArea()->objects()) {
        if (object->type() == ObjectType::Placeable && boost::iequals(object->tag(), parsed.positional[1])) {
            container = object;
            break;
        }
    }
    if (!container) {
        throw std::runtime_error("containergive: no placeable with tag " + parsed.positional[1]);
    }
    if (giveItemToContainer(*this, *leader, item, *container) == script::kObjectInvalid) {
        throw std::runtime_error("containergive: could not give " + resRef);
    }
    _console.printLine("containergive " + item->tag() + " -> " + container->tag());
}

void Game::consoleEquipAuto(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: equipauto [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    static constexpr int kSlots[] {
        InventorySlots::body, InventorySlots::rightWeapon, InventorySlots::leftWeapon,
        InventorySlots::head, InventorySlots::hands, InventorySlots::implant,
        InventorySlots::belt, InventorySlots::leftArm, InventorySlots::rightArm};
    std::set<const Item *> used;
    std::string equipped;
    for (int slot : kSlots) {
        if (actor->getEquippedItem(slot)) {
            continue;
        }
        for (auto &item : actor->items()) {
            if (used.count(item.get()) || !item->isEquippable(slot)) {
                continue;
            }
            actor->addAction(newAction<EquipItemAction>(item, slot, true));
            used.insert(item.get());
            equipped += " " + item->tag() + "@" + std::to_string(slot);
            break;
        }
    }
    _console.printLine("equipauto:" + (equipped.empty() ? std::string(" nothing") : equipped));
}

void Game::consoleEquipSlot(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 2) {
        throw std::runtime_error("Usage: equipslot <item tag|template> <slot> [@id|tag]");
    }
    static const std::map<std::string, int> kSlots {
        {"head", InventorySlots::head}, {"body", InventorySlots::body},
        {"hands", InventorySlots::hands}, {"rightweapon", InventorySlots::rightWeapon},
        {"leftweapon", InventorySlots::leftWeapon}, {"leftarm", InventorySlots::leftArm},
        {"rightarm", InventorySlots::rightArm}, {"implant", InventorySlots::implant},
        {"belt", InventorySlots::belt}, {"rightweapon2", InventorySlots::rightWeapon2},
        {"leftweapon2", InventorySlots::leftWeapon2}};
    auto slotName = boost::to_lower_copy(parsed.positional[1]);
    int slot;
    if (auto named = kSlots.find(slotName); named != kSlots.end()) {
        slot = named->second;
    } else {
        slot = std::stoi(slotName);
        bool known = std::any_of(kSlots.begin(), kSlots.end(), [slot](const auto &entry) { return entry.second == slot; });
        if (!known) {
            throw std::runtime_error("equipslot: unknown slot " + parsed.positional[1]);
        }
    }
    auto actor = findConsoleActor(parsed.actor);
    auto wanted = boost::to_lower_copy(parsed.positional[0]);
    // Items live in the actor's own inventory or, for the party, in the shared
    // inventory the equipment screen lists.
    std::shared_ptr<Item> found;
    auto matches = [&wanted](const std::shared_ptr<Item> &item) {
        return item && !item->isEquipped() &&
               (boost::to_lower_copy(item->tag()) == wanted ||
                boost::to_lower_copy(item->blueprintResRef()) == wanted);
    };
    for (auto &item : actor->items()) {
        if (matches(item)) {
            found = item;
            break;
        }
    }
    if (!found) {
        if (auto shared = _party.sharedInventoryReceiver(actor)) {
            for (auto &item : shared->items()) {
                if (matches(item)) {
                    found = item;
                    break;
                }
            }
        }
    }
    if (!found) {
        throw std::runtime_error("equipslot: no unequipped item " + parsed.positional[0]);
    }
    // What the equipment screen does: the change applies at once, whatever
    // the creature is doing. A queued ActionEquipItem waited behind the
    // creature's actions, so a companion still holding an attack (Canderous
    // at Davik's hangar) never put the suit on, though equipslot said ok.
    auto outcome = applyEquipmentFromHolder(*this, *actor, found, slot);
    if (outcome == EquipmentOperationOutcome::Rejected || outcome == EquipmentOperationOutcome::Failed) {
        throw std::runtime_error("equipslot: " + found->tag() + " cannot go in slot " + std::to_string(slot) +
                                 " on " + actor->tag());
    }
    _console.printLine("equipslot " + found->tag() + "@" + std::to_string(slot) + " by " + actor->tag());
}

void Game::consoleSwapWeapons(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: swapweapons [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    // Same request as the HUD's swap-weapons button.
    requestWeaponSwitch(*this, *actor);
    _console.printLine("swapweapons by " + actor->tag() + (actor->isInCombat() ? " (queued, in combat)" : ""));
}

void Game::consolePartySelect(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.empty() || parsed.actor) {
        throw std::runtime_error("Usage: partyselect available|<npc>...");
    }
    if (_screen != Screen::PartySelection) {
        throw std::runtime_error("partyselect: the party selection screen is not open");
    }
    std::vector<int> npcs;
    if (parsed.positional.size() == 1 && parsed.positional[0] == "available") {
        for (int npc = 0; npc < _partySelect->selectableNpcCount(); ++npc) {
            if (_party.isMemberAvailable(npc)) {
                npcs.push_back(npc);
            }
        }
    } else {
        for (auto &token : parsed.positional) {
            npcs.push_back(std::stoi(token));
        }
    }
    std::string added;
    if (!(parsed.positional.size() == 1 && parsed.positional[0] == "available")) {
        // An explicit list is the whole party: take off whoever is not in it
        // first, as a player clicks a member and Remove. With a full party the
        // adds below otherwise found no free slot and Done kept the old party
        // (Janice's T3-M4 sale: Bastila and Carth stayed, T3-M4 was dropped).
        for (int npc = 0; npc < _partySelect->selectableNpcCount(); ++npc) {
            if (_partySelect->isNpcAdded(npc) && std::find(npcs.begin(), npcs.end(), npc) == npcs.end()) {
                _partySelect->clickNpc(npc);
                _partySelect->clickAccept();
            }
        }
    }
    for (int npc : npcs) {
        if (_partySelect->isNpcAdded(npc)) {
            continue;
        }
        _partySelect->clickNpc(npc);
        _partySelect->clickAccept();
        if (_partySelect->isNpcAdded(npc)) {
            added += " " + std::to_string(npc);
        }
    }
    _partySelect->clickDone();
    _console.printLine("partyselect: added" + (added.empty() ? std::string(" none") : added));
}

void Game::consoleSwitchLeader(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() > 1 || parsed.actor) {
        throw std::runtime_error("Usage: switchleader [tag]");
    }
    // Each step is what pressing Tab does; stop once the wanted tag leads.
    const size_t members = _party.getSize();
    for (size_t i = 0; i < std::max<size_t>(members, 1); ++i) {
        _party.switchLeader();
        if (parsed.positional.empty() || _party.getLeader()->tag() == parsed.positional[0]) {
            break;
        }
    }
    auto leader = _party.getLeader();
    if (!parsed.positional.empty() && leader->tag() != parsed.positional[0]) {
        throw std::runtime_error("switchleader: " + parsed.positional[0] + " is not in the party");
    }
    _console.printLine("switchleader: leader=" + leader->tag());
}

void Game::consoleUseSkill(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 2) {
        throw std::runtime_error("Usage: useskill <skill> <id|tag> [@actor]");
    }
    static const std::map<std::string, SkillType> kSkills {
        {"computeruse", SkillType::ComputerUse}, {"demolitions", SkillType::Demolitions},
        {"stealth", SkillType::Stealth}, {"awareness", SkillType::Awareness},
        {"persuade", SkillType::Persuade}, {"repair", SkillType::Repair},
        {"security", SkillType::Security}, {"treatinjury", SkillType::TreatInjury}};
    auto skill = kSkills.find(boost::to_lower_copy(parsed.positional[0]));
    if (skill == kSkills.end()) {
        throw std::runtime_error("useskill: unknown skill " + parsed.positional[0]);
    }
    auto target = findConsoleObject(parsed.positional[1]);
    auto actor = findConsoleActor(parsed.actor);
    // Same as picking the skill in the target menu (SelectionOverlay).
    auto action = newAction<UseSkillAction>(skill->second, target);
    action->setUserAction(true);
    actor->addAction(std::move(action));
    _console.printLine("useskill " + parsed.positional[0] + " -> " + target->tag() + " by " + actor->tag());
}

void Game::consoleGiveLoot(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() < 2 || parsed.positional.size() > 3 || parsed.actor) {
        throw std::runtime_error("Usage: giveloot <id|tag> <item template> [stack]");
    }
    auto target = findConsoleObject(parsed.positional[0]);
    int stack = parsed.positional.size() == 3 ? std::stoi(parsed.positional[2]) : 1;
    // Same receiver and droppable flag as the CreateItemOnObject routine.
    auto receiver = _party.sharedInventoryReceiver(target);
    auto item = receiver->addItem(boost::to_lower_copy(parsed.positional[1]), stack, true);
    if (!item) {
        throw std::runtime_error("giveloot: could not create " + parsed.positional[1]);
    }
    _console.printLine("giveloot " + item->tag() + " -> " + target->tag());
}

void Game::consoleVfx(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() < 2 || parsed.positional.size() > 4 || parsed.actor) {
        throw std::runtime_error("Usage: vfx <visualeffects row> <id|tag> [source id|tag|- [seconds]]");
    }
    int row = std::stoi(parsed.positional[0]);
    auto target = findConsoleObject(parsed.positional[1]);
    if (!_services.game.visualEffects.get(static_cast<uint32_t>(row))) {
        throw std::runtime_error("vfx: no visualeffects row " + parsed.positional[0]);
    }
    if (parsed.positional.size() >= 3 && parsed.positional[2] == "-") {
        // No source: the row as EffectVisualEffect applied temporary (a
        // duration row - shield, aura, hold, cloak - lasting that long).
        float seconds = parsed.positional.size() == 4 ? std::stof(parsed.positional[3]) : 3.0f;
        target->applyEffect(newEffect<VisualEffect>(row, false, _services), DurationType::Temporary, seconds);
        _console.printLine(str(boost::format("vfx %s -> %s for %.2f s") % parsed.positional[0] % target->tag() % seconds));
        return;
    }
    if (parsed.positional.size() >= 3) {
        // A beam from a source, as EffectBeam(row, source, BODY_NODE_HAND)
        // applied temporary: the same effect objects a script makes.
        auto source = findConsoleObject(parsed.positional[2]);
        float seconds = parsed.positional.size() == 4 ? std::stof(parsed.positional[3]) : 2.0f;
        auto beam = newEffect<BeamEffect>(row, source, BodyNode::Hand, false);
        beam->setImpactVisual(newEffect<VisualEffect>(row, false, _services));
        target->applyEffect(beam, DurationType::Temporary, seconds);
        _console.printLine("vfx " + parsed.positional[0] + " " + source->tag() + " -> " + target->tag());
        return;
    }
    hitvisuals::presentOn(*this, _services, row, *target);
    _console.printLine("vfx " + parsed.positional[0] + " -> " + target->tag());
}

void Game::consoleUseHealItem(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() > 1 || parsed.actor) {
        throw std::runtime_error("Usage: usehealitem [n|item tag or template]");
    }
    auto leader = _party.getLeader();
    // Same source and action as the action bar's healing slot.
    auto healing = _party.usableItemSpells(*leader, ItemAttributes::Kind::Healing);
    auto it = healing.begin();
    const std::string which = parsed.positional.empty() ? std::string("0") : parsed.positional[0];
    if (!which.empty() && std::all_of(which.begin(), which.end(), [](unsigned char c) { return std::isdigit(c); })) {
        size_t index = std::stoul(which);
        if (index >= healing.size()) {
            throw std::runtime_error("usehealitem: only " + std::to_string(healing.size()) + " healing items");
        }
        std::advance(it, index);
    } else {
        // The healing slot's entries are not in a stable order; a driver
        // that wants a particular item names it.
        it = std::find_if(healing.begin(), healing.end(), [&which](const auto &entry) {
            return boost::iequals(entry.first->tag(), which) ||
                   boost::iequals(entry.first->blueprintResRef(), which);
        });
        if (it == healing.end()) {
            throw std::runtime_error("usehealitem: no healing item " + which);
        }
    }
    const auto &[item, spell] = *it;
    auto action = newAction<CastSpellAtObjectAction>(spell, leader, std::optional<std::shared_ptr<Item>>(item));
    action->setUserAction(true);
    leader->addAction(std::move(action));
    _console.printLine("usehealitem " + item->tag() + " by " + leader->tag());
}

void Game::consoleUseDefensiveItem(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 1 || parsed.actor) {
        throw std::runtime_error("Usage: usedefitem <item tag or template>");
    }
    auto leader = _party.getLeader();
    // Same source and action as the action bar's defensive slot (stims,
    // energy shields), used on the leader.
    const auto defensive = _party.usableItemSpells(*leader, ItemAttributes::Kind::Defensive);
    const std::string &which = parsed.positional[0];
    auto it = std::find_if(defensive.begin(), defensive.end(), [&which](const auto &entry) {
        return boost::iequals(entry.first->tag(), which) || boost::iequals(entry.first->blueprintResRef(), which);
    });
    if (it == defensive.end()) {
        throw std::runtime_error("usedefitem: no defensive item " + which);
    }
    const auto &[item, spell] = *it;
    auto action = newAction<CastSpellAtObjectAction>(spell, leader, std::optional<std::shared_ptr<Item>>(item));
    action->setUserAction(true);
    leader->addAction(std::move(action));
    _console.printLine("usedefitem " + item->tag() + " by " + leader->tag());
}

void Game::consoleUseAttackItem(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 2 || parsed.actor) {
        throw std::runtime_error("Usage: useattackitem <item tag or template> <target id|tag>");
    }
    auto leader = _party.getLeader();
    auto target = findConsoleObject(parsed.positional[1]);
    if (!target) {
        throw std::runtime_error("useattackitem: no object " + parsed.positional[1]);
    }
    // Same source and action as the target menu's item slot on a hostile
    // creature (grenades): the leader's attacking items.
    const auto attacking = _party.usableItemSpells(*leader, ItemAttributes::Kind::Attacking);
    const std::string &which = parsed.positional[0];
    auto it = std::find_if(attacking.begin(), attacking.end(), [&which](const auto &entry) {
        return boost::iequals(entry.first->tag(), which) || boost::iequals(entry.first->blueprintResRef(), which);
    });
    if (it == attacking.end()) {
        throw std::runtime_error("useattackitem: no attacking item " + which);
    }
    const auto &[item, spell] = *it;
    auto action = newAction<CastSpellAtObjectAction>(spell, target, std::optional<std::shared_ptr<Item>>(item));
    action->setUserAction(true);
    leader->addAction(std::move(action));
    _console.printLine("useattackitem " + item->tag() + " by " + leader->tag() + " at " + target->tag());
}

void Game::consoleLevelUp(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"recommended", "prestige"});
    if (!parsed.positional.empty()) {
        throw std::runtime_error("Usage: levelup [recommended=1] [prestige=<class id>|skip] [@member]");
    }
    auto recommendedArg = parsed.value(0, "recommended");
    bool recommended = recommendedArg ? parseBoolArg(*recommendedArg, "recommended") : true;
    int prestigeClass = -1;
    if (auto it = parsed.named.find("prestige"); it != parsed.named.end() && it->second != "skip") {
        prestigeClass = std::stoi(it->second);
    }
    auto member = findConsoleActor(parsed.actor);
    if (!member || !member->isLevelUpPending()) {
        throw std::runtime_error("levelup: " + (member ? member->tag() : std::string("the party leader")) +
                                 " has no level-up pending");
    }
    if (!_party.isMember(*member)) {
        throw std::runtime_error("levelup: " + member->tag() + " is not in the party");
    }
    // The level-up screen's own steps: each step's Recommended pick (or, with
    // recommended=0, no skill picks), then Accept.
    auto line = levelUpThroughScreen(member, recommended, prestigeClass);
    if (line.empty()) {
        throw std::runtime_error("levelup: no level-up screen");
    }
    _console.printLine(line);
    info(line);
}

// K2 systems merge report (design note k2_merge): one JSON line per merged
// table, then a summary line.
void Game::consoleK2Merge(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    auto report = [this](const std::string &line) {
        _console.printLine(line);
        info(line);
    };
    auto *merge = _services.resource.k2Merge;
    if (!merge) {
        report("{\"k2merge\":false}");
        return;
    }
    // Test helpers: add a class as AddMultiClass does, or list the prestige
    // classes a member would be offered now.
    if (!parsed.positional.empty() && parsed.positional[0] == "addclass") {
        auto member = findConsoleActor(parsed.actor);
        if (!member || parsed.positional.size() != 2) {
            throw std::runtime_error("Usage: k2merge addclass <class id> [@member]");
        }
        bool added = member->addMultiClass(static_cast<ClassType>(std::stoi(parsed.positional[1])));
        report(str(boost::format("{\"tag\":\"%s\",\"added\":%d,\"classes\":%d}") %
                   member->tag() % (added ? 1 : 0) % member->attributes().classLevels().size()));
        return;
    }
    // K2 crafting at the bench: status, the recipe list, create, break down.
    if (!parsed.positional.empty() && parsed.positional[0] == "craft") {
        auto crafter = findConsoleActor(parsed.actor);
        if (!crafter) {
            throw std::runtime_error("Usage: k2merge craft [status|list|create <template>|breakdown <item id>|materials <components> <chemicals>] [@crafter]");
        }
        CraftingBench bench(*this, _services);
        std::string verb = parsed.positional.size() > 1 ? parsed.positional[1] : "status";
        bool done = true;
        if (verb == "list") {
            for (const auto &entry : bench.recipes(*crafter)) {
                report(str(boost::format("{\"template\":\"%s\",\"name\":\"%s\",\"cost\":%d,\"chemical\":%d,\"skill\":%d,\"rank\":%d,\"skilled\":%d,\"affordable\":%d}") %
                           entry.recipe.templateResRef % entry.name % entry.cost %
                           (entry.recipe.material == crafting::Material::Chemicals ? 1 : 0) %
                           static_cast<int>(entry.recipe.skill) % entry.recipe.skillRank %
                           (entry.skilled ? 1 : 0) % (entry.affordable ? 1 : 0)));
            }
            for (const auto &item : bench.breakdownCandidates()) {
                report(str(boost::format("{\"breakdown\":%u,\"tag\":\"%s\",\"yield\":%d,\"chemical\":%d}") %
                           item->id() % item->tag() % bench.breakdownYieldFor(*item, *crafter) %
                           (crafting::materialOfBaseItem(item->baseItemType()) == crafting::Material::Chemicals ? 1 : 0)));
            }
        } else if (verb == "create" && parsed.positional.size() == 3) {
            done = false;
            for (const auto &entry : bench.recipes(*crafter)) {
                if (boost::iequals(entry.recipe.templateResRef, parsed.positional[2])) {
                    done = bench.create(entry.recipe, *crafter);
                    break;
                }
            }
        } else if (verb == "breakdown" && parsed.positional.size() == 3) {
            done = false;
            for (const auto &item : bench.breakdownCandidates()) {
                if (std::to_string(item->id()) == parsed.positional[2] || boost::iequals(item->tag(), parsed.positional[2])) {
                    done = bench.breakDown(item, *crafter);
                    break;
                }
            }
        } else if (verb == "materials" && parsed.positional.size() == 4) {
            _party.setCraftingMaterials(static_cast<uint32_t>(std::stoi(parsed.positional[2])),
                                        static_cast<uint32_t>(std::stoi(parsed.positional[3])));
        } else if (verb != "status") {
            throw std::runtime_error("Usage: k2merge craft [status|list|create <template>|breakdown <item id>|materials <components> <chemicals>] [@crafter]");
        }
        report(str(boost::format("{\"craft\":\"%s\",\"done\":%d,\"components\":%d,\"chemicals\":%d}") %
                   verb % (done ? 1 : 0) % bench.components() % bench.chemicals()));
        return;
    }
    if (!parsed.positional.empty() && parsed.positional[0] == "prestige") {
        auto member = findConsoleActor(parsed.actor);
        if (!member) {
            throw std::runtime_error("Usage: k2merge prestige [@member]");
        }
        std::string offered;
        for (auto type : prestigeOffersFor(*member)) {
            offered += (offered.empty() ? "" : ",") + std::to_string(static_cast<int>(type));
        }
        std::string classes;
        for (const auto &[clazz, level] : member->attributes().classLevels()) {
            classes += (classes.empty() ? "" : ",") + str(boost::format("%d:%d") % static_cast<int>(clazz->type()) % level);
        }
        report(str(boost::format("{\"tag\":\"%s\",\"goodEvil\":%d,\"revealGoodEvil\":%d,\"offered\":[%s],\"classes\":\"%s\"}") %
                   member->tag() % member->goodEvil() % (getGlobalNumber(prestige::kRevealAlignmentGlobal) - 1) % offered % classes));
        return;
    }
    int appended = 0;
    int tables = 0;
    for (const auto &rule : resource::k2MergeRules()) {
        if (!parsed.positional.empty() && !boost::iequals(parsed.positional[0], rule.table)) {
            continue;
        }
        const auto *map = merge->mergeMap(rule.table);
        auto table = merge->get(rule.table);
        if (!map || !table) {
            report(str(boost::format("{\"table\":\"%s\",\"merged\":false}") % rule.table));
            continue;
        }
        ++tables;
        appended += map->appendedRows;
        report(str(boost::format("{\"table\":\"%s\",\"k1Rows\":%d,\"k2Rows\":%d,\"mergedRows\":%d,\"appended\":%d,\"rows\":%d,\"labelMismatches\":%d}") %
                   rule.table % map->k1Rows % map->k2Rows % map->mergedRows % map->appendedRows % table->getRowCount() % map->labelMismatches));
    }
    int grey = merge->ownRowIndex("feat", prestige::kGreyFeatLabel);
    report(str(boost::format("{\"k2merge\":true,\"tables\":%d,\"appendedRows\":%d,\"greyFeat\":%d,\"k2Routines\":%d}") %
               tables % appended % grey % (_k2Routines ? 1 : 0)));
}

// K2 Force forms: list, set, clear or grant.
void Game::consoleForm(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    auto member = findConsoleActor(parsed.actor);
    if (!member) {
        throw std::runtime_error("form: no creature");
    }
    std::string verb = parsed.positional.empty() ? "list" : boost::to_lower_copy(parsed.positional[0]);
    auto report = [this](const std::string &line) {
        _console.printLine(line);
        info(line);
    };
    if (verb == "set" || verb == "grant") {
        if (parsed.positional.size() != 2) {
            throw std::runtime_error("Usage: form set|grant <spell id> [@member]");
        }
        int form = std::stoi(parsed.positional[1]);
        if (!forms::isForm(form)) {
            throw std::runtime_error("form: not a form: " + parsed.positional[1]);
        }
        if (verb == "grant") {
            member->attributes().addSpell(static_cast<SpellType>(form));
        } else if (member->attributes().hasSpell(static_cast<SpellType>(form))) {
            member->setActiveForm(form);
        } else {
            throw std::runtime_error("form: " + member->tag() + " does not know form " + parsed.positional[1]);
        }
    } else if (verb == "clear") {
        member->setActiveForm(forms::kNoForm);
    } else if (verb != "list") {
        throw std::runtime_error("Usage: form [list|set <spell id>|clear|grant <spell id>] [@member]");
    }
    std::string known;
    for (int form = forms::kShiiCho; form <= forms::kMastery; ++form) {
        if (member->attributes().hasSpell(static_cast<SpellType>(form))) {
            known += (known.empty() ? "" : ",") + std::to_string(form);
        }
    }
    report(str(boost::format("{\"tag\":\"%s\",\"active\":%d,\"inEffect\":%d,\"known\":[%s]}") %
               member->tag() % member->activeForm() %
               (forms::isInEffect(member->activeForm(), member->activeForm(), member->wieldsLightsaber()) ? 1 : 0) % known));
}

namespace {

std::string itemLine(const Item &item) {
    return str(boost::format("%u %s %s x%d") % item.id() % item.tag() % item.blueprintResRef() % item.stackSize());
}

// An item in a list by id, tag or template, case-insensitively.
std::shared_ptr<Item> findListedItem(const std::vector<std::shared_ptr<Item>> &items, const std::string &key) {
    for (const auto &item : items) {
        if (std::to_string(item->id()) == key) {
            return item;
        }
    }
    for (const auto &item : items) {
        if (boost::iequals(item->tag(), key) || boost::iequals(item->blueprintResRef(), key)) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

void Game::consoleStore(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"markup", "markdown"});
    std::string verb = parsed.positional.empty() ? "status" : boost::to_lower_copy(parsed.positional[0]);
    auto report = [this](const std::string &line) {
        _console.printLine(line);
        info(line);
    };
    if (verb == "open") {
        if (parsed.positional.size() != 2) {
            throw std::runtime_error("Usage: store open <store tag|id> [markup=n] [markdown=n]");
        }
        auto store = dyn_cast<Store>(findConsoleObject(parsed.positional[1]));
        if (!store) {
            throw std::runtime_error("store: not a store: " + parsed.positional[1]);
        }
        int markUp = parsed.named.count("markup") ? std::stoi(parsed.named.at("markup")) : 0;
        int markDown = parsed.named.count("markdown") ? std::stoi(parsed.named.at("markdown")) : 0;
        // What OpenStore asks for, opened now instead of on the next tick.
        openStore(store, _party.getLeader(), markUp, markDown);
        if (_screen != Screen::Store) {
            throw std::runtime_error("store: the store screen did not open");
        }
        report(str(boost::format("store open %s markup=%d markdown=%d gold=%d") % store->tag() %
                   (store->markUp() + markUp) % (store->markDown() + markDown) % _party.gold()));
        return;
    }
    if (_screen != Screen::Store || !_store || !_storeSession.isOpen()) {
        throw std::runtime_error("store: no store is open");
    }
    auto store = _storeSession.store();
    if (verb == "status") {
        report(str(boost::format("store %s mode=%s gold=%d stock=%u sellable=%u") % store->tag() %
                   (_store->mode() == StoreGUI::Mode::Buy ? "buy" : "sell") % _party.gold() %
                   _storeSession.stock().size() % _storeSession.sellable().size()));
        return;
    }
    if (verb == "list") {
        bool sell = parsed.positional.size() > 1 && boost::iequals(parsed.positional[1], "sell");
        auto items = sell ? _storeSession.sellable() : _storeSession.stock();
        report(str(boost::format("store list %s count=%u") % (sell ? "sell" : "buy") % items.size()));
        for (const auto &item : items) {
            int price = sell ? _storeSession.sellPrice(*item) : _storeSession.buyPrice(*item);
            report(str(boost::format("  %s price=%d%s") % itemLine(*item) % price %
                       (item->isInfinite() ? " infinite" : "")));
        }
        return;
    }
    if (verb == "close") {
        closeStore();
        report("store closed");
        return;
    }
    if (verb == "mode") {
        // The Show Sell List / Show Buy List button.
        if (parsed.positional.size() != 2) {
            throw std::runtime_error("Usage: store mode buy|sell");
        }
        _store->setMode(boost::iequals(parsed.positional[1], "sell") ? StoreGUI::Mode::Sell : StoreGUI::Mode::Buy);
        report(std::string("store mode ") + (_store->mode() == StoreGUI::Mode::Buy ? "buy" : "sell"));
        return;
    }
    if (verb == "buy" || verb == "sell") {
        if (parsed.positional.size() < 2 || parsed.positional.size() > 3) {
            throw std::runtime_error("Usage: store buy|sell <item id|tag|template> [count]");
        }
        int count = parsed.positional.size() == 3 ? parsePositiveIntArg(parsed.positional[2], "count") : 1;
        bool buy = verb == "buy";
        // The screen's own path: switch list, select the item, press Accept.
        _store->setMode(buy ? StoreGUI::Mode::Buy : StoreGUI::Mode::Sell);
        int done = 0;
        int spent = 0;
        StoreSession::Result result = StoreSession::Result::Ok;
        std::string key = parsed.positional[1];
        for (int i = 0; i < count; ++i) {
            auto item = findListedItem(_store->shownItems(), key);
            if (!item || !_store->selectItem(item)) {
                result = StoreSession::Result::NoSuchItem;
                break;
            }
            int price = buy ? _storeSession.buyPrice(*item) : _storeSession.sellPrice(*item);
            result = _store->accept();
            if (result != StoreSession::Result::Ok) {
                break;
            }
            ++done;
            spent += price;
        }
        report(str(boost::format("store %s %s done=%d/%d %s=%d gold=%d result=%s") % verb % key % done % count %
                   (buy ? "paid" : "earned") % spent % _party.gold() % StoreSession::describe(result)));
        return;
    }
    throw std::runtime_error("Usage: store [status|open <tag>|list [buy|sell]|buy <item> [n]|sell <item> [n]|close]");
}

void Game::consoleUpgrade(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    auto report = [this](const std::string &line) {
        _console.printLine(line);
        info(line);
    };
    if (!_upgrade) {
        _upgrade = tryLoadGUI<UpgradeGUI>();
    }
    if (!_upgrade) {
        throw std::runtime_error("upgrade: the upgrade bench GUI did not load");
    }
    UpgradeBench &bench = _upgrade->bench();
    std::string verb = parsed.positional.empty() ? "list" : boost::to_lower_copy(parsed.positional[0]);
    auto describeItem = [&](const std::shared_ptr<Item> &item) {
        std::string line = itemLine(*item) + str(boost::format(" upgrades=0x%x slots:") % item->upgrades());
        UpgradeTable table;
        if (auto upgrades = _services.resource.twoDas.get("upgrade")) {
            table = UpgradeTable(*upgrades);
        }
        for (const auto &slot : upgradeSlotsFor(*item, item->upgrades(), table)) {
            line += slot.color ? str(boost::format(" color(%d)") % item->modelVariation())
                               : str(boost::format(" t%d=%d") % slot.type % slot.installedRow);
        }
        return line;
    };
    if (verb == "open") {
        // What ShowUpgradeScreen asks for, opened now: the bench, or one item.
        std::shared_ptr<Item> item;
        if (parsed.positional.size() == 2) {
            item = findListedItem(bench.partyItems(UpgradeCategory::None), parsed.positional[1]);
            if (!item) {
                throw std::runtime_error("upgrade: no upgradeable item " + parsed.positional[1]);
            }
        }
        openUpgradeScreen(item);
        report(std::string("upgrade open") + (item ? " " + item->tag() : std::string()));
        return;
    }
    if (verb == "category") {
        // A category button, then Upgrade Items.
        static const std::map<std::string, UpgradeCategory> kCategories {
            {"lightsaber", UpgradeCategory::Lightsaber}, {"ranged", UpgradeCategory::Ranged},
            {"melee", UpgradeCategory::Melee}, {"armor", UpgradeCategory::Armor}};
        auto it = parsed.positional.size() == 2 ? kCategories.find(boost::to_lower_copy(parsed.positional[1])) : kCategories.end();
        if (it == kCategories.end()) {
            throw std::runtime_error("Usage: upgrade category lightsaber|ranged|melee|armor");
        }
        if (_screen != Screen::Upgrade) {
            openUpgradeScreen(nullptr);
        }
        _upgrade->openCategory(it->second);
        report(str(boost::format("upgrade category %s items=%u") % it->first % bench.partyItems(it->second).size()));
        return;
    }
    if (verb == "craft") {
        // The K2 crafting page (K2 systems merge): create or break down.
        if (!_upgrade->craftingAvailable()) {
            throw std::runtime_error("upgrade craft: needs the K2 systems merge (--k2game)");
        }
        bool breakDown = parsed.positional.size() == 2 && boost::iequals(parsed.positional[1], "breakdown");
        if (_screen != Screen::Upgrade) {
            openUpgradeScreen(nullptr);
        }
        _upgrade->openCrafting(breakDown ? CraftingPanel::Mode::BreakDown : CraftingPanel::Mode::Create);
        report(std::string("upgrade craft ") + (breakDown ? "breakdown" : "create"));
        return;
    }
    if (verb == "list") {
        auto items = bench.partyItems(UpgradeCategory::None);
        report(str(boost::format("upgrade list count=%u") % items.size()));
        for (const auto &item : items) {
            report("  " + describeItem(item));
        }
        return;
    }
    bool remove = verb == "remove";
    size_t first = remove ? 1 : 0;
    if (parsed.positional.size() != first + 2) {
        throw std::runtime_error("Usage: upgrade [list|open [item]|<item> <upgrade>|remove <item> <upgrade>]");
    }
    auto item = findListedItem(bench.partyItems(UpgradeCategory::None), parsed.positional[first]);
    if (!item) {
        throw std::runtime_error("upgrade: no upgradeable item " + parsed.positional[first]);
    }
    std::string upgradeKey = boost::to_lower_copy(parsed.positional[first + 1]);
    // The screen's own path: open the item's slots, pick the slot, act on
    // the list row, then Assemble.
    bool opened = _screen == Screen::Upgrade;
    if (!opened) {
        openUpgradeScreen(nullptr);
    }
    _upgrade->openItem(item);
    auto slots = bench.slots();
    bool staged = false;
    for (int i = 0; i < static_cast<int>(slots.size()) && !staged; ++i) {
        if (remove) {
            const UpgradeKind *kind = bench.table().kind(slots[i].installedRow);
            if (kind && (kind->templateResRef == upgradeKey || std::to_string(kind->row) == upgradeKey)) {
                _upgrade->slotsPanel().selectSlot(i);
                staged = bench.remove(i);
            }
            continue;
        }
        auto candidate = findListedItem(bench.candidates(i), upgradeKey);
        if (candidate) {
            _upgrade->slotsPanel().selectSlot(i);
            staged = bench.install(i, candidate);
        }
    }
    bool applied = staged && assembleUpgrades();
    if (!opened) {
        closeUpgradeScreen();
    } else {
        _upgrade->back();
    }
    if (!applied) {
        throw std::runtime_error("upgrade: " + parsed.positional[first + 1] +
                                 (remove ? " is not installed on " : " does not fit ") + item->tag());
    }
    report(str(boost::format("upgrade %s %s -> %s") % (remove ? "remove" : "install") % upgradeKey % describeItem(item)));
}

void Game::consoleKey(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.empty() || parsed.positional.size() > 2) {
        throw std::runtime_error("Usage: key <name> [down|up]");
    }
    auto code = parseKeyName(parsed.positional[0]);
    if (code == input::KeyCode::Unknown) {
        throw std::runtime_error("key: unknown key " + parsed.positional[0]);
    }
    std::string phase = parsed.positional.size() > 1 ? boost::to_lower_copy(parsed.positional[1]) : "press";
    bool handled = false;
    if (phase == "press" || phase == "down") {
        handled = handle(input::Event::newKeyDown(input::KeyEvent(true, code, 0, false)));
    }
    if (phase == "press" || phase == "up") {
        handle(input::Event::newKeyUp(input::KeyEvent(false, code, 0, false)));
    }
    std::string line = str(boost::format("key %s handled=%d screen=%d") % keyName(code) % (handled ? 1 : 0) %
                           static_cast<int>(_screen));
    _console.printLine(line);
    info(line);
}

void Game::consolePointer(const ConsoleArgs &args) {
    // Move the pointer the way the mouse does; "middle" then clicks the middle button there.
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() < 2 || parsed.positional.size() > 3) {
        throw std::runtime_error("Usage: pointer <x> <y> [middle]");
    }
    int x = std::stoi(parsed.positional[0]);
    int y = std::stoi(parsed.positional[1]);
    handle(input::Event::newMouseMotion(input::MouseMotionEvent(x, y, 0.0f, 0.0f)));
    bool handled = false;
    if (parsed.positional.size() == 3) {
        if (boost::to_lower_copy(parsed.positional[2]) != "middle") {
            throw std::runtime_error("Usage: pointer <x> <y> [middle]");
        }
        handled = handle(input::Event::newMouseButtonDown(input::MouseButtonEvent(input::MouseButton::Middle, true, 1, x, y)));
        handle(input::Event::newMouseButtonUp(input::MouseButtonEvent(input::MouseButton::Middle, false, 1, x, y)));
    }
    std::string line = str(boost::format("pointer %d,%d handled=%d") % x % y % (handled ? 1 : 0));
    _console.printLine(line);
    info(line);
}

void Game::consoleGuiClick(const ConsoleArgs &args) {
    // Click a control of the screen's current page the way the mouse does:
    // move onto its centre, then press and release the left button. The
    // events go through Game::handle, so hit testing is the real one.
    HarnessArgs parsed(args);
    parsed.requireKnown({"clicks", "row", "gap"});
    if (parsed.positional.size() != 1) {
        throw std::runtime_error("Usage: guiclick <control tag> [clicks=1|2] [row=<n> [gap=<px>]]");
    }
    auto gui = getScreenGUI();
    if (!gui) {
        throw std::runtime_error("guiclick: the current screen has no GUI");
    }
    auto page = gui->inputPage();
    auto control = page->controlByTag(parsed.positional[0]);
    if (!control && _screen == Screen::InGameMenu && _inGame && _inGame->activeTab() == InGameMenuTab::Options &&
        _inGame->optionsMenu()) {
        // The Options page's own buttons (Co-op, and the Co-op page over it).
        page = _inGame->optionsMenu()->inputPage();
        control = page->controlByTag(parsed.positional[0]);
    }
    if (!control) {
        // An options panel open over the main menu or the in-game Options.
        OptionsPanels *panels = nullptr;
        if (_screen == Screen::MainMenu && _mainMenu) {
            panels = &_mainMenu->optionsPanels();
        } else if (_screen == Screen::InGameMenu && _inGame && _inGame->optionsMenu()) {
            panels = &_inGame->optionsMenu()->panels();
        }
        if (panels && panels->active()) {
            page = panels->active()->inputPage();
            control = page->controlByTag(parsed.positional[0]);
        }
    }
    if (!control && _screen == Screen::InGameMenu && _inGame && _inGame->activeTabPage()) {
        // The open tab's own controls (equipment slots, its buttons).
        page = _inGame->activeTabPage()->inputPage();
        control = page->controlByTag(parsed.positional[0]);
    }
    if (!control) {
        throw std::runtime_error("guiclick: no control " + parsed.positional[0]);
    }
    const auto &extent = control->extent();
    glm::ivec2 offset = page->guiControlOffset();
    int x = offset.x + extent.left + extent.width / 2;
    int y = offset.y + extent.top + extent.height / 2;
    if (auto it = parsed.named.find("row"); it != parsed.named.end()) {
        // A list box's n-th visible row (0 = first), for harness saves/loads.
        if (auto *list = dynamic_cast<gui::ListBox *>(control.get()); list && list->protoItemOrNull()) {
            int itemHeight = list->protoItemOrNull()->extent().height;
            int gap = 4;
            if (auto g = parsed.named.find("gap"); g != parsed.named.end()) {
                gap = std::stoi(g->second);
            }
            y = offset.y + extent.top + itemHeight / 2 + std::stoi(it->second) * (itemHeight + gap);
        }
    }
    int clicks = 1;
    if (auto it = parsed.named.find("clicks"); it != parsed.named.end()) {
        clicks = std::clamp(std::stoi(it->second), 1, 2);
    }
    handle(input::Event::newMouseMotion(input::MouseMotionEvent(x, y, 0.0f, 0.0f)));
    bool handled = false;
    for (int i = 1; i <= clicks; ++i) {
        handle(input::Event::newMouseButtonDown(input::MouseButtonEvent(input::MouseButton::Left, true, static_cast<uint8_t>(i), x, y)));
        handled = handle(input::Event::newMouseButtonUp(input::MouseButtonEvent(input::MouseButton::Left, false, static_cast<uint8_t>(i), x, y))) || handled;
    }
    std::string line = str(boost::format("guiclick %s at=%d,%d visible=%d handled=%d screen=%d") % parsed.positional[0] % x % y %
                           (control->isVisible() ? 1 : 0) % (handled ? 1 : 0) % static_cast<int>(_screen));
    _console.printLine(line);
    info(line);
}

void Game::consoleGuiDump(const ConsoleArgs &args) {
    // One line per named control of the current page: screen extent,
    // visibility and text, for layout checks.
    auto gui = getScreenGUI();
    if (!gui) {
        throw std::runtime_error("guidump: the current screen has no GUI");
    }
    auto page = gui->inputPage();
    // An options panel open over the main menu or the in-game Options is
    // what the screen shows.
    OptionsPanels *panels = nullptr;
    if (_screen == Screen::MainMenu && _mainMenu) {
        panels = &_mainMenu->optionsPanels();
    } else if (_screen == Screen::InGameMenu && _inGame && _inGame->optionsMenu()) {
        panels = &_inGame->optionsMenu()->panels();
    }
    if (panels && panels->active()) {
        page = panels->active()->inputPage();
    }
    glm::ivec2 offset = page->guiControlOffset();
    PresentationGUI *tabPage = _screen == Screen::InGameMenu && _inGame ? _inGame->activeTabPage() : nullptr;
    for (size_t i = 1; i < args.size(); ++i) {
        std::string tag(args[i].value());
        auto control = page->controlByTag(tag);
        if (!control && tabPage) {
            // The open tab's own controls.
            control = tabPage->inputPage()->controlByTag(tag);
            if (control) {
                const auto &e = control->extent();
                glm::ivec2 tabOffset = tabPage->inputPage()->guiControlOffset();
                _console.printLine(str(boost::format("%s at=%d,%d size=%dx%d visible=%d text=\"%s\"") % tag %
                                       (tabOffset.x + e.left) % (tabOffset.y + e.top) % e.width % e.height %
                                       (control->isVisible() ? 1 : 0) % control->text().text));
                continue;
            }
        }
        if (!control) {
            _console.printLine(tag + " missing");
            continue;
        }
        const auto &e = control->extent();
        _console.printLine(str(boost::format("%s at=%d,%d size=%dx%d visible=%d text=\"%s\"") % tag %
                               (offset.x + e.left) % (offset.y + e.top) % e.width % e.height %
                               (control->isVisible() ? 1 : 0) % control->text().text));
    }
}

void Game::consoleGuiShow(const ConsoleArgs &args) {
    if (args.size() != 3) {
        throw std::runtime_error("Usage: guishow <control tag> 0|1");
    }
    auto gui = getScreenGUI();
    auto control = gui ? gui->inputPage()->controlByTag(std::string(args[1].value())) : nullptr;
    if (!control) {
        throw std::runtime_error("guishow: no control " + std::string(args[1].value()));
    }
    control->setVisible(std::string(args[2].value()) != "0");
}

void Game::consoleShowLoadScreen(const ConsoleArgs &args) {
    if (args.size() > 2) {
        throw std::runtime_error("Usage: showloadscreen [image resref]");
    }
    std::string image = args.size() > 1 ? std::string(args[1].value()) : "load_chargen";
    // Presentation only: the screen stays up until another screen opens.
    withLoadingScreen(image, []() {});
    if (_loadScreen) {
        _loadScreen->setProgress(40);
    }
}

void Game::consoleOptions(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    std::string verb = parsed.positional.empty() ? "status" : boost::to_lower_copy(parsed.positional[0]);
    // Player settings (design note p1_ui): options set <key> <value> | get <key> | unset <key>.
    if (verb == "set" || verb == "get" || verb == "unset") {
        if (parsed.positional.size() != (verb == "set" ? 3u : 2u)) {
            throw std::runtime_error("Usage: options set <key> <value> | get <key> | unset <key>");
        }
        const std::string &key = parsed.positional[1];
        if (verb == "set") {
            _userSettings.set(key, parsed.positional[2]);
        } else if (verb == "unset") {
            _userSettings.erase(key);
        }
        if (verb != "get") {
            applyUserSettings(true);
        }
        std::string line = "options " + key + "=" + _userSettings.get(key).value_or("(default)") +
                           str(boost::format(" music=%d voice=%d sound=%d movie=%d difficulty=%d radius=%.1f fixedbar=%d") %
                               _options.audio.musicVolume % _options.audio.voiceVolume % _options.audio.soundVolume %
                               _options.audio.movieVolume % static_cast<int>(_options.game.clientDifficulty) %
                               _selectionRadius % (_fixedActionBar ? 1 : 0));
        _console.printLine(line);
        info(line);
        return;
    }
    // On the main menu, its own Options stack (design note p1_k2menus); else the in-game one.
    bool mainMenu = _screen == Screen::MainMenu && _mainMenu;
    if (!mainMenu && (!_inGame || !_inGame->optionsMenu())) {
        throw std::runtime_error("options: the in-game menu is not loaded");
    }
    OptionsPanels &panels = mainMenu ? _mainMenu->optionsPanels() : _inGame->optionsMenu()->panels();
    if (verb == "open") {
        // The in-game Options page, then a page button (or a sub-page's).
        if (parsed.positional.size() != 2) {
            throw std::runtime_error("Usage: options open <panel resref>");
        }
        auto panel = boost::to_lower_copy(parsed.positional[1]);
        // K2 menus name their panels with the PC suffix.
        if (hudPresentationId() == resource::GameID::TSL && !boost::ends_with(panel, "_p")) {
            panel += "_p";
        }
        if (mainMenu) {
            if (boost::starts_with(panel, "optionsmain")) {
                _mainMenu->openOptions();
                return;
            }
        } else if (_screen != Screen::InGameMenu || _inGame->activeTab() != InGameMenuTab::Options) {
            openInGameMenu(InGameMenuTab::Options);
        }
        if (!panels.open(panel)) {
            throw std::runtime_error("options: panel " + parsed.positional[1] + " did not load");
        }
    } else if (verb == "back") {
        panels.back();
    } else if (verb != "status") {
        throw std::runtime_error("Usage: options [status|open <panel>|back|set <key> <value>|get <key>|unset <key>]");
    }
    std::string line = "options panels:";
    for (const auto &panel : panels.stack()) {
        line += " " + panel;
    }
    _console.printLine(line);
    info(line);
}

void Game::consoleCredits(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    std::string verb = parsed.positional.empty() ? "status" : boost::to_lower_copy(parsed.positional[0]);
    if (verb == "start") {
        startCreditSequence(false);
    } else if (verb == "skip") {
        if (!isCreditSequenceInProgress()) {
            throw std::runtime_error("credits: the credits are not running");
        }
        // Escape's path.
        endCreditSequence();
    } else if (verb != "status") {
        throw std::runtime_error("Usage: credits [status|start|skip]");
    }
    std::string line = str(boost::format("credits running=%d line=%d/%d") % (isCreditSequenceInProgress() ? 1 : 0) %
                           (_credits ? _credits->offset() : 0) % (_credits ? _credits->lineCount() : 0));
    _console.printLine(line);
    info(line);
}

void Game::consoleTickGate(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() > 1 || parsed.actor) {
        throw std::runtime_error("Usage: tickgate [1|0]");
    }
    if (!parsed.positional.empty()) {
        setTickGate(parseBoolArg(parsed.positional[0], "closed"));
    }
    _console.printLine(str(boost::format("tickgate closed=%d tick=%u allowance=%u") %
                           (_tickGate ? 1 : 0) % _tickCount % _tickAllowance));
}

void Game::consoleStep(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 1 || parsed.actor) {
        throw std::runtime_error("Usage: step <ticks>");
    }
    int ticks = parsePositiveIntArg(parsed.positional[0], "ticks");
    _console.printLine(str(boost::format("step from=%u ticks=%d") % _tickCount % ticks));
    beginTickWait("step", _tickCount + static_cast<uint64_t>(ticks), nullptr);
}

void Game::consoleUntilTick(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 1 || parsed.actor) {
        throw std::runtime_error("Usage: untiltick <tick>");
    }
    uint64_t target = std::stoull(parsed.positional[0]);
    _console.printLine(str(boost::format("untiltick from=%u to=%u") % _tickCount % target));
    beginTickWait("untiltick", target, nullptr);
}

void Game::consoleStepIdle(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"maxticks"});
    if (parsed.positional.size() > 1) {
        throw std::runtime_error("Usage: stepidle [@id|tag] [maxticks=300]");
    }
    auto maxArg = parsed.value(0, "maxticks");
    int maxTicks = maxArg ? parsePositiveIntArg(*maxArg, "maxticks") : 300;
    auto actor = findConsoleActor(parsed.actor);
    uint32_t actorId = actor->id();
    _console.printLine(str(boost::format("stepidle id=%u from=%u maxticks=%d") % actorId % _tickCount % maxTicks));
    // The tick-counted waitidle: the condition is checked after every tick,
    // so the wait ends on the same tick whatever the frame rate.
    beginTickWait("stepidle", _tickCount + static_cast<uint64_t>(maxTicks), [this, actorId]() {
        auto creature = getObjectById<Creature>(actorId);
        return !creature || !firstPendingAction(*creature);
    });
}

void Game::consoleSpawnFollowers(const ConsoleArgs &args) {
    if (args.size() != 3) {
        throw std::runtime_error("Usage: spawnfollowers <template> <count>");
    }
    std::string res(args[1].value());
    int count = parsePositiveIntArg(std::string(args[2].value()), "count");
    auto area = getConsoleArea();
    auto leader = getConsoleLeader();
    // Stress fixture for co-op crowds: friendly characters outside the local
    // party (the co-op party-character flag), each force-following the
    // leader at the default follow distance. They are not party members, so
    // no heartbeat would re-issue a FollowLeader.
    std::vector<std::string> ids;
    for (int i = 0; i < count; ++i) {
        auto creature = newCreatureFromBlueprint(res);
        creature->setTag(str(boost::format("%s_f%d") % res % i));
        creature->setPosition(leader->position());
        creature->setFacing(leader->getFacing());
        creature->setFaction(leader->faction());
        creature->setCoopPartyCharacter(true);
        creature->setImmortal(_options.game.immortalPlayer);
        area->landObject(*creature);
        area->add(creature);
        area->regroupWithLeader(creature);
        creature->runSpawnScript();
        creature->addAction(newAction<FollowAction>(leader, kDefaultFollowDistance));
        ids.push_back(std::to_string(creature->id()));
    }
    std::string line = "spawnfollowers count=" + std::to_string(count) + " ids=" + boost::join(ids, ",");
    _console.printLine(line);
    info(line);
}

void Game::consoleSetVitals(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"hp", "fp"});
    auto hp = parsed.value(0, "hp");
    auto fp = parsed.value(1, "fp");
    if (!hp && !fp) {
        throw std::runtime_error("Usage: setvitals [hp=<n>] [fp=<n>] [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    // A harness input, like kill: sets the current values directly (within
    // 1..maximum for hit points, 0..maximum for force points).
    if (hp) {
        int value = std::stoi(*hp);
        actor->setCurrentHitPoints(std::clamp(value, 1, std::max(1, static_cast<int>(actor->maxHitPoints()))));
    }
    if (fp) {
        actor->setCurrentForcePoints(std::stoi(*fp));
    }
    _console.printLine(str(boost::format("setvitals %s hp=%d/%d fp=%d/%d") % actor->tag() %
                           actor->currentHitPoints() % actor->maxHitPoints() %
                           actor->currentForce() % actor->maxForcePoints()));
}

void Game::consoleSetAlign(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"value"});
    auto value = parsed.value(0, "value");
    if (!value) {
        throw std::runtime_error("Usage: setalign <0-100> [@id|tag]");
    }
    auto actor = findConsoleActor(parsed.actor);
    // A harness input: sets the value directly, with no shift feedback.
    actor->setGoodEvil(std::stoi(*value));
    _console.printLine(str(boost::format("setalign %s %d") % actor->tag() % actor->goodEvil()));
}

void Game::consoleTickStatus(const ConsoleArgs &args) {
    if (args.size() != 1) {
        throw std::runtime_error("Usage: tickstatus");
    }
    _console.printLine(str(boost::format("tick=%u rate=%d gate=%d allowance=%u waiting=%d last=%s dropped=%u rng.gameplay.seed=%u rng.gameplay.draws=%u rng.violations=%u") %
                           _tickCount %
                           _simClock.tickRate() %
                           (_tickGate ? 1 : 0) %
                           _tickAllowance %
                           (_tickWait.finished ? 0 : 1) %
                           (_tickWait.label.empty() ? std::string("none") : _tickWait.label) %
                           _simClock.droppedSteps() %
                           gameplayRandom().seedValue() %
                           gameplayRandom().draws() %
                           gameplayRandomViolations()));
}

// Render interpolation (design note render_interp). Presentation only: the
// trace reads where the frame drew an object, never gameplay state.
void Game::consoleInterp(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    const auto &pos = parsed.positional;
    std::string sub = pos.empty() ? std::string("status") : pos[0];
    if (sub == "0" || sub == "1") {
        _options.game.interpolate = sub == "1";
        if (!_options.game.interpolate) {
            scene::interp::restore();
        }
    } else if (sub == "headless") {
        // Captures of the path a player's window takes: blend between ticks
        // in a headless run (and with the tick gate closed).
        if (pos.size() != 2 || (pos[1] != "0" && pos[1] != "1")) {
            throw std::runtime_error("Usage: interp headless 0|1");
        }
        _interpHeadless = pos[1] == "1";
    } else if (sub == "trace") {
        if (pos.size() != 2) {
            throw std::runtime_error("Usage: interp trace <frames> [@id|tag]");
        }
        auto actor = findConsoleActor(parsed.actor);
        _interpTraceObject = actor->id();
        _interpTraceLimit = static_cast<size_t>(parsePositiveIntArg(pos[1], "frames"));
        _interpTrace.clear();
        _interpTrace.reserve(_interpTraceLimit);
    } else if (sub == "dump") {
        std::ostringstream out;
        out << "{\"object\":" << _interpTraceObject << ",\"samples\":[";
        out.precision(15);
        for (size_t i = 0; i < _interpTrace.size(); ++i) {
            const auto &t = _interpTrace[i];
            out << (i ? "," : "") << "[" << t.tick << "," << t.alpha << "," << t.time << ","
                << t.drawn.x << "," << t.drawn.y << "," << t.drawn.z << ","
                << t.simulated.x << "," << t.simulated.y << "," << t.simulated.z << "]";
        }
        out << "]}";
        _console.printLine(out.str());
        return;
    } else if (sub != "status") {
        throw std::runtime_error("Usage: interp [status|0|1|headless 0|1|trace <frames> [@id|tag]|dump]");
    }
    const auto &stats = scene::interp::stats();
    _console.printLine(str(boost::format("interp on=%d alpha=%.3f headless=%d gate=%d registered=%d blended=%d snapped=%d roots=%d cost.frames=%u cost.avg_us=%.1f trace=%u/%u") %
                           (_options.game.interpolate ? 1 : 0) %
                           presentationAlpha() %
                           (_options.graphics.headless ? 1 : 0) %
                           (_tickGate ? 1 : 0) %
                           stats.registered % stats.blended % stats.snapped % stats.roots %
                           stats.frames % (stats.frames ? stats.microseconds / static_cast<double>(stats.frames) : 0.0) %
                           _interpTrace.size() % _interpTraceLimit));
}

void Game::recordInterpTrace(float alpha) {
    if (_interpTrace.size() >= _interpTraceLimit) {
        return;
    }
    auto object = getObjectById(_interpTraceObject);
    if (!object || !object->sceneNode()) {
        return;
    }
    InterpTraceSample sample;
    sample.tick = _tickCount;
    sample.alpha = alpha;
    sample.time = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    sample.drawn = object->sceneNode()->origin();
    sample.simulated = object->position();
    _interpTrace.push_back(sample);
}

// Minigames and Pazaak. The minigame commands write the same input seam the
// keyboard and mouse handlers write (Game::miniGameInput), so a harness run
// plays through exactly the code a player does. design note minigames

namespace {

std::string jsonVec3(const glm::vec3 &v) {
    return str(boost::format("[%.3f,%.3f,%.3f]") % v.x % v.y % v.z);
}

std::string jsonString(const std::string &s) {
    std::string out("\"");
    for (char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

float parseUnitArg(const std::string &value, const char *name) {
    return glm::clamp(parseFloatArg(value, name), -1.0f, 1.0f);
}

bool parseOnOff(const HarnessArgs &parsed, bool current) {
    if (parsed.positional.empty()) {
        return !current;
    }
    const auto &v = parsed.positional.front();
    if (v == "on") {
        return true;
    }
    if (v == "off") {
        return false;
    }
    return parseBoolArg(v, "state");
}

const char *pazaakScreenName(PazaakFlowScreen screen) {
    switch (screen) {
    case PazaakFlowScreen::Wager:
        return "wager";
    case PazaakFlowScreen::Setup:
        return "setup";
    default:
        return "board";
    }
}

std::string pazaakCardText(const pazaak::CardDefinition &card) {
    switch (card.behavior()) {
    case pazaak::CardBehavior::FixedPositive:
        return "+" + std::to_string(card.magnitude());
    case pazaak::CardBehavior::FixedNegative:
        return "-" + std::to_string(card.magnitude());
    case pazaak::CardBehavior::SignSelectable:
        return "+-" + std::to_string(card.magnitude());
    default:
        return "special" + std::to_string(card.magnitude());
    }
}

} // namespace

void Game::consoleSetGlobal(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (parsed.positional.size() != 3 || (parsed.positional[0] != "number" && parsed.positional[0] != "boolean")) {
        throw std::runtime_error("Usage: setglobal number|boolean <name> <value>");
    }
    const auto &name = parsed.positional[1];
    int value = 0;
    const auto &text = parsed.positional[2];
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc {}) {
        throw std::runtime_error("value must be an integer");
    }
    if (parsed.positional[0] == "number") {
        setGlobalNumber(name, value);
    } else {
        setGlobalBoolean(name, value != 0);
    }
    _console.printLine(str(boost::format("setglobal %s %s=%d") % parsed.positional[0] % name % value));
}

void Game::consoleMiniGameState(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!_miniGame) {
        _console.printLine("{\"active\":false}");
        return;
    }
    auto &session = *_miniGame;
    auto player = session.player();
    int alive = 0;
    for (const auto &actor : session.actors()) {
        if (actor.kind == MiniGameActorKind::Enemy && !actor.dead) {
            ++alive;
        }
    }
    std::string globals;
    static const char *kNumbers[] = {"MIN_RACE_GEAR", "TAR_SWOOP_MIN", "TAR_SWOOP_SEC", "TAR_SWOOP_MSEC",
                                     "TAT_SWOOP_MIN", "TAT_SWOOP_SEC", "TAT_SWOOP_MSEC",
                                     "MAN_SWOOP_MIN", "MAN_SWOOP_SEC", "MAN_SWOOP_MSEC",
                                     "TAR_SWOOP_MIN_BEAT", "TAR_SWOOP_SEC_BEAT", "TAR_SWOOP_MSEC_BEAT",
                                     "Tar_SwoopRaceCounter", "ebo_num_fighters"};
    for (const char *name : kNumbers) {
        globals += str(boost::format("%s\"%s\":%d") % (globals.empty() ? "" : ",") % name % getGlobalNumber(name));
    }
    static const char *kBooleans[] = {"TAR_SWOOP_RUN", "ebo_turret_done", "M12AB_END_SYNC"};
    for (const char *name : kBooleans) {
        globals += str(boost::format(",\"%s\":%d") % name % static_cast<int>(getGlobalBoolean(name)));
    }
    std::string turret;
    if (_turret.isActive()) {
        auto outcome = _turret.outcome();
        turret = str(boost::format(",\"turret\":{\"outcome\":\"%s\",\"shots\":%zu,\"hits\":%zu,\"hitsTaken\":%zu,\"bullets\":%zu,\"pitch\":%.2f,\"yaw\":%.2f}")
                     % turretOutcomeName(outcome) % _turret.shotsFired() % _turret.hitsLanded() % _turret.hitsTaken()
                     % _turret.bulletCount() % glm::degrees(_turret.aim().pitch()) % glm::degrees(_turret.aim().yaw()));
    }
    std::string line = str(boost::format(
                               "{\"active\":true,\"type\":\"%s\",\"module\":%s,\"tick\":%llu,\"elapsed\":%.3f,\"scripts\":%zu,"
                               "\"player\":{\"id\":%u,\"pos\":%s,\"speed\":%.3f,\"maxSpeed\":%.3f,\"minSpeed\":%.3f,\"accel\":%.3f,"
                               "\"offset\":%s,\"tunnelPos\":%s,\"tunnelNeg\":%s,\"hp\":%d,\"maxHp\":%d,\"invulnerable\":%d,\"dead\":%d},"
                               "\"lateralAccel\":%.3f,\"blur\":[%d,%.3f],\"enemies\":%d,\"enemiesAlive\":%d,\"obstacles\":%d,"
                               "\"input\":{\"steer\":%.3f,\"fire\":%d},\"autopilot\":{\"swoop\":%d,\"turret\":%d},\"globals\":{%s}%s}")
                           % minigameTypeName(session.type()) % jsonString(_miniGameModule)
                           % static_cast<unsigned long long>(session.tickCount()) % session.elapsed() % session.scriptsRun()
                           % (player ? player->objectId : 0) % jsonVec3(player ? player->position : glm::vec3(0.0f))
                           % session.playerSpeed() % session.maxSpeed() % session.minSpeed() % session.accelPerSecond()
                           % jsonVec3(session.offset()) % jsonVec3(session.tunnelPos()) % jsonVec3(session.tunnelNeg())
                           % (player ? player->hitPoints : 0) % (player ? player->maxHitPoints : 0)
                           % static_cast<int>(player && player->invulnerable()) % static_cast<int>(player && player->dead)
                           % session.lateralAccel() % static_cast<int>(session.speedBlurEnabled()) % session.speedBlurRatio()
                           % session.enemyCount() % alive % session.obstacleCount()
                           % _miniGameInput.steer % static_cast<int>(_miniGameInput.fire)
                           % static_cast<int>(_swoopAutopilot) % static_cast<int>(_turretAutopilot)
                           % globals % turret);
    // A co-op client draws the host's minigame: say so, with the playback's state.
    line.pop_back();
    line += str(boost::format(",\"replica\":%d") % static_cast<int>(_miniGameReplica));
    if (_coop) {
        line += ",\"coop\":" + boost::json::serialize(_coop->miniGameJson());
    }
    line += "}";
    _console.printLine(line);
    info(line);
}

void Game::consoleMiniGameInputCommand(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({"steer", "fire", "pitch", "yaw", "dpitch", "dyaw"});
    if (!_miniGame) {
        throw std::runtime_error("No minigame is running");
    }
    auto &in = _miniGameInput;
    if (auto v = parsed.value(99, "steer")) {
        in.steer = parseUnitArg(*v, "steer");
    }
    if (auto v = parsed.value(99, "fire")) {
        in.fire = parseBoolArg(*v, "fire");
    }
    if (auto v = parsed.value(99, "pitch")) {
        in.pitchRate = parseUnitArg(*v, "pitch");
    }
    if (auto v = parsed.value(99, "yaw")) {
        in.yawRate = parseUnitArg(*v, "yaw");
    }
    if (auto v = parsed.value(99, "dpitch")) {
        in.pitchDelta += parseFloatArg(*v, "dpitch");
    }
    if (auto v = parsed.value(99, "dyaw")) {
        in.yawDelta += parseFloatArg(*v, "dyaw");
    }
    _console.printLine(str(boost::format("mginput steer=%.3f fire=%d pitch=%.2f yaw=%.2f dpitch=%.2f dyaw=%.2f")
                           % in.steer % static_cast<int>(in.fire) % in.pitchRate % in.yawRate % in.pitchDelta % in.yawDelta));
}

void Game::consoleInstantDoors(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    _instantDoorTransitions = parseOnOff(parsed, _instantDoorTransitions);
    std::string line = str(boost::format("instantdoors %d") % static_cast<int>(_instantDoorTransitions));
    _console.printLine(line);
    info(line);
}

void Game::consoleSwoopAuto(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    _swoopAutopilot = parseOnOff(parsed, _swoopAutopilot);
    if (!_swoopAutopilot) {
        _miniGameInput.steer = 0.0f;
        _miniGameInput.fire = false;
    }
    _console.printLine(str(boost::format("swoopauto %d") % static_cast<int>(_swoopAutopilot)));
}

void Game::consoleTurretAuto(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    _turretAutopilot = parseOnOff(parsed, _turretAutopilot);
    if (!_turretAutopilot) {
        _miniGameInput.fire = false;
        _miniGameInput.pitchDelta = 0.0f;
        _miniGameInput.yawDelta = 0.0f;
    }
    _console.printLine(str(boost::format("turretauto %d") % static_cast<int>(_turretAutopilot)));
}

void Game::consolePazaakState(const ConsoleArgs &args) {
    if (!_pazaakSession) {
        std::string line = str(boost::format("{\"active\":false,\"lastResult\":%d}")
                               % (_lastPazaakResult ? static_cast<int>(*_lastPazaakResult == PazaakCompletedResult::PlayerWon) : -1));
        _console.printLine(line);
        return;
    }
    auto &session = *_pazaakSession;
    std::string hand;
    std::string board;
    if (session.screen() == PazaakFlowScreen::Board && session.match()) {
        auto projection = session.boardProjection();
        for (size_t i = 0; i < projection.playerHand.size(); ++i) {
            const auto &slot = projection.playerHand[i];
            hand += str(boost::format("%s{\"i\":%zu,\"card\":%s,\"used\":%d,\"playable\":%d}")
                        % (hand.empty() ? "" : ",") % i
                        % jsonString(slot.occupied && slot.definition ? pazaakCardText(*slot.definition) : "")
                        % static_cast<int>(slot.used) % static_cast<int>(slot.playable));
        }
        board = str(boost::format(",\"player\":{\"total\":%d,\"sets\":%d,\"stood\":%d,\"active\":%d},"
                                  "\"opponent\":{\"total\":%d,\"sets\":%d,\"stood\":%d,\"active\":%d},\"canEndTurn\":%d,\"canStand\":%d")
                    % projection.playerTotal % static_cast<int>(projection.playerSetWins) % static_cast<int>(projection.playerStood)
                    % static_cast<int>(projection.playerActive)
                    % projection.opponentTotal % static_cast<int>(projection.opponentSetWins) % static_cast<int>(projection.opponentStood)
                    % static_cast<int>(projection.opponentActive)
                    % static_cast<int>(projection.canEndTurn) % static_cast<int>(projection.canStand));
    }
    std::string line = str(boost::format("{\"active\":true,\"screen\":\"%s\",\"wager\":%d,\"wagerLimit\":%d,\"credits\":%d,"
                                         "\"chosen\":%zu,\"collection\":%zu,\"hand\":[%s]%s,\"completed\":%d}")
                           % pazaakScreenName(session.screen()) % session.wager() % session.wagerLimit() % _party.gold()
                           % session.chosenCards().size() % session.collection().size() % hand % board
                           % static_cast<int>(session.completedResult().has_value()));
    _console.printLine(line);
    info(line);
}

void Game::consolePazaakWager(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Wager) {
        throw std::runtime_error("No Pazaak wager screen is open");
    }
    auto &session = *_pazaakSession;
    if (!parsed.positional.empty() && parsed.positional.front() != "confirm") {
        int target = parsePositiveIntArg(parsed.positional.front(), "credits");
        // The wager screen steps the amount up and down; do the same.
        for (int guard = 0; guard < 100000 && session.wager() < target; ++guard) {
            int before = session.wager();
            session.increaseWager();
            if (session.wager() == before) {
                break;
            }
        }
        for (int guard = 0; guard < 100000 && session.wager() > target; ++guard) {
            int before = session.wager();
            session.decreaseWager();
            if (session.wager() == before) {
                break;
            }
        }
    }
    int wager = session.wager();
    if (!session.confirmWager()) {
        throw std::runtime_error("Pazaak wager was not accepted");
    }
    showPazaakSetup();
    _console.printLine(str(boost::format("pazaakwager %d") % wager));
}

void Game::consolePazaakDeck(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Setup) {
        throw std::runtime_error("No Pazaak side-deck screen is open");
    }
    auto &session = *_pazaakSession;
    bool fill = parsed.positional.empty() || parsed.positional.front() == "auto";
    if (fill) {
        // Fill the deck as the setup screen's cards are clicked: the owned
        // cards in collection order until ten are chosen.
        for (size_t i = 0; i < session.collection().size() && !session.canConfirmSetup(); ++i) {
            while (session.remainingCopies(i) > 0 && !session.canConfirmSetup()) {
                if (!session.selectCard(i)) {
                    break;
                }
            }
        }
    }
    if (!session.confirmSetup()) {
        throw std::runtime_error("Pazaak side deck was not accepted");
    }
    showPazaakBoard();
    _console.printLine(str(boost::format("pazaakdeck chosen=%zu") % session.chosenCards().size()));
}

namespace {

std::string pazaakErrorText(pazaak::ActionError error) {
    return error == pazaak::ActionError::None ? "ok" : str(boost::format("error%d") % static_cast<int>(error));
}

} // namespace

void Game::consolePazaakPlay(const ConsoleArgs &args) {
    HarnessArgs parsed(args);
    parsed.requireKnown({});
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Board) {
        throw std::runtime_error("No Pazaak board is open");
    }
    if (parsed.positional.empty()) {
        throw std::runtime_error("Usage: pazaakplay <hand index> [plus|minus]");
    }
    int index = 0;
    const auto &value = parsed.positional.front();
    auto result = std::from_chars(value.data(), value.data() + value.size(), index);
    if (result.ec != std::errc {} || index < 0) {
        throw std::runtime_error("hand index must be 0 or more");
    }
    if (parsed.positional.size() > 1) {
        const auto &sign = parsed.positional[1];
        _pazaakSession->selectPlayerCardSign(static_cast<size_t>(index),
                                             sign == "minus" ? pazaak::CardSign::Negative : pazaak::CardSign::Positive);
    }
    auto error = _pazaakSession->playPlayerHandCard(static_cast<size_t>(index));
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    if (error == pazaak::ActionError::None) {
        completePazaakIfReady();
    }
    _console.printLine("pazaakplay " + pazaakErrorText(error));
}

void Game::consolePazaakEnd(const ConsoleArgs &args) {
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Board) {
        throw std::runtime_error("No Pazaak board is open");
    }
    auto error = _pazaakSession->endPlayerTurn();
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    if (error == pazaak::ActionError::None) {
        completePazaakIfReady();
    }
    _console.printLine("pazaakend " + pazaakErrorText(error));
}

void Game::consolePazaakStand(const ConsoleArgs &args) {
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Board) {
        throw std::runtime_error("No Pazaak board is open");
    }
    auto error = _pazaakSession->standPlayer();
    if (_pazaakBoard) {
        _pazaakBoard->refresh();
    }
    if (error == pazaak::ActionError::None) {
        completePazaakIfReady();
    }
    _console.printLine("pazaakstand " + pazaakErrorText(error));
}

void Game::consolePazaakForfeit(const ConsoleArgs &args) {
    if (!_pazaakSession || _pazaakSession->screen() != PazaakFlowScreen::Board) {
        throw std::runtime_error("No Pazaak board is open");
    }
    bool ok = _pazaakSession->requestForfeit() && _pazaakSession->confirmForfeit();
    completePazaakIfReady();
    _console.printLine(str(boost::format("pazaakforfeit %d") % static_cast<int>(ok)));
}

} // namespace game

} // namespace reone
