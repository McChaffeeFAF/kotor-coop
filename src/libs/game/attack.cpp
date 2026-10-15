/*
 * Copyright (c) 2025 The reone project contributors
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

#include "reone/game/coopsession.h"
#include "reone/game/onhit.h"
#include "reone/game/attack.h"

#include "reone/game/d20/forms.h"
#include "reone/game/d20/k2feats.h"

#include "action/commonactions.h"
#include "reone/game/hitvisuals.h"
#include "reone/resource/provider/2das.h"

#include "reone/game/animations.h"
#include "reone/game/combatsounds.h"
#include "reone/game/d20/feats.h"
#include "reone/game/di/services.h"
#include "reone/game/difficultyoptions.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/projectiles.h"
#include "reone/graphics/animation.h"
#include "reone/scene/collision.h"
#include "reone/scene/graph.h"
#include "reone/system/arrayref.h"
#include "reone/system/randomutil.h"

#include <algorithm>
#include <initializer_list>

namespace reone {

namespace game {

static constexpr char kModelEventDetonate[] = "detonate";
static constexpr float kProjectileSpeed = 16.0f;
static constexpr int kUnarmedCriticalThreat = 1;
static constexpr float kSpecialAttackDefensePenaltyDuration = 3.0f;
static constexpr float kCriticalStrikeStunDuration = 6.0f;

bool isMeleeWieldType(CreatureWieldType type) {
    switch (type) {
    case CreatureWieldType::SingleSword:
    case CreatureWieldType::DoubleBladedSword:
    case CreatureWieldType::DualSwords:
        return true;
    default:
        return false;
    }
}

bool isRangedWieldType(CreatureWieldType type) {
    switch (type) {
    case CreatureWieldType::BlasterPistol:
    case CreatureWieldType::DualPistols:
    case CreatureWieldType::BlasterRifle:
    case CreatureWieldType::HeavyWeapon:
        return true;
    default:
        return false;
    }
}

bool isAttackSuccessful(AttackResultType result) {
    switch (result) {
    case AttackResultType::HitSuccessful:
    case AttackResultType::CriticalHit:
    case AttackResultType::AutomaticHit:
        return true;
    default:
        return false;
    }
}

bool isPhysicalAttackFeat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
    case FeatType::Flurry:
    case FeatType::ImprovedFlurry:
    case FeatType::WhirlwindAttack:
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
    case FeatType::SniperShot:
    case FeatType::ImprovedSniperShot:
    case FeatType::MasterSniperShot:
    case FeatType::PowerBlast:
    case FeatType::ImprovedPowerBlast:
    case FeatType::MasterPowerBlast:
        return true;
    default:
        return false;
    }
}

static bool hasActiveItemProperty(
    const Item &item,
    ItemProperty type,
    int subtype = -1) {

    for (const Item::PropertyEntry &property : item.properties()) {
        if (!item.isPropertyActive(property) ||
            property.propertyName != static_cast<uint16_t>(type) ||
            (subtype >= 0 && property.subtype != subtype)) {
            continue;
        }
        return true;
    }
    return false;
}

static bool hasEffectImmunity(
    const Creature &target,
    ImmunityType immunityType) {

    for (const EffectInstance *appliedPtr : target.flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (!applied.effect) {
            continue;
        }
        if (applied.effect->type() != EffectType::Immunity) {
            continue;
        }

        const auto &effect = static_cast<const ImmunityEffect &>(*applied.effect);
        if (effect.immunityType() == immunityType) {
            return true;
        }
    }
    return false;
}

static bool hasStunImmunity(const Creature &target) {
    return hasEffectImmunity(target, ImmunityType::Stun) ||
           target.hasEffectiveFeat(FeatType::ForceImmunityStun) ||
           target.hasEffectiveFeat(FeatType::ForceImmunityParalysis);
}

static bool hasCriticalHitImmunity(const Creature &target) {
    return hasEffectImmunity(target, ImmunityType::CriticalHit);
}

bool canAttemptBlasterDeflection(const Creature &target) {
    if (!target.hasEffectiveFeat(FeatType::WeaponProficiencyLightsaber)) {
        return false;
    }
    for (int slot : {InventorySlots::rightWeapon, InventorySlots::leftWeapon}) {
        auto weapon = target.getEquippedItem(slot);
        if (weapon && weapon->isLightsaber()) {
            return true;
        }
    }
    return false;
}

int blasterDeflectionFeatBonus(const Creature &target) {
    if (!target.hasEffectiveFeat(FeatType::Deflect)) {
        return 0;
    }
    int level = target.attributes().getAggregateLevel();
    return 1 + std::max(0, (level - 1) / 2);
}

bool resolveBlasterDeflection(int deflectRoll, int deflectBonus, int attackTotal) {
    return (deflectRoll + deflectBonus) >= attackTotal;
}

static int getBaseCriticalThreat(const Item *weapon) {
    return weapon ? weapon->criticalThreat() : kUnarmedCriticalThreat;
}

static int getCriticalThreat(const Item *weapon, int threatBonus) {
    int threat = getBaseCriticalThreat(weapon);
    if (weapon && hasActiveItemProperty(*weapon, ItemProperty::Keen)) {
        threat *= 2;
    }
    return threat + threatBonus;
}

struct AttackResolution {
    AttackResultType result {AttackResultType::Invalid};
    int roll {0};
    int defense {0};
    bool assuredHit {false};
};

/**
 * A ranged hit against a lightsaber wielder gets an independent deflect
 * roll; on success the shot misses instead (attack.cpp's blaster_deflect,
 * design note blaster_deflect).
 */
static void applyBlasterDeflection(
    AttackResolution &resolution,
    bool ranged,
    const Creature *targetCreature,
    int attackBonus) {

    if (!ranged || !targetCreature || !isAttackSuccessful(resolution.result)) {
        return;
    }
    if (!canAttemptBlasterDeflection(*targetCreature)) {
        return;
    }
    int deflectRoll = randomInt(1, 20);
    int deflectBonus = blasterDeflectionFeatBonus(*targetCreature);
    int attackTotal = resolution.roll + attackBonus;
    if (resolveBlasterDeflection(deflectRoll, deflectBonus, attackTotal)) {
        resolution.result = AttackResultType::Deflected;
        debug(str(boost::format("computeAttack: deflected: roll(%d), bonus(%d), attackTotal(%d)") %
                  deflectRoll % deflectBonus % attackTotal),
              LogChannel::Combat);
    }
}

static AttackResolution computeAttack(
    const Creature &attacker,
    const Object &target,
    int attackBonus,
    int criticalThreat,
    int damageFlags,
    bool ranged) {

    AttackResolution resolution;

    // Determine defense of a target
    const auto *targetCreature = dyn_cast<Creature>(&target);
    resolution.defense = targetCreature
                             ? targetCreature->getDefense(&attacker, damageFlags)
                             : 0;

    // Attack roll
    resolution.roll = randomInt(1, 20);

    if (attacker.hasAssuredHit()) {
        resolution.result = AttackResultType::HitSuccessful;
        resolution.assuredHit = true;
        debug(str(boost::format("computeAttack: assured hit: roll(%d)") % resolution.roll),
              LogChannel::Combat);
        applyBlasterDeflection(resolution, ranged, targetCreature, attackBonus);
        return resolution;
    }

    if (resolution.roll == 1) {
        resolution.result = AttackResultType::Miss;
        debug(str(boost::format("computeAttack: miss: roll(1)")), LogChannel::Combat);
        return resolution;
    }

    if (resolution.roll != 20 &&
        (resolution.roll + attackBonus) < resolution.defense) {
        resolution.result = AttackResultType::Miss;
        debug(str(boost::format("computeAttack: miss: roll(%d), bonus(%d), defense(%d)") %
                  resolution.roll % attackBonus % resolution.defense),
              LogChannel::Combat);
        return resolution;
    }

    // Critical threat
    if (resolution.roll >= (21 - criticalThreat)) {
        // Critical confirmation
        int confirmationRoll = randomInt(1, 20);
        // Juyo (a K2 lightsaber form): +4 to confirm.
        int confirmationBonus = forms::criticalConfirmationBonus(attacker.saberFormInEffect());
        if ((confirmationRoll + attackBonus + confirmationBonus) >= resolution.defense) {
            bool criticalHitImmune =
                targetCreature && hasCriticalHitImmunity(*targetCreature);
            if (!criticalHitImmune) {
                resolution.result = AttackResultType::CriticalHit;
                debug(str(boost::format("computeAttack: critical hit: roll(%d), confirmation(%d),"
                                        " bonus(%d), defense(%d), critical threat(%d)") %
                          resolution.roll % confirmationRoll % attackBonus %
                          resolution.defense % criticalThreat),
                      LogChannel::Combat);
                applyBlasterDeflection(resolution, ranged, targetCreature, attackBonus);
                return resolution;
            }
        }
    }

    resolution.result = AttackResultType::HitSuccessful;
    debug(str(boost::format("computeAttack: hit: roll(%d), bonus(%d), defense(%d),"
                            " critical threat(%d)") %
              resolution.roll % attackBonus % resolution.defense % criticalThreat),
          LogChannel::Combat);

    applyBlasterDeflection(resolution, ranged, targetCreature, attackBonus);
    return resolution;
}

int weaponBaseDamage(int amount, bool noDamage) {
    // A weapon with the no-damage property rolls no dice and has no one-point
    // floor: it hurts only through its bonus-damage properties (the Endar
    // Spire's end_onedam rifles deal exactly their +1).
    return noDamage ? std::max(amount, 0) : std::max(amount, 1);
}

static int rollDamageDice(int numDice, int die) {
    int result = 0;
    for (int i = 0; i < numDice; ++i) {
        result += randomInt(1, die);
    }
    return result;
}

/**
 * One roll of damage dice with the diffsettings damage-percent rule applied
 * (a no-op at the game's fixed "normal" column). "PC" is a party member.
 */
static int rollDamageDiceForDifficulty(const Creature &attacker, const Object &target, int numDice, int die) {
    int roll = rollDamageDice(numDice, die);
    Game &game = attacker.game();
    return adjustDamageRollForDifficulty(
        game.services().game.difficultyOptions, roll, numDice * die,
        game.party().isMember(attacker), game.party().isMember(target),
        [](int n) { return n > 0 ? randomInt(0, n - 1) : 0; });
}

static void computeWeaponDamage(
    const Creature &attacker, const Object &target, const Item &weapon,
    AttackBuffer::Source source, AttackResultType result,
    int damageBonus, DamagePacket &damage) {

    int multiplier = result == AttackResultType::CriticalHit
                         ? weapon.criticalHitMultiplier()
                         : 1;
    if (result == AttackResultType::CriticalHit && weapon.isLightsaber()) {
        // Shien (a K2 lightsaber form): +1 critical multiplier.
        multiplier += forms::criticalMultiplierModifier(attacker.saberFormInEffect());
    }
    bool offHand = source == AttackBuffer::Source::Offhand;

    int amount = multiplier * (damageBonus + attacker.getPhysicalDamageBonus(&weapon, offHand));
    if (!hasActiveItemProperty(weapon, ItemProperty::NoDamage)) {
        for (int multiple = 0; multiple < multiplier; ++multiple) {
            amount += rollDamageDiceForDifficulty(attacker, target, weapon.numDice(), weapon.dieToRoll());
        }
    }

    amount += attacker.getMassiveCriticalDamage(
        &weapon, result == AttackResultType::CriticalHit);

    DamageType type = getPrimaryDamageType(weapon.damageFlags());
    damage.add(weaponBaseDamage(amount, hasActiveItemProperty(weapon, ItemProperty::NoDamage)), type);
    damage.setDamageFlags(weapon.damageFlags());
    attacker.addPhysicalDamageModifiers(
        damage,
        dyn_cast<Creature>(&target),
        &weapon,
        offHand,
        multiplier);

    debug(str(boost::format("computeWeaponDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % damage.total()),
          LogChannel::Combat);
}

static int getUnarmedDamageDie(const Creature &attacker) {
    return attacker.size() <= CreatureSize::Small ? 2 : 1;
}

static void computeUnarmedDamage(
    const Creature &attacker, const Object &target,
    AttackResultType result, int damageBonus, DamagePacket &damage) {

    int multiplier = (result == AttackResultType::CriticalHit) ? 2 : 1;

    int amount = multiplier * (damageBonus + attacker.getPhysicalDamageBonus(nullptr, false));
    // Unarmed Specialist (a K2 feat): the highest tier N adds Nd4.
    int specialistDice = k2feats::unarmedSpecialistDice([&attacker](FeatType feat) { return attacker.hasEffectiveFeat(feat); });
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        amount += rollDamageDiceForDifficulty(attacker, target, 1, getUnarmedDamageDie(attacker));
        if (specialistDice > 0) {
            amount += rollDamageDiceForDifficulty(attacker, target, specialistDice, 4);
        }
    }

    amount += attacker.getMassiveCriticalDamage(
        nullptr, result == AttackResultType::CriticalHit);

    damage.add(std::max(amount, 1), DamageType::Bludgeoning);
    damage.setDamageFlags(static_cast<int>(DamageType::Bludgeoning));
    attacker.addPhysicalDamageModifiers(
        damage,
        dyn_cast<Creature>(&target),
        nullptr,
        false,
        multiplier);

    debug(str(boost::format("computeUnarmedDamage: %s -> %s (%d)") % attacker.tag() % target.tag() % damage.total()),
          LogChannel::Combat);
}

static bool grantsExtraMainHandAttack(FeatType feat) {
    switch (feat) {
    case FeatType::Flurry:
    case FeatType::ImprovedFlurry:
    case FeatType::WhirlwindAttack:
    case FeatType::RapidShot:
    case FeatType::ImprovedRapidShot:
    case FeatType::MultiShot:
        return true;
    default:
        return false;
    }
}

static int getSpecialAttackRollBonus(FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
    case FeatType::ImprovedPowerAttack:
    case FeatType::MasterPowerAttack:
        return -3;
    case FeatType::Flurry:
        return -4;
    case FeatType::ImprovedFlurry:
        return -2;
    case FeatType::WhirlwindAttack:
        return -1;
    default:
        return 0;
    }
}

static int getSpecialAttackDamageBonus(FeatType feat) {
    switch (feat) {
    case FeatType::PowerAttack:
        return 5;
    case FeatType::ImprovedPowerAttack:
        return 8;
    case FeatType::MasterPowerAttack:
        return 10;
    default:
        return 0;
    }
}

static int getSpecialAttackThreatBonus(
    FeatType feat,
    const Item *weapon) {

    int multiplier = 0;
    switch (feat) {
    case FeatType::CriticalStrike:
        multiplier = 1;
        break;
    case FeatType::ImprovedCriticalStrike:
        multiplier = 2;
        break;
    case FeatType::MasterCriticalStrike:
        multiplier = 3;
        break;
    default:
        break;
    }
    return multiplier * getBaseCriticalThreat(weapon);
}

static int getMeleeSpecialAttackDefensePenalty(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return 5;
    case FeatType::Flurry:
        return 4;
    case FeatType::ImprovedFlurry:
        return 2;
    case FeatType::WhirlwindAttack:
        return 1;
    default:
        return 0;
    }
}

static bool isCriticalStrikeFeat(FeatType feat) {
    switch (feat) {
    case FeatType::CriticalStrike:
    case FeatType::ImprovedCriticalStrike:
    case FeatType::MasterCriticalStrike:
        return true;
    default:
        return false;
    }
}

void AttackBuffer::addPhysicalAttacks(const Creature &attacker, const Object &target,
                                      FeatType feat) {
    _feat = feat;

    int mainHandAttacks = 1 + attacker.modifiedAttacks();
    if (grantsExtraMainHandAttack(feat)) {
        ++mainHandAttacks;
    }
    // Juyo (a K2 lightsaber form): one more attack per round.
    mainHandAttacks += forms::extraAttacks(attacker.saberFormInEffect());

    int attackRollBonus = getSpecialAttackRollBonus(feat);
    int damageBonus = getSpecialAttackDamageBonus(feat);

    auto main = attacker.getEquippedItem(InventorySlots::rightWeapon);
    if (!main) {
        auto gloves = attacker.getEquippedItem(InventorySlots::hands);
        int attackThreatBonus = getSpecialAttackThreatBonus(feat, gloves.get());
        for (int i = 0; i < mainHandAttacks; ++i) {
            addPhysicalAttack(
                attacker,
                target,
                nullptr,
                Source::Main,
                attackRollBonus,
                attackThreatBonus,
                damageBonus);
        }
        return;
    }

    int mainThreatBonus = getSpecialAttackThreatBonus(feat, main.get());
    for (int i = 0; i < mainHandAttacks; ++i) {
        addPhysicalAttack(
            attacker,
            target,
            main.get(),
            Source::Main,
            attackRollBonus,
            mainThreatBonus,
            damageBonus);
    }

    auto offhand = attacker.getOffhandAttackWeapon();
    if (offhand) {
        int offhandThreatBonus = getSpecialAttackThreatBonus(feat, offhand.get());
        addPhysicalAttack(
            attacker,
            target,
            offhand.get(),
            Source::Offhand,
            attackRollBonus,
            offhandThreatBonus,
            damageBonus);
    }
}

void AttackBuffer::resolveMeleeSpecialAttack(
    FeatType feat,
    Creature &attacker,
    Object &target,
    Game &game) {

    if (_attacks.empty() || _attacks.front().ranged) {
        return;
    }

    int defensePenalty = getMeleeSpecialAttackDefensePenalty(feat);
    if (defensePenalty != 0) {
        auto effect = game.newEffect<ACDecreaseEffect>(
            defensePenalty,
            ACBonus::Dodge,
            kAllDamageTypeFlags);
        attacker.applyEffect(
            std::move(effect),
            DurationType::Temporary,
            kSpecialAttackDefensePenaltyDuration);
    }

    if (!isCriticalStrikeFeat(feat) ||
        !isAttackSuccessful(_attacks.front().result)) {
        return;
    }

    auto *targetCreature = dyn_cast<Creature>(&target);
    if (!targetCreature || hasStunImmunity(*targetCreature)) {
        return;
    }

    int difficultyClass =
        attacker.attributes().getAggregateLevel() +
        attacker.getEffectiveAbilityModifier(Ability::Strength);
    if (!targetCreature->rollFortitudeSave(difficultyClass)) {
        _attacks.front().stunTarget = true;
    }
}

void AttackBuffer::addPhysicalAttack(
    const Creature &attacker,
    const Object &target,
    const Item *weapon,
    Source source,
    int attackRollBonus,
    int attackThreatBonus,
    int damageBonus) {

    bool offHand = source == Source::Offhand;
    const auto *targetCreature = dyn_cast<Creature>(&target);
    AttackBonusBreakdown attackBonusBreakdown = attacker.getAttackBonusBreakdown(
        targetCreature,
        weapon,
        offHand);
    attackBonusBreakdown.featBonus = attackRollBonus;
    attackRollBonus = attackBonusBreakdown.total();

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : attacker.getEquippedItem(InventorySlots::hands);
    const Item *criticalWeapon = weapon ? weapon : handItem.get();
    if (weapon && weapon->isLightsaber()) {
        // Soresu / Ataru (K2 lightsaber forms): threat range -1 / +1.
        attackThreatBonus += forms::threatRangeModifier(attacker.saberFormInEffect());
    }
    int criticalThreat = getCriticalThreat(criticalWeapon, attackThreatBonus);
    int damageFlags = weapon
                          ? weapon->damageFlags()
                          : static_cast<int>(DamageType::Bludgeoning);

    AttackResolution resolution = computeAttack(
        attacker,
        target,
        attackRollBonus,
        criticalThreat,
        damageFlags,
        weapon && weapon->isRanged());

    debug(str(boost::format("attackroll: %s -> %s weapon(%s) roll(%d) bab(%d) str(%d) dex(%d) "
                            "effect(%d) feat(%d) total(%d) defense(%d) result(%d)") %
              attacker.tag() % target.tag() % (weapon ? weapon->blueprintResRef() : std::string("unarmed")) %
              resolution.roll % attackBonusBreakdown.baseAttackBonus %
              attackBonusBreakdown.strengthModifier % attackBonusBreakdown.dexterityModifier %
              attackBonusBreakdown.effectBonus % attackBonusBreakdown.featBonus %
              attackRollBonus % resolution.defense % static_cast<int>(resolution.result)),
          LogChannel::Combat);

    _attacks.emplace_back(
        source,
        weapon && weapon->isRanged(),
        resolution.result,
        resolution.roll,
        std::move(attackBonusBreakdown),
        resolution.defense,
        resolution.assuredHit);

    if (!isAttackSuccessful(resolution.result)) {
        return;
    }

    if (weapon) {
        computeWeaponDamage(
            attacker,
            target,
            *weapon,
            source,
            resolution.result,
            damageBonus,
            _attacks.back().damage);
    } else {
        computeUnarmedDamage(
            attacker,
            target,
            resolution.result,
            damageBonus,
            _attacks.back().damage);
    }
}

void AttackBuffer::resolveDamage(Object &target) {
    for (Attack &attack : _attacks) {
        if (attack.damage.empty()) {
            continue;
        }

        attack.damage.resolve(target);
    }
}

void AttackBuffer::resolve(Creature &attacker, Object &target) {
    if (_attacks.empty()) {
        throw std::logic_error("Physical attack buffer is empty");
    }

    resolveDamage(target);
    attacker.setLastAttackResult(_attacks.back().result);
    auto weapon = attacker.getEquippedItem(
        _attacks.back().source == Source::Offhand ? InventorySlots::leftWeapon : InventorySlots::rightWeapon);
    attacker.setLastWeaponUsed(weapon ? weapon->id() : script::kObjectInvalid);

    if (auto *targetCreature = dyn_cast<Creature>(&target)) {
        targetCreature->runAttackedScript(attacker.id());
    } else if (auto *targetPlaceable = dyn_cast<Placeable>(&target)) {
        targetPlaceable->runMeleeAttackedScript(attacker.id());
    } else if (auto *targetDoor = dyn_cast<Door>(&target)) {
        targetDoor->runMeleeAttackedScript(attacker.id());
    }
}

void AttackBuffer::applyEffects(
    Attack &attack,
    Creature &attacker,
    Object &target,
    Game &game) {

    // Failed ranged attacks do not produce floating miss text.
    if (!attack.ranged && !isAttackSuccessful(attack.result)) {
        game.floatingText().addMiss(attacker, target);
    }
    if (!attack.damage.empty()) {
        auto effect = game.newEffect<DamageEffect>(
            std::move(attack.damage));
        auto liveAttacker = game.getObjectById(attacker.id());
        if (liveAttacker.get() == &attacker) {
            effect->setSaveFacingCreator(liveAttacker);
        }
        target.applyEffect(std::move(effect), DurationType::Instant);
    }
    if (attack.stunTarget) {
        auto effect = game.newEffect<StunnedEffect>();
        target.applyEffect(
            std::move(effect),
            DurationType::Temporary,
            kCriticalStrikeStunDuration);
    }
}

void AttackBuffer::signalAttack(
    Attack &attack,
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    addCombatFeedback(game, services, attacker, target, attack);
    playAttackSounds(attack, game, services, attacker, target);
    hitvisuals::presentAttack(game, services, target, attack.ranged,
                              isAttackSuccessful(attack.result),
                              attack.result == AttackResultType::Deflected,
                              attack.damage.damageFlags());
    applyEffects(attack, attacker, target, game);
    // Weapon on-hit properties (design note c11_data, "Item properties").
    auto *targetCreature = dyn_cast<Creature>(&target);
    if (isAttackSuccessful(attack.result) && targetCreature && !targetCreature->isDead()) {
        auto weapon = attacker.getEquippedItem(
            attack.source == Source::Offhand ? InventorySlots::leftWeapon : InventorySlots::rightWeapon);
        if (weapon) {
            applyOnHitProperties(game, services, attacker, *weapon, *targetCreature);
        }
    }
}

void AttackBuffer::playAttackSounds(
    const Attack &attack,
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {
    // Presentation (K2 behaviour): a melee hit plays the weapon's sound for
    // the target's material, a parried or deflected blow the parry sound.
    // The attacker grunts (leader always, others 20%) and calls a critical.
    if (attack.result == AttackResultType::CriticalHit) {
        attacker.bark(resource::SoundSetEntry::CriticalHit, 100);
    } else {
        attacker.bark(static_cast<resource::SoundSetEntry>(
                          static_cast<int>(resource::SoundSetEntry::AttackGrunt1) + cosmeticRandomInt(0, 2)),
                      20);
    }
    if (attack.ranged) {
        return;
    }
    auto &twoDas = services.resource.twoDas;
    std::string family;
    if (isAttackSuccessful(attack.result)) {
        family = combatsounds::targetMaterial(twoDas, target);
    } else if (attack.result == AttackResultType::Parried || attack.result == AttackResultType::Deflected) {
        family = "parry";
    } else {
        return;
    }
    int row = combatsounds::weaponSoundRow(twoDas, attacker);
    std::string sound(combatsounds::weaponSound(twoDas, row, family, cosmeticRandomInt(0, 1)));
    game.playSoundCue("hit", sound, target.position() + glm::vec3(0.0f, 0.0f, 1.0f));
}

void AttackBuffer::signal(
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    for (Attack &attack : _attacks) {
        signalAttack(attack, game, services, attacker, target);
    }
}

void AttackBuffer::prepareMeleeSequence(
    const IAnimations &animations,
    const std::vector<std::string> &attackAnimations) {

    if (_attacks.empty()) {
        throw std::logic_error("Physical attack buffer is empty");
    }
    if (std::any_of(
            _attacks.begin(),
            _attacks.end(),
            [](const Attack &attack) { return attack.ranged; })) {
        throw std::logic_error("Ranged attack in melee sequence");
    }
    if (attackAnimations.size() != _attacks.size()) {
        throw std::logic_error(
            "Melee attack animation count does not match attack count");
    }

    for (size_t index = 0; index < _attacks.size(); ++index) {
        Attack &attack = _attacks[index];
        attack.impactTimeMilliseconds =
            animations.getMeleeImpactTime(attackAnimations[index], index);
        attack.meleeSignaled = false;
    }
    _pendingMeleeAttacks = _attacks.size();
    _meleeSequencePrepared = true;
}

size_t AttackBuffer::signalReadyMelee(
    int elapsedMilliseconds,
    Game &game,
    ServicesView &services,
    Creature &attacker,
    Object &target) {

    if (!_meleeSequencePrepared) {
        throw std::logic_error("Melee attack sequence is not prepared");
    }
    if (!hasPendingMelee()) {
        return 0;
    }

    std::vector<size_t> ready;
    ready.reserve(_pendingMeleeAttacks);
    for (size_t index = 0; index < _attacks.size(); ++index) {
        const Attack &attack = _attacks[index];
        if (!attack.meleeSignaled &&
            elapsedMilliseconds >= attack.impactTimeMilliseconds) {
            ready.push_back(index);
        }
    }
    std::stable_sort(
        ready.begin(),
        ready.end(),
        [this](size_t left, size_t right) {
            return _attacks[left].impactTimeMilliseconds <
                   _attacks[right].impactTimeMilliseconds;
        });

    for (size_t index : ready) {
        Attack &attack = _attacks[index];
        signalAttack(attack, game, services, attacker, target);
        attack.meleeSignaled = true;
        --_pendingMeleeAttacks;
    }
    return ready.size();
}

int AttackBuffer::latestMeleeImpactMilliseconds() const {
    if (!_meleeSequencePrepared) {
        throw std::logic_error("Melee attack sequence is not prepared");
    }
    int latest = 0;
    for (const Attack &attack : _attacks) {
        latest = std::max(latest, attack.impactTimeMilliseconds);
    }
    return latest;
}

void AttackBuffer::discardPendingMelee() {
    if (!_meleeSequencePrepared) {
        return;
    }
    for (Attack &attack : _attacks) {
        attack.meleeSignaled = true;
    }
    _pendingMeleeAttacks = 0;
}

bool AttackBuffer::hasPendingMelee() const {
    return _meleeSequencePrepared && _pendingMeleeAttacks != 0;
}

bool AttackBuffer::hasRangedAttack() const {
    return std::any_of(_attacks.begin(), _attacks.end(), [](const Attack &attack) { return attack.ranged; });
}

static constexpr float kCombatFeedbackRange2 = 900.0f;

static bool canReceiveCombatFeedback(
    const Creature &player,
    const Creature &subject) {

    return player.faction() == subject.faction() &&
           player.getSquareDistanceTo(subject) <= kCombatFeedbackRange2;
}

static constexpr int kStrRefAttackSummary = 42042;
static constexpr int kStrRefAttackSuccessVerb = 42043;
static constexpr int kStrRefAttackFailureVerb = 42044;
static constexpr int kStrRefAttackFeat = 42046;
static constexpr int kStrRefAttackRoll = 42119;
static constexpr int kStrRefAttackRollSuccess = 42133;
static constexpr int kStrRefAttackRollFailure = 42134;
static constexpr int kStrRefAttackBreakdown = 42146;
static constexpr int kStrRefStrengthModifier = 42154;
static constexpr int kStrRefMainhand = 42314;
static constexpr int kStrRefOffhand = 42315;
static constexpr int kStrRefAttackRollComponent = 42316;
static constexpr int kStrRefMeleeOnRangedBonus = 42317;
static constexpr int kStrRefFeatAttackBonus = 42318;
static constexpr int kStrRefCloseProximityRangedBonus = 42330;
static constexpr int kStrRefWeaponFocusBonus = 42331;
static constexpr int kStrRefEffectBonus = 42332;
static constexpr int kStrRefDualWieldPenalty = 42333;
static constexpr int kStrRefSmallOffhandBonus = 42334;
static constexpr int kStrRefDexterityModifier = 42375;
static constexpr int kStrRefAutomaticHit = 42390;
static constexpr int kStrRefAutomaticMiss = 42391;
static constexpr int kStrRefBaseAttackBonus = 42392;

static std::string getFeedbackString(
    Game &game,
    ServicesView &services,
    int strRef,
    std::initializer_list<std::pair<int, std::string>> tokens) {

    std::string text = services.resource.strings.getText(strRef);
    for (const auto &[token, value] : tokens) {
        text = game.substituteCustomToken(
            std::move(text),
            token,
            value);
    }
    return text;
}

void AttackBuffer::addCombatFeedback(
    Game &game,
    ServicesView &services,
    const Creature &attacker,
    const Object &target,
    const Attack &attack) const {

    if (game.isTSL()) {
        return;
    }

    auto leader = game.party().getLeader();
    if (!leader) {
        return;
    }

    bool broadcastFromAttacker = canReceiveCombatFeedback(*leader, attacker);
    const auto *targetCreature = dyn_cast<Creature>(&target);
    bool broadcastFromTarget = targetCreature &&
                               canReceiveCombatFeedback(*leader, *targetCreature);
    int broadcasts = static_cast<int>(broadcastFromAttacker) +
                     static_cast<int>(broadcastFromTarget);
    if (broadcasts == 0) {
        return;
    }

    const std::string &attackerName = attacker.name();
    const std::string &targetName = target.name();

    bool successful = isAttackSuccessful(attack.result);
    std::string feedback = getFeedbackString(
        game,
        services,
        kStrRefAttackSummary,
        {
            {0, attackerName},
            {1, services.resource.strings.getText(
                    successful ? kStrRefAttackSuccessVerb : kStrRefAttackFailureVerb)},
            {2, targetName},
        });
    feedback += ". ";

    if (isPhysicalAttackFeat(_feat)) {
        auto feat = services.game.feats.get(_feat);
        feedback += getFeedbackString(
            game,
            services,
            kStrRefAttackFeat,
            {{0, feat->name}});
        feedback += ". ";
    }

    feedback += getFeedbackString(
        game,
        services,
        kStrRefAttackRoll,
        {
            {0, services.resource.strings.getText(
                    successful ? kStrRefAttackRollSuccess : kStrRefAttackRollFailure)},
            {1, std::to_string(
                    attack.roll + attack.attackBonusBreakdown.total())},
            {2, std::to_string(attack.defense)},
            {3, std::to_string(
                    attack.damage.empty()
                        ? 0
                        : game.scaleDamageForDifficulty(
                              attack.damage.resolvedDamage(), target))},
        });

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Combat,
            feedback);
    }

    const AttackBonusBreakdown &bonus = attack.attackBonusBreakdown;
    std::string breakdown = getFeedbackString(
        game,
        services,
        kStrRefAttackBreakdown,
        {
            {0, services.resource.strings.getText(
                    attack.source == Source::Main
                        ? kStrRefMainhand
                        : kStrRefOffhand)},
            {1, std::to_string(
                    attack.roll + attack.attackBonusBreakdown.total())},
        });
    breakdown += getFeedbackString(
        game,
        services,
        kStrRefAttackRollComponent,
        {{0, std::to_string(attack.roll)}});

    if (!attack.assuredHit && attack.roll == 20) {
        breakdown += " ";
        breakdown += services.resource.strings.getText(kStrRefAutomaticHit);
    } else if (!attack.assuredHit && attack.roll == 1) {
        breakdown += " ";
        breakdown += services.resource.strings.getText(kStrRefAutomaticMiss);
    } else {
        breakdown += getFeedbackString(
            game,
            services,
            kStrRefBaseAttackBonus,
            {{0, std::to_string(bonus.baseAttackBonus)}});

        if (bonus.dualWieldPenalty != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefDualWieldPenalty,
                {{0, std::to_string(bonus.dualWieldPenalty)}});
        }
        if (bonus.smallOffhandBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefSmallOffhandBonus,
                {{0, std::to_string(bonus.smallOffhandBonus)}});
        }
        if (_feat != FeatType::Invalid && bonus.featBonus != 0) {
            auto feat = services.game.feats.get(_feat);
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefFeatAttackBonus,
                {
                    {0, feat->name},
                    {1, std::to_string(bonus.featBonus)},
                });
        }
        if (bonus.duelingBonus != 0) {
            auto feat = services.game.feats.get(bonus.duelingFeat);
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefFeatAttackBonus,
                {
                    {0, feat->name},
                    {1, std::to_string(bonus.duelingBonus)},
                });
        }
        if (bonus.closeProximityRangedBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefCloseProximityRangedBonus,
                {{0, std::to_string(bonus.closeProximityRangedBonus)}});
        }
        if (bonus.meleeOnRangedBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefMeleeOnRangedBonus,
                {{0, std::to_string(bonus.meleeOnRangedBonus)}});
        }
        if (bonus.dexterityModifier != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefDexterityModifier,
                {{0, std::to_string(bonus.dexterityModifier)}});
        } else if (bonus.strengthModifier != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefStrengthModifier,
                {{0, std::to_string(bonus.strengthModifier)}});
        }
        if (bonus.weaponFocusBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefWeaponFocusBonus,
                {{0, std::to_string(bonus.weaponFocusBonus)}});
        }
        if (bonus.effectBonus != 0) {
            breakdown += getFeedbackString(
                game,
                services,
                kStrRefEffectBonus,
                {{0, std::to_string(bonus.effectBonus)}});
        }
    }

    for (int broadcast = 0; broadcast < broadcasts; ++broadcast) {
        game.messageLog().add(
            MessageLog::kFeedbackMessageType,
            MessageLog::Style::Normal,
            breakdown);
    }
}

AttackResultType AttackBuffer::result() const {
    AttackResultType sorted[] {
        AttackResultType::Invalid,
        AttackResultType::Miss,
        AttackResultType::AttackResisted,
        AttackResultType::AttackFailed,
        AttackResultType::Parried,
        AttackResultType::Deflected,
        AttackResultType::HitSuccessful,
        AttackResultType::CriticalHit,
        AttackResultType::AutomaticHit,
    };
    ArrayRef<AttackResultType> sortedByScore(sorted);

    unsigned bestIndex = 0;

    for (const Attack &attack : _attacks) {
        for (unsigned i = 0; i < sortedByScore.size(); ++i) {
            if (sortedByScore[i] == attack.result) {
                bestIndex = std::max(bestIndex, i);
            }
        }
    }

    return sortedByScore[bestIndex];
}

std::shared_ptr<Item> determineProjectileWeapon(Creature &attacker, Projectile::Source source) {
    int slot = (source == Projectile::Main)
                   ? InventorySlots::rightWeapon
                   : InventorySlots::leftWeapon;

    std::shared_ptr<Item> weapon(attacker.getEquippedItem(slot));
    if (!weapon) {
        slot = (source == Projectile::Main)
                   ? InventorySlots::leftWeapon
                   : InventorySlots::rightWeapon;

        weapon = attacker.getEquippedItem(slot);
    }

    return weapon;
}

static glm::vec3 determineProjectileOrigin(scene::ModelSceneNode &model, Projectile::Source source) {
    std::string attachment = (source == Projectile::Main) ? "rhand" : "lhand";
    auto weaponModel = static_cast<scene::ModelSceneNode *>(model.getAttachment(attachment));
    if (weaponModel) {
        auto bulletHook = weaponModel->getNodeByName("bullethook");
        if (bulletHook) {
            return bulletHook->origin();
        }
        return weaponModel->origin();
    }

    // Droids do not have weapon model, but they have hooks in the main (body) model.
    std::string directAttachment = (source == Projectile::Main) ? "rbullet" : "lbullet";
    if (scene::SceneNode *direct = model.getNodeByName(directAttachment)) {
        return direct->origin();
    }

    return model.origin();
}

static std::optional<glm::vec3> determineMuzzleFlashOrigin(scene::ModelSceneNode &model, Projectile::Source source) {
    std::string attachment = (source == Projectile::Main) ? "rhand" : "lhand";
    auto weaponModel = static_cast<scene::ModelSceneNode *>(model.getAttachment(attachment));
    if (weaponModel) {
        if (auto muzzleHook = weaponModel->getNodeByName("muzzlehook")) {
            return muzzleHook->origin();
        }
    }
    return std::nullopt;
}

// Blaster bolt and muzzle-flash models point along their local +Y (their
// emitters sit on +Y and emit along it). Turn +Y onto dir, yaw then pitch.
static glm::mat4 orientAlongY(const glm::vec3 &position, const glm::vec3 &dir) {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    float length = glm::length(dir);
    if (length < 1e-5f) {
        return transform;
    }
    glm::vec3 d = dir / length;
    float yaw = glm::atan(d.y, d.x) - glm::half_pi<float>();
    float pitch = glm::asin(glm::clamp(d.z, -1.0f, 1.0f));
    return transform * glm::eulerAngleZ(yaw) * glm::eulerAngleX(pitch);
}

// Plays an effect model's animation. The scene signals an animation's events
// only once time has moved past them, so an event at time 0 (a bolt's
// detonate, a muzzle flash's first burst) is signalled here instead.
static void playEffectAnimation(scene::ModelSceneNode &model, const std::string &name) {
    auto anim = model.model().getAnimation(name);
    if (!anim) {
        model.signalEvent(kModelEventDetonate);
        return;
    }
    model.playAnimation(*anim, nullptr, scene::AnimationProperties::fromFlags(0));
    for (const auto &event : anim->events()) {
        if (event.time <= 0.0f) {
            model.signalEvent(event.name);
        }
    }
}

void Projectile::fire(Creature &attacker, Object &target, scene::ISceneGraph &sceneGraph) {
    auto attackerModel = std::static_pointer_cast<scene::ModelSceneNode>(attacker.sceneNode());
    auto targetModel = std::static_pointer_cast<scene::ModelSceneNode>(target.sceneNode());
    if (!attackerModel || !targetModel)
        return;

    std::shared_ptr<Item> weapon = determineProjectileWeapon(attacker, _source);
    if (!weapon)
        return;

    std::shared_ptr<Item::AmmunitionType> ammunitionType(weapon->ammunitionType());
    if (!ammunitionType)
        return;

    glm::vec3 projectilePos = determineProjectileOrigin(*attackerModel, _source);
    _damageFlags = weapon->damageFlags();

    // Determine projectile direction
    auto impact = targetModel->getNodeByName("impact");
    if (impact) {
        _target = impact->origin();
    } else {
        _target = targetModel->origin();
    }

    if (_miss) {
        float offsetRadius = 1.5f * glm::length(targetModel->origin() - _target);
        // A miss flies past the target, not through it: aim at a point off
        // to one side (cosmetic stream only) and carry on beyond it.
        glm::vec3 offsetDir(cosmeticRandomFloat(-1.0f, 1.0f),
                            cosmeticRandomFloat(-1.0f, 1.0f),
                            cosmeticRandomFloat(-0.5f, 0.5f));
        if (glm::length(offsetDir) < 1e-3f) {
            offsetDir = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        // Close enough to read as a near miss, never through the body.
        offsetRadius = glm::clamp(offsetRadius, 0.5f, 1.0f);
        glm::vec3 offsetTarget = _target + glm::normalize(offsetDir) * offsetRadius;
        glm::vec3 dir = offsetTarget - projectilePos;
        _target = projectilePos + dir * 1000.0f;

        scene::Collision collision;
        if (sceneGraph.testLineOfSight(projectilePos, _target, collision)) {
            _target = collision.intersection;
            _struckGeometry = true;
        }
    }

    // Create and add a projectile to the scene graph, facing its flight.
    glm::vec3 flight = _target - projectilePos;
    _model = sceneGraph.newModel(*ammunitionType->model, scene::ModelUsage::Projectile);
    _model->setLocalTransform(orientAlongY(projectilePos, flight));
    sceneGraph.addRoot(_model);
    // The bolt's travel animation keys its light colour and detonates the
    // bolt emitter; a model without one is detonated directly.
    playEffectAnimation(*_model, "travel01");

    if (ammunitionType->muzzleFlash) {
        glm::vec3 origin = determineMuzzleFlashOrigin(*attackerModel, _source)
                               .value_or(projectilePos);

        _flash = sceneGraph.newModel(*ammunitionType->muzzleFlash, scene::ModelUsage::Projectile);
        _flash->setLocalTransform(orientAlongY(origin, _target - origin));
        sceneGraph.addRoot(_flash);
        // The flash's impact animation detonates its emitters in turn.
        playEffectAnimation(*_flash, "impact");
    }

    // Play shot sound, if any
    weapon->playShotSound(0, projectilePos);

    if (auto *coop = attacker.game().coop()) {
        coop->onProjectileFired(attacker, target, _source == Offhand, _miss);
    }
}

bool Projectile::update(float dt) {
    if (!_model) {
        return false;
    }

    glm::vec3 position = _model->origin();
    glm::vec3 vec = _target - position;
    float length = glm::length(vec);

    float dist = dt * kProjectileSpeed;
    if (dist >= length) {
        return true;
    }

    glm::vec3 dir = vec / length;
    position += dir * dist;

    _model->setLocalTransform(orientAlongY(position, dir));

    return false;
}

void Projectile::presentImpact(Creature &attacker, Object &target) {
    if (!_model) {
        return;
    }
    Game &game = attacker.game();
    ServicesView &services = game.services();
    if (!_miss) {
        if (auto table = services.resource.twoDas.get("damagehitvisual")) {
            for (int row : hitvisuals::damageTypeVisuals(*table, _damageFlags, /*ranged=*/true)) {
                hitvisuals::presentOn(game, services, row, target);
            }
        }
    } else if (_struckGeometry) {
        hitvisuals::presentAt(game, services, hitvisuals::kBlasterImpactGround, _target, 0.0f);
    }
}

void Projectile::reset() {
    if (!_model) {
        return;
    }

    _model->graph().removeRoot(*_model);
    _model.reset();
    if (_flash) {
        _flash->graph().removeRoot(*_flash);
        _flash.reset();
    }
}

void ProjectileSequence::push_back(float time, Projectile::Source source, bool miss) {
    _projectiles.emplace_back(source, miss);
    _events.push_back(time, _projectiles.size());
}

void ProjectileSequence::update(float dt, Creature &attacker, Object &target,
                                scene::ISceneGraph &sceneGraph) {
    // Update projectiles in flight
    for (Projectile &proj : _projectiles) {
        if (proj.update(dt)) {
            // Projectile hit the target
            proj.presentImpact(attacker, target);
            proj.reset();
        }
    }

    // Fire new projectiles
    _events.update(dt);
    while (TimeEvents::Event ev = _events.next()) {
        size_t index = ev - 1;
        _projectiles[index].fire(attacker, target, sceneGraph);
    }
}

void ProjectileSequence::reset() {
    for (Projectile &proj : _projectiles) {
        proj.reset();
    }
}

static void addProjectile(ProjectileSequence &seq, std::pair<float, int> timeKind, bool miss) {
    float time = timeKind.first;
    float kind = timeKind.second;
    seq.push_back(time, kind == 0 ? Projectile::Main : Projectile::Offhand, miss);
}

void addProjectilesFromSpec(ProjectileSequence &seq, const ProjectileSpec &spec) {
    uint32_t remainingMisses = spec.misses;
    size_t numProjectiles = spec.projectiles.size();
    for (size_t i = 0; i < numProjectiles; ++i) {
        bool autoMiss = (i + remainingMisses) >= numProjectiles;
        bool miss = autoMiss || (remainingMisses && cosmeticRandomInt(0, 1));
        if (miss) {
            --remainingMisses;
        }
        addProjectile(seq, spec.projectiles[i], miss);
    }
}

AttackSchedule::State AttackSchedule::update(
    const CombatRound &round, Action &action, float dt) {

    _time += dt;
    if (_melee && _state != AttackSchedule::WaitAttack) {
        float elapsedMilliseconds =
            dt * 1000.0f + _meleeElapsedRemainderMilliseconds;
        int wholeMilliseconds = static_cast<int>(elapsedMilliseconds);
        _meleeElapsedRemainderMilliseconds =
            elapsedMilliseconds - wholeMilliseconds;
        _meleeElapsedMilliseconds += wholeMilliseconds;
    }

    switch (_state) {
    case AttackSchedule::WaitAttack: {
        if (round.canExecute(action)) {
            _state = AttackSchedule::Attack;
        }
        break;
    }
    case AttackSchedule::Attack: {
        if (_melee &&
            _meleeElapsedMilliseconds >= _meleeCompletionMilliseconds) {
            _state = AttackSchedule::Damage;
        } else {
            _state = AttackSchedule::WaitDamage;
        }
        break;
    }
    case AttackSchedule::WaitDamage: {
        if ((_melee &&
             _meleeElapsedMilliseconds >= _meleeCompletionMilliseconds) ||
            (!_melee && _time >= kAttackDamageDelay)) {
            _state = AttackSchedule::Damage;
        }
        break;
    }
    case AttackSchedule::Damage: {
        _state = AttackSchedule::WaitFinish;
        break;
    }
    case AttackSchedule::WaitFinish: {
        if (round.state == CombatRound::Finished) {
            _state = AttackSchedule::Finish;
        }
        break;
    }
    case AttackSchedule::Finish: {
        break;
    }
    }

    return _state;
}

void AttackSchedule::startMelee(int latestImpactMilliseconds) {
    if (_state != AttackSchedule::Attack) {
        throw std::logic_error("Melee attack schedule has not started");
    }

    // Standard physical attack actions use a 1500 ms pause. A malformed
    // authored impact beyond it extends the execution window rather than
    // dropping the pending hit.
    static constexpr int kPhysicalAttackPauseMilliseconds = 1500;
    _melee = true;
    _meleeElapsedMilliseconds = 0;
    _meleeCompletionMilliseconds = std::max(
        kPhysicalAttackPauseMilliseconds,
        latestImpactMilliseconds);
    _meleeElapsedRemainderMilliseconds = 0.0f;
}

static constexpr float kLineOfFireCloseInDistance = 2.0f;
// How near the attacker comes to a shut door between it and its target to open it.
static constexpr float kDoorOpenDistance = 2.0f;

bool navigateToAttackTarget(Game &game, Creature &attacker, Object &target, float dt, bool &reachedOnce, bool *doorApproach) {
    if (reachedOnce) {
        return true;
    }

    // A shut door stands in its doorway, often off the walkmesh: when no path
    // leads to it, walk to where the door is used from, as for opening it.
    if (auto door = dyn_cast<Door>(&target); door && doorApproach) {
        if (!approachDoor(game, attacker, *door, *doorApproach, attacker.getAttackRange(), dt)) {
            return false;
        }
        reachedOnce = true;
        return true;
    }

    auto module = game.module();
    auto area = module ? module->area() : nullptr;
    float range = attacker.getAttackRange();

    // A shut door between the two stops blows and bolts alike. In range, the
    // attacker opens an unlocked one, as a player's click on it and the AI's
    // OnBlocked (k_ai_master: DoDoorAction) do, and attacks once it stands
    // open; behind a locked one the target is out of reach. Before, the
    // close-in rule below let an attack land through a shut door (the
    // tar_m10aa patrol droids, shot through ptar_secdoor from 2 m). Tested
    // before the walk to the target: that walk ends (and drops its path) at
    // once in range, which would undo the walk to the door every frame.
    Door *door = nullptr;
    if (area && attacker.getSquareDistanceTo(target) <= range * range) {
        door = area->closedDoorOnLineOfSight(attacker, target);
    }
    if (door) {
        if (door->isLocked() || door->isStatic()) {
            // No blow lands through it, but the target may be reachable
            // another way: walk the path to it (a locked door is a path
            // barrier, so the path goes round) until the sight line clears.
            // Before, the attacker stood in place for good (tar_m10aa: the
            // PC 28 m from a rec-room Vulkar for 900 ticks while the Vulkar's
            // friends shot it).
            attacker.navigateTo(target.position(), true, kLineOfFireCloseInDistance, dt);
            return false;
        }
        // Walk to where the door is used from, on the ground at the
        // attacker's level: the use point's own height can lie off the
        // walkmesh (ptar_highdoor in tar_m05aa, 0.2 m low), and a walk there
        // plans no path and never starts.
        glm::vec3 use = doorApproachPoint(game, *door, attacker);
        if (auto z = walkmeshElevationAt(area->pathfinder().uni, glm::vec2(use), attacker.position().z)) {
            use.z = *z;
        }
        if (!attacker.navigateTo(use, true, 0.5f, dt) &&
            attacker.getSquareDistanceTo(glm::vec2(door->position())) > kDoorOpenDistance * kDoorOpenDistance) {
            return false;
        }
        if (!door->isOpening()) {
            attacker.face(*door);
            door->open(&attacker);
            door->onOpen(attacker.id());
        }
        return false;
    }

    // In range is not enough for a ranged attacker: with a wall in the way it
    // closes in until it has a clear line of fire. Close up it attacks anyway,
    // so an attack can never wait forever on the sight test. The close-in is
    // its own walk, not a second one after the walk into range: that walk
    // ended "reached" every frame and cleared the close-in's failed path
    // requests and its no-progress clock, so an attack on a target behind a
    // wall with no path to it (tar08_davikguest03, locked in a side room
    // 23 m from the hall) never gave up.
    float approach = range;
    if (isRangedWieldType(attacker.getWieldType()) && area &&
        attacker.getSquareDistanceTo(target) <= range * range &&
        !area->hasLineOfSight(attacker, target)) {
        approach = kLineOfFireCloseInDistance;
    }
    if (!attacker.navigateTo(target.position(), true, approach, dt)) {
        return false;
    }

    reachedOnce = true;
    return true;
}

static bool hasAnim(const graphics::Model &model, const std::string &anim) {
    if (model.animations().count(anim)) {
        return true;
    }

    if (std::shared_ptr<graphics::Model> super = model.superModel()) {
        return hasAnim(*super, anim);
    }

    return false;
}

std::string getRangedAttackAnim(Creature &attacker, int kind) {
    CreatureWieldType wield = attacker.getWieldType();
    assert(isRangedWieldType(wield) && "invalid wield");

    // A headless creature (dedicated server, tests) has no model to animate.
    auto attackerModel = std::static_pointer_cast<scene::ModelSceneNode>(attacker.sceneNode());
    if (!attackerModel) {
        return "";
    }
    const graphics::Model &model = attackerModel->model();

    std::string animByWield = str(boost::format("b%da%d") % static_cast<int>(wield) % kind);
    if (hasAnim(model, animByWield)) {
        return animByWield;
    }

    std::string animBasic = str(boost::format("b0a%d") % kind);
    if (hasAnim(model, animBasic)) {
        return animBasic;
    }

    return "";
}

} // namespace game

} // namespace reone
