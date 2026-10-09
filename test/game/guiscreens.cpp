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

// Gameplay screens: the store's prices and trades, item upgrades and the
// upgrade bench, and the screen-opening routines. The notes these pin are in
// design note gui_screens.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"
#include "../fixtures/resource.h"

#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/store.h"
#include "reone/game/object/waypoint.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/game/storesession.h"
#include "reone/game/upgradebench.h"
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

constexpr int kSaber = 8;
constexpr int kPistol = 12;
constexpr int kArmor = 38;
constexpr int kUpgradeItem = 64;

// baseitems.2da rows the items below use: a lightsaber, a blaster, body
// armour and the upgrade parts' base item.
std::shared_ptr<TwoDA> makeBaseItemsTable() {
    TwoDA::Builder builder;
    builder.columns({"equipableslots", "itemclass", "weapontype", "stacking"});
    for (int i = 0; i <= kUpgradeItem; ++i) {
        if (i == kSaber) {
            builder.row({"0x00030", "w_lghtsbr", "1", "1"});
        } else if (i == kPistol) {
            builder.row({"0x00030", "w_blstrpstl", "4", "1"});
        } else if (i == kArmor) {
            builder.row({"0x00002", "a_class4", "", "1"});
        } else if (i == kUpgradeItem) {
            builder.row({"", "p_pltuseitm", "", "10"});
        } else {
            builder.row({"", "", "", ""});
        }
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

// upgrade.2da as K1 ships its first rows and the blaster/armour parts.
std::shared_ptr<TwoDA> makeUpgradeTable() {
    TwoDA::Builder builder;
    builder.columns({"label", "template", "upgradetype"});
    builder.row({"Rubat_crystal", "G_W_SBRCRSTL01", "0"});  // 0
    builder.row({"Damind_crystal", "G_W_SBRCRSTL02", "0"}); // 1
    builder.row({"Scope", "G_I_UPGRADE001", "4"});          // 2
    builder.row({"Imp_Energy_Cell", "G_I_UPGRADE002", "5"});// 3
    builder.row({"Armor_Reinforcement", "G_I_UPGRADE005", "8"}); // 4
    return std::shared_ptr<TwoDA>(builder.build());
}

struct PropertySpec {
    int propertyName;
    int costValue;
    std::optional<int> upgradeType; // no field when empty
};

std::shared_ptr<Gff> itemGff(const std::string &tag, int baseItem, int addCost, bool plot = false,
                             std::vector<PropertySpec> properties = {}, int stack = 1,
                             std::optional<uint32_t> upgrades = std::nullopt) {
    std::vector<std::shared_ptr<Gff>> props;
    for (const auto &spec : properties) {
        Gff::Builder prop;
        prop.type(0)
            .field(Gff::Field::newWord("PropertyName", static_cast<uint16_t>(spec.propertyName)))
            .field(Gff::Field::newWord("CostValue", static_cast<uint16_t>(spec.costValue)));
        if (spec.upgradeType) {
            prop.field(Gff::Field::newByte("UpgradeType", static_cast<uint8_t>(*spec.upgradeType)));
        }
        props.push_back(prop.build());
    }
    Gff::Builder builder;
    builder.field(Gff::Field::newCExoString("Tag", tag))
        .field(Gff::Field::newResRef("TemplateResRef", tag))
        .field(Gff::Field::newInt("BaseItem", baseItem))
        .field(Gff::Field::newDword("AddCost", static_cast<uint32_t>(addCost)))
        .field(Gff::Field::newDword("Cost", static_cast<uint32_t>(addCost)))
        .field(Gff::Field::newByte("Plot", plot ? 1 : 0))
        .field(Gff::Field::newWord("StackSize", static_cast<uint16_t>(stack)))
        .field(Gff::Field::newList("PropertiesList", std::move(props)));
    if (upgrades) {
        builder.field(Gff::Field::newDword("Upgrades", *upgrades));
    }
    return builder.build();
}

class GuiScreensTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_engine.resourceModule().twoDas(), get("baseitems")).WillByDefault(Return(makeBaseItemsTable()));
        ON_CALL(_engine.resourceModule().twoDas(), get("upgrade")).WillByDefault(Return(makeUpgradeTable()));
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);

        _player = _game->newCreature();
        _player->setTag("player");
        _area->add(_player);
        _game->party().addMember(kNpcPlayer, _player);
        _game->party().setPlayer(_player);
    }

    std::shared_ptr<Item> makeItem(const std::string &tag, int baseItem, int addCost, bool plot = false,
                                   std::vector<PropertySpec> properties = {}, int stack = 1,
                                   std::optional<uint32_t> upgrades = std::nullopt) {
        auto item = _game->newItem();
        item->deserialize(*itemGff(tag, baseItem, addCost, plot, std::move(properties), stack, upgrades),
                          SerializedIdentityContext::templateResource(tag));
        return item;
    }

    std::shared_ptr<Item> give(const std::string &tag, int baseItem, int addCost, bool plot = false,
                               std::vector<PropertySpec> properties = {}, int stack = 1) {
        auto item = makeItem(tag, baseItem, addCost, plot, std::move(properties), stack);
        _player->addItem(item);
        return _player->getItemByTag(tag);
    }

    std::shared_ptr<Store> makeStore(int markUp, int markDown, uint8_t flag = 3) {
        auto store = _game->newStore();
        store->setTag("merchant");
        store->setMarkUp(markUp);
        store->setMarkDown(markDown);
        store->setBuySellFlag(flag);
        _area->add(store);
        return store;
    }

    std::shared_ptr<Item> stock(Store &store, const std::string &tag, int addCost, int stack, bool infinite) {
        auto item = makeItem(tag, kPistol, addCost, false, {}, stack);
        item->setInfinite(infinite);
        store.addItem(item);
        for (auto &held : store.items()) {
            if (held->tag() == tag) {
                return held;
            }
        }
        return nullptr;
    }

    Variable call(const std::string &name, std::vector<Variable> args) {
        Routine &routine = _routines->get(_routines->getIndexByName(name));
        ExecutionContext execution;
        execution.routines = _routines.get();
        return routine.invoke(args, execution);
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _player;
};

} // namespace

// --- Prices -------------------------------------------------------------------

TEST_F(GuiScreensTest, item_value_is_add_cost_zero_for_plot_and_at_least_one) {
    EXPECT_EQ(250, StoreSession::itemValue(*makeItem("a", kPistol, 250)));
    EXPECT_EQ(0, StoreSession::itemValue(*makeItem("b", kPistol, 250, /*plot=*/true)));
    EXPECT_EQ(1, StoreSession::itemValue(*makeItem("c", kPistol, 0)));
}

TEST_F(GuiScreensTest, item_value_is_the_template_cost_not_only_add_cost) {
    // The K1 short sword: Cost 20 (base cost + AddCost), AddCost 0.
    auto gff = Gff::Builder()
                   .field(Gff::Field::newCExoString("Tag", "sword"))
                   .field(Gff::Field::newInt("BaseItem", kPistol))
                   .field(Gff::Field::newDword("Cost", 20))
                   .field(Gff::Field::newDword("AddCost", 0))
                   .build();
    auto item = _game->newItem();
    item->deserialize(*gff, SerializedIdentityContext::templateResource("sword"));
    EXPECT_EQ(20, StoreSession::itemValue(*item));
}

TEST(StorePrices, percent_is_integer_division_and_never_negative) {
    EXPECT_EQ(140, StoreSession::applyPercent(100, 140));
    EXPECT_EQ(33, StoreSession::applyPercent(99, 34));
    EXPECT_EQ(0, StoreSession::applyPercent(100, -20));
    EXPECT_EQ(0, StoreSession::applyPercent(0, 100));
}

TEST_F(GuiScreensTest, bonus_mark_up_and_down_add_to_the_store_percentages) {
    auto store = makeStore(100, 40);
    auto item = makeItem("blaster", kPistol, 200);
    _game->storeSession().open(store, _player, -20, 10);
    EXPECT_EQ(160, _game->storeSession().buyPrice(*item)); // 200 x 80%
    EXPECT_EQ(100, _game->storeSession().sellPrice(*item)); // 200 x 50%
}

// --- Trades -------------------------------------------------------------------

TEST_F(GuiScreensTest, buying_takes_gold_and_moves_one_unit_from_a_finite_stack) {
    auto store = makeStore(100, 40);
    auto finite = stock(*store, "medpac", 50, 3, false);
    _game->party().giveGold(120);
    _game->storeSession().open(store, _player, 0, 0);

    EXPECT_EQ(StoreSession::Result::Ok, _game->storeSession().buy(finite, _player.get()));
    EXPECT_EQ(70, _game->party().gold());
    EXPECT_EQ(2, finite->stackSize());
    auto bought = _player->getItemByTag("medpac");
    ASSERT_TRUE(bought);
    EXPECT_EQ(1, bought->stackSize());
    EXPECT_FALSE(bought->isInfinite());
}

TEST_F(GuiScreensTest, an_infinite_entry_never_runs_out) {
    auto store = makeStore(100, 40);
    auto infinite = stock(*store, "grenade", 10, 1, true);
    _game->party().giveGold(100);
    _game->storeSession().open(store, _player, 0, 0);
    for (int i = 0; i < 3; ++i) {
        ASSERT_EQ(StoreSession::Result::Ok, _game->storeSession().buy(infinite, _player.get()));
    }
    EXPECT_EQ(70, _game->party().gold());
    ASSERT_EQ(1u, store->items().size());
    EXPECT_EQ(1, store->items().front()->stackSize());
}

TEST_F(GuiScreensTest, a_failed_buy_changes_nothing) {
    auto store = makeStore(100, 40);
    auto item = stock(*store, "rifle", 500, 1, false);
    _game->party().giveGold(100);
    _game->storeSession().open(store, _player, 0, 0);
    EXPECT_EQ(StoreSession::Result::NotEnoughGold, _game->storeSession().buy(item, _player.get()));
    EXPECT_EQ(100, _game->party().gold());
    EXPECT_EQ(1u, store->items().size());
    EXPECT_FALSE(_player->getItemByTag("rifle"));

    auto buyOnly = makeStore(100, 40, Store::kSellsToPlayer);
    _game->storeSession().open(buyOnly, _player, 0, 0);
    auto mine = give("sword", kPistol, 100);
    EXPECT_EQ(StoreSession::Result::StoreDoesNotBuy, _game->storeSession().sell(mine, _player.get()));
    EXPECT_EQ(100, _game->party().gold());
}

TEST_F(GuiScreensTest, selling_pays_the_mark_down_and_the_store_keeps_the_unit) {
    auto store = makeStore(100, 40);
    auto mine = give("sword", kPistol, 100, false, {}, 2);
    _game->storeSession().open(store, _player, 0, 0);
    ASSERT_EQ(StoreSession::Result::Ok, _game->storeSession().sell(mine, _player.get()));
    EXPECT_EQ(40, _game->party().gold());
    EXPECT_EQ(1, mine->stackSize());
    ASSERT_EQ(1u, store->items().size());
    EXPECT_EQ("sword", store->items().front()->tag());
    EXPECT_FALSE(store->items().front()->isInfinite());
}

TEST_F(GuiScreensTest, plot_items_are_not_offered_for_sale) {
    auto store = makeStore(100, 40);
    auto plot = give("datapad", kPistol, 100, /*plot=*/true);
    give("sword", kPistol, 100);
    _game->storeSession().open(store, _player, 0, 0);
    auto sellable = _game->storeSession().sellable();
    ASSERT_EQ(1u, sellable.size());
    EXPECT_EQ("sword", sellable.front()->tag());
    EXPECT_EQ(StoreSession::Result::NotSellable, _game->storeSession().sell(plot, _player.get()));
}

TEST_F(GuiScreensTest, only_the_host_party_may_trade) {
    auto store = makeStore(100, 40);
    auto item = stock(*store, "medpac", 10, 1, true);
    _game->party().giveGold(100);
    _game->storeSession().open(store, _player, 0, 0);
    auto stranger = _game->newCreature();
    EXPECT_FALSE(_game->storeSession().mayTransact(stranger.get()));
    EXPECT_EQ(StoreSession::Result::NotAllowed, _game->storeSession().buy(item, stranger.get()));
    EXPECT_EQ(100, _game->party().gold());
    EXPECT_TRUE(_game->storeSession().mayTransact(_player.get()));
}

// --- Item upgrades ------------------------------------------------------------

TEST_F(GuiScreensTest, a_property_with_an_upgrade_type_is_live_only_when_installed) {
    // Row 0 (Rubat) is a real upgrade: "no field" and "row 0" differ.
    auto saber = makeItem("saber", kSaber, 900, false, {{38, 1, std::nullopt}, {38, 2, 0}});
    ASSERT_EQ(2u, saber->properties().size());
    EXPECT_TRUE(saber->isPropertyActive(saber->properties()[0]));
    EXPECT_FALSE(saber->isPropertyActive(saber->properties()[1]));
    saber->setUpgrades(1u << 0);
    EXPECT_TRUE(saber->isPropertyActive(saber->properties()[1]));
    EXPECT_TRUE(saber->isUpgradeable());
    EXPECT_EQ(std::vector<int>({0}), saber->upgradeRows());
}

TEST_F(GuiScreensTest, upgrades_are_read_from_the_item_field) {
    auto item = makeItem("rifle", kPistol, 100, false, {{38, 1, 2}}, 1, 0x4u);
    EXPECT_EQ(0x4u, item->upgrades());
    EXPECT_TRUE(item->isPropertyActive(item->properties()[0]));
}

TEST_F(GuiScreensTest, a_lightsaber_has_a_colour_slot_and_two_power_crystal_slots) {
    UpgradeTable table(*makeUpgradeTable());
    auto saber = makeItem("saber", kSaber, 900, false, {{38, 1, 0}, {38, 2, 1}});
    saber->setModelVariation(3);
    auto slots = upgradeSlotsFor(*saber, 1u << 1, table);
    ASSERT_EQ(3u, slots.size());
    EXPECT_TRUE(slots[0].color);
    EXPECT_EQ(1, slots[1].installedRow);
    EXPECT_EQ(-1, slots[2].installedRow);
    EXPECT_EQ(UpgradeCategory::Lightsaber, upgradeCategoryOf(*saber));
    EXPECT_EQ(3, colorCrystalModelVariation("G_W_SBRCRSTL16"));
    EXPECT_EQ("g_w_sbrcrstl18", colorCrystalForModelVariation(2));
}

TEST_F(GuiScreensTest, other_items_have_one_slot_per_upgrade_type_in_type_order) {
    UpgradeTable table(*makeUpgradeTable());
    auto blaster = makeItem("blaster", kPistol, 100, false, {{11, 1, 3}, {38, 1, 2}});
    auto slots = upgradeSlotsFor(*blaster, 0, table);
    ASSERT_EQ(2u, slots.size());
    EXPECT_EQ(4, slots[0].type);
    EXPECT_EQ(5, slots[1].type);
    EXPECT_EQ(UpgradeCategory::Ranged, upgradeCategoryOf(*blaster));
    auto plain = makeItem("plain", kPistol, 100);
    EXPECT_EQ(UpgradeCategory::None, upgradeCategoryOf(*plain));
}

TEST_F(GuiScreensTest, assembling_installs_and_consumes_the_upgrade_item) {
    auto blaster = give("blaster", kPistol, 100, false, {{38, 1, 2}, {11, 1, 3}});
    auto scope = give("g_i_upgrade001", kUpgradeItem, 200, false, {}, 2);
    UpgradeBench bench(*_game, _engine.services());
    ASSERT_TRUE(bench.begin(blaster));
    auto candidates = bench.candidates(0);
    ASSERT_EQ(1u, candidates.size());
    EXPECT_EQ(scope, candidates.front());
    EXPECT_TRUE(bench.candidates(1).empty()); // no energy cell held
    ASSERT_TRUE(bench.install(0, scope));
    EXPECT_TRUE(bench.candidates(0).empty()); // the slot is full now
    EXPECT_EQ(0u, blaster->upgrades());       // nothing applies before Assemble
    ASSERT_TRUE(bench.assemble());
    EXPECT_EQ(1u << 2, blaster->upgrades());
    EXPECT_EQ(1, scope->stackSize());
    EXPECT_TRUE(blaster->isPropertyActive(blaster->properties()[0]));
    EXPECT_FALSE(blaster->isPropertyActive(blaster->properties()[1]));
}

TEST_F(GuiScreensTest, cancelling_the_bench_discards_staged_slots) {
    auto blaster = give("blaster", kPistol, 100, false, {{38, 1, 2}});
    auto scope = give("g_i_upgrade001", kUpgradeItem, 200);
    UpgradeBench bench(*_game, _engine.services());
    ASSERT_TRUE(bench.begin(blaster));
    ASSERT_TRUE(bench.install(0, scope));
    EXPECT_TRUE(bench.isDirty());
    bench.cancel();
    EXPECT_EQ(0u, blaster->upgrades());
    EXPECT_EQ(scope, _player->getItemByTag("g_i_upgrade001"));
}

TEST_F(GuiScreensTest, removing_an_installed_upgrade_returns_its_item) {
    auto uti = itemGff("g_i_upgrade001", kUpgradeItem, 200);
    ON_CALL(_engine.resourceModule().gffs(), get("g_i_upgrade001", ResType::Uti)).WillByDefault(Return(uti));
    auto blaster = give("blaster", kPistol, 100, false, {{38, 1, 2}});
    blaster->setUpgrades(1u << 2);
    UpgradeBench bench(*_game, _engine.services());
    ASSERT_TRUE(bench.begin(blaster));
    ASSERT_EQ(2, bench.slots()[0].installedRow);
    ASSERT_TRUE(bench.remove(0));
    ASSERT_TRUE(bench.assemble());
    EXPECT_EQ(0u, blaster->upgrades());
    EXPECT_TRUE(_player->getItemByTag("g_i_upgrade001"));
}

TEST_F(GuiScreensTest, a_colour_crystal_swaps_the_blade_colour) {
    auto saber = give("saber", kSaber, 900, false, {{38, 1, 0}});
    saber->setModelVariation(1); // blue
    auto red = give("g_w_sbrcrstl18", 11, 100);
    UpgradeBench bench(*_game, _engine.services());
    ASSERT_TRUE(bench.begin(saber));
    EXPECT_FALSE(bench.remove(0)); // a saber always keeps a colour
    auto candidates = bench.candidates(0);
    ASSERT_EQ(1u, candidates.size());
    ASSERT_TRUE(bench.install(0, red));
    ASSERT_TRUE(bench.assemble());
    EXPECT_EQ(2, saber->modelVariation());
    EXPECT_FALSE(_player->getItemByTag("g_w_sbrcrstl18"));
}

// --- Routines -----------------------------------------------------------------

TEST_F(GuiScreensTest, show_level_up_gui_answers_whether_a_level_is_pending) {
    // Level 0 needs no XP, so the player has a level pending.
    EXPECT_EQ(1, call("ShowLevelUpGUI", {}).intValue);
    _game->party().reset();
    EXPECT_EQ(0, call("ShowLevelUpGUI", {}).intValue);
}

TEST_F(GuiScreensTest, set_map_pin_enabled_shows_a_waypoint_note) {
    auto pin = _game->newWaypoint();
    _area->add(pin);
    call("SetMapPinEnabled", {Variable::ofObject(pin->id()), Variable::ofInt(1)});
    EXPECT_TRUE(pin->isMapNoteEnabled());
    call("SetMapPinEnabled", {Variable::ofObject(pin->id()), Variable::ofInt(0)});
    EXPECT_FALSE(pin->isMapNoteEnabled());
    // Anything but a waypoint is ignored.
    call("SetMapPinEnabled", {Variable::ofObject(_player->id()), Variable::ofInt(1)});
}

TEST_F(GuiScreensTest, open_store_and_the_movie_routines_do_not_throw) {
    auto store = makeStore(100, 40);
    call("OpenStore", {Variable::ofObject(store->id()), Variable::ofObject(kObjectInvalid), Variable::ofInt(0), Variable::ofInt(0)});
    call("ShowUpgradeScreen", {Variable::ofObject(kObjectInvalid)});
    EXPECT_EQ(0, call("IsCreditSequenceInProgress", {}).intValue);
    EXPECT_EQ(0, call("IsMoviePlaying", {}).intValue);
    call("QueueMovie", {Variable::ofString("55"), Variable::ofInt(1)});
    call("PlayMovieQueue", {Variable::ofInt(1)});
    EXPECT_FALSE(_game->isMoviePlaying()); // no movie resource in the test
}
