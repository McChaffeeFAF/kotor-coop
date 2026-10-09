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

#include "reone/graphics/types.h"
#include "reone/input/event.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/ifo.h"
#include "reone/resource/types.h"

#include "../contextaction.h"
#include "../object.h"
#include "../player.h"

#include "area.h"

namespace reone {

namespace scene {

class SceneGraph;

}

namespace game {

struct ModuleInfo {
    std::string entryArea;
    glm::vec3 entryPosition {0.0f};
    float entryFacing {0.0f};
    std::string onModLoad;
    std::string onModStart;
    std::string onActivateItem;
    std::string onAcquireItem;
    std::string onUnacquireItem;
    std::string onClientEnter;
    std::string onClientLeave;
    std::string onHeartbeat;
    std::string onPlayerDeath;
    std::string onPlayerDying;
    std::string onRespawnButton;
    std::string onPlayerRest;
    // Mod_XPScale, in tenths: 10 is 100 %.
    int xpScale {10};
    // The IFO game clock (design note c11_data, "Clock").
    int minutesPerHour {0};
    uint32_t startYear {0};
    int startMonth {1};
    int startDay {1};
    int startHour {0};
    int dawnHour {0};
    int duskHour {0};
};

/**
 * The values the module stored when it last handled each kind of event, as
 * the GetLastPlayerDied / GetModuleItemAcquired / ... routines read them.
 * Each stays until the next event of its kind replaces it.
 */
struct ModuleEventRecord {
    uint32_t lastPlayerDied {script::kObjectInvalid};
    uint32_t lastPlayerDying {script::kObjectInvalid};
    uint32_t lastRespawnButtonPresser {script::kObjectInvalid};
    uint32_t itemAcquired {script::kObjectInvalid};
    uint32_t itemAcquiredFrom {script::kObjectInvalid};
    uint32_t itemLost {script::kObjectInvalid};
    uint32_t itemLostBy {script::kObjectInvalid};
};

/** Where the party stands in the defeat flow (every party member dead). */
enum class PartyDefeatState {
    None,
    // Everyone is down; the screen is fading to black.
    Fading,
    // The game is over; Game shows the death screen at the next tick.
    Ended
};

/**
 * The last item activation, as the module's OnActivateItem script and the
 * GetItemActivated* routines see it. It stays until the next activation.
 */
struct ItemActivation {
    uint32_t item {script::kObjectInvalid};
    uint32_t activator {script::kObjectInvalid};
    uint32_t target {script::kObjectInvalid};
    glm::vec3 targetPosition {0.0f};
    float targetFacing {0.0f};
};

class Door;
class Placeable;
class SavedScriptContinuation;
class ModuleSnapshotBuilder;

class Module : public Object {
public:
    Module(
        uint32_t id,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Module,
            "",
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Module;
    }

    void load(std::string name, const resource::Gff &ifo, bool restoreSavedWorld = false);
    // Structural load from records already validated by Game. Fresh-world
    // spawn scripts are deliberately dispatched later via runSpawnScripts(),
    // after the destination has crossed its publication boundary.
    void load(
        std::string name,
        const resource::Gff &ifo,
        const resource::Gff &are,
        const resource::Gff &git,
        bool restoreSavedWorld = false);
    void activate();
    void loadParty(
        const std::string &entry = "",
        bool preserveSavedPlacement = false);
    void runOnLoadScript();
    void runOnStartScript();
    void runSpawnScripts();

    /**
     * An item's Activate Item property was used by activator on target.
     * Records the activation, then runs the module's OnActivateItem script.
     */
    void runOnActivateItem(const Object &item, const Object &activator, const Object &target);

    const ItemActivation &itemActivation() const { return _itemActivation; }

    // Module events. Raising one queues it; updateEvents() (the simulation
    // tick) records its values and runs the module's script for it.

    /** A party member reached 0 hit points and died: dying, then death. */
    void raisePlayerDeath(const Creature &creature);
    /** The player asked to respawn (the death screen's respawn button). */
    void raiseRespawnButton(const Object &presser);
    /** item came into the party's hands; from is where it came from. */
    void raiseItemAcquired(const Object &item, uint32_t from);
    /** item left the party member lostBy. */
    void raiseItemLost(const Object &item, uint32_t lostBy);
    /** A party member finished resting. */
    void raisePlayerRest(const Object &creature);

    /** Run inline: the party arrives in / leaves the module. */
    void runOnClientEnterScript(const Object &entering);
    void runOnClientLeaveScript(const Object &exiting);

    /**
     * One simulation step of the module's own events: the heartbeat, the
     * queued events in order, and the party's death/recovery/defeat flow.
     */
    void updateEvents(float dt);

    size_t pendingModuleEventCount() const { return _moduleEvents.size(); }
    const ModuleEventRecord &eventRecord() const { return _eventRecord; }
    PartyDefeatState defeatState() const { return _defeat; }

    /** Mod_XPScale in tenths (10 is 100 %). */
    int xpScale() const { return _info.xpScale; }

    /**
     * Scale XP the engine computes itself (combat, stealth) by Mod_XPScale.
     * Scripted and plot awards are not scaled.
     */
    int scaleEngineXP(int amount) const {
        return static_cast<int>((static_cast<int64_t>(amount) * _info.xpScale) / 10);
    }

    bool handle(const input::Event &event);
    void update(float dt);

    std::vector<ContextAction> getContextActions(const std::shared_ptr<Object> &object) const;

    // Reputation is directed, so how the player may interact with a creature
    // follows that creature's own view of the party leader, not the reverse
    // relationship: an effect that lowers only the creature's hostility still
    // has to open up conversation. A dead creature is never hostile.
    bool isHostileToPartyLeader(const Creature &creature) const;

    /**
     * A player's left click on an object: the first click selects it, a click
     * on the already selected object acts on it. Returns true when it acted.
     * The mouse and the console click command both come through here.
     */
    bool clickObject(const std::shared_ptr<Object> &object);

    /**
     * Act on an object as a click on the selected object does: attack, loot,
     * talk, open or use it with the party leader.
     */
    void onObjectClick(const std::shared_ptr<Object> &object);

    /**
     * The party leader goes through a transition door: in co-op this starts
     * the ready check, otherwise the module transition is scheduled.
     */
    void useTransitionDoor(const std::shared_ptr<Door> &door, const std::shared_ptr<Creature> &user);

    /**
     * The party leader reached a transition door it was sent to: a shut,
     * locked door runs its fail-to-open; otherwise the leader goes through.
     */
    void useTransitionDoorOnArrival(const std::shared_ptr<Door> &door, const std::shared_ptr<Creature> &user);

    const std::string &name() const { return _name; }

    /**
     * The module's localized name, as authored in the module IFO's Mod_Name.
     * Distinct from name(), which is the module's resource name and is
     * normalized to lower case. Empty when the field resolves to nothing.
     */
    const std::string &localizedName() const { return _localizedName; }

    /** The map screen's "return to base" option (SetReturnStrref). */
    struct ReturnOption {
        // Offered unless a script turns it off (the Endar Spire does).
        bool show {true};
        int strref {0};
        int queryStrref {0};
    };
    const ReturnOption &returnOption() const { return _returnOption; }
    void setReturnOption(ReturnOption option) { _returnOption = option; }

    const ModuleInfo &info() const { return _info; }
    std::shared_ptr<Area> area() const { return _area; }
    Player &player() { return *_player; }
    bool isSaveGame() const { return _isSaveGame; }
    const std::vector<std::shared_ptr<Creature>> &limboCreatures() const { return _limboCreatures; }
    const SavedEventQueue &savedEventQueue() const { return _savedEventQueue; }
    size_t pendingSavedEventCount() const;
    std::vector<SavedEventRecord> saveEventSnapshot() const;
    size_t enqueueSaveEvent(SavedEventRecord event);
    size_t enqueueBoundSaveEvent(
        SavedEventRecord event, bool referencesBound);
    bool cancelSaveEvent(size_t index);

    void deserializeSavedEventQueue(
        const resource::Gff &ifo,
        const SerializedIdentityContext &identityContext);
    void bindSavedEventQueue();
    void publishSavedEventQueue();
    void dispatchDueSavedEvents();

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    std::string _name;
    std::string _localizedName;
    ModuleInfo _info;
    ReturnOption _returnOption;
    ItemActivation _itemActivation;
    std::shared_ptr<Area> _area;
    std::unique_ptr<Player> _player;
    bool _isSaveGame {false};
    std::vector<std::shared_ptr<Creature>> _limboCreatures;
    SavedEventQueue _savedEventQueue;
    std::vector<bool> _savedEventLive;
    std::vector<bool> _savedEventReferencesBound;

    struct PublishedSavedEvent {
        size_t savedIndex {0};
        /**
         * Absolute due time in world milliseconds, composed once from the
         * record's day/time pair when the queue is published. Dispatch then
         * compares clocks without rebuilding a calendar every frame.
         */
        uint64_t dueMilliseconds {0};
        std::shared_ptr<SavedScriptContinuation> continuation;
        bool delivered {false};
    };

    std::vector<PublishedSavedEvent> _publishedSavedEvents;
    bool _savedEventsPublished {false};

    struct ModuleEvent {
        enum class Kind {
            PlayerDying,
            PlayerDeath,
            RespawnButton,
            ItemAcquired,
            ItemLost,
            PlayerRest
        };
        Kind kind {Kind::PlayerDeath};
        uint32_t object {script::kObjectInvalid};
        uint32_t other {script::kObjectInvalid};
    };

    std::vector<ModuleEvent> _moduleEvents;
    ModuleEventRecord _eventRecord;
    float _heartbeatElapsed {0.0f};
    PartyDefeatState _defeat {PartyDefeatState::None};
    bool _defeatReported {false};
    float _defeatElapsed {0.0f};

    void dispatchModuleEvent(const ModuleEvent &event);
    void runHeartbeatScript();
    void runPlayerDyingScript(uint32_t creature);
    void runPlayerDeathScript(uint32_t creature);
    void runRespawnButtonScript(uint32_t presser);
    void runAcquireItemScript(uint32_t item, uint32_t from);
    void runUnacquireItemScript(uint32_t item, uint32_t lostBy);
    void runPlayerRestScript(uint32_t creature);
    void updatePartyVitality(float dt);
    bool fightGoingOn(const std::vector<std::shared_ptr<Creature>> &standing) const;
    bool hasLiveAttack(const Creature &member) const;

    void onCreatureClick(const std::shared_ptr<Creature> &creature);
    void onDoorClick(const std::shared_ptr<Door> &door);
    void onPlaceableClick(const std::shared_ptr<Placeable> &placeable);

    void getEntryPoint(const std::string &waypoint, glm::vec3 &position, float &facing) const;

    // Loading

    void loadInfo(const resource::generated::IFO &ifo);
    void loadArea(
        const resource::generated::IFO &ifo,
        const resource::Gff &are,
        const resource::Gff &git,
        bool restoreSavedWorld = false);
    void loadPlayer();
    void loadLimboCreatures(const resource::Gff &ifo);
    void deliverSavedEvent(PublishedSavedEvent &event);

    // END Loading

    // User input

    bool handleMouseMotion(const input::MouseMotionEvent &event);
    bool handleMouseButtonDown(const input::MouseButtonEvent &event);
    bool handleKeyDown(const input::KeyEvent &event);

    // END User input

    friend class TestGameModule;
};

} // namespace game

} // namespace reone
