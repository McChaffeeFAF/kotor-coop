/* Copyright (c) 2026 The reone project contributors */

// John Shepard cameos (cameo.h): the table, the decision and the director
// that keeps the loaded area in step with them.

#include "reone/game/cameo.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include <boost/algorithm/string.hpp>

#include "reone/game/action/playanimation.h"
#include "reone/game/coopcast.h"
#include "reone/game/coopsession.h"
#include "reone/game/coopstandin.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/scene/collision.h"
#include "reone/system/logutil.h"

namespace reone {

namespace game {

namespace {

constexpr int kBody = InventorySlots::body;

// The Ebon Hawk interior is one layout in four modules; the spot (54.0, 65.0)
// is the north lounge: off the corridor, 2.4 m clear of the hawk's own
// waypoints. All other spots were picked the same way from the module's
// walkmesh: on the floor, a metre off the walls, at least 2 m from every
// waypoint, creature, door, placeable and trigger the module authors, and in
// view from where the scene's cameras stand.
constexpr const char *kBriefing = "K_CURRENT_PLANET=10";

} // namespace

const std::vector<CameoRow> &cameoRows() {
    static const std::vector<CameoRow> rows {
        // The Ebon Hawk between planets: the lounge north of the main hold.
        {"ebo_m12aa", "Ebon Hawk lounge", 54.0f, 65.0f, 1.8f, 135.0f, "pause2", ClassType::Scoundrel, "g_a_clothes03", "K_CURRENT_PLANET!=10"},
        // The briefing after the escape from Taris: the cockpit's starboard side, behind the pilot's seat.
        {"ebo_m12aa", "Ebon Hawk briefing after Taris", 54.2f, 76.4f, 1.8f, 20.0f, "listen", ClassType::Scoundrel, "g_a_clothes03", kBriefing},
        // The Ebon Hawk after the Leviathan: the alcove off the main hold, clear of every companion's mark.
        {"ebo_m40aa", "Ebon Hawk after the Leviathan", 57.3f, 53.3f, 1.8f, 135.0f, "pause2", ClassType::Scoundrel, "g_a_clothes03", ""},
        {"ebo_m40ad", "Ebon Hawk after the Leviathan", 57.3f, 53.3f, 1.8f, 135.0f, "pause2", ClassType::Scoundrel, "g_a_clothes03", ""},
        // The Taris apartment (the hideout): the west corner, off Carth's and Bastila's marks.
        {"tar_m02af", "Taris apartment", 79.8f, 140.8f, 0.0f, -90.0f, "pause3", ClassType::Scoundrel, "g_a_clothes06", ""},
        // The Dantooine courtyard in front of the Enclave door, looking at it.
        {"danm14aa", "Dantooine Enclave courtyard", 120.5f, 95.0f, 1.5f, 90.0f, "pause2", ClassType::Scoundrel, "g_a_clothes02", ""},
        // The Star Forge landing: beside the Jedi's welcome, off the Ebon Hawk's ramp.
        {"sta_m45aa", "Star Forge approach", 61.0f, 163.0f, 36.2f, -90.0f, "ready", ClassType::Scoundrel, "g_a_clothes05", ""},
    };
    return rows;
}

namespace {

bool compare(int left, const std::string &op, int right) {
    if (op == "=" || op == "==") return left == right;
    if (op == "!=") return left != right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    return false;
}

} // namespace

bool cameoConditionHolds(const std::string &condition, const CameoInput &input) {
    std::vector<std::string> terms;
    boost::split(terms, condition, boost::is_any_of("&"));
    for (auto &term : terms) {
        boost::trim(term);
        if (term.empty()) {
            continue;
        }
        size_t at = term.find_first_of("=<>!");
        if (at != std::string::npos && at > 0) {
            // NAME<op>number
            std::string name = term.substr(0, at);
            size_t end = at;
            while (end < term.size() && std::string("=<>!").find(term[end]) != std::string::npos) {
                ++end;
            }
            std::string op = term.substr(at, end - at);
            int value = std::atoi(term.substr(end).c_str());
            int have = input.globalNumber ? input.globalNumber(name) : 0;
            if (!compare(have, op, value)) {
                return false;
            }
            continue;
        }
        bool negate = term[0] == '!';
        std::string name = negate ? term.substr(1) : term;
        bool have = input.globalBoolean ? input.globalBoolean(name) : false;
        if (have == negate) {
            return false;
        }
    }
    return true;
}

std::vector<int> cameosWanted(const CameoInput &input) {
    std::vector<int> wanted;
    if (!input.enabled || input.guestPlaysShepard) {
        return wanted;
    }
    auto module = boost::to_lower_copy(input.module);
    const auto &rows = cameoRows();
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (module == rows[i].module && cameoConditionHolds(rows[i].condition, input)) {
            wanted.push_back(i);
        }
    }
    return wanted;
}

bool cameoPoseAnimation(const std::string &pose, AnimationType &animation) {
    if (pose == "pause2") {
        animation = AnimationType::LoopingPause2;
    } else if (pose == "pause3") {
        animation = AnimationType::LoopingPause3;
    } else if (pose == "listen") {
        animation = AnimationType::LoopingListen;
    } else if (pose == "ready") {
        animation = AnimationType::LoopingReady;
    } else {
        return false;
    }
    return true;
}

int cameoRowNear(const std::string &module, float x, float y) {
    auto name = boost::to_lower_copy(module);
    const auto &rows = cameoRows();
    int best = -1;
    float bestDistance = 3.0f;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        if (name != rows[i].module) {
            continue;
        }
        float distance = std::hypot(rows[i].x - x, rows[i].y - y);
        if (distance <= bestDistance) {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

void makeCameo(Game &game, Creature &creature, const CameoRow *row) {
    creature.setCameo(true);
    creature.setTag(kCameoTag);
    creature.setConversation(std::string());
    creature.setOnHeartbeat(std::string());
    creature.setOnSpawn(std::string());
    creature.setOnDeath(std::string());
    creature.setOnNotice(std::string());
    creature.setOnEndRound(std::string());
    creature.setOnSpellAt(std::string());
    creature.setOnAttacked(std::string());
    creature.setOnDamaged(std::string());
    creature.setOnDisturbed(std::string());
    creature.setOnEndDialogue(std::string());
    creature.setOnRested(std::string());
    creature.setOnBlocked(std::string());
    creature.setOnDialogue(std::string());
    creature.setFaction(Faction::Player);
    creature.setPuppet(true); // outside every party table
    creature.setPlotFlag(true); // nothing can hurt him
    creature.setImmortal(true);
    for (const auto &[slot, item] : creature.equipment()) {
        if (item) {
            item->setDropable(false);
        }
    }
    AnimationType animation;
    if (row && cameoPoseAnimation(row->pose, animation)) {
        // An hour of it: the pose holds until the area unloads.
        creature.addAction(game.newAction<PlayAnimationAction>(animation, 1.0f, 3600.0f));
    }
}

std::shared_ptr<Creature> buildCameo(Game &game, const CameoRow &row) {
    std::vector<std::pair<int, const char *>> outfit;
    if (row.body && *row.body) {
        outfit.push_back({kBody, row.body});
    }
    auto shepard = buildCoopShepard(game, row.clazz, 1, outfit);
    if (!shepard) {
        return nullptr;
    }
    makeCameo(game, *shepard, &row);
    return shepard;
}

void cameoBark(Creature &creature) {
    static int next = 0;
    static const resource::SoundSetEntry lines[] {resource::SoundSetEntry::Select1, resource::SoundSetEntry::Select2, resource::SoundSetEntry::Select3};
    creature.playSound(lines[next++ % 3]);
}

// Director

void CameoDirector::forget() {
    _area = nullptr;
    _module.clear();
    _spawned.clear();
    _noFloorWarned.clear();
    _lastState.clear();
}

std::vector<int> CameoDirector::active() const {
    std::vector<int> result;
    for (const auto &[row, id] : _spawned) {
        if (id) {
            result.push_back(row);
        }
    }
    return result;
}

std::shared_ptr<Creature> CameoDirector::creatureFor(Game &game, int row) const {
    auto found = _spawned.find(row);
    return found != _spawned.end() && found->second ? game.getObjectById<Creature>(found->second) : nullptr;
}

void CameoDirector::update(Game &game) {
    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    if (!area || !game.party().getLeader()) {
        forget();
        return;
    }
    if (area.get() != _area || module->name() != _module) {
        forget();
        _area = area.get();
        _module = module->name();
    }
    auto *coop = game.coop();
    if (coop && coop->isClient()) {
        return; // the host's cameos arrive as replicated entities
    }
    CameoInput input;
    input.module = _module;
    input.enabled = game.options().game.cameos;
    input.guestPlaysShepard = coop && coop->isHost() && coop->hostGuestPlaysShepard(_module);
    input.globalBoolean = [&game](const std::string &name) { return game.getGlobalBoolean(name); };
    input.globalNumber = [&game](const std::string &name) { return game.getGlobalNumber(name); };
    auto wanted = cameosWanted(input);
    {
        // One line whenever the picture changes (the option, a guest's Shepard, the rows).
        auto state = _module + (input.enabled ? " on " : " off ") + (input.guestPlaysShepard ? "guest-shepard " : "") + std::to_string(wanted.size());
        if (state != _lastState) {
            _lastState = state;
            if (!wanted.empty() || input.guestPlaysShepard) {
                info("Cameo: " + _module + ": " + std::to_string(wanted.size()) + " wanted" + (input.enabled ? "" : " (option off)") +
                     (input.guestPlaysShepard ? " (a guest plays John Shepard here)" : ""));
            }
        }
    }

    std::vector<int> gone;
    for (const auto &[row, id] : _spawned) {
        if (std::find(wanted.begin(), wanted.end(), row) == wanted.end()) {
            gone.push_back(row);
        }
    }
    for (int row : gone) {
        despawn(game, *area, row);
        _spawned.erase(row);
    }
    // Not during a conversation's setup frames or a movie: the area is ready once the screen is the game's.
    auto screen = game.currentScreen();
    if (!lenient && screen != Game::Screen::InGame && screen != Game::Screen::Conversation) {
        return;
    }
    for (int row : wanted) {
        if (!_spawned.count(row)) {
            spawn(game, *area, row);
        }
    }
}

void CameoDirector::despawn(Game &game, Area &area, int row) {
    auto found = _spawned.find(row);
    if (found == _spawned.end() || !found->second) {
        return;
    }
    auto creature = game.getObjectById<Creature>(found->second);
    if (!creature) {
        return;
    }
    if (auto *coop = game.coop()) {
        coop->hostForgetCameo(creature->id());
    }
    creature->clearAllActions();
    area.destroyObject(*creature);
}

void CameoDirector::spawn(Game &game, Area &area, int row) {
    const auto &spec = cameoRows().at(row);
    _spawned[row] = 0; // spent whatever happens below; respawned only after the row stops being wanted
    // On the floor, exactly there (the ray starts a little above the spot, so a floor a
    // few decimetres higher than the table's z still counts): a spot the walkmesh does not
    // have gets no cameo.
    scene::Collision collision;
    bool floor = false;
    for (float lift : {0.0f, 0.6f, 1.5f}) {
        if (area.graph().testElevation(glm::vec3(spec.x, spec.y, spec.z + lift), collision)) {
            floor = true;
            break;
        }
    }
    if (!floor) {
        // The room's walkmesh may not be streamed in yet (rooms near the player are): try again
        // on a later look; say so once.
        if (_noFloorWarned.insert(row).second) {
            warn(std::string("Cameo: no floor at the ") + spec.scene + " spot yet; John Shepard waits for it");
        }
        _spawned.erase(row);
        return;
    }
    std::shared_ptr<Creature> cameo;
    try {
        cameo = buildCameo(game, spec);
    } catch (const std::exception &e) {
        warn(std::string("Cameo: cannot build John Shepard for ") + spec.scene + ": " + e.what());
        return;
    }
    if (!cameo) {
        return;
    }
    cameo->setFacing(glm::radians(spec.yaw));
    cameo->setPosition(collision.intersection);
    area.add(cameo);
    _spawned[row] = cameo->id();
    if (auto *coop = game.coop()) {
        coop->hostRegisterCameo(cameo->id());
    }
    info(std::string("Cameo: John Shepard at the ") + spec.scene + " (" + std::to_string(cameo->position().x) + ", " +
         std::to_string(cameo->position().y) + ", " + std::to_string(cameo->position().z) + ")");
}

} // namespace game

} // namespace reone
