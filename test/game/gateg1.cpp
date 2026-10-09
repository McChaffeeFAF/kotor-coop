/*
 * Copyright (c) 2026 The reone project contributors
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

// Gate batch G1: the end_m01aa routines and the six end_m01ab adds.
// Behaviour notes: design note gate_g1.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/action/castfakespellatobject.h"
#include "reone/game/action/equipmostdamagingmelee.h"
#include "reone/game/action/equipmostdamagingranged.h"
#include "reone/game/action/moveawayfromobject.h"
#include "reone/game/action/randomwalk.h"
#include "reone/game/action/wait.h"
#include "reone/game/areamusic.h"
#include "reone/game/d20/spell.h"
#include "reone/game/effect/beam.h"
#include "reone/game/effect/choke.h"
#include "reone/game/effect/forcepushtargeted.h"
#include "reone/game/effect/linkeffects.h"
#include "reone/game/effect/resurrection.h"
#include "reone/game/effect/stunned.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/mapexploration.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/weaponchoice.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"
#include "reone/system/randomutil.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

/** Routine table bound to a game, as the VM calls it. */
class G1Harness : boost::noncopyable {
public:
    G1Harness() :
        _game(GameID::KotOR, "", testEngine().options(), testEngine().services(), _console),
        _routines(GameID::KotOR, &_game, &testEngine().services()) {
        _routines.init();
    }

    Variable call(const std::string &name, std::vector<Variable> args, std::vector<Argument> scriptArgs = {}) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        ExecutionContext execution;
        execution.routines = &_routines;
        execution.args = std::move(scriptArgs);
        return routine.invoke(args, execution);
    }

    Variable callAs(const std::shared_ptr<Object> &caller, const std::string &name, std::vector<Variable> args,
                    std::vector<Argument> scriptArgs = {}) {
        scriptArgs.insert(scriptArgs.begin(), Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        return call(name, std::move(args), std::move(scriptArgs));
    }

    /** Run a routine pair that keeps iteration state in one execution context. */
    ExecutionContext &context() { return _execution; }

    Variable callIn(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines.get(_routines.getIndexByName(name));
        _execution.routines = &_routines;
        return routine.invoke(args, _execution);
    }

    Game &game() { return _game; }

private:
    StubConsole _console;
    Game _game;
    Routines _routines;
    ExecutionContext _execution;
};

/** An area installed as the game's current module area. */
std::shared_ptr<Area> installArea(Game &game) {
    auto area = game.newArea();
    TestGameModule::setActiveModuleArea(game, area);
    return area;
}

std::shared_ptr<Creature> killed(Game &game) {
    auto creature = game.newCreature();
    creature->damage(1, nullptr);
    return creature;
}

class CountingEffect : public Effect {
public:
    CountingEffect() :
        Effect(EffectType::Haste) {}

    void applyTo(Object &) override { ++applied; }
    void onRemove(Object &, const EffectInstance &) override { ++removed; }

    int applied {0};
    int removed {0};
};

} // namespace

// Presentation-only and debug routines.

TEST(GateG1Presentation, rumble_pattern_returns_zero) {
    G1Harness harness;
    EXPECT_EQ(0, harness.call("PlayRumblePattern", {Variable::ofInt(16)}).intValue);
}

TEST(GateG1Presentation, subscreen_id_is_none_outside_the_menu) {
    G1Harness harness;
    EXPECT_EQ(0, harness.call("GetSubScreenID", {}).intValue);
    EXPECT_EQ(0, harness.game().subScreenId());
}

TEST(GateG1Presentation, tutorial_window_pops_once) {
    TestEngine &engine = testEngine();
    TwoDA::Builder builder;
    builder.columns({"label", "message0", "message1", "message2", "icon"});
    builder.row({"Use_Combat_Feat", "48559", "", "", ""});
    builder.row({"Use_Grenade", "", "", "", ""});
    builder.row({"Movement_Keys", "42000", "", "", ""});
    std::shared_ptr<TwoDA> table(builder.build());
    EXPECT_CALL(engine.resourceModule().twoDas(), get("tutorial"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(table));
    EXPECT_CALL(engine.resourceModule().strings(), getText(42000))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::string("Use the movement keys.")));

    G1Harness harness;
    EXPECT_FALSE(harness.game().wasTutorialWindowShown(2));
    harness.call("ShowTutorialWindow", {Variable::ofInt(2)});
    EXPECT_TRUE(harness.game().wasTutorialWindowShown(2));
    // Second call for the same window does nothing.
    EXPECT_FALSE(harness.game().showTutorialWindow(2));
    // A row without a message is never shown.
    EXPECT_FALSE(harness.game().showTutorialWindow(1));
    EXPECT_FALSE(harness.game().wasTutorialWindowShown(1));
    // Out of range.
    EXPECT_FALSE(harness.game().showTutorialWindow(40));

    Mock::VerifyAndClearExpectations(&engine.resourceModule().twoDas());
    engine.resourceModule().twoDas().allowAnyLookup();
    Mock::VerifyAndClearExpectations(&engine.resourceModule().strings());
}

TEST(GateG1Presentation, return_strref_is_stored_on_the_module) {
    G1Harness harness;
    installArea(harness.game());
    auto module = harness.game().module();
    ASSERT_TRUE(module);
    EXPECT_TRUE(module->returnOption().show);

    harness.call("SetReturnStrref", {Variable::ofInt(0), Variable::ofInt(32228), Variable::ofInt(0)});
    EXPECT_FALSE(module->returnOption().show);
    EXPECT_EQ(32228, module->returnOption().strref);
    EXPECT_EQ(0, module->returnOption().queryStrref);

    harness.call("SetReturnStrref", {Variable::ofInt(1), Variable::ofInt(100), Variable::ofInt(200)});
    EXPECT_TRUE(module->returnOption().show);
    EXPECT_EQ(200, module->returnOption().queryStrref);
}

// Map exploration.

TEST(GateG1Map, reveal_all_and_radius) {
    MapExploration grid;
    grid.reset(16, 8);
    EXPECT_EQ(0, grid.exploredCount());

    // Radius 1 around the centre cell is a diamond of five cells.
    grid.reveal(glm::vec2(0.5f, 0.5f), 1);
    EXPECT_EQ(5, grid.exploredCount());
    EXPECT_TRUE(grid.isExplored(8, 4));
    EXPECT_TRUE(grid.isExplored(7, 4));
    EXPECT_TRUE(grid.isExplored(8, 3));
    EXPECT_FALSE(grid.isExplored(7, 3));

    // Points off the map clamp to its edge.
    grid.reveal(glm::vec2(-3.0f, 9.0f), 0);
    EXPECT_TRUE(grid.isExplored(0, 7));

    grid.revealAll();
    EXPECT_EQ(16 * 8, grid.exploredCount());
}

TEST(GateG1Map, reveal_map_routine_reveals_the_whole_area) {
    G1Harness harness;
    auto area = installArea(harness.game());
    area->mapExploration().reset(4, 2);
    harness.call("RevealMap", {Variable::ofVector(glm::vec3(0.0f)), Variable::ofInt(-1)});
    EXPECT_EQ(8, area->mapExploration().exploredCount());
}

// Music.

TEST(GateG1Music, battle_music_takes_precedence) {
    AreaMusic music;
    music.setTracks("mus_area_endar", "mus_bat_endar");
    EXPECT_EQ("mus_area_endar", music.current());
    music.setBattlePlaying(true);
    EXPECT_EQ("mus_bat_endar", music.current());
    music.setBackgroundPlaying(false);
    EXPECT_EQ("mus_bat_endar", music.current());
    // Ending battle music hands the area back to its background music
    // (design note c04_audio), even though a script stopped it.
    music.setBattlePlaying(false);
    EXPECT_EQ("mus_area_endar", music.current());
    music.setBackgroundPlaying(false);
    EXPECT_EQ("", music.current());
    music.setBackgroundPlaying(true);
    EXPECT_EQ("mus_area_endar", music.current());
}

TEST(GateG1Music, routines_switch_the_area_controller) {
    G1Harness harness;
    auto area = installArea(harness.game());
    area->musicState().setTracks("background", "battle");

    // k_pend_camera: stop the level theme, start battle music.
    harness.call("MusicBackgroundStop", {Variable::ofObject(area->id())});
    harness.call("MusicBattlePlay", {Variable::ofObject(area->id())});
    EXPECT_FALSE(area->musicState().isBackgroundPlaying());
    EXPECT_TRUE(area->musicState().isBattlePlaying());
    EXPECT_EQ("battle", area->music());

    // k_pend_cut1_end: the reverse.
    harness.call("MusicBattleStop", {Variable::ofObject(area->id())});
    harness.call("MusicBackgroundPlay", {Variable::ofObject(area->id())});
    EXPECT_EQ("background", area->music());
}

// Dialogue camera and participants.

TEST(GateG1Dialog, placeable_camera_needs_a_running_dialog) {
    G1Harness harness;
    installArea(harness.game());
    EXPECT_FALSE(harness.game().setDialogPlaceableCamera(19));
    harness.call("SetDialogPlaceableCamera", {Variable::ofInt(19)});
}

TEST(GateG1Dialog, orientation_lock_is_kept_per_dialog) {
    G1Harness harness;
    auto trask = harness.game().newCreature();
    auto door = harness.game().newItem();

    harness.call("SetLockOrientationInDialog", {Variable::ofObject(trask->id()), Variable::ofInt(1)});
    harness.call("SetLockHeadFollowInDialog", {Variable::ofObject(trask->id()), Variable::ofInt(1)});
    EXPECT_TRUE(harness.game().dialogLocks().isOrientationLocked(trask->id()));
    EXPECT_TRUE(harness.game().dialogLocks().isHeadFollowLocked(trask->id()));

    // Only creatures are locked.
    harness.call("SetLockOrientationInDialog", {Variable::ofObject(door->id()), Variable::ofInt(1)});
    EXPECT_FALSE(harness.game().dialogLocks().isOrientationLocked(door->id()));

    harness.call("SetLockHeadFollowInDialog", {Variable::ofObject(trask->id()), Variable::ofInt(0)});
    EXPECT_FALSE(harness.game().dialogLocks().isHeadFollowLocked(trask->id()));
    EXPECT_TRUE(harness.game().dialogLocks().isOrientationLocked(trask->id()));
}

TEST(GateG1Dialog, reset_dialog_state_releases_a_held_fade) {
    G1Harness harness;
    harness.game().globalFade().holdForDialog();
    ASSERT_TRUE(harness.game().globalFade().heldForDialog());
    harness.call("ResetDialogState", {});
    EXPECT_FALSE(harness.game().globalFade().heldForDialog());
}

// Creature state.

TEST(GateG1Creature, destroyable_flags) {
    G1Harness harness;
    auto henchman = harness.game().newCreature();
    EXPECT_TRUE(henchman->isDestroyable());
    EXPECT_TRUE(henchman->isRaiseable());
    EXPECT_FALSE(henchman->isSelectableWhenDead());

    harness.callAs(henchman, "SetIsDestroyable", {Variable::ofInt(0), Variable::ofInt(0), Variable::ofInt(1)});
    EXPECT_FALSE(henchman->isDestroyable());
    EXPECT_FALSE(henchman->isRaiseable());
    EXPECT_TRUE(henchman->isSelectableWhenDead());

    // A corpse with nothing to loot is still selectable when flagged.
    henchman->damage(1, nullptr);
    ASSERT_TRUE(henchman->isDead());
    EXPECT_TRUE(henchman->isSelectable());
}

TEST(GateG1Creature, lightsaber_override_persists) {
    G1Harness harness;
    auto jedi = harness.game().newCreature();
    EXPECT_FALSE(jedi->lightsaberPowerOverride());
    harness.call("SetLightsaberPowered",
                 {Variable::ofObject(jedi->id()), Variable::ofInt(1), Variable::ofInt(1), Variable::ofInt(0)});
    ASSERT_TRUE(jedi->lightsaberPowerOverride());
    EXPECT_TRUE(*jedi->lightsaberPowerOverride());

    harness.call("SetLightsaberPowered",
                 {Variable::ofObject(jedi->id()), Variable::ofInt(0), Variable::ofInt(1), Variable::ofInt(0)});
    EXPECT_FALSE(jedi->lightsaberPowerOverride());
}

TEST(GateG1Creature, stealth_xp_awarded_once) {
    G1Harness harness;
    auto area = installArea(harness.game());
    auto pc = harness.game().newCreature();
    area->setStealthXPEnabled(true);
    area->setMaxStealthXP(80);
    area->setCurrentStealthXP(60);
    EXPECT_EQ(1, harness.call("GetStealthXPEnabled", {}).intValue);

    int before = harness.game().party().xp();
    harness.call("AwardStealthXP", {Variable::ofObject(pc->id())});
    EXPECT_EQ(before + 60, harness.game().party().xp());
    EXPECT_EQ(0, area->currentStealthXP());
    EXPECT_FALSE(area->isStealthXPEnabled());

    // Only once.
    harness.call("AwardStealthXP", {Variable::ofObject(pc->id())});
    EXPECT_EQ(before + 60, harness.game().party().xp());
}

TEST(GateG1Creature, stealth_xp_needs_a_creature_target) {
    G1Harness harness;
    auto area = installArea(harness.game());
    auto item = harness.game().newItem();
    area->setStealthXPEnabled(true);
    area->setCurrentStealthXP(40);
    harness.call("AwardStealthXP", {Variable::ofObject(item->id())});
    EXPECT_EQ(40, area->currentStealthXP());
}

// Persistent objects.

TEST(GateG1Persistent, iterates_trigger_tenants) {
    G1Harness harness;
    auto zone = harness.game().newTrigger();
    auto first = harness.game().newCreature();
    auto second = harness.game().newCreature();
    auto item = harness.game().newItem();
    zone->addTenant(second);
    zone->addTenant(item);
    zone->addTenant(first);

    std::vector<Variable> args {Variable::ofObject(zone->id()), Variable::ofInt(1), Variable::ofInt(0)};
    std::set<uint32_t> seen;
    uint32_t id = harness.callIn("GetFirstInPersistentObject", args).objectId;
    while (id != kObjectInvalid) {
        seen.insert(id);
        id = harness.callIn("GetNextInPersistentObject", args).objectId;
    }
    EXPECT_EQ((std::set<uint32_t> {first->id(), second->id()}), seen);

    // First restarts; ids come in ascending order.
    EXPECT_EQ(std::min(first->id(), second->id()),
              harness.callIn("GetFirstInPersistentObject", args).objectId);
}

TEST(GateG1Persistent, non_persistent_objects_hold_nothing) {
    G1Harness harness;
    auto creature = harness.game().newCreature();
    EXPECT_EQ(kObjectInvalid,
              harness.call("GetFirstInPersistentObject",
                           {Variable::ofObject(creature->id()), Variable::ofInt(1), Variable::ofInt(0)})
                  .objectId);
    EXPECT_EQ(kObjectInvalid,
              harness.call("GetNextInPersistentObject",
                           {Variable::ofObject(creature->id()), Variable::ofInt(1), Variable::ofInt(0)})
                  .objectId);
}

// Events.

TEST(GateG1Events, item_activated_reads_event_arguments) {
    G1Harness harness;
    auto pc = harness.game().newCreature();
    auto kit = harness.game().newItem();
    EXPECT_EQ(kObjectInvalid, harness.call("GetItemActivated", {}).objectId);
    EXPECT_EQ(kObjectInvalid, harness.call("GetItemActivator", {}).objectId);

    std::vector<Argument> eventArgs {
        {ArgKind::ItemActivated, Variable::ofObject(kit->id())},
        {ArgKind::ItemActivator, Variable::ofObject(pc->id())}};
    EXPECT_EQ(kit->id(), harness.call("GetItemActivated", {}, eventArgs).objectId);
    EXPECT_EQ(pc->id(), harness.call("GetItemActivator", {}, eventArgs).objectId);
}

TEST(GateG1Events, item_activation_args_parse_from_strings) {
    Argument activated = Argument::fromString("ItemActivated:42");
    EXPECT_EQ(ArgKind::ItemActivated, activated.kind);
    EXPECT_EQ(42u, activated.var.objectId);
    Argument activator = Argument::fromString("ItemActivator:7");
    EXPECT_EQ(ArgKind::ItemActivator, activator.kind);
    EXPECT_EQ(7u, activator.var.objectId);
    EXPECT_THAT(activated.toString(), HasSubstr("ItemActivated"));
}

// Effects.

TEST(GateG1Effects, resurrection_raises_the_dead_with_one_hit_point) {
    G1Harness harness;
    auto carth = killed(harness.game());
    ASSERT_TRUE(carth->isDead());
    carth->applyEffect(std::make_shared<ResurrectionEffect>(0), DurationType::Instant);
    EXPECT_FALSE(carth->isDead());
    EXPECT_EQ(1, carth->currentHitPoints());
}

TEST(GateG1Effects, resurrection_leaves_the_unraiseable_and_the_living) {
    G1Harness harness;
    auto corpse = killed(harness.game());
    corpse->setDestroyable(true, /*raiseable=*/false, false);
    corpse->applyEffect(std::make_shared<ResurrectionEffect>(0), DurationType::Instant);
    EXPECT_TRUE(corpse->isDead());

    auto living = harness.game().newCreature();
    living->setMaxHitPoints(20);
    living->setCurrentHitPoints(12);
    living->applyEffect(std::make_shared<ResurrectionEffect>(0), DurationType::Instant);
    EXPECT_EQ(12, living->currentHitPoints());
}

TEST(GateG1Effects, choke_holds_the_creature) {
    G1Harness harness;
    auto trask = harness.game().newCreature();
    ASSERT_FALSE(trask->isMovementRestricted());
    auto choke = std::make_shared<ChokeEffect>();
    trask->applyEffect(choke, DurationType::Permanent);
    EXPECT_TRUE(trask->isChoked());
    // Held: no actions, so no movement either.
    EXPECT_TRUE(trask->isMovementRestricted());
    trask->removeEffect(choke);
    EXPECT_FALSE(trask->isChoked());
    EXPECT_FALSE(trask->isMovementRestricted());
}

TEST(GateG1Effects, force_push_targeted_throws_away_from_the_centre) {
    G1Harness harness;
    auto soldier = harness.game().newCreature();
    soldier->setPosition(glm::vec3(1.0f, 0.0f, 0.0f));
    auto centre = std::make_shared<Location>(glm::vec3(0.0f), 0.0f);
    soldier->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, false), DurationType::Instant);
    // The throw advances in the simulation tick (c08effects.cpp covers it).
    for (int i = 0; i < 30; ++i) {
        soldier->update(1.0f / 30.0f);
    }
    EXPECT_NEAR(1.0f + ForcePushTargetedEffect::kPushDistance, soldier->position().x, 1e-3f);
    EXPECT_NEAR(0.0f, soldier->position().y, 1e-4f);

    // Facing the centre: (-sin f, cos f) points back at the origin.
    float facing = soldier->getFacing();
    EXPECT_NEAR(-1.0f, -std::sin(facing), 1e-4f);
}

TEST(GateG1Effects, force_push_targeted_leaves_the_dead) {
    G1Harness harness;
    auto corpse = killed(harness.game());
    corpse->setPosition(glm::vec3(2.0f, 0.0f, 0.0f));
    auto centre = std::make_shared<Location>(glm::vec3(0.0f), 0.0f);
    corpse->applyEffect(std::make_shared<ForcePushTargetedEffect>(centre, true), DurationType::Instant);
    for (int i = 0; i < 30; ++i) {
        corpse->update(1.0f / 30.0f);
    }
    EXPECT_FLOAT_EQ(2.0f, corpse->position().x);
}

TEST(GateG1Effects, link_applies_and_removes_both) {
    G1Harness harness;
    auto target = harness.game().newCreature();
    auto counting = std::make_shared<CountingEffect>();
    auto link = std::make_shared<LinkEffectsEffect>(std::make_shared<StunnedEffect>(), counting);
    target->applyEffect(link, DurationType::Permanent);
    EXPECT_EQ(1, counting->applied);
    // A linked stun still stops the creature.
    EXPECT_TRUE(target->hasEffect(EffectType::Stunned));
    EXPECT_TRUE(target->isMovementRestricted());

    target->removeEffect(link);
    EXPECT_EQ(1, counting->removed);
    EXPECT_FALSE(target->hasEffect(EffectType::Stunned));
}

TEST(GateG1Effects, instant_link_applies_its_children_once) {
    G1Harness harness;
    auto target = harness.game().newCreature();
    auto counting = std::make_shared<CountingEffect>();
    auto nested = std::make_shared<LinkEffectsEffect>(counting, nullptr);
    target->applyEffect(std::make_shared<LinkEffectsEffect>(nested, nullptr), DurationType::Instant);
    EXPECT_EQ(1, counting->applied);
    EXPECT_EQ(1, counting->removed);
    EXPECT_TRUE(target->effects().empty());
}

TEST(GateG1Effects, beam_applies_without_gameplay_change) {
    G1Harness harness;
    auto target = harness.game().newCreature();
    target->setMaxHitPoints(10);
    target->setCurrentHitPoints(10);
    auto beam = std::make_shared<BeamEffect>(2038, nullptr, BodyNode::Hand, false);
    target->applyEffect(beam, DurationType::Temporary, 1.0f);
    EXPECT_EQ(10, target->currentHitPoints());
    EXPECT_EQ(1u, target->effects().size());
    target->removeEffect(beam);
    EXPECT_TRUE(target->effects().empty());
}

// Actions.

namespace {

std::shared_ptr<TwoDA> makeWeaponTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 12; ++i) {
        if (i == 1) {
            builder.row({"1.5", "2", "2", "2", "4", "48", "w_stunbaton", "1", "1", "1", "-1", ""});
        } else if (i == 3) {
            builder.row({"1.5", "2", "2", "2", "6", "48", "w_vbroswrd", "1", "1", "2", "-1", ""});
        } else if (i == 8) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == 10) {
            builder.row({"23", "2", "2", "2", "8", "16", "w_blstrrfl", "1", "4", "5", "-1", ""});
        } else if (i == 12) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Item> makeWeapon(Game &game, std::string tag, int baseItem) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", 1))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    item->setDropable(true);
    return item;
}

class GateG1Weapons : public Test {
protected:
    G1Harness harness;

    void SetUp() override {
        EXPECT_CALL(testEngine().resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeWeaponTable()));
    }

    void TearDown() override {
        Mock::VerifyAndClearExpectations(&testEngine().resourceModule().twoDas());
        testEngine().resourceModule().twoDas().allowAnyLookup();
    }
};

} // namespace

TEST_F(GateG1Weapons, equip_most_damaging_picks_by_kind) {
    auto soldier = harness.game().newCreature();
    auto baton = makeWeapon(harness.game(), "baton", 1);
    auto saber = makeWeapon(harness.game(), "saber", 8);
    auto pistol = makeWeapon(harness.game(), "pistol", 12);
    auto rifle = makeWeapon(harness.game(), "rifle", 10);
    soldier->addItem(baton);
    soldier->addItem(saber);
    soldier->addItem(pistol);
    soldier->addItem(rifle);

    auto melee = chooseMostDamagingWeapon(*soldier, WeaponKind::Melee, false);
    ASSERT_TRUE(melee);
    EXPECT_EQ(saber, melee->item);
    EXPECT_EQ(InventorySlots::rightWeapon, melee->slot);

    auto ranged = chooseMostDamagingWeapon(*soldier, WeaponKind::Ranged, false);
    ASSERT_TRUE(ranged);
    EXPECT_EQ(rifle, ranged->item);

    // The off hand takes only melee weapons that fit it.
    auto offHand = chooseMostDamagingWeapon(*soldier, WeaponKind::Melee, true);
    ASSERT_TRUE(offHand);
    EXPECT_EQ(saber, offHand->item);
    EXPECT_EQ(InventorySlots::leftWeapon, offHand->slot);
}

TEST_F(GateG1Weapons, equip_most_damaging_falls_back_to_the_other_kind) {
    auto soldier = harness.game().newCreature();
    auto baton = makeWeapon(harness.game(), "baton", 1);
    soldier->addItem(baton);
    auto choice = chooseMostDamagingWeapon(*soldier, WeaponKind::Ranged, false);
    ASSERT_TRUE(choice);
    EXPECT_EQ(baton, choice->item);

    auto unarmed = harness.game().newCreature();
    EXPECT_FALSE(chooseMostDamagingWeapon(*unarmed, WeaponKind::Melee, false));
}

TEST_F(GateG1Weapons, equip_most_damaging_actions_equip_the_weapon) {
    auto soldier = harness.game().newCreature();
    auto saber = makeWeapon(harness.game(), "saber", 8);
    auto pistol = makeWeapon(harness.game(), "pistol", 12);
    soldier->addItem(saber);
    soldier->addItem(pistol);

    auto melee = harness.game().newAction<EquipMostDamagingMeleeAction>(nullptr, false);
    melee->execute(melee, *soldier, 0.0f);
    EXPECT_TRUE(melee->isCompleted());
    EXPECT_EQ(saber, soldier->getEquippedItem(InventorySlots::rightWeapon));

    auto ranged = harness.game().newAction<EquipMostDamagingRangedAction>(nullptr);
    ranged->execute(ranged, *soldier, 0.0f);
    EXPECT_TRUE(ranged->isCompleted());
    EXPECT_EQ(pistol, soldier->getEquippedItem(InventorySlots::rightWeapon));

    // Already holding the best: nothing to do.
    auto again = harness.game().newAction<EquipMostDamagingRangedAction>(nullptr);
    again->execute(again, *soldier, 0.0f);
    EXPECT_TRUE(again->isCompleted());
    EXPECT_EQ(pistol, soldier->getEquippedItem(InventorySlots::rightWeapon));
}

TEST(GateG1Actions, move_away_chooses_a_point_at_range) {
    G1Harness harness;
    auto coward = harness.game().newCreature();
    auto enemy = harness.game().newCreature();
    coward->setPosition(glm::vec3(2.0f, 0.0f, 0.0f));
    enemy->setPosition(glm::vec3(0.0f));

    auto action = harness.game().newAction<MoveAwayFromObject>(enemy, true, 20.0f);
    action->execute(action, *coward, 1.0f / 30.0f);
    ASSERT_TRUE(action->destination());
    EXPECT_NEAR(20.0f, action->destination()->x, 1e-4f);
    EXPECT_NEAR(0.0f, action->destination()->y, 1e-4f);
}

TEST(GateG1Actions, move_away_completes_when_already_far_enough) {
    G1Harness harness;
    auto coward = harness.game().newCreature();
    auto enemy = harness.game().newCreature();
    coward->setPosition(glm::vec3(30.0f, 0.0f, 0.0f));
    auto action = harness.game().newAction<MoveAwayFromObject>(enemy, false, 20.0f);
    action->execute(action, *coward, 1.0f / 30.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_FALSE(action->destination());
}

TEST(GateG1Actions, random_walk_stays_near_its_anchor) {
    setRandomSeed(7);
    G1Harness harness;
    auto walker = harness.game().newCreature();
    walker->setPosition(glm::vec3(10.0f, 10.0f, 0.0f));
    auto action = harness.game().newAction<RandomWalkAction>();
    for (int i = 0; i < 20; ++i) {
        action->execute(action, *walker, 1.0f / 30.0f);
        ASSERT_TRUE(action->anchor());
        EXPECT_EQ(glm::vec3(10.0f, 10.0f, 0.0f), *action->anchor());
        if (action->destination()) {
            float distance = glm::distance(glm::vec2(*action->destination()), glm::vec2(10.0f, 10.0f));
            EXPECT_LE(distance, RandomWalkAction::kRadius + 1e-4f);
        }
    }
    // It keeps walking until something else is queued.
    EXPECT_FALSE(action->isCompleted());
}

TEST(GateG1Actions, random_walk_is_removed_by_any_new_command) {
    G1Harness harness;
    auto walker = harness.game().newCreature();
    walker->addAction(harness.game().newAction<RandomWalkAction>());
    ASSERT_EQ(1u, walker->actions().size());

    // A new random walk replaces the old one.
    walker->addAction(harness.game().newAction<RandomWalkAction>());
    ASSERT_EQ(1u, walker->actions().size());

    walker->addAction(harness.game().newAction<WaitAction>(1.0f));
    ASSERT_EQ(1u, walker->actions().size());
    EXPECT_EQ(ActionType::Wait, walker->actions().front()->type());
}

TEST(GateG1Actions, random_walk_draws_from_the_gameplay_stream) {
    auto destinations = [](uint32_t seed) {
        setRandomSeed(seed);
        G1Harness harness;
        auto walker = harness.game().newCreature();
        auto action = harness.game().newAction<RandomWalkAction>();
        action->execute(action, *walker, 1.0f / 30.0f);
        return action->destination().value_or(glm::vec3(0.0f));
    };
    EXPECT_EQ(destinations(3), destinations(3));
}

TEST(GateG1Actions, fake_cast_runs_its_timeline) {
    TestEngine &engine = testEngine();
    auto spell = std::make_shared<Spell>();
    spell->type = static_cast<SpellType>(9);
    spell->conjTime = 0.1f;
    spell->castTime = 0.2f;
    EXPECT_CALL(engine.gameModule().spells(), get(static_cast<SpellType>(9)))
        .Times(AnyNumber())
        .WillRepeatedly(Return(spell));

    G1Harness harness;
    auto jedi = harness.game().newCreature();
    auto trask = harness.game().newCreature();
    trask->setPosition(glm::vec3(0.0f, 5.0f, 0.0f));
    int hitPoints = trask->currentHitPoints();

    auto action = harness.game().newAction<CastFakeSpellAtObjectAction>(
        static_cast<SpellType>(9), trask, ProjectilePathType::Default);
    int ticks = 0;
    while (!action->isCompleted() && ticks < 100) {
        action->execute(action, *jedi, 1.0f / 30.0f);
        if (!action->isCompleted()) {
            EXPECT_TRUE(jedi->isMovementRestricted());
        }
        ++ticks;
    }
    EXPECT_TRUE(action->isCompleted());
    // conjure 0.1 s + cast 0.2 s at 30 Hz
    EXPECT_GE(ticks, 9);
    EXPECT_LE(ticks, 11);
    EXPECT_FALSE(jedi->isMovementRestricted());
    EXPECT_EQ(hitPoints, trask->currentHitPoints());
    EXPECT_EQ(-1, jedi->getLastForcePower());

    Mock::VerifyAndClearExpectations(&engine.gameModule().spells());
}

TEST(GateG1Actions, fake_cast_of_an_unknown_spell_completes_at_once) {
    G1Harness harness;
    auto jedi = harness.game().newCreature();
    auto trask = harness.game().newCreature();
    auto action = harness.game().newAction<CastFakeSpellAtObjectAction>(
        static_cast<SpellType>(12345), trask, ProjectilePathType::Default);
    action->execute(action, *jedi, 1.0f / 30.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_FALSE(jedi->isMovementRestricted());
}
