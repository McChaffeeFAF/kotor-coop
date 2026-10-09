/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <string>

#include "reone/net/protocol.h"

namespace reone {

namespace game {

class Game;
class Object;

/**
 * Reads replicated state off host objects and writes it onto client copies.
 *
 * A friend of Object and Door: the client sets hit points, death and door
 * state directly, because the setters a simulation uses also run the rules
 * (death handling, door events) that only the host may run.
 */
/** Template field of a placeable that has none: "@appearance:<placeables.2da row>". */
constexpr const char *kAppearancePrefix = "@appearance:";

class CoopReplication {
public:
    /** Host: one object's replicated state. */
    static net::EntityState capture(const Object &object, const std::string &key);

    /**
     * Client: apply the named fields of a replicated state to the local copy.
     * No scripts, no actions, no dice; presentation follows the host.
     */
    static void apply(Game &game, Object &object, const net::EntityState &state, uint16_t fields);

    /** Client: the object's scene node at an interpolated transform. */
    static void presentTransform(Object &object, const net::Vec3 &position, float facing);

    /** Client: put the scene node back on the object's own transform. */
    static void restoreTransform(Object &object);

    /** Client: does this object take part in replication at all? */
    static bool isReplicated(const Object &object);
};

} // namespace game

} // namespace reone
