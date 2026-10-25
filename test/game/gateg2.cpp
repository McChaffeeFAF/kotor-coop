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

// Gate batch G2 (end_m01aa): object events and behaviour fields. The notes
// these pin are in design note gate_g2.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"

#include "reone/game/action/giveitem.h"
#include "reone/game/action/takeitem.h"
#include "reone/game/equipmentrules.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/item.h"
#include "reone/game/object/module.h"
#include "reone/game/object/placeable.h"
#include "reone/game/party.h"
#include "reone/game/script/routines.h"
#include "reone/resource/dialog.h"
#include "reone/resource/gff.h"
#include "reone/resource/parser/gff/are.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::loadAreaBehaviourFlags(Area &area, const resource::generated::ARE &are) {
    area.loadBehaviourFlags(are);
}

void reone::game::TestGameModule::loadAreaFlagsFromGit(Area &area, const resource::Gff &gff) {
    area.loadFlagsFromGit(gff);
}

namespace {

// Locals the probe scripts below write their observations to.
constexpr int kLocalFirst = 40;
constexpr int kLocalSecond = 41;
constexpr int kLocalThird = 42;

// One observation a probe script makes: call a routine that takes no
// arguments and store its result - or, for an object routine, whether the
// result is the expected object - in a local number on OBJECT_SELF.
struct Observation {
    std::string routine;
    std::optional<uint32_t> equalsObject;
    int localIndex {0};
};

std::shared_ptr<script::ScriptProgram> probeProgram(
    const Routines &routines,
    const std::string &name,
    const std::vector<Observation> &observations) {

    auto program = std::make_shared<script::ScriptProgram>(name);
    int setLocalNumber = routines.getIndexByName("SetLocalNumber");
    for (const auto &observation : observations) {
        program->add(script::Instruction::newACTION(routines.getIndexByName(observation.routine), 0));
        if (observation.equalsObject) {
            program->add(script::Instruction::newCONSTO(static_cast<int>(*observation.equalsObject)));
            program->add(script::Instruction(script::InstructionType::EQUALOO));
        }
        // SetLocalNumber(OBJECT_SELF, index, value): argument 0 on top.
        program->add(script::Instruction::newCONSTI(observation.localIndex));
        program->add(script::Instruction::newCONSTO(static_cast<int>(script::kObjectSelf)));
        program->add(script::Instruction::newACTION(setLocalNumber, 3));
    }
    program->add(script::Instruction(script::InstructionType::RETN));
    return program;
}

// Script request counts, keyed by resref. Static so the stubs registered on
// the shared scripts mock stay valid past the test that installed them.
std::map<std::string, int> &requestCounts() {
    static std::map<std::string, int> counts;
    return counts;
}

class G2Conversation : public Conversation {
public:
    G2Conversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}
    void setBarkText(std::string text, float duration) override {}
};

class GateG2Test : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    // The named script resolves to nothing; only its requests are counted.
    void countRequests(const std::string &resRef) {
        requestCounts()[resRef] = 0;
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([](const std::string &key) {
                ++requestCounts()[key];
                return std::shared_ptr<script::ScriptProgram>();
            }));
    }

    int requests(const std::string &resRef) const {
        return requestCounts()[resRef];
    }

    // The named script runs the probe, and its requests are counted.
    void probe(const std::string &resRef, std::vector<Observation> observations) {
        requestCounts()[resRef] = 0;
        auto program = probeProgram(*_routines, resRef, observations);
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([program](const std::string &key) {
                ++requestCounts()[key];
                return program;
            }));
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        _area->add(creature);
        return creature;
    }

    std::shared_ptr<Item> addItemTo(Object &owner, const std::string &tag) {
        auto item = _game->newItem();
        item->setTag(tag);
        owner.addItem(item);
        return item;
    }

    void loadModuleInfo(std::vector<Gff::Field> extra) {
        std::vector<Gff::Field> fields {Gff::Field::newResRef("Mod_Entry_Area", "entryarea")};
        for (auto &field : extra) {
            fields.push_back(std::move(field));
        }
        auto ifo = std::make_shared<Gff>(0xffffffff, std::move(fields));
        TestGameModule::loadModuleInfo(*_game->module(), "g2test", *ifo);
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

// A two-node dialogue: entry 0 offers two replies (so it is not an end node),
// reply 0 leads to entry 1, which has no replies and ends the conversation.
std::shared_ptr<Dialog> makeEndDialog() {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "g2_dialog";
    dialog->endScript = "g2_end";
    dialog->abortScript = "g2_abort";
    Dialog::EntryReplyLink start;
    start.index = 0;
    dialog->startEntries.push_back(start);
    dialog->entries.resize(2);
    dialog->replies.resize(2);
    auto &first = dialog->entries[0];
    first.text = "first";
    first.delay = 1;
    Dialog::EntryReplyLink reply0;
    reply0.index = 0;
    Dialog::EntryReplyLink reply1;
    reply1.index = 1;
    first.replies = {reply0, reply1};
    dialog->replies[0].text = "go on";
    Dialog::EntryReplyLink toLast;
    toLast.index = 1;
    dialog->replies[0].entries.push_back(toLast);
    dialog->replies[1].text = "other";
    dialog->entries[1].text = "last";
    dialog->entries[1].delay = 1;
    return dialog;
}

std::shared_ptr<Dialog> makeOneLiner() {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "g2_bark";
    dialog->endScript = "g2_end";
    dialog->abortScript = "g2_abort";
    Dialog::EntryReplyLink start;
    start.index = 0;
    dialog->startEntries.push_back(start);
    dialog->entries.resize(1);
    dialog->replies.resize(1);
    dialog->entries[0].text = "bark";
    dialog->entries[0].delay = 1;
    Dialog::EntryReplyLink terminal;
    terminal.index = 0;
    dialog->entries[0].replies.push_back(terminal);
    return dialog;
}

} // namespace

// --- utc:ScriptDisturbed --------------------------------------------------

TEST_F(GateG2Test, disturbed_runs_in_the_next_update_with_its_event_values) {
    auto owner = addCreature("owner");
    auto giver = addCreature("giver");
    auto item = addItemTo(*owner, "thing");
    owner->setOnDisturbed("g2_disturbed");
    probe("g2_disturbed", {{"GetLastDisturbed", giver->id(), kLocalFirst},
                           {"GetInventoryDisturbItem", item->id(), kLocalSecond},
                           {"GetInventoryDisturbType", std::nullopt, kLocalThird}});

    owner->signalInventoryDisturbed(InventoryDisturbType::Stolen, giver->id(), item->id());

    // Queued, not run inline with the inventory change.
    EXPECT_EQ(0, requests("g2_disturbed"));
    EXPECT_EQ(1u, owner->pendingScriptEventCount());

    owner->update(0.0f);

    EXPECT_EQ(1, requests("g2_disturbed"));
    EXPECT_EQ(0u, owner->pendingScriptEventCount());
    EXPECT_EQ(1, owner->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, owner->getLocalNumber(kLocalSecond));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Stolen), owner->getLocalNumber(kLocalThird));
}

TEST_F(GateG2Test, disturbed_take_item_action_reports_removed_to_the_owner) {
    auto owner = addCreature("owner");
    auto taker = addCreature("taker");
    auto item = addItemTo(*owner, "thing");
    owner->setOnDisturbed("g2_disturbed");
    probe("g2_disturbed", {{"GetLastDisturbed", taker->id(), kLocalFirst},
                           {"GetInventoryDisturbType", std::nullopt, kLocalThird}});

    auto action = _game->newAction<TakeItemAction>(item, owner);
    action->execute(action, *taker, 0.0f);
    EXPECT_TRUE(action->isCompleted());
    EXPECT_EQ(taker->id(), item->owner());

    owner->update(0.0f);
    EXPECT_EQ(1, requests("g2_disturbed"));
    EXPECT_EQ(1, owner->getLocalNumber(kLocalFirst));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Removed), owner->getLocalNumber(kLocalThird));
}

TEST_F(GateG2Test, disturbed_give_item_action_reports_added_to_the_receiver) {
    auto giver = addCreature("giver");
    auto receiver = addCreature("receiver");
    auto item = addItemTo(*giver, "thing");
    receiver->setOnDisturbed("g2_disturbed");
    giver->setOnDisturbed("g2_giver_disturbed");
    probe("g2_disturbed", {{"GetLastDisturbed", giver->id(), kLocalFirst},
                           {"GetInventoryDisturbItem", item->id(), kLocalSecond},
                           {"GetInventoryDisturbType", std::nullopt, kLocalThird}});
    countRequests("g2_giver_disturbed");

    auto action = _game->newAction<GiveItemAction>(item, receiver);
    action->execute(action, *giver, 0.0f);
    EXPECT_EQ(receiver->id(), item->owner());

    receiver->update(0.0f);
    giver->update(0.0f);
    EXPECT_EQ(1, requests("g2_disturbed"));
    EXPECT_EQ(1, receiver->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, receiver->getLocalNumber(kLocalSecond));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Added), receiver->getLocalNumber(kLocalThird));
    // The giver moved its own item: nothing disturbed it.
    EXPECT_EQ(0, requests("g2_giver_disturbed"));
}

TEST_F(GateG2Test, container_give_moves_one_of_a_stack_and_disturbs_the_container) {
    auto leader = addCreature("leader");
    auto crate = _game->newPlaceable();
    crate->setTag("crate");
    _area->add(crate);
    crate->setOnInvDisturbed("g2_crate_disturbed");
    auto item = addItemTo(*leader, "thing");
    item->setStackSize(3);
    probe("g2_crate_disturbed", {{"GetLastDisturbed", leader->id(), kLocalFirst},
                                 {"GetInventoryDisturbType", std::nullopt, kLocalThird}});

    uint32_t given = giveItemToContainer(*_game, *leader, item, *crate);

    ASSERT_NE(script::kObjectInvalid, given);
    EXPECT_EQ(2, item->stackSize());
    ASSERT_EQ(1u, crate->items().size());
    EXPECT_EQ(given, crate->items()[0]->id());
    EXPECT_EQ(1, crate->items()[0]->stackSize());
    EXPECT_EQ(1, requests("g2_crate_disturbed"));
    EXPECT_EQ(1, crate->getLocalNumber(kLocalFirst));
    EXPECT_EQ(static_cast<int>(InventoryDisturbType::Added), crate->getLocalNumber(kLocalThird));

    // A second give merges into the crate's stack; the last one empties the giver.
    giveItemToContainer(*_game, *leader, item, *crate);
    giveItemToContainer(*_game, *leader, item, *crate);
    EXPECT_TRUE(leader->items().empty());
    ASSERT_EQ(1u, crate->items().size());
    EXPECT_EQ(3, crate->items()[0]->stackSize());
    EXPECT_EQ(3, requests("g2_crate_disturbed"));
}

TEST_F(GateG2Test, container_give_refuses_an_item_the_giver_does_not_hold) {
    auto leader = addCreature("leader");
    auto other = addCreature("other");
    auto crate = _game->newPlaceable();
    _area->add(crate);
    auto item = addItemTo(*other, "thing");

    EXPECT_EQ(script::kObjectInvalid, giveItemToContainer(*_game, *leader, item, *crate));
    EXPECT_TRUE(crate->items().empty());
    EXPECT_EQ(1u, other->items().size());
}

TEST_F(GateG2Test, disturbed_is_not_raised_by_the_owner_moving_its_own_item) {
    auto owner = addCreature("owner");
    auto item = addItemTo(*owner, "thing");
    auto chest = addCreature("chest");

    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, item, *owner, owner->id()));
    EXPECT_EQ(0u, owner->pendingScriptEventCount());

    // Putting it into another object disturbs only that object.
    ASSERT_TRUE(transferItemAndSignalDisturbed(*_game, item, *chest, owner->id()));
    EXPECT_EQ(0u, owner->pendingScriptEventCount());
    EXPECT_EQ(1u, chest->pendingScriptEventCount());
}

// --- utc:ScriptEndDialogu, dlg:EndConversation / EndConverAbort -----------

TEST_F(GateG2Test, end_dialogue_normal_end_runs_end_script_then_every_creature_hears_it) {
    auto owner = addCreature("owner");
    auto bystander = addCreature("bystander");
    owner->setOnEndDialogue("g2_enddlg");
    bystander->setOnEndDialogue("g2_enddlg");
    countRequests("g2_end");
    countRequests("g2_abort");
    countRequests("g2_enddlg");

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(makeEndDialog(), owner);
    ASSERT_TRUE(conversation.isRunning());
    conversation.updateSimulation(2.0f); // entry 0 ends, its replies are shown
    ASSERT_TRUE(conversation.pickReplyAtRow(0));
    conversation.updateSimulation(2.0f); // entry 1 ends with no replies

    EXPECT_FALSE(conversation.isRunning());
    EXPECT_EQ(1, requests("g2_end"));
    EXPECT_EQ(0, requests("g2_abort"));
    // Queued on every creature in the area, run in their next update.
    EXPECT_EQ(0, requests("g2_enddlg"));
    EXPECT_EQ(1u, owner->pendingScriptEventCount());
    EXPECT_EQ(1u, bystander->pendingScriptEventCount());

    owner->update(0.0f);
    bystander->update(0.0f);
    EXPECT_EQ(2, requests("g2_enddlg"));
}

TEST_F(GateG2Test, end_dialogue_script_runs_as_the_creature) {
    auto owner = addCreature("owner");
    auto bystander = addCreature("bystander");
    bystander->setOnEndDialogue("g2_enddlg");
    probe("g2_enddlg", {{"GetInventoryDisturbType", std::nullopt, kLocalFirst}});
    countRequests("g2_end");

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(makeEndDialog(), owner);
    conversation.updateSimulation(2.0f);
    ASSERT_TRUE(conversation.pickReplyAtRow(0));
    conversation.updateSimulation(2.0f);
    bystander->setLocalNumber(kLocalFirst, 7);
    bystander->update(0.0f);

    // The probe wrote to OBJECT_SELF: the bystander ran its own script.
    EXPECT_EQ(1, requests("g2_enddlg"));
    EXPECT_NE(7, bystander->getLocalNumber(kLocalFirst));
}

TEST_F(GateG2Test, abort_runs_the_abort_script_and_nobody_hears_an_end) {
    auto owner = addCreature("owner");
    auto bystander = addCreature("bystander");
    bystander->setOnEndDialogue("g2_enddlg");
    countRequests("g2_end");
    countRequests("g2_abort");
    countRequests("g2_enddlg");

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(makeEndDialog(), owner);
    conversation.abort();

    EXPECT_FALSE(conversation.isRunning());
    EXPECT_EQ(0, requests("g2_end"));
    EXPECT_EQ(1, requests("g2_abort"));
    EXPECT_EQ(0u, bystander->pendingScriptEventCount());

    // A second abort of an ended conversation does nothing.
    conversation.abort();
    EXPECT_EQ(1, requests("g2_abort"));
}

TEST_F(GateG2Test, abort_when_a_new_conversation_replaces_a_running_one) {
    auto owner = addCreature("owner");
    countRequests("g2_end");
    countRequests("g2_abort");

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(makeEndDialog(), owner);
    conversation.start(makeEndDialog(), owner);

    EXPECT_EQ(1, requests("g2_abort"));
    EXPECT_EQ(0, requests("g2_end"));
    EXPECT_TRUE(conversation.isRunning());
}

TEST_F(GateG2Test, abort_when_the_conversation_never_shows_an_entry) {
    auto owner = addCreature("owner");
    owner->setOnEndDialogue("g2_enddlg");
    countRequests("g2_end");
    countRequests("g2_abort");
    auto dialog = makeEndDialog();
    dialog->startEntries.clear(); // no start entry can become active

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(dialog, owner);

    EXPECT_FALSE(conversation.isRunning());
    EXPECT_EQ(1, requests("g2_abort"));
    EXPECT_EQ(0, requests("g2_end"));
    EXPECT_EQ(0u, owner->pendingScriptEventCount());
}

TEST_F(GateG2Test, end_dialogue_one_liner_reaches_only_the_owner) {
    auto owner = addCreature("owner");
    auto bystander = addCreature("bystander");
    countRequests("g2_end");

    G2Conversation conversation(*_game, _engine.services());
    conversation.start(makeOneLiner(), owner);

    EXPECT_FALSE(conversation.isRunning());
    EXPECT_EQ(1, requests("g2_end"));
    EXPECT_EQ(1u, owner->pendingScriptEventCount());
    EXPECT_EQ(0u, bystander->pendingScriptEventCount());
}

// --- ifo:Mod_OnActvtItem ----------------------------------------------------

TEST_F(GateG2Test, activate_item_records_the_activation_and_runs_the_module_script) {
    loadModuleInfo({Gff::Field::newResRef("Mod_OnActvtItem", "g2_activate")});
    auto module = _game->module();
    auto user = addCreature("user");
    auto target = addCreature("target");
    auto medpac = addItemTo(*user, "g_i_medeqpmnt01");
    user->setPosition(glm::vec3(0.0f));
    target->setPosition(glm::vec3(0.0f, 2.0f, 0.0f));
    probe("g2_activate", {{"GetItemActivated", medpac->id(), kLocalFirst},
                          {"GetItemActivator", user->id(), kLocalSecond},
                          {"GetItemActivatedTarget", target->id(), kLocalThird}});

    module->runOnActivateItem(*medpac, *user, *target);

    EXPECT_EQ(1, requests("g2_activate"));
    // The module script runs as the module.
    EXPECT_EQ(1, module->getLocalNumber(kLocalFirst));
    EXPECT_EQ(1, module->getLocalNumber(kLocalSecond));
    EXPECT_EQ(1, module->getLocalNumber(kLocalThird));

    // The target location is the target's position, facing away from the user.
    script::ExecutionContext ctx;
    auto location = _routines->get(_routines->getIndexByName("GetItemActivatedTargetLocation")).invoke({}, ctx);
    ASSERT_TRUE(location.engineType);
    auto loc = std::static_pointer_cast<Location>(location.engineType);
    EXPECT_FLOAT_EQ(2.0f, loc->position().y);
    EXPECT_NEAR(0.0f, loc->facing(), 1e-5f);
}

TEST_F(GateG2Test, activate_item_routines_report_invalid_before_any_activation) {
    script::ExecutionContext ctx;
    for (const char *name : {"GetItemActivated", "GetItemActivator", "GetItemActivatedTarget"}) {
        auto result = _routines->get(_routines->getIndexByName(name)).invoke({}, ctx);
        EXPECT_EQ(script::kObjectInvalid, result.objectId) << name;
    }
}

TEST_F(GateG2Test, activate_item_without_a_module_script_still_records_it) {
    loadModuleInfo({});
    auto module = _game->module();
    auto user = addCreature("user");
    auto medpac = addItemTo(*user, "g_i_medeqpmnt01");

    module->runOnActivateItem(*medpac, *user, *user);

    EXPECT_EQ(medpac->id(), module->itemActivation().item);
    EXPECT_EQ(user->id(), module->itemActivation().activator);
}

// --- ifo:Mod_XPScale ------------------------------------------------------

TEST_F(GateG2Test, xp_scale_scales_engine_awards_but_not_scripted_ones) {
    loadModuleInfo({Gff::Field::newByte("Mod_XPScale", 20)});
    EXPECT_EQ(20, _game->module()->xpScale());

    _game->party().awardXP(100, XPSource::Combat);
    EXPECT_EQ(200, _game->party().xp());

    _game->party().awardXP(100, XPSource::Plot);
    EXPECT_EQ(300, _game->party().xp());
}

TEST_F(GateG2Test, xp_scale_of_ten_is_identity) {
    loadModuleInfo({Gff::Field::newByte("Mod_XPScale", 10)});

    _game->party().awardXP(37, XPSource::Stealth);
    EXPECT_EQ(37, _game->party().xp());
}

// --- are:Unescapable, are:Flags -------------------------------------------

TEST_F(GateG2Test, unescapable_is_loaded_from_the_are_and_read_by_the_routine) {
    resource::generated::ARE are;
    are.Unescapable = 1;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    EXPECT_TRUE(_area->isUnescapable());

    script::ExecutionContext ctx;
    auto result = _routines->get(_routines->getIndexByName("GetAreaUnescapable")).invoke({}, ctx);
    EXPECT_EQ(1, result.intValue);

    are.Unescapable = 0;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    EXPECT_FALSE(_area->isUnescapable());
}

// The game keeps the area flags in the GIT's AreaProperties.
TEST_F(GateG2Test, area_flags_are_read_from_the_git_area_properties) {
    auto properties = Gff::Builder()
                          .type(100)
                          .field(Gff::Field::newByte("Unescapable", 1))
                          .build();
    auto git = Gff::Builder()
                   .type(0xffffffff)
                   .field(Gff::Field::newStruct("AreaProperties", std::move(properties)))
                   .build();
    EXPECT_FALSE(_area->isUnescapable());
    TestGameModule::loadAreaFlagsFromGit(*_area, *git);
    EXPECT_TRUE(_area->isUnescapable());

    // The GIT's value wins over the ARE's.
    resource::generated::ARE are;
    are.Unescapable = 0;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    TestGameModule::loadAreaFlagsFromGit(*_area, *git);
    EXPECT_TRUE(_area->isUnescapable());
}

// A save of an older build kept Unescapable in the ARE and none in the GIT.
TEST_F(GateG2Test, area_flags_of_an_older_save_still_come_from_the_are) {
    resource::generated::ARE are;
    are.Unescapable = 1;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    auto bareGit = Gff::Builder().type(0xffffffff).build();
    TestGameModule::loadAreaFlagsFromGit(*_area, *bareGit);
    EXPECT_TRUE(_area->isUnescapable());

    auto emptyProperties = Gff::Builder().type(100).build();
    auto git = Gff::Builder()
                   .type(0xffffffff)
                   .field(Gff::Field::newStruct("AreaProperties", std::move(emptyProperties)))
                   .build();
    TestGameModule::loadAreaFlagsFromGit(*_area, *git);
    EXPECT_TRUE(_area->isUnescapable());
}

TEST_F(GateG2Test, area_flags_interior_areas_have_no_weather) {
    resource::generated::ARE are;
    are.ChanceRain = 50;
    are.ChanceSnow = 20;
    are.ChanceLightning = 10;
    are.WindPower = 2;

    are.Flags = Area::kFlagNatural;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    EXPECT_FALSE(_area->isInterior());
    EXPECT_TRUE(_area->isNatural());
    EXPECT_EQ(50, _area->weather().chanceRain);
    EXPECT_EQ(2, _area->weather().windPower);

    are.Flags = Area::kFlagInterior | Area::kFlagUnderground;
    TestGameModule::loadAreaBehaviourFlags(*_area, are);
    EXPECT_TRUE(_area->isInterior());
    EXPECT_TRUE(_area->isUnderground());
    EXPECT_EQ(0, _area->weather().chanceRain);
    EXPECT_EQ(0, _area->weather().chanceSnow);
    EXPECT_EQ(0, _area->weather().chanceLightning);
    EXPECT_EQ(0, _area->weather().windPower);
}
