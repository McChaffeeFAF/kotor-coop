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

#include "reone/resource/format/gffreader.h"
#include "reone/scene/node/walkmesh.h"

#include "../object.h"
#include "../traps.h"

namespace reone {

namespace game {

class Creature;
class IReputes;

/** Model animation for a utp AnimationState (0-5) and Open flag. */
const char *placeableStateAnimation(int animationState, bool open);

class Placeable : public Object {
public:
    Placeable(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Placeable,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Placeable;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    void damage(
        int amount,
        const std::shared_ptr<Object> &damager) override;

    bool hasInventory() const { return _hasInventory; }
    bool isSelectable() const override { return _usable; }
    bool isUsable() const { return _usable; }
    void setUsable(bool usable) { _usable = usable; }
    /** PartyInteract: a companion leading the party may talk to it itself. */
    bool isPartyInteract() const { return _partyInteract; }
    const std::string &getOnSpellCastAt() const { return _onSpellCastAt; }
    bool isLocked() const { return _locked; }
    /** LightState: whether the placeable's light is switched on (GetPlaceableIllumination). */
    bool isIlluminated() const { return _lightState != 0; }
    void setIlluminated(bool illuminated) { _lightState = illuminated ? 1 : 0; }
    bool isKeyRequired() const { return _keyRequired; }
    bool isLockable() const { return _lockable; }
    /** The tag of the key that opens the lock. */
    const std::string &keyName() const { return _keyName; }
    /** The Security check DC to lock it again (retail CloseLockDC). */
    int closeLockDC() const { return _closeLockDC; }
    /** The Security check DC to pick this lock (retail OpenLockDCk). */
    int openLockDC() const { return _openLockDC; }
    /** Trap fields (read by the trap routines; K1 content traps none). */
    TrapFields trapFields() const {
        TrapFields fields;
        fields.trapped = _trapFlag != 0;
        fields.detectable = _trapDetectable;
        fields.disarmable = _trapDisarmable;
        fields.oneShot = _trapOneShot;
        fields.baseType = _trapType;
        fields.detectDC = _trapDetectDC;
        fields.disarmDC = _disarmDC;
        fields.keyTag = _keyName;
        return fields;
    }
    void disableTrap() { _trapFlag = 0; }
    bool isNotBlastable() const { return _notBlastable; }

    int appearance() const { return _appearance; }
    Faction faction() const { return _faction; }
    std::shared_ptr<scene::WalkmeshSceneNode> walkmesh() const { return _walkmesh; }

    /** Records who locked or unlocked the object and runs its OnLock / OnUnlock script. */
    void runLockScript(bool locked, uint32_t actor);

    void setLocked(bool locked) { _locked = locked; }
    void setHasInventory(bool hasInventory) { _hasInventory = hasInventory; }
    void setOnClosed(std::string script) { _onClosed = std::move(script); }
    void setOnInvDisturbed(std::string script) { _onInvDisturbed = std::move(script); }
    void setOnMeleeAttacked(std::string script) { _onMeleeAttacked = std::move(script); }
    void setOnEndDialogue(std::string script) { _onEndDialogue = std::move(script); }
    void setOnUsed(std::string script) { _onUsed = std::move(script); }

    /// What a click on this placeable does. A container opens. A placeable
    /// with an OnUsed script is used (walk up, then OnUsed): its script
    /// picks the conversation, as 234 of the 248 K1 placeables that have
    /// both a conversation and OnUsed do (k_ptar_startconv, tar05_ffcontrol:
    /// Mission's line or the terminal). Only without OnUsed does a click
    /// start the placeable's own conversation.
    enum class ClickAction {
        OpenContainer,
        Use,
        Converse,
        None
    };
    ClickAction clickAction() const;

    /**
     * A creature opened the container: the lid swings open (close2open, then
     * the open pose) and stays open. The state is AnimationState, so saves
     * and area reloads keep it.
     */
    void openContainer(uint32_t opener);
    /** Seconds until a broken-open container opens for the leader; negative when none is pending. */
    float brokenOpenSeconds() const { return _brokenOpenSeconds; }

    /**
     * PlayAnimation on a placeable: open, close, activate, deactivate and the
     * animloops, through the model's transition animation when it has one.
     */
    void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties()) override;

    /** The model animation PlayAnimation plays first (for ActionPlayAnimation's timer). */
    std::string getAnimationName(AnimationType type) const override;

    int animationState() const { return _animationState; }

    // Scripts

    void onOpen(uint32_t triggererId);
    void runOnUsed(std::shared_ptr<Object> usedBy);
    const std::string &getOnUsed() const { return _onUsed; }
    void runOnInvDisturbed(uint32_t triggerrer, InventoryDisturbType type, uint32_t item);

    /**
     * closer shut the placeable (left its container screen). It is marked
     * closed and its OnClosed script runs on the next simulation update.
     */
    void signalClosed(uint32_t closer);

    /**
     * A conversation ended normally. The OnEndDialogue script runs on the
     * next simulation update.
     */
    void signalEndDialogue();

    /** A creature's physical attack against the placeable was resolved. */
    void runMeleeAttackedScript(uint32_t attackerId);

    /** Queued object events (closed, end of dialogue) not yet run. */
    size_t pendingScriptEventCount() const { return _pendingScriptEvents.size(); }

    void update(float dt) override;

    // END Scripts

private:
    friend class ModuleSnapshotBuilder;
    // Serializable
    resource::LocString _locName;
    bool _autoRemoveKey {false};
    Faction _faction {Faction::Invalid};
    uint8_t _openLockDC {0};
    std::string _keyName;
    bool _trapDisarmable {true};
    bool _trapDetectable {true};
    uint8_t _disarmDC {0};
    uint8_t _trapDetectDC {0};
    uint8_t _trapFlag {0};
    bool _trapOneShot {true};
    uint8_t _trapType {0};
    bool _usable {false};
    bool _static {false};
    bool _notBlastable {false};
    bool _groundPile {false};
    uint32_t _appearance {0};
    uint8_t _hardness {0};
    uint8_t _fort {0};
    uint8_t _will {0};
    uint8_t _ref {0};
    bool _lockable {false};
    bool _locked {false};
    bool _hasInventory {false};
    bool _keyRequired {false};
    uint8_t _closeLockDC {0};
    bool _partyInteract {false};
    uint16_t _portraitId {0};
    uint8_t _bodyBagId {0xFF};
    bool _dieWhenEmpty {false};
    uint8_t _lightState {0};
    resource::LocString _description;

    std::string _onClosed;
    std::string _onDamaged;
    std::string _onDeath;
    std::string _onDisarm;
    std::string _onInvDisturbed;
    std::string _onLock;
    std::string _onMeleeAttacked;
    std::string _onOpen;
    std::string _onSpellCastAt;
    std::string _onUnlock;
    std::string _onUsed;
    std::string _onDialog;
    std::string _onEndDialogue;
    std::string _onTrapTriggered;

    int32_t _animation {-1};
    bool _isBodyBag {false};
    bool _isBodyBagVisible {true};
    bool _isCorpse {false};

    // END Serializable

    int _animationState {0}; // AnimationState
    std::shared_ptr<scene::WalkmeshSceneNode> _walkmesh;

    struct PendingScriptEvent {
        enum class Kind {
            Closed,
            EndDialogue
        };
        Kind kind {Kind::Closed};
        uint32_t actor {script::kObjectInvalid};
    };
    std::vector<PendingScriptEvent> _pendingScriptEvents;

    void runClosedScript(uint32_t closer);
    void runEndDialogueScript();

    // Scripts

    // END Scripts

    void runDamagedScript();
    void runDeathScript();

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void loadAppearance();
    void playStateAnimation();
    /** Go to a looping state, through its transition animation if any. */
    void changeAnimationState(int state, const char *transition);

    /** Seconds until a container that was broken open opens for the leader; negative when none is pending. */
    float _brokenOpenSeconds {-1.0f};

    std::string _transitionAnimation; /**< playing once; then the state loops */

    void updateTransform() override;
};

/** A locked, breakable container the actor may attack to open. */
bool canBashPlaceable(const Placeable &placeable, const Creature &actor, const IReputes &reputes);

} // namespace game

} // namespace reone
