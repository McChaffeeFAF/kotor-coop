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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <boost/json.hpp>

#include "../fixtures/engine.h"
#include "reone/game/action/attackobject.h"
#include "reone/game/action/movetopoint.h"
#include "reone/game/action/wait.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/resource/dialog.h"
#include "reone/scene/graph.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace json = boost::json;

namespace {

class DumpConversation : public Conversation {
public:
    DumpConversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}
};

std::shared_ptr<Dialog> makeTwoReplyDialog() {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "dump_test";
    Dialog::EntryReplyLink startLink;
    startLink.index = 0;
    dialog->startEntries.push_back(startLink);
    dialog->entries.resize(1);
    dialog->replies.resize(2);

    auto &entry = dialog->entries[0];
    entry.text = "Hello there";
    entry.speaker = "trask";
    entry.delay = 100;
    for (uint32_t i = 0; i < 2; ++i) {
        Dialog::EntryReplyLink link;
        link.index = i;
        entry.replies.push_back(link);
    }
    dialog->replies[0].text = "Yes";
    dialog->replies[1].text = "No";
    return dialog;
}

class StateDumpTest : public Test {
protected:
    void SetUp() override {
        _engine.init();
        ON_CALL(_engine.sceneModule().graphs(), get(_))
            .WillByDefault(ReturnRef(_sceneGraph));
        _game = std::make_unique<Game>(GameID::KotOR, "", _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        TestGameModule::initConsole(*_game);

        _player = _game->newCreature();
        _player->setTag("player");
        _player->setPosition(glm::vec3(1.0f, 2.0f, 0.0f));
        _player->setMaxHitPoints(30);
        _player->setCurrentHitPoints(25);
        _area = _game->newArea();
        TestGameModule::configureModuleSnapshot(*_game, _area, _player, "end_m01aa", "end_m01aa");
        TestGameModule::addSnapshotObject(*_area, _player);

        _nearCreature = _game->newCreature();
        _nearCreature->setTag("trask");
        _nearCreature->setPosition(glm::vec3(4.0f, 2.0f, 0.0f));
        TestGameModule::addSnapshotObject(*_area, _nearCreature);

        _placeable = _game->newPlaceable();
        _placeable->setTag("footlocker");
        _placeable->setPosition(glm::vec3(1.0f, 7.0f, 0.0f));
        TestGameModule::addSnapshotObject(*_area, _placeable);

        _farCreature = _game->newCreature();
        _farCreature->setTag("far_away");
        _farCreature->setPosition(glm::vec3(100.0f, 2.0f, 0.0f));
        TestGameModule::addSnapshotObject(*_area, _farCreature);
    }

    json::object dump(std::vector<std::string> arguments = {}) {
        _console.lines.clear();
        _console.execute("dumpstate", std::move(arguments));
        EXPECT_EQ(1u, _console.lines.size());
        EXPECT_EQ(std::string::npos, _console.lines.front().find('\n'));
        return json::parse(_console.lines.front()).as_object();
    }

    TestEngine _engine;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::shared_ptr<Creature> _player;
    std::shared_ptr<Area> _area;
    std::shared_ptr<Creature> _nearCreature;
    std::shared_ptr<Placeable> _placeable;
    std::shared_ptr<Creature> _farCreature;
};

const json::object *findById(const json::array &objects, uint32_t id) {
    for (auto &value : objects) {
        auto &object = value.as_object();
        if (object.at("id").to_number<uint32_t>() == id) {
            return &object;
        }
    }
    return nullptr;
}

} // namespace

TEST_F(StateDumpTest, dumpstate_reports_party_actions_and_nearby_objects) {
    _player->addAction(_game->newAction<MoveToPointAction>(glm::vec3(5.0f, 6.0f, 0.0f)));
    _player->addAction(_game->newAction<AttackObjectAction>(_nearCreature));
    _player->delayAction(_game->newAction<WaitAction>(1.0f), 2.0f);

    auto root = dump();

    EXPECT_EQ("end_m01aa", root.at("module").as_string());
    EXPECT_EQ("end_m01aa", root.at("area").as_string());
    EXPECT_EQ("none", root.at("screen").as_string());
    EXPECT_FALSE(root.at("paused").as_bool());
    EXPECT_EQ(_player->id(), root.at("leader").to_number<uint32_t>());
    EXPECT_EQ(_player->id(), root.at("player").to_number<uint32_t>());
    EXPECT_TRUE(root.at("conversation").is_null());
    EXPECT_TRUE(root.at("selected").is_null());
    EXPECT_TRUE(root.at("combat").as_array().empty());

    auto &party = root.at("party").as_array();
    ASSERT_EQ(1u, party.size());
    auto &leader = party.front().as_object();
    EXPECT_EQ("player", leader.at("tag").as_string());
    EXPECT_EQ(25, leader.at("hp").to_number<int>());
    EXPECT_EQ(30, leader.at("maxhp").to_number<int>());
    EXPECT_FALSE(leader.at("dead").as_bool());
    auto &pos = leader.at("pos").as_array();
    ASSERT_EQ(3u, pos.size());
    EXPECT_DOUBLE_EQ(1.0, pos[0].to_number<double>());
    EXPECT_DOUBLE_EQ(2.0, pos[1].to_number<double>());
    EXPECT_EQ(1u, leader.at("delayed").to_number<size_t>());

    auto &actions = leader.at("actions").as_array();
    ASSERT_EQ(2u, actions.size());
    EXPECT_EQ("move_to_point", actions[0].as_object().at("type").as_string());
    auto &point = actions[0].as_object().at("point").as_array();
    EXPECT_DOUBLE_EQ(5.0, point[0].to_number<double>());
    EXPECT_DOUBLE_EQ(6.0, point[1].to_number<double>());
    EXPECT_EQ("attack_object", actions[1].as_object().at("type").as_string());
    EXPECT_EQ(_nearCreature->id(), actions[1].as_object().at("target").to_number<uint32_t>());

    auto &nearby = root.at("nearby").as_array();
    ASSERT_EQ(2u, nearby.size());
    // Sorted by distance from the leader; the leader itself is party, not nearby.
    EXPECT_EQ(_nearCreature->id(), nearby[0].as_object().at("id").to_number<uint32_t>());
    EXPECT_EQ(nullptr, findById(nearby, _player->id()));
    EXPECT_EQ(nullptr, findById(nearby, _farCreature->id()));

    auto &creature = nearby[0].as_object();
    EXPECT_EQ("creature", creature.at("type").as_string());
    EXPECT_DOUBLE_EQ(3.0, creature.at("dist").to_number<double>());
    EXPECT_TRUE(creature.contains("hostile"));
    EXPECT_TRUE(creature.contains("hp"));

    auto *placeable = findById(nearby, _placeable->id());
    ASSERT_NE(nullptr, placeable);
    EXPECT_EQ("placeable", placeable->at("type").as_string());
    EXPECT_EQ("footlocker", placeable->at("tag").as_string());
    EXPECT_FALSE(placeable->at("locked").as_bool());
    EXPECT_EQ(0u, placeable->at("itemCount").to_number<size_t>());

    // A wider radius reaches the distant creature.
    auto wide = dump({"200"});
    EXPECT_NE(nullptr, findById(wide.at("nearby").as_array(), _farCreature->id()));
}

TEST_F(StateDumpTest, dumpstate_is_read_only) {
    _player->addAction(_game->newAction<MoveToPointAction>(glm::vec3(5.0f, 6.0f, 0.0f)));
    auto before = _player->actions();

    auto first = dump();
    auto second = dump();

    EXPECT_EQ(before, _player->actions());
    EXPECT_FALSE(_area->selectedObject());
    first.erase("frame");
    second.erase("frame");
    EXPECT_EQ(first, second);
}

TEST_F(StateDumpTest, dumpstate_reports_conversation_replies_by_list_row) {
    DumpConversation conversation(*_game, _engine.services());
    conversation.start(makeTwoReplyDialog(), _nearCreature);
    TestGameModule::setConversation(*_game, &conversation);
    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::Conversation));

    auto root = dump();

    EXPECT_EQ("conversation", root.at("screen").as_string());
    auto &dialog = root.at("conversation").as_object();
    EXPECT_EQ("dump_test", dialog.at("dialog").as_string());
    EXPECT_EQ(_nearCreature->id(), dialog.at("owner").to_number<uint32_t>());
    EXPECT_EQ(0, dialog.at("entry").to_number<int>());
    EXPECT_EQ("Hello there", dialog.at("text").as_string());
    EXPECT_EQ("trask", dialog.at("speaker").as_string());
    auto &replies = dialog.at("replies").as_array();
    ASSERT_EQ(2u, replies.size());
    EXPECT_EQ(0, replies[0].as_object().at("index").to_number<int>());
    EXPECT_EQ("Yes", replies[0].as_object().at("text").as_string());
    EXPECT_EQ(1, replies[1].as_object().at("index").to_number<int>());
    EXPECT_EQ("No", replies[1].as_object().at("text").as_string());

    TestGameModule::setCurrentScreen(*_game, static_cast<int>(Game::Screen::InGame));
    TestGameModule::setConversation(*_game, nullptr);
}

TEST_F(StateDumpTest, dumpobject_resolves_id_and_tag_with_locals) {
    _nearCreature->setLocalNumber(3, 42);
    _nearCreature->setLocalBoolean(1, true);

    _console.lines.clear();
    _console.execute("dumpobject", {"trask"});
    ASSERT_EQ(1u, _console.lines.size());
    auto byTag = json::parse(_console.lines.front()).as_object();
    EXPECT_EQ(_nearCreature->id(), byTag.at("id").to_number<uint32_t>());
    EXPECT_EQ(42, byTag.at("localNumbers").as_object().at("3").to_number<int>());
    EXPECT_TRUE(byTag.at("localBooleans").as_object().at("1").as_bool());
    EXPECT_TRUE(byTag.at("actions").as_array().empty());

    _console.lines.clear();
    _console.execute("dumpobject", {std::to_string(_placeable->id())});
    ASSERT_EQ(1u, _console.lines.size());
    auto byId = json::parse(_console.lines.front()).as_object();
    EXPECT_EQ("footlocker", byId.at("tag").as_string());

    EXPECT_THROW(_console.execute("dumpobject", {"no_such_tag"}), std::runtime_error);
}
