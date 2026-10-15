/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopcommand.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>

#include <boost/algorithm/string.hpp>

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/trap.h"
#include "reone/game/traps.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/useskill.h"
#include "reone/game/d20/spells.h"
#include "reone/game/game.h"
#include "reone/game/script/runner.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/resource/dialog.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/scripts.h"
#include "reone/script/program.h"
#include "reone/script/routine.h"
#include "reone/script/routines.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/scene/graphs.h"

using namespace reone::net;
using reone::resource::Dialog;

namespace reone {

namespace game {

namespace {

/** Screens on which the world runs and a player may act (menus do not pause co-op). */
bool acceptsInput(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::InGame:
    case Game::Screen::InGameMenu:
    case Game::Screen::Container:
    case Game::Screen::Store:
    case Game::Screen::Upgrade:
        return true;
    default:
        return false;
    }
}

bool inArea(const Area &area, const Object &object) {
    const auto &objects = area.objects();
    return std::any_of(objects.begin(), objects.end(), [&object](const auto &candidate) {
        return candidate.get() == &object;
    });
}

float distance2d(const glm::vec3 &a, const glm::vec3 &b) {
    return glm::distance(glm::vec2(a), glm::vec2(b));
}


template <class List>
auto findItemSpell(const List &list, const std::string &which) {
    return std::find_if(list.begin(), list.end(), [&which](const auto &entry) {
        return entry.first && (boost::iequals(entry.first->tag(), which) || boost::iequals(entry.first->blueprintResRef(), which));
    });
}

/** The item and the spell it casts, from the healing or attacking slot. */
bool resolveItemSpell(const Creature &actor, const std::string &which,
                      std::shared_ptr<Item> &item, std::shared_ptr<Spell> &spell);

/**
 * An item by its host id: the actor's own, or one in the shared party
 * inventory (design item 4, Story Mode) for the party's creatures and the
 * players' stand-ins. It must have an Activate Item power.
 */
bool resolveItemById(Game &game, const Creature &actor, uint32_t itemId, bool shared,
                     std::shared_ptr<Item> &item, std::shared_ptr<Spell> &spell) {
    item = game.getObjectById<Item>(itemId);
    if (!item || !item->isRuntimeLive()) {
        return false;
    }
    auto holds = [&item](const Object &owner) {
        const auto &items = owner.items();
        return std::find(items.begin(), items.end(), item) != items.end();
    };
    auto owner = game.party().actualPlayer();
    if (!holds(actor) && !(shared && owner && holds(*owner))) {
        return false;
    }
    auto type = item->activateSpell();
    spell = type ? game.services().game.spells.get(*type) : nullptr;
    return spell != nullptr;
}

bool resolveItemSpell(const Creature &actor, const std::string &which,
                      std::shared_ptr<Item> &item, std::shared_ptr<Spell> &spell) {
    if (which.empty()) {
        return false;
    }
    const auto &healing = actor.itemAttributes().healingSpells();
    auto it = findItemSpell(healing, which);
    if (it != healing.end()) {
        item = it->first;
        spell = it->second;
        return true;
    }
    const auto &attacking = actor.itemAttributes().attackingSpells();
    auto at = findItemSpell(attacking, which);
    if (at != attacking.end()) {
        item = at->first;
        spell = at->second;
        return true;
    }
    return false;
}

std::optional<glm::vec3> floorUnder(Game &game, ServicesView &services, const glm::vec3 &point, float actorZ) {
    auto &sceneGraph = services.scene.graphs.get(kSceneMain);
    scene::Collision collision;
    glm::vec3 nearProbe(point.x, point.y, actorZ + 1.0f);
    glm::vec3 topProbe(point.x, point.y, actorZ + scene::kElevationTestZ);
    if (sceneGraph.testElevation(nearProbe, collision) || sceneGraph.testElevation(topProbe, collision)) {
        return glm::vec3(point.x, point.y, collision.intersection.z);
    }
    return std::nullopt;
}

} // namespace

net::CommandReject validateCoopCommand(
    Game &game,
    const net::Command &command,
    uint32_t controlledId,
    CoopCommandTargets &targets,
    bool standIn,
    bool ownItemsOnly) {

    targets = CoopCommandTargets();
    if (controlledId == 0) {
        return CommandReject::NoSlot;
    }
    if (command.actor != controlledId) {
        return CommandReject::NotControlled;
    }
    auto module = game.module();
    if (!module || !module->area() || game.movie() || !acceptsInput(game.currentScreen())) {
        return CommandReject::Busy;
    }
    auto &area = *module->area();
    auto actor = game.getObjectById<Creature>(controlledId);
    if (!actor || !actor->isRuntimeLive() || actor->isDead() || !inArea(area, *actor) ||
        !(standIn || game.party().isMember(*actor))) {
        return CommandReject::ActorUnavailable;
    }
    if (!actor->isCommandable()) {
        // A script took the creature for a scene (SetCommandable FALSE).
        return CommandReject::Busy;
    }
    targets.actor = actor;

    auto resolveTarget = [&](bool required) -> CommandReject {
        if (command.target == 0) {
            return required ? CommandReject::TargetInvalid : CommandReject::None;
        }
        auto target = game.getObjectById(command.target);
        if (!target || !target->isRuntimeLive() || !inArea(area, *target)) {
            return CommandReject::TargetInvalid;
        }
        if (distance2d(actor->position(), target->position()) > kCoopMaxCommandRange) {
            return CommandReject::TargetTooFar;
        }
        targets.target = std::move(target);
        return CommandReject::None;
    };

    switch (command.type) {
    case CommandType::Cancel:
        // param 0: clear the queue; 1: Cancel Combat; 2: clear the queued action.
        return command.param >= 0 && command.param <= 2 ? CommandReject::None : CommandReject::PointInvalid;

    case CommandType::SwitchWeapons:
        return CommandReject::None;

    case CommandType::AutoCombat:
        return command.param == 0 || command.param == 1 ? CommandReject::None : CommandReject::PointInvalid;

    case CommandType::SetMine: {
        std::shared_ptr<Item> item;
        std::shared_ptr<Spell> spell;
        bool shared = !ownItemsOnly && (standIn || game.party().isMember(*actor));
        item = command.param > 0 ? game.getObjectById<Item>(static_cast<uint32_t>(command.param)) : nullptr;
        auto holds = [&item](const Object &owner) {
            const auto &items = owner.items();
            return item && std::find(items.begin(), items.end(), item) != items.end();
        };
        auto owner = game.party().actualPlayer();
        if (!item || mineTrapRow(*item) < 0 || !(holds(*actor) || (shared && owner && holds(*owner)))) {
            return CommandReject::ItemUnavailable;
        }
        glm::vec3 point(command.point.x, command.point.y, command.point.z);
        if (distance2d(actor->position(), point) > 5.0f) {
            return CommandReject::PointInvalid;
        }
        return CommandReject::None;
    }

    case CommandType::MoveTo: {
        glm::vec3 point(command.point.x, command.point.y, command.point.z);
        if (distance2d(actor->position(), point) > kCoopMaxCommandRange) {
            return CommandReject::PointInvalid;
        }
        return CommandReject::None;
    }

    case CommandType::Attack: {
        auto reason = resolveTarget(true);
        if (reason != CommandReject::None) {
            return reason;
        }
        auto creature = std::dynamic_pointer_cast<Creature>(targets.target);
        if (!creature || creature == actor || creature->isDead() || game.party().isMember(*creature) || creature->isPuppet()) {
            return CommandReject::TargetInvalid;
        }
        return CommandReject::None;
    }

    case CommandType::OpenDoor:
    case CommandType::UseObject: {
        auto reason = resolveTarget(true);
        if (reason != CommandReject::None) {
            return reason;
        }
        if (auto door = std::dynamic_pointer_cast<Door>(targets.target)) {
            if (door->isStatic()) {
                return CommandReject::TargetInvalid;
            }
            // An area exit: the whole party's ready check (design item 8).
            if (!door->linkedToModule().empty() && door->getOnOpen().empty()) {
                targets.use = CoopUse::Transition;
            }
            return CommandReject::None;
        }
        if (command.type == CommandType::OpenDoor) {
            return CommandReject::TargetInvalid;
        }
        auto talk = [&](const Object &object) {
            // Design, "Store and minigame prompts": the host leads
            // conversations; a store or minigame NPC offers its own prompt.
            if (coopDialogOffersStoreOrMinigame(game, object.conversation())) {
                return CommandReject::StoreUnavailable;
            }
            return CommandReject::HostLeadsConversation;
        };
        if (auto creature = std::dynamic_pointer_cast<Creature>(targets.target)) {
            if (creature->isDead()) {
                // Loot on the body (design items 14-15).
                bool loot = std::any_of(creature->items().begin(), creature->items().end(), [](const auto &item) {
                    return item && item->isDropable();
                });
                if (!loot) {
                    return CommandReject::TargetInvalid;
                }
                targets.use = CoopUse::Container;
                return CommandReject::None;
            }
            if (!game.party().isMember(*creature) && game.services().game.reputes.getIsEnemy(*actor, *creature)) {
                return CommandReject::None; // using a hostile is attacking it
            }
            if (!creature->conversation().empty()) {
                return talk(*creature);
            }
            return CommandReject::NotSupported;
        }
        if (auto placeable = std::dynamic_pointer_cast<Placeable>(targets.target)) {
            if (placeable->hasInventory()) {
                if (placeable->isLocked()) {
                    return CommandReject::TargetInvalid; // open it with Security first
                }
                targets.use = CoopUse::Container;
                return CommandReject::None;
            }
            // A workbench or lab opens a screen on whoever runs its script: the host's. A guest
            // gets its own bench view instead (the script is not run for it). Checked before
            // the conversation: K1's Taris workbench talks once before it opens the screen.
            if (placeable->isUsable() && !placeable->getOnUsed().empty() &&
                coopScriptBench(game, placeable->getOnUsed()) != CoopBench::None) {
                targets.use = CoopUse::Bench;
                return CommandReject::None;
            }
            if (!placeable->conversation().empty()) {
                return talk(*placeable);
            }
            // A console with no conversation (the terminal design): its
            // OnUsed script decides whether it is a story use.
            if (placeable->isUsable() && !placeable->getOnUsed().empty()) {
                if (coopScriptOpensScreen(game, placeable->getOnUsed())) {
                    return CommandReject::NotSupported;
                }
                targets.use = CoopUse::Placeable;
                targets.story = coopScriptChangesStory(game, placeable->getOnUsed());
                return CommandReject::None;
            }
        }
        return CommandReject::NotSupported;
    }

    case CommandType::UseItem: {
        std::shared_ptr<Item> item;
        std::shared_ptr<Spell> spell;
        // Free Mode guests use only their own items (ownItemsOnly).
        bool shared = !ownItemsOnly && (standIn || game.party().isMember(*actor));
        bool found = command.param > 0 ? resolveItemById(game, *actor, static_cast<uint32_t>(command.param), shared, item, spell)
                                       : resolveItemSpell(*actor, command.item, item, spell);
        if (!found) {
            return CommandReject::ItemUnavailable;
        }
        return resolveTarget(false);
    }

    case CommandType::UseFeat: {
        if (command.param < 0 || !actor->attributes().hasFeat(static_cast<FeatType>(command.param))) {
            return CommandReject::FeatUnavailable;
        }
        auto reason = resolveTarget(true);
        if (reason != CommandReject::None) {
            return reason;
        }
        auto creature = std::dynamic_pointer_cast<Creature>(targets.target);
        if (!creature || creature->isDead()) {
            return CommandReject::TargetInvalid;
        }
        return CommandReject::None;
    }

    case CommandType::UsePower: {
        if (command.param < 0 || !actor->attributes().hasSpell(static_cast<SpellType>(command.param)) ||
            !game.services().game.spells.get(static_cast<SpellType>(command.param))) {
            return CommandReject::PowerUnavailable;
        }
        return resolveTarget(false);
    }

    case CommandType::UseSkill: {
        // The skills a target menu offers on an object: open a lock, slice
        // or repair, set or recover a mine. Rolls and outcomes are the host's.
        auto skill = static_cast<SkillType>(command.param);
        if (skill != SkillType::Security && skill != SkillType::ComputerUse && skill != SkillType::Repair &&
            skill != SkillType::Demolitions) {
            return CommandReject::SkillUnavailable;
        }
        auto reason = resolveTarget(true);
        if (reason != CommandReject::None) {
            return reason;
        }
        auto type = targets.target->type();
        if (type != ObjectType::Door && type != ObjectType::Placeable && type != ObjectType::Trigger) {
            return CommandReject::TargetInvalid;
        }
        return CommandReject::None;
    }

    case CommandType::DisarmTrap: {
        // The target menu's direct "Disarm Trap" item (as opposed to
        // selecting the Demolitions skill first, which is CommandType::UseSkill).
        auto reason = resolveTarget(true);
        if (reason != CommandReject::None) {
            return reason;
        }
        auto trap = std::dynamic_pointer_cast<Trigger>(targets.target);
        if (!trap || !trap->isTrap() ||
            !(trap->isTrapDetectedByParty() || trap->isTrapDetectedBy(*actor))) {
            return CommandReject::TargetInvalid;
        }
        return CommandReject::None;
    }
    }
    return CommandReject::NotSupported;
}

void executeCoopCommand(Game &game, const net::Command &command, const CoopCommandTargets &targets) {
    auto &actor = targets.actor;
    if (!actor) {
        return;
    }
    switch (command.type) {
    case CommandType::Cancel:
        if (command.param == 1) {
            actor->cancelCombatNow(); // Cancel Combat: the queue and combat mode
        } else if (command.param == 2) {
            actor->cancelLastQueuedAction();
        } else {
            actor->clearAllActions();
        }
        break;

    case CommandType::SwitchWeapons:
        requestWeaponSwitch(game, *actor);
        break;

    case CommandType::AutoCombat:
        game.setAutoCombat(*actor, command.param == 1);
        break;

    case CommandType::SetMine: {
        auto item = game.getObjectById<Item>(static_cast<uint32_t>(command.param));
        if (item) {
            auto action = game.newAction<SetTrapAction>(item, glm::vec3(command.point.x, command.point.y, command.point.z));
            action->setUserAction(true);
            actor->addAction(std::move(action));
        }
        break;
    }

    case CommandType::MoveTo: {
        glm::vec3 point(command.point.x, command.point.y, command.point.z);
        if (auto floor = floorUnder(game, game.services(), point, actor->position().z)) {
            point = *floor;
        }
        // As a player click: drop what the creature was doing, then walk.
        actor->clearAllActions();
        actor->addAction(game.newAction<MoveToLocationAction>(
            std::make_shared<Location>(point, actor->getFacing()), command.run));
        break;
    }

    case CommandType::Attack: {
        actor->clearAllActions();
        auto action = game.newAction<AttackObjectAction>(targets.target);
        action->setUserAction(true);
        actor->addAction(std::move(action));
        break;
    }

    case CommandType::OpenDoor:
    case CommandType::UseObject: {
        if (targets.use != CoopUse::Normal) {
            break; // containers and exits: the session carries them out
        }
        if (auto door = std::dynamic_pointer_cast<Door>(targets.target)) {
            if (!door->isOpen()) {
                actor->clearAllActions();
                actor->addAction(game.newAction<OpenDoorAction>(door));
            }
            break;
        }
        if (auto creature = std::dynamic_pointer_cast<Creature>(targets.target)) {
            actor->clearAllActions();
            auto action = game.newAction<AttackObjectAction>(creature);
            action->setUserAction(true);
            actor->addAction(std::move(action));
        }
        break;
    }

    case CommandType::UseItem: {
        std::shared_ptr<Item> item;
        std::shared_ptr<Spell> spell;
        bool found = command.param > 0 ? resolveItemById(game, *actor, static_cast<uint32_t>(command.param), true, item, spell)
                                       : resolveItemSpell(*actor, command.item, item, spell);
        if (!found) {
            break;
        }
        std::shared_ptr<Object> target = targets.target ? targets.target : std::static_pointer_cast<Object>(actor);
        auto action = game.newAction<CastSpellAtObjectAction>(spell, target, std::optional<std::shared_ptr<Item>>(item));
        action->setUserAction(true);
        actor->addAction(std::move(action));
        break;
    }

    case CommandType::UseFeat: {
        auto action = game.newAction<UseFeatAction>(static_cast<FeatType>(command.param), targets.target);
        action->setUserAction(true);
        actor->clearAllActions();
        actor->addAction(std::move(action));
        break;
    }

    case CommandType::UsePower: {
        auto spell = game.services().game.spells.get(static_cast<SpellType>(command.param));
        if (!spell) {
            break;
        }
        std::shared_ptr<Object> target = targets.target ? targets.target : std::static_pointer_cast<Object>(actor);
        auto action = game.newAction<CastSpellAtObjectAction>(spell, target, std::nullopt);
        action->setUserAction(true);
        actor->addAction(std::move(action));
        break;
    }

    case CommandType::UseSkill: {
        // As the target menu does (SelectionOverlay): queue after what is running.
        auto action = game.newAction<UseSkillAction>(static_cast<SkillType>(command.param), targets.target);
        action->setUserAction(true);
        actor->addAction(std::move(action));
        break;
    }

    case CommandType::DisarmTrap: {
        // As the target menu's direct disarm item does (SelectionOverlay).
        auto action = game.newAction<TrapSkillAction>(ActionType::DisableTrap, targets.target);
        action->setUserAction(true);
        actor->addAction(std::move(action));
        break;
    }
    }
}

bool coopDialogOffersStoreOrMinigame(Game &game, const std::string &dialogResRef) {
    // Routine numbers of the K1 script table (nwscript): OpenStore 378,
    // PlayPazaak 364, StartNewModule 509.
    static constexpr int kOpenStore = 378;
    static constexpr int kPlayPazaak = 364;
    static constexpr int kStartNewModule = 509;
    static std::map<std::string, bool> cache;
    if (dialogResRef.empty()) {
        return false;
    }
    auto key = boost::to_lower_copy(dialogResRef);
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    bool offers = false;
    try {
        auto dialog = game.services().resource.dialogs.get(key);
        std::set<std::string> scripts;
        if (dialog) {
            for (const auto *nodes : {&dialog->entries, &dialog->replies}) {
                for (const auto &node : *nodes) {
                    if (!node.script.empty()) {
                        scripts.insert(boost::to_lower_copy(node.script));
                    }
                    if (!node.script2.empty()) {
                        scripts.insert(boost::to_lower_copy(node.script2));
                    }
                }
            }
        }
        for (const auto &name : scripts) {
            auto program = game.services().resource.scripts.get(name);
            if (!program) {
                continue;
            }
            bool minigameModule = false;
            for (const auto &instruction : program->instructions()) {
                if (instruction.type == script::InstructionType::CONSTS) {
                    auto value = boost::to_lower_copy(instruction.strValue);
                    minigameModule = minigameModule || (value.size() > 2 && value.compare(value.size() - 2, 2, "mg") == 0);
                } else if (instruction.type == script::InstructionType::ACTION) {
                    if (instruction.routine == kOpenStore || instruction.routine == kPlayPazaak ||
                        (instruction.routine == kStartNewModule && minigameModule)) {
                        offers = true;
                        break;
                    }
                }
            }
            if (offers) {
                break;
            }
        }
    } catch (const std::exception &) {
        offers = false;
    }
    cache[key] = offers;
    return offers;
}

bool coopDialogReaches(
    const Dialog &dialog,
    const std::function<bool(const std::string &, const Dialog::EntryReplyLink::ConditionParams &)> &condition,
    const std::function<bool(const Dialog::EntryReply &)> &isTarget) {
    auto active = [&](const Dialog::EntryReplyLink &link) {
        std::optional<bool> first;
        std::optional<bool> second;
        if (!link.active.empty()) {
            first = condition(link.active, link.params) != link.notActive;
        }
        if (!link.active2.empty()) {
            second = condition(link.active2, link.params2) != link.notActive2;
        }
        if (first && second) {
            return link.logic == 1 ? (*first || *second) : (*first && *second);
        }
        return first ? *first : second ? *second : true;
    };
    auto firstActive = [&](const std::vector<Dialog::EntryReplyLink> &links) {
        for (const auto &link : links) {
            if (active(link)) {
                return link.index >= 0 && link.index < static_cast<int>(dialog.entries.size()) ? link.index : -1;
            }
        }
        return -1;
    };
    // Conditions are taken as fixed for the walk, so an entry seen once
    // leads nowhere new the second time.
    std::set<int> seen;
    std::vector<int> pending;
    if (int start = firstActive(dialog.startEntries); start >= 0) {
        pending.push_back(start);
    }
    while (!pending.empty()) {
        int index = pending.back();
        pending.pop_back();
        if (!seen.insert(index).second) {
            continue;
        }
        const auto &entry = dialog.entries[index];
        if (isTarget(entry)) {
            return true;
        }
        for (const auto &link : entry.replies) {
            if (link.index < 0 || link.index >= static_cast<int>(dialog.replies.size()) || !active(link)) {
                continue;
            }
            const auto &reply = dialog.replies[link.index];
            if (isTarget(reply)) {
                return true;
            }
            if (int next = firstActive(reply.entries); next >= 0) {
                pending.push_back(next);
            }
        }
    }
    return false;
}

bool coopDialogReachesOffer(Game &game, const Object &npc, OfferKind kind) {
    if (kind != OfferKind::Store && kind != OfferKind::Pazaak) {
        return true;
    }
    if (npc.conversation().empty()) {
        return false;
    }
    int routine = game.routines().getIndexByName(kind == OfferKind::Store ? "OpenStore" : "PlayPazaak");
    try {
        auto dialog = game.services().resource.dialogs.get(boost::to_lower_copy(npc.conversation()));
        if (!dialog) {
            return false;
        }
        std::map<std::string, bool> opens;
        auto scriptOpens = [&](const std::string &name) {
            if (name.empty()) {
                return false;
            }
            auto key = boost::to_lower_copy(name);
            auto known = opens.find(key);
            if (known != opens.end()) {
                return known->second;
            }
            bool found = false;
            if (auto program = game.services().resource.scripts.get(key)) {
                for (const auto &instruction : program->instructions()) {
                    if (instruction.type == script::InstructionType::ACTION && instruction.routine == routine) {
                        found = true;
                        break;
                    }
                }
            }
            opens[key] = found;
            return found;
        };
        uint32_t caller = npc.id();
        auto condition = [&](const std::string &script, const Dialog::EntryReplyLink::ConditionParams &params) {
            // As Conversation::evaluateCondition: the NPC is OBJECT_SELF.
            static const script::ArgKind kParams[] {script::ArgKind::ScriptParam1, script::ArgKind::ScriptParam2,
                                                    script::ArgKind::ScriptParam3, script::ArgKind::ScriptParam4,
                                                    script::ArgKind::ScriptParam5};
            std::vector<script::Argument> args;
            args.emplace_back(script::ArgKind::Caller, script::Variable::ofObject(caller));
            for (size_t i = 0; i < params.ints.size(); ++i) {
                args.emplace_back(kParams[i], script::Variable::ofInt(params.ints[i]));
            }
            args.emplace_back(script::ArgKind::ScriptStringParam, script::Variable::ofString(params.str));
            return game.scriptRunner().run(boost::to_lower_copy(script), args) != 0;
        };
        return coopDialogReaches(
            *dialog, condition, [&](const Dialog::EntryReply &node) { return scriptOpens(node.script) || scriptOpens(node.script2); });
    } catch (const std::exception &) {
        return false;
    }
}

bool coopScriptOpensScreen(Game &game, const std::string &script) {
    static std::map<std::string, bool> cache;
    auto key = boost::to_lower_copy(script);
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    bool opens = false;
    try {
        if (auto program = game.services().resource.scripts.get(key)) {
            auto &routines = game.routines();
            for (const auto &instruction : program->instructions()) {
                if (instruction.type == script::InstructionType::ACTION && instruction.routine >= 0 &&
                    instruction.routine < routines.getNumRoutines()) {
                    const auto &name = routines.get(instruction.routine).name();
                    if (name == "ShowUpgradeScreen" || name == "OpenStore" || name == "ShowPartySelectionGUI" ||
                        name == "ShowGalaxyMap" || name == "PlayPazaak") {
                        opens = true;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception &) {
        opens = false;
    }
    cache[key] = opens;
    return opens;
}

CoopBench coopScriptBench(Game &game, const std::string &script) {
    static std::map<std::string, CoopBench> cache;
    auto key = boost::to_lower_copy(script);
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    CoopBench bench = CoopBench::None;
    try {
        if (auto program = game.services().resource.scripts.get(key)) {
            auto &routines = game.routines();
            for (const auto &instruction : program->instructions()) {
                if (instruction.type == script::InstructionType::ACTION && instruction.routine >= 0 &&
                    instruction.routine < routines.getNumRoutines()) {
                    const auto &name = routines.get(instruction.routine).name();
                    if (name == "ShowUpgradeScreen") {
                        bench = CoopBench::Workbench;
                        break;
                    }
                    if (name == "ShowChemicalUpgradeScreen") {
                        bench = CoopBench::Lab;
                        break;
                    }
                }
            }
        }
    } catch (const std::exception &) {
        bench = CoopBench::None;
    }
    cache[key] = bench;
    return bench;
}

bool coopScriptChangesStory(Game &game, const std::string &script) {
    // Script table routines that change what the story or the world holds.
    // Reads (Get*, Is*), sounds, animations, camera and text do not.
    static const std::set<std::string> changes {
        "SetGlobalNumber", "SetGlobalBoolean", "SetGlobalString", "SetGlobalLocation", "SetGlobalFadeIn",
        "SetLocalInt", "SetLocalFloat", "SetLocalString", "SetLocalObject", "SetLocalLocation",
        "SetLocalNumber", "SetLocalBoolean", "DeleteLocalInt", "DeleteLocalFloat", "DeleteLocalString",
        "CreateObject", "DestroyObject", "CreateItemOnObject", "GiveItem", "ActionGiveItem", "ActionTakeItem",
        "ActionPickUpItem", "ActionPutDownItem", "ActionEquipItem", "ActionUnequipItem",
        "GiveXPToCreature", "SetXP", "GiveGoldToCreature", "TakeGoldFromCreature",
        "StartNewModule", "SetLocked", "ActionOpenDoor", "ActionCloseDoor", "ActionLockObject", "ActionUnlockObject",
        "SetPlotFlag", "ChangeFaction", "ChangeToStandardFaction", "SetCommandable", "ApplyEffectToObject",
        "ApplyEffectAtLocation", "AddJournalQuestEntry", "RemoveJournalQuestEntry", "AddPartyMember",
        "RemovePartyMember", "RemoveFromParty", "SetPartyLeader", "AddAvailableNPCByTemplate",
        "AddAvailableNPCByObject", "RemoveAvailableNPC", "SetNPCSelectability", "ShowPartySelectionGUI",
        "ActionStartConversation", "BeginConversation", "ExecuteScript", "SignalEvent", "AssignCommand",
        "SetTransitionTarget", "SetAreaUnescapable", "SetMapPinEnabled", "SetPlanetAvailable", "SetPlanetSelectable",
        "AwardStealthXP", "SetTrapDisabled", "SetPlaceableIllumination", "ActionCastSpellAtObject",
        "ActionCastSpellAtLocation", "ActionAttack", "SetIsDestroyable", "SetMinOneHP", "SetMaxHitPoints",
        "ChangeObjectAppearance", "AddBonusForcePoints", "SetPlayerRestrictMode", "SetSoloMode", "PlayMovie",
        "ShowGalaxyMap", "SetFacing", "JumpToLocation", "JumpToObject", "ActionJumpToObject", "ActionJumpToLocation"};
    static std::map<std::string, bool> cache;
    auto key = boost::to_lower_copy(script);
    if (key.empty()) {
        return false;
    }
    auto cached = cache.find(key);
    if (cached != cache.end()) {
        return cached->second;
    }
    bool story = false;
    try {
        auto program = game.services().resource.scripts.get(key);
        if (!program) {
            story = true; // unknown: the host keeps it
        } else {
            auto &routines = game.routines();
            for (const auto &instruction : program->instructions()) {
                if (instruction.type != script::InstructionType::ACTION) {
                    continue;
                }
                if (instruction.routine < 0 || instruction.routine >= routines.getNumRoutines()) {
                    story = true;
                    break;
                }
                if (changes.count(routines.get(instruction.routine).name())) {
                    story = true;
                    break;
                }
            }
        }
    } catch (const std::exception &) {
        story = true;
    }
    cache[key] = story;
    return story;
}

bool CoopRateLimiter::allow(uint64_t tick, int tickRate, int perSecond) {
    uint64_t window = static_cast<uint64_t>(std::max(1, tickRate));
    while (!_ticks.empty() && _ticks.front() + window <= tick) {
        _ticks.pop_front();
    }
    if (static_cast<int>(_ticks.size()) >= perSecond) {
        return false;
    }
    _ticks.push_back(tick);
    return true;
}

} // namespace game

} // namespace reone
