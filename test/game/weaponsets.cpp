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

/**
 * K2's two weapon sets, available in K1 sessions: the swap itself, who may
 * request it and when it runs, and K1 save persistence of the second set.
 * Rules: design note weapon_sets.
 */

#include "../fixtures/engine.h"
#include "reone/game/action/switchweapons.h"
#include "reone/game/action/wait.h"
#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::deserializeSavedEquipment(
    Creature &creature, const Gff &gff, const SerializedIdentityContext &identityContext) {
    creature.deserializeOwnedItemsAndEquipment(gff, identityContext);
}

namespace {

constexpr int kPistol = 12;
constexpr int kSaber = 8;

std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"maxattackrange", "crithitmult", "critthreat", "damageflags", "dietoroll",
                     "equipableslots", "itemclass", "numdice", "weapontype", "weaponwield",
                     "ammunitiontype", "bodyvar"});
    for (int i = 0; i <= 12; ++i) {
        if (i == kSaber) {
            builder.row({"1.5", "2", "2", "2", "8", "48", "w_lghtsbr", "1", "1", "2", "-1", ""});
        } else if (i == kPistol) {
            builder.row({"23", "2", "2", "2", "6", "48", "w_blstrpstl", "1", "4", "4", "-1", ""});
        } else {
            builder.row({"", "", "", "", "", "", "", "", "", "", "", ""});
        }
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Item> makeItem(Game &game, std::string tag, int baseItem) {
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", std::move(tag)))
                   .field(Gff::Field::newInt("BaseItem", baseItem))
                   .field(Gff::Field::newWord("StackSize", 1))
                   .build();
    auto item = game.newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource());
    return item;
}

std::string tagAt(const Creature &creature, int slot) {
    auto item = creature.getEquippedItem(slot);
    return item ? item->tag() : "";
}

class WeaponSets : public Test {
protected:
    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game {GameID::KotOR, "", engine.options(), engine.services(), console};

    void SetUp() override {
        EXPECT_CALL(engine.resourceModule().twoDas(), get("baseitems"))
            .Times(AnyNumber())
            .WillRepeatedly(Return(makeBaseItemsTable()));
    }

    std::shared_ptr<Creature> armedCreature() {
        auto creature = game.newCreature();
        EXPECT_TRUE(creature->equip(InventorySlots::rightWeapon, makeItem(game, "pistol_a", kPistol)));
        EXPECT_TRUE(creature->equip(InventorySlots::leftWeapon, makeItem(game, "pistol_b", kPistol)));
        EXPECT_TRUE(creature->equip(InventorySlots::rightWeapon2, makeItem(game, "saber", kSaber)));
        return creature;
    }

    /** Runs the front action once, as the action queue would. */
    static void runFront(Creature &creature) {
        auto action = creature.getCurrentAction();
        ASSERT_TRUE(action);
        action->execute(action, creature, 0.0f);
    }
};

} // namespace

TEST_F(WeaponSets, k1_session_equips_the_second_set) {
    auto creature = armedCreature();
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon2));
    EXPECT_TRUE(creature->equip(InventorySlots::leftWeapon2, makeItem(game, "saber_b", kSaber)));
    EXPECT_EQ("saber_b", tagAt(*creature, InventorySlots::leftWeapon2));
}

TEST_F(WeaponSets, swap_exchanges_sets_including_empty_slots) {
    auto creature = armedCreature();
    ASSERT_TRUE(creature->swapWeaponSets());
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon));
    EXPECT_EQ("", tagAt(*creature, InventorySlots::leftWeapon));
    EXPECT_EQ("pistol_a", tagAt(*creature, InventorySlots::rightWeapon2));
    EXPECT_EQ("pistol_b", tagAt(*creature, InventorySlots::leftWeapon2));
    for (auto &[slot, item] : creature->equipment()) {
        EXPECT_TRUE(item->isEquipped()) << slot;
        EXPECT_EQ(creature->id(), item->owner()) << slot;
    }

    ASSERT_TRUE(creature->swapWeaponSets());
    EXPECT_EQ("pistol_a", tagAt(*creature, InventorySlots::rightWeapon));
    EXPECT_EQ("pistol_b", tagAt(*creature, InventorySlots::leftWeapon));
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon2));
    EXPECT_EQ("", tagAt(*creature, InventorySlots::leftWeapon2));
    EXPECT_EQ(3u, creature->equipment().size());
}

TEST_F(WeaponSets, swap_with_both_sets_empty_changes_nothing) {
    auto creature = game.newCreature();
    EXPECT_FALSE(creature->swapWeaponSets());
    EXPECT_TRUE(creature->equipment().empty());
}

TEST_F(WeaponSets, script_switch_does_not_clear_queue) {
    auto creature = armedCreature();
    creature->addAction(game.newAction<WaitAction>(5.0f));
    creature->addAction(game.newAction<SwitchWeaponsAction>());
    ASSERT_EQ(2u, creature->actions().size());
    EXPECT_EQ(ActionType::Wait, creature->actions().front()->type());
    // It runs when it reaches the front, not before.
    EXPECT_EQ("pistol_a", tagAt(*creature, InventorySlots::rightWeapon));
    auto swap = creature->actions().back();
    swap->execute(swap, *creature, 0.0f);
    EXPECT_TRUE(swap->isCompleted());
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon));
}

TEST_F(WeaponSets, player_request_out_of_combat_clears_queue_and_swaps) {
    auto creature = armedCreature();
    creature->addAction(game.newAction<WaitAction>(5.0f));
    requestWeaponSwitch(game, *creature);
    ASSERT_EQ(1u, creature->actions().size());
    EXPECT_EQ(ActionType::SwitchWeapons, creature->actions().front()->type());
    runFront(*creature);
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon));
}

TEST_F(WeaponSets, player_request_in_combat_waits_behind_current_action_then_resumes_attack) {
    auto creature = armedCreature();
    auto enemy = game.newCreature();
    creature->activateCombat();
    creature->beginCombatAttack(enemy, FeatType::Invalid);
    creature->addAction(game.newAction<WaitAction>(5.0f));
    requestWeaponSwitch(game, *creature);
    ASSERT_EQ(2u, creature->actions().size());
    EXPECT_EQ(ActionType::Wait, creature->actions().front()->type());
    EXPECT_EQ(ActionType::SwitchWeapons, creature->actions().back()->type());
    EXPECT_EQ("pistol_a", tagAt(*creature, InventorySlots::rightWeapon));

    auto swap = creature->actions().back();
    swap->execute(swap, *creature, 0.0f);
    EXPECT_EQ("saber", tagAt(*creature, InventorySlots::rightWeapon));
    ASSERT_EQ(3u, creature->actions().size());
    EXPECT_EQ(ActionType::AttackObject, creature->actions().back()->type());
}

TEST_F(WeaponSets, k1_save_round_trips_second_set) {
    auto area = game.newArea();
    auto player = game.newCreature();
    TestGameModule::configureModuleSnapshot(game, area, player, "end_m01aa", "end_m01aa");
    TestGameModule::addSnapshotObject(*area, player);
    auto creature = armedCreature();
    creature->setTag("armed");
    TestGameModule::addSnapshotObject(*area, creature);

    auto saved = ModuleSnapshotBuilder(game, "end_m01aa").build();
    ASSERT_TRUE(saved) << saved.message;
    std::shared_ptr<Gff> record;
    for (const auto &entry : saved.snapshot->git->getList("Creature List")) {
        if (entry->getString("Tag") == "armed") {
            record = entry;
        }
    }
    ASSERT_TRUE(record);
    std::set<uint32_t> slotBits;
    for (const auto &item : record->getList("Equip_ItemList")) {
        slotBits.insert(item->type());
    }
    EXPECT_EQ((std::set<uint32_t> {1u << InventorySlots::rightWeapon,
                                   1u << InventorySlots::leftWeapon,
                                   1u << InventorySlots::rightWeapon2}),
              slotBits);

    auto loaded = game.newCreature();
    TestGameModule::deserializeSavedEquipment(*loaded, *record, SerializedIdentityContext::moduleGraph("end_m01aa"));
    EXPECT_EQ("pistol_a", tagAt(*loaded, InventorySlots::rightWeapon));
    EXPECT_EQ("pistol_b", tagAt(*loaded, InventorySlots::leftWeapon));
    EXPECT_EQ("saber", tagAt(*loaded, InventorySlots::rightWeapon2));
    EXPECT_EQ("", tagAt(*loaded, InventorySlots::leftWeapon2));
}

TEST_F(WeaponSets, old_k1_save_without_second_set_loads_empty) {
    auto item = Gff::Builder()
                    .type(1u << InventorySlots::rightWeapon)
                    .field(Gff::Field::newDword("ObjectId", 7001))
                    .field(Gff::Field::newCExoString("Tag", "pistol_a"))
                    .field(Gff::Field::newInt("BaseItem", kPistol))
                    .field(Gff::Field::newWord("StackSize", 1))
                    .build();
    auto record = Gff::Builder()
                      .field(Gff::Field::newList("Equip_ItemList", {item}))
                      .field(Gff::Field::newList("ItemList", {}))
                      .build();
    auto loaded = game.newCreature();
    TestGameModule::deserializeSavedEquipment(*loaded, *record, SerializedIdentityContext::moduleGraph("end_m01aa"));
    EXPECT_EQ("pistol_a", tagAt(*loaded, InventorySlots::rightWeapon));
    EXPECT_FALSE(loaded->getEquippedItem(InventorySlots::rightWeapon2));
    EXPECT_FALSE(loaded->getEquippedItem(InventorySlots::leftWeapon2));
    // Swapping into the empty set leaves the creature unarmed, and back again.
    ASSERT_TRUE(loaded->swapWeaponSets());
    EXPECT_FALSE(loaded->getEquippedItem(InventorySlots::rightWeapon));
    EXPECT_EQ("pistol_a", tagAt(*loaded, InventorySlots::rightWeapon2));
}
