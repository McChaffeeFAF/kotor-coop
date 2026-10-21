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

#include "reone/system/portablemath.h"
#include "reone/game/creaturestates.h"
#include "reone/game/object/creature.h"

#include "reone/game/d20/forms.h"
#include "reone/game/d20/k2feats.h"
#include "reone/game/d20/prestige.h"
#include "reone/resource/k2merge.h"
#include "reone/game/hitvisuals.h"

#include "reone/game/companionbrain.h"
#include "reone/game/coopsession.h"

#include "reone/game/d20/skills.h"

#include <algorithm>
#include <array>
#include <set>

#include "reone/audio/di/services.h"
#include "reone/audio/mixer.h"
#include "reone/game/action.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/usefeat.h"
#include "reone/game/animationutil.h"
#include "reone/game/attack.h"
#include "reone/game/d20/classes.h"
#include "reone/game/debug.h"
#include "reone/game/di/services.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/experience.h"
#include "reone/game/effect/acdecrease.h"
#include "reone/game/effect/blasterdeflectiondecrease.h"
#include "reone/game/effect/blasterdeflectionincrease.h"
#include "reone/game/effect/factionmodifier.h"
#include "reone/game/effect/forcebody.h"
#include "reone/game/effect/forceresistancedecrease.h"
#include "reone/game/effect/forceresistanceincrease.h"
#include "reone/game/effect/fpregenmodifier.h"
#include "reone/game/effect/vpregenmodifier.h"
#include "reone/game/effect/acincrease.h"
#include "reone/game/effect/abilitydecrease.h"
#include "reone/game/effect/abilityincrease.h"
#include "reone/game/effect/attackdecrease.h"
#include "reone/game/effect/attackincrease.h"
#include "reone/game/effect/bonusfeat.h"
#include "reone/game/effect/damage.h"
#include "reone/game/effect/damagedecrease.h"
#include "reone/game/effect/damageimmunitydecrease.h"
#include "reone/game/effect/damageimmunityincrease.h"
#include "reone/game/effect/damageincrease.h"
#include "reone/game/effect/damagereduction.h"
#include "reone/game/effect/damageresistance.h"
#include "reone/game/effect/immunity.h"
#include "reone/game/effect/invisibility.h"
#include "reone/game/effect/savingthrowdecrease.h"
#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/effect/disguise.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/movementspeeddecrease.h"
#include "reone/game/effect/movementspeedincrease.h"
#include "reone/game/effect/skilldecrease.h"
#include "reone/game/effect/skillincrease.h"
#include "reone/game/effect/source.h"
#include "reone/game/effect/temporaryforcepoints.h"
#include "reone/game/effect/trueseeing.h"
#include "reone/game/d20/forcepoints.h"
#include "reone/game/d20/spell.h"
#include "reone/game/d20/classes.h"
#include "reone/game/combatsounds.h"
#include "reone/game/footstepsounds.h"
#include "reone/game/game.h"
#include "reone/game/object/area.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/reputes.h"
#include "reone/game/portraits.h"
#include "reone/game/script/runner.h"
#include "reone/game/surfaces.h"
#include "reone/game/twodautil.h"
#include "reone/graphics/animation.h"
#include "reone/graphics/context.h"
#include "reone/graphics/di/services.h"
#include "reone/graphics/textureregistry.h"
#include "reone/resource/2da.h"
#include "reone/resource/di/services.h"
#include "reone/resource/exception/notfound.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/2das.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/provider/models.h"
#include "reone/resource/provider/soundsets.h"
#include "reone/resource/provider/textures.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/scene/di/services.h"
#include "reone/scene/drawdebug.h"
#include "reone/scene/graphs.h"
#include "reone/scene/types.h"
#include "reone/script/types.h"
#include "reone/system/clock.h"
#include "reone/system/di/services.h"
#include "reone/system/exception/validation.h"
#include "reone/system/logutil.h"
#include "reone/system/randomutil.h"
#include "reone/system/timer.h"

using namespace reone::audio;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::scene;
using namespace reone::script;

namespace reone {

namespace game {

/**
 * A companion a co-op client controls gets no party AI: no follow from its
 * heartbeat, no combat orders from perception, attacks or round ends. Never
 * true without a co-op host session.
 */
static bool coopPlayerControlled(const Creature &creature) {
    auto *coop = creature.game().coop();
    return coop && coop->isPlayerControlled(creature.id());
}

// ClassList/KnownList0 SpellFlags: the power is ready for use.
static constexpr uint8_t kKnownSpellReady = 0x1;

static constexpr int kStrRefRemains = 38151;
static constexpr int kMaximumDodgeBonus = 10;
static constexpr int kMaximumSavingThrowModifier = 20;
static constexpr int kMaximumDamageEffectModifier = 36;
static constexpr int kAllSavingThrows = 0;
static constexpr int kFortitudeSavingThrow = 1;
static constexpr int kReflexSavingThrow = 2;
static constexpr int kWillSavingThrow = 3;
static constexpr int kSituationalAttackBonus = 10;
// Auto-combat asks the AI again after this long with nothing queued.
static constexpr float kAutoCombatRetry = 3.0f;
static constexpr float kCloseRangeAttackDistance2 = 25.0f;
static constexpr size_t kACBonusTypeCount = static_cast<size_t>(ACBonus::Deflection) + 1;
static constexpr float kKeepPathDuration = 1000.0f;
static constexpr float kPathPointTolerance = 0.5f;

static constexpr char kItemPropertyCostTable[] = "iprp_costtable";
static constexpr char kBonusCostTable[] = "iprp_bonuscost";
static constexpr char kMeleeCostTable[] = "iprp_meleecost";
static constexpr char kDecreaseCostTable[] = "iprp_neg5cost";
static constexpr char kDamageCostTable[] = "iprp_damagecost";
static constexpr char kResistanceCostTable[] = "iprp_resistcost";
static constexpr char kReductionCostTable[] = "iprp_soakcost";
static constexpr char kVulnerabilityCostTable[] = "iprp_damvulcost";
static constexpr char kDamageTypeTable[] = "iprp_damagetype";
static constexpr char kProtectionTable[] = "iprp_protection";

static std::string g_talkDummyNode("talkdummy");

static const std::string g_headHookNode("headhook");
static const std::string g_maskHookNode("gogglehook");
static const std::string g_rightHandNode("rhand");
static const std::string g_leftHandNode("lhand");

static int getEquipabilitySlot(int slot) {
    switch (slot) {
    case InventorySlots::rightWeapon2:
        return InventorySlots::rightWeapon;
    case InventorySlots::leftWeapon2:
        return InventorySlots::leftWeapon;
    default:
        return slot;
    }
}

static bool attackModifierApplies(
    AttackBonus modifierType,
    const Item *weapon,
    bool offHand) {

    switch (modifierType) {
    case AttackBonus::Misc:
        return true;
    case AttackBonus::Onhand:
        return weapon && !offHand;
    case AttackBonus::Offhand:
        return weapon && offHand;
    default:
        return false;
    }
}

static bool racialTypeMatches(uint16_t racialType, const Creature &target) {
    auto type = static_cast<RacialType>(racialType);
    return type == RacialType::All || type == target.racialType();
}

static bool attackAlignmentGroupMatches(uint16_t alignment, const Creature &target) {
    auto group = static_cast<Alignment>(alignment);
    // The native attack-property handler stores Neutral in the unused
    // law/chaos qualifier, so it applies to every target.
    return group == Alignment::All ||
           group == Alignment::Neutral ||
           group == target.alignment();
}

static bool defenseAlignmentGroupMatches(uint16_t alignment, const Creature &attacker) {
    auto group = static_cast<Alignment>(alignment);
    return group == Alignment::All || group == attacker.alignment();
}

static DamageType getItemPropertyDamageType(
    ServicesView &services,
    uint16_t subtype) {

    auto table = getRequiredTwoDA(
        services.resource.twoDas,
        kDamageTypeTable);
    validateTwoDARow(*table, kDamageTypeTable, subtype);
    return static_cast<DamageType>(1 << subtype);
}

static DamagePower getDamageReductionPower(
    ServicesView &services,
    uint16_t subtype) {

    auto table = getRequiredTwoDA(
        services.resource.twoDas,
        kProtectionTable);
    validateTwoDARow(*table, kProtectionTable, subtype);
    return static_cast<DamagePower>(subtype + 1);
}

static bool acDamageTypePropertyApplies(uint16_t subtype, int damageFlags) {
    // KOTOR 1 stores the raw IPRP_COMBATDAM row in the effect qualifier, then
    // compares it directly with runtime damage flags. Preserve that mismatch.
    switch (subtype) {
    case 1:
        return damageFlags == 0;
    case 2:
        return damageFlags == static_cast<int>(DamageType::Bludgeoning);
    case 4:
        return damageFlags == static_cast<int>(DamageType::Piercing);
    default:
        return false;
    }
}

static bool attackPropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::AttackBonus:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::AttackBonusVsAlignmentGroup:
        return target && attackAlignmentGroupMatches(subtype, *target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::AttackBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static bool defensePropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *attacker,
    int damageFlags) {

    switch (property) {
    case ItemProperty::AcBonus:
        return true;
    case ItemProperty::AcBonusVsAlignmentGroup:
        return attacker && defenseAlignmentGroupMatches(subtype, *attacker);
    case ItemProperty::AcBonusVsDamageType:
        return acDamageTypePropertyApplies(subtype, damageFlags);
    case ItemProperty::AcBonusVsRacialGroup:
        return attacker && racialTypeMatches(subtype, *attacker);
    default:
        return false;
    }
}

static void getSituationalAttackBonuses(
    const Creature &attacker,
    const Creature &target,
    const Item *weapon,
    int &closeProximityRangedBonus,
    int &meleeOnRangedBonus) {

    closeProximityRangedBonus = 0;
    meleeOnRangedBonus = 0;

    if (weapon && weapon->isRanged()) {
        if (attacker.getSquareDistanceTo(target) <= kCloseRangeAttackDistance2) {
            closeProximityRangedBonus = kSituationalAttackBonus;
        }
        return;
    }

    auto targetWeapon = target.getEquippedItem(InventorySlots::rightWeapon);
    if (targetWeapon && targetWeapon->isRanged()) {
        meleeOnRangedBonus = k2feats::meleeOnRangedAttackBonus(
            [&attacker](FeatType feat) { return attacker.hasEffectiveFeat(feat); });
    }
}

static bool equippedItemAppliesToAttack(
    int slot,
    const Item &item,
    const Item *weapon,
    bool offHand) {

    switch (slot) {
    case InventorySlots::rightWeapon:
        return weapon == &item &&
               (!offHand || item.weaponWield() == WeaponWield::DoubleBladedSword);
    case InventorySlots::leftWeapon:
        return weapon == &item && offHand;
    case InventorySlots::hands:
        return !weapon && !offHand;
    case InventorySlots::cWeaponL:
    case InventorySlots::cWeaponR:
    case InventorySlots::cWeaponB:
    case InventorySlots::rightWeapon2:
    case InventorySlots::leftWeapon2:
        return false;
    default:
        return true;
    }
}

static bool isHandSpecificAttackModifierSlot(int slot) {
    switch (slot) {
    case InventorySlots::rightWeapon:
    case InventorySlots::leftWeapon:
    case InventorySlots::hands:
        return true;
    default:
        return false;
    }
}

static void addAttackModifier(int modifier, int &bonus, int &penalty) {
    if (modifier > 0) {
        bonus += modifier;
    } else if (modifier < 0) {
        penalty -= modifier;
    }
}

static int getCostTableValue(
    ServicesView &services,
    const std::string &resRef,
    int row,
    const std::string &column,
    int blankValue) {

    auto table = getRequiredTwoDA(services.resource.twoDas, resRef);
    return table->getInt(row, column, blankValue);
}

struct NamedTwoDA {
    std::string resRef;
    std::shared_ptr<TwoDA> table;
};

static NamedTwoDA getItemPropertyCostTable(
    ServicesView &services,
    int index) {

    auto costTables = getRequiredTwoDA(
        services.resource.twoDas,
        kItemPropertyCostTable);
    std::string resRef = boost::to_lower_copy(
        costTables->getString(index, "name"));
    return {resRef, getRequiredTwoDA(services.resource.twoDas, resRef)};
}

static int getItemPropertyValue(
    ServicesView &services,
    const Item::PropertyEntry &property,
    const std::string &column,
    int blankValue) {

    auto costTable = getItemPropertyCostTable(
        services,
        property.costTable);
    return costTable.table->getInt(
        property.costValue,
        column,
        blankValue);
}

static bool savingThrowModifierApplies(
    int save,
    SavingThrowType modifierType,
    int requestedSave,
    SavingThrowType requestedType) {

    return (save == kAllSavingThrows || save == requestedSave) &&
           (modifierType == SavingThrowType::All ||
            modifierType == requestedType);
}

static bool savingThrowPropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    int requestedSave,
    SavingThrowType requestedType) {

    switch (property) {
    case ItemProperty::ImprovedSavingThrow:
    case ItemProperty::DecreasedSavingThrows:
        return subtype == static_cast<uint16_t>(SavingThrowType::All) ||
               subtype == static_cast<uint16_t>(requestedType);
    case ItemProperty::ImprovedSavingThrowSpecific:
    case ItemProperty::DecreasedSavingThrowsSpecific:
        return subtype == kAllSavingThrows || subtype == requestedSave;
    default:
        return false;
    }
}

struct DamageModifier {
    int costValue;
    int numDice;
    int die;
    int flat;
    DamageType type;
};

static std::optional<DamageModifier> getDamageModifier(
    ServicesView &services,
    uint16_t costValue,
    DamageType type) {

    auto table = getRequiredTwoDA(services.resource.twoDas, kDamageCostTable);

    DamageModifier result {costValue, 0, 0, 0, type};

    auto numDice = table->getIntOpt(costValue, "numdice");
    if (!numDice) {
        result.flat = costValue;
    } else {
        auto die = table->getIntOpt(costValue, "die");
        if (!die) {
            throw ValidationException(
                "Missing die in iprp_damagecost row " +
                std::to_string(costValue));
        }
        result.numDice = *numDice;
        result.die = *die;
    }

    if (result.numDice <= 0 && result.flat <= 0) {
        return std::nullopt;
    }
    return result;
}

static std::optional<DamageModifier> getFlatDamageModifier(
    ServicesView &services,
    const std::string &resRef,
    uint16_t costValue,
    DamageType type) {

    int value = getCostTableValue(
        services,
        resRef,
        costValue,
        "value",
        0);
    if (value == 0) {
        return std::nullopt;
    }

    return DamageModifier {costValue, 0, 0, value, type};
}

static int rollDamageModifier(
    const DamageModifier &modifier,
    int multiplier) {

    if (modifier.numDice <= 0 || modifier.die <= 0) {
        return multiplier * modifier.flat;
    }

    int result = 0;
    for (int multiple = 0; multiple < multiplier; ++multiple) {
        for (int die = 0; die < modifier.numDice; ++die) {
            result += randomInt(1, modifier.die);
        }
    }
    return result;
}

static void selectDamageModifier(
    std::map<int, DamageModifier> &modifiers,
    DamageModifier modifier) {

    int type = static_cast<int>(modifier.type);
    auto it = modifiers.find(type);
    if (it == modifiers.end() ||
        modifier.costValue > it->second.costValue) {
        modifiers.insert_or_assign(type, std::move(modifier));
    }
}

static bool damagePropertyApplies(
    ItemProperty property,
    uint16_t subtype,
    const Creature *target) {

    switch (property) {
    case ItemProperty::EnhancementBonus:
    case ItemProperty::DamageBonus:
    case ItemProperty::DecreasedDamage:
        return true;
    case ItemProperty::EnhancementBonusVsAlignmentGroup:
    case ItemProperty::DamageBonusVsAlignmentGroup:
        return target && attackAlignmentGroupMatches(subtype, *target);
    case ItemProperty::EnhancementBonusVsRacialGroup:
    case ItemProperty::DamageBonusVsRacialGroup:
        return target && racialTypeMatches(subtype, *target);
    default:
        return false;
    }
}

static bool equippedItemPropertiesAreActive(int slot) {
    return slot != InventorySlots::rightWeapon2 &&
           slot != InventorySlots::leftWeapon2;
}

static void addDefenseModifier(
    int value,
    ACBonus modifierType,
    std::array<int, kACBonusTypeCount> &modifiers) {

    int index = static_cast<int>(modifierType);
    if (value <= 0 || index < 0 || index >= static_cast<int>(modifiers.size())) {
        return;
    }

    if (modifierType == ACBonus::Dodge) {
        modifiers[index] += value;
    } else {
        modifiers[index] = std::max(modifiers[index], value);
    }
}

static int getDefenseModifier(
    const std::array<int, kACBonusTypeCount> &bonuses,
    const std::array<int, kACBonusTypeCount> &penalties) {

    int result = std::min(
        bonuses[static_cast<int>(ACBonus::Dodge)] -
            penalties[static_cast<int>(ACBonus::Dodge)],
        kMaximumDodgeBonus);

    for (int i = static_cast<int>(ACBonus::Natural);
         i <= static_cast<int>(ACBonus::Deflection);
         ++i) {
        result += bonuses[i] - penalties[i];
    }
    return result;
}

Creature::Creature(
    uint32_t id,
    std::string sceneName,
    Game &game,
    ServicesView &services) :
    Object(id, ObjectType::Creature, std::move(sceneName), game, services) {

    // Workaround: the original engine does not retain perception range in
    // savegames. Set default ranges to match PercepRngDefault from ranges.2da.
    _perception.sightRange = 20.0f;
    _perception.hearingRange = 20.0f;
}

void Creature::retireAreaRuntime(
    Pathfinder &pathfinder,
    const std::set<const Object *> &retainedObjects) {
    retireAreaRuntimeState(retainedObjects);

    if (_path) {
        releasePath(pathfinder, *_path);
        _path.reset();
    }
    _pathVelocity = glm::vec3(0.0f);
    _previousPosition = _position;
    _stuckForce = glm::vec3(0.0f);
    _sideStepTime = 0.0f;
    _sideStepCount = 0;
    _replanPending = false;
    _steeringStarted = false;
    setMovementType(MovementType::None);
    _blockingDoorId = script::kObjectInvalid;
    _blockedEventDoorId = script::kObjectInvalid;

    deactivateCombat(0.0f);
    _combatState.attackTarget.reset();
    _combatState.attemptedAttackTarget.reset();
    _combatState.attemptedSpellTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    _combatState.deactivationTimer.reset(0.0f);
    _lastHostileTarget.reset();
    _lastAttackAction = ActionType::QueueEmpty;
    _lastCombatFeat = FeatType::Invalid;
    _lastForcePower = -1;
    _lastForcePowerSuccess = false;
    _blockingCreature.reset();
    _foundEnemyCreature.reset();
    _lastAttackResult = AttackResultType::Invalid;

    _perception.seen.clear();
    _perception.heard.clear();
    stopTalking();
    stopStuntMode();
    if (_audioSourceVoice) _audioSourceVoice->stop();
    if (_audioSourceFootstep) _audioSourceFootstep->stop();
    _audioSourceVoice.reset();
    _audioSourceFootstep.reset();
}

void Creature::loadFromBlueprint(const std::string &resRef) {
    auto utc = _services.resource.gffs.get(resRef, ResType::Utc);
    if (!utc) {
        return;
    }
    // A blueprint is a single source, so deserialize it once. Routing through
    // deserialize() would re-read the self-referential TemplateResRef and
    // deserialize the same data twice, doubling accumulated class levels.
    deserializeAll(*utc, SerializedIdentityContext::templateResource(resRef));
    restoreSerializedVitality();
    updateTransform();
    loadAppearance();
}

void Creature::resolveMovementSpeeds() const {
    if (_resolvedForRate == _walkRate && _resolvedForAppearance == static_cast<int>(_appearance)) {
        return;
    }
    _resolvedForRate = _walkRate;
    _resolvedForAppearance = static_cast<int>(_appearance);
    // Fallback: the clip loop distances, which is what reone used before.
    _resolvedWalkSpeed = _walkSpeed;
    _resolvedRunSpeed = _runSpeed;

    std::shared_ptr<TwoDA> speeds(_services.resource.twoDas.get("creaturespeed"));
    if (!speeds) {
        return;
    }
    static constexpr int kDefaultMovementRate = 7;
    int row = _walkRate;
    if (row == kDefaultMovementRate) {
        // "Default": the creaturespeed row named by the appearance's moverate.
        row = 0;
        for (int i = 0; i < speeds->getRowCount(); ++i) {
            if (boost::iequals(speeds->getString(i, "2daname"), _appearanceMoveRate)) {
                row = i;
                break;
            }
        }
    }
    if (row < 0 || row >= speeds->getRowCount()) {
        return;
    }
    _resolvedWalkSpeed = speeds->getFloat(row, "walkrate", _walkSpeed);
    _resolvedRunSpeed = speeds->getFloat(row, "runrate", _runSpeed);
}

namespace {

/** Visit an applied effect's payload and, through links, every payload in it. */
template <class F>
void forEachPayload(const std::shared_ptr<Effect> &effect, EffectId instanceId, F &&fn) {
    if (!effect) {
        return;
    }
    if (auto link = std::dynamic_pointer_cast<LinkEffectsEffect>(effect)) {
        // A child the target refused at apply contributes nothing.
        for (const auto &child : {link->childEffect(), link->parentEffect()}) {
            if (child && link->accepts(instanceId, *child)) {
                forEachPayload(child, instanceId, fn);
            }
        }
        return;
    }
    fn(*effect);
}

template <class T, class F>
void forEachLiveEffect(const std::deque<EffectInstance> &effects, F &&fn) {
    for (const EffectInstance &applied : effects) {
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        forEachPayload(applied.effect, applied.id, [&fn](Effect &payload) {
            if (auto *typed = dynamic_cast<T *>(&payload)) {
                fn(*typed);
            }
        });
    }
}

} // namespace

float Creature::getMovementSpeedFactor() const {
    // The largest increase and the largest decrease apply (nwscript notes:
    // an increase under 100 adds to 100 %; a decrease of 100 or more does
    // nothing, a negative one is an increase).
    int increase = 100;
    int decrease = 0;
    forEachLiveEffect<MovementSpeedIncreaseEffect>(effects(), [&](const MovementSpeedIncreaseEffect &effect) {
        int percent = effect.newSpeedPercent();
        percent = percent < 100 ? 100 + percent : percent;
        increase = std::max(increase, percent);
    });
    forEachLiveEffect<MovementSpeedDecreaseEffect>(effects(), [&](const MovementSpeedDecreaseEffect &effect) {
        int change = effect.percentChange();
        if (change < 0) {
            increase = std::max(increase, 100 - change);
        } else if (change < 100) {
            decrease = std::max(decrease, change);
        }
    });
    return (increase / 100.0f) * ((100 - decrease) / 100.0f);
}

void Creature::surrender(bool retainOwnEffects) {
    // K2 behaviour, owner decision (design note c08_effects): the caller and
    // every creature it is hostile to within 250 m stop fighting.
    static constexpr float kSurrenderRadius = 250.0f;
    if (_isPC || _dead) {
        return;
    }
    // Hostility is judged before the caller's faction changes.
    std::vector<Creature *> affected {this};
    if (auto module = _game.module(); module && module->area()) {
        for (const auto &object : module->area()->getObjectsByType(ObjectType::Creature)) {
            auto *other = static_cast<Creature *>(object.get());
            if (other == this || other->isDead() ||
                glm::distance2(other->position(), _position) > kSurrenderRadius * kSurrenderRadius ||
                !_services.game.reputes.getIsEnemy(*this, *other)) {
                continue;
            }
            affected.push_back(other);
        }
    }
    for (Creature *creature : affected) {
        creature->clearAllActions();
        creature->cancelCombat();
        if (creature != this) {
            creature->clearHostileActionsAgainst(*this);
        }
        // Strip temporary and permanent effects; item and innate ones stay.
        // The retain form keeps what each creature made on itself.
        std::vector<EffectId> stripped;
        for (const EffectInstance &applied : creature->effects()) {
            if (applied.durationType() == DurationType::Equipped ||
                applied.durationType() == DurationType::Innate) {
                continue;
            }
            if (retainOwnEffects && applied.boundCreator().get() == creature) {
                continue;
            }
            stripped.push_back(applied.id);
        }
        for (EffectId id : stripped) {
            creature->removeEffectsById(id);
        }
    }
    // Invalid faction is neutral to every faction, whatever reputes.2da says.
    setFaction(Faction::Invalid);
}

bool Creature::isEntangled() const {
    return hasEffect(EffectType::Entangle);
}

bool Creature::isBlind() const {
    return hasEffect(EffectType::Blindness);
}

bool Creature::isDistracted() const {
    return hasEffect(EffectType::MindTrick) || hasEffect(EffectType::DroidScramble);
}

bool Creature::isConfused() const {
    return hasEffect(EffectType::PsychicStatic);
}

bool Creature::hasBodyFuel() const {
    return hasEffect(EffectType::BodyFuel);
}

bool Creature::isInFury() const {
    return hasEffect(EffectType::Fury);
}

void Creature::addFuryKill() {
    if (isInFury()) {
        _furyDamageBonus = std::min(_furyDamageBonus + 1, kMaximumFuryDamageBonus);
    }
}

void Creature::refreshFactionModifier() {
    std::optional<Faction> modified;
    forEachLiveEffect<FactionModifierEffect>(effects(), [&](const FactionModifierEffect &effect) {
        modified = static_cast<Faction>(effect.newFaction()); // the latest applied wins
    });
    if (modified) {
        if (!_factionBeforeModifier) {
            _factionBeforeModifier = _faction;
        }
        _faction = *modified;
    } else if (_factionBeforeModifier) {
        _faction = *_factionBeforeModifier;
        _factionBeforeModifier.reset();
    }
}

Faction Creature::serializedFaction() const {
    return _factionBeforeModifier.value_or(_faction);
}

int Creature::getForceResistance() const {
    // Resistances do not stack: the best increase counts, every decrease
    // lowers it.
    int best = 0;
    int decrease = 0;
    forEachLiveEffect<ForceResistanceIncreaseEffect>(effects(), [&](const ForceResistanceIncreaseEffect &effect) {
        best = std::max(best, effect.value());
    });
    forEachLiveEffect<ForceResistanceDecreaseEffect>(effects(), [&](const ForceResistanceDecreaseEffect &effect) {
        decrease += std::max(0, effect.value());
    });
    return std::max(0, best - decrease);
}

int Creature::getBlasterDeflectionModifier() const {
    int result = 0;
    forEachLiveEffect<BlasterDeflectionIncreaseEffect>(effects(), [&](const BlasterDeflectionIncreaseEffect &effect) {
        result += effect.change();
    });
    forEachLiveEffect<BlasterDeflectionDecreaseEffect>(effects(), [&](const BlasterDeflectionDecreaseEffect &effect) {
        result -= effect.change();
    });
    return result;
}

int Creature::getVitalityRegenPercent() const {
    int result = 0;
    forEachLiveEffect<VPRegenModifierEffect>(effects(), [&](const VPRegenModifierEffect &effect) {
        result += effect.percent();
    });
    return result;
}

int Creature::getForcePointRegenPercent() const {
    int result = 0;
    forEachLiveEffect<FPRegenModifierEffect>(effects(), [&](const FPRegenModifierEffect &effect) {
        result += effect.percent();
    });
    return result;
}

int Creature::getVitalityRegenModifierCount() const {
    int result = 0;
    forEachLiveEffect<VPRegenModifierEffect>(effects(), [&](const VPRegenModifierEffect &) {
        ++result;
    });
    return result;
}

int Creature::getItemRegeneration(bool forcePoints) const {
    const auto wanted = forcePoints ? ItemProperty::RegenerationForcePoints : ItemProperty::Regeneration;
    int result = 0;
    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemPropertiesAreActive(slot)) {
            continue;
        }
        for (const auto &property : item->properties()) {
            if (static_cast<ItemProperty>(property.propertyName) != wanted || !item->isPropertyActive(property)) {
                continue;
            }
            result += std::max(0, getItemPropertyValue(_services, property, "value", property.costValue));
        }
    }
    return result;
}

int Creature::getForceBodyLevel() const {
    int level = -1;
    forEachLiveEffect<ForceBodyEffect>(effects(), [&](const ForceBodyEffect &effect) {
        if (effect.level() >= 0 && effect.level() <= 2) {
            level = std::max(level, effect.level());
        }
    });
    return level;
}

float gaitPlaybackRate(float groundSpeed, float clipLength, float clipDistance) {
    if (groundSpeed <= 0.0f || clipLength <= 0.0f || clipDistance <= 0.0f) {
        return 1.0f;
    }
    return groundSpeed * clipLength / clipDistance;
}

float Creature::walkSpeed() const {
    resolveMovementSpeeds();
    return _resolvedWalkSpeed * getMovementSpeedFactor();
}

float Creature::runSpeed() const {
    resolveMovementSpeeds();
    return _resolvedRunSpeed * getMovementSpeedFactor();
}

void Creature::loadAppearanceProperties() {
    std::shared_ptr<TwoDA> appearances(_services.resource.twoDas.get("appearance"));
    if (!appearances) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    _modelType = parseModelType(appearances->getString(_appearance, "modeltype"));
    // walkdist/rundist are the ground one loop of the walk/run clip covers
    // (design note p2_timing). The driveanim* columns belong to direct
    // (keyboard) driving and are not the clip distances.
    _walkSpeed = appearances->getFloat(_appearance, "walkdist", 1.0f);
    _runSpeed = appearances->getFloat(_appearance, "rundist", 1.0f);
    _appearanceMoveRate = boost::to_lower_copy(appearances->getString(_appearance, "moverate"));
    _resolvedForAppearance = -1;
    float personalSpace = appearances->getFloat(_appearance, "perspace", 0.6f);
    _creaturePersonalSpace = appearances->getFloat(_appearance, "creperspace", personalSpace);
    _size = static_cast<CreatureSize>(appearances->getInt(
        _appearance,
        "sizecategory",
        static_cast<int>(CreatureSize::Invalid)));
    _footstepType = appearances->getInt(_appearance, "footsteptype", -1);
    _envmap = boost::to_lower_copy(appearances->getString(_appearance, "envmap"));

    if (_portraitId > 0) {
        _portrait = _services.game.portraits.getTextureByIndex(_portraitId);
    } else {
        _portrait = _services.game.portraits.getTextureByAppearance(_appearance);
    }
}

void Creature::loadAppearance() {
    loadAppearanceProperties();

    auto modelSceneNode = buildModel();
    if (modelSceneNode) {
        finalizeModel(*modelSceneNode);
        _sceneNode = std::move(modelSceneNode);
        _sceneNode->setUser(*this);
        _sceneNode->setLocalTransform(_transform);
    }

    _animDirty = true;
}

Creature::ModelType Creature::parseModelType(const std::string &s) const {
    if (s == "S" || s == "L") {
        return ModelType::Creature;
    } else if (s == "F") {
        return ModelType::Droid;
    } else if (s == "B") {
        return ModelType::Character;
    }

    throw std::invalid_argument(str(boost::format("Model type '%s' is not supported") % s));
}

void Creature::updateModel() {
    if (!_sceneNode) {
        return;
    }
    auto bodyModelName = getBodyModelName();
    if (bodyModelName.empty()) {
        return;
    }
    auto replacement = _services.resource.models.get(bodyModelName);
    if (!replacement) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    model->setModel(*replacement);
    finalizeModel(*model);
    if (!_stunt) {
        model->setLocalTransform(_transform);
    }
    _animDirty = true;
}

void Creature::loadTransformFromGIT(const resource::generated::GIT_Creature_List &git) {
    _position[0] = git.XPosition;
    _position[1] = git.YPosition;
    _position[2] = git.ZPosition;

    float cosine = git.XOrientation;
    float sine = git.YOrientation;
    _orientation = yawQuat(-portable::atan2f(cosine, sine));

    updateTransform();
}

bool Creature::isDebilitated() const {
    return _combatState.debilitated || hasEffect(EffectType::Stunned);
}

bool Creature::isTemporarilyDead() const {
    return _game.party().isMember(*this) && currentHitPoints() <= 0;
}

bool Creature::isInvisibleTo(const Creature &observer) const {
    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource() ||
            applied.type() != EffectType::Invisibility ||
            !applied.appliesVersus(&observer)) {
            continue;
        }

        auto type = static_cast<InvisibilityType>(
            applied.integerParameter(0));
        switch (type) {
        case InvisibilityType::Normal:
        case InvisibilityType::Improved:
            if (observer.hasVisibilityCounter(
                    kSeeInvisibleCounter | kTrueSeeingCounter)) {
                continue;
            }
            return true;
        case InvisibilityType::Darkness:
            if (observer.hasVisibilityCounter(
                    kUltravisionCounter | kTrueSeeingCounter)) {
                continue;
            }
            return true;
        }
    }
    return false;
}

void Creature::setVisibilityCounter(uint8_t bit) {
    _visibilityCounterBits |= bit;
}

bool Creature::hasVisibilityCounter(uint8_t bits) const {
    uint8_t effective = _visibilityCounterBits;
    if (!hasEffect(EffectType::SeeInvisible)) {
        effective &= ~kSeeInvisibleCounter;
    }
    if (!hasEffect(EffectType::TrueSeeing)) {
        effective &= ~kTrueSeeingCounter;
    }
    if (!hasEffect(EffectType::Ultravision) &&
        !_trueSeeingUltravisionQuirk) {
        effective &= ~kUltravisionCounter;
    }
    return (effective & bits) != 0;
}

void Creature::restoreVisibilityCounter(
    EffectType type,
    uint8_t bit,
    EffectId removedEffect,
    bool trueSeeingRemovalQuirk) {

    _visibilityCounterBits &= ~bit;
    if (bit == kUltravisionCounter) {
        _trueSeeingUltravisionQuirk = false;
    }
    bool another = std::any_of(
        effects().begin(),
        effects().end(),
        [type, removedEffect](const EffectInstance &applied) {
            return applied.id != removedEffect &&
                   applied.hasLiveRuntimeSource() &&
                   applied.type() == type;
        });
    if (another) {
        if (trueSeeingRemovalQuirk) {
            _visibilityCounterBits |= kUltravisionCounter;
            _trueSeeingUltravisionQuirk = true;
        } else {
            _visibilityCounterBits |= bit;
        }
    }
}

void Creature::refreshVisibilityPerception() {
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (area && area->isObjectResident(*this)) {
        area->refreshPerceptionFor(*this);
    }
}

void Creature::clearHostileActionsAgainst(const Object &object) {
    auto attackTarget = _combatState.attackTarget.resolve();
    if (attackTarget.get() == &object) {
        _combatState.attackTarget.reset();
        _combatState.shouldDeactivate = true;
    }

    for (auto it = _actions.begin(); it != _actions.end();) {
        const std::shared_ptr<Action> &action = *it;
        std::shared_ptr<Object> target;
        if (action && action->type() == ActionType::AttackObject) {
            target = static_cast<AttackObjectAction &>(*action).target();
        } else if (action && action->type() == ActionType::UseFeat) {
            auto &featAction = static_cast<UseFeatAction &>(*action);
            if (isPhysicalAttackFeat(featAction.feat())) {
                target = featAction.target();
            }
        } else if (action && action->type() == ActionType::CastSpellAtObject && action != getCurrentAction()) {
            // A queued cast or item use at it; one already under way plays out.
            target = static_cast<CastSpellAtObjectAction &>(*action).target();
        }

        if (target.get() != &object) {
            ++it;
            continue;
        }
        action->cancel(action, *this);
        action->markCancelled();
        it = _actions.erase(it);
    }
}

bool Creature::canExecuteActions() const {
    return !hasEffect(EffectType::Stunned) &&
           !hasEffect(EffectType::CutSceneStunned) &&
           !isChoked() &&
           !isHeldInPose();
}

bool Creature::isChoked() const {
    return hasEffect(EffectType::Choke);
}

std::string Creature::getHeldPoseAnimation() const {
    if (hasEffect(EffectType::Crush)) {
        return "forcecrush";
    }
    if (hasEffect(EffectType::WhirlWind)) {
        return "whirlwind";
    }
    if (hasEffect(EffectType::Paralyze) || hasEffect(EffectType::CutSceneParalyze)) {
        return "paralyzed";
    }
    if (hasEffect(EffectType::Sleep)) {
        return "sleep";
    }
    if (hasEffect(EffectType::Horrified) || hasEffect(EffectType::CutSceneHorrified)) {
        return "horror";
    }
    if (hasEffect(EffectType::DroidStun)) {
        // Droids shut down; a droid model without the clip twitches instead.
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model && !model->model().getAnimation("disabled")) {
            return getFirstIfCreatureModel("cspasm", "spasm");
        }
        return "disabled";
    }
    if (hasEffect(EffectType::LightsaberThrow)) {
        // The saber is out: the thrower waits, hand raised, for it to return.
        return "throwsablp";
    }
    return "";
}

bool Creature::isHeldInPose() const {
    return isKnockedDown() || !getHeldPoseAnimation().empty();
}

void Creature::enterHeldPose() {
    setMovementType(MovementType::None);
    if (!isKnockedDown()) {
        resumeStateDrivenAnimation();
    }
}

void Creature::leaveHeldPose() {
    if (!isKnockedDown()) {
        resumeStateDrivenAnimation();
    }
}

bool Creature::isKnockdownHeld() const {
    return hasEffect(EffectType::Knockdown) ||
           hasEffect(EffectType::ForcePushed) ||
           hasEffect(EffectType::ForcePushTargeted);
}

void Creature::knockDown() {
    if (_dead || isKnockedDown()) {
        return;
    }
    static constexpr float kDefaultFallLength = 1.0f;
    static constexpr float kDefaultGetUpLength = 1.5f;

    // Humanoid supermodels fall with g1y1 and get up with g1z1, holding the
    // last frame of the fall while down. Creature and droid models have a
    // fall, a lying loop and a get-up of their own.
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    bool humanoid = _modelType == ModelType::Character;
    if (model) {
        if (model->model().getAnimation("g1y1")) {
            humanoid = true;
        } else if (model->model().getAnimation("ckdbck")) {
            humanoid = false;
        }
    }
    if (humanoid) {
        _knockdown.fallAnim = "g1y1";
        _knockdown.downAnim.clear();
        _knockdown.getUpAnim = "g1z1";
    } else {
        _knockdown.fallAnim = "ckdbck";
        _knockdown.downAnim = "ckdbcklp";
        _knockdown.getUpAnim = "cgustandb";
    }
    auto clipLength = [&model](const std::string &name, float fallback) {
        auto anim = model ? model->model().getAnimation(name) : nullptr;
        return anim && anim->length() > 0.0f ? anim->length() : fallback;
    };
    _knockdown.fallLength = clipLength(_knockdown.fallAnim, kDefaultFallLength);
    _knockdown.getUpLength = clipLength(_knockdown.getUpAnim, kDefaultGetUpLength);

    setMovementType(MovementType::None);
    setKnockdownPhase(KnockdownPhase::Falling);
    updateModelAnimation();
}

void Creature::startKnockback(const glm::vec2 &direction, float distance, bool ignoreObstacles) {
    if (_dead || distance <= 0.0f || glm::length(direction) < 1e-4f) {
        return;
    }
    _knockback.direction = glm::normalize(direction);
    snapPresentation();
    _knockback.remaining = distance;
    _knockback.ignoreObstacles = ignoreObstacles;
}

void Creature::setKnockdownPhase(KnockdownPhase phase) {
    _knockdown.phase = phase;
    _knockdown.phaseTime = 0.0f;
    _knockdown.clipStarted = false;
    _animFireForget = false;
    _animDirty = true;
}

void Creature::updateKnockdown(float dt) {
    if (_dead) {
        _knockback.remaining = 0.0f;
        _knockdown.phase = KnockdownPhase::None;
        return;
    }

    if (_knockback.remaining > 0.0f) {
        float step = std::min(kKnockbackSpeed * dt, _knockback.remaining);
        bool moved = true;
        auto module = _game.module();
        auto area = module ? module->area() : nullptr;
        if (area) {
            moved = area->knockbackCreature(*this, _knockback.direction, step, _knockback.ignoreObstacles);
        } else {
            setPosition(_position + glm::vec3(_knockback.direction * step, 0.0f));
        }
        _knockback.remaining = moved ? _knockback.remaining - step : 0.0f;
    }

    if (_knockdown.phase == KnockdownPhase::None) {
        return;
    }
    _knockdown.phaseTime += dt;
    switch (_knockdown.phase) {
    case KnockdownPhase::Falling:
        if (_knockdown.phaseTime >= _knockdown.fallLength) {
            setKnockdownPhase(isKnockdownHeld() ? KnockdownPhase::Down : KnockdownPhase::GettingUp);
        }
        break;
    case KnockdownPhase::Down:
        if (!isKnockdownHeld()) {
            setKnockdownPhase(KnockdownPhase::GettingUp);
        }
        break;
    case KnockdownPhase::GettingUp:
        if (_knockdown.phaseTime >= _knockdown.getUpLength) {
            setKnockdownPhase(KnockdownPhase::None);
        }
        break;
    default:
        break;
    }
}

void Creature::setDestroyable(bool destroyable, bool raiseable, bool selectableWhenDead) {
    _destroyable = destroyable;
    _raiseable = raiseable;
    _selectableWhenDead = selectableWhenDead;
}

bool Creature::resurrect(int hpPercent) {
    if (!_dead || !_raiseable) {
        return false;
    }
    int hitPoints = 1;
    if (hpPercent > 0) {
        hitPoints = std::max(1, static_cast<int>(
                                    (static_cast<int64_t>(maxHitPoints()) * hpPercent) / 100));
    }
    setCurrentHitPoints(hitPoints);
    if (_dead) {
        // Min-HP rules or a TSL PC threshold kept it down.
        return false;
    }
    // Death renamed the creature "Remains".
    _name = _firstName.str();
    const std::string &lastName = _lastName.str();
    if (!_name.empty() && !lastName.empty()) {
        _name += ' ';
    }
    _name += lastName;
    _animFireForget = false;
    _animDirty = true;
    playAnimation("getupdead", AnimationProperties::fromFlags(AnimationFlags::blend));
    return true;
}

bool Creature::isSelectable() const {
    bool hasDropableItems = false;
    for (auto &item : _items) {
        if (item->isDropable()) {
            hasDropableItems = true;
            break;
        }
    }
    return !_dead || hasDropableItems || _selectableWhenDead;
}

void Creature::update(float dt) {
    Object::update(dt);
    runPendingScriptEvents();
    // Movement restriction belongs to an action in progress. With no action
    // left nothing can release it, so never let it outlive the queue.
    if (_movementRestricted && actions().empty()) {
        _movementRestricted = false;
    }
    updateKnockdown(dt);
    updateModelAnimation();
    updateCombat(dt);
    updateCombatHandover(dt);
    updateLightsaberSoundPositions();
    updateDecay(dt);
}

// Body decay and body bags (engine design notes "Loot on death and body bags").
static constexpr float kDefaultDestroyObjectDelay = 3.0f;
static constexpr int kBodyBagFallbackAppearance = 3; // placeables.2da Backpack
static constexpr char kBodyBagTag[] = "Body Bag";
static constexpr char kCreditsTemplate[] = "g_i_credits001";

void Creature::updateDecay(float dt) {
    if (!_dead) {
        _decayTimer.reset();
        return;
    }
    if (_decayed || !_destroyable || _plot || _game.party().isMember(*this)) {
        return;
    }
    if (!_decayTimer) {
        // Removal waits appearance.2da DestroyObjectDelay; blank means 3 s.
        float delay = kDefaultDestroyObjectDelay;
        if (auto appearances = _services.resource.twoDas.get("appearance")) {
            if (auto value = appearances->getFloatOpt(static_cast<int>(_appearance), "destroyobjectdelay")) {
                delay = *value;
            }
        }
        _decayTimer = delay;
    }
    *_decayTimer -= dt;
    if (*_decayTimer > 0.0f) {
        return;
    }
    auto module = _game.module();
    if (!module || !module->area()) {
        return;
    }
    _decayed = true;
    spawnBodyBag();
    module->area()->destroyObject(*this);
}

void Creature::spawnBodyBag() {
    auto bodyBags = _services.resource.twoDas.get("bodybag");
    if (!bodyBags) {
        return;
    }
    // The creature's BodyBag row, else its appearance's.
    int row = _bodyBagId == 0xFF ? 0 : _bodyBagId;
    if (row == 0) {
        if (auto appearances = _services.resource.twoDas.get("appearance")) {
            row = appearances->getInt(static_cast<int>(_appearance), "body_bag", 0);
        }
    }
    if (row < 0 || row >= bodyBags->getRowCount()) {
        row = 0;
    }
    bool corpse = bodyBags->getBool(row, "corpse");
    bool loot = _gold > 0 || std::any_of(_items.begin(), _items.end(), [](const auto &item) { return item->isDropable(); });
    if (!corpse && !loot) {
        return;
    }
    int appearance = bodyBags->getInt(row, "appearance", -1);
    if (appearance < 0) {
        appearance = kBodyBagFallbackAppearance;
    }
    int nameStrRef = bodyBags->getInt(row, "name", kStrRefRemains);
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", kBodyBagTag))
                   .field(Gff::Field::newCExoLocString("LocName", nameStrRef, ""))
                   .field(Gff::Field::newDword("Appearance", static_cast<uint32_t>(appearance)))
                   .field(Gff::Field::newByte("Useable", 1))
                   .field(Gff::Field::newByte("HasInventory", 1))
                   .field(Gff::Field::newByte("PartyInteract", 1))
                   .field(Gff::Field::newByte("IsBodyBag", 1))
                   .field(Gff::Field::newByte("IsCorpse", corpse ? 1 : 0))
                   .field(Gff::Field::newByte("DieWhenEmpty", corpse ? 0 : 1))
                   .field(Gff::Field::newByte("Plot", corpse ? 0 : 1))
                   .field(Gff::Field::newByte("Faction", 1))
                   .field(Gff::Field::newShort("HP", 15))
                   .field(Gff::Field::newByte("Hardness", 5))
                   .field(Gff::Field::newByte("Fort", 16))
                   .field(Gff::Field::newFloat("Bearing", getFacing()))
                   .field(Gff::Field::newFloat("X", _position.x))
                   .field(Gff::Field::newFloat("Y", _position.y))
                   .field(Gff::Field::newFloat("Z", _position.z))
                   .build();
    auto bag = _game.newPlaceableFromGff(*gff, "bodybag", _sceneName);
    if (!bag) {
        return;
    }
    // The loot moves now: droppable items, then the gold as one credits item.
    moveDropableItemsTo(*bag);
    if (_gold > 0) {
        bag->addItem(kCreditsTemplate, _gold, true);
        _gold = 0;
    }
    _game.module()->area()->add(bag);
    debug("Creature " + _tag + " left a body bag");
}

void Creature::syncTalkLayer(ModelSceneNode &model) {
    // The state-driven path below plays the talk overlay along with the body
    // clip. It does not run while an explicitly played clip (a dialogue
    // gesture, a stunt) owns the body, and playing that clip drops the overlay
    // too: the speaker's mouth then stays shut. A layer sits on top of any
    // body clip and so covers that gap.
    const bool statePathRuns = _animFireForget ? model.isAnimationFinished() : _animDirty;
    auto talkAnim = model.model().getAnimation(getHeadTalkAnimation());
    if (!talkAnim) {
        _talkLayerOn = false;
        return;
    }
    if (_talking && !_dead && !statePathRuns) {
        if (!_talkLayerOn || _talkLayerLip != _lipAnimation) {
            if (_talkLayerOn) {
                model.fadeOutLayer(talkAnim->name(), true);
            }
            model.playAnimation(*talkAnim, _lipAnimation,
                                AnimationProperties::fromFlags(AnimationFlags::loopOverlay | AnimationFlags::layer | AnimationFlags::propagate));
            _talkLayerOn = true;
            _talkLayerLip = _lipAnimation;
        }
    } else if (_talkLayerOn) {
        model.fadeOutLayer(talkAnim->name(), true);
        _talkLayerOn = false;
        _talkLayerLip.reset();
    }
}

void Creature::updateModelAnimation() {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model)
        return;

    // While a stunt or gesture clip owns the body, the mouth still moves.
    syncTalkLayer(*model);

    if (_animFireForget) {
        if (!model->isAnimationFinished())
            return;

        _animFireForget = false;
        _animDirty = true;
    }
    if (!_animDirty)
        return;

    std::shared_ptr<Animation> anim;
    std::shared_ptr<Animation> talkAnim;
    // Knockdown fall and get-up play once and hold their last frame.
    bool playOnce = false;

    switch (_movementType) {
    case MovementType::Run:
        anim = model->model().getAnimation(getRunAnimation());
        if (_talking && !_dead) {
            // The mouth moves while the speaker walks or runs (a soldier
            // delivering a line as he storms in).
            talkAnim = model->model().getAnimation(getHeadTalkAnimation());
        }
        break;
    case MovementType::Walk:
        anim = model->model().getAnimation(getWalkAnimation());
        if (_talking && !_dead) {
            talkAnim = model->model().getAnimation(getHeadTalkAnimation());
        }
        break;
    default:
        if (_dead) {
            anim = model->model().getAnimation(getDeadAnimation());
        } else if (isKnockedDown() && _knockdown.clipStarted) {
            // Other state changes (combat, equipment) must not restart the
            // fall or get-up clip part way through.
        } else if (isKnockedDown()) {
            _knockdown.clipStarted = true;
            switch (_knockdown.phase) {
            case KnockdownPhase::Falling:
                anim = model->model().getAnimation(_knockdown.fallAnim);
                playOnce = true;
                break;
            case KnockdownPhase::Down:
                // With no lying loop, the last frame of the fall still holds.
                if (!_knockdown.downAnim.empty()) {
                    anim = model->model().getAnimation(_knockdown.downAnim);
                }
                break;
            default:
                anim = model->model().getAnimation(_knockdown.getUpAnim);
                playOnce = true;
                break;
            }
        } else if (isChoked()) {
            anim = model->model().getAnimation("choke");
        } else if (auto held = getHeldPoseAnimation(); !held.empty()) {
            anim = model->model().getAnimation(held);
        } else if (_talking) {
            anim = model->model().getAnimation(getTalkNormalAnimation());
            talkAnim = model->model().getAnimation(getHeadTalkAnimation());
        } else {
            anim = model->model().getAnimation(getPauseAnimation());
        }
        break;
    }

    if (_talking) {
        debug(str(boost::format("Lip: %s talking lip=%d body=%s talk=%d move=%d held=%d") % _tag % (_lipAnimation ? 1 : 0) %
                  (anim ? anim->name() : std::string("-")) % (talkAnim ? 1 : 0) % static_cast<int>(_movementType) %
                  (getHeldPoseAnimation().empty() ? 0 : 1)));
    }
    if (talkAnim && anim) {
        AnimationProperties bodyProperties = AnimationProperties::fromFlags(AnimationFlags::loopOverlay | AnimationFlags::propagate);
        // The gait still matches the ground speed under the talking overlay.
        if (_movementType == MovementType::Run) {
            bodyProperties.speed = gaitPlaybackRate(runSpeed(), anim->length(), _runSpeed);
        } else if (_movementType == MovementType::Walk) {
            bodyProperties.speed = gaitPlaybackRate(walkSpeed(), anim->length(), _walkSpeed);
        }
        model->playAnimation(*anim, nullptr, std::move(bodyProperties));
        model->playAnimation(*talkAnim, _lipAnimation, AnimationProperties::fromFlags(AnimationFlags::loopOverlay | AnimationFlags::propagate));
    } else {
        if (anim) {
            // The corpse pose is a short clip; looping/blending it makes the
            // model jerk and never settle, so play it once and hold the final
            // frame. Living poses keep looping and blending.
            int animFlags = (_dead || playOnce)
                                ? AnimationFlags::propagate
                                : (AnimationFlags::loopBlend | AnimationFlags::propagate);
            AnimationProperties properties = AnimationProperties::fromFlags(animFlags);
            // One loop of the walk/run clip covers walkdist/rundist metres;
            // scale playback to the creature's actual speed so the feet match
            // the ground.
            if (_movementType == MovementType::Run) {
                properties.speed = gaitPlaybackRate(runSpeed(), anim->length(), _runSpeed);
            } else if (_movementType == MovementType::Walk) {
                properties.speed = gaitPlaybackRate(walkSpeed(), anim->length(), _walkSpeed);
            }
            model->playAnimation(*anim, nullptr, properties);
        }

        if (talkAnim) {
            model->playAnimation(*talkAnim, _lipAnimation, AnimationProperties::fromFlags(AnimationFlags::loopBlend | AnimationFlags::propagate));
        }
    }

    _animDirty = false;
}

int Creature::derivePermanentMaxHitPoints() const {
    int result = _attributes.getPermanentMaxHitPoints(_hitPoints);
    return std::clamp<int>(
        result,
        0,
        std::numeric_limits<int16_t>::max());
}

void Creature::updateDeathFromCurrentHitPoints() {
    if (_minOneHP && _currentHitPoints < 1) {
        _currentHitPoints = 1;
    }
    _dead = _currentHitPoints <= (_game.isTSL() && _isPC ? -10 : 0);
}

void Creature::restoreSerializedVitality() {
    // MaxHitPoints is a retail cache. HitPoints and CurrentHitPoints are both
    // serialized on the base-vitality axis, so reconstruct the runtime value
    // only after permanent attributes, levels and feats have been read.
    if (_hitPoints <= 0 && _maxHitPoints > 0) {
        // Preserve the long-standing tolerance for malformed/mod records that
        // omit HitPoints but provide the cached maximum.
        _hitPoints = _maxHitPoints;
    }

    // CurrentForce, like CurrentHitPoints, is stored on the base axis.
    _maxForcePoints = static_cast<int16_t>(deriveMaxForcePoints());
    _currentForce = static_cast<int16_t>(std::clamp(
        _currentForce + _maxForcePoints - _forcePoints,
        0,
        static_cast<int>(_maxForcePoints)));

    const int serializedCurrent = _currentHitPoints;
    _maxHitPoints = derivePermanentMaxHitPoints();
    if (serializedCurrent > 0) {
        const int damage = static_cast<int>(_hitPoints) - serializedCurrent;
        _currentHitPoints = static_cast<int16_t>(std::clamp(
            static_cast<int>(_maxHitPoints) - damage,
            static_cast<int>(std::numeric_limits<int16_t>::min()),
            static_cast<int>(std::numeric_limits<int16_t>::max())));
    }
    updateDeathFromCurrentHitPoints();
}

int Creature::serializedCurrentHitPoints() const {
    // The temporary pool is not saved; the effect restores it on load.
    const int current = _currentHitPoints - _temporaryHitPoints;
    if (current <= 0) {
        return current;
    }
    const int damage = static_cast<int>(_maxHitPoints) - current;
    return std::clamp(
        static_cast<int>(_hitPoints) - damage,
        static_cast<int>(std::numeric_limits<int16_t>::min()),
        static_cast<int>(std::numeric_limits<int16_t>::max()));
}

void Creature::recalculatePermanentVitality() {
    recalculateForcePoints();
    const int oldMaximum = _maxHitPoints;
    const int oldCurrent = _currentHitPoints;
    _maxHitPoints = derivePermanentMaxHitPoints();
    if (oldCurrent > 0) {
        _currentHitPoints = static_cast<int16_t>(std::clamp(
            oldCurrent + static_cast<int>(_maxHitPoints) - oldMaximum,
            static_cast<int>(std::numeric_limits<int16_t>::min()),
            static_cast<int>(std::numeric_limits<int16_t>::max())));
    }
    updateDeathFromCurrentHitPoints();
}

void Creature::setMaxHitPoints(int baseHitPoints) {
    const int oldMaximum = _maxHitPoints;
    const int oldCurrent = _currentHitPoints;
    _hitPoints = static_cast<int16_t>(std::clamp(
        baseHitPoints,
        0,
        static_cast<int>(std::numeric_limits<int16_t>::max())));
    _maxHitPoints = derivePermanentMaxHitPoints();
    if (oldCurrent > 0) {
        _currentHitPoints = static_cast<int16_t>(std::clamp(
            oldCurrent + static_cast<int>(_maxHitPoints) - oldMaximum,
            static_cast<int>(std::numeric_limits<int16_t>::min()),
            static_cast<int>(std::numeric_limits<int16_t>::max())));
    }
    updateDeathFromCurrentHitPoints();
}

void Creature::setCurrentHitPoints(int hitPoints) {
    _currentHitPoints = static_cast<int16_t>(std::clamp(
        hitPoints,
        static_cast<int>(std::numeric_limits<int16_t>::min()),
        static_cast<int>(std::numeric_limits<int16_t>::max())));
    updateDeathFromCurrentHitPoints();
}

void Creature::initializeGeneratedVitality() {
    _hitPoints = static_cast<int16_t>(std::clamp(
        _attributes.getAggregateHitDie(),
        0,
        static_cast<int>(std::numeric_limits<int16_t>::max())));
    _maxHitPoints = derivePermanentMaxHitPoints();
    _currentHitPoints = _maxHitPoints;
    updateDeathFromCurrentHitPoints();
    _forcePoints = static_cast<int16_t>(std::clamp(
        _attributes.getAggregateForceDie(),
        0,
        static_cast<int>(std::numeric_limits<int16_t>::max())));
    _maxForcePoints = static_cast<int16_t>(deriveMaxForcePoints());
    _currentForce = _maxForcePoints;
}

int Creature::getSkillValue(SkillType skill) const {
    SkillUser user;
    user.ranks = _attributes.getSkillRank(skill);
    user.droid = _race == RacialType::Droid;
    user.partyMember = _game.party().isMember(*this);
    if (auto armor = getEquippedItem(InventorySlots::body)) {
        user.armorCheckPenalty = armor->armorCheckPenalty();
    }
    std::shared_ptr<Skill> info(_services.game.skills.get(skill));
    int value = user.ranks;
    if (info) {
        user.keyAbilityModifier = getEffectiveAbilityModifier(info->keyAbility);
        value = computeSkillValue(*info, user);
    }
    if (value < 0) {
        return value; // unusable untrained: effects do not make it usable
    }
    // One spell's effects do not add up with themselves; different sources do.
    EffectModifierReducer reducer;
    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource() ||
            applied.integerParameter(0, -1) != static_cast<int>(skill)) {
            continue;
        }
        if (applied.type() == EffectType::SkillIncrease) {
            reducer.addIncrease(getEffectSourceKey(applied), 0, applied.integerParameter(1));
        } else if (applied.type() == EffectType::SkillDecrease) {
            reducer.addDecrease(getEffectSourceKey(applied), 0, applied.integerParameter(1));
        }
    }
    constexpr int kUncapped = std::numeric_limits<int>::max() / 2;
    int modifier = reducer.totalIncrease(kUncapped) - reducer.totalDecrease(kUncapped);
    return std::max(0, value + modifier);
}

void Creature::drainForcePoints(int amount) {
    _currentForce = static_cast<int16_t>(std::max(0, _currentForce - std::max(0, amount)));
}

static int16_t clampForceShort(int value) {
    return static_cast<int16_t>(std::clamp(
        value,
        0,
        static_cast<int>(std::numeric_limits<int16_t>::max())));
}

int Creature::deriveMaxForcePoints() const {
    ForcePointSubject subject;
    subject.basePoints = _forcePoints;
    subject.droid = _race == RacialType::Droid;
    subject.playerProgression = _isPC;
    // Temporary force points are a separate pool, not part of the maximum.
    return std::min(
        computeMaxForcePoints(_attributes, subject),
        static_cast<int>(std::numeric_limits<int16_t>::max()));
}

void Creature::addBaseForcePoints(int amount) {
    _forcePoints = clampForceShort(_forcePoints + amount);
    recalculateForcePoints();
}

void Creature::recalculateForcePoints() {
    const int oldMaximum = _maxForcePoints;
    const int newMaximum = deriveMaxForcePoints();
    _maxForcePoints = static_cast<int16_t>(newMaximum);
    _currentForce = static_cast<int16_t>(std::clamp(
        _currentForce + newMaximum - oldMaximum,
        0,
        static_cast<int>(clampForceShort(newMaximum + _temporaryForce))));
}

void Creature::setCurrentForcePoints(int value) {
    _currentForce = static_cast<int16_t>(std::clamp(
        value, 0, static_cast<int>(clampForceShort(_maxForcePoints + _temporaryForce))));
}

void Creature::addTemporaryForcePoints(int amount) {
    if (amount <= 0) {
        return;
    }
    _temporaryForce += amount;
    _currentForce = clampForceShort(_currentForce + amount);
}

void Creature::removeTemporaryForcePoints(int amount) {
    amount = std::min(std::max(0, amount), _temporaryForce);
    _temporaryForce -= amount;
    _currentForce = clampForceShort(_currentForce - amount);
}

void Creature::addTemporaryHitPoints(int amount) {
    if (amount <= 0 || _dead) {
        return;
    }
    _temporaryHitPoints += amount;
    _currentHitPoints = static_cast<int16_t>(std::min(
        _currentHitPoints + amount,
        static_cast<int>(std::numeric_limits<int16_t>::max())));
}

void Creature::removeTemporaryHitPoints(int amount) {
    amount = std::min(std::max(0, amount), _temporaryHitPoints);
    _temporaryHitPoints -= amount;
    if (_dead || amount == 0) {
        return;
    }
    int remaining = _currentHitPoints - amount;
    if (remaining >= 1 || _minOneHP || _immortal) {
        _currentHitPoints = static_cast<int16_t>(std::max(1, remaining));
        return;
    }
    // Damage taken while the pool held the creature up is real: losing the
    // pool kills it.
    damage(std::numeric_limits<int>::max(), nullptr);
}

int Creature::serializedCurrentForce() const {
    // The temporary pool is not saved; the effect restores it on load.
    return std::clamp(
        _currentForce - _temporaryForce - (_maxForcePoints - _forcePoints),
        static_cast<int>(std::numeric_limits<int16_t>::min()),
        static_cast<int>(std::numeric_limits<int16_t>::max()));
}

void Creature::setIsPC(bool isPC) {
    _isPC = isPC;
    recalculateForcePoints();
}

int Creature::forcePowerCost(const Spell &spell) const {
    ForcePowerPayer payer;
    payer.playerCharacter = _isPC;
    payer.goodEvil = _goodEvil;
    payer.charismaModifier = _attributes.getAbilityModifier(Ability::Charisma);
    payer.waiveOppositeSurcharge = prestige::waivesOppositeAlignmentSurcharge(
        _services.resource.k2Merge != nullptr, _goodEvil);
    payer.formMultiplier = forms::forcePointCostMultiplier(_activeForm);
    std::shared_ptr<TwoDA> forceAdjust;
    if (spell.forcePoints > 0 && _isPC && (spell.goodEvil == 'G' || spell.goodEvil == 'E')) {
        forceAdjust = _services.resource.twoDas.get("forceadjust");
    }
    int cost = computeForcePowerCost(spell, payer, forceAdjust.get());
    // Force Body (K2): the force point part of the cost; the same part is
    // paid in hit points (forceBodyHitPointCost).
    return forceBodyShare(cost);
}

int Creature::forceBodyShare(int cost) const {
    static constexpr int kForceBodyPercent[] {50, 40, 30};
    int level = getForceBodyLevel();
    return level < 0 ? cost : cost * kForceBodyPercent[level] / 100;
}

bool Creature::canAffordForcePower(const Spell &spell) const {
    return forcePowerCost(spell) <= _currentForce;
}

void Creature::spendForcePoints(const Spell &spell) {
    int cost = forcePowerCost(spell);
    drainForcePoints(cost);
    // Force Body pays the same share in hit points; Body Fuel costs hit
    // points equal to the force points paid. Neither kills: the creature
    // keeps at least 1 hit point.
    int hitPointCost = getForceBodyLevel() >= 0 ? cost : 0;
    if (hasBodyFuel()) {
        hitPointCost += cost;
    }
    if (hitPointCost > 0 && !_dead) {
        _currentHitPoints = static_cast<int16_t>(std::max(1, _currentHitPoints - hitPointCost));
    }
}

bool Creature::addMultiClass(ClassType classType) {
    std::shared_ptr<CreatureClass> clazz;
    try {
        clazz = _services.game.classes.get(classType);
    } catch (const std::exception &) {
        // A row classes.2da does not define.
        return false;
    }
    if (!clazz || !_attributes.addMultiClass(clazz.get())) {
        return false;
    }
    // The class arrives at level 0: nothing is gained until the level-up,
    // which always takes the latest class slot.
    recalculateForcePoints();
    int threshold = getNeededXP();
    if (_xp < threshold) {
        setXP(threshold);
    }
    return true;
}

void Creature::adjustGoodEvil(int delta) {
    _goodEvil = static_cast<uint8_t>(std::clamp(static_cast<int>(_goodEvil) + delta, 0, 100));
}

static int damageTypeIndex(int damageTypeBit) {
    for (int i = 0; i < 15; ++i) {
        if ((1 << i) == damageTypeBit) {
            return i;
        }
    }
    return -1;
}

int Creature::lastDamageOfType(int damageTypeBit) const {
    int index = damageTypeIndex(damageTypeBit);
    return index >= 0 ? _lastDamageByType[static_cast<size_t>(index)] : 0;
}

void Creature::recordTypedDamage(int amount, int damageFlags, const std::shared_ptr<Object> &damager) {
    uint64_t tick = _game.tickCount();
    uint32_t damagerId = damager ? damager->id() : script::kObjectInvalid;
    if (!_lastDamageEventOpen || tick != _lastDamageEventTick || damagerId != _lastDamageEventDamager) {
        _lastDamageByType.fill(0);
        _lastDamageEventOpen = true;
        _lastDamageEventTick = tick;
        _lastDamageEventDamager = damagerId;
    }
    int index = damageTypeIndex(damageFlags);
    if (index >= 0 && amount > 0) {
        _lastDamageByType[static_cast<size_t>(index)] += amount;
    }
}

uint32_t Creature::lastKiller() const {
    auto killer = _lastKiller.resolve();
    return killer ? killer->id() : script::kObjectInvalid;
}

void Creature::damage(
    int amount,
    const std::shared_ptr<Object> &damager) {
    if (_dead) {
        return;
    }

    if (amount < 0) {
        // Heal instead of damage.
        int previousHitPoints = _currentHitPoints;
        // Healing restores real hit points only: up to the maximum plus the
        // temporary pool, and never lowers the current value.
        _currentHitPoints = static_cast<int16_t>(std::max(
            static_cast<int>(_currentHitPoints),
            std::min(maxHitPoints() + _temporaryHitPoints, _currentHitPoints - amount)));
        _game.floatingText().addHeal(*this, _currentHitPoints - previousHitPoints);
        return;
    }

    bool deathEffect = amount == std::numeric_limits<int>::max();
    int previousHitPoints = _currentHitPoints;
    setLastDamager(damager);
    uint32_t damagerId = getLastDamager();
    _lastDamageTotal = deathEffect ? previousHitPoints : amount;
    if (deathEffect) {
        _currentHitPoints = 0; // special case for Death effect
    } else {
        int adjustedAmount = applyDamageToHitPoints(amount, _currentHitPoints);
        if (amount > 0) {
            _game.floatingText().addDamage(
                *this, amount, adjustedAmount, damagerId);
        }
    }

    // Taking damage excites the party: battle music (excitedduration.2da
    // row 1). Area::exciteCombat keeps only party members near an enemy.
    if (auto module = _game.module(); module && module->area()) {
        auto &area = *module->area();
        area.exciteCombat(*this, 1);
        if (auto attacker = damager ? dyn_cast<Creature>(damager) : nullptr) {
            area.exciteCombat(*attacker, 1);
        }
    }

    runDamagedScript();

    if (_immortal || _currentHitPoints > 0) {
        if (amount > 0 && !deathEffect && damager && dyn_cast<Creature>(damager) && _currentHitPoints > 0) {
            bark(cosmeticRandomInt(0, 1) == 0 ? SoundSetEntry::PainGrunt1 : SoundSetEntry::PainGrunt2, 20);
        }
        return;
    }

    _dead = true;
    // Blades retract on death, as in retail, whatever held them lit: combat
    // stays active until the round ends, and a script override no longer
    // applies to a corpse.
    _lightsaberIdlePowerDownPending = false;
    _lightsaberPowerOverride.reset();
    setLightsabersPowered(false, true);
    // Death takes every effect but the equipped, innate and removefxondeath ones.
    removeEffectsOnDeath(_services.resource.twoDas, *this);
    // GetLastKiller: recorded with the death, kept after the death script.
    _lastKiller = damager;
    if (auto *killer = damager ? dyn_cast<Creature>(damager.get()) : nullptr) {
        killer->addFuryKill();
    }
    awardKillExperience();
    _name = _services.resource.strings.getText(kStrRefRemains);
    // A creature that dies mid-walk would otherwise hold its path slot.
    clearPath();
    setMovementType(MovementType::None);

    debug(str(boost::format("Creature %s is dead") % _tag));

    playSound(SoundSetEntry::Dead);
    playAnimation(getDieAnimation());
    runDeathScript();
    notifyEncounterGone(true);

    // A party member's death is also the module's player-death event.
    if (_game.party().isMember(*this)) {
        if (auto module = _game.module()) {
            module->raisePlayerDeath(*this);
        }
    }
}

uint32_t Creature::encounterId() const {
    if (_encounterId != script::kObjectInvalid) {
        return _encounterId;
    }
    // Loaded from a save: the encounter link travels in ReoneEncounter.
    auto saved = savedReference("ReoneEncounter");
    return saved && saved->type() == ObjectType::Encounter ? saved->id() : script::kObjectInvalid;
}

void Creature::notifyEncounterGone(bool died) {
    uint32_t encounterObject = encounterId();
    if (_encounterNotified || encounterObject == script::kObjectInvalid) {
        return;
    }
    _encounterNotified = true;
    if (auto encounter = _game.getObjectById<Encounter>(encounterObject)) {
        encounter->onSpawnedCreatureGone(died);
    }
}

void Creature::updateCombat(float dt) {
    _combatState.deactivationTimer.update(dt);
    _lightsaberIdlePowerDownTimer.update(dt);
    if (_lightsaberIdlePowerDownPending &&
        !_combatState.active &&
        _lightsaberIdlePowerDownTimer.elapsed()) {
        _lightsaberIdlePowerDownPending = false;
        setLightsabersPowered(false, true);
    }
    if (_combatState.shouldDeactivate && _combatState.deactivationTimer.elapsed()) {
        _combatState.active = false;
        _combatState.shouldDeactivate = false;
        _combatState.debilitated = false;
        _combatState.attackTarget.reset();
        _combatState.attemptedAttackTarget.reset();
        _combatState.attemptedSpellTarget.reset();
        _combatState.attackAction = ActionType::QueueEmpty;
        _combatState.combatFeat = FeatType::Invalid;
        _lastHostileTarget.reset();
        _lastAttackAction = ActionType::QueueEmpty;
        _lastCombatFeat = FeatType::Invalid;
        _lastForcePower = -1;
        _lastForcePowerSuccess = false;
        setLastHostileActor(script::kObjectInvalid);

        _animDirty = true;
        setLightsabersPowered(false, true);
    }
}

void Creature::clearAllActions(bool force) {
    Object::clearAllActions(force);
    setMovementType(MovementType::None);
    // The area's path pool is fixed-size; a path abandoned here would never
    // be released and would eventually starve every creature of paths.
    clearPath();
}

void Creature::playAnimation(AnimationType type, AnimationProperties properties) {
    // If animation is looping by type and duration is -1.0, set flags accordingly
    bool looping = isAnimationLooping(type) && properties.duration == -1.0f;
    if (looping) {
        properties.flags |= AnimationFlags::loop;
    }

    std::string animName(getAnimationName(type));
    if (animName.empty())
        return;

    playAnimation(animName, std::move(properties));
}

void Creature::playAnimation(const std::string &name, AnimationProperties properties) {
    // A knocked-down or held creature keeps its pose: no combat reactions or
    // other clips over it. Dying still plays.
    if (!_dead && isHeldInPose()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    auto animation = model ? model->model().getAnimation(name) : nullptr;
    if (animation) {
        playAnimation(animation, std::move(properties));
    } else if (model) {
        // Unknown clip: only the scene node's missing-animation report runs,
        // so a pending state refresh and the current pose stay intact.
        model->playAnimation(name, nullptr, properties);
    }
}

bool Creature::doPlayAnimation(bool fireForget, const std::function<void()> &callback) {
    if (!_sceneNode || _movementType != MovementType::None) {
        return false;
    }

    callback();

    // An accepted explicit request owns the channel until a later state
    // change or its own fire-and-forget completion. Do not let an older
    // pending idle refresh (or an older clip's completion) replace it.
    _animDirty = false;
    _animFireForget = fireForget;
    return true;
}

bool Creature::playAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties) {
    if (!anim) {
        return false;
    }
    if (!_dead && isHeldInPose()) {
        return false;
    }
    bool fireForget = !(properties.flags & AnimationFlags::loop);

    return doPlayAnimation(fireForget, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(*anim, nullptr, properties);
        }
    });
}

bool Creature::playExternalAnimation(const std::shared_ptr<Animation> &anim, AnimationProperties properties) {
    properties.flags |= AnimationFlags::retargetRoot;
    bool started = doPlayAnimation(false, [&]() {
        auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
        if (model) {
            model->playAnimation(*anim, nullptr, properties);
        }
    });
    if (started) {
        // Dialogue owns the external clip until it explicitly releases the
        // participant. A pending state-driven refresh must not install an idle
        // or locomotion channel before the first stunt render update.
        _animDirty = false;
    }
    return started;
}

void Creature::playOverlayAnimation(AnimationType type) {
    std::string animName(getAnimationName(type));
    if (animName.empty()) {
        return;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    // Deliberately not routed through doPlayAnimation: that refuses to play
    // anything while the creature is moving, which is the one case an overlay
    // exists to cover. _animFireForget is left alone too, so
    // updateModelAnimation goes on driving locomotion underneath the layer,
    // and the layer erases itself once it has finished.
    model->playAnimation(
        animName,
        nullptr,
        AnimationProperties::fromFlags(
            AnimationFlags::overlay |
            AnimationFlags::layer |
            AnimationFlags::fireForget |
            AnimationFlags::propagate));
}

void Creature::resumeStateDrivenAnimation() {
    _animFireForget = false;
    _animDirty = true;
    updateModelAnimation();
}

void Creature::playAnimation(CombatAnimation anim, CreatureWieldType wield, int variant) {
    std::string animName(getAnimationName(anim, wield, variant));
    if (!animName.empty()) {
        playAnimation(animName, AnimationProperties::fromFlags(AnimationFlags::blend));
    }
}

bool Creature::equip(const std::string &resRef) {
    std::shared_ptr<Item> item;
    if (isPresentationOnly()) {
        item = _game.newPresentationItem();
        item->loadFromBlueprint(resRef);
    } else {
        item = _game.newItemFromBlueprint(resRef);
    }

    bool equipped = false;

    if (item->isEquippable(InventorySlots::body)) {
        equipped = equip(InventorySlots::body, item);
    } else if (item->isEquippable(InventorySlots::rightWeapon)) {
        equipped = equip(InventorySlots::rightWeapon, item);
    }

    if (!equipped && item->isRuntimeLive()) {
        _game.destroyRuntimeObjectGraph(item);
    }

    return equipped;
}

void Creature::updateDisguise() {
    int disguiseAppearance = -1;
    // A Disguise effect (the last applied) wins over a disguise item.
    forEachLiveEffect<DisguiseEffect>(effects(), [&](const DisguiseEffect &effect) {
        disguiseAppearance = effect.appearance();
    });
    for (auto &[slot, item] : _equipment) {
        if (disguiseAppearance >= 0) {
            break;
        }
        if (item->hasDisguise()) {
            disguiseAppearance = item->disguiseAppearance();
            break;
        }
    }
    if (disguiseAppearance >= 0) {
        if (!_disguised) {
            _appearanceBeforeDisguise = _appearance;
            _disguised = true;
        }
        _appearance = static_cast<uint32_t>(disguiseAppearance);
    } else if (_disguised) {
        _appearance = _appearanceBeforeDisguise;
        _disguised = false;
    }
}

bool Creature::canEquip(
    int slot,
    const std::shared_ptr<Item> &item) const {
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        !item->isEquippable(getEquipabilitySlot(slot))) {
        return false;
    }
    if (std::find(_items.begin(), _items.end(), item) != _items.end()) {
        // Inventory -> equipment is a transfer. Callers must first remove or
        // split the candidate so one object never has two ownership edges.
        return false;
    }
    for (const auto &[equippedSlot, equipped] : _equipment) {
        if (equipped == item && equippedSlot != slot) {
            return false;
        }
    }
    auto previous = getEquippedItem(slot);
    // SetItemNonEquippable leaves a worn item on, but it cannot go back on.
    if (previous != item && item->isNonEquippable()) {
        return false;
    }
    if (previous != item && item->owner() != 0 &&
        item->owner() != script::kObjectInvalid) {
        return false;
    }
    if (previous && previous != item) return false;
    return true;
}

bool Creature::equip(int slot, const std::shared_ptr<Item> &item) {
    if (!canEquip(slot, item) || isActiveAreaOwnedItem(_game, item)) {
        return false;
    }
    if (getEquippedItem(slot) == item) {
        return true;
    }
    auto replacementEffects = effectsWithoutEquippedSource(nullptr);
    appendEquippedItemEffects(replacementEffects, slot, item);

    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);
    replaceEffectState(std::move(replacementEffects));
    if (auto module = _game.module()) {
        module->setLastItemEquipped(item->id());
    }

    updateEquipmentPresentation();
    if (_sceneNode && _combatState.active &&
        (slot == InventorySlots::rightWeapon ||
         slot == InventorySlots::leftWeapon)) {
        setLightsabersPowered(true, true);
    } else if (_sceneNode && _lightsaberPowerOverride &&
               (slot == InventorySlots::rightWeapon ||
                slot == InventorySlots::leftWeapon)) {
        // SetLightsaberPowered can run before the saber reaches the hand
        // (k_pend_cut23 queues the equip and powers it at once).
        setLightsabersPowered(*_lightsaberPowerOverride, false);
    }

    return true;
}

bool Creature::canReplaceEquipment(
    int slot,
    const std::shared_ptr<Item> &item,
    const Object &displacedReceiver) const {
    auto previous = getEquippedItem(slot);
    if (!previous || previous == item) return canEquip(slot, item);
    if (!item || (!item->isRuntimeLive() && !item->isPresentationOnly()) ||
        !item->isEquippable(getEquipabilitySlot(slot)) ||
        std::find(_items.begin(), _items.end(), item) != _items.end() ||
        (item->owner() != 0 && item->owner() != script::kObjectInvalid)) {
        return false;
    }
    for (const auto &[equippedSlot, equipped] : _equipment) {
        if (equipped == item && equippedSlot != slot) return false;
    }
    if ((!previous->isRuntimeLive() && !previous->isPresentationOnly()) ||
        (!displacedReceiver.isRuntimeLive() &&
         !displacedReceiver.isPresentationOnly()) ||
        (isPresentationOnly() != displacedReceiver.isPresentationOnly())) {
        return false;
    }
    return true;
}

bool Creature::replaceEquipment(
    int slot,
    const std::shared_ptr<Item> &item,
    Object &displacedReceiver) {
    auto previous = getEquippedItem(slot);
    if (!previous || previous == item) return equip(slot, item);
    if (!canReplaceEquipment(slot, item, displacedReceiver) ||
        isActiveAreaOwnedItem(_game, item)) {
        return false;
    }
    auto replacementEffects = effectsWithoutEquippedSource(previous.get());
    appendEquippedItemEffects(replacementEffects, slot, item);

    // All ordinary recoverable validation is complete. The remainder is one
    // synchronous ownership move; only exceptional allocation failure remains.
    previous->powerDown(_position);
    previous->setEquipped(false);
    previous->setOwner(0);
    _equipment[slot] = item;
    item->setEquipped(true);
    item->setOwner(_id);
    displacedReceiver.addItem(previous);
    replaceEffectState(std::move(replacementEffects));
    updateEquipmentPresentation();
    return true;
}

std::shared_ptr<Item> Creature::takeEquippedItem(
    const std::shared_ptr<Item> &item) {
    auto equipped = std::find_if(
        _equipment.begin(), _equipment.end(),
        [&item](const auto &entry) { return entry.second == item; });
    if (equipped == _equipment.end()) return nullptr;

    auto result = equipped->second;
    auto replacementEffects = effectsWithoutEquippedSource(result.get());
    result->powerDown(_position);
    result->setEquipped(false);
    result->setOwner(0);
    _equipment.erase(equipped);
    replaceEffectState(std::move(replacementEffects));
    updateEquipmentPresentation();
    return result;
}

bool Creature::moveEquippedItemTo(
    const std::shared_ptr<Item> &item,
    Object &receiver) {
    if (!item ||
        (!receiver.isRuntimeLive() && !receiver.isPresentationOnly()) ||
        (isPresentationOnly() != receiver.isPresentationOnly())) {
        return false;
    }
    auto taken = takeEquippedItem(item);
    if (!taken) return false;
    receiver.addItem(taken);
    return true;
}

void Creature::updateEquipmentPresentation() {
    uint32_t prevAppearance = _appearance;
    updateDisguise();
    if (_appearance != prevAppearance) {
        // Refresh appearance-derived state so the in-place model swap below picks
        // up the disguise (or restored) model; rebuilding the scene node here would
        // orphan it from the area scene graph.
        loadAppearanceProperties();
    }
    if (_sceneNode) updateModel();
}

std::shared_ptr<Item> Creature::getEquippedItem(int slot) const {
    auto equipped = _equipment.find(slot);
    return equipped != _equipment.end() ? equipped->second : nullptr;
}

bool Creature::swapWeaponSets() {
    static constexpr std::array<std::pair<int, int>, 2> kPairs {{
        {InventorySlots::rightWeapon, InventorySlots::rightWeapon2},
        {InventorySlots::leftWeapon, InventorySlots::leftWeapon2},
    }};
    std::set<const Object *> moved;
    auto replacementEquipment = _equipment;
    for (auto [active, stowed] : kPairs) {
        auto activeItem = getEquippedItem(active);
        auto stowedItem = getEquippedItem(stowed);
        replacementEquipment.erase(active);
        replacementEquipment.erase(stowed);
        if (activeItem) {
            moved.insert(activeItem.get());
            replacementEquipment[stowed] = activeItem;
        }
        if (stowedItem) {
            moved.insert(stowedItem.get());
            replacementEquipment[active] = stowedItem;
        }
    }
    if (moved.empty()) {
        return false;
    }

    // Item properties are live only in the active set, so every moved item's
    // equipped effects are rebuilt from its new slot.
    std::deque<EffectInstance> replacementEffects;
    for (const EffectInstance &effect : effects()) {
        auto creator = effect.boundCreator();
        if (effect.durationType() == DurationType::Equipped && creator &&
            moved.count(creator.get()) != 0) {
            continue;
        }
        replacementEffects.push_back(effect);
    }
    for (auto [active, stowed] : kPairs) {
        if (auto item = replacementEquipment.count(active) ? replacementEquipment.at(active) : nullptr) {
            appendEquippedItemEffects(replacementEffects, active, item);
        }
    }

    for (auto [active, stowed] : kPairs) {
        if (auto stowedNow = replacementEquipment.count(stowed) ? replacementEquipment.at(stowed) : nullptr) {
            stowedNow->powerDown(_position);
        }
    }
    _equipment = std::move(replacementEquipment);
    replaceEffectState(std::move(replacementEffects));
    updateEquipmentPresentation();
    if (_sceneNode && _combatState.active) {
        setLightsabersPowered(true, true);
    }
    return true;
}

bool Creature::isSlotEquipped(int slot) const {
    return _equipment.find(slot) != _equipment.end();
}

void Creature::setMovementType(MovementType type) {
    if (_movementType == type)
        return;

    _movementType = type;
    _animDirty = true;
    _animFireForget = false;
}

glm::vec3 Creature::getSelectablePosition() const {
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return _position;
    }
    if (_dead) {
        return model->getWorldCenterOfAABB();
    }
    auto headModel = static_cast<ModelSceneNode *>(model->getAttachment(g_headHookNode));
    if (headModel) {
        auto talkDummy = headModel->getNodeByName(g_talkDummyNode);
        return talkDummy ? talkDummy->origin() : headModel->getWorldCenterOfAABB();
    } else {
        auto talkDummy = model->getNodeByName(g_talkDummyNode);
        return talkDummy ? talkDummy->origin() : model->getWorldCenterOfAABB();
    }
}

float Creature::getAttackRange() const {
    float result = kDefaultAttackRange;

    std::shared_ptr<Item> item(getEquippedItem(InventorySlots::rightWeapon));
    if (item && item->attackRange() > kDefaultAttackRange) {
        result = item->attackRange();
    }

    return result;
}

bool Creature::isLevelUpPending() const {
    return _xp >= getNeededXP();
}

int Creature::getNeededXP() const {
    // exptable.2da, sized from the data (design note c11_data).
    return experienceForNextLevel(_services.resource.twoDas, _attributes.getAggregateLevel());
}

void Creature::runSpawnScript() {
    // Retail gates the creation script on CreatnScrptFird, so it fires at most
    // once per creature rather than once per area attachment. A party member
    // carried through an ordinary module transition is the same creature
    // object: the destination area takes it in without recreating it, and its
    // OnSpawn must not run a second time. Retail also latches the flag
    // regardless of whether a script was authored.
    if (_spawnScriptFired) {
        return;
    }
    _spawnScriptFired = true;
    if (!_onSpawn.empty()) {
        _game.scriptRunner().run(_onSpawn, _id);
    }
}

void Creature::runBlockedScript(uint32_t blockingDoorId) {
    if (_onBlocked.empty()) {
        return;
    }
    // The obstructing door travels with the run as an argument, so every
    // continuation and delayed action started from it goes on seeing the door
    // this event was raised for, whatever the creature has run into since.
    _game.scriptRunner().run(
        _onBlocked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::BlockingDoor, Variable::ofObject(blockingDoorId)}});
}

void Creature::cancelCombatNow() {
    // A swing already under way is locked and finishes (its rolls are made);
    // the cancel then holds until the queue is empty, see updateCombatHandover.
    clearAllActions();
    _cancelledActions.clear();
    for (const auto &action : _actions) {
        _cancelledActions.push_back(action);
    }
    _cancelCombatPending = !_actions.empty();
    _combatHeld = true;
    _engageIdle = 0.0f;
    _combatHandoverPending = false;
    deactivateCombat(0.0f);
    _combatState.attackTarget.reset();
    _combatState.attemptedAttackTarget.reset();
    _combatState.attemptedSpellTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    _combatState.deactivationTimer.reset(0.0f);
}

bool Creature::seesLiveEnemy() const {
    for (const auto &[id, reference] : _perception.seen) {
        auto object = reference.resolve();
        auto *other = object ? dyn_cast<Creature>(object.get()) : nullptr;
        if (other && !other->isDead() && _services.game.reputes.getIsEnemy(*this, *other)) {
            return true;
        }
    }
    return false;
}

void Creature::updateCombatHandover(float dt) {
    if (_cancelCombatPending) {
        // Drop what was queued at the cancel once its swing unlocks; actions
        // the player queued since are kept.
        bool waiting = false;
        for (const auto &weak : _cancelledActions) {
            auto action = weak.lock();
            auto it = action ? std::find(_actions.begin(), _actions.end(), action) : _actions.end();
            if (it == _actions.end()) {
                continue;
            }
            if (action->locked()) {
                waiting = true;
                continue;
            }
            action->cancel(action, *this);
            action->markCancelled();
            _actions.erase(it);
        }
        if (waiting) {
            return;
        }
        _cancelledActions.clear();
        _cancelCombatPending = false;
        deactivateCombat(0.0f);
        _combatState.attackTarget.reset();
        _combatState.attemptedAttackTarget.reset();
        _combatState.attackAction = ActionType::QueueEmpty;
    }
    // Cancel Combat's hold lasts until the player orders something again or
    // the fight is out of sight.
    if (_combatHeld && (hasUserActionsPending() || _dead || !seesLiveEnemy())) {
        _combatHeld = false;
    }
    // A player's hand: the host's leader, or a creature a co-op client
    // controls. Neither takes the party AI's orders.
    bool leading = _game.party().getLeader().get() == this;
    bool playerHand = leading || coopPlayerControlled(*this);
    // Auto-combat on a player's hand: with its queue empty and a fight on,
    // ask the AI for a round, at most once per round length.
    if (_autoCombat) {
        _autoCombatIdle += dt;
    }
    if (_autoCombat && !_combatHandoverPending && !_dead && _actions.empty() &&
        _autoCombatIdle >= kAutoCombatRetry && playerHand &&
        (isInCombat() || seesLiveEnemy())) {
        _combatHandoverPending = true;
    }
    // An AI companion idles the same way when its round ended with nothing
    // queued (tar_m09aa: Bastila stood 4 m behind the PC for a whole fight
    // after her first target died; her end-of-round script never ran again
    // and the heartbeat branch of k_ai_master does not start a round).
    if (!leading && (coopPlayerControlled(*this) || _game.party().isMember(*this))) {
        updateIdleEngage(dt);
    }
    if (!_combatHandoverPending) {
        return;
    }
    if (_dead) {
        _combatHandoverPending = false;
        return;
    }
    if (!_actions.empty()) {
        return;
    }
    if (playerHand && !_autoCombat) {
        // Under the player's hand: nothing to hand over.
        _combatHandoverPending = false;
        return;
    }
    _combatHandoverPending = false;
    _autoCombatIdle = 0.0f;
    if (playerHand) {
        runAutoCombatRound();
    } else {
        runEndRoundScript();
    }
}

// Co-op idle engage: a client's character joins a fight it stands in.
static constexpr float kIdleEngageDelay = 1.0f;
static constexpr float kIdleEngageRange = 15.0f;

void Creature::updateIdleEngage(float dt) {
    // Only a character with nothing to do: any order of its own (a walk, an
    // attack, a skill, a following step) is left to run.
    if (!_actions.empty() || _dead || isTemporarilyDead()) {
        _engageIdle = 0.0f;
        return;
    }
    _engageIdle += dt;
    if (_engageIdle < kIdleEngageDelay) {
        return;
    }
    _engageIdle = 0.0f;
    // Auto-combat decides its rounds itself; Cancel Combat holds it back;
    // solo mode and stealth keep the party's other characters still.
    if (_autoCombat || isCombatHeld() || _combatHandoverPending || _game.party().isSoloMode() ||
        hasEffect(EffectType::Invisibility) || hasEffect(EffectType::ImprovedInvisibility) ||
        _game.isConversationActive()) {
        return;
    }
    auto module = _game.module();
    auto area = module ? module->area() : nullptr;
    if (!area) {
        return;
    }
    auto *coop = _game.coop();
    auto ownSide = [&](const Object *object) {
        auto *creature = object ? dyn_cast<Creature>(object) : nullptr;
        return creature && !creature->isDead() &&
               (creature == this || _game.party().isMember(*creature) ||
                (coop && coop->isPlayerControlled(creature->id())));
    };
    // The party is fighting nearby: an enemy in sight is attacking one of
    // the players' characters, or one of the party is attacking it.
    std::set<uint32_t> partyTargets;
    for (const auto &member : _game.party().members()) {
        if (member.creature && !member.creature->isDead()) {
            if (auto target = member.creature->getAttackTarget()) {
                partyTargets.insert(target->id());
            }
        }
    }
    std::shared_ptr<Creature> nearest;
    float nearestDistance = kIdleEngageRange;
    bool fighting = false;
    for (const auto &[id, reference] : _perception.seen) {
        auto object = reference.resolve();
        auto enemy = object ? std::dynamic_pointer_cast<Creature>(object) : nullptr;
        if (!enemy || enemy->isDead() || enemy->isTemporarilyDead() ||
            !_services.game.reputes.getIsEnemy(*this, *enemy)) {
            continue;
        }
        float distance = getDistanceTo(*enemy);
        if (distance > kIdleEngageRange) {
            continue;
        }
        if (ownSide(enemy->getAttackTarget().get()) || partyTargets.count(enemy->id()) > 0) {
            fighting = true;
        }
        if (distance < nearestDistance) {
            nearest = enemy;
            nearestDistance = distance;
        }
    }
    if (!fighting || !nearest) {
        return;
    }
    debug("Idle " + tag() + " engages " + nearest->tag(), LogChannel::Combat);
    if (!coopPlayerControlled(*this)) {
        // A companion under the party AI: let its own round script choose
        // (talents, the target it prefers) and fall back on a plain attack.
        runEndRoundScript();
        if (!_actions.empty()) {
            return;
        }
    }
    addAction(_game.newAction<AttackObjectAction>(std::move(nearest)));
}

void Creature::runAutoCombatRound() {
    if (companion::usesBrain(_game, *this)) {
        companion::runCompanionBrainRound(_game, *this);
        return;
    }
    uint32_t previous = _game.autoCombatScriptCaller();
    _game.setAutoCombatScriptCaller(_id);
    runEndRoundScript();
    _game.setAutoCombatScriptCaller(previous);
}

void Creature::runEndRoundScript() {
    // Smarter companions (C13, opt-in): the brain decides the round instead
    // of the generic script, for AI-controlled party members only.
    if (companion::usesBrain(_game, *this)) {
        companion::runCompanionBrainRound(_game, *this);
        return;
    }
    if (!_onEndRound.empty()) {
        _game.scriptRunner().run(_onEndRound, _id);
    }
}

void Creature::runDialogueScript(uint32_t speakerId, int32_t listenNumber) {
    _game.scriptRunner().run(
        _onDialogue,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastSpeaker, Variable::ofObject(speakerId)},
         {script::ArgKind::ListenPatternNumber, Variable::ofInt(listenNumber)}});
}

void Creature::signalInventoryDisturbed(InventoryDisturbType type, uint32_t disturberId, uint32_t itemId) {
    PendingScriptEvent event;
    event.kind = PendingScriptEvent::Kind::Disturbed;
    event.disturbType = type;
    event.disturberId = disturberId;
    event.itemId = itemId;
    _pendingScriptEvents.push_back(event);
}

void Creature::signalEndDialogue() {
    PendingScriptEvent event;
    event.kind = PendingScriptEvent::Kind::EndDialogue;
    _pendingScriptEvents.push_back(event);
}

void Creature::runPendingScriptEvents() {
    if (!isRuntimeLive()) {
        _pendingScriptEvents.clear();
        return;
    }
    // A script run below may raise further events on this creature; those
    // wait for the next update, so one update never loops on its own events.
    auto events = std::move(_pendingScriptEvents);
    _pendingScriptEvents.clear();
    for (const auto &event : events) {
        switch (event.kind) {
        case PendingScriptEvent::Kind::Disturbed:
            runDisturbedScript(event.disturbType, event.disturberId, event.itemId);
            break;
        case PendingScriptEvent::Kind::EndDialogue:
            runEndDialogueScript();
            break;
        }
        if (!isRuntimeLive()) {
            return;
        }
    }
}

void Creature::runDisturbedScript(InventoryDisturbType type, uint32_t disturberId, uint32_t itemId) {
    if (_onDisturbed.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDisturbed,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastDisturbed, Variable::ofObject(disturberId)},
         {script::ArgKind::InventoryDisturbItem, Variable::ofObject(itemId)},
         {script::ArgKind::InventoryDisturbType, Variable::ofInt(static_cast<int>(type))}});
}

void Creature::finishRest() {
    if (_currentHitPoints < maxHitPoints()) {
        setCurrentHitPoints(maxHitPoints());
    }
    if (_currentForce < _maxForcePoints) {
        _currentForce = _maxForcePoints;
    }
    runRestedScript();
}

void Creature::runRestedScript() {
    if (_onRested.empty()) {
        return;
    }
    _game.scriptRunner().run(_onRested, _id);
}

void Creature::runEndDialogueScript() {
    if (_onEndDialogue.empty()) {
        return;
    }
    _game.scriptRunner().run(_onEndDialogue, _id);
}

void Creature::giveXP(int amount) {
    setXP(_xp + amount);
}

void Creature::setXP(int xp) {
    bool wasLevelUpPending = isLevelUpPending();
    _xp = xp;

    if (!wasLevelUpPending && isLevelUpPending()) {
        _game.notifyLevelUpPending(*this);
    }
}

void Creature::presentBladeContact() {
    auto model = std::dynamic_pointer_cast<scene::ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }
    auto hook = model->getNodeByName("lightsaberhook");
    if (!hook) {
        return;
    }
    hitvisuals::presentAt(_game, _services, hitvisuals::kSparksParry, hook->origin(), getFacing());
}

bool Creature::playFoleyEvent(const std::string &name) {
    // Presentation: swing, clash and parry events on the attack animations
    // pick a weaponsounds.2da cell; the body-fall event picks the
    // appearance's fall sound for the surface underfoot.
    auto &twoDas = _services.resource.twoDas;
    int variants = 0;
    std::string family(combatsounds::eventFamily(name, variants));
    if (!family.empty()) {
        int row = combatsounds::weaponSoundRow(twoDas, *this);
        std::string sound(combatsounds::weaponSound(twoDas, row, family, cosmeticRandomInt(0, variants - 1)));
        _game.playSoundCue(family, sound, _position + glm::vec3(0.0f, 0.0f, 1.0f));
        return true;
    }
    if (!boost::iequals(name, "snd_hitground")) {
        return false;
    }
    auto appearance = twoDas.get("appearance");
    auto sets = twoDas.get("appearancesndset");
    if (!appearance || !sets || _appearance < 0 || _appearance >= appearance->getRowCount()) {
        return true;
    }
    int soundSet = appearance->getInt(_appearance, "soundapptype", -1);
    if (soundSet < 0 || soundSet >= sets->getRowCount()) {
        return true;
    }
    std::string surfaceSound;
    if (_walkmeshMaterial != -1) {
        surfaceSound = _services.game.surfaces.getSurface(_walkmeshMaterial).sound;
    }
    _game.playSoundCue("fall", sets->getString(soundSet, combatsounds::fallColumn(surfaceSound)), _position);
    return true;
}

bool Creature::bark(SoundSetEntry entry, int othersPercent) {
    // Sound-set barks (K2 behaviour): the controlled leader always barks,
    // anyone else by chance; only within 30 m of the leader.
    auto leader = _game.party().getLeader();
    if (!leader) {
        return false;
    }
    if (leader.get() != this) {
        if (getSquareDistanceTo(*leader) > 900.0f) {
            return false;
        }
        if (othersPercent < 100 && cosmeticRandomInt(0, 99) >= othersPercent) {
            return false;
        }
    }
    playSound(entry);
    return true;
}

void Creature::playSound(SoundSetEntry entry, bool positional) {
    if (!_soundSet) {
        return;
    }
    auto maybeSound = _soundSet->find(entry);
    if (maybeSound == _soundSet->end()) {
        return;
    }
    // One bark per creature: a new one cuts off the last.
    if (_audioSourceVoice) {
        _audioSourceVoice->stop();
    }
    _game.recordSoundCue("bark", str(boost::format("%s:%d") % _tag % static_cast<int>(entry)));
    std::optional<glm::vec3> position;
    if (positional) {
        position = _position + glm::vec3 {0.0f, 0.0f, 1.7f};
    }
    _audioSourceVoice = _services.audio.mixer.play(
        maybeSound->second,
        AudioType::Sound,
        1.0f,
        false,
        std::move(position));
}

void Creature::runAttackedScript(uint32_t attackerId) {
    if (_onAttacked.empty() || coopPlayerControlled(*this)) {
        return;
    }
    _game.scriptRunner().run(
        _onAttacked,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(attackerId)}});
}

void Creature::runDamagedScript() {
    if (_onDamaged.empty() || coopPlayerControlled(*this)) {
        return;
    }
    _game.scriptRunner().run(
        _onDamaged,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

Faction Creature::reputationFaction() const {
    return _game.party().isMember(*this) ? Faction::Player : _faction;
}

void Creature::awardKillExperience() {
    // K2 behaviour (design note c11_data): a death outside the party that the player
    // faction is hostile to (reputation 10 or less) gives the party the
    // xptable award, read at the level of the party's experience.
    if (_game.party().isMember(*this)) {
        return;
    }
    int reputation = _services.game.reputes.getReputation(Faction::Player, _faction);
    if (reputation > 10) {
        return;
    }
    auto &twoDas = _services.resource.twoDas;
    int level = levelForExperience(twoDas, _game.party().xp());
    int amount = killExperience(twoDas, level, _challengeRating);
    if (amount > 0) {
        int awarded = _game.party().awardXP(amount, XPSource::Combat);
        // Floated over the body for the player to see (design note p2_ui).
        _game.floatingText().addExperience(*this, awarded);
    }
}

void Creature::runDeathScript() {
    if (_onDeath.empty()) {
        return;
    }
    _game.scriptRunner().run(
        _onDeath,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastAttacker, Variable::ofObject(getLastDamager())},
         {script::ArgKind::LastDamager, Variable::ofObject(getLastDamager())}});
}

CreatureWieldType Creature::getWieldType() const {
    auto rightWeapon = getEquippedItem(InventorySlots::rightWeapon);
    auto leftWeapon = getEquippedItem(InventorySlots::leftWeapon);

    if (rightWeapon && leftWeapon) {
        return (rightWeapon->weaponWield() == WeaponWield::BlasterPistol) ? CreatureWieldType::DualPistols : CreatureWieldType::DualSwords;
    } else if (rightWeapon) {
        switch (rightWeapon->weaponWield()) {
        case WeaponWield::SingleSword:
            return CreatureWieldType::SingleSword;
        case WeaponWield::DoubleBladedSword:
            return CreatureWieldType::DoubleBladedSword;
        case WeaponWield::BlasterPistol:
            return CreatureWieldType::BlasterPistol;
        case WeaponWield::BlasterRifle:
            return CreatureWieldType::BlasterRifle;
        case WeaponWield::HeavyWeapon:
            return CreatureWieldType::HeavyWeapon;
        case WeaponWield::StunBaton:
        default:
            return CreatureWieldType::StunBaton;
        }
    }

    if (hasEffectiveFeat(FeatType::ComplexUnarmedAnims)) {
        return CreatureWieldType::HandToHandComplex;
    }

    return CreatureWieldType::HandToHand;
}

void Creature::startTalking(const std::shared_ptr<LipAnimation> &animation) {
    if (!_talking || _lipAnimation != animation) {
        _lipAnimation = animation;
        _talking = true;
        _animDirty = true;
    }
}

void Creature::stopTalking() {
    if (_talking || _lipAnimation) {
        _lipAnimation.reset();
        _talking = false;
        _animDirty = true;
    }
}

void Creature::setObjectSeen(const std::shared_ptr<Object> &object, bool seen) {
    if (seen) {
        _perception.seen[object->id()] = object;
    } else {
        _perception.seen.erase(object->id());
    }
}

void Creature::setObjectHeard(const std::shared_ptr<Object> &object, bool heard) {
    if (heard) {
        _perception.heard[object->id()] = object;
    } else {
        _perception.heard.erase(object->id());
    }
}

void Creature::runOnNotice(const Object &object, bool heard, bool seen) {
    // Execute onNotice once to handle both "heard" and "seen" perception
    // checks. k_ai_master script checks them in sequence, and performs
    // differently when an object is just "heard" assuming that it is not
    // "seen".

    if (_onNotice.empty() || coopPlayerControlled(*this)) {
        return;
    }

    _game.scriptRunner().run(
        _onNotice,
        {{script::ArgKind::Caller, Variable::ofObject(_id)},
         {script::ArgKind::LastPerceived, Variable::ofObject(object.id())},
         {script::ArgKind::LastPerceptionHeard, Variable::ofInt(heard)},
         {script::ArgKind::LastPerceptionInaudible, Variable::ofInt(!heard)},
         {script::ArgKind::LastPerceptionSeen, Variable::ofInt(seen)},
         {script::ArgKind::LastPerceptionVanished, Variable::ofInt(!seen)}});
}

void Creature::activateCombat() {
    _lightsaberIdlePowerDownPending = false;
    if (_combatState.active) {
        _combatState.shouldDeactivate = false;
        return;
    }
    _combatState.active = true;
    _combatState.shouldDeactivate = false;
    _animDirty = true;
    setLightsabersPowered(true, true);
    // Battle cry: party members 75%, others 30%; one of BattleCry1-5.
    if (!_dead) {
        int chance = _game.party().isMember(*this) ? 75 : 30;
        if (cosmeticRandomInt(0, 99) < chance) {
            auto cry = static_cast<SoundSetEntry>(static_cast<int>(SoundSetEntry::BattleCry1) + cosmeticRandomInt(0, 4));
            bark(cry, 100);
        }
    }
}

void Creature::setLightsaberPowerOverride(bool override, bool powered, bool showTransition) {
    if (!override) {
        _lightsaberPowerOverride.reset();
        return;
    }
    _lightsaberPowerOverride = powered;
    _lightsaberIdlePowerDownPending = false;
    setLightsabersPowered(powered, showTransition);
}

void Creature::setLightsabersPowered(bool powered, bool animate) {
    // A script override holds the blades in its state against every
    // game-driven change (combat start/end, drawing, idle power-down).
    if (_lightsaberPowerOverride) {
        powered = *_lightsaberPowerOverride;
    }
    // A dead creature's blades stay off (a corpse can still be the target of
    // a round that is starting, which would otherwise light them again).
    if (_dead) {
        powered = false;
    }
    auto model = std::static_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model) {
        return;
    }

    const std::array<std::pair<int, std::string>, 2> weaponSlots {{
        {InventorySlots::rightWeapon, g_rightHandNode},
        {InventorySlots::leftWeapon, g_leftHandNode},
    }};
    for (auto [slot, attachment] : weaponSlots) {
        auto weapon = static_cast<ModelSceneNode *>(model->getAttachment(attachment));
        if (!weapon || weapon->model().classification() != MdlClassification::lightsaber) {
            continue;
        }

        const auto activeAnimation = weapon->activeAnimationName();
        if ((powered && (activeAnimation == "powerup" || activeAnimation == "powered")) ||
            (!powered && (activeAnimation == "powerdown" || activeAnimation == "off"))) {
            continue;
        }
        weapon->playAnimation(animate ? (powered ? "powerup" : "powerdown") : (powered ? "powered" : "off"));
        if (!animate) {
            continue;
        }
        auto item = getEquippedItem(slot);
        if (item) {
            powered ? item->powerUp(_position) : item->powerDown(_position);
        }
    }
}

void Creature::updateLightsaberSoundPositions() {
    for (int slot : {InventorySlots::rightWeapon, InventorySlots::leftWeapon}) {
        auto item = getEquippedItem(slot);
        if (item) {
            item->updatePoweredSoundPosition(_position);
        }
    }
}

void Creature::deactivateCombat(float delay) {
    if (delay <= 0.0f) {
        _lightsaberIdlePowerDownPending = false;
        _combatState.active = false;
        _combatState.shouldDeactivate = false;
        _combatState.debilitated = false;
        _animDirty = true;
        setLightsabersPowered(false, true);
        return;
    }
    if (!_combatState.active) {
        return;
    }
    _combatState.shouldDeactivate = true;
    _combatState.deactivationTimer.reset(delay);
}

bool Creature::isTwoWeaponFighting() const {
    return static_cast<bool>(getOffhandAttackWeapon());
}

std::shared_ptr<Item> Creature::getOffhandAttackWeapon() const {
    auto main = getEquippedItem(InventorySlots::rightWeapon);
    if (!main) {
        return nullptr;
    }

    int relativeSize = static_cast<int>(main->weaponSize()) -
                       static_cast<int>(_size);
    if (relativeSize > 0) {
        return main->weaponWield() == WeaponWield::DoubleBladedSword
                   ? main
                   : nullptr;
    }

    auto offhand = getEquippedItem(InventorySlots::leftWeapon);
    if (!offhand ||
        offhand->weaponType() == WeaponType::None ||
        offhand->weaponWield() == WeaponWield::None) {
        return nullptr;
    }
    return offhand;
}

void Creature::beginCombatAttack(std::shared_ptr<Object> target, FeatType feat) {
    _combatState.attackTarget = target;
    _combatState.attackAction = ActionType::AttackObject;
    _combatState.combatFeat = feat;
}

void Creature::setAttemptedAttackTarget(uint32_t target) {
    _combatState.attemptedAttackTarget = _game.getObjectById(target);
}

void Creature::setAttemptedSpellTarget(const std::shared_ptr<Object> &target) {
    _combatState.attemptedSpellTarget = target;
}

void Creature::recordForcePowerUsed(SpellType spell) {
    _lastForcePower = static_cast<int>(spell);
    _lastForcePowerSuccess = true;
    _lastAttackAction = ActionType::CastSpell;
}

void Creature::cancelCombat() {
    _combatState.active = false;
    _combatState.shouldDeactivate = false;
    _combatState.debilitated = false;
    _combatState.attackTarget.reset();
    _combatState.attemptedAttackTarget.reset();
    _combatState.attemptedSpellTarget.reset();
    _combatState.attackAction = ActionType::QueueEmpty;
    _combatState.combatFeat = FeatType::Invalid;
    _combatState.deactivationTimer.reset(0.0f);
    _lastHostileTarget.reset();
    _lastAttackAction = ActionType::QueueEmpty;
    _lastCombatFeat = FeatType::Invalid;
    _lastForcePower = -1;
    _lastForcePowerSuccess = false;
    _animDirty = true;
}

void Creature::finishCombatRound() {
    if (auto target = _combatState.attackTarget.resolve()) {
        _lastHostileTarget = target;
    } else {
        _lastHostileTarget.reset();
    }
    _lastAttackAction = _combatState.attackAction;
    if (_combatState.combatFeat != FeatType::Invalid) {
        _lastCombatFeat = _combatState.combatFeat;
    }
}

void Creature::adjustModifiedAttacks(int amount) {
    _modifiedAttacks = std::clamp(_modifiedAttacks + amount, 0, 2);
}

void Creature::onEffectsCleared() {
    _modifiedAttacks = 0;
    _assuredHit = false;
}

bool Creature::applyAssuredHit() {
    if (_assuredHit) {
        return false;
    }
    _assuredHit = true;
    return true;
}

Alignment Creature::alignment() const {
    if (_goodEvil <= 40) {
        return Alignment::DarkSide;
    }
    if (_goodEvil >= 60) {
        return Alignment::LightSide;
    }
    return Alignment::Neutral;
}

bool Creature::hasEffectImmunity(
    ImmunityType immunityType, const Creature *creator) const {
    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (applied.type() == EffectType::Immunity &&
            applied.integerParameter(0) == static_cast<int>(immunityType) &&
            applied.appliesVersus(creator)) {
            return true;
        }
    }
    return false;
}

int Creature::getAbilityEffectModifier(Ability ability) const {
    static constexpr int kMaximumAbilityEffectModifier = 30;
    EffectModifierReducer reducer;
    int subtype = static_cast<int>(ability);

    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (applied.integerParameter(0, -1) != subtype) {
            continue;
        }
        int amount = applied.integerParameter(1);
        switch (applied.type()) {
        case EffectType::AbilityIncrease:
            reducer.addIncrease(getEffectSourceKey(applied), subtype, amount);
            break;
        case EffectType::AbilityDecrease:
            reducer.addDecrease(getEffectSourceKey(applied), subtype, amount);
            break;
        default:
            break;
        }
    }
    return reducer.totalIncrease(kMaximumAbilityEffectModifier) -
           reducer.totalDecrease(kMaximumAbilityEffectModifier);
}

int Creature::getEffectiveAbilityScore(Ability ability) const {
    return std::max(
        3,
        _attributes.getAbilityScore(ability) +
            getAbilityEffectModifier(ability));
}

int Creature::getEffectiveAbilityModifier(Ability ability) const {
    int score = getEffectiveAbilityScore(ability);
    return score >= 10 ? (score - 10) / 2 : (score - 11) / 2;
}

bool Creature::hasEffectiveFeat(FeatType feat) const {
    if (_attributes.hasFeat(feat)) {
        return true;
    }
    return std::any_of(
        effects().begin(), effects().end(),
        [feat](const EffectInstance &effect) {
            return effect.hasLiveRuntimeSource() &&
                   effect.type() == EffectType::BonusFeat &&
                   effect.integerParameter(0, static_cast<int>(FeatType::Invalid)) ==
                       static_cast<int>(feat);
        });
}

void Creature::appendEquippedItemEffects(
    std::deque<EffectInstance> &effects,
    int slot,
    const std::shared_ptr<Item> &item) const {
    if (!item || !equippedItemPropertiesAreActive(slot)) {
        return;
    }

    auto append = [&](std::shared_ptr<Effect> effect) {
        if (!effect) {
            return;
        }
        effect->setSaveFacingCreator(item);
        EffectInstance instance = effect->saveFacingInstance();
        instance.effect = std::move(effect);
        instance.id = _game.allocateEffectId();
        instance.subType = static_cast<uint16_t>(
            (instance.subType & ~static_cast<uint16_t>(0x7)) |
            static_cast<uint16_t>(DurationType::Equipped));
        instance.creatorId = item->id();
        instance.exposed = 1;
        effects.push_back(std::move(instance));
    };

    for (const Item::PropertyEntry &property : item->properties()) {
        if (!item->isPropertyActive(property)) {
            continue;
        }
        switch (static_cast<ItemProperty>(property.propertyName)) {
        case ItemProperty::AbilityBonus: {
            int amount = getItemPropertyValue(
                _services, property, "value", 0);
            if (amount > 0) {
                append(_game.newEffect<AbilityIncreaseEffect>(
                    static_cast<Ability>(property.subtype), amount));
            }
            break;
        }
        case ItemProperty::DecreasedAbilityScore: {
            // The cost table (iprp_neg10cost) holds the decrease as a negative value.
            int amount = std::abs(getItemPropertyValue(
                _services, property, "value", 0));
            if (amount > 0) {
                append(_game.newEffect<AbilityDecreaseEffect>(
                    static_cast<Ability>(property.subtype), amount));
            }
            break;
        }
        case ItemProperty::BonusFeat:
            append(_game.newEffect<BonusFeatEffect>(
                static_cast<FeatType>(property.subtype)));
            break;
        case ItemProperty::Immunity:
            append(_game.newEffect<ImmunityEffect>(
                static_cast<ImmunityType>(property.subtype)));
            break;
        case ItemProperty::ImmunityDamageType: {
            int amount = getItemPropertyValue(
                _services, property, "value", 0);
            if (amount > 0) {
                append(_game.newEffect<DamageImmunityIncreaseEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount));
            }
            break;
        }
        case ItemProperty::DamageVulnerability: {
            if (plotFlag()) {
                break;
            }
            int amount = getCostTableValue(
                _services,
                kVulnerabilityCostTable,
                property.costValue,
                "value",
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageImmunityDecreaseEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount));
            }
            break;
        }
        case ItemProperty::DamageResistance: {
            int amount = getCostTableValue(
                _services,
                kResistanceCostTable,
                property.costValue,
                "amount",
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageResistanceEffect>(
                    getItemPropertyDamageType(_services, property.subtype),
                    amount,
                    0));
            }
            break;
        }
        case ItemProperty::DamageReduction: {
            int amount = getCostTableValue(
                _services,
                kReductionCostTable,
                property.costValue,
                "amount",
                0);
            if (amount > 0) {
                append(_game.newEffect<DamageReductionEffect>(
                    amount,
                    getDamageReductionPower(_services, property.subtype),
                    0));
            }
            break;
        }
        case ItemProperty::TrueSeeing:
            append(_game.newEffect<TrueSeeingEffect>());
            break;
        default:
            break;
        }
    }
}

std::deque<EffectInstance> Creature::effectsWithoutEquippedSource(
    const Item *source) const {
    std::deque<EffectInstance> result;
    for (const EffectInstance &effect : effects()) {
        auto creator = effect.boundCreator();
        if (source && effect.durationType() == DurationType::Equipped &&
            creator.get() == source) {
            continue;
        }
        result.push_back(effect);
    }
    return result;
}

void Creature::refreshEquippedItems() {
    replaceEffectState(rebuildEquippedItemEffects(_equipment));
    updateEquipmentPresentation();
}

std::deque<EffectInstance> Creature::rebuildEquippedItemEffects(
    const std::map<int, std::shared_ptr<Item>> &equipment) const {
    std::deque<EffectInstance> result;
    for (const EffectInstance &effect : effects()) {
        if (effect.durationType() != DurationType::Equipped) {
            result.push_back(effect);
        }
    }
    std::set<const Item *> seen;
    for (const auto &[slot, item] : equipment) {
        if (item && seen.insert(item.get()).second) {
            appendEquippedItemEffects(result, slot, item);
        }
    }
    return result;
}

AttackBonusBreakdown Creature::getAttackBonusBreakdown(
    const Creature *target,
    const Item *weapon,
    bool offHand) const {

    AttackBonusBreakdown result;

    int strengthModifier = getEffectiveAbilityModifier(Ability::Strength);
    int dexterityModifier = getEffectiveAbilityModifier(Ability::Dexterity);

    if (weapon && weapon->isRanged()) {
        result.dexterityModifier = dexterityModifier;
    } else if (weapon && dexterityModifier > strengthModifier) {
        bool finesse = !_game.isTSL() && weapon->isLightsaber();
        if (weapon->isLightsaber() &&
            hasEffectiveFeat(FeatType::FinesseLightsabers)) {
            finesse = true;
        }
        switch (weapon->weaponWield()) {
        case WeaponWield::StunBaton:
        case WeaponWield::SingleSword:
        case WeaponWield::DoubleBladedSword:
            finesse = finesse ||
                      hasEffectiveFeat(FeatType::FinesseMeleeWeapons);
            break;
        default:
            break;
        }

        if (finesse) {
            result.dexterityModifier = dexterityModifier;
        } else {
            result.strengthModifier = strengthModifier;
        }
    } else {
        result.strengthModifier = strengthModifier;
    }

    int modifierBonus = 0;
    int modifierPenalty = 0;
    EffectModifierReducer miscModifierReducer;

    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (!applied.appliesVersus(target)) {
            continue;
        }
        auto modifierType = static_cast<AttackBonus>(
            applied.integerParameter(1));
        switch (applied.type()) {
        case EffectType::AttackIncrease: {
            int bonus = applied.integerParameter(0);
            if (bonus > 0 &&
                attackModifierApplies(modifierType, weapon, offHand)) {
                if (modifierType == AttackBonus::Misc) {
                    miscModifierReducer.addIncrease(
                        getEffectSourceKey(applied),
                        static_cast<int>(AttackBonus::Misc),
                        bonus);
                } else {
                    modifierBonus += bonus;
                }
            }
            break;
        }
        case EffectType::AttackDecrease: {
            int penalty = applied.integerParameter(0);
            if (penalty > 0 &&
                attackModifierApplies(modifierType, weapon, offHand)) {
                if (modifierType == AttackBonus::Misc) {
                    miscModifierReducer.addDecrease(
                        getEffectSourceKey(applied),
                        static_cast<int>(AttackBonus::Misc),
                        penalty);
                } else {
                    modifierPenalty += penalty;
                }
            }
            break;
        }
        default:
            break;
        }
    }

    modifierBonus += miscModifierReducer.totalIncrease(20);
    modifierPenalty += miscModifierReducer.totalDecrease(20);
    if (isEntangled()) {
        modifierPenalty += 2; // EffectEntangle: -2 to all attacks
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToAttack(slot, *item, weapon, offHand)) {
            continue;
        }

        int itemBonus = 0;
        int itemPenalty = 0;
        for (const auto &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            int modifier = 0;
            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            switch (propertyType) {
            case ItemProperty::EnhancementBonus:
            case ItemProperty::EnhancementBonusVsAlignmentGroup:
            case ItemProperty::EnhancementBonusVsRacialGroup:
            case ItemProperty::AttackBonus:
            case ItemProperty::AttackBonusVsAlignmentGroup:
            case ItemProperty::AttackBonusVsRacialGroup: {
                if (!attackPropertyApplies(
                        propertyType,
                        property.subtype,
                        target)) {
                    continue;
                }
                int value = getCostTableValue(
                    _services,
                    kMeleeCostTable,
                    property.costValue,
                    "value",
                    0);
                if (value <= 0) {
                    continue;
                }
                modifier = value;
                break;
            }
            case ItemProperty::AttackPenalty:
            case ItemProperty::DecreasedAttackModifier: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
                    0);
                if (value >= 0) {
                    continue;
                }
                modifier = value;
                break;
            }
            default:
                continue;
            }

            if (isHandSpecificAttackModifierSlot(slot)) {
                addAttackModifier(modifier, modifierBonus, modifierPenalty);
            } else if (modifier > 0) {
                itemBonus = std::max(itemBonus, modifier);
            } else {
                itemPenalty = std::max(itemPenalty, -modifier);
            }
        }
        modifierBonus += itemBonus;
        modifierPenalty += itemPenalty;
    }

    result.effectBonus = std::min(modifierBonus, 20) -
                         std::min(modifierPenalty, 20);

    if (weapon &&
        weapon->weaponFocusFeat() != FeatType::Invalid &&
        hasEffectiveFeat(weapon->weaponFocusFeat())) {
        result.weaponFocusBonus = 1;
    }

    if (target) {
        getSituationalAttackBonuses(
            *this,
            *target,
            weapon,
            result.closeProximityRangedBonus,
            result.meleeOnRangedBonus);
    }

    int twoWeaponPenalty = getTwoWeaponAttackPenalty(
        weapon,
        offHand,
        &result.smallOffhandBonus);
    result.dualWieldPenalty = -twoWeaponPenalty - result.smallOffhandBonus;

    if (!offHand) {
        result.duelingBonus = getDuelingBonus();
        switch (result.duelingBonus) {
        case 3:
            result.duelingFeat = FeatType::MasterDueling;
            break;
        case 2:
            result.duelingFeat = FeatType::ImprovedDueling;
            break;
        case 1:
            result.duelingFeat = FeatType::Dueling;
            break;
        default:
            break;
        }
    }

    result.k2FeatBonus = getK2FeatAttackBonus(target, weapon, offHand);

    result.baseAttackBonus = _attributes.getAggregateAttackBonus();
    return result;
}

// K2 feats merged into K1's feat table (design note k2_merge, "Feat
// effects"): only creatures that own them get anything.
int Creature::getK2FeatAttackBonus(const Creature *target, const Item *weapon, bool offHand) const {
    auto hasFeat = [this](FeatType feat) { return hasEffectiveFeat(feat); };
    int result = 0;
    if (weapon && weapon->isRanged()) {
        result += k2feats::targetingAttackBonus(hasFeat);
        if (target && getSquareDistanceTo(*target) <= k2feats::kCloseCombatRange * k2feats::kCloseCombatRange) {
            result += k2feats::closeCombatAttackBonus(hasFeat);
        }
    }
    if (weapon && weapon->isLightsaber()) {
        result += k2feats::superiorLightsaberFocusBonus(hasFeat);
        // K2 lightsaber forms.
        bool targetSaber = target && target->wieldsLightsaber();
        result += forms::attackModifier(saberFormInEffect(), targetSaber);
    }
    if (weapon && isTwoWeaponFighting()) {
        result += k2feats::superiorTwoWeaponBonus(hasFeat, offHand);
    }
    if (target) {
        int dualStrike = k2feats::dualStrikeAttackBonus(hasFeat);
        if (dualStrike > 0 && _game.party().isMember(*this)) {
            for (const auto &member : _game.party().members()) {
                if (!member.creature || member.creature.get() == this) {
                    continue;
                }
                auto memberTarget = member.creature->getAttackTarget();
                if (memberTarget && memberTarget.get() == target) {
                    result += dualStrike;
                    break;
                }
            }
        }
    }
    return result;
}

int Creature::getAttackBonus(bool offHand) const {
    auto weapon = offHand
                      ? getOffhandAttackWeapon()
                      : getEquippedItem(InventorySlots::rightWeapon);
    return getAttackBonusBreakdown(nullptr, weapon.get(), offHand).total();
}

int Creature::getDefense(const Creature *attacker, int damageFlags) const {
    int dexterityModifier = getEffectiveAbilityModifier(Ability::Dexterity);
    auto armor = getEquippedItem(InventorySlots::body);
    int armorDefense = armor ? armor->baseDefense() : 0;

    if (isDebilitated()) {
        dexterityModifier = std::min(dexterityModifier, 0);
    } else if (armorDefense > 0 && armor->isBodyModel() && armor->maxDexterityBonus() >= 0) {
        // Only armour drawn as the body model with an armour class limits Dexterity.
        dexterityModifier = std::min(
            dexterityModifier,
            armor->maxDexterityBonus());
    }

    std::array<int, kACBonusTypeCount> modifierBonuses {};
    std::array<int, kACBonusTypeCount> modifierPenalties {};
    EffectModifierReducer dodgeEffects;

    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (!applied.appliesVersus(attacker)) {
            continue;
        }
        switch (applied.type()) {
        case EffectType::ACIncrease: {
            int effectDamageType = applied.integerParameter(5, kAllDamageTypeFlags);
            if (damageTypeMatches(effectDamageType, damageFlags)) {
                auto bonusType = static_cast<ACBonus>(applied.integerParameter(0));
                if (bonusType == ACBonus::Dodge) {
                    // The one AC type that adds up: but not one spell with itself.
                    dodgeEffects.addIncrease(getEffectSourceKey(applied), 0, applied.integerParameter(1));
                } else {
                    addDefenseModifier(applied.integerParameter(1), bonusType, modifierBonuses);
                }
            }
            break;
        }
        case EffectType::ACDecrease: {
            int effectDamageType = applied.integerParameter(5, kAllDamageTypeFlags);
            if (damageTypeMatches(effectDamageType, damageFlags)) {
                auto bonusType = static_cast<ACBonus>(applied.integerParameter(0));
                if (bonusType == ACBonus::Dodge) {
                    dodgeEffects.addDecrease(getEffectSourceKey(applied), 0, applied.integerParameter(1));
                } else {
                    addDefenseModifier(applied.integerParameter(1), bonusType, modifierPenalties);
                }
            }
            break;
        }
        default:
            break;
        }
    }
    {
        constexpr int kUncapped = std::numeric_limits<int>::max() / 2;
        auto dodge = static_cast<int>(ACBonus::Dodge);
        modifierBonuses[dodge] += dodgeEffects.totalIncrease(kUncapped);
        modifierPenalties[dodge] += dodgeEffects.totalDecrease(kUncapped);
    }
    if (isEntangled()) {
        // EffectEntangle: -4 AC.
        addDefenseModifier(4, ACBonus::Dodge, modifierPenalties);
    }

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemPropertiesAreActive(slot)) {
            continue;
        }

        for (const auto &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            switch (propertyType) {
            case ItemProperty::AcBonus:
            case ItemProperty::AcBonusVsAlignmentGroup:
            case ItemProperty::AcBonusVsDamageType:
            case ItemProperty::AcBonusVsRacialGroup: {
                if (!defensePropertyApplies(
                        propertyType,
                        property.subtype,
                        attacker,
                        damageFlags)) {
                    continue;
                }
                int value = getCostTableValue(
                    _services,
                    kBonusCostTable,
                    property.costValue,
                    "value",
                    0);
                addDefenseModifier(
                    value,
                    item->acBonusType(),
                    modifierBonuses);
                break;
            }
            case ItemProperty::DecreasedAc: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
                    0);
                addDefenseModifier(
                    -value,
                    static_cast<ACBonus>(property.subtype),
                    modifierPenalties);
                break;
            }
            default:
                break;
            }
        }
    }

    int defenseModifier = getDefenseModifier(
        modifierBonuses,
        modifierPenalties);
    if (attacker) {
        int dodgeModifier = std::min(
            modifierBonuses[static_cast<int>(ACBonus::Dodge)] -
                modifierPenalties[static_cast<int>(ACBonus::Dodge)],
            kMaximumDodgeBonus);
        bool invisible = attacker->isInvisibleTo(*this);
        bool seen = _perception.sees(attacker->id());
        if (invisible) {
            defenseModifier -= dodgeModifier;
            dexterityModifier = std::min(dexterityModifier, 0);
        } else if (!seen) {
            defenseModifier -= dodgeModifier;
            dexterityModifier = 0;
        }
    }

    int defense = 10 +
                  _attributes.getAggregateDefenseBonus() +
                  armorDefense +
                  _naturalAC +
                  dexterityModifier +
                  defenseModifier +
                  getDuelingBonus();

    // K2 lightsaber forms: the "current target" is the creature this one is
    // attacking (or trying to).
    if (int form = saberFormInEffect(); form != 0) {
        bool currentTarget = false;
        if (attacker) {
            auto target = getAttackTarget();
            currentTarget = (target && target.get() == attacker) ||
                            getAttemptedAttackTarget() == attacker->id();
        }
        defense += forms::defenseModifier(form, currentTarget);
    }

    return defense - (isDebilitated() ? 4 : 0);
}

int Creature::getDefense() const {
    return getDefense(nullptr, 0);
}

int Creature::getSavingThrow(int savingThrow, SavingThrowType savingThrowType) const {
    // Effects from one spell cast on this creature again do not add up; those
    // of different spells, items and scripts do.
    EffectModifierReducer effectModifiers;
    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        switch (applied.type()) {
        case EffectType::SavingThrowIncrease: {
            if (savingThrowModifierApplies(
                    applied.integerParameter(1),
                    static_cast<SavingThrowType>(applied.integerParameter(2)),
                    savingThrow,
                    savingThrowType)) {
                effectModifiers.addIncrease(
                    getEffectSourceKey(applied), applied.integerParameter(1), applied.integerParameter(0));
            }
            break;
        }
        case EffectType::SavingThrowDecrease: {
            if (savingThrowModifierApplies(
                    applied.integerParameter(1),
                    static_cast<SavingThrowType>(applied.integerParameter(2)),
                    savingThrow,
                    savingThrowType)) {
                effectModifiers.addDecrease(
                    getEffectSourceKey(applied), applied.integerParameter(1), applied.integerParameter(0));
            }
            break;
        }
        default:
            break;
        }
    }
    constexpr int kUncapped = std::numeric_limits<int>::max() / 2;
    int modifier = effectModifiers.totalIncrease(kUncapped) - effectModifiers.totalDecrease(kUncapped);

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemPropertiesAreActive(slot)) {
            continue;
        }

        int itemBonus = 0;
        int itemPenalty = 0;
        for (const auto &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            if (!savingThrowPropertyApplies(
                    propertyType,
                    property.subtype,
                    savingThrow,
                    savingThrowType)) {
                continue;
            }

            switch (propertyType) {
            case ItemProperty::ImprovedSavingThrow:
            case ItemProperty::ImprovedSavingThrowSpecific: {
                int value = getCostTableValue(
                    _services,
                    kBonusCostTable,
                    property.costValue,
                    "value",
                    0);
                itemBonus = std::max(itemBonus, value);
                break;
            }
            case ItemProperty::DecreasedSavingThrows:
            case ItemProperty::DecreasedSavingThrowsSpecific: {
                int value = getCostTableValue(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    "value",
                    0);
                itemPenalty = std::max(itemPenalty, -value);
                break;
            }
            default:
                break;
            }
        }
        modifier += itemBonus - itemPenalty;
    }
    modifier = std::min(modifier, kMaximumSavingThrowModifier);

    int conditioningBonus = 0;
    if (hasEffectiveFeat(FeatType::LightningReflexes)) {
        conditioningBonus = 3;
    } else if (hasEffectiveFeat(FeatType::IronWill)) {
        conditioningBonus = 2;
    } else if (hasEffectiveFeat(FeatType::GreatFortitude)) {
        conditioningBonus = 1;
    }

    // Conditioning feats apply to every save; each save adds its class base,
    // its governing ability modifier and the creature's own bonus.
    const SavingThrows &base = _attributes.getAggregateSavingThrows();
    int save = 0;
    switch (savingThrow) {
    case kReflexSavingThrow:
        save = base.reflex + getEffectiveAbilityModifier(Ability::Dexterity) + _refBonus;
        break;
    case kWillSavingThrow:
        save = base.will + getEffectiveAbilityModifier(Ability::Wisdom) + _willBonus;
        break;
    default:
        save = base.fortitude + getEffectiveAbilityModifier(Ability::Constitution) + _fortBonus;
        break;
    }
    // K2 systems merge: Force forms against Force powers, and our Grey feat.
    int k2Bonus = 0;
    if (savingThrowType == SavingThrowType::ForcePower) {
        k2Bonus += forms::forceSaveModifier(_activeForm, wieldsLightsaber());
    }
    if (auto *merge = _services.resource.k2Merge) {
        int grey = merge->ownRowIndex("feat", prestige::kGreyFeatLabel);
        k2Bonus += prestige::greySavingThrowBonus(grey >= 0 && hasEffectiveFeat(static_cast<FeatType>(grey)), _goodEvil);
    }
    return save + conditioningBonus + modifier + k2Bonus;
}

int Creature::getFortitudeSave(SavingThrowType savingThrowType) const {
    return getSavingThrow(kFortitudeSavingThrow, savingThrowType);
}

bool Creature::rollFortitudeSave(
    int difficultyClass,
    SavingThrowType savingThrowType) const {

    return rollSavingThrow(kFortitudeSavingThrow, difficultyClass, savingThrowType);
}

bool Creature::rollSavingThrow(
    int savingThrow,
    int difficultyClass,
    SavingThrowType savingThrowType) const {

    return randomInt(1, 20) + getSavingThrow(savingThrow, savingThrowType) >= difficultyClass;
}

bool Creature::rollSavingThrowVersus(
    int savingThrow,
    int difficultyClass,
    SavingThrowType savingThrowType,
    const Creature *versus) const {

    int modifier = 0;
    if (versus && savingThrowType == SavingThrowType::ForcePower) {
        modifier = forms::opponentForceSaveModifier(versus->activeForm());
    }
    return randomInt(1, 20) + getSavingThrow(savingThrow, savingThrowType) + modifier >= difficultyClass;
}

bool Creature::wieldsLightsaber() const {
    auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    return weapon && weapon->isLightsaber();
}

int Creature::saberFormInEffect() const {
    return forms::saberFormInEffect(_activeForm, wieldsLightsaber());
}

int Creature::getPhysicalDamageBonus(
    const Item *weapon,
    bool offHand) const {

    int strengthModifier = getEffectiveAbilityModifier(Ability::Strength);
    int abilityModifier = strengthModifier;

    if (weapon && weapon->isRanged()) {
        if (strengthModifier > 0) {
            int mighty = 0;
            for (const auto &property : weapon->properties()) {
                if (!weapon->isPropertyActive(property) ||
                    property.propertyName != static_cast<uint16_t>(ItemProperty::Mighty)) {
                    continue;
                }
                mighty = property.costValue;
                break;
            }
            abilityModifier = std::min(strengthModifier, mighty);
        }
    } else if (strengthModifier > 0) {
        if (offHand) {
            abilityModifier = strengthModifier / 2;
        } else if (weapon &&
                   weapon->weaponWield() != WeaponWield::DoubleBladedSword &&
                   static_cast<int>(weapon->weaponSize()) ==
                       static_cast<int>(_size) + 1) {
            abilityModifier = 3 * strengthModifier / 2;
        }
    }

    int specialization = 0;
    if (weapon &&
        weapon->weaponSpecializationFeat() != FeatType::Invalid &&
        hasEffectiveFeat(weapon->weaponSpecializationFeat())) {
        specialization = 2;
    }

    // K2 feats (Increase Combat/Melee Damage, Precise Shot).
    auto hasFeat = [this](FeatType feat) { return hasEffectiveFeat(feat); };
    bool ranged = weapon && weapon->isRanged();
    int k2Feats = k2feats::combatDamageBonus(hasFeat, ranged);
    if (ranged) {
        k2Feats += k2feats::preciseShotDamageBonus(hasFeat);
    }
    if (weapon && weapon->isLightsaber()) {
        k2Feats += forms::damageModifier(saberFormInEffect());
    }

    return abilityModifier + specialization + k2Feats;
}

int Creature::getMassiveCriticalDamage(
    const Item *weapon,
    bool criticalHit) const {

    if (!criticalHit) {
        return 0;
    }

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : getEquippedItem(InventorySlots::hands);
    const Item *sourceItem = weapon ? weapon : handItem.get();
    if (!sourceItem) {
        return 0;
    }

    for (const auto &property : sourceItem->properties()) {
        if (!sourceItem->isPropertyActive(property) ||
            property.propertyName != static_cast<uint16_t>(ItemProperty::MassiveCriticals)) {
            continue;
        }

        auto modifier = getDamageModifier(
            _services,
            property.costValue,
            weapon
                ? getPrimaryDamageType(sourceItem->damageFlags())
                : DamageType::Bludgeoning);
        return modifier
                   ? rollDamageModifier(*modifier, 1)
                   : 0;
    }

    return 0;
}

int Creature::getDamageResistanceFeatBonus() const {
    int result = 0;
    if (hasEffectiveFeat(FeatType::ImprovedToughness)) {
        result += 2;
    }
    if (hasEffectiveFeat(FeatType::WookieEndurance)) {
        result += 2;
    }
    return result;
}

void Creature::addPhysicalDamageModifiers(
    DamagePacket &damage,
    const Creature *target,
    const Item *weapon,
    bool offHand,
    int criticalMultiplier) const {

    std::vector<DamageModifier> itemBonuses;
    std::vector<DamageModifier> itemPenalties;

    auto handItem = weapon
                        ? std::shared_ptr<Item>()
                        : getEquippedItem(InventorySlots::hands);
    const Item *sourceItem = weapon ? weapon : handItem.get();

    for (const auto &[slot, item] : _equipment) {
        if (!item || !equippedItemAppliesToAttack(slot, *item, weapon, offHand)) {
            continue;
        }

        std::map<int, DamageModifier> bonuses;
        std::map<int, DamageModifier> penalties;

        for (const auto &property : item->properties()) {
            if (!item->isPropertyActive(property)) {
                continue;
            }

            auto propertyType = static_cast<ItemProperty>(property.propertyName);
            if (!damagePropertyApplies(
                    propertyType,
                    property.subtype,
                    target)) {
                continue;
            }

            switch (propertyType) {
            case ItemProperty::EnhancementBonus:
            case ItemProperty::EnhancementBonusVsAlignmentGroup:
            case ItemProperty::EnhancementBonusVsRacialGroup: {
                auto modifier = getFlatDamageModifier(
                    _services,
                    kMeleeCostTable,
                    property.costValue,
                    !weapon && item.get() == sourceItem
                        ? DamageType::Bludgeoning
                        : getPrimaryDamageType(item->damageFlags()));
                if (!modifier || modifier->flat <= 0) {
                    break;
                }
                // Damage bonuses of a type add up across an item's properties.
                itemBonuses.push_back(*modifier);
                damage.setPower(static_cast<DamagePower>(modifier->flat));
                break;
            }
            case ItemProperty::DamageBonus: {
                DamageType type = getItemPropertyDamageType(
                    _services,
                    property.subtype);
                auto modifier = getDamageModifier(
                    _services,
                    property.costValue,
                    type);
                if (modifier) {
                    // Damage bonuses of a type add up across an item's properties.
                itemBonuses.push_back(*modifier);
                }
                break;
            }
            case ItemProperty::DamageBonusVsAlignmentGroup:
            case ItemProperty::DamageBonusVsRacialGroup: {
                DamageType type = getItemPropertyDamageType(
                    _services,
                    property.paramValue);
                auto modifier = getDamageModifier(
                    _services,
                    property.costValue,
                    type);
                if (modifier) {
                    // Damage bonuses of a type add up across an item's properties.
                itemBonuses.push_back(*modifier);
                }
                break;
            }
            case ItemProperty::DecreasedDamage: {
                auto modifier = getFlatDamageModifier(
                    _services,
                    kDecreaseCostTable,
                    property.costValue,
                    !weapon && item.get() == sourceItem
                        ? DamageType::Bludgeoning
                        : getPrimaryDamageType(item->damageFlags()));
                if (!modifier) {
                    break;
                }
                selectDamageModifier(penalties, *modifier);
                break;
            }
            default:
                break;
            }
        }

        for (const auto &entry : bonuses) {
            itemBonuses.push_back(entry.second);
        }
        for (const auto &entry : penalties) {
            itemPenalties.push_back(entry.second);
        }
    }

    EffectModifierReducer effectModifierReducer;
    for (const EffectInstance *appliedPtr : flatEffects()) {
        const EffectInstance &applied = *appliedPtr;
        if (!applied.hasLiveRuntimeSource()) {
            continue;
        }
        if (!applied.appliesVersus(target)) {
            continue;
        }
        switch (applied.type()) {
        case EffectType::DamageIncrease: {
            int bonus = applied.integerParameter(0);
            if (bonus > 0) {
                int type = static_cast<int>(getPrimaryDamageType(
                    applied.integerParameter(1)));
                effectModifierReducer.addIncrease(
                    getEffectSourceKey(applied),
                    type,
                    criticalMultiplier * bonus);
            }
            break;
        }
        case EffectType::DamageDecrease: {
            int penalty = applied.integerParameter(0);
            if (penalty > 0) {
                int type = static_cast<int>(getPrimaryDamageType(
                    applied.integerParameter(1)));
                effectModifierReducer.addDecrease(
                    getEffectSourceKey(applied),
                    type,
                    criticalMultiplier * penalty);
            }
            break;
        }
        default:
            break;
        }
    }

    for (const auto &[type, amount] :
         effectModifierReducer.increasesBySubtype(kMaximumDamageEffectModifier)) {
        damage.add(amount, static_cast<DamageType>(type));
    }
    if (int fury = furyDamageBonus(); fury > 0) {
        damage.add(criticalMultiplier * fury, DamageType::Universal);
    }
    for (const DamageModifier &modifier : itemBonuses) {
        damage.add(
            rollDamageModifier(modifier, criticalMultiplier),
            modifier.type);
    }
    for (const auto &[type, amount] :
         effectModifierReducer.decreasesBySubtype(kMaximumDamageEffectModifier)) {
        damage.add(-amount, static_cast<DamageType>(type));
    }
    for (const DamageModifier &modifier : itemPenalties) {
        damage.add(
            rollDamageModifier(modifier, criticalMultiplier),
            modifier.type);
    }
}

void Creature::getMainHandDamage(int &min, int &max) const {
    auto weapon = getEquippedItem(InventorySlots::rightWeapon);
    getWeaponDamage(weapon.get(), min, max);
}

void Creature::getWeaponDamage(const Item *weapon, int &min, int &max) const {
    if (!weapon) {
        min = 1;
        max = 1;
    } else {
        min = weapon->numDice();
        max = weapon->numDice() * weapon->dieToRoll();
    }

    int modifier;
    if (weapon && weapon->isRanged()) {
        modifier = getEffectiveAbilityModifier(Ability::Dexterity);
    } else {
        modifier = getEffectiveAbilityModifier(Ability::Strength);
    }
    min += modifier;
    max += modifier;
}

void Creature::getOffhandDamage(int &min, int &max) const {
    auto weapon = getOffhandAttackWeapon();
    getWeaponDamage(weapon.get(), min, max);
}

void Creature::onEventSignalled(const std::string &name) {
    if (name == "draw_weapon") {
        setLightsabersPowered(true, true);
        if (!_combatState.active) {
            _lightsaberIdlePowerDownPending = true;
            _lightsaberIdlePowerDownTimer.reset(8.0f);
        }
        return;
    }
    if (boost::iequals(name, "contact")) {
        // Blades meet: parry sparks at the saber hook (P2 item 4).
        presentBladeContact();
        return;
    }
    if (playFoleyEvent(name)) {
        return;
    }
    if (_footstepType == -1 || _walkmeshMaterial == -1 || name != "snd_footstep") {
        return;
    }
    std::shared_ptr<FootstepTypeSounds> sounds(_services.game.footstepSounds.get(_footstepType));
    if (!sounds) {
        return;
    }
    const Surface &surface = _services.game.surfaces.getSurface(_walkmeshMaterial);
    std::vector<std::shared_ptr<AudioClip>> materialSounds;
    if (surface.sound == "DT") {
        materialSounds = sounds->dirt;
    } else if (surface.sound == "GR") {
        materialSounds = sounds->grass;
    } else if (surface.sound == "ST") {
        materialSounds = sounds->stone;
    } else if (surface.sound == "WD") {
        materialSounds = sounds->wood;
    } else if (surface.sound == "WT") {
        materialSounds = sounds->water;
    } else if (surface.sound == "CP") {
        materialSounds = sounds->carpet;
    } else if (surface.sound == "MT") {
        materialSounds = sounds->metal;
    } else if (surface.sound == "LV") {
        materialSounds = sounds->leaves;
    }
    int index = cosmeticRandomInt(0, 3);
    if (index >= static_cast<int>(materialSounds.size())) {
        return;
    }
    auto clip = materialSounds[index];
    if (clip) {
        _audioSourceFootstep = _services.audio.mixer.play(
            std::move(clip),
            AudioType::Sound,
            1.0f,
            false,
            _position);
    }
}

void Creature::giveGold(int amount) {
    _gold += amount;
}

void Creature::takeGold(int amount) {
    _gold -= amount;
}

// Steering progress below this per tick counts as no progress (1 cm).
static constexpr float kSteeringMinProgress2 = 0.0001f;
// How long a side-step lasts before the path is replanned.
static constexpr float kSideStepSeconds = 0.5f;
// Probe distances for picking the free side of a side-step.
static constexpr float kSideStepProbeNear = 0.75f;
static constexpr float kSideStepProbeFar = 1.5f;

glm::vec3 Creature::chooseSideStep(const Uniwalk &uni, const glm::vec3 &desired, const glm::vec3 &keepout) const {
    glm::vec2 forward(desired);
    if (glm::length2(forward) < 1e-8f) {
        // No direction to go around: use the facing.
        forward = glm::vec2(-portable::sinf(getFacing()), portable::cosf(getFacing()));
    }
    forward = glm::normalize(forward);
    const glm::vec2 left(-forward.y, forward.x);
    const glm::vec2 right(forward.y, -forward.x);

    auto area = _game.module() ? _game.module()->area() : nullptr;
    auto scoreSide = [&](const glm::vec2 &side) {
        float score = 0.0f;
        for (float probe : {kSideStepProbeNear, kSideStepProbeFar}) {
            glm::vec3 point(glm::vec2(_position) + side * probe, _position.z);
            if (uni.faces.empty() || isOnWalkmesh(uni, point)) {
                score += 2.0f;
            }
            if (area && !area->isPointClearOfCreatures(*this, point)) {
                score -= 1.5f;
            }
        }
        // Prefer the side the walls push towards.
        score += 0.25f * glm::clamp(glm::dot(glm::vec2(keepout), side), -1.0f, 1.0f);
        return score;
    };
    float leftScore = scoreSide(left);
    float rightScore = scoreSide(right);
    // K2 behaviour: the higher score wins, and a tie goes to the side this
    // creature remembers.
    bool goRight = rightScore == leftScore ? !_passLeft : rightScore > leftScore;
    glm::vec2 side = goRight ? right : left;
    return glm::vec3(side, 0.0f);
}

// Local avoidance looks this far ahead for creatures in the way.
static constexpr float kAvoidLookahead = 2.5f;
// Clearance kept around another creature, on top of both personal spaces.
static constexpr float kAvoidMargin = 0.2f;
static constexpr float kAvoidGain = 1.5f;

glm::vec3 Creature::computeCreatureAvoidance(const glm::vec3 &forward3) const {
    auto module = _game.module();
    if (!module || !module->area()) {
        return glm::vec3(0.0f);
    }
    glm::vec2 forward(forward3);
    if (glm::length2(forward) < 1e-8f) {
        return glm::vec3(0.0f);
    }
    forward = glm::normalize(forward);
    const glm::vec2 right(forward.y, -forward.x);
    const glm::vec2 origin(_position);
    const glm::vec2 destination(_pathDestination);
    auto &area = *module->area();

    std::vector<const Creature *> blockers;
    for (const auto &object : area.getObjectsByType(ObjectType::Creature)) {
        const auto *other = static_cast<const Creature *>(object.get());
        if (other == this || other->isDead() || area.ignoresCreatureCollision(*this, *other)) {
            continue;
        }
        float reach = kAvoidLookahead + _creaturePersonalSpace + other->creaturePersonalSpace() + kAvoidMargin;
        if (glm::distance2(origin, glm::vec2(other->position())) > reach * reach) {
            continue;
        }
        blockers.push_back(other);
    }
    std::sort(blockers.begin(), blockers.end(), [](const Creature *a, const Creature *b) {
        return a->id() < b->id();
    });

    glm::vec2 force(0.0f);
    for (const Creature *other : blockers) {
        glm::vec2 rel = glm::vec2(other->position()) - origin;
        float along = glm::dot(rel, forward);
        float combined = _creaturePersonalSpace + other->creaturePersonalSpace() + kAvoidMargin;
        if (along <= 0.0f || along > kAvoidLookahead + combined) {
            continue; // behind us or too far ahead
        }
        float lateral = glm::dot(rel, right); // positive: the other is on our right
        if (glm::abs(lateral) >= combined) {
            continue; // not in the way
        }
        if (glm::distance2(destination, glm::vec2(other->position())) < combined * combined) {
            continue; // standing at the destination, e.g. the creature we go to
        }
        if (other->movementType() != MovementType::None) {
            // Moving the same way: it is not an obstacle.
            float facing = other->getFacing();
            glm::vec2 heading(-portable::sinf(facing), portable::cosf(facing));
            if (glm::dot(heading, forward) > 0.5f) {
                continue;
            }
        }
        // Pass on the side away from it; straight ahead, keep right.
        glm::vec2 side = lateral > 1e-3f ? -right : right;
        float closeness = 1.0f - along / (kAvoidLookahead + combined);
        float overlap = 1.0f - glm::abs(lateral) / combined;
        force += side * (kAvoidGain * closeness * overlap);
    }
    return glm::vec3(force, 0.0f);
}

void Creature::registerBump() {
    // K2 behaviour: count a tick whose step something blocked.
    if (_stepBlockCreature == script::kObjectInvalid && !_stepBlockStatic) {
        return;
    }
    ++_bumpCount;
    if (_bumpCount > kMaxBumps) {
        _bumpGiveUp = true;
        return;
    }
    if (_stepBlockCreature == script::kObjectInvalid) {
        return;
    }
    auto blocker = _game.getObjectById(_stepBlockCreature);
    if (!blocker) {
        return;
    }
    // The same creature again, still where it was: no way round.
    constexpr float kSameSpot2 = 0.00001f;
    if (_stepBlockCreature == _lastBumpCreature &&
        glm::distance2(blocker->position(), _lastBumpCreaturePosition) <= kSameSpot2) {
        _bumpGiveUp = true;
        return;
    }
    _lastBumpCreature = _stepBlockCreature;
    _lastBumpCreaturePosition = blocker->position();
}

void Creature::rememberPassSide() {
    // K2 behaviour: after a detour the walker remembers the opposite of the
    // side it started from. The creature it bumped takes the walker's new
    // side when the two face each other, and the opposite side when they
    // face the same way.
    _passLeft = !_passLeft;
    if (_stepBlockCreature == script::kObjectInvalid) {
        return;
    }
    auto blocker = _game.getObjectById<Creature>(_stepBlockCreature);
    if (!blocker) {
        return;
    }
    float mine = getFacing();
    float theirs = blocker->getFacing();
    glm::vec2 myHeading(-portable::sinf(mine), portable::cosf(mine));
    glm::vec2 theirHeading(-portable::sinf(theirs), portable::cosf(theirs));
    bool sameWay = glm::dot(myHeading, theirHeading) >= 0.0f;
    blocker->setPassesOnLeft(sameWay ? !_passLeft : _passLeft);
}

glm::vec3 Creature::computeSteeringForce(const Uniwalk &uni, const glm::vec3 &next, float dt) {
    glm::vec3 desiredForce(glm::vec2(next - _position), 0.0f);
    if (glm::length2(desiredForce) > 1e-8f) {
        desiredForce = glm::normalize(desiredForce);
    } else {
        desiredForce = glm::vec3(0.0f);
    }
    glm::vec3 keepoutForce = computeKeepoutForce(uni, _position);
    keepoutForce.z = 0.0f;

    // No progress: side-step along the walkmesh towards the free side for a
    // moment, then replan from wherever that leaves us. Progress is measured
    // between consecutive steering ticks of the same path.
    bool noProgress = _steeringStarted &&
                      glm::distance2(glm::vec2(_position), glm::vec2(_previousPosition)) < kSteeringMinProgress2;
    if (_sideStepTime > 0.0f) {
        _sideStepTime -= dt;
        if (_sideStepTime <= 0.0f) {
            _sideStepTime = 0.0f;
            _replanPending = true;
        }
        desiredForce = glm::vec3(0.0f);
    } else if (noProgress) {
        registerBump();
        _stuckForce = chooseSideStep(uni, desiredForce, keepoutForce);
        rememberPassSide();
        _sideStepTime = kSideStepSeconds;
        ++_sideStepCount;
        // Turn at once: the smoothing below would otherwise spend most of the
        // side-step turning.
        _pathVelocity = _stuckForce * 0.3f;
        desiredForce = glm::vec3(0.0f);
    } else {
        _stuckForce = glm::vec3(0.0f);
    }
    _previousPosition = _position;
    _steeringStarted = true;

    glm::vec3 avoidForce = _sideStepTime > 0.0f ? glm::vec3(0.0f) : computeCreatureAvoidance(desiredForce);
    glm::vec3 combinedForce = desiredForce + 0.1f * keepoutForce + avoidForce + _stuckForce;

    drawdebug::pushId("computeSteeringForce");
    drawdebug::pushId(_id);
    drawdebug::clear();

    if (isShowPathEnabled()) {
        drawdebug::line(_position, _position + desiredForce, 0x00BFFFFF, 0.02);
        drawdebug::line(_position, _position + keepoutForce, 0xDDA0DDFF, 0.02);
        drawdebug::line(_position, _position + _stuckForce, 0xF08080FF, 0.02);
        drawdebug::line(_position, _position + combinedForce, 0xADFF2FFF, 0.02);
    }

    drawdebug::popId();
    drawdebug::popId();

    return combinedForce;
}

bool Creature::navigateTo(const glm::vec3 &dest, bool run, float distance, float dt) {
    if (_movementRestricted)
        return false;

    auto module = _game.module();
    if (!module || !module->area()) {
        // Navigation without a module does not make sense. This is only useful
        // for unit tests.
        return true;
    }

    Pathfinder &pf = module->area()->pathfinder();

    // A new destination starts a fresh count of failed path requests.
    constexpr float kDestinationChange2 = 1.0f;
    if (glm::distance2(glm::vec2(dest), glm::vec2(_pathRequestDestination)) > kDestinationChange2) {
        _pathRequestDestination = dest;
        _pathRequestFailures = 0;
        _sideStepCount = 0;
        _navBestDistance = FLT_MAX;
        _navNoProgressTime = 0.0f;
    }
    // K2 behaviour: the bump count starts again when the destination moves
    // 0.1 m or more (K2 replans then).
    constexpr float kBumpDestinationChange2 = 0.01f;
    if (glm::distance2(dest, _bumpDestination) >= kBumpDestinationChange2) {
        _bumpDestination = dest;
        _bumpCount = 0;
        _lastBumpCreature = script::kObjectInvalid;
        _bumpGiveUp = false;
    }
    if (_bumpGiveUp) {
        // Give up the walk, reported like repeated path failures.
        _bumpGiveUp = false;
        _bumpCount = 0;
        _lastBumpCreature = script::kObjectInvalid;
        _navNoProgressTime = 0.0f;
        _navBestDistance = FLT_MAX;
        setMovementType(Creature::MovementType::None);
        clearPath();
        _pathRequestFailures = kMaxPathRequestFailures;
        return false;
    }

    // Stop if we reached the destination.
    float distToDest2 = getSquareDistanceTo(glm::vec2(dest));
    if (distToDest2 <= distance * distance) {
        setMovementType(Creature::MovementType::None);
        clearPath();
        _pathRequestFailures = 0;
        _navBestDistance = FLT_MAX;
        _navNoProgressTime = 0.0f;
        return true;
    }

    // Give up after a while without progress: a route blocked by creatures
    // or by a door the mover cannot open, or steering that keeps failing.
    // Progress is getting 0.5 m shorter on the remaining route than it has
    // been so far, so a detour along the path counts and shuffling at a
    // blockage does not. This reports like repeated path failures, so actions
    // finish instead of waiting forever. The window then starts again, so a
    // caller that keeps trying (a forced move) gets another attempt.
    constexpr float kProgressStep = 0.5f;
    if (_path) {
        float remaining = getRemainingPathLength(pf, *_path, _position);
        if (remaining < _navBestDistance - kProgressStep) {
            _navBestDistance = remaining;
            _navNoProgressTime = 0.0f;
        }
    }
    _navNoProgressTime += dt;
    if (_navNoProgressTime >= kNavigationGiveUpSeconds) {
        _navNoProgressTime = 0.0f;
        _navBestDistance = FLT_MAX;
        setMovementType(Creature::MovementType::None);
        clearPath();
        _pathRequestFailures = kMaxPathRequestFailures;
        return false;
    }

    // A path planned for an earlier destination leads somewhere else, e.g.
    // after the action that requested it was cleared. Replan.
    if (_path && glm::distance2(glm::vec2(dest), glm::vec2(_pathDestination)) > kDestinationChange2) {
        clearPath();
    }

    float eps2 = std::min(0.5f * 0.5f, distance * distance);
    if (_path && getSquareDistanceTo(getLastPathPoint(pf, *_path)) < eps2) {
        // Reached the last point, but not reached the destination. Find another
        // path.
        releasePath(pf, *_path);
        _path = std::nullopt;
    }

    if (_path && _pathBarrierRevision != pf.barrierRevision) {
        // A door on the level opened, closed, locked or unlocked: the path
        // may cross a doorway that is now shut, or miss one that opened.
        _replanPending = true;
    }
    if (_path && _replanPending) {
        // A side-step has ended: plan again from where it left us.
        releasePath(pf, *_path);
        _path = std::nullopt;
    }
    _replanPending = false;

    if (_path && !updatePath(pf, *_path, position())) {
        // Lost the path and cannot recalculate.
        releasePath(pf, *_path);
        _path = std::nullopt;
        return false;
    }

    // Advance on path.
    if (_path) {
        glm::vec3 steeringForce = computeSteeringForce(pf.uni, getNextPathPoint(pf, *_path), dt);
        _pathVelocity += steeringForce * dt;

        float maxSpeed = 0.3f;
        float speed = glm::min(glm::length(_pathVelocity), maxSpeed);
        _pathVelocity = glm::normalize(_pathVelocity) * speed;

        glm::vec3 dir = glm::normalize(_pathVelocity);
        advanceOnPath(dest, dir, run, distance, dt);
        return false;
    }

    // Find a path and start following it.
    _path = createPath(pf, position(), dest);
    if (!_path) {
        // Either end is off the walkmesh, no route exists, or every path slot
        // is in use. Count it so callers can give up via navigationFailed().
        if (_pathRequestFailures < kMaxPathRequestFailures) {
            ++_pathRequestFailures;
        }
        return false;
    }
    _pathDestination = dest;
    _pathBarrierRevision = pf.barrierRevision;
    _pathRequestFailures = 0;
    if (_sideStepTime <= 0.0f) {
        _pathVelocity = {0.0f, 0.0f, 0.0f};
    }
    _steeringStarted = false;

    return navigateTo(dest, run, distance, dt);
}

void Creature::advanceOnPath(const glm::vec3 &dest, const glm::vec3 &dir, bool run, float distance, float dt) {
    setMovementType(run ? Creature::MovementType::Run : Creature::MovementType::Walk);
    _game.module()->area()->moveCreature(
        _game.getObjectById<Creature>(_id), dir, run, dt, getDistanceTo(dest));

    // Report a door that obstructed this step. A door can stop the creature
    // from making progress while the slide in moveCreature still produces
    // some sideways motion, so this is keyed on the recorded obstruction
    // rather than on whether the step moved the creature at all.
    dispatchBlockedEvent();
}

void Creature::clearPath() {
    _sideStepTime = 0.0f;
    _stuckForce = glm::vec3(0.0f);
    _replanPending = false;
    if (!_path) {
        return;
    }

    auto module = _game.module();
    if (module && module->area()) {
        releasePath(module->area()->pathfinder(), *_path);
    }
    _path = std::nullopt;
}

void Creature::dispatchBlockedEvent() {
    if (_blockingDoorId == script::kObjectInvalid) {
        // Nothing obstructed this step. Re-arm, so meeting the same door again
        // later reports again.
        _blockedEventDoorId = script::kObjectInvalid;
        return;
    }
    if (_blockedEventDoorId == _blockingDoorId) {
        // Still the same obstruction that was already reported.
        return;
    }
    _blockedEventDoorId = _blockingDoorId;
    runBlockedScript(_blockingDoorId);
}

std::string Creature::getAnimationName(AnimationType anim) const {
    std::string result;
    switch (anim) {
    case AnimationType::LoopingPause:
        return getPauseAnimation();
    case AnimationType::LoopingPause2:
        return getFirstIfCreatureModel("cpause2", "pause2");
    case AnimationType::LoopingListen:
        return "listen";
    case AnimationType::LoopingMeditate:
        return "meditate";
    case AnimationType::LoopingTalkNormal:
        return "tlknorm";
    case AnimationType::LoopingTalkPleading:
        return "tlkplead";
    case AnimationType::LoopingTalkForceful:
        return "tlkforce";
    case AnimationType::LoopingTalkLaughing:
        return getFirstIfCreatureModel("", "tlklaugh");
    case AnimationType::LoopingTalkSad:
        return "tlksad";
    case AnimationType::LoopingPauseTired:
        return "pausetrd";
    case AnimationType::LoopingFlirt:
        return "flirt";
    case AnimationType::LoopingUseComputer:
        return "usecomplp";
    case AnimationType::LoopingDance:
        return "dance";
    case AnimationType::LoopingDance1:
        return "dance1";
    case AnimationType::LoopingHorror:
        return "horror";
    case AnimationType::LoopingDeactivate:
        return getFirstIfCreatureModel("", "deactivate");
    case AnimationType::LoopingSpasm:
        return getFirstIfCreatureModel("cspasm", "spasm");
    case AnimationType::LoopingSleep:
        return "sleep";
    case AnimationType::LoopingProne:
        return "prone";
    case AnimationType::LoopingPause3:
        return getFirstIfCreatureModel("", "pause3");
    case AnimationType::LoopingWeld:
        return "weld";
    case AnimationType::LoopingDead:
        return getDeadAnimation();
    case AnimationType::LoopingDeadProne:
        return getFirstIfCreatureModel("cdead", "dead3");
    case AnimationType::LoopingTalkInjured:
        return "talkinj";
    case AnimationType::LoopingListenInjured:
        return "listeninj";
    case AnimationType::LoopingTreatInjured:
        return "treatinjlp";
    case AnimationType::LoopingUnlockDoor:
        return "unlockdr";
    case AnimationType::LoopingClosed:
        return "closed";
    case AnimationType::LoopingStealth:
        return "stealth";
    case AnimationType::FireForgetHeadTurnLeft:
        return getFirstIfCreatureModel("chturnl", "hturnl");
    case AnimationType::FireForgetHeadTurnRight:
        return getFirstIfCreatureModel("chturnr", "hturnr");
    case AnimationType::FireForgetSalute:
        return "salute";
    case AnimationType::FireForgetBow:
        return "bow";
    case AnimationType::FireForgetGreeting:
        return "greeting";
    case AnimationType::FireForgetTaunt:
        return getFirstIfCreatureModel("ctaunt", "taunt");
    case AnimationType::FireForgetDiveRoll:
        // Row 567 of K2 animations.2da, the one animation the shipped scripts
        // ever ask PlayOverlayAnimation for. K1 has neither the clip nor the
        // constant.
        return getFirstIfCreatureModel("cdiveroll", "diveroll");
    case AnimationType::FireForgetVictory1:
        return getFirstIfCreatureModel("cvictory", "victory");
    case AnimationType::FireForgetInject:
        return "inject";
    case AnimationType::FireForgetUseComputer:
        return "usecomp";
    case AnimationType::FireForgetPersuade:
        return "persuade";
    case AnimationType::FireForgetActivate:
        return "activate";
    case AnimationType::LoopingChoke:
        return "choke";
    case AnimationType::FireForgetTreatInjured:
        return "treatinj";
    case AnimationType::FireForgetOpen:
        return "open";
    case AnimationType::LoopingReady:
        return getAnimationName(CombatAnimation::Ready, getWieldType(), 0);

    case AnimationType::LoopingWorship:
    case AnimationType::LoopingGetLow:
    case AnimationType::LoopingGetMid:
    case AnimationType::LoopingPauseDrunk:
    case AnimationType::LoopingKneelTalkAngry:
    case AnimationType::LoopingKneelTalkSad:
    case AnimationType::LoopingCheckBody:
    case AnimationType::LoopingSitAndMeditate:
    case AnimationType::LoopingSitChair:
    case AnimationType::LoopingSitChairDrink:
    case AnimationType::LoopingSitChairPazak:
    case AnimationType::LoopingSitChairComp1:
    case AnimationType::LoopingSitChairComp2:
    case AnimationType::LoopingRage:
    case AnimationType::LoopingChokeWorking:
    case AnimationType::LoopingMeditateStand:
    case AnimationType::FireForgetPauseScratchHead:
    case AnimationType::FireForgetPauseBored:
    case AnimationType::FireForgetVictory2:
    case AnimationType::FireForgetVictory3:
    case AnimationType::FireForgetThrowHigh:
    case AnimationType::FireForgetThrowLow:
    case AnimationType::FireForgetCustom01:
    case AnimationType::FireForgetForceCast:
    case AnimationType::FireForgetScream:
    default:
        debug("CreatureAnimationResolver: unsupported animation type: " + std::to_string(static_cast<int>(anim)));
        return "";
    }
}

std::string Creature::getDieAnimation() const {
    return getFirstIfCreatureModel("cdie", "die");
}

std::string Creature::getFirstIfCreatureModel(std::string creatureAnim, std::string elseAnim) const {
    return _modelType == Creature::ModelType::Creature ? std::move(creatureAnim) : std::move(elseAnim);
}

std::string Creature::getDeadAnimation() const {
    return getFirstIfCreatureModel("cdead", "dead");
}

std::string Creature::getPauseAnimation() const {
    if (_modelType == Creature::ModelType::Creature)
        return "cpause1";

    // TODO: if (_lowHP) return "pauseinj"

    if (_combatState.active) {
        WeaponType type = WeaponType::None;
        WeaponWield wield = WeaponWield::None;
        getWeaponInfo(type, wield);

        int wieldNumber = getWeaponWieldNumber(wield);
        return str(boost::format("g%dr1") % wieldNumber);
    }

    return "pause1";
}

bool Creature::getWeaponInfo(WeaponType &type, WeaponWield &wield) const {
    std::shared_ptr<Item> item(getEquippedItem(InventorySlots::rightWeapon));
    if (item) {
        type = item->weaponType();
        wield = item->weaponWield();
        return true;
    }

    return false;
}

int Creature::getWeaponWieldNumber(WeaponWield wield) const {
    switch (wield) {
    case WeaponWield::StunBaton:
        return 1;
    case WeaponWield::SingleSword:
        return getOffhandAttackWeapon() ? 4 : 2;
    case WeaponWield::DoubleBladedSword:
        return 3;
    case WeaponWield::BlasterPistol:
        return getOffhandAttackWeapon() ? 6 : 5;
    case WeaponWield::BlasterRifle:
        return 7;
    case WeaponWield::HeavyWeapon:
        return 9;
    default:
        return 8;
    }
}

int Creature::getRelativeWeaponSize(const Item &weapon) const {
    if (_size == CreatureSize::Invalid ||
        weapon.weaponSize() == CreatureSize::Invalid) {
        return -10;
    }

    int relativeSize = static_cast<int>(weapon.weaponSize()) -
                       static_cast<int>(_size);
    return relativeSize >= -2 && relativeSize <= 1 ? relativeSize : -10;
}

int Creature::getTwoWeaponAttackPenalty(
    const Item *weapon,
    bool offHand,
    int *smallOffhandBonus) const {

    if (smallOffhandBonus) {
        *smallOffhandBonus = 0;
    }

    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    if (!mainHand || mainHand->weaponType() == WeaponType::None) {
        return 0;
    }

    auto offHandWeapon = getOffhandAttackWeapon();
    bool doubleBladed = offHandWeapon == mainHand &&
                        mainHand->weaponWield() == WeaponWield::DoubleBladedSword;
    if (!offHandWeapon || offHandWeapon->weaponType() == WeaponType::None) {
        return 0;
    }

    if (offHand) {
        if (weapon != offHandWeapon.get()) {
            return 0;
        }
        if (hasEffectiveFeat(FeatType::AdvancedDoubleWeaponFighting)) {
            return 2;
        }
        if (hasEffectiveFeat(FeatType::DoubleWeaponFighting)) {
            return 4;
        }
        if (hasEffectiveFeat(FeatType::Ambidexterity)) {
            return 6;
        }
        return 10;
    }

    if (weapon != mainHand.get()) {
        return 0;
    }

    bool balanced = doubleBladed;
    if (!balanced) {
        int relativeSize = getRelativeWeaponSize(
            mainHand->isRanged() ? *mainHand : *offHandWeapon);
        balanced = mainHand->isRanged() ? relativeSize <= -1 : relativeSize == -1;
    }

    if (balanced && smallOffhandBonus) {
        *smallOffhandBonus = 2;
    }

    int penalty = balanced ? 4 : 6;
    if (hasEffectiveFeat(FeatType::AdvancedDoubleWeaponFighting)) {
        penalty -= 4;
    } else if (hasEffectiveFeat(FeatType::DoubleWeaponFighting)) {
        penalty -= 2;
    }
    return penalty;
}

int Creature::getDuelingBonus() const {
    auto mainHand = getEquippedItem(InventorySlots::rightWeapon);
    if (!mainHand || getEquippedItem(InventorySlots::leftWeapon)) {
        return 0;
    }
    if (mainHand->weaponWield() != WeaponWield::SingleSword &&
        mainHand->weaponWield() != WeaponWield::BlasterPistol) {
        return 0;
    }

    if (hasEffectiveFeat(FeatType::MasterDueling)) {
        return 3;
    }
    if (hasEffectiveFeat(FeatType::ImprovedDueling)) {
        return 2;
    }
    return hasEffectiveFeat(FeatType::Dueling) ? 1 : 0;
}

std::string Creature::getWalkAnimation() const {
    return getFirstIfCreatureModel("cwalk", "walk");
}

std::string Creature::getRunAnimation() const {
    if (_modelType == Creature::ModelType::Creature)
        return "crun";

    // TODO: if (_lowHP) return "runinj"

    if (_combatState.active) {
        WeaponType type = WeaponType::None;
        WeaponWield wield = WeaponWield::None;
        getWeaponInfo(type, wield);

        switch (wield) {
        case WeaponWield::SingleSword:
            return isSlotEquipped(InventorySlots::leftWeapon) ? "runds" : "runss";
        case WeaponWield::DoubleBladedSword:
            return "runst";
        case WeaponWield::BlasterRifle:
        case WeaponWield::HeavyWeapon:
            return "runrf";
        default:
            break;
        }
    }

    return "run";
}

std::string Creature::getTalkNormalAnimation() const {
    return "tlknorm";
}

std::string Creature::getHeadTalkAnimation() const {
    return "talk";
}

static std::string formatCombatAnimation(const std::string &format, CreatureWieldType wield, int variant) {
    return str(boost::format(format) % static_cast<int>(wield) % variant);
}

std::string Creature::getAnimationName(CombatAnimation anim, CreatureWieldType wield, int variant) const {
    switch (anim) {
    case CombatAnimation::Draw:
        return getFirstIfCreatureModel("", formatCombatAnimation("g%dw%d", wield, 1));
    case CombatAnimation::Ready:
        return getFirstIfCreatureModel("creadyr", formatCombatAnimation("g%dr%d", wield, 1));
    case CombatAnimation::Attack:
        return getFirstIfCreatureModel("g0a1", formatCombatAnimation("g%da%d", wield, variant));
    case CombatAnimation::Damage:
        return getFirstIfCreatureModel("cdamages", formatCombatAnimation("g%dd%d", wield, variant));
    case CombatAnimation::Dodge:
        return getFirstIfCreatureModel("cdodgeg", formatCombatAnimation("g%dg%d", wield, variant));
    case CombatAnimation::MeleeAttack:
        return getFirstIfCreatureModel("m0a1", formatCombatAnimation("m%da%d", wield, variant));
    case CombatAnimation::MeleeDamage:
        return getFirstIfCreatureModel("cdamages", formatCombatAnimation("m%dd%d", wield, variant));
    case CombatAnimation::MeleeDodge:
        return getFirstIfCreatureModel("cdodgeg", formatCombatAnimation("m%dg%d", wield, variant));
    case CombatAnimation::CinematicMeleeAttack:
        return formatCombatAnimation("c%da%d", wield, variant);
    case CombatAnimation::CinematicMeleeDamage:
        return formatCombatAnimation("c%dd%d", wield, variant);
    case CombatAnimation::CinematicMeleeParry:
        return formatCombatAnimation("c%dp%d", wield, variant);
    case CombatAnimation::BlasterAttack:
        return getFirstIfCreatureModel("b0a1", formatCombatAnimation("b%da%d", wield, variant));
    default:
        return "";
    }
}

std::string Creature::getActiveAnimationName() const {
    auto model = std::dynamic_pointer_cast<ModelSceneNode>(_sceneNode);
    if (!model)
        return "";

    return model->activeAnimationName();
}

std::shared_ptr<ModelSceneNode> Creature::buildModel() {
    std::string modelName(getBodyModelName());
    if (modelName.empty()) {
        return nullptr;
    }
    std::shared_ptr<Model> model(_services.resource.models.get(modelName));
    if (!model) {
        return nullptr;
    }
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);
    auto sceneNode = sceneGraph.newModel(*model, ModelUsage::Creature);
    sceneNode->setDrawDistance(_game.options().graphics.drawDistance);
    sceneNode->setAnimationEventListener(*this);

    return sceneNode;
}

void Creature::finalizeModel(ModelSceneNode &body) {
    auto &sceneGraph = _services.scene.graphs.get(_sceneName);

    // Body texture

    if (!_envmap.empty()) {
        if (_envmap == "default") {
            body.setEnvironmentMap(&_services.graphics.textureRegistry.get(TextureName::defaultCubemapRgb));
        } else {
            body.setEnvironmentMap(_services.resource.textures.get(_envmap, TextureUsage::EnvironmentMap).get());
        }
    }
    std::string bodyTextureName(getBodyTextureName());
    if (!bodyTextureName.empty()) {
        std::shared_ptr<Texture> texture(_services.resource.textures.get(bodyTextureName, TextureUsage::MainTex));
        if (texture) {
            body.setMainTexture(texture.get());
        }
    }

    // Mask

    std::shared_ptr<Model> maskModel;
    std::string maskModelName(getMaskModelName());
    if (!maskModelName.empty()) {
        maskModel = _services.resource.models.get(maskModelName);
    }

    // Head

    std::string headModelName(getHeadModelName());
    if (!headModelName.empty()) {
        std::shared_ptr<Model> headModel(_services.resource.models.get(headModelName));
        if (headModel) {
            std::shared_ptr<ModelSceneNode> headSceneNode(sceneGraph.newModel(*headModel, ModelUsage::Creature));
            body.attach(g_headHookNode, *headSceneNode);
            if (maskModel) {
                auto maskSceneNode = sceneGraph.newModel(*maskModel, ModelUsage::Equipment);
                headSceneNode->attach(g_maskHookNode, *maskSceneNode);
            }
        }
    }

    // Right weapon

    std::string rightWeaponModelName(getWeaponModelName(InventorySlots::rightWeapon));
    if (!rightWeaponModelName.empty()) {
        std::shared_ptr<Model> weaponModel(_services.resource.models.get(rightWeaponModelName));
        if (weaponModel) {
            std::shared_ptr<ModelSceneNode> weaponSceneNode(sceneGraph.newModel(*weaponModel, ModelUsage::Equipment));
            body.attach(g_rightHandNode, *weaponSceneNode);
            if (weaponModel->classification() == MdlClassification::lightsaber) {
                weaponSceneNode->playAnimation(_combatState.active && !_dead ? "powered" : "off");
            }
        }
    }

    // Left weapon

    std::string leftWeaponModelName(getWeaponModelName(InventorySlots::leftWeapon));
    if (!leftWeaponModelName.empty()) {
        std::shared_ptr<Model> weaponModel(_services.resource.models.get(leftWeaponModelName));
        if (weaponModel) {
            std::shared_ptr<ModelSceneNode> weaponSceneNode(sceneGraph.newModel(*weaponModel, ModelUsage::Equipment));
            body.attach(g_leftHandNode, *weaponSceneNode);
            if (weaponModel->classification() == MdlClassification::lightsaber) {
                weaponSceneNode->playAnimation(_combatState.active && !_dead ? "powered" : "off");
            }
        }
    }
}

CreaturePresentation Creature::presentation() const {
    if (_presentation) return *_presentation;
    CreaturePresentation result;
    result.gender = static_cast<int>(_gender);
    result.appearance = _appearance;
    auto body = getEquippedItem(InventorySlots::body);
    if (body && !body->baseBodyVariation().empty()) {
        auto variation = boost::to_lower_copy(body->baseBodyVariation());
        result.bodyVariation = variation.front() - 'a';
        result.textureVariation = body->textureVariation();
    }
    return result;
}

void Creature::setPresentation(const CreaturePresentation &presentation) {
    if (!isPresentationOnly()) {
        throw std::logic_error("Visual tuples require a presentation-only creature");
    }
    if (presentation.appearance < 0 || presentation.appearance > 65535 ||
        presentation.gender < 0 || presentation.gender > static_cast<int>(Gender::None) ||
        presentation.bodyVariation < 0 || presentation.bodyVariation >= 26 ||
        presentation.textureVariation < 0 || presentation.textureVariation > 255) {
        throw std::invalid_argument("Invalid creature presentation");
    }
    _presentation = presentation;
    _appearance = presentation.appearance;
    _gender = static_cast<Gender>(presentation.gender);
}

std::string Creature::getBodyModelName() const {
    std::string column;

    if (_modelType == Creature::ModelType::Character) {
        column = "model";

        std::shared_ptr<Item> bodyItem(getEquippedItem(InventorySlots::body));
        if (_presentation) {
            column += static_cast<char>('a' + _presentation->bodyVariation);
        } else if (bodyItem) {
            std::string baseBodyVar(bodyItem->baseBodyVariation());
            column += baseBodyVar;
        } else {
            column += "a";
        }

    } else {
        column = "race";
    }

    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    std::string modelName(appearance->getString(_appearance, column));
    boost::to_lower(modelName);

    return modelName;
}

std::string Creature::getBodyTextureName() const {
    std::string column;
    std::shared_ptr<Item> bodyItem(getEquippedItem(InventorySlots::body));

    if (_modelType == Creature::ModelType::Character) {
        column = "tex";

        if (_presentation) {
            column += static_cast<char>('a' + _presentation->bodyVariation);
        } else if (bodyItem) {
            std::string baseBodyVar(bodyItem->baseBodyVariation());
            column += baseBodyVar;
        } else {
            column += "a";
        }
    } else {
        column = "racetex";
    }

    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }

    std::string texName(boost::to_lower_copy(appearance->getString(_appearance, column)));
    if (texName.empty())
        return "";

    if (_modelType == Creature::ModelType::Character) {
        bool texFound = false;
        if (_presentation || bodyItem) {
            auto variation = _presentation ? _presentation->textureVariation : bodyItem->textureVariation();
            std::string tmp(str(boost::format("%s%02d") % texName % variation));
            std::shared_ptr<Texture> texture(_services.resource.textures.get(tmp, TextureUsage::MainTex));
            if (texture) {
                texName = std::move(tmp);
                texFound = true;
            }
        }
        if (!texFound) {
            texName += "01";
        }
    }

    return texName;
}

std::string Creature::getHeadModelName() const {
    if (_modelType != Creature::ModelType::Character) {
        return "";
    }
    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    if (!appearance) {
        throw ResourceNotFoundException("appearance 2DA not found");
    }
    int headIdx = _headOverride ? *_headOverride : appearance->getInt(_appearance, "normalhead", -1);
    if (headIdx == -1) {
        return "";
    }
    std::shared_ptr<TwoDA> heads(_services.resource.twoDas.get("heads"));
    if (!heads) {
        throw ResourceNotFoundException("heads 2DA not found");
    }

    std::string modelName(heads->getString(headIdx, "head"));
    boost::to_lower(modelName);

    return modelName;
}

int Creature::headRow() const {
    if (_headOverride) {
        return *_headOverride;
    }
    std::shared_ptr<TwoDA> appearance(_services.resource.twoDas.get("appearance"));
    return appearance ? appearance->getInt(_appearance, "normalhead", -1) : -1;
}

void Creature::setHeadOverride(int headRow) {
    if (_headOverride && *_headOverride == headRow) {
        return;
    }
    _headOverride = headRow;
    if (_sceneNode) {
        updateModel();
    }
}

void Creature::setSceneHiddenEquipment(bool weapons, bool headItem) {
    if (_sceneHideWeapons == weapons && _sceneHideHeadItem == headItem) {
        return;
    }
    _sceneHideWeapons = weapons;
    _sceneHideHeadItem = headItem;
    updateModel();
}

std::string Creature::getMaskModelName() const {
    if (_sceneHideHeadItem) {
        return "";
    }
    std::shared_ptr<Item> headItem(getEquippedItem(InventorySlots::head));
    if (!headItem)
        return "";

    std::string modelName(boost::to_lower_copy(headItem->itemClass()));
    modelName += str(boost::format("_%03d") % headItem->modelVariation());

    return modelName;
}

std::string Creature::getWeaponModelName(int slot) const {
    if (_sceneHideWeapons) {
        return "";
    }
    std::shared_ptr<Item> bodyItem(getEquippedItem(slot));
    if (!bodyItem)
        return "";

    std::string modelName(bodyItem->itemClass());
    boost::to_lower(modelName);

    modelName += str(boost::format("_%03d") % bodyItem->modelVariation());

    return modelName;
}

void Creature::deserialize(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    std::string templateRes;
    if (!identityContext.isSerializedState() &&
        gff.readResRef(templateRes, "TemplateResRef")) {
        if (auto utc = _services.resource.gffs.get(templateRes, ResType::Utc)) {
            deserializeAll(
                *utc,
                SerializedIdentityContext::templateResource(templateRes));
        }
    }
    deserializeAll(gff, identityContext);

    restoreSerializedVitality();

    updateTransform();
    loadAppearance();
}

void Creature::deserializeAll(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    Object::deserialize(gff, identityContext);

    // Retail reads IsPC before post-processing hit points. A player character
    // remains an incapacitated, resumable runtime object through 0..-9 HP and
    // becomes truly dead only at -10 HP.
    gff.readBool(_isPC, "IsPC");
    gff.readInt(_assignedPuppet, "AssignedPup");

    // index into racialtypes.2da
    gff.readEnum(_race, "Race");

    // index into subrace.2da
    gff.readEnum(_subrace, "SubraceIndex");

    // index into appearance.2da
    gff.readEnum(_appearance, "Appearance_Type");

    // Savegames keep the visible disguise appearance in Appearance_Type and
    // the normal appearance separately. Restore this before equipped items
    // are loaded, so equipping a saved disguise does not overwrite it.
    _disguised = false;
    _appearanceBeforeDisguise = 0;
    bool disguised;
    uint16_t appearanceBeforeDisguise;
    if (gff.readBool(disguised, "PM_IsDisguised") &&
        disguised &&
        gff.readWord(appearanceBeforeDisguise, "PM_Appearance")) {
        _disguised = true;
        _appearanceBeforeDisguise = appearanceBeforeDisguise;
    }

    // in dex into gender.2da
    gff.readEnum(_gender, "Gender");

    // index into portrait.2da
    gff.readWord(_portraitId, "PortraitId");

    // index into repute.2da
    gff.readEnum(_faction, "FactionID");

    gff.readBool(_disarmable, "Disarmable");
    gff.readBool(_noPermDeath, "NoPermDeath");
    gff.readBool(_notReorienting, "NotReorienting");
    gff.readByte(_bodyVariation, "BodyVariation");
    gff.readByte(_textureVar, "TextureVar");
    gff.readBool(_partyInteract, "PartyInteract");

    // index into creaturespeed.2da
    gff.readInt(_walkRate, "WalkRate");
    uint8_t movementRate;
    if (gff.readByte(movementRate, "MovementRate")) {
        _walkRate = movementRate;
    }
    gff.readBool(_isListening, "Listening");

    gff.readByte(_naturalAC, "NaturalAC");
    gff.readShort(_forcePoints, "ForcePoints");
    if (!gff.readShort(_currentForce, "CurrentForce")) {
        // Missing: full, i.e. the base on the serialized axis.
        _currentForce = _forcePoints;
    }
    gff.readShort(_refBonus, "refbonus");
    gff.readShort(_willBonus, "willbonus");
    gff.readShort(_fortBonus, "fortbonus");
    gff.readByte(_goodEvil, "GoodEvil");
    gff.readInt(_activeForm, "CurrentForm");
    gff.readFloat(_challengeRating, "ChallengeRating");
    gff.readDword(_xp, "Experience");

    gff.readResRef(_onNotice, "ScriptOnNotice");
    gff.readResRef(_onSpellAt, "ScriptSpellAt");
    gff.readResRef(_onAttacked, "ScriptAttacked");
    gff.readResRef(_onDamaged, "ScriptDamaged");
    gff.readResRef(_onDisturbed, "ScriptDisturbed");
    gff.readResRef(_onEndRound, "ScriptEndRound");
    gff.readResRef(_onEndDialogue, "ScriptEndDialogu");
    gff.readResRef(_onRested, "ScriptRested");
    gff.readResRef(_onDialogue, "ScriptDialogue");
    gff.readResRef(_onSpawn, "ScriptSpawn");
    gff.readResRef(_onDeath, "ScriptDeath");
    gff.readResRef(_onBlocked, "ScriptOnBlocked");

    // Only a saved creature record carries CreatnScrptFird; a blueprint leaves
    // the flag clear so a freshly materialized creature still spawns once.
    uint8_t spawnScriptFired = 0;
    if (gff.readByte(spawnScriptFired, "CreatnScrptFird")) {
        _spawnScriptFired = spawnScriptFired != 0;
    }

    deserializeName(gff);
    deserializeSoundSet(gff);
    deserializeBodyBag(gff);
    deserializeAttributes(gff);
    deserializePerception(gff);
    deserializeOwnedItemsAndEquipment(gff, identityContext);
}

void Creature::deserializeName(const resource::Gff &gff) {
    gff.readLocString(_firstName, "FirstName", _services.resource.strings);
    gff.readLocString(_lastName, "LastName", _services.resource.strings);

    _name = _firstName.str();
    const std::string &last = _lastName.str();
    if (!_name.empty() && !last.empty()) {
        _name += ' ';
    }
    _name += last;
}

void Creature::deserializeSoundSet(const resource::Gff &gff) {
    gff.readWord(_soundSetId, "SoundSetFile");
    if (_soundSetId == 0xffff) {
        return;
    }

    std::shared_ptr<TwoDA> soundSetTable(_services.resource.twoDas.get("soundset"));
    if (!soundSetTable) {
        return;
    }
    std::string soundSetResRef(soundSetTable->getString(_soundSetId, "resref"));
    if (!soundSetResRef.empty()) {
        _soundSet = _services.resource.soundSets.get(soundSetResRef);
    }
}

void Creature::deserializeBodyBag(const resource::Gff &gff) {
    gff.readByte(_bodyBagId, "BodyBag");
    if (_bodyBagId == 0xFF) {
        return;
    }

    std::shared_ptr<TwoDA> bodyBags(_services.resource.twoDas.get("bodybag"));
    if (!bodyBags) {
        return;
    }
    _bodyBag.name = _services.resource.strings.getText(bodyBags->getInt(_bodyBagId, "name"));
    _bodyBag.appearance = bodyBags->getInt(_bodyBagId, "appearance");
    _bodyBag.corpse = bodyBags->getBool(_bodyBagId, "corpse");
    return;
}

void Creature::deserializeAttributes(const resource::Gff &gff) {
    CreatureAttributes &attributes = _attributes;
    {
        uint8_t value;
        if (gff.readByte(value, "Str")) {
            attributes.setAbilityScore(Ability::Strength, value);
        }
        if (gff.readByte(value, "Dex")) {
            attributes.setAbilityScore(Ability::Dexterity, value);
        }
        if (gff.readByte(value, "Con")) {
            attributes.setAbilityScore(Ability::Constitution, value);
        }
        if (gff.readByte(value, "Int")) {
            attributes.setAbilityScore(Ability::Intelligence, value);
        }
        if (gff.readByte(value, "Wis")) {
            attributes.setAbilityScore(Ability::Wisdom, value);
        }
        if (gff.readByte(value, "Cha")) {
            attributes.setAbilityScore(Ability::Charisma, value);
        }
    }

    for (const auto &clazz : gff.getList("ClassList")) {
        deserializeClass(*clazz);
    }

    int skillType = 0;
    for (const auto &skill : gff.getList("SkillList")) {
        attributes.setSkillRank(
            static_cast<SkillType>(skillType++), skill->getUint("Rank"));
    }

    for (const auto &feat : gff.getList("FeatList")) {
        auto featType = static_cast<FeatType>(feat->getUint("Feat"));
        _attributes.addFeat(featType);
    }
}

void Creature::deserializeClass(const resource::Gff &gff) {
    auto clazz = _services.game.classes.get(
        static_cast<ClassType>(gff.getInt("Class")));
    if (!clazz) {
        return;
    }

    int16_t level;
    if (gff.readShort(level, "ClassLevel")) {
        _attributes.addClassLevels(clazz.get(), level);
    }

    for (const auto &spell : gff.getList("KnownList0")) {
        auto spellType = static_cast<SpellType>(spell->getUint("Spell"));
        // Bit 0x1 of SpellFlags marks the power ready for use. K1 sets it on
        // every entry; a missing field counts as ready.
        uint8_t spellFlags = kKnownSpellReady;
        spell->readByte(spellFlags, "SpellFlags");
        if ((spellFlags & kKnownSpellReady) == 0) {
            continue;
        }
        _attributes.addSpell(spellType);
    }
}

void Creature::deserializePerception(const resource::Gff &gff) {
    gff.readByte(_perceptionId, "PerceptionRange");
    if (_perceptionId == 0xFF) {
        return;
    }

    std::shared_ptr<TwoDA> ranges(_services.resource.twoDas.get("ranges"));
    if (!ranges) {
        return;
    }

    _perception.sightRange = ranges->getFloat(_perceptionId, "primaryrange");
    _perception.hearingRange = ranges->getFloat(_perceptionId, "secondaryrange");
}

std::vector<std::shared_ptr<Object>> Creature::ownedRuntimeObjects() const {
    auto result = Object::ownedRuntimeObjects();
    std::set<const Object *> seen;
    for (const auto &object : result) {
        if (object) {
            seen.insert(object.get());
        }
    }
    for (const auto &[_, item] : _equipment) {
        if (item && seen.insert(item.get()).second) {
            result.push_back(item);
        }
    }
    return result;
}

void Creature::deserializeOwnedItemsAndEquipment(
    const resource::Gff &gff,
    const SerializedIdentityContext &identityContext) {
    const bool replaceItems = gff.has("ItemList");
    const bool replaceEquipment = gff.has("Equip_ItemList");
    if (!replaceItems && !replaceEquipment) return;

    if (!identityContext.isSerializedState()) {
        std::vector<std::shared_ptr<Object>> obsolete;
        if (replaceItems) {
            obsolete.insert(obsolete.end(), _items.begin(), _items.end());
        }
        if (replaceEquipment) {
            for (const auto &[_, item] : _equipment) {
                obsolete.push_back(item);
            }
        }
        std::vector<std::shared_ptr<Item>> replacementItems =
            replaceItems ? std::vector<std::shared_ptr<Item>> {} : _items;
        std::map<int, std::shared_ptr<Item>> replacementEquipment =
            replaceEquipment
                ? std::map<int, std::shared_ptr<Item>> {}
                : _equipment;
        ItemAttributes replacementAttributes;
        std::deque<EffectInstance> replacementEffects;
        _game.replaceRuntimeObjectGraph(
            obsolete,
            [&]() {
                if (replaceItems) {
                    for (const auto &itemGff : gff.getList("ItemList")) {
                        auto item = _game.newOwnedItem(*itemGff, identityContext);
                        item->setOwner(_id);
                        appendOwnedItemCandidate(
                            replacementItems, item, true);
                    }
                }
                if (replaceEquipment) {
                    for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                        auto item = _game.newOwnedItem(*itemGff, identityContext);
                        int slot = item->isEquippable(InventorySlots::body)
                                       ? InventorySlots::body
                                       : InventorySlots::rightWeapon;
                        // SetItemNonEquippable: a flagged item loads into
                        // the backpack (K2 behaviour, design note c11_data).
                        if (!item->isEquippable(getEquipabilitySlot(slot)) ||
                            item->isNonEquippable() ||
                            replacementEquipment.count(slot) != 0) {
                            item->setOwner(_id);
                            appendOwnedItemCandidate(
                                replacementItems, item, true);
                            continue;
                        }
                        item->setOwner(_id);
                        item->setEquipped(true);
                        replacementEquipment.emplace(slot, std::move(item));
                    }
                }
                for (const auto &item : replacementItems) {
                    replacementAttributes.addItem(item, _services.game);
                }
                if (replaceEquipment) {
                    replacementEffects =
                        rebuildEquippedItemEffects(replacementEquipment);
                }
            },
            [&]() noexcept {
                _items = std::move(replacementItems);
                _equipment = std::move(replacementEquipment);
                _itemAttributes = std::move(replacementAttributes);
                if (replaceEquipment) {
                    replaceEffectState(std::move(replacementEffects));
                }
            });
        return;
    }

    std::vector<std::shared_ptr<Object>> obsolete;
    if (replaceItems) {
        obsolete.insert(obsolete.end(), _items.begin(), _items.end());
    }
    if (replaceEquipment) {
        for (const auto &[_, item] : _equipment) {
            obsolete.push_back(item);
        }
    }
    std::vector<std::shared_ptr<Item>> replacementItems =
        replaceItems ? std::vector<std::shared_ptr<Item>> {} : _items;
    std::map<int, std::shared_ptr<Item>> replacementEquipment =
        replaceEquipment
            ? std::map<int, std::shared_ptr<Item>> {}
            : _equipment;
    ItemAttributes replacementAttributes;
    std::deque<EffectInstance> replacementEffects;
    _game.replaceRuntimeObjectGraph(
        obsolete,
        [&]() {
            if (replaceItems) {
                for (const auto &itemGff : gff.getList("ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    item->captureSaveRecord(
                        *itemGff,
                        identityContext,
                        {SaveRecordOriginKind::ContainedItem, std::to_string(_id)});
                    item->setOwner(_id);
                    appendOwnedItemCandidate(
                        replacementItems, item, true);
                }
            }

            if (replaceEquipment) {
                for (const auto &itemGff : gff.getList("Equip_ItemList")) {
                    auto item = _game.newOwnedItem(*itemGff, identityContext);
                    item->captureSaveRecord(
                        *itemGff,
                        identityContext,
                        {SaveRecordOriginKind::EquippedItem, std::to_string(_id)});

                    std::optional<int> slot;
                    uint32_t slotMask = itemGff->type();
                    if (slotMask != 0 && (slotMask & (slotMask - 1)) == 0) {
                        int value = 0;
                        while ((slotMask >>= 1) != 0) {
                            ++value;
                        }
                        if (item->isEquippable(getEquipabilitySlot(value))) {
                            slot = value;
                        }
                    }
                    if (!slot && item->isEquippable(InventorySlots::body)) {
                        slot = InventorySlots::body;
                    } else if (!slot &&
                               item->isEquippable(InventorySlots::rightWeapon)) {
                        slot = InventorySlots::rightWeapon;
                    }

                    if (!slot) {
                        item->setOwner(_id);
                        appendOwnedItemCandidate(
                            replacementItems, item, true);
                        warn(str(boost::format("item is not equippable: %s") %
                                 replacementItems.back()->tag()));
                        continue;
                    }
                    if (replacementEquipment.count(*slot) != 0) {
                        throw ValidationException(
                            "Multiple saved items occupy equipment slot " +
                            std::to_string(*slot));
                    }
                    item->setOwner(_id);
                    item->setEquipped(true);
                    replacementEquipment.emplace(*slot, std::move(item));
                }
            }
            for (const auto &item : replacementItems) {
                replacementAttributes.addItem(item, _services.game);
            }
            if (replaceEquipment) {
                replacementEffects =
                    rebuildEquippedItemEffects(replacementEquipment);
            }
        },
        [&]() noexcept {
            if (replaceEquipment) {
                for (auto &[_, item] : _equipment) {
                    if (item) {
                        item->setEquipped(false);
                        item->setOwner(0);
                    }
                }
            }
            if (replaceItems) {
                for (auto &item : _items) {
                    if (item) {
                        item->setOwner(0);
                    }
                }
            }
            _items = std::move(replacementItems);
            _equipment = std::move(replacementEquipment);
            _itemAttributes = std::move(replacementAttributes);
            if (replaceEquipment) {
                replaceEffectState(std::move(replacementEffects));
            }
        });
    uint32_t previousAppearance = _appearance;
    updateDisguise();
    if (_appearance != previousAppearance) {
        loadAppearanceProperties();
    }
}

} // namespace game

} // namespace reone
