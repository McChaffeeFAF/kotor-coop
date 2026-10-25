/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "reone/game/types.h"

namespace reone {

namespace game {

class Area;
class Creature;
class Game;

/**
 * John Shepard cameos (design note shepard_cameos): an easter egg. The same
 * man as the Story Mode stand-in (coopShepard in coopcast.h) stands in the
 * background of a few places and scenes, a stage prop and nothing else:
 *
 * - not in the party, not recruitable, not attackable (plot, and nothing
 *   perceives him), no scripts, no conversation, no tag any script keys on;
 * - invisible to script searches (nearest creature or object, objects in an
 *   area or a shape, faction members): see Creature::isCameo;
 * - clicking him plays one line of his sound set and nothing more;
 * - spawned by the host (or in single player) from the table below when the
 *   Cameos option is on, replicated to guests like a stand-in, never spawned
 *   while a guest plays John Shepard in that module, gone when the area
 *   unloads and never written into a save.
 *
 * The table is our own data: module, spot, facing, pose, outfit, condition.
 */

/** User setting (reone_settings.ini) behind Options > Gameplay > "Cameos"; on by default. */
constexpr const char *kCameosSetting = "optgameplay.CB_CAMEOS";

/** The template field of a cameo on the wire (a stand-in template, see coopstandin.h). */
constexpr const char *kCoopCameoTemplate = "coop_shepard_cam";

/** The tag of every cameo: nothing in the game keys on it. */
constexpr const char *kCameoTag = "coop_cameo";

struct CameoRow {
    const char *module;    // module resref, lower case
    const char *scene;     // what the place or moment is (documentation, logs)
    float x, y, z;         // metres; z is where the floor roughly is, he lands on the walkmesh
    float yaw;             // degrees counter-clockwise from +y (0 faces +y, 90 faces -x)
    const char *pose;      // idle pose: "stand", "pause2", "pause3", "listen", "ready"
    ClassType clazz;       // the class he is (nothing depends on it)
    const char *body;      // outfit: body item template
    const char *condition; // see cameoConditionHolds; empty: always
};

const std::vector<CameoRow> &cameoRows();

/** What the decision knows. */
struct CameoInput {
    std::string module;
    bool enabled {true};           // the Cameos option
    bool guestPlaysShepard {false}; // a co-op guest plays John Shepard in this module
    std::function<bool(const std::string &)> globalBoolean;
    std::function<int(const std::string &)> globalNumber;
};

/**
 * A condition: terms joined by '&', all must hold. A term is a global
 * boolean (NAME, !NAME), or a global number compared with a literal
 * (NAME=5, NAME!=5, NAME>=5, NAME<5 ...). An empty condition holds; a global
 * the input cannot read counts as false / 0.
 */
bool cameoConditionHolds(const std::string &condition, const CameoInput &input);

/** Indices into cameoRows() of the cameos that stand in this module now. Deterministic. */
std::vector<int> cameosWanted(const CameoInput &input);

/** The pose name as an animation, or false for "stand" and unknown names. */
bool cameoPoseAnimation(const std::string &pose, AnimationType &animation);

/** The row of the module whose spot is nearest to the position (within 3 m), or -1: a guest's screen finds the pose that way. */
int cameoRowNear(const std::string &module, float x, float y);

/**
 * The cameo as a creature (not added to an area): John Shepard at level 1,
 * puppet scrub, tag, plot, wearing the row's outfit. Null when his class
 * does not load.
 */
std::shared_ptr<Creature> buildCameo(Game &game, const CameoRow &row);

/** Turns a creature into a stage prop: the exclusions, the pose. Shared with the guest's build. */
void makeCameo(Game &game, Creature &creature, const CameoRow *row);

/** One line from his sound set (a click on him). */
void cameoBark(Creature &creature);

/**
 * Keeps the cameos of the loaded area in step with the table, the Cameos
 * option and a co-op guest's character. Game::tick runs update a few times a
 * second; clients of a co-op session leave it to the host (they see cameos
 * as replicated stand-ins).
 */
class CameoDirector {
public:
    /** Tests: no screen or walkmesh floor is required (the test engine has neither). */
    bool lenient {false};

    void update(Game &game);

    /** Forget the area (it unloaded: its objects went with it). */
    void forget();

    /** Row indices of the cameos standing in the loaded area. */
    std::vector<int> active() const;

    std::shared_ptr<Creature> creatureFor(Game &game, int row) const;

private:
    const Area *_area {nullptr};
    std::string _module;
    std::map<int, uint32_t> _spawned; // row -> creature id (0: spent, not respawned until the row stops being wanted)
    std::set<int> _noFloorWarned;
    std::string _lastState;

    void despawn(Game &game, Area &area, int row);
    void spawn(Game &game, Area &area, int row);
};

} // namespace game

} // namespace reone
