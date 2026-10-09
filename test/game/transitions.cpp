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

// Area transitions: transition doors and walk-over transition triggers
// (design note transitions).
//
// Each test has its own engine fixture, so no mock state leaks between suites.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "../../src/libs/game/action/commonactions.h"
#include "reone/game/action/opendoor.h"
#include "reone/game/game.h"
#include "reone/game/messagelog.h"
#include "reone/graphics/walkmesh.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/scene/node/trigger.h"
#include "reone/script/program.h"

#include <algorithm>

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

void transitionSceneGraph(TestEngine &engine, NiceMock<scene::MockSceneGraph> &graph) {
    ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(graph));
    ON_CALL(graph, newTrigger(_))
        .WillByDefault(Invoke([&engine, &graph](std::vector<glm::vec3> geometry) {
            return std::make_shared<scene::TriggerSceneNode>(
                std::move(geometry),
                graph,
                engine.services().graphics,
                engine.services().audio,
                engine.services().resource);
        }));
    ON_CALL(graph, testWalk(_, _, _, _)).WillByDefault(Return(false));
    ON_CALL(graph, testElevation(_, _))
        .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
            collision.intersection = position;
            collision.intersection.z = 0.0f;
            collision.material = 0;
            collision.user = nullptr;
            return true;
        }));
}

std::shared_ptr<TwoDA> appearanceTable() {
    TwoDA::Builder builder;
    builder.columns({"modeltype", "walkdist", "rundist", "footsteptype", "envmap", "race", "racetex"});
    builder.row({"S", "1", "1", "-1", "", "", ""});
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<TwoDA> genericDoorsTable() {
    TwoDA::Builder builder;
    builder.columns({"modelname"});
    builder.row({""});
    return std::shared_ptr<TwoDA>(builder.build());
}

class TransitionsTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        transitionSceneGraph(_engine, _sceneGraph);
        ON_CALL(_engine.resourceModule().twoDas(), get("appearance")).WillByDefault(Return(appearanceTable()));
        ON_CALL(_engine.resourceModule().twoDas(), get("genericdoors")).WillByDefault(Return(genericDoorsTable()));

        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        ON_CALL(_engine.resourceModule().scripts(), get(_)).WillByDefault(Invoke([this](const std::string &resRef) {
            _scriptRequests.push_back(resRef);
            return std::shared_ptr<script::ScriptProgram>();
        }));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);

        _leader = newCreature();
        _leader->setPosition(glm::vec3(0.0f, -1.5f, 0.0f));
        _area->add(_leader);
        _game->party().addMember(kNpcPlayer, _leader);
        _game->party().setPlayer(_leader);
        _game->party().setActualPlayer(_leader);
    }

    std::shared_ptr<Creature> newCreature() {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newDword("Appearance_Type", 0))
                       .field(Gff::Field::newWord("SoundSetFile", 0xffff))
                       .field(Gff::Field::newByte("BodyBag", 0xff))
                       .field(Gff::Field::newByte("PerceptionRange", 0xff))
                       .build();
        auto creature = _game->newCreature();
        creature->deserialize(*gff, SerializedIdentityContext::templateResource());
        creature->setCurrentHitPoints(10);
        return creature;
    }

    /** A shut transition door, as the Taris elevator tar02_lowdoor is authored. */
    std::shared_ptr<Door> addTransitionDoor(bool locked, std::string onFailToOpen = "") {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newByte("GenericType", 0))
                       .field(Gff::Field::newByte("Static", 0))
                       .field(Gff::Field::newByte("Locked", locked ? 1 : 0))
                       .field(Gff::Field::newByte("Plot", 1))
                       .field(Gff::Field::newByte("OpenLockDC", 100))
                       .field(Gff::Field::newResRef("OnFailToOpen", std::move(onFailToOpen)))
                       .field(Gff::Field::newFloat("X", 0.0f))
                       .field(Gff::Field::newFloat("Y", 5.0f))
                       .field(Gff::Field::newFloat("Z", 0.0f))
                       .field(Gff::Field::newByte("LinkedToFlags", 1))
                       .field(Gff::Field::newResRef("LinkedToModule", "tar_m03aa"))
                       .field(Gff::Field::newCExoString("LinkedTo", "tar03_sw03aai"))
                       .build();
        auto door = _game->newDoor();
        door->deserialize(*gff, SerializedIdentityContext::templateResource());
        _area->add(door);
        return door;
    }

    /** A plain locked (non-plot, non-transition) door, close enough to use without walking. */
    std::shared_ptr<Door> addSecurityDoor(int openLockDC) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newByte("GenericType", 0))
                       .field(Gff::Field::newByte("Static", 0))
                       .field(Gff::Field::newByte("Locked", 1))
                       .field(Gff::Field::newByte("OpenLockDC", static_cast<uint8_t>(openLockDC)))
                       .field(Gff::Field::newFloat("X", 0.0f))
                       .field(Gff::Field::newFloat("Y", 0.0f))
                       .field(Gff::Field::newFloat("Z", 0.0f))
                       .build();
        auto door = _game->newDoor();
        door->deserialize(*gff, SerializedIdentityContext::templateResource());
        _area->add(door);
        return door;
    }

    /** A walk-over transition line: a 2 m square at the origin, Type 1. */
    std::shared_ptr<Trigger> addTransitionTrigger() {
        std::vector<std::shared_ptr<Gff>> geometry;
        for (const auto &point : std::vector<glm::vec2> {{-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, -1.0f}}) {
            geometry.push_back(Gff::Builder()
                                   .field(Gff::Field::newFloat("PointX", point.x))
                                   .field(Gff::Field::newFloat("PointY", point.y))
                                   .field(Gff::Field::newFloat("PointZ", 0.0f))
                                   .build());
        }
        auto gff = Gff::Builder()
                       .field(Gff::Field::newInt("Type", 1))
                       .field(Gff::Field::newByte("LinkedToFlags", 2))
                       .field(Gff::Field::newResRef("LinkedToModule", "danm14ab"))
                       .field(Gff::Field::newCExoString("LinkedTo", "from14aw"))
                       .field(Gff::Field::newList("Geometry", std::move(geometry)))
                       .build();
        auto trigger = _game->newTrigger();
        trigger->deserialize(*gff, SerializedIdentityContext::templateResource());
        trigger->setPosition(glm::vec3(0.0f));
        _area->add(trigger);
        return trigger;
    }

    /** One step north, from just below the trigger into it. No click. */
    bool walkNorth(const std::shared_ptr<Creature> &creature) {
        return _area->moveCreature(creature, glm::vec2(0.0f, 1.0f), false, 0.75f);
    }

    static std::pair<std::string, std::string> matale() {
        return {std::string("danm14ab"), std::string("from14aw")};
    }

    void click(const std::shared_ptr<Object> &object) {
        _game->module()->onObjectClick(object);
    }

    /**
     * The player clicks a door and the leader walks up to it: the walk is
     * cut short by standing the leader a metre from the door, then the
     * queued action runs.
     */
    void clickAndArrive(const std::shared_ptr<Door> &door) {
        click(door);
        _leader->setPosition(door->position() - glm::vec3(0.0f, 1.0f, 0.0f));
        for (int i = 0; i < 3; ++i) {
            _leader->update(1.0f / 30.0f);
        }
    }

    /** Door updates for the given simulation time, in 1/30 s steps. */
    void runDoor(Door &door, float seconds) {
        for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f) {
            door.update(1.0f / 30.0f);
        }
    }

    std::pair<std::string, std::string> scheduled() const {
        return TestGameModule::scheduledTransition(*_game);
    }

    static std::pair<std::string, std::string> none() {
        return {std::string(), std::string()};
    }

    static std::pair<std::string, std::string> lowerCity() {
        return {std::string("tar_m03aa"), std::string("tar03_sw03aai")};
    }

    TestEngine _engine;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _leader;
    std::vector<std::string> _scriptRequests;

    int requests(const std::string &resRef) const {
        return static_cast<int>(std::count(_scriptRequests.begin(), _scriptRequests.end(), resRef));
    }
};

} // namespace

// --- Transition doors --------------------------------------------------------

TEST_F(TransitionsTest, unlocked_transition_door_click_moves_the_party) {
    auto door = addTransitionDoor(/*locked=*/false);

    clickAndArrive(door);

    EXPECT_EQ(scheduled(), lowerCity());
}

TEST_F(TransitionsTest, locked_transition_door_click_does_not_move_the_party) {
    auto door = addTransitionDoor(/*locked=*/true);

    clickAndArrive(door);
    runDoor(*door, 1.0f);

    EXPECT_EQ(scheduled(), none());
    EXPECT_FALSE(door->isOpen());
}

// k_ptar_alertsith, the elevator's OnFailToOpen, opens the door for a party in
// Sith uniform without unlocking it. The leader's try goes on through.
TEST_F(TransitionsTest, locked_transition_door_opened_by_its_fail_script_moves_the_party) {
    auto door = addTransitionDoor(/*locked=*/true);

    clickAndArrive(door);
    ASSERT_EQ(scheduled(), none());
    door->open(); // what the fail-to-open script's ActionOpenDoor(OBJECT_SELF) does
    runDoor(*door, 0.5f);

    EXPECT_TRUE(door->isLocked());
    EXPECT_EQ(scheduled(), lowerCity());
}

// A later click on the open, still locked door uses it: retail checks the lock
// only on a shut door.
TEST_F(TransitionsTest, open_locked_transition_door_click_moves_the_party) {
    auto door = addTransitionDoor(/*locked=*/true);
    door->open();
    runDoor(*door, 0.5f);
    ASSERT_TRUE(door->isOpen());

    clickAndArrive(door);

    EXPECT_EQ(scheduled(), lowerCity());
}

TEST_F(TransitionsTest, locked_transition_door_opened_long_after_the_click_does_not_move_the_party) {
    auto door = addTransitionDoor(/*locked=*/true);

    clickAndArrive(door);
    runDoor(*door, 6.0f);
    door->open();
    runDoor(*door, 0.5f);

    EXPECT_EQ(scheduled(), none());
}

TEST_F(TransitionsTest, pending_door_use_is_dropped_when_another_member_leads) {
    auto door = addTransitionDoor(/*locked=*/true);
    auto companion = newCreature();
    _area->add(companion);
    _game->party().addAvailableMember(0, companion);
    _game->party().addMember(0, companion);

    clickAndArrive(door);
    _game->party().setPartyLeader(0);
    ASSERT_TRUE(_game->party().getLeader() == companion);
    door->open();
    runDoor(*door, 0.5f);

    EXPECT_EQ(scheduled(), none());
}

// Retail walks the leader to the exit first: a click from afar leaves nothing
// behind until the leader arrives (SWEEP_FINDINGS F3).
TEST_F(TransitionsTest, transition_door_click_from_afar_walks_before_it_moves_the_party) {
    auto door = addTransitionDoor(/*locked=*/false);

    click(door);

    EXPECT_EQ(scheduled(), none());
    ASSERT_FALSE(_leader->actions().empty());
    auto action = std::dynamic_pointer_cast<OpenDoorAction>(_leader->actions().front());
    ASSERT_TRUE(action);
    EXPECT_TRUE(action->usesTransition());
    EXPECT_EQ(action->door(), door);
}

// Nor does a locked exit's fail-to-open run until the leader stands at it.
TEST_F(TransitionsTest, locked_transition_door_fail_script_waits_for_the_leader) {
    auto door = addTransitionDoor(/*locked=*/true, "k_fail_open");
    _scriptRequests.clear();

    click(door);
    EXPECT_EQ(requests("k_fail_open"), 0);

    _leader->setPosition(door->position() - glm::vec3(0.0f, 1.0f, 0.0f));
    for (int i = 0; i < 3; ++i) {
        _leader->update(1.0f / 30.0f);
    }
    EXPECT_EQ(requests("k_fail_open"), 1);
    EXPECT_EQ(scheduled(), none());
}

// Test harness escape hatch: instantdoors takes the exit where the leader stands.
TEST_F(TransitionsTest, instant_door_transitions_skip_the_walk) {
    auto door = addTransitionDoor(/*locked=*/false);
    _game->setInstantDoorTransitions(true);

    click(door);

    EXPECT_EQ(scheduled(), lowerCity());
}

// --- Walk-over transition triggers -------------------------------------------

TEST_F(TransitionsTest, leader_walking_into_a_transition_line_moves_the_party_without_a_click) {
    auto trigger = addTransitionTrigger();

    ASSERT_TRUE(walkNorth(_leader));

    EXPECT_TRUE(trigger->isTenant(_leader));
    EXPECT_EQ(scheduled(), matale());
}

TEST_F(TransitionsTest, a_party_member_down_refuses_the_transition_line) {
    auto trigger = addTransitionTrigger();
    auto companion = newCreature();
    companion->setPosition(glm::vec3(5.0f, 5.0f, 0.0f));
    _area->add(companion);
    _game->party().addAvailableMember(0, companion);
    _game->party().addMember(0, companion);
    companion->setCurrentHitPoints(0); // down
    ASSERT_TRUE(companion->isDead());

    ASSERT_TRUE(walkNorth(_leader));

    EXPECT_EQ(scheduled(), none());
    EXPECT_EQ(requests("k_trg_transfail"), 1);
    // The leader stays inside: standing there does not retry.
    EXPECT_TRUE(trigger->isTenant(_leader));
}

TEST_F(TransitionsTest, a_follower_crossing_the_line_does_not_move_the_party) {
    addTransitionTrigger();
    auto companion = newCreature();
    companion->setPosition(glm::vec3(0.0f, -1.5f, 0.0f));
    _area->add(companion);
    _game->party().addAvailableMember(0, companion);
    _game->party().addMember(0, companion);

    ASSERT_TRUE(walkNorth(companion));

    EXPECT_EQ(scheduled(), none());
}

TEST_F(TransitionsTest, a_party_member_down_refuses_a_transition_door) {
    auto door = addTransitionDoor(/*locked=*/false);
    auto companion = newCreature();
    _area->add(companion);
    _game->party().addAvailableMember(0, companion);
    _game->party().addMember(0, companion);
    companion->setCurrentHitPoints(0); // down

    clickAndArrive(door);

    EXPECT_EQ(scheduled(), none());
}

// --- Walking up to a door --------------------------------------------------------

// A shut door stands in its doorway, often off the walkmesh (the Jedi Enclave
// dorm doors): the creature walks to the door walkmesh's use point on its own
// side instead, as retail does.
TEST_F(TransitionsTest, a_door_is_approached_at_the_use_point_on_the_actors_side) {
    auto walkmesh = std::make_shared<graphics::Walkmesh>();
    walkmesh->usePositions = {glm::vec3(0.0f, 0.85f, 0.0f), glm::vec3(0.0f, -0.85f, 0.0f)};
    ON_CALL(_engine.resourceModule().walkmeshes(), get("testdoor0", ResType::Dwk)).WillByDefault(Return(walkmesh));
    TwoDA::Builder doors;
    doors.columns({"modelname"});
    doors.row({"testdoor"});
    std::shared_ptr<TwoDA> table(doors.build());
    ON_CALL(_engine.resourceModule().twoDas(), get("genericdoors")).WillByDefault(Return(table));
    auto door = addTransitionDoor(/*locked=*/true); // at (0, 5), bearing 0

    auto uses = door->useWorldPositions();
    ASSERT_EQ(uses.size(), 2u);

    // The leader stands south of the door, at (0, -1.5).
    glm::vec3 approach(doorApproachPoint(*_game, *door, *_leader));
    EXPECT_NEAR(approach.x, 0.0f, 1e-4f);
    EXPECT_NEAR(approach.y, 5.0f - 0.85f, 1e-4f);
}

// --- Security: the retail lock roll -----------------------------------------

// Security no longer always succeeds: it rolls d20 (take 20 outside combat,
// as Demolitions does) plus skill rank against the lock's Open Lock DC
// (design note security_roll).

TEST_F(TransitionsTest, security_roll_fails_below_the_lock_dc_and_leaves_it_locked) {
    auto door = addSecurityDoor(/*openLockDC=*/25); // take 20 + rank 0 = 20, short of 25
    _leader->attributes().setSkillRank(SkillType::Security, 0);
    bool doorApproach = false;

    EXPECT_TRUE(unlockDoor(*_game, *door, *_leader, doorApproach, 2.0f, 1.0f / 30.0f));

    EXPECT_TRUE(door->isLocked());
    EXPECT_FALSE(door->isOpen());
    EXPECT_THAT(_game->messageLog().entries(), Not(IsEmpty()));
}

TEST_F(TransitionsTest, security_roll_succeeds_at_or_above_the_lock_dc) {
    auto door = addSecurityDoor(/*openLockDC=*/20); // take 20 + rank 0 = 20, meets 20
    _leader->attributes().setSkillRank(SkillType::Security, 0);
    bool doorApproach = false;

    EXPECT_TRUE(unlockDoor(*_game, *door, *_leader, doorApproach, 2.0f, 1.0f / 30.0f));

    EXPECT_FALSE(door->isLocked());
    EXPECT_TRUE(door->isOpen());
}

// A failed attempt does not consume anything: raising Security and trying
// again (the retry the brief asks for) can still open the same lock.
TEST_F(TransitionsTest, security_roll_can_be_retried_after_a_failure) {
    auto door = addSecurityDoor(/*openLockDC=*/25);
    _leader->attributes().setSkillRank(SkillType::Security, 0);
    bool doorApproach = false;

    ASSERT_TRUE(unlockDoor(*_game, *door, *_leader, doorApproach, 2.0f, 1.0f / 30.0f));
    ASSERT_TRUE(door->isLocked());

    _leader->attributes().setSkillRank(SkillType::Security, 10); // take 20 + 10 = 30 >= 25
    doorApproach = false;

    EXPECT_TRUE(unlockDoor(*_game, *door, *_leader, doorApproach, 2.0f, 1.0f / 30.0f));

    EXPECT_FALSE(door->isLocked());
    EXPECT_TRUE(door->isOpen());
}

// A door requiring a named key is untouched by the Security roll (K1's
// scripted/key-gated plot doors, doorpass_sweep's "key" and "script" classes).
TEST_F(TransitionsTest, a_key_required_door_bypasses_the_security_roll) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newByte("GenericType", 0))
                   .field(Gff::Field::newByte("Static", 0))
                   .field(Gff::Field::newByte("Locked", 1))
                   .field(Gff::Field::newByte("KeyRequired", 1))
                   .field(Gff::Field::newCExoString("KeyName", "test_key"))
                   .field(Gff::Field::newByte("OpenLockDC", 100))
                   .field(Gff::Field::newFloat("X", 0.0f))
                   .field(Gff::Field::newFloat("Y", 0.0f))
                   .field(Gff::Field::newFloat("Z", 0.0f))
                   .build();
    auto door = _game->newDoor();
    door->deserialize(*gff, SerializedIdentityContext::templateResource());
    _area->add(door);
    bool doorApproach = false;

    EXPECT_TRUE(unlockDoor(*_game, *door, *_leader, doorApproach, 2.0f, 1.0f / 30.0f));

    // Neither opened nor failed-with-feedback: a key-gated door is simply
    // left alone by Security (unaffected by this change).
    EXPECT_TRUE(door->isLocked());
    EXPECT_THAT(_game->messageLog().entries(), IsEmpty());
}

