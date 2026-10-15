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

#include <map>
#include <utility>

#include "reone/scene/animproperties.h"
#include "reone/scene/graph.h"
#include "reone/scene/node.h"
#include "reone/scene/user.h"
#include "reone/script/types.h"
#include "reone/system/cast.h"
#include "reone/system/portablemath.h"
#include "reone/system/timer.h"

#include "action.h"
#include "action/playanimation.h"
#include "effect.h"
#include "runtimeref.h"
#include "saveprovenance.h"
#include "savedruntime.h"
#include "types.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

/** Rotation about +Z, built without the C library's sin/cos so every platform agrees on the bits. */
inline glm::quat yawQuat(float yaw) {
    float half = 0.5f * yaw;
    return glm::quat(portable::cosf(half), 0.0f, 0.0f, portable::sinf(half));
}

struct ServicesView;

class Action;
class Game;
class Item;
class ModuleSnapshotBuilder;
class Room;

class Object : public scene::IUser, boost::noncopyable {
public:
    enum class RuntimeState {
        Constructing,
        Live,
        Retired,
        Presentation,
    };

    virtual ~Object() = default;

    static bool classof(Object *from) {
        return true;
    }

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    virtual void update(float dt);
    virtual void damage(
        int amount,
        const std::shared_ptr<Object> &damager);
    void heal(int amount) { damage(-amount, nullptr); }

    void face(const Object &other);
    void face(const glm::vec3 &point);
    void faceAwayFrom(const Object &other);

    bool contains(const glm::vec3 &point) const;

    virtual bool isSelectable() const;
    bool isOpen() const { return _open; }

    bool isMinOneHP() const { return _minOneHP; }
    bool isDead() const { return _dead; }
    bool isCommandable() const { return _commandable; }
    bool isInConversation() const { return _isInConversation; }

    float getDistanceTo(const glm::vec2 &point) const;
    float getSquareDistanceTo(const glm::vec2 &point) const;
    float getDistanceTo(const glm::vec3 &point) const;
    float getSquareDistanceTo(const glm::vec3 &point) const;
    float getDistanceTo(const Object &other) const;
    float getSquareDistanceTo(const Object &other) const;

    virtual glm::vec3 getSelectablePosition() const;
    float getFacing() const { return portable::atan2f(2.0f * _orientation.w * _orientation.z, _orientation.w * _orientation.w - _orientation.z * _orientation.z); }

    uint32_t id() const { return _id; }
    Game &game() const { return _game; }
    bool isRuntimeLive() const { return _runtimeState == RuntimeState::Live; }
    bool isPresentationOnly() const {
        return _runtimeState == RuntimeState::Presentation;
    }
    uint64_t runtimeIncarnation() const { return _runtimeIncarnation; }
    const std::string &tag() const { return _tag; }
    ObjectType type() const { return _type; }
    const std::string &blueprintResRef() const { return _blueprintResRef; }
    const std::string &name() const { return _name; }
    const std::string &conversation() const { return _conversation; }
    bool plotFlag() const { return _plot; }

    Room *room() const { return _room; }
    const glm::vec3 &position() const { return _position; }
    const glm::mat4 &transform() const { return _transform; }
    bool visible() const { return _visible; }
    std::shared_ptr<scene::SceneNode> sceneNode() const { return _sceneNode; }

    void setTag(std::string tag) { _tag = std::move(tag); }
    void setConversation(std::string conversation) { _conversation = std::move(conversation); }
    void setName(std::string name) { _name = std::move(name); }
    void setPlotFlag(bool plot) { _plot = plot; }
    void setCommandable(bool commandable) { _commandable = commandable; }
    void setIsInConversation(bool isInConversation) { _isInConversation = isInConversation; }

    void setRoom(Room *room);
    void setPosition(const glm::vec3 &position);

    /**
     * Draw this object where the simulation has it this tick, not blended
     * from the last one (render interpolation): warps and resets.
     */
    void snapPresentation() {
        if (_sceneNode) {
            _sceneNode->snapInterpolation();
        }
    }
    void setFacing(float facing);
    void setVisible(bool visible);

    // Animation

    virtual void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties());

    virtual std::string getAnimationName(AnimationType type) const { return ""; }
    virtual std::string getActiveAnimationName() const { return ""; };

    // END Animation

    // Inventory

    std::shared_ptr<Item> addItem(const std::string &resRef, int stackSize = 1, bool dropable = true);
    void addItem(const std::shared_ptr<Item> &item);
    bool removeItem(const std::shared_ptr<Item> &item, bool &last);
    bool removeItemStack(const std::shared_ptr<Item> &item);
    void moveDropableItemsTo(Object &other);
    /** One dropable item, as moveDropableItemsTo moves each. False when it is not here or not dropable. */
    bool moveDropableItemTo(uint32_t itemId, Object &other);

    std::shared_ptr<Item> getFirstItem();
    std::shared_ptr<Item> getNextItem();
    std::shared_ptr<Item> getItemByTag(const std::string &tag);

    const std::vector<std::shared_ptr<Item>> &items() const { return _items; }

    // Runtime children whose semantic lifetime is owned by this object. New
    // nested game-object types participate in registry finalization by
    // extending this list; Game does not need to know their concrete type.
    virtual std::vector<std::shared_ptr<Object>> ownedRuntimeObjects() const;

    // END Inventory

    // Effects

    void clearAllEffects();
    void removeEffect(const std::shared_ptr<Effect> &effect);
    void applyEffect(const std::shared_ptr<Effect> &effect, DurationType durationType, float duration = 0.0f);
    bool restoreEffect(EffectInstance effect);
    size_t removeEffectsById(EffectId id);

    const std::deque<EffectInstance> &effects() const { return _effects; }
    /**
     * The applied effects with every link opened up: each linked child is
     * seen as an applied effect of its own (the link's id, duration and
     * creator, the child's type and parameters). Modifier queries read this,
     * so a linked ability or attack increase counts like a plain one.
     */
    std::vector<const EffectInstance *> flatEffects() const;
    /** Find the canonical applied record for an exact executable payload. */
    EffectInstance *findEffectInstance(const Effect &effect);
    /**
     * The flattened instance of a linked child (see flatEffects), whose
     * parameters (a resistance's remaining limit) may be worn down.
     */
    EffectInstance *findLinkedChildInstance(EffectId linkId, const Effect &child);
    std::vector<EffectInstance> saveEffectSnapshot() const;
    bool hasEffect(EffectType type) const;
    std::shared_ptr<Effect> getFirstEffect();
    std::shared_ptr<Effect> getNextEffect();

    // END Effects

    // Stunt mode

    bool isStuntMode() const { return _stunt; }

    /**
     * Places this object into the stunt mode. Objects in this mode have their
     * position and orientation fixed to the world origin. Subsequent changes to
     * position and orientation will be buffered and applied when
     * stopStuntMode is called.
     */
    void startStuntMode();

    void stopStuntMode();

    // END Stunt mode

    // Hit Points

    // Base maximum hit points, not considering any bonuses.
    int hitPoints() const { return _hitPoints; }

    // Maximum hit points, after considering all bonuses and penalties.
    int maxHitPoints() const { return _maxHitPoints; }

    // Current runtime hit points.
    int currentHitPoints() const { return _currentHitPoints; }

    void setMinOneHP(bool minOneHP) { _minOneHP = minOneHP; }
    virtual void setMaxHitPoints(int maxHitPoints) { _maxHitPoints = maxHitPoints; }
    virtual void setCurrentHitPoints(int hitPoints) { _currentHitPoints = hitPoints; }

    // END Hit Points

    // Actions

    virtual void clearAllActions(bool force = false);

    /**
     * While alive, the object's executing action no longer counts as the
     * caller of ClearAllActions: a script the action did not run itself (a
     * trigger's OnEnter fired by the action's own step) clears it too.
     */
    class ForeignScriptScope {
    public:
        explicit ForeignScriptScope(Object &object) :
            _object(object), _saved(std::exchange(object._executingAction, std::weak_ptr<Action>())) {}
        ~ForeignScriptScope() { _object._executingAction = _saved; }
        ForeignScriptScope(const ForeignScriptScope &) = delete;
        ForeignScriptScope &operator=(const ForeignScriptScope &) = delete;

    private:
        Object &_object;
        std::weak_ptr<Action> _saved;
    };

    void addAction(std::shared_ptr<Action> action);
    void addActionOnTop(std::shared_ptr<Action> action);
    /** Keymap ClearOneAction: drop the newest queued user action. True if one went. */
    bool cancelLastQueuedAction();
    /** Drop queued random walks: any new command replaces them. */
    void removeRandomWalks(const Action &incoming);
    void delayAction(std::shared_ptr<Action> action, float seconds);

    bool hasUserActionsPending(const Action *excluded = nullptr) const;

    std::shared_ptr<Action> getCurrentAction() const;

    const std::deque<std::shared_ptr<Action>> &actions() const { return _actions; }
    // dropUnsaveable: leave out a live action with no save form instead of
    // throwing (a module transition leaving an NPC mid-cast).
    std::vector<SavedActionRecord> saveActionSnapshot(bool dropUnsaveable = false) const;

    /** Drop live execution and object bindings after their Area was captured. */
    void retireAreaRuntimeState(
        const std::set<const Object *> &retainedObjects);

    // END Actions

    // Combat

    uint32_t getLastHostileActor() const;
    /**
     * GetEnteringObject / GetExitingObject: who last came into (or was let in at) and who last
     * left this object, kept on it, so a script it runs later (DelayCommand, ExecuteScript) still
     * reads them. Set where the module, areas, triggers, encounters, doors and placeables run
     * their enter, exit, click and fail-to-open scripts.
     */
    uint32_t lastEnteredId() const { return _lastEnteredId; }
    uint32_t lastLeftId() const { return _lastLeftId; }
    void setLastEntered(uint32_t id) { _lastEnteredId = id; }
    void setLastLeft(uint32_t id) { _lastLeftId = id; }

    void setLastHostileActor(uint32_t actor);

    uint32_t getLastDamager() const;
    void setLastDamager(const std::shared_ptr<Object> &damager);

    // END Combat

    // Script-visible state
    //
    // Last lock and unlock (GetLastLocked / GetLastUnlocked): who locked or
    // unlocked this object, kept after the OnLock / OnUnlock script has run.
    uint32_t lastLockedBy() const { return _lastLockedBy; }
    uint32_t lastUnlockedBy() const { return _lastUnlockedBy; }
    void noteLockChange(bool locked, uint32_t actor) { (locked ? _lastLockedBy : _lastUnlockedBy) = actor; }

    // Named integer settings scripts store on an object (SetHealTarget and the
    // like). Kept for the Get routines that read them back; not saved.
    int scriptSetting(const std::string &key, int fallback = 0) const {
        auto found = _scriptSettings.find(key);
        return found != _scriptSettings.end() ? found->second : fallback;
    }
    void setScriptSetting(const std::string &key, int value) { _scriptSettings[key] = value; }

    // END Script-visible state

    // Local variables

    bool getLocalBoolean(int index) const;
    int getLocalNumber(int index) const;

    const std::map<int, bool> &localBooleans() const { return _localBooleans; }
    const std::map<int, int> &localNumbers() const { return _localNumbers; }
    void deserializeRuntimeState(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void bindSavedRuntimeState();
    void publishSavedRuntimeState();
    const std::vector<EffectInstance> &savedEffects() const { return _savedEffects; }
    const SavedActionQueue &savedActionQueue() const { return _savedActionQueue; }
    bool hasPublishedSavedRuntimeState() const { return _savedRuntimePublished; }

    void captureSaveRecord(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        SaveRecordOrigin origin = {});
    const std::optional<SaveRecordProvenance> &saveRecordProvenance() const {
        return _saveRecordProvenance;
    }
    std::optional<SerializedObjectIdentity> serializedObjectIdentity() const {
        return _saveRecordProvenance
                   ? _saveRecordProvenance->identity
                   : std::nullopt;
    }
    void assignSerializedObjectIdentity(
        const SerializedObjectIdentity &identity);


    void resolveSavedReferences(
        const std::function<std::shared_ptr<Object>(uint32_t)> &resolver);
    std::shared_ptr<Object> savedReference(std::string_view field) const;



    void setLocalBoolean(int index, bool value);
    void setLocalNumber(int index, int value);

    // END Local variables

    // Scripts

    const std::string &getOnHeartbeat() const { return _onHeartbeat; }
    const std::string &getOnUserDefined() const { return _onUserDefined; }

    /**
     * Drop this object's OnHeartbeat script, leaving its other event scripts
     * alone. Area heartbeat dispatch skips objects without one, so the object
     * stops receiving heartbeats. Used by the KotOR II RemoveHeartbeat routine
     * once a heartbeat script has done its one-off work.
     */
    void clearOnHeartbeat() { _onHeartbeat.clear(); }

    // END Scripts

protected:
    friend class Game;
    friend class CoopReplication;
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;

    // Add one privately constructed Item to a candidate owned graph using the
    // same stacking rules as runtime inventory insertion. Authoritative saved
    // identities can be preserved as distinct records during restoration.
    std::shared_ptr<Item> appendOwnedItemCandidate(
        std::vector<std::shared_ptr<Item>> &items,
        const std::shared_ptr<Item> &item,
        bool preserveSerializedIdentities);
    /** Atomically replace canonical effect state and run exact lifecycle hooks. */
    void replaceEffectState(std::deque<EffectInstance> replacement) noexcept;
    struct DelayedAction {
        std::shared_ptr<Action> action;
        std::unique_ptr<Timer> timer;
    };

    uint32_t _id;
    RuntimeState _runtimeState {RuntimeState::Constructing};
    uint64_t _runtimeIncarnation {0};
    ObjectType _type;
    std::string _sceneName;
    Game &_game;
    ServicesView &_services;

    // Serializable
    std::string _tag;
    std::string _blueprintResRef;
    std::string _conversation;
    std::string _onHeartbeat;
    std::string _onUserDefined;
    bool _minOneHP {false};
    bool _plot {false};
    bool _commandable {true};
    bool _interruptable {false};
    int16_t _hitPoints {0};
    int16_t _maxHitPoints {0};
    int16_t _currentHitPoints {0};
    glm::vec3 _position {0.0f};
    glm::quat _orientation {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<std::shared_ptr<Item>> _items; // FIXME: deserialize
    // END Serializable

    std::string _name;
    bool _dead {false};
    bool _isInConversation {false};
    glm::mat4 _transform {1.0f};
    bool _visible {true};
    Room *_room {nullptr};
    std::deque<EffectInstance> _effects;
    mutable std::map<EffectId, std::vector<EffectInstance>> _linkChildInstances;
    bool _open {false};
    bool _stunt {false};
    std::string _activeAnimName;

    std::shared_ptr<scene::SceneNode> _sceneNode;

    int _itemIndex {0};
    int _effectIndex {0};

    // Actions

    std::deque<std::shared_ptr<Action>> _actions;
    std::vector<DelayedAction> _delayed;
    std::weak_ptr<Action> _executingAction;

    struct LoadedSaveActionSlot {
        SavedActionRecord original;
        std::weak_ptr<Action> runtimeAction;
        bool unsupportedPending {false};
    };
    std::vector<LoadedSaveActionSlot> _loadedSaveActionSlots;

    // END Actions

    RuntimeObjectRef<Object> _lastHostileActor;
    RuntimeObjectRef<Object> _lastDamager;
    uint32_t _lastLockedBy {script::kObjectInvalid};
    uint32_t _lastEnteredId {script::kObjectInvalid};
    uint32_t _lastLeftId {script::kObjectInvalid};
    uint32_t _lastUnlockedBy {script::kObjectInvalid};
    std::map<std::string, int> _scriptSettings;
    std::optional<uint32_t> _savedLastDamagerId;

    // Local variables
    std::map<std::string, uint32_t> _savedReferenceIds;
    std::map<std::string, RuntimeObjectRef<Object>> _savedReferences;
    std::vector<EffectInstance> _savedEffects;
    SavedActionQueue _savedActionQueue;
    SerializedIdentityContext _savedRuntimeIdentityContext;
    std::vector<bool> _savedEffectReferencesBound;
    std::vector<bool> _savedActionReferencesBound;
    bool _savedRuntimeParsed {false};
    bool _savedRuntimePublished {false};
    std::optional<SaveRecordProvenance> _saveRecordProvenance;


    std::map<int, bool> _localBooleans;
    std::map<int, int> _localNumbers;

    // END Local variables

    Object(
        uint32_t id,
        ObjectType type,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        _id(id),
        _type(type),
        _sceneName(std::move(sceneName)),
        _game(game),
        _services(services) {
    }

    virtual void updateTransform();
    virtual bool canExecuteActions() const { return true; }
    bool currentActionKeepsConversationTime() const;

    void deserializeOwnedItems(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext,
        SaveRecordOriginKind originKind,
        bool forceDropable = false,
        std::string originOwner = {});

    // Actions

    void updateActions(float dt);
    void removeCompletedActions();
    void updateDelayedActions(float dt);

    void executeActions(float dt);

    // END Actions

    // Effects

    void updateEffects(float dt);
    virtual void onEffectsCleared() {}

    int applyDamageToHitPoints(int amount, int currentHitPoints);

    // END Effects
};

} // namespace game

} // namespace reone
