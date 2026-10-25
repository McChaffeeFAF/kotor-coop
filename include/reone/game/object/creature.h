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

#include <optional>

#include <array>
#include <deque>

#include "reone/audio/clip.h"
#include "reone/audio/source.h"
#include "reone/graphics/lipanimation.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/parser/gff/git.h"
#include "reone/resource/strings.h"
#include "reone/resource/types.h"
#include "reone/scene/animeventlistener.h"
#include "reone/scene/node/model.h"
#include "reone/script/types.h"
#include "reone/system/timer.h"

#include "../d20/attributes.h"
#include "../d20/itemattributes.h"
#include "../object.h"
#include "../menupresentation.h"
#include "../runtimeref.h"
#include "../pathfinder.h"
#include "../visualeffectpresenter.h"

#include "item.h"

namespace reone {

namespace resource {
class Gff;
}

namespace game {

constexpr float kDefaultAttackRange = 2.0f;

/**
 * Playback rate of a walk/run clip whose loop covers clipDistance metres in
 * clipLength seconds, for a creature moving at groundSpeed metres per second.
 * 1 when any input is missing.
 */
float gaitPlaybackRate(float groundSpeed, float clipLength, float clipDistance);

class DamagePacket;
class Spell;
class ModuleSnapshotBuilder;
struct AttackBonusBreakdown;

class Creature : public Object, public scene::IAnimationEventListener {
public:
    enum class ModelType {
        Creature,
        Droid,
        Character
    };

    enum class MovementType {
        None,
        Walk,
        Run
    };

    struct BodyBag {
        std::string name;
        int appearance {0}; /**< index into placeables.2da */
        bool corpse {false};
    };

    struct Perception {
        float sightRange {0.0f};
        float hearingRange {0.0f};
        std::map<uint32_t, RuntimeObjectRef<Object>> seen;
        std::map<uint32_t, RuntimeObjectRef<Object>> heard;

        bool sees(uint32_t id) const {
            auto found = seen.find(id);
            return found != seen.end() && found->second.resolve() != nullptr;
        }
        bool hears(uint32_t id) const {
            auto found = heard.find(id);
            return found != heard.end() && found->second.resolve() != nullptr;
        }
    };

    struct CombatState {
        bool active {false};
        bool shouldDeactivate {false};
        bool debilitated {false};
        RuntimeObjectRef<Object> attackTarget;
        RuntimeObjectRef<Object> attemptedAttackTarget;
        RuntimeObjectRef<Object> attemptedSpellTarget;
        ActionType attackAction {ActionType::QueueEmpty};
        FeatType combatFeat {FeatType::Invalid};
        Timer deactivationTimer;
    };

    Creature(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services);

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Creature;
    }

    void loadFromBlueprint(const std::string &resRef);
    void loadAppearance();

    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);

    /**
     * Invalidate state that only exists while this retained creature inhabits
     * one Area. The owning Area supplies its still-live Pathfinder so path
     * handles are released before that owner is destroyed.
     */
    void retireAreaRuntime(
        Pathfinder &pathfinder,
        const std::set<const Object *> &retainedObjects);

    void update(float dt) override;

    void clearAllActions(bool force = false) override;
    void damage(
        int amount,
        const std::shared_ptr<Object> &damager) override;

    void giveXP(int amount);
    void setXP(int xp);

    void playSound(resource::SoundSetEntry entry, bool positional = true);

    void startTalking(const std::shared_ptr<graphics::LipAnimation> &animation);
    void stopTalking();

    bool isSelectable() const override;
    bool isMovementRestricted() const { return _movementRestricted || !canExecuteActions(); }
    bool isLevelUpPending() const;

    glm::vec3 getSelectablePosition() const override;
    float getAttackRange() const;
    int getNeededXP() const;
    float challengeRating() const { return _challengeRating; }
    void setChallengeRating(float rating) { _challengeRating = rating; }
    /** PartyInteract: a companion leading the party may talk to it itself. */
    bool isPartyInteract() const { return _partyInteract; }

    Gender gender() const { return _gender; }
    const std::string &firstNameText() const { return _firstName.str(); }
    const std::string &lastNameText() const { return _lastName.str(); }
    ModelType modelType() const { return _modelType; }
    int appearance() const { return _appearance; }
    CreaturePresentation presentation() const;
    void setPresentation(const CreaturePresentation &presentation);
    uint16_t portraitId() const { return _portraitId; }
    std::shared_ptr<graphics::Texture> portrait() const { return _portrait; }
    // Movement speeds in m/s from creaturespeed.2da, selected by the
    // creature's movement rate (7 = the appearance's moverate row).
    float walkSpeed() const;
    float runSpeed() const;
    int walkRate() const { return _walkRate; }
    void setWalkRate(int rate) { _walkRate = rate; }
    float creaturePersonalSpace() const { return _creaturePersonalSpace; }
    CreatureSize size() const { return _size; }
    CreatureAttributes &attributes() { return _attributes; }
    const CreatureAttributes &attributes() const { return _attributes; }
    ItemAttributes &itemAttributes() { return _itemAttributes; }
    const ItemAttributes &itemAttributes() const { return _itemAttributes; }
    Faction faction() const { return _faction; }

    /**
     * Faction used for reputation between creatures: a party member counts
     * as the player faction whatever its own faction (K2 behaviour; Trask's
     * template is Neutral). design note p1_combat.
     */
    Faction reputationFaction() const;
    int xp() const { return _xp; }
    Alignment alignment() const;
    RacialType racialType() const { return _race; }

    /**
     * @return the skill value scripts see (GetSkillRank): ranks plus the key
     *         ability modifier, or -1/0 per the skills.2da use rules
     */
    int getSkillValue(SkillType skill) const;
    Subrace subrace() const { return _subrace; }
    void setRacialType(RacialType race) { _race = race; }
    void setSubrace(Subrace subrace) { _subrace = subrace; }
    NPCAIStyle aiStyle() const { return _aiStyle; }
    int walkmeshMaterial() const { return _walkmeshMaterial; }
    bool isPC() const { return _isPC; }
    /** Marks the player character (template/save IsPC); re-derives force points. */
    void setIsPC(bool isPC);
    int assignedPuppet() const { return _assignedPuppet; }
    bool isPuppet() const { return _puppet; }
    /**
     * An ambient cameo (cameo.h): a stage prop. Scripts never see it (searches,
     * shapes, faction loops), nothing perceives it, it is not saved.
     */
    bool isCameo() const { return _cameo; }

    void setGender(Gender gender) { _gender = gender; }
    void setAppearance(int appearance) { _appearance = appearance; }
    void setMovementType(MovementType type);
    void setFaction(Faction faction) { _faction = faction; }
    /** Set the serialized base maximum and preserve the existing damage. */
    void setMaxHitPoints(int baseHitPoints) override;
    void setCurrentHitPoints(int hitPoints) override;

    /**
     * Recalculate permanent vitality after attributes, levels or feats change,
     * preserving the exact amount of damage already sustained.
     */
    void recalculatePermanentVitality();

    /** Initialize a newly generated creature at full derived vitality. */
    void initializeGeneratedVitality();

    /** Current vitality translated back to the serialized base-HP axis. */
    int serializedCurrentHitPoints() const;
    void setMovementRestricted(bool restricted) { _movementRestricted = restricted; }
    /**
     * Its player is steering it right now (a co-op guest's movement keys):
     * it is not idle, so the idle engage and the auto-combat retry wait.
     */
    void noteDirectControl() {
        _engageIdle = 0.0f;
        _autoCombatIdle = 0.0f;
    }
    void setImmortal(bool immortal) { _immortal = immortal; }
    // A friendly player character or companion that is not in the local
    // party, e.g. another co-op player's. Friendly collision treats it like a
    // party member.
    void setCoopPartyCharacter(bool value) { _coopPartyCharacter = value; }
    bool isCoopPartyCharacter() const { return _coopPartyCharacter; }

    /**
     * Encounter provenance (GetIsEncounterCreature): the encounter that
     * spawned this creature, or kObjectInvalid.
     */
    uint32_t encounterId() const;
    void setEncounterId(uint32_t encounterId) {
        _encounterId = encounterId;
        _encounterNotified = false;
    }
    /**
     * Tell the spawning encounter, once, that this creature died or left
     * alive; see Encounter::onSpawnedCreatureGone.
     */
    void notifyEncounterGone(bool died);

    // Death handling (SetIsDestroyable). Defaults: destroyable, raiseable,
    // not selectable when dead.
    bool isDestroyable() const { return _destroyable; }
    bool isRaiseable() const { return _raiseable; }
    void setRaiseable(bool raiseable) { _raiseable = raiseable; }
    bool isSelectableWhenDead() const { return _selectableWhenDead; }
    void setDestroyable(bool destroyable, bool raiseable, bool selectableWhenDead);

    /**
     * Bring a dead, raiseable creature back to life with hpPercent of its
     * maximum hit points (at least 1). Returns false when there was nothing
     * to raise.
     */
    bool resurrect(int hpPercent);

    /**
     * Script control of the lightsaber blades (SetLightsaberPowered). While an
     * override stands, the game's own power changes keep the overridden state.
     */
    void setLightsaberPowerOverride(bool override, bool powered, bool showTransition);
    std::optional<bool> lightsaberPowerOverride() const { return _lightsaberPowerOverride; }

    /** Held in the choke pose by a Choke effect. */
    bool isChoked() const;

    // Knockdown and held poses (design note c08_effects)

    enum class KnockdownPhase {
        None,
        Falling,
        Down,
        GettingUp
    };

    /** Throw speed of a knockback, metres per second. */
    // K2 behaviour: a pushed creature slides at 25 m/s (5 m in 0.2 s).
    static constexpr float kKnockbackSpeed = 25.0f;

    /**
     * Knock the creature off its feet: the fall, held down while a knockdown
     * effect remains, then the get-up. A creature already down is not knocked
     * down again. Advanced in the simulation tick.
     */
    void knockDown();

    /**
     * Throw the creature along the ground, away along direction, over the
     * following ticks. Walls, doors and creatures stop the throw unless
     * ignoreObstacles; it never leaves the walkmesh.
     */
    void startKnockback(const glm::vec2 &direction, float distance, bool ignoreObstacles);

    KnockdownPhase knockdownPhase() const { return _knockdown.phase; }
    bool isKnockedDown() const { return _knockdown.phase != KnockdownPhase::None; }
    float knockbackRemaining() const { return _knockback.remaining; }

    /** The looping clip a held-state effect (whirlwind, paralysis, ...) keeps, if any. */
    std::string getHeldPoseAnimation() const;

    /**
     * Knocked down or held in a pose by an effect: no actions, no movement,
     * no combat reactions.
     */
    bool isHeldInPose() const;

    /**
     * SurrenderToEnemies / SurrenderRetainBuffs: become neutral to everyone
     * and make every creature within 10 m stop what it is doing. Unless
     * retainOwnEffects, effects another creature put on this one are removed
     * (design note c08_effects). The player character is exempt.
     */
    void surrender(bool retainOwnEffects);

    /** Re-evaluate the shown appearance after a Disguise effect or item changed. */
    void refreshDisguise() { updateEquipmentPresentation(); }

    /** Walk and run speed multiplier from movement speed effects. */
    float getMovementSpeedFactor() const;

    /** Held in place by an Entangle effect: it can act, but not move. */
    bool isEntangled() const;

    // Batch C08b effect queries (design note c08b_effects).

    /** Blinded (EffectBlind): the creature sees nothing. */
    bool isBlind() const;
    /**
     * Distracted by Mind Trick or Droid Trick: sees party members only when
     * they come within kDistractedNoticeRange.
     */
    bool isDistracted() const;
    static constexpr float kDistractedNoticeRange = 2.0f;
    /** Under Psychic Static (a confusion effect). */
    bool isConfused() const;
    /** The best Force resistance increase, less every decrease, never below 0. */
    int getForceResistance() const;
    /** Blaster bolt deflection increases less decreases, from effects. */
    int getBlasterDeflectionModifier() const;
    /** Vitality / force point regeneration modifiers, percent, summed. */
    int getVitalityRegenPercent() const;
    int getForcePointRegenPercent() const;
    /** Live VP regeneration modifier effects, counted (each doubles natural HP regeneration). */
    int getVitalityRegenModifierCount() const;
    /**
     * Summed value of the equipped, active Regeneration (hit points) or
     * RegenerationForcePoints (force points) item properties.
     */
    int getItemRegeneration(bool forcePoints) const;
    /** Force Body level (0, 1, 2) while one applies, else -1; the best one wins. */
    int getForceBodyLevel() const;
    /** Body Fuel is on. */
    bool hasBodyFuel() const;
    /** Fury is on; each kill while it is adds 1 to its damage bonus, up to 6. */
    bool isInFury() const;
    int furyDamageBonus() const { return isInFury() ? _furyDamageBonus : 0; }
    void resetFury() { _furyDamageBonus = 0; }
    void addFuryKill();
    static constexpr int kMaximumFuryDamageBonus = 6;

    /**
     * EffectFactionModifier: the creature belongs to another faction while
     * the effect lasts. The latest modifier wins; with none left the creature
     * returns to the faction it had before the first one.
     */
    void refreshFactionModifier();
    /** The faction to save: the creature's own, not a modifier's. */
    Faction serializedFaction() const;

    /** A held-state effect began: stop moving and take its pose. */
    void enterHeldPose();

    /** A held-state effect ended: take whatever pose the state now calls for. */
    void leaveHeldPose();

    void setAIStyle(NPCAIStyle style) { _aiStyle = style; }
    void setAssignedPuppet(int puppet) { _assignedPuppet = puppet; }
    void setPuppet(bool puppet) { _puppet = puppet; }
    void setCameo(bool cameo) { _cameo = cameo; }
    void setWalkmeshMaterial(int material) { _walkmeshMaterial = material; }

    // Animation

    void playAnimation(AnimationType type, scene::AnimationProperties properties = scene::AnimationProperties()) override;

    void playAnimation(CombatAnimation anim, CreatureWieldType wield, int variant = 1);
    void playAnimation(const std::string &name, scene::AnimationProperties properties = scene::AnimationProperties());
    bool playAnimation(const std::shared_ptr<graphics::Animation> &anim, scene::AnimationProperties properties = scene::AnimationProperties());
    // Holds an externally sourced animation until resumeStateDrivenAnimation is called.
    bool playExternalAnimation(const std::shared_ptr<graphics::Animation> &anim, scene::AnimationProperties properties = scene::AnimationProperties());
    void resumeStateDrivenAnimation();

    /**
     * Play an animation as a layer over whatever the creature is already doing,
     * including while it is walking or running. Unlike the other playAnimation
     * overloads this neither waits for the creature to stand still nor takes
     * over its state-driven animation, so locomotion carries on underneath and
     * the layer disappears on its own once it has run.
     */
    void playOverlayAnimation(AnimationType type);

    void updateModelAnimation();
    void syncTalkLayer(scene::ModelSceneNode &model);
    void updateDecay(float dt);
    void spawnBodyBag();

    // END Animation

    // Equipment

    bool equip(const std::string &resRef);
    /** Validate an equipment commit without changing ownership. */
    bool canEquip(int slot, const std::shared_ptr<Item> &item) const;
    bool equip(int slot, const std::shared_ptr<Item> &item);
    /** Validate an equipment replacement without changing ownership. */
    bool canReplaceEquipment(
        int slot,
        const std::shared_ptr<Item> &item,
        const Object &displacedReceiver) const;
    bool replaceEquipment(
        int slot,
        const std::shared_ptr<Item> &item,
        Object &displacedReceiver);
    /** Remove the equipment ownership edge for immediate transfer or retirement. */
    std::shared_ptr<Item> takeEquippedItem(const std::shared_ptr<Item> &item);
    /** Unequip an Item directly into an explicit owning inventory. */
    bool moveEquippedItemTo(
        const std::shared_ptr<Item> &item,
        Object &receiver);

    bool isSlotEquipped(int slot) const;

    /**
     * Exchange the two weapon sets: the items in the main- and off-hand slots
     * trade places with those in the second set's slots, empty slots
     * included. Returns false, changing nothing, when all four are empty.
     */
    bool swapWeaponSets();
    /**
     * Rebuild the effects of every equipped item and its presentation, after
     * an equipped item's properties or model changed (an upgrade assembled).
     */
    void refreshEquippedItems();

    std::shared_ptr<Item> getEquippedItem(int slot) const;
    CreatureWieldType getWieldType() const;
    /**
     * Conversation UnequipItems/UnequipHItem: hide the weapon models and the
     * head item for a scene. Presentation only; equipment is unchanged.
     */
    void setSceneHiddenEquipment(bool weapons, bool headItem);
    /** Plays a sound-set entry: always for the leader, by chance for others within 30 m. */
    bool bark(resource::SoundSetEntry entry, int othersPercent);
    /** Swing/clash/parry and body-fall model events; true when handled. */
    bool playFoleyEvent(const std::string &name);
    void presentBladeContact();
    bool sceneHidesWeapons() const { return _sceneHideWeapons; }
    bool sceneHidesHeadItem() const { return _sceneHideHeadItem; }

    const std::map<int, std::shared_ptr<Item>> &equipment() const { return _equipment; }
    std::vector<std::shared_ptr<Object>> ownedRuntimeObjects() const override;

    // END Equipment

    // Pathfinding
    bool navigateTo(const glm::vec3 &dest, bool run, float distance, float dt);
    void clearPath();
    // True once repeated path requests toward the current destination have
    // failed. Callers use it to abandon an action instead of waiting forever.
    bool navigationFailed() const { return _pathRequestFailures >= kMaxPathRequestFailures; }
    // Backstop (ours): seconds without getting closer to the destination
    // before navigation gives up even when nothing counted as a bump.
    static constexpr float kNavigationGiveUpSeconds = 6.0f;
    void advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt);
    glm::vec3 computeSteeringForce(const Uniwalk &uni, const glm::vec3 &next, float dt);
    // Direction of a side-step away from an obstruction: perpendicular to
    // \p desired, towards the side that is on the walkmesh and clear of
    // creatures. Deterministic; attempts alternate sides.
    glm::vec3 chooseSideStep(const Uniwalk &uni, const glm::vec3 &desired, const glm::vec3 &keepout) const;
    // Lateral force that steers around creatures standing in the way along
    // \p forward. Creatures are considered in object-id order; a creature
    // straight ahead is passed on the right.
    glm::vec3 computeCreatureAvoidance(const glm::vec3 &forward) const;
    void registerBump();
    void rememberPassSide();
    MovementType movementType() const { return _movementType; }
    bool isTalking() const { return _talking; }
    /**
     * A party member that arrived on a FollowLeader keeps following: when
     * the leader moves off, it goes after it again at once instead of
     * standing until its heartbeat re-issues the follow (up to 6 s, the
     * follower's "inefficient" bursts). Anything else it is ordered ends it.
     */
    void setFollowingLeader(bool following) { _followingLeader = following; }
    bool isFollowingLeader() const { return _followingLeader; }
    // END Pathfinding

    // Blocking doors

    /**
     * Remember the door that obstructed the last attempted step. Written by the
     * collision layer for every mover, including the directly controlled player.
     *
     * This lives only to carry the obstruction from the collision test to the
     * blocked event raised after the step. It is not what scripts read:
     * GetBlockingDoor answers from the argument captured when the event was
     * raised, so it stays fixed for that run while this keeps changing.
     */
    void setBlockingDoor(uint32_t doorId) { _blockingDoorId = doorId; }

    void clearBlockingDoor() { _blockingDoorId = script::kObjectInvalid; }
    // What stopped this tick's step, if anything: another creature, or static
    // geometry (walls, doors, placeables). Recorded by Area::moveCreature.
    void recordStepBlock(uint32_t creatureId, bool staticObstruction) {
        _stepBlockCreature = creatureId;
        _stepBlockStatic = staticObstruction;
    }
    // The side this creature passes a blocker on when both sides score the
    // same (K2: one remembered flag per creature, flipped by every detour).
    bool passesOnLeft() const { return _passLeft; }
    void setPassesOnLeft(bool left) { _passLeft = left; }
    int bumpCount() const { return _bumpCount; }

    uint32_t blockingDoorId() const { return _blockingDoorId; }

    /**
     * Edge-trigger ScriptOnBlocked for the door currently obstructing this
     * creature. Called by navigation after each attempted step, so it only
     * applies to AI, script and action driven movement. A continuous
     * obstruction by the same door reports once; an unobstructed step re-arms.
     */
    void dispatchBlockedEvent();

    // END Blocking doors

    // Perception

    void onObjectSeen(const std::shared_ptr<Object> &object);
    void onObjectVanished(const std::shared_ptr<Object> &object);
    void onObjectHeard(const std::shared_ptr<Object> &object);
    void onObjectInaudible(const std::shared_ptr<Object> &object);

    void setObjectSeen(const std::shared_ptr<Object> &object, bool seen);
    void setObjectHeard(const std::shared_ptr<Object> &object, bool heard);
    void runOnNotice(const Object &object, bool heard, bool seen);
    void refreshVisibilityPerception();

    static constexpr uint8_t kSeeInvisibleCounter = 0x01;
    static constexpr uint8_t kUltravisionCounter = 0x02;
    static constexpr uint8_t kTrueSeeingCounter = 0x04;

    void setVisibilityCounter(uint8_t bit);
    void restoreVisibilityCounter(
        EffectType type,
        uint8_t bit,
        EffectId removedEffect,
        bool trueSeeingRemovalQuirk = false);
    bool hasVisibilityCounter(uint8_t bits) const;

    const Perception &perception() const { return _perception; }

    // END Perception

    // Combat

    void activateCombat();
    void deactivateCombat(float delay);

    /**
     * Hand this creature's combat to its AI: on its next update with an empty
     * action queue, and while it is not a player-controlled leader, it runs
     * its end-of-round script once (the generic AI decides its next round).
     */
    void requestCombatHandover() { _combatHandoverPending = true; }
    bool isCombatHandoverPending() const { return _combatHandoverPending; }

    /**
     * Auto-combat (opt-in, per character; it stays with the character across
     * leader switches): while this creature leads, its combat rounds are
     * decided by the same generic AI party members use. Queued player actions
     * run first.
     */
    bool autoCombat() const { return _autoCombat; }
    void setAutoCombat(bool autoCombat) { _autoCombat = autoCombat; }
    // Stealth Mode (design note stealth)

    /** In Stealth Mode: hostile observers notice it only by an Awareness check. */
    bool isStealthed() const { return _stealthMode; }
    /** Turn the mode on or off with no checks (the game's request path checks). */
    void setStealthMode(bool on);
    /** A stealth unit (baseitems.2da Stealth_Unit) in the belt or creature armour slot. */
    bool hasStealthUnitEquipped() const;
    /** Stealthed and without K2's Stealth Run feat: walking only. */
    bool stealthWalkOnly() const;
    /** Co-op client: the host's word on the mode, for the HUD and walking (no visual of its own). */
    void setReplicaStealthMode(bool on) { _stealthMode = on; }
    /** Co-op client: the host's level-up state for this creature (HUD icon, level-up button). */
    void setReplicaLevelUp(bool due);

    bool brainRepositioned() const { return _brainRepositioned; }
    void setBrainRepositioned(bool value) { _brainRepositioned = value; }

    /**
     * Cancel Combat: clear the queue and leave combat mode now.
     */
    void cancelCombatNow();

    /**
     * Cancel Combat holds a player's character back: no automatic attack is
     * queued for it (end-of-round continuation, co-op idle engage) until its
     * player gives another order or no enemy is in sight.
     */
    bool isCombatHeld() const { return _cancelCombatPending || _combatHeld; }

    /** Run the end-of-round AI for this leader as if it were a follower. */
    void runAutoCombatRound();

    bool isInCombat() const { return _combatState.active; }
    bool isDebilitated() const;
    bool isTemporarilyDead() const;
    bool isInvisibleTo(const Creature &observer) const;
    void clearHostileActionsAgainst(const Object &object);
    bool isTwoWeaponFighting() const;
    std::shared_ptr<Item> getOffhandAttackWeapon() const;

    /** The stored base force points (ForcePoints), before Wisdom. */
    int forcePoints() const { return _forcePoints; }
    int currentForce() const { return _currentForce; }
    /** The derived maximum (design note c03_stats, "Maximum"). */
    int maxForcePoints() const { return _maxForcePoints; }

    /**
     * Raises the base (blueprint) force points, as a level-up does; the
     * maximum is re-derived and current moves by the same change.
     */
    void addBaseForcePoints(int amount);

    /**
     * Re-derive the maximum after levels, classes or Wisdom change. Current
     * force points move by the change and stay within 0..maximum.
     */
    void recalculateForcePoints();

    /** Lowers the current force points, not below zero. */
    void drainForcePoints(int amount);

    /** Sets current force points within 0..maximum plus the temporary pool. */
    void setCurrentForcePoints(int value);

    /** Current force points on the serialized base axis (CurrentForce). */
    int serializedCurrentForce() const;

    /**
     * Temporary force points and hit points (design note c08b_effects): a
     * separate pool that the current value includes and the maximum does not.
     * Adding raises the current value by the amount; removing lowers it by the
     * amount (never below 0 for force points; hit points that fall below 1
     * kill the creature).
     */
    int temporaryForcePoints() const { return _temporaryForce; }
    void addTemporaryForcePoints(int amount);
    void removeTemporaryForcePoints(int amount);
    int temporaryHitPoints() const { return _temporaryHitPoints; }
    void addTemporaryHitPoints(int amount);
    void removeTemporaryHitPoints(int amount);

    /**
     * What spell costs this creature in force points (spells.2da forcepoints,
     * forceadjust.2da; under Force Body, only its force point share).
     */
    int forcePowerCost(const Spell &spell) const;
    /** Under Force Body, the share of a cost paid in force points (and again in hit points). */
    int forceBodyShare(int cost) const;
    bool canAffordForcePower(const Spell &spell) const;
    /** Pays for a power at the start of its cast; never fails, clamps at 0. */
    void spendForcePoints(const Spell &spell);
    /**
     * The cast of \p spell has begun: its force point cost is recorded as the
     * charge stands now, whether or not this cast pays it (an item or cheat
     * cast does not). GetSpellForcePointCost reads the recorded cost.
     */
    void recordSpellForcePointCost(const Spell &spell);
    /** The cost recorded by the last cast of \p spell, if that was the last cast recorded. */
    std::optional<int> recordedSpellForcePointCost(SpellType spell) const;

    /**
     * AddMultiClass: the class joins at level 0 (appended, or replacing the
     * second slot) and XP is topped up to the next level's threshold.
     * @return false if nothing changed
     */
    bool addMultiClass(ClassType classType);

    /** Shift GoodEvil by delta, kept within 0..100. */
    void adjustGoodEvil(int delta);
    void setGoodEvil(int value) { _goodEvil = static_cast<uint8_t>(std::clamp(value, 0, 100)); }

    /** GetDamageDealtByType: damage of one DAMAGE_TYPE_* bit in the last damage event. */
    int lastDamageOfType(int damageTypeBit) const;
    /** Record a typed part of damage about to be applied by damage(). */
    void recordTypedDamage(int amount, int damageFlags, const std::shared_ptr<Object> &damager);
    /** GetLastKiller: the object whose damage killed this creature. */
    uint32_t lastKiller() const;

    uint32_t getAttemptedAttackTarget() const {
        auto target = _combatState.attemptedAttackTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    std::shared_ptr<Object> getAttackTarget() const {
        return _combatState.attackTarget.resolve();
    }
    uint32_t getLastHostileTarget() const {
        auto target = _lastHostileTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    uint32_t getAttemptedSpellTarget() const {
        auto target = _combatState.attemptedSpellTarget.resolve();
        return target ? target->id() : script::kObjectInvalid;
    }
    ActionType getLastAttackAction() const { return _lastAttackAction; }
    int getLastForcePower() const { return _lastForcePower; }
    bool wasLastForcePowerSuccessful() const { return _lastForcePowerSuccess; }
    int lastDamageTotal() const { return _lastDamageTotal; }
    uint32_t blockingCreature() const {
        auto blocker = _blockingCreature.resolve();
        return blocker ? blocker->id() : script::kObjectInvalid;
    }
    uint32_t foundEnemyCreature() const {
        auto enemy = _foundEnemyCreature.resolve();
        return enemy ? enemy->id() : script::kObjectInvalid;
    }
    int goodEvil() const { return _goodEvil; }

    // K2 Force forms (design note k2_merge): the active form, a spells.2da
    // row of user type 6, or 0. Saved as "CurrentForm", only while set.
    int activeForm() const { return _activeForm; }
    void setActiveForm(int form) { _activeForm = form; }
    /// True when the right-hand weapon (or a double-bladed one) is a lightsaber.
    bool wieldsLightsaber() const;
    /// The lightsaber form in effect (form and lightsaber both), or 0.
    int saberFormInEffect() const;
    FeatType getLastCombatFeat() const { return _lastCombatFeat; }
    AttackResultType getLastAttackResult() const { return _lastAttackResult; }
    int modifiedAttacks() const { return _modifiedAttacks; }
    bool hasAssuredHit() const { return _assuredHit; }
    AttackBonusBreakdown getAttackBonusBreakdown(
        const Creature *target,
        const Item *weapon,
        bool offHand) const;
    /// Attack bonus from the K2 feats the K2 systems merge adds (0 without).
    int getK2FeatAttackBonus(const Creature *target, const Item *weapon, bool offHand) const;
    int getAttackBonus(bool offHand = false) const;
    bool hasEffectImmunity(
        ImmunityType immunityType,
        const Creature *creator = nullptr) const;
    int getAbilityEffectModifier(Ability ability) const;
    int getEffectiveAbilityScore(Ability ability) const;
    int getEffectiveAbilityModifier(Ability ability) const;
    bool hasEffectiveFeat(FeatType feat) const;
    int getDefense(const Creature *attacker, int damageFlags) const;
    int getDefense() const;
    int getFortitudeSave(SavingThrowType savingThrowType = SavingThrowType::All) const;
    // savingThrow: 1 = Fortitude, 2 = Reflex, 3 = Will (nwscript SAVING_THROW_*).
    int getSavingThrow(int savingThrow, SavingThrowType savingThrowType = SavingThrowType::All) const;
    bool rollSavingThrow(int savingThrow, int difficultyClass, SavingThrowType savingThrowType = SavingThrowType::All) const;
    /// A save against a Force power (or anything) cast by `versus`, whose
    /// active form can lower it (Force Mastery).
    bool rollSavingThrowVersus(int savingThrow, int difficultyClass, SavingThrowType savingThrowType, const Creature *versus) const;
    bool rollFortitudeSave(
        int difficultyClass,
        SavingThrowType savingThrowType = SavingThrowType::All) const;
    int getPhysicalDamageBonus(const Item *weapon, bool offHand) const;
    int getMassiveCriticalDamage(const Item *weapon, bool criticalHit) const;
    int getDamageResistanceFeatBonus() const;
    void addPhysicalDamageModifiers(
        DamagePacket &damage,
        const Creature *target,
        const Item *weapon,
        bool offHand,
        int criticalMultiplier) const;
    void getMainHandDamage(int &min, int &max) const;
    void getOffhandDamage(int &min, int &max) const;

    void setAttemptedAttackTarget(uint32_t target);
    /** A cast has begun at target: GetAttemptedSpellTarget / GetSpellTarget. */
    void setAttemptedSpellTarget(const std::shared_ptr<Object> &target);
    /** A cast reached its effect point; it counts as successful until resisted. */
    void recordForcePowerUsed(SpellType spell);
    void setLastForcePowerFailed() { _lastForcePowerSuccess = false; }
    void setBlockingCreature(const std::shared_ptr<Object> &blocker) { _blockingCreature = blocker; }
    void setFoundEnemyCreature(const std::shared_ptr<Object> &enemy) { _foundEnemyCreature = enemy; }
    /** CancelCombat: end combat now and forget this creature's combat record. */
    void cancelCombat();
    void beginCombatAttack(std::shared_ptr<Object> target, FeatType feat);
    void finishCombatRound();
    void setLastAttackResult(AttackResultType result) { _lastAttackResult = result; }
    /** The weapon of the creature's latest attack (GetLastWeaponUsed); invalid when unarmed. */
    /** The combat feat mode in effect (power attack, flurry and so on), Invalid when none. */
    FeatType activeCombatFeat() const { return _combatState.combatFeat; }
    uint32_t lastWeaponUsed() const { return _lastWeaponUsed; }
    void setLastWeaponUsed(uint32_t item) { _lastWeaponUsed = item; }
    void adjustModifiedAttacks(int amount);
    bool applyAssuredHit();
    void removeAssuredHit() { _assuredHit = false; }

    // END Combat

    // Gold

    void giveGold(int amount);
    void takeGold(int amount);

    int gold() const { return _gold; }

    // END Gold

    // Scripts

    void runSpawnScript();
    void runBlockedScript(uint32_t blockingDoorId);
    void runEndRoundScript();
    void updateCombatHandover(float dt);
    void updateIdleEngage(float dt);
    void updateIdleFollow(float dt);
    bool seesLiveEnemy() const;
    void runDialogueScript(uint32_t speakerId, int32_t listenNumber);
    void runAttackedScript(uint32_t attackerId);

    /**
     * Another object changed this creature's inventory. The OnDisturbed
     * script runs on the creature's next simulation update, with the
     * disturber, the disturb type and the item as its event values.
     */
    void signalInventoryDisturbed(InventoryDisturbType type, uint32_t disturberId, uint32_t itemId);

    /**
     * A conversation ended normally. The OnEndDialogue script runs on the
     * creature's next simulation update.
     */
    void signalEndDialogue();

    /**
     * A rest completed: the creature recovers its hit points and force
     * points and its OnRested script runs.
     */
    void finishRest();
    const std::string &getOnRested() const { return _onRested; }

    /** Queued object events (disturbed, end of dialogue) not yet run. */
    size_t pendingScriptEventCount() const { return _pendingScriptEvents.size(); }

    const std::string &getOnDisturbed() const { return _onDisturbed; }
    const std::string &getOnEndDialogue() const { return _onEndDialogue; }

    bool spawnScriptFired() const { return _spawnScriptFired; }

    void setOnHeartbeat(std::string onHeartbeat) { _onHeartbeat = onHeartbeat; }
    void setOnSpawn(std::string onSpawn) { _onSpawn = onSpawn; }
    void setOnDeath(std::string onDeath) { _onDeath = onDeath; }
    void setOnNotice(std::string onNotice) { _onNotice = onNotice; }
    void setOnEndRound(std::string onEndRound) { _onEndRound = onEndRound; }
    const std::string &getOnEndRound() const { return _onEndRound; }
    void setOnSpellAt(std::string onSpellAt) { _onSpellAt = onSpellAt; }
    const std::string &getOnSpellAt() const { return _onSpellAt; }
    void setOnAttacked(std::string onAttacked) { _onAttacked = onAttacked; }
    void setOnDamaged(std::string onDamaged) { _onDamaged = onDamaged; }
    void setOnDisturbed(std::string onDisturbed) { _onDisturbed = onDisturbed; }
    void setOnEndDialogue(std::string onEndDialogue) { _onEndDialogue = onEndDialogue; }
    void setOnRested(std::string onRested) { _onRested = std::move(onRested); }
    void setOnBlocked(std::string onBlocked) { _onBlocked = onBlocked; }
    void setOnDialogue(std::string onDialogue) { _onDialogue = onDialogue; }
    const std::string &getOnDialogue() const { return _onDialogue; }

    // END Scripts

    // IAnimationEventListener

    void onEventSignalled(const std::string &name) override;

    // END IAnimationEventListener

    // Listeners

    bool isListening() { return _isListening; }
    void setIsListening(bool value) { _isListening = value; }

    // END Listeners

    /**
     * heads.2da row shown now: the DuplicateHeadAppearance override, else
     * the appearance's normalhead, else -1.
     */
    int headRow() const;

    /** DuplicateHeadAppearance: show this heads.2da row. Rebuilds the model. */
    void setHeadOverride(int headRow);

    /**
     * Co-op: the body model letter (a = 0) and texture variation a stand-in
     * shows, as the host built them; -1 for both returns to the body armour's.
     * Presentation only. Rebuilds the model.
     */
    void setReplicaBodyLook(int bodyVariation, int textureVariation);
    int replicaBodyVariation() const { return _replicaBodyVariation; }
    int replicaTextureVariation() const { return _replicaTextureVariation; }

protected:
    bool canExecuteActions() const override;

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    std::optional<int> _headOverride; /**< DuplicateHeadAppearance; runtime only */
    int _replicaBodyVariation {-1};    /**< co-op stand-in look; runtime only */
    int _replicaTextureVariation {-1}; /**< co-op stand-in look; runtime only */
    // Serializable
    RacialType _race {RacialType::Unknown};
    Subrace _subrace {Subrace::None};
    uint32_t _appearance {0};
    uint32_t _appearanceBeforeDisguise {0};
    bool _disguised {false};
    Gender _gender {Gender::Male};
    uint16_t _portraitId {0};
    bool _isPC {false};
    int32_t _assignedPuppet {-1};
    bool _puppet {false};
    bool _cameo {false};
    Faction _faction {Faction::Invalid};
    std::optional<Faction> _factionBeforeModifier;
    int _furyDamageBonus {0};
    bool _disarmable {false};
    bool _noPermDeath {false};
    bool _notReorienting {false};
    uint8_t _bodyVariation {0};
    std::optional<CreaturePresentation> _presentation;
    uint8_t _textureVar {0};
    bool _partyInteract {false};
    int32_t _walkRate {0};
    uint8_t _naturalAC {0};
    int16_t _forcePoints {0};
    int16_t _currentForce {0};
    int _temporaryForce {0};
    int _temporaryHitPoints {0};
    int16_t _maxForcePoints {0};
    // The last cast's spell and the force point cost recorded when it began
    // (not saved: after a load the cost is worked out again).
    std::optional<std::pair<SpellType, int>> _recordedSpellCost;
    // GetDamageDealtByType: amounts per DAMAGE_TYPE_* bit of the last damage
    // event (one damager, one tick).
    std::array<int, 15> _lastDamageByType {};
    uint64_t _lastDamageEventTick {0};
    uint32_t _lastDamageEventDamager {0};
    bool _lastDamageEventOpen {false};
    RuntimeObjectRef<Object> _lastKiller;
    int16_t _refBonus {0};
    int16_t _willBonus {0};
    int16_t _fortBonus {0};
    uint8_t _goodEvil {0};
    int32_t _activeForm {0};
    float _challengeRating {0};
    uint32_t _xp {0};

    std::string _onNotice;
    std::string _onSpellAt;
    std::string _onAttacked;
    std::string _onDamaged;
    std::string _onDisturbed;
    std::string _onEndRound;
    std::string _onEndDialogue;
    std::string _onRested;
    std::string _onDialogue;
    std::string _onSpawn;
    std::string _onDeath;
    std::string _onBlocked;

    struct PendingScriptEvent {
        enum class Kind {
            Disturbed,
            EndDialogue
        };
        Kind kind {Kind::Disturbed};
        InventoryDisturbType disturbType {InventoryDisturbType::Added};
        uint32_t disturberId {0};
        uint32_t itemId {0};
    };

    // Object events raised outside the creature's own update (inventory
    // changes, conversation ends) wait here so their scripts run in the
    // simulation tick, in the order they were raised.
    std::deque<PendingScriptEvent> _pendingScriptEvents;

    void runPendingScriptEvents();
    void runDisturbedScript(InventoryDisturbType type, uint32_t disturberId, uint32_t itemId);
    void runEndDialogueScript();
    void runRestedScript();

    // Retail CreatnScrptFird. The creation script belongs to the creature, not
    // to any one area attachment: it fires at most once per creature and the
    // flag travels with the creature through saves.
    bool _spawnScriptFired {false};

    // Door currently obstructing this creature, and the door the blocked event
    // was last reported for. Object ids rather than pointers, so a door that is
    // destroyed while remembered simply resolves to no object.
    uint32_t _blockingDoorId {script::kObjectInvalid};
    uint32_t _blockedEventDoorId {script::kObjectInvalid};

    resource::LocString _firstName;
    resource::LocString _lastName;

    uint16_t _soundSetId {0xFFFF};
    uint8_t _bodyBagId {0xFF};
    uint8_t _perceptionId {0xFF};

    CreatureAttributes _attributes;
    std::map<int, std::shared_ptr<Item>> _equipment;
    // END Serializable

    ModelType _modelType {ModelType::Creature};
    std::shared_ptr<graphics::Texture> _portrait;

    // Current path that the creature is following, its velocity and position at
    // the previous frame.
    std::optional<Path> _path;
    // Destination the current path was planned for. A path planned for an
    // earlier destination is stale and must be replanned.
    glm::vec3 _pathDestination {0.0f};
    // Door-barrier revision the path was planned against.
    uint32_t _pathBarrierRevision {0};
    // Consecutive failed path requests toward the same destination.
    static constexpr int kMaxPathRequestFailures = 30;
    int _pathRequestFailures {0};
    glm::vec3 _pathRequestDestination {0.0f};
    glm::vec3 _pathVelocity {0.0f};
    glm::vec3 _previousPosition {0.0f};
    // When steering makes no progress, the creature side-steps along the
    // walkmesh (_stuckForce) for a moment and then replans. The side is
    // chosen from the walkmesh and nearby creatures, never at random.
    glm::vec3 _stuckForce {0.0f};
    float _sideStepTime {0.0f};
    int _sideStepCount {0};
    bool _replanPending {false};
    bool _steeringStarted {false};
    // K2 give-up rule. A bump is a steering tick that made no progress
    // because something blocked the step. The walk gives up on the sixth
    // bump, or on bumping the same creature again when it has not moved.
    // The count restarts when the destination moves 0.1 m or more.
    static constexpr int kMaxBumps = 5;
    int _bumpCount {0};
    uint32_t _lastBumpCreature {script::kObjectInvalid};
    glm::vec3 _lastBumpCreaturePosition {0.0f};
    glm::vec3 _bumpDestination {0.0f};
    bool _bumpGiveUp {false};
    uint32_t _stepBlockCreature {script::kObjectInvalid};
    bool _stepBlockStatic {false};
    bool _passLeft {false};
    // Give-up rule: the shortest remaining route this navigation has had,
    // and how long it has gone without shortening it.
    float _navBestDistance {FLT_MAX};
    float _navNoProgressTime {0.0f};

    // Ground one loop of the walk/run clip covers at normal playback
    // (appearance.2da walkdist/rundist); playback is scaled to the actual speed.
    float _walkSpeed {0.0f};
    float _runSpeed {0.0f};
    std::string _appearanceMoveRate;
    mutable float _resolvedWalkSpeed {0.0f};
    mutable float _resolvedRunSpeed {0.0f};
    mutable int _resolvedForRate {-1};
    mutable int _resolvedForAppearance {-1};
    void resolveMovementSpeeds() const;
    float _creaturePersonalSpace {0.6f};
    CreatureSize _size {CreatureSize::Invalid};
    MovementType _movementType {MovementType::None};
    bool _talking {false};

    ItemAttributes _itemAttributes;

    bool _movementRestricted {false};
    CombatState _combatState;
    RuntimeObjectRef<Object> _lastHostileTarget;
    ActionType _lastAttackAction {ActionType::QueueEmpty};
    FeatType _lastCombatFeat {FeatType::Invalid};
    int _lastForcePower {-1};
    bool _lastForcePowerSuccess {false};
    int _lastDamageTotal {0};
    RuntimeObjectRef<Object> _blockingCreature;
    RuntimeObjectRef<Object> _foundEnemyCreature;
    uint32_t _lastWeaponUsed {script::kObjectInvalid};
    AttackResultType _lastAttackResult {AttackResultType::Invalid};
    int _modifiedAttacks {0};
    bool _assuredHit {false};
    bool _immortal {false};
    bool _coopPartyCharacter {false};
    std::shared_ptr<resource::SoundSet> _soundSet;
    BodyBag _bodyBag;
    // Body decay (design note p1_game "Body decay and body bags").
    std::optional<float> _decayTimer;
    bool _decayed {false};
    Perception _perception;
    uint8_t _visibilityCounterBits {0};
    bool _trueSeeingUltravisionQuirk {false};
    NPCAIStyle _aiStyle {NPCAIStyle::DefaultAttack};

    uint32_t _footstepType {0};
    int _walkmeshMaterial {-1};
    int _gold {0}; /**< aka credits */
    std::string _envmap;
    bool _isListening {false};

    std::shared_ptr<audio::AudioSource> _audioSourceVoice;
    std::shared_ptr<audio::AudioSource> _audioSourceFootstep;
    bool _lightsaberIdlePowerDownPending {false};
    Timer _lightsaberIdlePowerDownTimer;
    std::optional<bool> _lightsaberPowerOverride;

    uint32_t _encounterId {script::kObjectInvalid};
    bool _encounterNotified {false};
    bool _combatHandoverPending {false};
    bool _cancelCombatPending {false};
    std::vector<std::weak_ptr<Action>> _cancelledActions;
    bool _autoCombat {false};
    // Smarter companions: the brain moved this creature last round (C13).
    bool _brainRepositioned {false};
    float _autoCombatIdle {0.0f};
    // Cancel Combat's hold, see isCombatHeld.
    bool _combatHeld {false};
    // Co-op: how long a client's character has stood with nothing queued.
    float _engageIdle {0.0f};
    bool _followingLeader {false}; /**< arrived on a FollowLeader and nothing else ordered since */
    float _followCheck {0.0f};
    // Stealth Mode and the stealth field visual it wears while on.
    bool _stealthMode {false};
    std::optional<bool> _replicaLevelUp; // co-op client: the host's level-up state, when it sent one
    std::shared_ptr<VisualEffectPresenter::Request> _stealthVisual;
    uint32_t _stealthVisualGeneration {0};
    void updateStealth();

    bool _destroyable {true};
    bool _raiseable {true};
    bool _selectableWhenDead {false};

    // Animation

    bool _animDirty {true};
    bool _animFireForget {false};
    bool _talkLayerOn {false};
    std::shared_ptr<graphics::LipAnimation> _talkLayerLip;
    std::shared_ptr<graphics::LipAnimation> _lipAnimation;

    struct KnockdownState {
        KnockdownPhase phase {KnockdownPhase::None};
        float phaseTime {0.0f};
        std::string fallAnim;
        std::string downAnim; /**< empty: hold the fall's last frame */
        std::string getUpAnim;
        float fallLength {0.0f};
        float getUpLength {0.0f};
        bool clipStarted {false}; /**< this phase's clip is on the model */
    } _knockdown;

    struct KnockbackState {
        glm::vec2 direction {0.0f};
        float remaining {0.0f};
        bool ignoreObstacles {false};
    } _knockback;

    bool isKnockdownHeld() const;
    void updateKnockdown(float dt);
    void setKnockdownPhase(KnockdownPhase phase);

    // END Animation

    // Scripts

    // END Scripts

    void loadTransformFromGIT(const resource::generated::GIT_Creature_List &git);

    void onEffectsCleared() override;
    void updateModel();

    // Refresh appearance-derived state (model type, size, speeds, footstep, envmap,
    // portrait) for the current _appearance, without building a scene node.
    void loadAppearanceProperties();

    // Recompute the disguise appearance override from equipped items: switch to a
    // disguise item's appearance when one is equipped, and restore the original
    // appearance when none remains. Updates _appearance only; callers rebuild the model.
    void updateDisguise();
    void updateEquipmentPresentation();
    void updateCombat(float dt);
    void setLightsabersPowered(bool powered, bool animate);
    void updateLightsaberSoundPositions();

    void runDeathScript();
    void awardKillExperience();
    void runDamagedScript();

    ModelType parseModelType(const std::string &s) const;

    // Appearance

    std::shared_ptr<scene::ModelSceneNode> buildModel();
    void finalizeModel(scene::ModelSceneNode &body);

    std::string getBodyModelName() const;
    std::string getBodyTextureName() const;
    std::string getHeadModelName() const;

    bool _sceneHideWeapons {false};
    bool _sceneHideHeadItem {false};
    std::string getMaskModelName() const;
    std::string getWeaponModelName(int slot) const;

    // END Appearance

    // Animation

    bool doPlayAnimation(bool fireForget, const std::function<void()> &callback);

    std::string getAnimationName(AnimationType anim) const override;
    std::string getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const;
    std::string getActiveAnimationName() const override;

    std::string getDeadAnimation() const;
    std::string getDieAnimation() const;
    std::string getHeadTalkAnimation() const;
    std::string getPauseAnimation() const;
    std::string getRunAnimation() const;
    std::string getTalkNormalAnimation() const;
    std::string getWalkAnimation() const;

    /**
     * @return creatureAnim if model type is creature, elseAnim otherwise
     */
    inline std::string getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const;

    bool getWeaponInfo(WeaponType &type, WeaponWield &wield) const;
    int getWeaponWieldNumber(WeaponWield wield) const;
    int getRelativeWeaponSize(const Item &weapon) const;
    int getTwoWeaponAttackPenalty(
        const Item *weapon,
        bool offHand,
        int *smallOffhandBonus = nullptr) const;
    int getDuelingBonus() const;
    void getWeaponDamage(const Item *weapon, int &min, int &max) const;

    // END Animation

    // Blueprint
    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void deserializeName(const resource::Gff &gff);
    void deserializeSoundSet(const resource::Gff &gff);
    void deserializeBodyBag(const resource::Gff &gff);
    void deserializeAttributes(const resource::Gff &gff);
    void deserializeClass(const resource::Gff &gff);
    void deserializePerception(const resource::Gff &gff);
    void deserializeOwnedItemsAndEquipment(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void appendEquippedItemEffects(
        std::deque<EffectInstance> &effects,
        int slot,
        const std::shared_ptr<Item> &item) const;
    std::deque<EffectInstance> effectsWithoutEquippedSource(
        const Item *source) const;
    std::deque<EffectInstance> rebuildEquippedItemEffects(
        const std::map<int, std::shared_ptr<Item>> &equipment) const;
    int derivePermanentMaxHitPoints() const;
    int deriveMaxForcePoints() const;
    void restoreSerializedVitality();
    void updateDeathFromCurrentHitPoints();
    // END Blueprint
};

/** Whether the object is an ambient cameo creature (see Creature::isCameo). */
inline bool isCameoObject(const Object &object) {
    return object.type() == ObjectType::Creature && static_cast<const Creature &>(object).isCameo();
}

} // namespace game

} // namespace reone
