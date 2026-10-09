/* Copyright (c) 2026 The reone project contributors */

#include "reone/game/coopreplication.h"

#include <algorithm>

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
        const auto &channels = model->animationChannels();
        if (!channels.empty() && channels.front().anim) {
            const auto &channel = channels.front();
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
        if (auto target = creature.getAttackTarget()) {
            s.attackTarget = target->id();
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
    if ((fields & kFieldWeapons) && object.type() == ObjectType::Creature) {
        auto &creature = static_cast<Creature &>(object);
        syncSlot(game, creature, InventorySlots::body, state.armor);
        syncSlot(game, creature, InventorySlots::rightWeapon, state.rightWeapon);
        syncSlot(game, creature, InventorySlots::leftWeapon, state.leftWeapon);
        syncSlot(game, creature, InventorySlots::head, state.headgear);
    }
    if ((fields & kFieldDoor) && object.type() == ObjectType::Door) {
        auto &door = static_cast<Door &>(object);
        auto transition = static_cast<DoorTransition>(state.doorTransition);
        auto resting = static_cast<DoorState>(state.doorState);
        if (transition == DoorTransition::Opening && door._transition != DoorTransition::Opening) {
            door.open();
        } else if (transition == DoorTransition::Closing && door._transition != DoorTransition::Closing) {
            door.close();
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
