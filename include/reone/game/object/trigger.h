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

#include "../object.h"
#include "../traps.h"

namespace reone {

namespace game {

class Door;

class Trigger : public Object {
public:
    enum class DebugState {
        Default,
        Tested,
        Inside,
        Entered
    };

    Trigger(
        uint32_t id,
        std::string sceneName,
        Game &game,
        ServicesView &services) :
        Object(
            id,
            ObjectType::Trigger,
            std::move(sceneName),
            game,
            services) {
    }

    static bool classof(const Object *from) {
        return from->type() == ObjectType::Trigger;
    }

    void loadFromBlueprint(const std::string &resRef);
    void deserialize(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void configureLinkedDoorTransition(const std::shared_ptr<Door> &door);

    void update(float dt) override;

    void addTenant(const std::shared_ptr<Object> &object);

    // Drop an object from the tenant set without firing OnExit. Used when an
    // object is destroyed while standing inside the trigger: a destroyed object
    // never moves, so Trigger::update would otherwise keep it as a tenant
    // forever (leaking it and leaving the trigger stuck in the Inside state).
    void removeTenant(const Object *object);

    bool isIn(const glm::vec2 &point) const;
    bool isTenant(const std::shared_ptr<Object> &object) const;
    /** Objects currently inside the trigger (unordered). */
    const std::set<std::shared_ptr<Object>> &tenants() const { return _tenants; }
    bool isActive() const;
    bool isLinkedDoorTransition() const { return _linkedDoorTransition; }
    bool acceptsTransitionActivator(const std::shared_ptr<Object> &activator) const;
    bool detachLinkedDoorTransition(const Door &door);

    const std::vector<glm::vec3> &geometry() const { return _geometry; }
    DebugState debugState() const;
    glm::vec4 debugColor() const;

    void markDebugTested(bool inside);
    void markDebugEntered();

    const std::string &getOnEnter() const { return _onEnter; }
    const std::string &getOnExit() const { return _onExit; }
    bool hasClickScript() const { return !_onClick.empty(); }
    void setOnClick(std::string script) { _onClick = std::move(script); }
    void setOnEnter(std::string script) { _onEnter = std::move(script); }

    /**
     * The party leader took the area transition: its OnClick runs in place of
     * the default transition.
     */
    void runClickScript(uint32_t clickerId);

    // Traps (design note c11_data, "Traps")

    bool isTrapTrigger() const { return _triggerType == kTriggerTypeTrap; }
    bool isTrap() const { return isTrapTrigger() && _trapFlag != 0; }
    TrapFields trapFields() const;
    Faction faction() const { return _faction; }
    uint32_t trapCreatorId() const { return _creatorId; }
    /** Trap centre: the trigger position plus the mean of its geometry points. */
    glm::vec3 trapCenter() const;

    bool isTrapDetectedBy(const Object &creature) const;
    bool isTrapDetectedByParty() const { return _trapDetectedByParty; }
    /** A party detection is shared by the whole party and shows the trap marker. */
    void setTrapDetectedBy(uint32_t creatureId, bool byParty);
    bool isTrapFlagged() const { return _trapFlagged; }
    void setTrapFlagged(bool flagged) { _trapFlagged = flagged; }
    uint32_t lastDisarmedBy() const { return _lastDisarmedBy; }

    /** A trap fires on this creature when it enters (hostility rule). */
    bool trapFiresOn(const Object &creature) const;
    /** Runs OnTrapTriggered (or the traps.2da script); a one-shot trap is then removed. */
    void fireTrap(uint32_t victimId);
    /** Disarmed or recovered: OnDisarm runs, then the trap is removed. */
    void disarmTrap(uint32_t disarmerId);
    /** Turns the trap off and removes it (SetTrapDisabled). */
    void disableTrap();

    /** A mine laid by a creature: a 4 × 4 m trap trigger centred on position. */
    void configureMine(int trapRow, const glm::vec3 &position, Faction faction,
                       uint32_t creatorId, int detectDC, int disarmDC);
    void removeTrapMarker();

    /**
     * cursors.2da row shown over the trigger: row 1 for a transition (Type 1),
     * row 10 for a trap (Type 2), otherwise the UTT Cursor.
     */
    int cursorRow() const;

    bool isSelectable() const override;
    glm::vec3 getSelectablePosition() const override;

    const std::string &linkedToModule() const { return _linkedToModule; }
    const std::string &linkedTo() const { return _linkedTo; }
    uint8_t linkedToFlags() const { return _linkedToFlags; }
    const std::string &transitionDestin() const { return _transitionDestin.str(); }

private:
    friend class ModuleSnapshotBuilder;
    friend class TestGameModule;
    // Serializable
    std::string _onEnter;
    std::string _onExit;
    std::string _onClick;
    std::string _onDisarm;
    std::string _onTrapTriggered;

    uint8_t _trapType {0};
    bool _trapOneShot {true};
    std::string _linkedTo;
    uint8_t _linkedToFlags {0};
    std::string _linkedToModule;
    bool _autoRemoveKey {false};
    resource::LocString _locName;
    Faction _faction {Faction::Invalid};
    std::string _keyName;
    bool _trapDisarmable {false};
    bool _trapDetectable {false};
    int32_t _triggerType {0};
    float _highlightHeight {0.1f};
    uint16_t _loadScreenId {0};
    resource::LocString _transitionDestin;
    bool _setByPlayerParty {false};
    std::vector<glm::vec3> _geometry;
    uint8_t _trapFlag {0};
    uint8_t _trapDetectDC {0};
    uint8_t _disarmDC {0};
    uint32_t _creatorId {script::kObjectInvalid};
    bool _trapDetectedByParty {false};
    bool _trapFlagged {false};
    uint8_t _cursor {0};
    // END Serializable

    std::set<uint32_t> _trapDetectedBy;
    uint32_t _lastDisarmedBy {script::kObjectInvalid};
    std::shared_ptr<scene::ModelSceneNode> _trapMarker;

    std::set<std::shared_ptr<Object>> _tenants;
    RuntimeObjectRef<Door> _linkedDoor;
    bool _linkedDoorTransition {false};
    float _debugTestAge {0.0f};
    float _debugInsideAge {0.0f};
    float _debugEnterAge {0.0f};

    void deserializeAll(
        const resource::Gff &gff,
        const SerializedIdentityContext &identityContext);
    void loadAppearance();

    void syncDebugVisual();
    void showTrapMarker();
    bool partyHoldsTrapKey() const;
    void removeTrapFromArea();
};

} // namespace game

} // namespace reone
