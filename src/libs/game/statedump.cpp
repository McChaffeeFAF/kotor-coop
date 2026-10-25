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

// Machine-readable state dumps for automated drivers (the dumpstate and
// dumpobject console commands). Everything here only reads game state: no
// selection, no action queue changes and no random draws.

#include "reone/game/coopsession.h"
#include "reone/game/game.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <boost/json.hpp>

#include "reone/graphics/texture.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/closedoor.h"
#include "reone/game/action/cutsceneattack.h"
#include "reone/game/action/follow.h"
#include "reone/game/action/interactobject.h"
#include "reone/game/action/jumptolocation.h"
#include "reone/game/action/jumptoobject.h"
#include "reone/game/action/movetolocation.h"
#include "reone/game/action/movetoobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/opencontainer.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/action/startconversation.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/action/usetalentonobject.h"
#include "reone/game/d20/spell.h"
#include "reone/game/object/item.h"
#include "reone/game/combat.h"
#include "reone/game/pathfinder.h"
#include "reone/game/di/services.h"
#include "reone/game/gui/computer.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/gui/dialog.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/system/logutil.h"
#include "reone/scene/node/model.h"
#include "reone/system/randomutil.h"

namespace json = boost::json;

namespace reone {

namespace game {

static constexpr float kDefaultDumpRadius = 15.0f;

// Boost.JSON prints doubles in exponent form (1.5E0). Drivers read this line
// as text too, so numbers are written as plain decimals with millimetre
// precision; everything else defers to Boost.JSON for correct escaping.
static void writeCompact(std::string &out, const json::value &value) {
    switch (value.kind()) {
    case json::kind::null:
        out += "null";
        break;
    case json::kind::bool_:
        out += value.get_bool() ? "true" : "false";
        break;
    case json::kind::int64:
        out += std::to_string(value.get_int64());
        break;
    case json::kind::uint64:
        out += std::to_string(value.get_uint64());
        break;
    case json::kind::double_: {
        double number = value.get_double();
        if (!std::isfinite(number)) {
            out += "null";
            break;
        }
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.3f", number);
        std::string text(buffer);
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
        if (text == "-0" || text.empty()) {
            text = "0";
        }
        out += text;
        break;
    }
    case json::kind::string:
        out += json::serialize(value);
        break;
    case json::kind::array: {
        out += '[';
        bool first = true;
        for (auto &element : value.get_array()) {
            if (!first) {
                out += ',';
            }
            first = false;
            writeCompact(out, element);
        }
        out += ']';
        break;
    }
    case json::kind::object: {
        out += '{';
        bool first = true;
        for (auto &field : value.get_object()) {
            if (!first) {
                out += ',';
            }
            first = false;
            out += json::serialize(json::string(field.key()));
            out += ':';
            writeCompact(out, field.value());
        }
        out += '}';
        break;
    }
    }
}

static std::string toCompactString(const json::value &value) {
    std::string out;
    writeCompact(out, value);
    return out;
}

static const char *screenName(Game::Screen screen) {
    switch (screen) {
    case Game::Screen::None:
        return "none";
    case Game::Screen::MainMenu:
        return "main_menu";
    case Game::Screen::Loading:
        return "loading";
    case Game::Screen::CharacterGeneration:
        return "character_generation";
    case Game::Screen::InGame:
        return "in_game";
    case Game::Screen::InGameMenu:
        return "in_game_menu";
    case Game::Screen::Conversation:
        return "conversation";
    case Game::Screen::Container:
        return "container";
    case Game::Screen::PartySelection:
        return "party_selection";
    case Game::Screen::SaveLoad:
        return "save_load";
    case Game::Screen::GalaxyMap:
        return "galaxy_map";
    case Game::Screen::SwoopRace:
        return "swoop_race";
    case Game::Screen::PazaakWager:
        return "pazaak_wager";
    case Game::Screen::PazaakSetup:
        return "pazaak_setup";
    case Game::Screen::PazaakBoard:
        return "pazaak_board";
    case Game::Screen::Turret:
        return "turret";
    case Game::Screen::Store:
        return "store";
    case Game::Screen::Upgrade:
        return "upgrade";
    case Game::Screen::Credits:
        return "credits";
    }
    return "unknown";
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
    case ObjectType::AreaOfEffect:
        return "area_of_effect";
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
        return "unknown";
    }
}

static json::array vec3Json(const glm::vec3 &v) {
    return json::array {v.x, v.y, v.z};
}

static json::value idOrNull(const std::shared_ptr<Object> &object) {
    if (!object) {
        return nullptr;
    }
    return object->id();
}

static json::object actionJson(Action &action) {
    json::object result;
    auto name = actionTypeName(action.type());
    if (name.empty()) {
        result["type"] = "unknown";
        result["typeId"] = static_cast<int>(action.type());
    } else {
        result["type"] = json::string(name);
    }
    if (action.isUserAction()) {
        result["user"] = true;
    }
    if (action.locked()) {
        result["locked"] = true;
    }

    std::shared_ptr<Object> target;
    const Location *location = nullptr;
    switch (action.type()) {
    case ActionType::AttackObject:
        target = static_cast<AttackObjectAction &>(action).target();
        break;
    case ActionType::CutsceneAttack:
        target = static_cast<CutsceneAttackAction &>(action).target();
        break;
    case ActionType::UseFeat:
        target = static_cast<UseFeatAction &>(action).target();
        result["feat"] = static_cast<int>(static_cast<UseFeatAction &>(action).feat());
        break;
    case ActionType::CastSpellAtObject: {
        // What the AI chose: the power or item spell, and the item if any.
        auto &cast = static_cast<CastSpellAtObjectAction &>(action);
        target = cast.target();
        if (cast.spell()) {
            result["spell"] = static_cast<int>(cast.spell()->type);
        }
        if (cast.item() && *cast.item()) {
            result["item"] = (*cast.item())->blueprintResRef();
        }
        break;
    }
    case ActionType::UseTalentOnObject: {
        auto &talent = static_cast<UseTalentOnObjectAction &>(action);
        if (talent.subAction()) {
            result["sub"] = actionJson(*talent.subAction());
        }
        break;
    }
    case ActionType::MoveToObject: {
        auto &move = static_cast<MoveToObjectAction &>(action);
        target = move.target();
        result["run"] = move.isRun();
        break;
    }
    case ActionType::MoveToPoint:
        result["point"] = vec3Json(static_cast<MoveToPointAction &>(action).point());
        break;
    case ActionType::MoveToLocation: {
        auto &move = static_cast<MoveToLocationAction &>(action);
        location = move.destination().get();
        result["run"] = move.isRun();
        break;
    }
    case ActionType::JumpToLocation:
        location = static_cast<JumpToLocationAction &>(action).location().get();
        break;
    case ActionType::JumpToObject:
        target = static_cast<JumpToObjectAction &>(action).target();
        break;
    case ActionType::Follow:
        target = static_cast<FollowAction &>(action).followee();
        break;
    case ActionType::OpenDoor:
        target = static_cast<OpenDoorAction &>(action).door();
        break;
    case ActionType::CloseDoor:
        target = static_cast<CloseDoorAction &>(action).door();
        break;
    case ActionType::InteractObject:
        target = static_cast<InteractObjectAction &>(action).placeable();
        break;
    case ActionType::OpenContainer:
        target = static_cast<OpenContainerAction &>(action).object();
        break;
    case ActionType::StartConversation: {
        auto &converse = static_cast<StartConversationAction &>(action);
        target = converse.target();
        if (!converse.dialogResRef().empty()) {
            result["dialog"] = converse.dialogResRef();
        }
        break;
    }
    default:
        break;
    }
    if (target) {
        result["target"] = target->id();
    }
    if (location) {
        result["point"] = vec3Json(location->position());
        result["facing"] = location->facing();
    }
    return result;
}

static json::object objectJson(const Object &object,
                               const Creature *leader,
                               IReputes &reputes,
                               const glm::vec3 *origin) {
    json::object result;
    result["id"] = object.id();
    result["tag"] = object.tag();
    result["type"] = objectTypeName(object.type());
    if (!object.name().empty()) {
        result["name"] = object.name();
    }
    if (!object.blueprintResRef().empty()) {
        result["tpl"] = object.blueprintResRef();
    }
    result["pos"] = vec3Json(object.position());
    result["facing"] = object.getFacing();
    if (origin) {
        result["dist"] = object.getDistanceTo(*origin);
    }
    result["selectable"] = object.isSelectable();

    switch (object.type()) {
    case ObjectType::Creature: {
        auto &creature = static_cast<const Creature &>(object);
        result["hp"] = creature.currentHitPoints();
        result["maxhp"] = creature.maxHitPoints();
        result["fp"] = creature.currentForce();
        result["maxfp"] = creature.maxForcePoints();
        result["movementRestricted"] = creature.isMovementRestricted();
        result["xp"] = creature.xp();
        result["level"] = creature.attributes().getAggregateLevel();
        result["levelUpPending"] = creature.isLevelUpPending();
        // Loot: a dead creature is clickable only while it holds droppable items.
        size_t dropable = 0;
        json::array items;
        for (auto &item : creature.items()) {
            if (item->isDropable()) {
                ++dropable;
            }
            items.push_back(json::string(item->blueprintResRef().empty() ? item->tag() : item->blueprintResRef()));
        }
        result["itemCount"] = creature.items().size();
        result["dropableCount"] = dropable;
        result["items"] = std::move(items);
        // Equipment by slot number, e.g. "4" main hand, "18" second-set main hand.
        json::object equipped;
        for (const auto &[slot, item] : creature.equipment()) {
            if (item) {
                equipped[std::to_string(slot)] =
                    json::string(item->blueprintResRef().empty() ? item->tag() : item->blueprintResRef());
            }
        }
        result["equipped"] = std::move(equipped);
        if (auto portrait = creature.portrait()) {
            result["portrait"] = portrait->name();
        }
        result["dead"] = creature.isDead();
        result["faction"] = static_cast<int>(creature.faction());
        if (leader && leader != &creature) {
            result["hostile"] = reputes.getIsEnemy(*leader, creature);
        }
        result["inCombat"] = creature.isInCombat();
        if (creature.autoCombat()) {
            result["autoCombat"] = true; // the HUD toggle is lit (a co-op client: the host's word)
        }
        if (creature.isStealthed()) {
            result["stealth"] = true;
            // The stealth field's shimmer (row 8002) as the presenter holds it.
            json::array rows;
            for (uint32_t row : creature.game().visualEffectPresenter().rowsOn(creature.id())) {
                rows.push_back(row);
            }
            result["vfx"] = std::move(rows);
        }
        // Animation advances with the simulation tick, and gameplay waits on it.
        if (auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(creature.sceneNode())) {
            result["anim"] = model->activeAnimationName();
            // Every channel, layers first: name, time, speed, flags.
            json::array channels;
            for (const auto &channel : model->animationChannels()) {
                json::object c;
                c["name"] = channel.anim ? channel.anim->name() : std::string();
                c["time"] = std::round(channel.time * 1000.0f) / 1000.0f;
                c["speed"] = std::round(channel.properties.speed * 1000.0f) / 1000.0f;
                c["flags"] = channel.properties.flags;
                if (channel.finished) {
                    c["finished"] = true;
                }
                channels.push_back(std::move(c));
            }
            result["animChannels"] = std::move(channels);
        }
        result["movementType"] = static_cast<int>(creature.movementType());
        result["talking"] = creature.isTalking();
        auto attackTarget = creature.getAttackTarget();
        result["attackTarget"] = idOrNull(attackTarget);
        if (creature.isInConversation()) {
            result["inConversation"] = true;
        }
        if (!object.conversation().empty()) {
            result["dialog"] = object.conversation();
        }
        break;
    }
    case ObjectType::Door: {
        auto &door = static_cast<const Door &>(object);
        result["open"] = door.isOpen();
        result["locked"] = door.isLocked();
        result["keyRequired"] = door.isKeyRequired();
        result["usable"] = !door.isStatic();
        if (!door.conversation().empty()) {
            result["dialog"] = door.conversation();
        }
        break;
    }
    case ObjectType::Placeable: {
        auto &placeable = static_cast<const Placeable &>(object);
        result["open"] = placeable.isOpen();
        result["locked"] = placeable.isLocked();
        result["usable"] = placeable.isUsable();
        result["hasInventory"] = placeable.hasInventory();
        result["itemCount"] = placeable.items().size();
        result["animState"] = placeable.animationState();
        if (auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(placeable.sceneNode())) {
            result["anim"] = model->activeAnimationName();
        }
        if (!placeable.conversation().empty()) {
            result["dialog"] = placeable.conversation();
        }
        break;
    }
    default:
        break;
    }
    return result;
}

static json::array actionsJson(const Object &object) {
    json::array actions;
    for (auto &action : object.actions()) {
        if (action) {
            actions.push_back(actionJson(*action));
        }
    }
    return actions;
}

std::string Game::dumpObjectJson(const Object &object, bool withLocals) const {
    auto leader = _party.getLeader();
    glm::vec3 origin(leader ? leader->position() : object.position());
    json::object result = objectJson(object, leader.get(), _services.game.reputes, leader ? &origin : nullptr);
    result["actions"] = actionsJson(object);
    result["delayed"] = object._delayed.size();
    result["commandable"] = object.isCommandable();
    if (withLocals) {
        json::object booleans;
        for (auto &[index, value] : object.localBooleans()) {
            booleans[std::to_string(index)] = value;
        }
        json::object numbers;
        for (auto &[index, value] : object.localNumbers()) {
            numbers[std::to_string(index)] = value;
        }
        result["localBooleans"] = std::move(booleans);
        result["localNumbers"] = std::move(numbers);
    }
    return toCompactString(result);
}

std::string Game::dumpStateJson(float radius) const {
    auto leader = _party.getLeader();
    auto player = _party.player();
    std::shared_ptr<Area> area = _module ? _module->area() : nullptr;
    IReputes &reputes = _services.game.reputes;

    json::object root;
    // Render-only: frames per tick depend on the frame rate. Compare "tick".
    root["frame"] = _frameCount;
    root["tick"] = _tickCount;
    root["time"] = _worldTimeMilliseconds;
    root["screen"] = screenName(_screen);
    root["paused"] = _paused;
    root["movie"] = static_cast<bool>(_movie);
    if (_globalFade.opacity() > 0.0f || _globalFade.locked() || _globalFade.heldForDialog()) {
        json::object fade;
        fade["opacity"] = _globalFade.opacity();
        fade["locked"] = _globalFade.locked();
        fade["heldForDialog"] = _globalFade.heldForDialog();
        root["fade"] = std::move(fade);
    }
    if (_dialogFade.opacity() > 0.0f) {
        root["dialogFade"] = _dialogFade.opacity();
    }
    if (_videoEffect.row >= 0) {
        root["videoEffect"] = _videoEffect.row;
    }
    root["module"] = _module ? json::value(_module->name()) : json::value(nullptr);
    if (area) {
        root["area"] = area->tag().empty() ? area->name() : area->tag();
        // Occupancy of the area's fixed-size path pool; a steady climb means
        // navigation paths are being leaked.
        const auto &paths = area->pathfinder().paths;
        root["pathSlotsActive"] = std::count_if(paths.begin(), paths.end(), [](const auto &path) { return path.active; });
        root["pathSlots"] = paths.size();
        // Map fog of war (design note p2_ui): the explored grid and the leader's cell.
        const auto &exploration = area->mapExploration();
        json::object map;
        map["columns"] = exploration.columns();
        map["rows"] = exploration.rows();
        map["explored"] = exploration.exploredCount();
        if (_coop && exploration.columns() > 0) { // co-op harness only: the golden hashes cover this dump
            // A hash of the cells, so two screens' grids compare exactly.
            uint64_t hash = 1469598103934665603ull;
            for (uint8_t b : exploration.bytes()) {
                hash = (hash ^ b) * 1099511628211ull;
            }
            map["cellsHash"] = std::to_string(hash);
            if (exploration.columns() * exploration.rows() <= 4096) {
                std::string hex;
                static const char *digits = "0123456789abcdef";
                for (uint8_t b : exploration.bytes()) {
                    hex += digits[b >> 4];
                    hex += digits[b & 15];
                }
                map["cells"] = hex;
            }
        }
        if (_coop && exploration.columns() > 0) {
            // Where the other players' characters stand on the map, and whether that cell is explored.
            json::array players;
            if (auto coop = _coop.get()) {
                for (const auto &marker : coop->mapMarkers()) {
                    if (!marker.object) {
                        continue;
                    }
                    glm::vec2 point = area->worldToMapPoint(glm::vec2(marker.object->position()));
                    int column = std::clamp(static_cast<int>(std::floor(point.x * exploration.columns())), 0, exploration.columns() - 1);
                    int row = std::clamp(static_cast<int>(std::floor(point.y * exploration.rows())), 0, exploration.rows() - 1);
                    json::object entry;
                    entry["id"] = marker.object->id();
                    entry["player"] = marker.player;
                    entry["column"] = column;
                    entry["row"] = row;
                    entry["explored"] = exploration.isExplored(column, row);
                    players.push_back(std::move(entry));
                }
            }
            map["markers"] = std::move(players);
        }
        if (leader) {
            glm::vec2 point = area->worldToMapPoint(glm::vec2(leader->position()));
            map["leaderMapPoint"] = json::array {point.x, point.y};
        }
        root["map"] = std::move(map);
        if (area->maxStealthXP() > 0 || area->isStealthXPEnabled()) {
            // Stealth XP (design note stealth): the pool and who has cost it.
            json::object stealthXP;
            stealthXP["enabled"] = area->isStealthXPEnabled();
            stealthXP["current"] = area->currentStealthXP();
            stealthXP["max"] = area->maxStealthXP();
            stealthXP["decrement"] = area->stealthXPDecrement();
            stealthXP["detectors"] = area->stealthXPDetectors().size();
            root["stealthXP"] = std::move(stealthXP);
        }
    } else {
        root["area"] = nullptr;
    }
    root["leader"] = idOrNull(leader);
    root["player"] = idOrNull(player);
    root["radius"] = radius;

    json::array party;
    std::set<uint32_t> partyIds;
    for (auto &member : _party.members()) {
        if (!member.creature) {
            continue;
        }
        partyIds.insert(member.creature->id());
        json::object entry = objectJson(*member.creature, leader.get(), reputes, nullptr);
        entry["npc"] = member.npc;
        entry["actions"] = actionsJson(*member.creature);
        entry["delayed"] = member.creature->_delayed.size();
        entry["commandable"] = member.creature->isCommandable();
        party.push_back(std::move(entry));
    }
    root["party"] = std::move(party);

    json::value conversation = nullptr;
    if (_screen == Screen::Conversation && _conversation) {
        auto state = _conversation->inspectionState();
        if (state.dialog) {
            json::object dialog;
            dialog["kind"] = _conversation == _dialog.get() ? "dialog" : "computer";
            dialog["owner"] = idOrNull(state.owner);
            if (state.owner) {
                dialog["ownerTag"] = state.owner->tag();
            }
            dialog["dialog"] = state.dialog->resRef;
            if (state.entry) {
                const auto &entries = state.dialog->entries;
                if (!entries.empty() && state.entry >= entries.data() && state.entry < entries.data() + entries.size()) {
                    dialog["entry"] = static_cast<int64_t>(state.entry - entries.data());
                }
                dialog["text"] = substituteCustomTokens(state.entry->text);
                std::string speaker = state.entry->speaker;
                if (speaker.empty() && state.owner) {
                    speaker = state.owner->tag();
                }
                dialog["speaker"] = speaker;
            }
            // Positions in this array are the reply-list rows, which is
            // what selectdialogoption indexes.
            json::array replies;
            const auto &allReplies = state.dialog->replies;
            for (size_t i = 0; i < state.replies.size(); ++i) {
                auto reply = state.replies[i];
                json::object entry;
                entry["index"] = i;
                entry["text"] = substituteCustomTokens(reply->text);
                if (!allReplies.empty() && reply >= allReplies.data() && reply < allReplies.data() + allReplies.size()) {
                    entry["node"] = static_cast<int64_t>(reply - allReplies.data());
                }
                replies.push_back(std::move(entry));
            }
            dialog["replies"] = std::move(replies);
            dialog["autoPick"] = state.autoPickFirstReply;
            dialog["entryEnded"] = state.entryEnded;
            dialog["paused"] = state.paused;
            // Who speaks to whom, and the dialog camera's shot (P2-Cine).
            auto presentation = _conversation->presentationReport();
            if (presentation.speaker) {
                dialog["speakerId"] = idOrNull(presentation.speaker);
            }
            if (presentation.listener) {
                dialog["listener"] = presentation.listener->tag();
                dialog["listenerId"] = idOrNull(presentation.listener);
            }
            int cameraId = 0;
            switch (_conversation->getCamera(cameraId)) {
            case CameraType::Animated:
                dialog["cameraType"] = "animated";
                break;
            case CameraType::Static:
                dialog["cameraType"] = "static";
                dialog["cameraId"] = cameraId;
                break;
            default:
                dialog["cameraType"] = "dialog";
                break;
            }
            if (presentation.dialogShot) {
                json::object shot;
                shot["angle"] = presentation.angle;
                shot["side"] = presentation.side;
                shot["closeUpFallback"] = presentation.closeUpFallback;
                shot["eye"] = vec3Json(presentation.eye);
                shot["target"] = vec3Json(presentation.target);
                dialog["camera"] = std::move(shot);
            }
            conversation = std::move(dialog);
        }
    }
    root["conversation"] = std::move(conversation);

    // How recent conversations ended: normal (EndConversation) or aborted
    // (EndConverAbort), in end order per conversation screen.
    json::array conversationEnds;
    for (Conversation *gui : {static_cast<Conversation *>(_dialog.get()), static_cast<Conversation *>(_computer.get())}) {
        if (!gui) {
            continue;
        }
        for (const auto &record : gui->endHistory()) {
            json::object end;
            end["dialog"] = record.dialog;
            end["entry"] = record.entry;
            end["normal"] = record.normal;
            end["tick"] = record.tick;
            conversationEnds.push_back(std::move(end));
        }
    }
    root["conversationEnds"] = std::move(conversationEnds);

    json::array nearby;
    if (area && leader) {
        glm::vec3 origin(leader->position());
        std::vector<std::pair<float, std::shared_ptr<Object>>> found;
        for (auto &object : area->objects()) {
            if (!object || partyIds.count(object->id()) || !object->isRuntimeLive()) {
                continue;
            }
            // Ambient sounds and dialogue cameras crowd the line without
            // being anything a driver can act on.
            if (object->type() == ObjectType::Sound || object->type() == ObjectType::Camera) {
                continue;
            }
            float distance = object->getDistanceTo(origin);
            if (distance <= radius) {
                found.emplace_back(distance, object);
            }
        }
        std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
            return a.first < b.first;
        });
        for (auto &[distance, object] : found) {
            nearby.push_back(objectJson(*object, leader.get(), reputes, &origin));
        }
    }
    root["nearby"] = std::move(nearby);

    root["selected"] = area ? idOrNull(area->selectedObject()) : json::value(nullptr);

    json::array rounds;
    for (auto &round : _combat.rounds()) {
        if (!round) {
            continue;
        }
        json::object entry;
        static const char *kStates[] {"pending", "first_action", "second_action", "finished"};
        // The round's state and time are its first creature's; each action
        // carries its own creature's.
        const auto &owner = round->actions.front();
        int state = static_cast<int>(owner.state);
        entry["state"] = state >= 0 && state < 4 ? kStates[state] : "unknown";
        entry["time"] = owner.time;
        entry["duel"] = round->duel;
        json::array actions;
        for (auto &roundAction : round->actions) {
            json::object item;
            auto attacker = roundAction.attacker.resolve();
            auto target = roundAction.target.resolve();
            item["attacker"] = attacker ? json::value(attacker->id()) : json::value(nullptr);
            item["target"] = idOrNull(target);
            int sideState = static_cast<int>(roundAction.state);
            item["state"] = sideState >= 0 && sideState < 4 ? kStates[sideState] : "unknown";
            item["time"] = roundAction.time;
            item["abandoned"] = roundAction.abandoned;
            if (roundAction.action) {
                auto name = actionTypeName(roundAction.action->type());
                item["type"] = name.empty() ? json::string("unknown") : json::string(name);
            }
            actions.push_back(std::move(item));
        }
        entry["actions"] = std::move(actions);
        rounds.push_back(std::move(entry));
    }
    root["combat"] = std::move(rounds);

    // Draw counts diverge at the first roll that differs between two runs,
    // usually well before any position or hit point does.
    json::object rng;
    rng["seed"] = gameplayRandom().seedValue();
    rng["draws"] = gameplayRandom().draws();
    rng["violations"] = gameplayRandomViolations();
    root["rng"] = std::move(rng);

    // Co-op only: a client's replicated objects keyed by the host's ids.
    if (_coop) {
        root["coop"] = _coop->dumpJson();
    }

    return toCompactString(root);
}

void Game::consoleDumpState(const ConsoleArgs &args) {
    if (args.size() > 2) {
        throw std::runtime_error("Usage: dumpstate [radius]");
    }
    float radius = kDefaultDumpRadius;
    if (args.size() > 1) {
        auto value = args.get<float>(1);
        if (!value || *value < 0.0f) {
            throw std::runtime_error("Usage: dumpstate [radius], where radius is a non-negative number");
        }
        radius = *value;
    }
    // The console buffer is not logged, so scripted headless runs (commands
    // files) would otherwise have no way to read the dump back.
    std::string line = dumpStateJson(radius);
    info("dumpstate " + line);
    _console.printLine(line);
}

void Game::consoleDumpObject(const ConsoleArgs &args) {
    if (args.size() != 2) {
        throw std::runtime_error("Usage: dumpobject <id|tag>");
    }
    std::string key(args[1].value());
    std::shared_ptr<Object> object;
    bool numeric = !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char c) { return std::isdigit(c); });
    if (numeric) {
        auto found = _objectById.find(static_cast<uint32_t>(std::stoul(key)));
        if (found != _objectById.end()) {
            object = found->second;
        }
    }
    if (!object) {
        for (auto &member : _party.members()) {
            if (member.creature && member.creature->tag() == key) {
                object = member.creature;
                break;
            }
        }
    }
    if (!object && _module && _module->area()) {
        object = _module->area()->getObjectByTag(key);
        if (!object) {
            for (auto &candidate : _module->area()->objects()) {
                if (candidate && candidate->tag() == key) {
                    object = candidate;
                    break;
                }
            }
        }
    }
    if (!object) {
        throw std::runtime_error("Object not found: " + key);
    }
    std::string line = dumpObjectJson(*object, true);
    info("dumpobject " + line);
    _console.printLine(line);
}

} // namespace game

} // namespace reone
