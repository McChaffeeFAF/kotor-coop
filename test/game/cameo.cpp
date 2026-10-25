/* Copyright (c) 2026 The reone project contributors */

// John Shepard cameos (cameo.h, design note shepard_cameos): the table and
// the spawn conditions, the puppet rules (scripts never see him, nothing
// perceives or targets him, a click only barks) and what a save keeps.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>

#include <boost/algorithm/string.hpp>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/cameo.h"
#include "reone/game/coopcast.h"
#include "reone/game/coopstandin.h"
#include "reone/game/d20/class.h"
#include "reone/game/d20/classes.h"
#include "reone/game/game.h"
#include "reone/game/location.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/2da.h"
#include "reone/resource/gff.h"
#include "reone/scene/collision.h"
#include "reone/script/executioncontext.h"
#include "reone/script/variable.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace reone::script;
using namespace testing;

namespace {

CameoInput cameoIn(const std::string &module) {
    CameoInput result;
    result.module = module;
    return result;
}

std::set<std::string> scenes(const std::vector<int> &rows) {
    std::set<std::string> result;
    for (int row : rows) {
        result.insert(cameoRows().at(row).scene);
    }
    return result;
}

} // namespace

// The decision

TEST(Cameo, a_listed_module_gets_its_cameo_and_an_unlisted_one_none) {
    EXPECT_EQ(std::set<std::string>({"Taris apartment"}), scenes(cameosWanted(cameoIn("tar_m02af"))));
    EXPECT_EQ(std::set<std::string>({"Dantooine Enclave courtyard"}), scenes(cameosWanted(cameoIn("danm14aa"))));
    EXPECT_EQ(std::set<std::string>({"Star Forge approach"}), scenes(cameosWanted(cameoIn("sta_m45aa"))));
    EXPECT_EQ(std::set<std::string>({"Ebon Hawk after the Leviathan"}), scenes(cameosWanted(cameoIn("ebo_m40aa"))));
    EXPECT_EQ(std::set<std::string>({"Ebon Hawk after the Leviathan"}), scenes(cameosWanted(cameoIn("ebo_m40ad"))));
    EXPECT_TRUE(cameosWanted(cameoIn("tar_m02aa")).empty());
    EXPECT_TRUE(cameosWanted(cameoIn("end_m01aa")).empty());
    EXPECT_TRUE(cameosWanted(cameoIn("")).empty());
    EXPECT_TRUE(cameosWanted(cameoIn("001ebo")).empty()) << "K2 modules are not in the table";
}

TEST(Cameo, the_module_name_matches_whatever_its_case) {
    EXPECT_EQ(1u, cameosWanted(cameoIn("TAR_M02AF")).size());
}

TEST(Cameo, the_option_off_means_no_cameos_anywhere) {
    for (const char *module : {"tar_m02af", "danm14aa", "sta_m45aa", "ebo_m12aa", "ebo_m40aa"}) {
        auto in = cameoIn(module);
        in.enabled = false;
        EXPECT_TRUE(cameosWanted(in).empty()) << module;
    }
}

TEST(Cameo, a_guest_playing_shepard_in_the_module_means_no_cameo_there) {
    for (const char *module : {"tar_m02af", "danm14aa", "sta_m45aa", "ebo_m12aa", "ebo_m40aa"}) {
        auto in = cameoIn(module);
        EXPECT_FALSE(cameosWanted(in).empty()) << module;
        in.guestPlaysShepard = true;
        EXPECT_TRUE(cameosWanted(in).empty()) << module;
    }
}

TEST(Cameo, the_ebon_hawk_has_one_man_at_a_time_the_lounge_or_the_briefing) {
    auto in = cameoIn("ebo_m12aa");
    std::map<std::string, int> planet;
    in.globalNumber = [&planet](const std::string &name) { return planet[name]; };

    planet["K_CURRENT_PLANET"] = 15; // Dantooine chosen: the lounge
    auto lounge = cameosWanted(in);
    ASSERT_EQ(1u, lounge.size());
    EXPECT_EQ(std::string("Ebon Hawk lounge"), cameoRows().at(lounge[0]).scene);

    planet["K_CURRENT_PLANET"] = 10; // still at Taris: the briefing
    auto briefing = cameosWanted(in);
    ASSERT_EQ(1u, briefing.size());
    EXPECT_EQ(std::string("Ebon Hawk briefing after Taris"), cameoRows().at(briefing[0]).scene);
    EXPECT_NE(lounge[0], briefing[0]);

    in.globalNumber = nullptr; // a global nothing can read counts as 0
    auto unread = cameosWanted(in);
    ASSERT_EQ(1u, unread.size());
    EXPECT_EQ(lounge[0], unread[0]);
}

TEST(Cameo, conditions_read_booleans_and_numbers) {
    CameoInput in;
    in.globalBoolean = [](const std::string &name) { return name == "ON"; };
    in.globalNumber = [](const std::string &name) { return name == "N" ? 7 : 0; };
    EXPECT_TRUE(cameoConditionHolds("", in));
    EXPECT_TRUE(cameoConditionHolds("ON", in));
    EXPECT_FALSE(cameoConditionHolds("OFF", in));
    EXPECT_TRUE(cameoConditionHolds("!OFF", in));
    EXPECT_FALSE(cameoConditionHolds("!ON", in));
    EXPECT_TRUE(cameoConditionHolds("N=7", in));
    EXPECT_FALSE(cameoConditionHolds("N=8", in));
    EXPECT_TRUE(cameoConditionHolds("N!=8", in));
    EXPECT_TRUE(cameoConditionHolds("N>=7", in));
    EXPECT_FALSE(cameoConditionHolds("N>7", in));
    EXPECT_TRUE(cameoConditionHolds("N<8", in));
    EXPECT_TRUE(cameoConditionHolds("ON & N=7", in));
    EXPECT_FALSE(cameoConditionHolds("ON & N=8", in));
    EXPECT_FALSE(cameoConditionHolds("OFF & N=7", in));
    EXPECT_TRUE(cameoConditionHolds("MISSING=0", in)) << "an unread number is 0";
}

TEST(Cameo, every_row_is_well_formed) {
    for (const auto &row : cameoRows()) {
        std::string module = row.module;
        EXPECT_EQ(module, boost::to_lower_copy(module));
        EXPECT_FALSE(std::string(row.scene).empty()) << module;
        EXPECT_NE(std::string(row.body), "") << module << ": an outfit";
        EXPECT_TRUE(std::string(row.body).rfind("g_a_clothes", 0) == 0) << module << ": neutral clothing, no weapon";
        AnimationType animation;
        std::string pose = row.pose;
        EXPECT_TRUE(pose == "stand" || cameoPoseAnimation(pose, animation)) << module << ": pose " << pose;
        EXPECT_GE(row.yaw, -180.0f);
        EXPECT_LE(row.yaw, 180.0f);
    }
    EXPECT_FALSE(cameoRows().empty());
    EXPECT_LE(std::string(kCoopCameoTemplate).size(), 16u);
    EXPECT_TRUE(isCoopTemplateName(kCoopCameoTemplate)) << "the wire accepts the cameo template";
    EXPECT_NE(std::string(kCoopCameoTemplate), kCoopShepardTemplate) << "a guest's Shepard and a cameo are told apart";
}

TEST(Cameo, rows_never_share_a_spot_in_a_module_unless_their_conditions_exclude_each_other) {
    const auto &rows = cameoRows();
    for (size_t i = 0; i < rows.size(); ++i) {
        for (size_t j = i + 1; j < rows.size(); ++j) {
            if (std::string(rows[i].module) != rows[j].module) {
                continue;
            }
            bool sameSpot = std::abs(rows[i].x - rows[j].x) < 1.0f && std::abs(rows[i].y - rows[j].y) < 1.0f;
            EXPECT_FALSE(sameSpot) << rows[i].scene << " and " << rows[j].scene;
        }
    }
}

TEST(Cameo, a_row_is_found_by_its_spot) {
    ASSERT_GE(cameoRows().size(), 2u);
    const auto &row = cameoRows().front();
    EXPECT_EQ(0, cameoRowNear(row.module, row.x + 0.5f, row.y - 0.5f));
    EXPECT_EQ(-1, cameoRowNear(row.module, row.x + 20.0f, row.y));
    EXPECT_EQ(-1, cameoRowNear("tar_m02aa", row.x, row.y));
}

// The stage prop

namespace {

std::shared_ptr<TwoDA> table(const std::vector<std::string> &columns, const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

class CameoStageTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        ON_CALL(_sceneGraph, testElevation(_, _))
            .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
                collision.intersection = glm::vec3(position.x, position.y, 0.25f);
                collision.user = nullptr;
                return true;
            }));
        // A class for him to be (classes.2da rows 0-8 all a soldier here).
        auto &twoDas = _engine.resourceModule().twoDas();
        const std::vector<std::string> columns = {"name", "description", "hitdie", "skillpointbase", "str", "dex", "con", "int", "wis", "cha",
                                                  "skillstable", "savingthrowtable", "attackbonustable", "featstable", "featgain", "spellgaintable", "forcedie", "spellcaster"};
        std::vector<std::vector<std::string>> classRows(9, {"1", "2", "10", "2", "16", "14", "14", "10", "12", "10", "sol", "save", "attack", "sol", "", "", "0", "0"});
        ON_CALL(twoDas, get("classes")).WillByDefault(Return(table(columns, classRows)));
        ON_CALL(twoDas, get("skills")).WillByDefault(Return(table({"label", "sol_class", "sol_reco"}, {{"computeruse", "0", ""}})));
        ON_CALL(twoDas, get("save")).WillByDefault(Return(table({"level", "fortsave", "refsave", "willsave"}, {{"1", "0", "0", "0"}})));
        ON_CALL(twoDas, get("attack")).WillByDefault(Return(table({"bab"}, {{"0"}})));
        _classes = std::make_shared<Classes>(_engine.resourceModule().strings(), twoDas);
        ON_CALL(_engine.gameModule().classes(), get(_)).WillByDefault(Invoke([this](ClassType type) { return _classes->get(type); }));

        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
        _player = _game->newCreature();
        _player->setTag("player");
        _player->setFaction(Faction::Player);
        _player->setPosition(glm::vec3(0.0f));
        _area->add(_player);
        _game->party().addMember(kNpcPlayer, _player);
        _game->party().setPlayer(_player);
        _game->cameos().lenient = true; // the test engine has no screens
    }

    void enterModule(const std::string &name) {
        auto ifo = Gff::Builder()
                       .field(Gff::Field::newCExoLocString("Mod_Name", -1, name))
                       .field(Gff::Field::newResRef("Mod_Entry_Area", "entryarea"))
                       .build();
        TestGameModule::loadModuleInfo(*_game->module(), name, *ifo);
    }

    std::vector<std::shared_ptr<Creature>> cameos() {
        std::vector<std::shared_ptr<Creature>> result;
        for (const auto &object : _area->getObjectsByType(ObjectType::Creature)) {
            auto creature = std::static_pointer_cast<Creature>(object);
            if (creature->isCameo() && !_area->isObjectPendingDestruction(*creature)) {
                result.push_back(creature);
            }
        }
        return result;
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, glm::vec3 position, Faction faction = Faction::Hostile1) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        creature->setPosition(position);
        creature->setFaction(faction);
        _area->add(creature);
        return creature;
    }

    Variable call(const std::string &name, std::vector<Variable> args, const std::shared_ptr<Object> &caller = nullptr) {
        _execution.args.erase(
            std::remove_if(_execution.args.begin(), _execution.args.end(), [](const Argument &arg) { return arg.kind == ArgKind::Caller; }),
            _execution.args.end());
        if (caller) {
            _execution.args.insert(_execution.args.begin(), Argument(ArgKind::Caller, Variable::ofObject(caller->id())));
        }
        _execution.routines = _routines.get();
        return _routines->get(_routines->getIndexByName(name)).invoke(args, _execution);
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::shared_ptr<Classes> _classes;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _player;
    ExecutionContext _execution;
};

} // namespace

TEST_F(CameoStageTest, john_shepard_stands_at_the_taris_apartment_spot_on_the_floor) {
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    auto found = cameos();
    ASSERT_EQ(1u, found.size());
    auto &cameo = *found[0];
    const auto &row = cameoRows().at(cameosWanted(cameoIn("tar_m02af")).at(0));
    EXPECT_EQ(coopShepardName(), cameo.name());
    EXPECT_EQ(coopShepard().appearance, cameo.appearance());
    EXPECT_EQ(coopShepard().portrait, cameo.portraitId());
    EXPECT_FLOAT_EQ(row.x, cameo.position().x);
    EXPECT_FLOAT_EQ(row.y, cameo.position().y);
    EXPECT_FLOAT_EQ(0.25f, cameo.position().z) << "on the walkmesh";
    EXPECT_EQ(std::string(kCameoTag), cameo.tag()) << "a tag no script keys on";
    EXPECT_EQ(1, _game->party().getSize());

    // A second look does not add a second man; the row is spent.
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());
}

TEST_F(CameoStageTest, he_has_no_part_in_the_story_a_puppet_with_nothing_to_run) {
    enterModule("danm14aa");
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    auto cameo = cameos()[0];
    EXPECT_TRUE(cameo->isPuppet());
    EXPECT_FALSE(_game->party().isMember(*cameo));
    EXPECT_EQ(1, _game->party().getSize());
    EXPECT_TRUE(cameo->conversation().empty()) << "not recruitable, nothing to talk about";
    EXPECT_TRUE(cameo->plotFlag()) << "nothing can hurt him";
    EXPECT_EQ(Faction::Player, cameo->faction()) << "no hostile's enemy";
    EXPECT_TRUE(cameo->getOnHeartbeat().empty());
    EXPECT_TRUE(cameo->getOnDialogue().empty());
    EXPECT_TRUE(cameo->getOnDisturbed().empty());
    EXPECT_TRUE(cameo->getOnEndDialogue().empty());
    EXPECT_TRUE(cameo->getOnSpellAt().empty());
    EXPECT_TRUE(cameo->getOnEndRound().empty());
    for (const auto &[slot, item] : cameo->equipment()) {
        if (item) {
            EXPECT_FALSE(item->isDropable()) << "never loot";
        }
    }
}

TEST_F(CameoStageTest, the_option_removes_him_and_brings_him_back) {
    enterModule("sta_m45aa");
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());

    _engine.options().game.cameos = false;
    _game->cameos().update(*_game);
    EXPECT_TRUE(cameos().empty());

    _engine.options().game.cameos = true;
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());
}

TEST_F(CameoStageTest, the_option_off_from_the_start_never_spawns_him) {
    _engine.options().game.cameos = false;
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    EXPECT_TRUE(cameos().empty());
}

TEST_F(CameoStageTest, an_unlisted_module_gets_no_cameo_and_a_spot_without_floor_none) {
    enterModule("tar_m02aa");
    _game->cameos().update(*_game);
    EXPECT_TRUE(cameos().empty());

    enterModule("tar_m02af");
    ON_CALL(_sceneGraph, testElevation(_, _)).WillByDefault(Return(false));
    _game->cameos().update(*_game);
    EXPECT_TRUE(cameos().empty()) << "a spot the walkmesh does not have gets no cameo";

    // The room streams in later: he turns up then, once.
    ON_CALL(_sceneGraph, testElevation(_, _))
        .WillByDefault(Invoke([](const glm::vec3 &position, scene::Collision &collision) {
            collision.intersection = glm::vec3(position.x, position.y, 0.25f);
            collision.user = nullptr;
            return true;
        }));
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());
}

TEST_F(CameoStageTest, the_ebon_hawk_swaps_the_lounge_for_the_briefing_while_the_planet_says_taris) {
    enterModule("ebo_m12aa");
    _game->setGlobalNumber("K_CURRENT_PLANET", 15);
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    glm::vec3 lounge = cameos()[0]->position();

    _game->setGlobalNumber("K_CURRENT_PLANET", 10);
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    EXPECT_NE(lounge, cameos()[0]->position());
}

TEST_F(CameoStageTest, script_searches_never_find_him) {
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    auto cameo = cameos()[0];
    // A bystander and a hostile close to him (both Player/Hostile factions the searches know).
    auto bystander = addCreature("bystander", cameo->position() + glm::vec3(3.0f, 0.0f, 0.0f), Faction::Player);
    addCreature("hostile", cameo->position() + glm::vec3(30.0f, 0.0f, 0.0f), Faction::Hostile1);

    // Nearest object of the creature type, from beside him.
    auto nearestObject = call("GetNearestObject", {Variable::ofInt(1), Variable::ofObject(cameo->id()), Variable::ofInt(1)}, _player);
    EXPECT_NE(cameo->id(), nearestObject.objectId);
    // Objects in the area: every creature but him.
    std::set<uint32_t> seen;
    uint32_t id = call("GetFirstObjectInArea", {Variable::ofObject(_area->id()), Variable::ofInt(1)}, _player).objectId;
    // GetNext keeps its own cursor in the execution; collect what First returned and what Next returns.
    while (id != kObjectInvalid && seen.size() < 16) {
        seen.insert(id);
        id = call("GetNextObjectInArea", {Variable::ofObject(_area->id()), Variable::ofInt(1)}, _player).objectId;
    }
    EXPECT_EQ(0u, seen.count(cameo->id()));
    EXPECT_EQ(1u, seen.count(bystander->id()));
    // Objects in a shape around him.
    std::vector<Variable> shape {Variable::ofInt(4), Variable::ofFloat(2.0f),
                                 Variable::ofLocation(std::make_shared<Location>(cameo->position(), 0.0f)), Variable::ofInt(0),
                                 Variable::ofInt(1), Variable::ofVector(glm::vec3(0.0f))};
    EXPECT_EQ(kObjectInvalid, call("GetFirstObjectInShape", shape, _player).objectId);
    // A faction loop over the Player faction: the player and the bystander, not him.
    std::set<uint32_t> members;
    uint32_t member = call("GetFirstFactionMember", {Variable::ofObject(_player->id()), Variable::ofInt(0)}, _player).objectId;
    while (member != kObjectInvalid && members.size() < 16) {
        members.insert(member);
        member = call("GetNextFactionMember", {Variable::ofObject(_player->id()), Variable::ofInt(0)}, _player).objectId;
    }
    EXPECT_EQ(0u, members.count(cameo->id()));
    EXPECT_EQ(1u, members.count(bystander->id()));
    // The nearest creature to a spot beside him.
    auto nearest = _area->getNearestCreatureToLocation(Location(cameo->position(), 0.0f), {{CreatureType::IsAlive, 1}}, 0);
    ASSERT_TRUE(nearest);
    EXPECT_NE(cameo->id(), nearest->id());
}

TEST_F(CameoStageTest, nothing_perceives_him_and_he_perceives_nothing) {
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    auto cameo = cameos()[0];
    auto guard = addCreature("guard", cameo->position() + glm::vec3(2.0f, 0.0f, 0.0f), Faction::Hostile1);
    _area->refreshPerceptionFor(*cameo);
    _area->refreshPerceptionFor(*guard);
    EXPECT_FALSE(guard->perception().sees(cameo->id()));
    EXPECT_FALSE(guard->perception().hears(cameo->id()));
    EXPECT_FALSE(cameo->perception().sees(guard->id()));
}

TEST_F(CameoStageTest, a_click_on_him_only_barks) {
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());
    auto cameo = cameos()[0];
    ASSERT_TRUE(_game->module());
    TestGameModule::clickCreature(*_game->module(), cameo);
    EXPECT_TRUE(_player->actions().empty()) << "no attack, no walk, no conversation";
    EXPECT_EQ(Game::Screen::None, _game->currentScreen()) << "no dialogue opened";
}

TEST_F(CameoStageTest, the_area_unloading_forgets_him_and_the_next_visit_spawns_him_again) {
    enterModule("tar_m02af");
    _game->cameos().update(*_game);
    ASSERT_EQ(1u, cameos().size());

    // A new area: the old one's objects went with it.
    auto next = _game->newArea();
    TestGameModule::replaceActiveModule(*_game, next);
    _game->cameos().update(*_game); // no leader in the new module yet: nothing, and the director forgets
    EXPECT_TRUE(_game->cameos().active().empty());
    _area = next;
    auto ifo = Gff::Builder()
                   .field(Gff::Field::newCExoLocString("Mod_Name", -1, "tar_m02af"))
                   .field(Gff::Field::newResRef("Mod_Entry_Area", "entryarea"))
                   .build();
    TestGameModule::loadModuleInfo(*_game->module(), "tar_m02af", *ifo);
    auto player = _game->newCreature();
    player->setTag("player");
    _area->add(player);
    _game->party().addMember(kNpcPlayer, player);
    _game->party().setPlayer(player);
    _game->cameos().update(*_game);
    EXPECT_EQ(1u, cameos().size());
}
