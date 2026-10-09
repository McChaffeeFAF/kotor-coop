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

// K2 reconciliation batch: behaviour that now follows KotOR 2 where K1's was
// unknown. The notes these pin are in the design notes marked
// "K2 behaviour".

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/game.h"
#include "reone/game/modulesnapshot.h"
#include "reone/game/object/area.h"
#include "reone/game/object/creature.h"
#include "reone/game/object/module.h"
#include "reone/game/savedruntime.h"
#include "reone/game/scripteventqueue.h"
#include "reone/game/script/routines.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/gff.h"
#include "reone/script/executioncontext.h"
#include "reone/script/program.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

void reone::game::TestGameModule::setOnUserDefined(Object &object, std::string script) {
    object._onUserDefined = std::move(script);
}

namespace {

constexpr int kLocalEventNumber = 50;
constexpr int kLocalSignalled = 51;

std::vector<std::string> &runLog() {
    static std::vector<std::string> log;
    return log;
}

class K2EventQueueTest : public Test {
protected:
    void SetUp() override {
        runLog().clear();
        _engine.init();
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _routines = std::make_unique<Routines>(GameID::KotOR, _game.get(), &_engine.services());
        _routines->init();
        ON_CALL(_engine.sceneModule().graphs(), get(_)).WillByDefault(ReturnRef(_sceneGraph));
        _area = _game->newArea();
        TestGameModule::setActiveModuleArea(*_game, _area);
    }

    std::shared_ptr<Creature> addCreature(const std::string &tag, const std::string &onUserDefined) {
        auto creature = _game->newCreature();
        creature->setTag(tag);
        TestGameModule::setOnUserDefined(*creature, onUserDefined);
        _area->add(creature);
        return creature;
    }

    // SignalEvent(target, EventUserDefined(number)) for each pair, then
    // SetLocalNumber(OBJECT_SELF, kLocalSignalled, 1).
    std::shared_ptr<script::ScriptProgram> signalProgram(
        const std::string &name,
        const std::vector<std::pair<uint32_t, int>> &signals) {
        auto program = std::make_shared<script::ScriptProgram>(name);
        for (const auto &[target, number] : signals) {
            program->add(script::Instruction::newCONSTI(number));
            program->add(script::Instruction::newACTION(_routines->getIndexByName("EventUserDefined"), 1));
            program->add(script::Instruction::newCONSTO(static_cast<int>(target)));
            program->add(script::Instruction::newACTION(_routines->getIndexByName("SignalEvent"), 2));
        }
        program->add(script::Instruction::newCONSTI(1));
        program->add(script::Instruction::newCONSTI(kLocalSignalled));
        program->add(script::Instruction::newCONSTO(static_cast<int>(script::kObjectSelf)));
        program->add(script::Instruction::newACTION(_routines->getIndexByName("SetLocalNumber"), 3));
        program->add(script::Instruction(script::InstructionType::RETN));
        return program;
    }

    // An OnUserDefined script: stores GetUserDefinedEventNumber() on
    // OBJECT_SELF, after signalling the given events.
    std::shared_ptr<script::ScriptProgram> recordProgram(
        const std::string &name,
        const std::vector<std::pair<uint32_t, int>> &signals = {}) {
        auto program = std::make_shared<script::ScriptProgram>(name);
        for (const auto &[target, number] : signals) {
            program->add(script::Instruction::newCONSTI(number));
            program->add(script::Instruction::newACTION(_routines->getIndexByName("EventUserDefined"), 1));
            program->add(script::Instruction::newCONSTO(static_cast<int>(target)));
            program->add(script::Instruction::newACTION(_routines->getIndexByName("SignalEvent"), 2));
        }
        program->add(script::Instruction::newACTION(_routines->getIndexByName("GetUserDefinedEventNumber"), 0));
        program->add(script::Instruction::newCONSTI(kLocalEventNumber));
        program->add(script::Instruction::newCONSTO(static_cast<int>(script::kObjectSelf)));
        program->add(script::Instruction::newACTION(_routines->getIndexByName("SetLocalNumber"), 3));
        program->add(script::Instruction(script::InstructionType::RETN));
        return program;
    }

    void provide(const std::string &resRef, std::shared_ptr<script::ScriptProgram> program) {
        EXPECT_CALL(_engine.resourceModule().scripts(), get(resRef))
            .Times(AnyNumber())
            .WillRepeatedly(Invoke([program](const std::string &key) {
                runLog().push_back(key);
                return program;
            }));
    }

    TestEngine _engine;
    StubConsole _console;
    NiceMock<scene::MockSceneGraph> _sceneGraph;
    std::unique_ptr<Game> _game;
    std::unique_ptr<Routines> _routines;
    std::shared_ptr<Area> _area;
};

} // namespace

TEST_F(K2EventQueueTest, signal_event_runs_on_the_next_drain_not_inside_the_script) {
    auto caller = addCreature("caller", "");
    auto target = addCreature("target", "ud_target");
    provide("sig", signalProgram("sig", {{target->id(), 1001}}));
    provide("ud_target", recordProgram("ud_target"));

    _game->scriptRunner().run("sig", caller->id());

    // The signalling script finished, and the event waits in the queue.
    EXPECT_EQ(1, caller->getLocalNumber(kLocalSignalled));
    EXPECT_EQ(0, target->getLocalNumber(kLocalEventNumber));
    ASSERT_EQ(1u, _game->scriptEvents().entries().size());
    EXPECT_EQ(caller->id(), _game->scriptEvents().entries().front().caller);

    EXPECT_EQ(1u, _game->scriptEvents().runDue());
    EXPECT_EQ(1001, target->getLocalNumber(kLocalEventNumber));
    EXPECT_TRUE(_game->scriptEvents().entries().empty());
}

TEST_F(K2EventQueueTest, events_run_first_in_first_out_and_a_signal_from_a_drained_event_runs_in_the_same_drain) {
    auto caller = addCreature("caller", "");
    auto third = addCreature("third", "ud_third");
    auto first = addCreature("first", "ud_first");
    auto second = addCreature("second", "ud_second");
    provide("sig", signalProgram("sig", {{first->id(), 1}, {second->id(), 2}}));
    // The first target's script signals the third while the queue drains.
    provide("ud_first", recordProgram("ud_first", {{third->id(), 3}}));
    provide("ud_second", recordProgram("ud_second"));
    provide("ud_third", recordProgram("ud_third"));

    _game->scriptRunner().run("sig", caller->id());
    runLog().clear();
    EXPECT_EQ(3u, _game->scriptEvents().runDue());

    EXPECT_EQ((std::vector<std::string> {"ud_first", "ud_second", "ud_third"}), runLog());
    EXPECT_EQ(1, first->getLocalNumber(kLocalEventNumber));
    EXPECT_EQ(2, second->getLocalNumber(kLocalEventNumber));
    EXPECT_EQ(3, third->getLocalNumber(kLocalEventNumber));
}

TEST_F(K2EventQueueTest, an_event_for_an_object_that_is_gone_is_dropped) {
    auto target = addCreature("target", "ud_target");
    provide("ud_target", recordProgram("ud_target"));
    _game->scriptEvents().signal(target->id(), script::kObjectInvalid, _game->newEvent(5));
    _game->scriptEvents().signal(0x7ffffff0u, script::kObjectInvalid, _game->newEvent(6));

    EXPECT_EQ(2u, _game->scriptEvents().runDue());
    EXPECT_EQ(5, target->getLocalNumber(kLocalEventNumber));
    EXPECT_TRUE(_game->scriptEvents().entries().empty());
}

TEST_F(K2EventQueueTest, a_later_event_waits_for_its_time_and_equal_times_keep_their_order) {
    auto target = addCreature("target", "ud_target");
    provide("ud_target", recordProgram("ud_target"));
    auto now = _game->worldTimeMilliseconds();
    _game->scriptEvents().enqueueAt(now + 1000, target->id(), script::kObjectInvalid, _game->newEvent(9));
    _game->scriptEvents().enqueueAt(now, target->id(), script::kObjectInvalid, _game->newEvent(7));
    _game->scriptEvents().enqueueAt(now, target->id(), script::kObjectInvalid, _game->newEvent(8));

    ASSERT_EQ(3u, _game->scriptEvents().entries().size());
    EXPECT_EQ(7, _game->scriptEvents().entries()[0].event->number());
    EXPECT_EQ(8, _game->scriptEvents().entries()[1].event->number());
    EXPECT_EQ(9, _game->scriptEvents().entries()[2].event->number());

    EXPECT_EQ(2u, _game->scriptEvents().runDue());
    EXPECT_EQ(8, target->getLocalNumber(kLocalEventNumber));
    EXPECT_EQ(1u, _game->scriptEvents().entries().size());
}

TEST_F(K2EventQueueTest, a_self_signalling_script_cannot_hang_the_drain) {
    auto target = addCreature("target", "ud_loop");
    provide("ud_loop", recordProgram("ud_loop", {{target->id(), 4}}));
    _game->scriptEvents().signal(target->id(), script::kObjectInvalid, _game->newEvent(4));

    EXPECT_EQ(ScriptEventQueue::kMaxEventsPerDrain, _game->scriptEvents().runDue());
    EXPECT_EQ(1u, _game->scriptEvents().entries().size());
    _game->scriptEvents().clear();
}

TEST(K2EventQueue, saved_form_uses_the_retail_event_types) {
    Event userDefined(1001);
    auto saved = savedScriptEventOf(userDefined);
    EXPECT_EQ(kSavedScriptEventUserDefined, saved.type);
    EXPECT_EQ((std::vector<int32_t> {1001}), saved.integers);
    auto restored = eventFromSaved(saved, {});
    EXPECT_EQ(EventKind::UserDefined, restored->kind());
    EXPECT_EQ(1001, restored->number());

    Event spell(0, {21, 1}, {}, {}, {});
    spell.setKind(EventKind::SpellCastAt);
    saved = savedScriptEventOf(spell);
    EXPECT_EQ(kSavedScriptEventSpellCastAt, saved.type);
    EXPECT_EQ((std::vector<int32_t> {21, 1}), saved.integers);
    restored = eventFromSaved(saved, {});
    EXPECT_EQ(EventKind::SpellCastAt, restored->kind());
    EXPECT_EQ((std::vector<int32_t> {21, 1}), restored->integers());

    // Earlier reone saves stored the user-defined number as the type.
    SavedScriptEvent legacy;
    legacy.type = 1006;
    restored = eventFromSaved(legacy, {});
    EXPECT_EQ(EventKind::UserDefined, restored->kind());
    EXPECT_EQ(1006, restored->number());
}

namespace {

std::shared_ptr<Gff> readSnapshotGff(ByteBuffer bytes) {
    MemoryInputStream stream(bytes);
    GffReader reader(stream);
    reader.load();
    return reader.root();
}

struct K2EventSnapshotFixture : Test {
    K2EventSnapshotFixture() :
        game(GameID::KotOR, "", engine.options(), engine.services(), console) {
    }

    void SetUp() override {
        area = game.newArea();
        player = game.newCreature();
        TestGameModule::configureModuleSnapshot(game, area, player, "tat_m17ab", "tat_m17ab");
        TestGameModule::addSnapshotObject(*area, player);
        auto ifo = Gff::Builder().type(0xffffffff)
                       .field(Gff::Field::newDword("Mod_NextObjId0", 50))
                       .build();
        auto are = Gff::Builder().type(0xffffffff).build();
        auto git = Gff::Builder().type(0xffffffff)
                       .field(Gff::Field::newByte("UseTemplates", 1))
                       .build();
        game.captureSaveResourceShadow({SaveResourceKind::ModuleIfo, "tat_m17ab"}, *ifo);
        game.captureSaveResourceShadow({SaveResourceKind::AreaAre, "tat_m17ab"}, *are);
        game.captureSaveResourceShadow({SaveResourceKind::AreaGit, "tat_m17ab"}, *git);
    }

    TestEngine &engine {testEngine()};
    StubConsole console;
    Game game;
    std::shared_ptr<Area> area;
    std::shared_ptr<Creature> player;
};

} // namespace

TEST_F(K2EventSnapshotFixture, a_queued_event_is_saved_and_a_loaded_one_is_queued_again) {
    game.scriptEvents().signal(player->id(), player->id(), game.newEvent(1001));

    auto saved = ModuleSnapshotBuilder(game, "module003").build();
    ASSERT_TRUE(saved) << saved.message;
    auto ifo = readSnapshotGff(saved.snapshot->ifoBytes);
    auto queue = SavedEventQueue::fromGff(*ifo, SerializedIdentityContext::moduleGraph("module003"));
    ASSERT_EQ(1u, queue.events.size());
    const auto &record = queue.events.front();
    EXPECT_EQ(static_cast<uint32_t>(SavedEventType::SignalEvent), record.eventId);
    const auto *payload = std::get_if<SavedScriptEvent>(&record.payload);
    ASSERT_NE(nullptr, payload);
    EXPECT_EQ(kSavedScriptEventUserDefined, payload->type);
    EXPECT_EQ((std::vector<int32_t> {1001}), payload->integers);
    EXPECT_EQ(SavedExecutionSupport::Executable, record.executionSupport());

    // A loaded record goes back into the queue when it falls due.
    game.scriptEvents().clear();
    SavedEventRecord loaded;
    loaded.day = 0;
    loaded.time = 0;
    loaded.object = SavedObjectReference::fromRuntimeId(player->id());
    loaded.caller = SavedObjectReference::fromRuntimeId(player->id());
    loaded.eventId = static_cast<uint32_t>(SavedEventType::SignalEvent);
    loaded.payload = *payload;
    game.module()->enqueueSaveEvent(loaded);
    game.module()->publishSavedEventQueue();
    game.module()->dispatchDueSavedEvents();
    ASSERT_EQ(1u, game.scriptEvents().entries().size());
    EXPECT_EQ(player->id(), game.scriptEvents().entries().front().target);
    EXPECT_EQ(1001, game.scriptEvents().entries().front().event->number());
    EXPECT_EQ(0u, game.module()->pendingSavedEventCount());
}

// --- Per-object light fading (K2 behaviour) ------------------------------------

#include "reone/scene/objectlights.h"

TEST(K2ObjectLights, fading_lights_fade_in_and_out_of_an_objects_set) {
    using reone::scene::ObjectLightSlot;
    int fadingLight = 0;
    int plainLight = 0;
    int newcomer = 0;
    auto isFading = [&](const void *key) { return key == &fadingLight; };
    std::vector<ObjectLightSlot> slots;

    reone::scene::updateObjectLightSlots(slots, {&fadingLight, &plainLight}, isFading, 0.1f, 2);
    ASSERT_EQ(2u, slots.size());
    EXPECT_FLOAT_EQ(0.0f, slots[0].weight); // joins at 0
    reone::scene::updateObjectLightSlots(slots, {&fadingLight, &plainLight}, isFading, 0.1f, 2);
    EXPECT_NEAR(0.3f, slots[0].weight, 1e-5f); // 3.0 per second
    EXPECT_FLOAT_EQ(1.0f, slots[1].weight); // not fading: full at once

    // Both leave the wanted set; a newcomer waits while the fading light
    // still holds its place.
    reone::scene::updateObjectLightSlots(slots, {&newcomer}, isFading, 0.05f, 2);
    ASSERT_EQ(2u, slots.size());
    EXPECT_EQ(&fadingLight, slots[0].key);
    EXPECT_NEAR(0.15f, slots[0].weight, 1e-5f);
    EXPECT_EQ(&newcomer, slots[1].key);
    reone::scene::updateObjectLightSlots(slots, {&newcomer}, isFading, 0.1f, 2);
    ASSERT_EQ(1u, slots.size());
    EXPECT_EQ(&newcomer, slots[0].key);
    EXPECT_FLOAT_EQ(1.0f, slots[0].weight);
}
