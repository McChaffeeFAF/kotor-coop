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

// NWScript routines implemented in tranche 4 (docs/routines_missing.md): the
// remaining string, message, creature, party, item and placeable routines.
// Where the public documentation is silent the chosen behaviour is stated in
// the test and in docs/routines_missing.md.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/effect/savingthrowincrease.h"
#include "reone/game/event.h"
#include "reone/game/game.h"
#include "reone/game/journal.h"
#include "reone/game/party.h"
#include "reone/game/types.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/door.h"
#include "reone/game/object/encounter.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/trigger.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

class Tranche4Routines : public TestWithParam<GameID> {
protected:
    void SetUp() override {
        engine.init();
        for (const char *table : {"genericdoors", "placeables"}) {
            TwoDA::Builder builder;
            builder.columns({"modelname"});
            builder.row({""});
            ON_CALL(engine.resourceModule().twoDas(), get(std::string(table)))
                .WillByDefault(Return(std::shared_ptr<TwoDA>(builder.build())));
        }
        TwoDA::Builder baseItems;
        baseItems.columns({"weapontype", "itemclass"});
        baseItems.row({"0", "armor"});
        baseItems.row({"4", "pistol"}); // WeaponType::Ranged
        ON_CALL(engine.resourceModule().twoDas(), get(std::string("baseitems")))
            .WillByDefault(Return(std::shared_ptr<TwoDA>(baseItems.build())));

        game = std::make_unique<Game>(GetParam(), std::filesystem::path {}, engine.options(), engine.services(), console);
        game->initLocalServices();
        ON_CALL(engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(sceneGraph));
        routines = std::make_unique<Routines>(GetParam(), game.get(), &engine.services());
        routines->init();
        area = game->newArea();
        TestGameModule::setActiveModuleArea(*game, area);
    }

    std::shared_ptr<Creature> addCreature(const glm::vec3 &position = glm::vec3(0.0f)) {
        auto creature = game->newCreature();
        creature->setPosition(position);
        area->add(creature);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        int index = routines->getIndexByName(name);
        EXPECT_GE(index, 0) << name << " is not registered";
        return routines->get(index).invoke(args, execution);
    }

    static Variable object(const std::shared_ptr<Object> &o) { return Variable::ofObject(o->id()); }
    static Variable i(int v) { return Variable::ofInt(v); }
    static Variable at(float x, float y) {
        return Variable::ofLocation(std::make_shared<Location>(glm::vec3(x, y, 0.0f), 0.0f));
    }

    TestEngine engine;
    StubConsole console;
    NiceMock<scene::MockSceneGraph> sceneGraph;
    std::unique_ptr<Game> game;
    std::unique_ptr<Routines> routines;
    std::shared_ptr<Area> area;
    ExecutionContext execution;
};

constexpr int kObjectTypeCreature = 1;
constexpr int kObjectTypeWaypoint = 32;

} // namespace

// Strings

TEST_P(Tranche4Routines, insert_string_clamps_the_position_to_the_string) {
    auto insert = [&](const char *dest, const char *s, int position) {
        return call("InsertString", {Variable::ofString(dest), Variable::ofString(s), i(position)}).strValue;
    };
    EXPECT_EQ("abXYcd", insert("abcd", "XY", 2));
    EXPECT_EQ("XYabcd", insert("abcd", "XY", 0));
    EXPECT_EQ("abcdXY", insert("abcd", "XY", 4));
    EXPECT_EQ("XYabcd", insert("abcd", "XY", -3)) << "before the start inserts at the start";
    EXPECT_EQ("abcdXY", insert("abcd", "XY", 99)) << "past the end appends";
    EXPECT_EQ("XY", insert("", "XY", 5));
}

TEST_P(Tranche4Routines, int_to_hex_string_is_0x_and_eight_lower_case_digits) {
    auto hex = [&](int value) { return call("IntToHexString", {i(value)}).strValue; };
    EXPECT_EQ("0x0000007b", hex(123));
    EXPECT_EQ("0x00000000", hex(0));
    EXPECT_EQ("0xffffffff", hex(-1));
    EXPECT_EQ("0x7fffffff", hex(2147483647));
}

TEST_P(Tranche4Routines, patterns_match_wildcards_blocks_and_alternatives_ignoring_case) {
    auto test = [&](const char *pattern, const char *text) {
        return call("TestStringAgainstPattern", {Variable::ofString(pattern), Variable::ofString(text)}).intValue;
    };
    EXPECT_EQ(1, test("**(bash|open|unlock)**(chest|door)", "open the door please"));
    EXPECT_EQ(1, test("**(bash|open|unlock)**(chest|door)", "he just bashed that chest!"));
    EXPECT_EQ(0, test("**(bash|open|unlock)**(chest|door)", "close the window"));
    EXPECT_EQ(1, test("FOO**", "foobar"));
    EXPECT_EQ(1, test("foo", "FOO"));
    EXPECT_EQ(1, test("foo", "foobar")) << "text after the last piece is ignored";
    EXPECT_EQ(0, test("foo", "a foo")) << "anchored at the start";
    EXPECT_EQ(1, test("**", ""));
    EXPECT_EQ(1, test("hello*wworld", "hello \t world"));
    EXPECT_EQ(0, test("hello*wworld", "helloworld")) << "*w is one or more";
    EXPECT_EQ(1, test("room *n", "room 101"));
    EXPECT_EQ(0, test("room *n", "room x"));
    EXPECT_EQ(1, test("wow*p", "wow!!"));
    EXPECT_EQ(1, test("*a", "abc"));
    EXPECT_EQ(1, test("*a", "ab1")) << "one or more letters, the rest ignored";
    EXPECT_EQ(0, test("*a", "1ab"));
    EXPECT_EQ(1, test("yes|no", "NO")) << "top-level alternatives";
}

TEST_P(Tranche4Routines, matched_substrings_follow_the_pattern_pieces_of_the_latest_test) {
    call("TestStringAgainstPattern", {Variable::ofString("FOO**"), Variable::ofString("foobar")});
    EXPECT_EQ(2, call("GetMatchedSubstringsCount", {}).intValue);
    EXPECT_EQ("foo", call("GetMatchedSubstring", {i(0)}).strValue);
    EXPECT_EQ("bar", call("GetMatchedSubstring", {i(1)}).strValue);
    EXPECT_EQ("", call("GetMatchedSubstring", {i(2)}).strValue);
    EXPECT_EQ("", call("GetMatchedSubstring", {i(-1)}).strValue);

    call("TestStringAgainstPattern", {Variable::ofString("**(open|bash)**"), Variable::ofString("please open it")});
    EXPECT_EQ(3, call("GetMatchedSubstringsCount", {}).intValue);
    EXPECT_EQ("please ", call("GetMatchedSubstring", {i(0)}).strValue);
    EXPECT_EQ("open", call("GetMatchedSubstring", {i(1)}).strValue);
    EXPECT_EQ(" it", call("GetMatchedSubstring", {i(2)}).strValue) << "the trailing ** takes the rest";

    call("TestStringAgainstPattern", {Variable::ofString("nothing"), Variable::ofString("else")});
    EXPECT_EQ(0, call("GetMatchedSubstringsCount", {}).intValue) << "a failed match leaves no substrings";
}

// Logging and messages

TEST_P(Tranche4Routines, messages_to_the_player_reach_the_console_and_log_entries_do_not_throw) {
    auto player = addCreature();
    call("SendMessageToPC", {object(player), Variable::ofString("hello")});
    ASSERT_EQ(1u, console.lines.size());
    EXPECT_EQ("hello", console.lines[0]);
    call("SendMessageToPC", {Variable::ofObject(kObjectInvalid), Variable::ofString("nobody")});
    EXPECT_EQ(1u, console.lines.size());
    EXPECT_NO_THROW(call("WriteTimestampedLogEntry", {Variable::ofString("an entry")}));
}

TEST_P(Tranche4Routines, floating_text_on_a_creature_floats_the_string_over_it) {
    auto creature = addCreature();
    call("FloatingTextStringOnCreature", {Variable::ofString("Ouch"), object(creature), i(1)});
    EXPECT_TRUE(game->floatingText().hasText(creature->id(), "Ouch"));
    call("FloatingTextStringOnCreature", {Variable::ofString("Nope"), Variable::ofObject(kObjectInvalid), i(1)});
    EXPECT_FALSE(game->floatingText().hasText(kObjectInvalid, "Nope"));
}

TEST_P(Tranche4Routines, intentionally_stubbed_routines_answer_their_documented_defaults) {
    EXPECT_EQ(0, call("StopRumblePattern", {i(1)}).intValue);
    EXPECT_EQ("", call("RandomName", {}).strValue);
    EXPECT_NO_THROW(call("ExportAllCharacters", {}));
    EXPECT_NO_THROW(call("SuppressStatusSummaryEntry", {i(1)}));
}


// Factions

TEST_P(Tranche4Routines, faction_rankings_pick_by_challenge_rating_damage_and_defense) {
    auto a = addCreature();
    auto b = addCreature();
    auto c = addCreature();
    for (auto &creature : {a, b, c}) {
        creature->setFaction(Faction::Hostile1);
        creature->setMaxHitPoints(20);
        creature->setCurrentHitPoints(20);
    }
    a->setChallengeRating(2.0f);
    b->setChallengeRating(5.0f);
    c->setChallengeRating(1.0f);
    b->setCurrentHitPoints(4);
    a->setCurrentHitPoints(15);
    // Ties go to the earlier member in area order.
    auto cTwin = addCreature();
    cTwin->setFaction(Faction::Hostile1);
    cTwin->setMaxHitPoints(20);
    cTwin->setCurrentHitPoints(20);
    cTwin->setChallengeRating(1.0f);

    auto pick = [&](const char *name) { return call(name, {object(a), i(0)}).objectId; };
    EXPECT_EQ(c->id(), pick("GetFactionWeakestMember"));
    EXPECT_EQ(b->id(), pick("GetFactionStrongestMember"));
    EXPECT_EQ(b->id(), pick("GetFactionMostDamagedMember"));
    EXPECT_EQ(c->id(), pick("GetFactionLeastDamagedMember"));
    EXPECT_EQ(call("GetFactionWorstAC", {object(a), i(0)}).objectId, call("GetFactionBestAC", {object(a), i(0)}).objectId)
        << "equal defense: the first member wins both";
    EXPECT_EQ(kObjectInvalid, call("GetFactionWeakestMember", {Variable::ofObject(kObjectInvalid), i(0)}).objectId);
}

TEST_P(Tranche4Routines, faction_gold_is_the_party_pool_for_the_player_and_zero_for_others) {
    auto player = addCreature();
    player->setFaction(Faction::Player);
    auto enemy = addCreature();
    enemy->setFaction(Faction::Hostile1);
    game->party().giveGold(250);
    EXPECT_EQ(250, call("GetFactionGold", {object(player)}).intValue);
    EXPECT_EQ(0, call("GetFactionGold", {object(enemy)}).intValue);
    EXPECT_EQ(enemy->id(), call("GetFactionLeader", {object(enemy)}).objectId) << "the first member leads a hostile faction";
    EXPECT_EQ(-1, call("GetFactionMostFrequentClass", {Variable::ofObject(kObjectInvalid)}).intValue);
}

TEST_P(Tranche4Routines, nobody_is_levelling_up_outside_the_level_up_screen) {
    EXPECT_EQ(kObjectInvalid, call("GetPCLevellingUp", {}).objectId);
}

// Creature queries

TEST_P(Tranche4Routines, creature_flags_and_settings_read_back) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature();
    auto other = addCreature();
    EXPECT_EQ(0, call("GetMetaMagicFeat", {}).intValue);
    EXPECT_EQ(0, call("GetIsXBox", {}).intValue);
    EXPECT_EQ(0, call("IsStealthed", {object(creature)}).intValue);
    EXPECT_EQ(0, call("IsRunning", {object(creature)}).intValue);
    creature->setMovementType(Creature::MovementType::Run);
    EXPECT_EQ(1, call("IsRunning", {object(creature)}).intValue);
    EXPECT_EQ(4, call("GetMovementRate", {object(creature)}).intValue) << "normal speed";

    call("SetHealTarget", {object(creature), object(other)});
    EXPECT_EQ(other->id(), call("GetHealTarget", {object(creature)}).objectId);
    call("SetHealTarget", {object(creature), Variable::ofObject(kObjectInvalid)});
    EXPECT_EQ(kObjectInvalid, call("GetHealTarget", {object(creature)}).objectId);

    call("SetBonusForcePoints", {object(creature), i(10)});
    call("AddBonusForcePoints", {object(creature), i(5)});
    EXPECT_EQ(15, call("GetBonusForcePoints", {object(creature)}).intValue);
    call("AddBonusForcePoints", {object(creature), i(-100)});
    EXPECT_EQ(0, call("GetBonusForcePoints", {object(creature)}).intValue) << "never below 0";

    call("SetCreatureAILevel", {object(creature), i(3)});
    EXPECT_EQ(3, creature->scriptSetting("AILevel"));
    call("ResetCreatureAILevel", {object(creature)});
    EXPECT_EQ(-1, creature->scriptSetting("AILevel"));

    EXPECT_EQ(creature->creaturePersonalSpace(), call("GetObjectPersonalSpace", {object(creature)}).floatValue);
    EXPECT_EQ(0.0f, call("GetObjectPersonalSpace", {Variable::ofObject(kObjectInvalid)}).floatValue);
    EXPECT_EQ(0, call("GetCombatActionsPending", {object(creature)}).intValue);
    EXPECT_EQ(0, call("GetHasFeatEffect", {i(1), object(creature)}).intValue);
    EXPECT_EQ(creature->attributes().getAggregateLevel(), call("GetCasterLevel", {object(creature)}).intValue);
}

TEST_P(Tranche4Routines, caster_level_metamagic_and_speed_apply_to_both_games) {
    auto creature = addCreature();
    EXPECT_EQ(0, call("GetMetaMagicFeat", {}).intValue);
    EXPECT_EQ(creature->attributes().getAggregateLevel(), call("GetCasterLevel", {object(creature)}).intValue);
    EXPECT_EQ(0, call("GetCasterLevel", {object(area)}).intValue);
    EXPECT_EQ(4, call("GetMovementRate", {object(creature)}).intValue) << "normal speed";
    EXPECT_EQ(4, call("GetMovementRate", {object(area)}).intValue) << "normal for anything but a creature";
}

TEST_P(Tranche4Routines, adjusting_attributes_skills_and_saves_changes_the_creature) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature();
    auto strength = creature->attributes().getAbilityScore(Ability::Strength);
    call("AdjustCreatureAttributes", {object(creature), i(0), i(3)});
    EXPECT_EQ(strength + 3, creature->attributes().getAbilityScore(Ability::Strength));
    call("AdjustCreatureAttributes", {object(creature), i(0), i(-1000)});
    EXPECT_EQ(1, creature->attributes().getAbilityScore(Ability::Strength)) << "never below 1";

    call("AdjustCreatureSkills", {object(creature), i(1), i(4)});
    EXPECT_EQ(4, creature->attributes().getSkillRank(SkillType::Demolitions));
    EXPECT_EQ(4, call("GetSkillRankBase", {i(1), object(creature)}).intValue);
    call("AdjustCreatureSkills", {object(creature), i(1), i(-9)});
    EXPECT_EQ(0, call("GetSkillRankBase", {i(1), object(creature)}).intValue);

    call("ModifyReflexSavingThrowBase", {object(creature), i(3)});
    call("ModifyWillSavingThrowBase", {object(creature), i(2)});
    call("ModifyFortitudeSavingThrowBase", {object(creature), i(-5)});
    auto saves = creature->attributes().getAggregateSavingThrows();
    EXPECT_EQ(3, saves.reflex);
    EXPECT_EQ(2, saves.will);
    EXPECT_EQ(0, saves.fortitude) << "never below 0";
}

TEST_P(Tranche4Routines, feats_and_powers_can_be_granted_outright) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature();
    call("GrantFeat", {i(7), object(creature)});
    EXPECT_TRUE(creature->attributes().hasFeat(static_cast<FeatType>(7)));
    call("GrantSpell", {i(3), object(creature)});
    EXPECT_TRUE(creature->attributes().hasSpell(static_cast<SpellType>(3)));
    EXPECT_NO_THROW(call("GrantFeat", {i(7), Variable::ofObject(kObjectInvalid)}));
}

TEST_P(Tranche4Routines, line_of_sight_and_random_destinations_without_a_scene_are_open) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature(glm::vec3(1.0f, 2.0f, 0.0f));
    EXPECT_EQ(1, call("HasLineOfSight", {Variable::ofVector(glm::vec3(0.0f)), Variable::ofVector(glm::vec3(5.0f, 0.0f, 0.0f))}).intValue);
    auto destination = call("GetRandomDestination", {object(creature), i(5)}).vecValue;
    EXPECT_LE(glm::distance(destination, creature->position()), 5.01f);
    EXPECT_EQ(glm::vec3(0.0f), call("GetRandomDestination", {Variable::ofObject(kObjectInvalid), i(5)}).vecValue);
}

TEST_P(Tranche4Routines, last_weapon_used_is_what_the_creature_last_attacked_with) {
    auto creature = addCreature();
    EXPECT_EQ(kObjectInvalid, call("GetLastWeaponUsed", {object(creature)}).objectId);
    creature->setLastWeaponUsed(1234);
    EXPECT_EQ(1234u, call("GetLastWeaponUsed", {object(creature)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetLastWeaponUsed", {Variable::ofObject(kObjectInvalid)}).objectId);
}

TEST_P(Tranche4Routines, attackers_are_the_creatures_aiming_at_the_victim_in_area_order) {
    auto victim = addCreature();
    auto first = addCreature();
    auto second = addCreature();
    auto bystander = addCreature();
    first->setAttemptedAttackTarget(victim->id());
    second->setAttemptedAttackTarget(victim->id());
    EXPECT_EQ(first->id(), call("GetGoingToBeAttackedBy", {object(victim)}).objectId);
    EXPECT_EQ(first->id(), call("GetFirstAttacker", {object(victim)}).objectId);
    EXPECT_EQ(second->id(), call("GetNextAttacker", {object(victim)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetNextAttacker", {object(victim)}).objectId);
    EXPECT_EQ(kObjectInvalid, call("GetFirstAttacker", {object(bystander)}).objectId);
}

TEST_P(Tranche4Routines, hearing_follows_the_creatures_perception) {
    auto listener = addCreature();
    auto noise = addCreature();
    EXPECT_EQ(0, call("GetObjectHeard", {object(noise), object(listener)}).intValue);
    listener->setObjectHeard(noise, true);
    EXPECT_EQ(1, call("GetObjectHeard", {object(noise), object(listener)}).intValue);
}

TEST_P(Tranche4Routines, weapon_effectiveness_needs_a_live_target) {
    auto target = addCreature();
    EXPECT_EQ(1, call("GetIsWeaponEffective", {object(target)}).intValue);
    target->setMaxHitPoints(10);
    target->setCurrentHitPoints(0);
    EXPECT_EQ(0, call("GetIsWeaponEffective", {object(target)}).intValue);
    EXPECT_EQ(0, call("GetIsWeaponEffective", {Variable::ofObject(kObjectInvalid)}).intValue);
}

TEST_P(Tranche4Routines, touch_attacks_answer_miss_hit_or_critical_and_need_a_target) {
    auto attacker = addCreature();
    auto target = addCreature();
    execution.args.push_back(Argument(ArgKind::Caller, Variable::ofObject(attacker->id())));
    for (int n = 0; n < 40; ++n) {
        int melee = call("TouchAttackMelee", {object(target)}).intValue;
        int ranged = call("TouchAttackRanged", {object(target), i(0)}).intValue;
        EXPECT_GE(melee, 0);
        EXPECT_LE(melee, 2);
        EXPECT_GE(ranged, 0);
        EXPECT_LE(ranged, 2);
    }
    EXPECT_EQ(0, call("TouchAttackMelee", {Variable::ofObject(kObjectInvalid)}).intValue);
}

// Locks

TEST_P(Tranche4Routines, locks_remember_who_locked_and_unlocked_them) {
    auto door = game->newDoor();
    area->add(door);
    auto player = addCreature();
    EXPECT_EQ(kObjectInvalid, door->lastLockedBy());
    door->setLocked(true);
    door->runLockScript(true, player->id());
    EXPECT_EQ(player->id(), door->lastLockedBy());
    door->runLockScript(false, player->id());
    EXPECT_EQ(player->id(), door->lastUnlockedBy());

    execution.args.push_back(Argument(ArgKind::Caller, Variable::ofObject(door->id())));
    EXPECT_EQ(player->id(), call("GetLastLocked", {}).objectId);
    EXPECT_EQ(player->id(), call("GetLastUnlocked", {}).objectId);
}

// Encounters, placeables, items

TEST_P(Tranche4Routines, encounter_spawn_counts_read_and_write_waves) {
    auto encounter = game->newEncounter();
    area->add(encounter);
    call("SetEncounterSpawnsMax", {i(5), object(encounter)});
    call("SetEncounterSpawnsCurrent", {i(2), object(encounter)});
    EXPECT_EQ(5, call("GetEncounterSpawnsMax", {object(encounter)}).intValue);
    EXPECT_EQ(2, call("GetEncounterSpawnsCurrent", {object(encounter)}).intValue);
    EXPECT_EQ(0, call("GetEncounterSpawnsMax", {object(addCreature())}).intValue);
}

TEST_P(Tranche4Routines, placeable_illumination_inventory_and_actions) {
    auto placeable = game->newPlaceable();
    area->add(placeable);
    EXPECT_EQ(0, call("GetPlaceableIllumination", {object(placeable)}).intValue);
    call("SetPlaceableIllumination", {object(placeable), i(1)});
    EXPECT_EQ(1, call("GetPlaceableIllumination", {object(placeable)}).intValue);
    call("SetPlaceableIllumination", {object(placeable), i(0)});
    EXPECT_EQ(0, call("GetPlaceableIllumination", {object(placeable)}).intValue);

    EXPECT_EQ(0, call("GetHasInventory", {object(placeable)}).intValue);
    placeable->setHasInventory(true);
    EXPECT_EQ(1, call("GetHasInventory", {object(placeable)}).intValue);
    EXPECT_EQ(1, call("GetHasInventory", {object(addCreature())}).intValue);
    EXPECT_EQ(0, call("GetHasInventory", {object(area)}).intValue);

    // The placeable is not usable or locked here, so no action is possible.
    for (int action = 0; action < 4; ++action) {
        EXPECT_EQ(0, call("GetIsPlaceableObjectActionPossible", {object(placeable), i(action)}).intValue) << action;
    }
    EXPECT_EQ(0, call("GetIsPlaceableObjectActionPossible", {object(addCreature()), i(0)}).intValue);
}

TEST_P(Tranche4Routines, item_properties_and_armour_class_values) {
    auto property = [](int name, int costValue) {
        return Gff::Builder()
            .field(Gff::Field::newWord("PropertyName", name))
            .field(Gff::Field::newByte("CostTable", 0))
            .field(Gff::Field::newWord("CostValue", costValue))
            .build();
    };
    auto gff = Gff::Builder()
                   .field(Gff::Field::newInt("BaseItem", 0))
                   .field(Gff::Field::newWord("StackSize", 1))
                   .field(Gff::Field::newList("PropertiesList", {property(static_cast<int>(ItemProperty::AcBonus), 2),
                                                                 property(static_cast<int>(ItemProperty::AbilityBonus), 1)}))
                   .build();
    auto item = game->newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    EXPECT_EQ(1, call("GetItemHasItemProperty", {object(item), i(static_cast<int>(ItemProperty::AcBonus))}).intValue);
    EXPECT_EQ(1, call("GetItemHasItemProperty", {object(item), i(static_cast<int>(ItemProperty::AbilityBonus))}).intValue);
    EXPECT_EQ(0, call("GetItemHasItemProperty", {object(item), i(static_cast<int>(ItemProperty::DecreasedAc))}).intValue);
    EXPECT_EQ(0, call("GetItemHasItemProperty", {object(addCreature()), i(1)}).intValue);
    EXPECT_EQ(item->baseDefense(), call("GetItemACValue", {object(item)}).intValue) << "no bonus table: base defense only";
    EXPECT_EQ(0, call("GetItemACValue", {object(addCreature())}).intValue);
}

TEST_P(Tranche4Routines, a_pile_of_credits_is_worth_its_stack_size) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newInt("BaseItem", 0))
                   .field(Gff::Field::newWord("StackSize", 75))
                   .build();
    auto item = game->newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    EXPECT_EQ(75, call("GetGoldPieceValue", {object(item)}).intValue);
    EXPECT_EQ(0, call("GetGoldPieceValue", {object(addCreature())}).intValue);
}

// Party and journal

TEST_P(Tranche4Routines, influence_is_clamped_and_modified_by_companion) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    EXPECT_EQ(0, call("GetInfluence", {i(2)}).intValue);
    call("SetInfluence", {i(2), i(150)});
    EXPECT_EQ(100, call("GetInfluence", {i(2)}).intValue);
    call("ModifyInfluence", {i(2), i(-30)});
    EXPECT_EQ(70, call("GetInfluence", {i(2)}).intValue);
    call("ModifyInfluence", {i(2), i(-300)});
    EXPECT_EQ(0, call("GetInfluence", {i(2)}).intValue);
    call("SetInfluence", {i(-1), i(50)});
    EXPECT_EQ(0, call("GetInfluence", {i(-1)}).intValue);
    EXPECT_EQ(0, call("GetInfluence", {i(9999)}).intValue);
}

TEST_P(Tranche4Routines, crafting_materials_are_the_party_pools_in_pieces_of_twenty_credits) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    game->party().setCraftingMaterials(7, 3);
    EXPECT_EQ(7, call("GetItemComponent", {}).intValue);
    EXPECT_EQ(3, call("GetChemicals", {}).intValue);
    EXPECT_EQ(20, call("GetItemComponentPieceValue", {}).intValue);
    EXPECT_EQ(20, call("GetChemicalPieceValue", {}).intValue);
}

TEST_P(Tranche4Routines, journal_world_entries_are_added_replaced_and_deleted) {
    auto &journal = game->journal();
    call("AddJournalWorldEntry", {i(1), Variable::ofString("first"), Variable::ofString("Title")});
    call("AddJournalWorldEntry", {i(2), Variable::ofString("second")});
    ASSERT_EQ(2u, journal.worldEntries().size());
    EXPECT_EQ("World Entry", journal.worldEntries()[1].title);
    call("AddJournalWorldEntry", {i(1), Variable::ofString("replaced"), Variable::ofString("Title")});
    ASSERT_EQ(2u, journal.worldEntries().size());
    EXPECT_EQ("replaced", journal.worldEntries()[0].text);
    call("DeleteJournalWorldEntry", {i(1)});
    ASSERT_EQ(1u, journal.worldEntries().size());
    call("DeleteJournalWorldEntry", {i(99)});
    EXPECT_EQ(1u, journal.worldEntries().size());
    call("AddJournalWorldEntryStrref", {i(500), i(501)});
    ASSERT_EQ(2u, journal.worldEntries().size());
    call("DeleteJournalWorldEntryStrref", {i(500)});
    EXPECT_EQ(1u, journal.worldEntries().size());
    call("DeleteJournalWorldAllEntries", {});
    EXPECT_TRUE(journal.worldEntries().empty());
}

TEST_P(Tranche4Routines, a_quest_entry_remembers_its_picture) {
    EXPECT_EQ(-1, game->journal().entryPicture("plot"));
    call("SetJournalQuestEntryPicture", {Variable::ofString("plot"), object(addCreature()), i(4), i(1), i(0)});
    EXPECT_EQ(4, game->journal().entryPicture("plot"));
}

TEST_P(Tranche4Routines, the_map_flag_and_tutorial_windows_follow_their_switches) {
    if (GetParam() == GameID::TSL) {
        call("DisableMap", {i(1)});
        EXPECT_TRUE(game->party().persistedState().mapDisabled);
        call("DisableMap", {i(0)});
        EXPECT_FALSE(game->party().persistedState().mapDisabled);
    }
    EXPECT_TRUE(game->tutorialWindowsEnabled());
    call("SetTutorialWindowsEnabled", {i(0)});
    EXPECT_FALSE(game->tutorialWindowsEnabled());
    EXPECT_FALSE(game->showTutorialWindow(0));
}

TEST_P(Tranche4Routines, simple_world_switches_are_stored_for_scripts) {
    call("SetButtonMashCheck", {i(1)});
    EXPECT_EQ(1, call("GetButtonMashCheck", {}).intValue);
    call("SetButtonMashCheck", {i(0)});
    EXPECT_EQ(0, call("GetButtonMashCheck", {}).intValue);
    EXPECT_EQ("", call("GetLastConversation", {}).strValue);
    EXPECT_NE(nullptr, call("GetStartingLocation", {}).engineType);
}

TEST_P(Tranche4Routines, a_creature_can_be_hidden_and_the_destroyed_effect_ids_are_ignored_when_unknown) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature();
    EXPECT_NO_THROW(call("EnableRendering", {object(creature), i(0)}));
    EXPECT_NO_THROW(call("RemoveEffectByID", {object(creature), i(12345)}));
    EXPECT_NO_THROW(call("ForceHeartbeat", {object(creature)}));
    EXPECT_NO_THROW(call("ExploreAreaForPlayer", {object(area), object(creature)}));
}

TEST_P(Tranche4Routines, owner_demolitions_reads_the_creature_or_the_trap_creator) {
    if (GetParam() != GameID::TSL) {
        GTEST_SKIP() << "KotOR II routines";
    }
    auto creature = addCreature();
    creature->attributes().setSkillRank(SkillType::Demolitions, 6);
    int own = call("GetOwnerDemolitionsSkill", {object(creature)}).intValue;
    EXPECT_EQ(creature->attributes().demolitions(), own);
    EXPECT_EQ(0, call("GetOwnerDemolitionsSkill", {Variable::ofObject(kObjectInvalid)}).intValue);
}

TEST_P(Tranche4Routines, events_for_conversation_and_item_activation_carry_their_kind) {
    auto conversation = call("EventConversation", {});
    ASSERT_NE(nullptr, conversation.engineType);
    EXPECT_EQ(EventKind::Conversation, std::static_pointer_cast<Event>(conversation.engineType)->kind());
    auto activation = call("EventActivateItem", {object(addCreature()), at(0.0f, 0.0f)});
    ASSERT_NE(nullptr, activation.engineType);
    EXPECT_EQ(EventKind::ActivateItem, std::static_pointer_cast<Event>(activation.engineType)->kind());
}

// Equipment and item spells

TEST_P(Tranche4Routines, last_item_equipped_is_the_item_most_recently_put_on) {
    TwoDA::Builder baseItems;
    baseItems.columns({"weapontype", "itemclass", "equipableslots", "weaponwield", "numdice", "dietoroll", "maxattackrange",
                       "crithitmult", "critthreat", "damageflags", "ammunitiontype", "bodyvar"});
    baseItems.row({"1", "w_stunbaton", "48", "1", "1", "4", "1.5", "2", "2", "2", "-1", ""});
    ON_CALL(engine.resourceModule().twoDas(), get(std::string("baseitems")))
        .WillByDefault(Return(std::shared_ptr<TwoDA>(baseItems.build())));
    auto makeBaton = [&] {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newInt("BaseItem", 0))
                       .field(Gff::Field::newWord("StackSize", 1))
                       .build();
        auto item = game->newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    };
    auto creature = addCreature();
    EXPECT_EQ(kObjectInvalid, call("GetLastItemEquipped", {}).objectId) << "nothing equipped yet";

    auto first = makeBaton();
    auto second = makeBaton();
    ASSERT_TRUE(creature->equip(InventorySlots::rightWeapon, first));
    EXPECT_EQ(first->id(), call("GetLastItemEquipped", {}).objectId);
    ASSERT_TRUE(creature->equip(InventorySlots::leftWeapon, second));
    EXPECT_EQ(second->id(), call("GetLastItemEquipped", {}).objectId);
}

TEST_P(Tranche4Routines, spell_cast_item_is_the_item_argument_of_the_impact_script) {
    auto item = game->newItem();
    EXPECT_EQ(kObjectInvalid, call("GetSpellCastItem", {}).objectId) << "not in a spell script";
    execution.args.push_back({ArgKind::SpellCastItem, Variable::ofObject(item->id())});
    EXPECT_EQ(item->id(), call("GetSpellCastItem", {}).objectId);
}

INSTANTIATE_TEST_SUITE_P(BothGames, Tranche4Routines, Values(GameID::KotOR, GameID::TSL));
