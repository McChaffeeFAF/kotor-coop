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

// Rules ported piece by piece from the vrifftech fork's fix list (64f5cb984): items, routines,
// casts, regeneration and CreateObject. Doors and entering objects are in object.cpp.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/attackobject.h"
#include "reone/game/action/castspellatobject.h"
#include "reone/game/d20/spell.h"
#include "reone/game/effect/regenerate.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/placeable.h"
#include "reone/game/object/store.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/party.h"
#include "reone/game/effect/damage.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/script/routine.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

constexpr int kPistol = 0;
constexpr int kBite = 1;
constexpr int kBodyArmour = 2;
constexpr int kOtherArmour = 3;

std::shared_ptr<TwoDA> makeTable(const std::vector<std::string> &columns, const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Gff> property(int name, int subtype, int costTable, int costValue) {
    return Gff::Builder()
        .type(0)
        .field(Gff::Field::newWord("PropertyName", name))
        .field(Gff::Field::newWord("Subtype", subtype))
        .field(Gff::Field::newByte("CostTable", costTable))
        .field(Gff::Field::newWord("CostValue", costValue))
        .field(Gff::Field::newByte("Param1", 255))
        .field(Gff::Field::newByte("Param1Value", 0))
        .build();
}

class ForkPorts : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        // Base items: a blaster (ranged), a creature bite (weapon type 4 but not ranged), body
        // armour and an armour-class item not drawn as the body (both cap Dexterity at +1).
        EXPECT_CALL(_engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeTable(
                {"equipableslots", "itemclass", "stacking", "weapontype", "rangedweapon", "modeltype", "baseac", "dexbonus",
                 "numdice", "dietoroll", "damageflags"},
                {{"0x00030", "w_blstrpstl", "1", "4", "1", "0", "", "", "1", "6", "1"},
                 {"0x00030", "w_crbite", "1", "4", "", "0", "", "", "1", "4", "1"},
                 {"0x00002", "a_class4", "1", "", "", "1", "4", "1", "", "", ""},
                 {"0x00002", "i_drdltplat", "1", "", "", "0", "4", "1", "", "", ""}})));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    void TearDown() override {
        Mock::VerifyAndClearExpectations(&_engine.resourceModule().twoDas());
        _engine.resourceModule().twoDas().allowAnyLookup();
    }

    std::shared_ptr<Item> makeItem(int baseItem, std::vector<std::shared_ptr<Gff>> properties = {}) {
        auto gff = Gff::Builder()
                       .field(Gff::Field::newCExoString("Tag", "item" + std::to_string(baseItem)))
                       .field(Gff::Field::newInt("BaseItem", baseItem))
                       .field(Gff::Field::newWord("StackSize", 1))
                       .field(Gff::Field::newList("PropertiesList", std::move(properties)))
                       .build();
        auto item = _game->newItem();
        item->deserialize(*gff, SerializedIdentityContext::templateResource());
        return item;
    }

    std::shared_ptr<Creature> addCreature(glm::vec3 position = glm::vec3(0.0f)) {
        auto creature = _game->newCreature();
        creature->setPosition(position);
        creature->setMaxHitPoints(20);
        creature->setCurrentHitPoints(20);
        _area->add(creature);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args, uint32_t caller = kObjectInvalid) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        if (caller != kObjectInvalid) {
            execution.args.emplace_back(ArgKind::Caller, Variable::ofObject(caller));
        }
        return routine.invoke(args, execution);
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

} // namespace

// --- Items ---------------------------------------------------------------------

TEST_F(ForkPorts, a_weapon_is_ranged_by_its_base_items_ranged_column) {
    EXPECT_TRUE(makeItem(kPistol)->isRanged());
    EXPECT_FALSE(makeItem(kBite)->isRanged()) << "a creature bite has weapon type 4 but is not ranged";
    // A table without the column (older test data) falls back on the weapon type.
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("baseitems"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"weapontype"}, {{"4"}, {"1"}})));
    EXPECT_TRUE(makeItem(0)->isRanged());
    EXPECT_FALSE(makeItem(1)->isRanged());
}

TEST_F(ForkPorts, only_armour_drawn_as_the_body_limits_dexterity) {
    EXPECT_TRUE(makeItem(kBodyArmour)->isBodyModel());
    EXPECT_FALSE(makeItem(kOtherArmour)->isBodyModel());

    auto plated = addCreature();
    plated->attributes().setAbilityScore(Ability::Dexterity, 18); // +4
    ASSERT_TRUE(plated->equip(InventorySlots::body, makeItem(kOtherArmour)));
    int uncapped = plated->getDefense();
    auto armoured = addCreature();
    armoured->attributes().setAbilityScore(Ability::Dexterity, 18);
    ASSERT_TRUE(armoured->equip(InventorySlots::body, makeItem(kBodyArmour)));
    int capped = armoured->getDefense();
    EXPECT_EQ(3, uncapped - capped) << "body armour caps Dexterity at +1, the other item does not";
}

TEST_F(ForkPorts, a_decreased_ability_property_lowers_the_ability) {
    std::vector<std::vector<std::string>> costTables(22, {"", "", "0"});
    costTables[21] = {"IPRP_NEG10COST", "Negative_Modifiers", "0"};
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_costtable"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"name", "label", "clientload"}, costTables)));
    // iprp_neg10cost holds the decreases as negative values.
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_neg10cost"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"name", "label", "cost", "value"},
                                         {{"", "Random", "0", "0"}, {"875", "Penalty_-1", "1", "-1"}, {"876", "Penalty_-2", "2", "-2"}})));
    auto wearer = addCreature();
    wearer->attributes().setAbilityScore(Ability::Dexterity, 14);
    int before = wearer->getEffectiveAbilityScore(Ability::Dexterity);
    auto armour = makeItem(kBodyArmour, {property(static_cast<int>(ItemProperty::DecreasedAbilityScore),
                                                  static_cast<int>(Ability::Dexterity), 21, 2)});
    ASSERT_TRUE(wearer->equip(InventorySlots::body, armour));
    EXPECT_EQ(before - 2, wearer->getEffectiveAbilityScore(Ability::Dexterity));
}

TEST_F(ForkPorts, damage_bonuses_of_a_type_add_up_across_an_items_properties) {
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_damagetype"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label"}, {{"Bludgeoning"}, {"Piercing"}, {"Slashing"}})));
    // Blank dice: a cost value is a flat amount.
    EXPECT_CALL(_engine.resourceModule().twoDas(), get("iprp_damagecost"))
        .Times(AnyNumber())
        .WillRepeatedly(Return(makeTable({"label", "numdice", "die"}, {{"0", "", ""}, {"1", "", ""}, {"2", "", ""}, {"3", "", ""}})));
    auto attacker = addCreature();
    auto blaster = makeItem(kPistol, {property(static_cast<int>(ItemProperty::DamageBonus), 0, 4, 2),
                                      property(static_cast<int>(ItemProperty::DamageBonus), 0, 4, 3)});
    ASSERT_TRUE(attacker->equip(InventorySlots::rightWeapon, blaster));
    DamagePacket damage;
    attacker->addPhysicalDamageModifiers(damage, nullptr, blaster.get(), false, 1);
    EXPECT_EQ(5, damage.total()) << "+2 and +3 bludgeoning, not only the larger";
}

// --- Routines ------------------------------------------------------------------

TEST_F(ForkPorts, set_party_leader_stands_a_downed_player_character_up) {
    auto pc = addCreature();
    pc->setIsPC(true);
    _game->party().addMember(kNpcPlayer, pc);
    _game->party().setPlayer(pc);
    pc->setCurrentHitPoints(0);
    ASSERT_TRUE(pc->isTemporarilyDead());
    EXPECT_EQ(1, call("SetPartyLeader", {Variable::ofInt(kNpcPlayer)}).intValue);
    EXPECT_FALSE(pc->isTemporarilyDead());
    EXPECT_GE(pc->currentHitPoints(), 1);
    EXPECT_TRUE(pc->isRaiseable());
    EXPECT_EQ(0, call("SetPartyLeader", {Variable::ofInt(5)}).intValue) << "no member in that slot";
}

TEST_F(ForkPorts, change_to_standard_faction_refuses_player_characters_and_bad_input) {
    auto npc = addCreature();
    npc->setFaction(Faction::Gizka1);
    call("ChangeToStandardFaction", {Variable::ofObject(npc->id()), Variable::ofInt(static_cast<int>(Faction::Hostile1))});
    EXPECT_EQ(Faction::Hostile1, npc->faction());

    auto pc = addCreature();
    pc->setIsPC(true);
    pc->setFaction(Faction::Player);
    call("ChangeToStandardFaction", {Variable::ofObject(pc->id()), Variable::ofInt(static_cast<int>(Faction::Hostile1))});
    EXPECT_EQ(Faction::Player, pc->faction());

    // A faction out of range or a non-creature is ignored, not an error.
    EXPECT_NO_THROW(call("ChangeToStandardFaction", {Variable::ofObject(npc->id()), Variable::ofInt(9999)}));
    EXPECT_EQ(Faction::Hostile1, npc->faction());
    auto placeable = _game->newPlaceable();
    _area->add(placeable);
    EXPECT_NO_THROW(call("ChangeToStandardFaction", {Variable::ofObject(placeable->id()), Variable::ofInt(1)}));
}

TEST_F(ForkPorts, get_last_attack_action_is_minus_one_before_any_round_and_zero_for_a_non_creature) {
    auto creature = addCreature();
    EXPECT_EQ(-1, call("GetLastAttackAction", {Variable::ofObject(creature->id())}).intValue);
    auto placeable = _game->newPlaceable();
    _area->add(placeable);
    EXPECT_EQ(0, call("GetLastAttackAction", {Variable::ofObject(placeable->id())}).intValue);
}

// --- Casts ---------------------------------------------------------------------

TEST_F(ForkPorts, a_placeable_casts_at_once_whatever_the_distance) {
    std::vector<std::string> requested;
    EXPECT_CALL(_engine.resourceModule().scripts(), get("k_test_impact"))
        .Times(AnyNumber())
        .WillRepeatedly(Invoke([&requested](const std::string &key) {
            requested.push_back(key);
            return std::shared_ptr<ScriptProgram>();
        }));
    auto spell = std::make_shared<Spell>();
    spell->impactScript = "k_test_impact";
    spell->range = 5.0f;
    spell->castTime = 1.5f;
    auto turret = _game->newPlaceable();
    turret->setPosition(glm::vec3(0.0f));
    _area->add(turret);
    auto target = addCreature(glm::vec3(50.0f, 0.0f, 0.0f));
    auto action = _game->newAction<CastSpellAtObjectAction>(spell, target, std::nullopt);
    action->execute(action, *turret, 0.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(1u, requested.size());
}

TEST_F(ForkPorts, an_instant_cast_does_not_walk_to_its_target) {
    auto spell = std::make_shared<Spell>();
    spell->range = 5.0f;
    auto target = addCreature(glm::vec3(50.0f, 0.0f, 0.0f));

    // The cast begins (records its target) only once the caster is in range.
    auto walker = addCreature();
    auto walk = _game->newAction<CastSpellAtObjectAction>(spell, target, std::nullopt, true);
    walk->execute(walk, *walker, 0.0f);
    EXPECT_NE(target->id(), walker->getAttemptedSpellTarget()) << "an ordinary cast closes to its range first";

    auto stayer = addCreature();
    auto instant = _game->newAction<CastSpellAtObjectAction>(spell, target, std::nullopt, true);
    instant->setInstant(true);
    instant->execute(instant, *stayer, 0.0f);
    EXPECT_EQ(target->id(), stayer->getAttemptedSpellTarget());
    EXPECT_FLOAT_EQ(0.0f, stayer->position().x);
}

// --- Regeneration and invisibility ------------------------------------------------

TEST_F(ForkPorts, a_downed_party_member_does_not_regenerate) {
    auto member = addCreature();
    _game->party().addMember(kNpcPlayer, member);
    member->setCurrentHitPoints(0);
    ASSERT_TRUE(member->isTemporarilyDead());
    RegenerateEffect regenerate(2, 1.0f);
    EffectInstance instance;
    regenerate.onApply(*member, instance);
    regenerate.onUpdate(*member, instance, 3.0f);
    EXPECT_EQ(0, member->currentHitPoints());

    auto stranger = addCreature();
    stranger->setCurrentHitPoints(10);
    regenerate.onApply(*stranger, instance);
    regenerate.onUpdate(*stranger, instance, 3.0f);
    EXPECT_EQ(16, stranger->currentHitPoints());
}

TEST_F(ForkPorts, hostile_actions_cleared_at_a_creature_include_queued_casts_and_item_uses) {
    auto spell = std::make_shared<Spell>();
    auto target = addCreature(glm::vec3(2.0f, 0.0f, 0.0f));
    auto other = addCreature(glm::vec3(3.0f, 0.0f, 0.0f));
    auto caster = addCreature();
    auto first = _game->newAction<CastSpellAtObjectAction>(spell, other, std::nullopt);
    auto queued = _game->newAction<CastSpellAtObjectAction>(spell, target, std::nullopt);
    auto elsewhere = _game->newAction<CastSpellAtObjectAction>(spell, other, std::nullopt);
    caster->addAction(first);
    caster->addAction(queued);
    caster->addAction(elsewhere);
    caster->clearHostileActionsAgainst(*target);
    EXPECT_TRUE(queued->isCancelled());
    EXPECT_FALSE(first->isCancelled());
    EXPECT_FALSE(elsewhere->isCancelled());
    EXPECT_EQ(2u, caster->actions().size());
}

// --- CreateObject ------------------------------------------------------------------

TEST_F(ForkPorts, create_object_makes_waypoints_and_stores) {
    EXPECT_CALL(_engine.resourceModule().gffs(), get(_, _)).Times(AnyNumber()).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(_engine.resourceModule().gffs(), get("wp_made", ResType::Utw))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<Gff>(Gff::Builder().field(Gff::Field::newCExoString("Tag", "wp_made")).build())));
    EXPECT_CALL(_engine.resourceModule().gffs(), get("store_made", ResType::Utm))
        .Times(AnyNumber())
        .WillRepeatedly(Return(std::shared_ptr<Gff>(Gff::Builder().field(Gff::Field::newCExoString("Tag", "store_made")).build())));
    auto location = std::make_shared<Location>(glm::vec3(4.0f, 5.0f, 0.0f), 0.0f);
    auto waypoint = _area->createObject(ObjectType::Waypoint, "wp_made", location);
    ASSERT_TRUE(waypoint);
    EXPECT_EQ(ObjectType::Waypoint, waypoint->type());
    EXPECT_EQ("wp_made", waypoint->tag());
    EXPECT_FLOAT_EQ(4.0f, waypoint->position().x);
    auto store = _area->createObject(ObjectType::Store, "store_made", location);
    ASSERT_TRUE(store);
    EXPECT_EQ(ObjectType::Store, store->type());
    EXPECT_EQ(nullptr, _area->createObject(ObjectType::Store, "no_such_store", location));
}
