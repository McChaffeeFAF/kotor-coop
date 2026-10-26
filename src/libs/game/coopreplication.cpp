/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopreplication.h"
#include "reone/game/combatqueue.h"

#include <algorithm>
#include <cmath>

#include <boost/algorithm/string.hpp>

#include "reone/game/game.h"
#include "reone/game/object.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/graphics/animation.h"
#include "reone/scene/node/model.h"

using namespace reone::net;

namespace reone {

namespace game {

namespace {

std::string itemRef(const std::shared_ptr<Item> &item) {
    if (!item) {
        return std::string();
    }
    return item->blueprintResRef().empty() ? item->tag() : item->blueprintResRef();
}

int16_t clamp16(int value) {
    return static_cast<int16_t>(std::clamp(value, -32768, 32767));
}

std::shared_ptr<scene::ModelSceneNode> modelOf(const Object &object) {
    return std::dynamic_pointer_cast<scene::ModelSceneNode>(object.sceneNode());
}

/** Bring one equipment slot to the host's template, by the client's own item. */
void syncSlot(Game &game, Creature &creature, int slot, const std::string &want) {
    auto have = creature.getEquippedItem(slot);
    if (boost::iequals(itemRef(have), want)) {
        return;
    }
    if (have) {
        if (auto taken = creature.takeEquippedItem(have)) {
            game.destroyRuntimeObjectGraph(taken);
        }
    }
    if (want.empty()) {
        return;
    }
    auto item = game.newItemFromBlueprint(boost::to_lower_copy(want));
    if (item && !creature.equip(slot, item)) {
        game.destroyRuntimeObjectGraph(item);
    }
}

} // namespace

static const scene::ModelSceneNode::AnimationChannel *replicatedChannel(const scene::ModelSceneNode &model) {
    // The one clip a client plays is the body's. Layers ride on top of it
    // (the talk layer, a scripted overlay gesture) and an overlay set is the
    // body clip under the head's talk clip: sending the front channel gave
    // the client the head alone, and a talking creature stood in its bind
    // pose (alpha-0.4.5 Trask in Story Mode).
    const auto &channels = model.animationChannels();
    auto base = model.baseAnimationChannel();
    if (!base) {
        return nullptr;
    }
    const auto *channel = &base->get();
    if (channel->properties.flags & scene::AnimationFlags::overlay) {
        channel = &channels.back();
    }
    return channel->anim ? channel : nullptr;
}

bool CoopReplication::isReplicated(const Object &object) {
    switch (object.type()) {
    case ObjectType::Creature:
    case ObjectType::Door:
    case ObjectType::Placeable:
        return true;
    default:
        return false;
    }
}

EntityState CoopReplication::capture(const Object &object, const std::string &key) {
    EntityState s;
    s.id = object.id();
    s.type = static_cast<uint8_t>(object.type());
    s.key = key;
    s.tag = object.tag();
    s.resref = object.blueprintResRef();
    s.position = {object.position().x, object.position().y, object.position().z};
    s.facing = dequantizeFacing(quantizeFacing(object.getFacing()));
    if (auto model = modelOf(object)) {
        if (const auto *body = replicatedChannel(*model)) {
            const auto &channel = *body;
            s.anim = channel.anim->name();
            s.animTime = std::max(0.0f, channel.time);
            s.animFlags = static_cast<uint16_t>(channel.properties.flags & 0xffff);
            s.animSpeed = std::clamp(channel.properties.speed, 0.0f, 100.0f);
        }
    }
    uint8_t flags = 0;
    if (object.visible()) {
        flags |= kFlagVisible;
    }
    if (object.isSelectable()) {
        flags |= kFlagSelectable;
    }
    switch (object.type()) {
    case ObjectType::Creature: {
        auto &creature = static_cast<const Creature &>(object);
        s.hp = clamp16(creature.currentHitPoints());
        s.maxHp = clamp16(creature.maxHitPoints());
        s.dead = creature.isDead();
        s.rightWeapon = itemRef(creature.getEquippedItem(InventorySlots::rightWeapon));
        s.leftWeapon = itemRef(creature.getEquippedItem(InventorySlots::leftWeapon));
        s.armor = itemRef(creature.getEquippedItem(InventorySlots::body));
        s.headgear = itemRef(creature.getEquippedItem(InventorySlots::head));
        if (creature.isInCombat()) {
            flags |= kFlagInCombat;
        }
        if (creature.isInConversation()) {
            flags |= kFlagInConversation;
        }
        if (creature.isStealthed()) {
            flags |= kFlagStealthed;
        }
        if (creature.autoCombat()) {
            flags |= kFlagAutoCombat;
        }
        if (auto target = creature.getAttackTarget()) {
            s.attackTarget = target->id();
        }
        // The HUD's level-up icon: a client computes nothing from its own copy.
        if (creature.isLevelUpPending()) {
            s.progress |= kProgressLevelUp;
        }
        // The faction the host's reputation rules use: a guest's client runs
        // no scripts, so ChangeToStandardFaction and friends reach it here.
        // Party members and Free Mode guests are the player faction.
        {
            int faction = static_cast<int>(creature.reputationFaction());
            s.faction = faction >= 0 && faction < static_cast<int>(kMaxFactions) ? static_cast<uint8_t>(faction) : kNoFaction;
        }
        // The HUD's combat queue of a player's character: a guest's client
        // runs none of its actions, so the host says what is queued.
        if (creature.isInCombat() && creature.reputationFaction() == Faction::Player) {
            s.queue = combatqueue::entriesOf(creature);
        }
        break;
    }
    case ObjectType::Door: {
        auto &door = static_cast<const Door &>(object);
        s.open = door.isOpen();
        s.locked = door.isLocked();
        s.doorState = static_cast<uint8_t>(door.state());
        s.doorTransition = static_cast<uint8_t>(door.transition());
        s.hp = clamp16(door.currentHitPoints());
        s.maxHp = clamp16(door.maxHitPoints());
        s.dead = door.isDead();
        break;
    }
    case ObjectType::Placeable: {
        auto &placeable = static_cast<const Placeable &>(object);
        if (s.resref.empty()) {
            // Built in code (a body bag): no template to name, so the client
            // builds its own from the appearance row.
            s.resref = kAppearancePrefix + std::to_string(placeable.appearance());
        }
        s.open = placeable.isOpen();
        s.locked = placeable.isLocked();
        s.hp = clamp16(placeable.currentHitPoints());
        s.maxHp = clamp16(placeable.maxHitPoints());
        s.dead = placeable.isDead();
        break;
    }
    default:
        break;
    }
    s.flags = flags;
    return s;
}

void CoopReplication::apply(Game &game, Object &object, const EntityState &state, uint16_t fields) {
    if (fields & kFieldPosition) {
        object.setPosition(glm::vec3(state.position.x, state.position.y, state.position.z));
    }
    if (fields & kFieldFacing) {
        object.setFacing(state.facing);
    }
    if (fields & kFieldFlags) {
        object.setVisible((state.flags & kFlagVisible) != 0);
        if (object.type() == ObjectType::Creature) {
            // The host's word on Stealth Mode: the HUD toggle and walking.
            // The shimmer arrives as the host's VfxBegin.
            static_cast<Creature &>(object).setReplicaStealthMode((state.flags & kFlagStealthed) != 0);
            // And on auto-combat: the player's HUD toggle shows it lit.
            static_cast<Creature &>(object).setAutoCombat((state.flags & kFlagAutoCombat) != 0);
        }
    }
    if (fields & kFieldVitals) {
        object._currentHitPoints = state.hp;
        object._maxHitPoints = state.maxHp;
        object._dead = state.dead;
    }
    if (fields & kFieldOpenLock) {
        object._open = state.open;
        if (auto *door = dynamic_cast<Door *>(&object)) {
            door->_locked = state.locked;
        } else if (auto *placeable = dynamic_cast<Placeable *>(&object)) {
            placeable->setLocked(state.locked);
        }
    }
    if ((fields & kFieldFaction) && object.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(object);
        creature.setFaction(state.faction == kNoFaction ? Faction::Invalid : static_cast<Faction>(state.faction));
    }
    if ((fields & kFieldProgress) && object.type() == ObjectType::Creature) {
        // The host's word on a level being due: the client's copy of a
        // creature has its own template XP and classes, not the host's.
        static_cast<Creature &>(object).setReplicaLevelUp((state.progress & kProgressLevelUp) != 0);
    }
    if ((fields & kFieldWeapons) && object.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(object);
        syncSlot(game, creature, InventorySlots::body, state.armor);
        syncSlot(game, creature, InventorySlots::rightWeapon, state.rightWeapon);
        syncSlot(game, creature, InventorySlots::leftWeapon, state.leftWeapon);
        syncSlot(game, creature, InventorySlots::head, state.headgear);
    }
    if ((fields & (kFieldLook | kFieldWeapons)) && object.type() == ObjectType::Creature && state.look.bodyVariation >= 0) {
        // A stand-in: the whole look the host built (head, the other
        // equipment slots, the body model and texture), in the same order.
        auto &creature = static_cast<Creature &>(object);
        const auto &look = state.look;
        for (int slot : {InventorySlots::hands, InventorySlots::leftArm, InventorySlots::rightArm, InventorySlots::implant,
                         InventorySlots::belt, InventorySlots::rightWeapon2, InventorySlots::leftWeapon2}) {
            std::string want;
            for (const auto &entry : look.slots) {
                if (entry.slot == slot) {
                    want = entry.item;
                }
            }
            syncSlot(game, creature, slot, want);
        }
        if (look.headRow >= 0 && creature.headRow() != look.headRow) {
            creature.setHeadOverride(look.headRow);
        }
        creature.setReplicaBodyLook(look.bodyVariation, look.textureVariation);
    }
    if ((fields & kFieldDoor) && object.type() == ObjectType::Door) {
        auto &door = static_cast<Door &>(object);
        auto transition = static_cast<DoorTransition>(state.doorTransition);
        auto resting = static_cast<DoorState>(state.doorState);
        if (transition == DoorTransition::Opening && door._transition != DoorTransition::Opening) {
            door.open(nullptr, false);
        } else if (transition == DoorTransition::Closing && door._transition != DoorTransition::Closing) {
            door.close(false);
        } else if (transition == DoorTransition::None &&
                   (door._state != resting || door._transition != DoorTransition::None)) {
            door._transition = DoorTransition::None;
            door._state = resting;
            door.applyRestingState();
        }
    }
    if (fields & kFieldAnim) {
        auto model = modelOf(object);
        if (model && !state.anim.empty()) {
            scene::AnimationProperties properties;
            properties.flags = state.animFlags;
            properties.speed = state.animSpeed;
            const auto &channels = model->animationChannels();
            bool same = !channels.empty() && channels.front().anim &&
                        channels.front().anim->name() == state.anim &&
                        channels.front().properties.flags == properties.flags;
            if (!same) {
                model->playAnimation(state.anim, nullptr, properties);
            }
            model->setAnimationTime(state.animTime);
        }
    }
    if (auto model = modelOf(object)) {
        model->setPickable(object.isSelectable());
    }
}

bool CoopReplication::playingAnimation(const Object &object, std::string &name, float &time) {
    auto model = modelOf(object);
    if (!model) {
        return false;
    }
    const auto *channel = replicatedChannel(*model);
    if (!channel) {
        // No base clip: whatever plays on top (an overlay set the host sent alone).
        for (const auto &candidate : model->animationChannels()) {
            if (candidate.anim) {
                channel = &candidate;
                break;
            }
        }
    }
    if (!channel) {
        return false;
    }
    name = channel->anim->name();
    time = channel->time;
    return true;
}

bool CoopReplication::animationDue(const EntityState &sent, const EntityState &current, uint32_t tick, float tickSeconds,
                                   bool clientHasNoBase) {
    if (current.anim.empty()) {
        return false;
    }
    if (clientHasNoBase) {
        return true;
    }
    float expected = sent.animTime + static_cast<float>(tick - sent.animSentTick) * tickSeconds * sent.animSpeed;
    return std::fabs(current.animTime - expected) > kAnimResync;
}

void CoopReplication::presentTransform(Object &object, const Vec3 &position, float facing) {
    auto node = object.sceneNode();
    if (!node || object._stunt) {
        return;
    }
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(position.x, position.y, position.z));
    transform *= glm::mat4_cast(glm::quat(glm::vec3(0.0f, 0.0f, facing)));
    node->setLocalTransform(transform);
}

void CoopReplication::restoreTransform(Object &object) {
    object.updateTransform();
}

} // namespace game

} // namespace reone
