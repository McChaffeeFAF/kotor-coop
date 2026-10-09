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

// Campaign batch C12: audio, conversation and cinematic data gaps.
// Behaviour notes: design note c12_audio_cine.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../fixtures/engine.h"
#include "../fixtures/game.h"

#include "reone/game/combatsounds.h"
#include "reone/game/console.h"
#include "reone/game/game.h"
#include "reone/game/gui/conversation.h"
#include "reone/resource/2da.h"
#include "reone/resource/container/memory.h"
#include "reone/resource/dialog.h"
#include "reone/resource/format/gffwriter.h"
#include "reone/resource/gff.h"
#include "reone/resource/provider/dialogs.h"
#include "reone/resource/provider/gffs.h"
#include "reone/resource/resources.h"
#include "reone/resource/strings.h"
#include "reone/system/stream/memoryoutput.h"

using namespace reone;
using namespace reone::game;
using namespace reone::resource;
using namespace testing;

namespace {

class StubConsole : public IConsole, boost::noncopyable {
public:
    void registerCommand(std::string name, std::string description, CommandHandler handler) override {}
    void printLine(const std::string &text) override {}
};

class C12Conversation : public Conversation {
public:
    C12Conversation(Game &game, ServicesView &services) :
        Conversation(game, services) {
    }

    void pickReplyForTest(int index) {
        pickReply(index);
    }

protected:
    void setReplyLines(std::vector<std::string> lines) override {}
    void setMessage(std::string message) override {}
    void setBarkText(std::string text, float duration) override {}
};

std::shared_ptr<TwoDA> makeTable(
    const std::vector<std::string> &columns,
    const std::vector<std::vector<std::string>> &rows) {
    TwoDA::Builder builder;
    builder.columns(columns);
    for (const auto &row : rows) {
        builder.row(row);
    }
    return std::shared_ptr<TwoDA>(builder.build());
}

std::shared_ptr<Dialog> loadDlg(const Gff &root) {
    auto bytes = ByteBuffer();
    auto stream = MemoryOutputStream(bytes);
    GffWriter(ResType::Dlg, root).save(stream);
    static Resources resources;
    static Strings strings;
    static int serial = 0;
    std::string name = "c12dlg" + std::to_string(serial++);
    auto container = std::make_unique<MemoryResourceContainer>();
    container->add(ResourceId(name, ResType::Dlg), std::move(bytes));
    resources.add(std::move(container));
    static Gffs gffs(resources);
    static Dialogs dialogs(gffs, strings);
    return dialogs.get(name);
}

// Entry 0 -> reply 0 (visible "Go") -> entry 1 -> terminal reply 1.
std::shared_ptr<Dialog> makeTwoEntryDialog() {
    auto dialog = std::make_shared<Dialog>();
    dialog->resRef = "c12_fades";
    Dialog::EntryReplyLink start;
    start.index = 0;
    dialog->startEntries.push_back(start);
    dialog->entries.resize(2);
    dialog->replies.resize(2);
    dialog->entries[0].text = "first";
    dialog->entries[0].delay = 100;
    Dialog::EntryReplyLink toReply0;
    toReply0.index = 0;
    dialog->entries[0].replies.push_back(toReply0);
    dialog->replies[0].text = "Go";
    Dialog::EntryReplyLink toEntry1;
    toEntry1.index = 1;
    dialog->replies[0].entries.push_back(toEntry1);
    dialog->entries[1].text = "second";
    dialog->entries[1].delay = 100;
    Dialog::EntryReplyLink toReply1;
    toReply1.index = 1;
    dialog->entries[1].replies.push_back(toReply1);
    dialog->replies[1].text = "Bye";
    return dialog;
}

class C12Test : public Test {
protected:
    void SetUp() override {
        _engine.init();
        _engine.gameOptions().variableStep = false;
        _game = std::make_unique<Game>(GameID::KotOR, std::filesystem::path {}, _engine.options(), _engine.services(), _console);
        _game->initLocalServices();
        _conversation = std::make_unique<C12Conversation>(*_game, _engine.services());
        // K1's videoeffects.2da, first two rows.
        ON_CALL(_engine.resourceModule().twoDas(), get("videoeffects")).WillByDefault(Return(makeTable(
            {"label", "enablesaturation", "modulationred", "modulationgreen", "modulationblue", "saturation", "enablescannoise"},
            {{"VIDEO_EFFECT_SECURITY_CAMERA", "1", "1", "1.4", "2", "0.15", "1"},
             {"VIDEO_EFFECT_FREELOOK_T3M4", "1", "1", "1.4", "2", "0.15", "1"}})));
    }

    // Whole fixed ticks (30 Hz) of simulated time.
    void ticks(int count) {
        for (int i = 0; i < count; ++i) {
            _game->update(1.0f / 30.0f);
        }
    }

    TestEngine _engine;
    StubConsole _console;
    std::unique_ptr<Game> _game;
    std::unique_ptr<C12Conversation> _conversation;
};

} // namespace

// --- DLG fields -------------------------------------------------------------

TEST(C12Dialogs, node_and_dialog_fields_are_read_from_the_dlg) {
    auto entry = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {
            Gff::Field::newCExoLocString("Text", -1, "hello"),
            Gff::Field::newByte("FadeType", 3),
            Gff::Field::newVector("FadeColor", glm::vec3(1.0f, 0.5f, 0.0f)),
            Gff::Field::newFloat("FadeDelay", 0.5f),
            Gff::Field::newFloat("FadeLength", 1.5f),
            Gff::Field::newInt("CamVidEffect", 0),
            Gff::Field::newFloat("CamHeightOffset", 0.8f),
            Gff::Field::newFloat("TarHeightOffset", -0.15f)});
    auto reply = std::make_shared<Gff>(
        0,
        std::vector<Gff::Field> {Gff::Field::newCExoLocString("Text", -1, "bye")});
    auto root = Gff(
        0xffffffff,
        std::vector<Gff::Field> {
            Gff::Field::newList("EntryList", {entry}),
            Gff::Field::newList("ReplyList", {reply}),
            Gff::Field::newByte("UnequipItems", 1),
            Gff::Field::newByte("UnequipHItem", 1),
            Gff::Field::newByte("OldHitCheck", 1),
            Gff::Field::newDword("DelayEntry", 0),
            Gff::Field::newDword("DelayReply", 0),
            Gff::Field::newResRef("AmbientTrack", "MUS_Theme")});

    auto dialog = loadDlg(root);
    ASSERT_TRUE(dialog);
    const auto &e = dialog->entries.at(0);
    EXPECT_EQ(3, e.fadeType);
    EXPECT_FLOAT_EQ(1.0f, e.fadeColor.r);
    EXPECT_FLOAT_EQ(0.5f, e.fadeColor.g);
    EXPECT_FLOAT_EQ(0.5f, e.fadeDelay);
    EXPECT_FLOAT_EQ(1.5f, e.fadeLength);
    EXPECT_EQ(0, e.camVidEffect);
    EXPECT_FLOAT_EQ(0.8f, e.camHeightOffset);
    EXPECT_FLOAT_EQ(-0.15f, e.tarHeightOffset);
    EXPECT_TRUE(dialog->unequipItems);
    EXPECT_TRUE(dialog->unequipHItem);
    EXPECT_TRUE(dialog->oldHitCheck);
    EXPECT_EQ("mus_theme", dialog->ambientTrack);

    // A node without the field has no video effect (row 0 is a real effect).
    const auto &r = dialog->replies.at(0);
    EXPECT_EQ(-1, r.camVidEffect);
    EXPECT_EQ(0, r.fadeType);
}

// --- Dialog node fades --------------------------------------------------------

TEST_F(C12Test, start_entry_fade_in_holds_the_colour_for_its_delay_then_clears) {
    auto dialog = makeTwoEntryDialog();
    // The most common K1 start fade: in from black, 0.5 s delay, 0.5 s length.
    dialog->entries[0].fadeType = 3;
    dialog->entries[0].fadeDelay = 0.5f;
    dialog->entries[0].fadeLength = 0.5f;
    _conversation->start(dialog, nullptr);

    EXPECT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());
    ticks(9); // 0.3 s + the fade's 0.1 s head start: still inside the delay
    EXPECT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());
    ticks(6); // 0.5 s + 0.1: 0.1 s into the fade
    EXPECT_NEAR(0.8f, _game->dialogFade().opacity(), 1e-3f);
    ticks(12); // past 1.0 s
    EXPECT_FLOAT_EQ(0.0f, _game->dialogFade().opacity());
    // The screen fade is not touched.
    EXPECT_FLOAT_EQ(0.0f, _game->globalFade().opacity());
}

TEST_F(C12Test, types_one_and_four_fade_out_and_type_one_is_an_instant_cut) {
    auto dialog = makeTwoEntryDialog();
    dialog->entries[0].fadeType = 4;
    dialog->entries[0].fadeLength = 1.0f;
    dialog->entries[0].fadeColor = glm::vec3(1.0f, 1.0f, 1.0f);
    _conversation->start(dialog, nullptr);
    EXPECT_NEAR(0.1f, _game->dialogFade().opacity(), 1e-3f);
    ticks(15);
    EXPECT_NEAR(0.6f, _game->dialogFade().opacity(), 1e-3f);
    EXPECT_EQ(glm::vec3(1.0f), _game->dialogFade().color());

    // Reply 0 carries type 1 with no length: black at once.
    dialog->replies[0].fadeType = 1;
    _conversation->pickReplyForTest(0);
    EXPECT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());
    EXPECT_EQ(glm::vec3(0.0f), _game->dialogFade().color());
}

TEST_F(C12Test, a_node_without_a_fade_keeps_the_layer_and_the_end_clears_it) {
    auto dialog = makeTwoEntryDialog();
    dialog->entries[0].fadeType = 1;
    _conversation->start(dialog, nullptr);
    ASSERT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());
    // Entry 1 has no fade of its own: the layer stays as it was.
    _conversation->pickReplyForTest(0);
    ticks(3);
    EXPECT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());

    _conversation->pickReplyForTest(0); // terminal reply: the conversation ends
    EXPECT_FALSE(_conversation->isRunning());
    EXPECT_FLOAT_EQ(0.0f, _game->dialogFade().opacity());
}

TEST_F(C12Test, a_new_conversation_starts_with_a_clear_fade_layer) {
    auto first = makeTwoEntryDialog();
    first->entries[0].fadeType = 1;
    _conversation->start(first, nullptr);
    ASSERT_FLOAT_EQ(1.0f, _game->dialogFade().opacity());
    _conversation->start(makeTwoEntryDialog(), nullptr);
    EXPECT_FLOAT_EQ(0.0f, _game->dialogFade().opacity());
}

// --- Video effects ------------------------------------------------------------

TEST_F(C12Test, a_node_video_effect_applies_its_row_and_the_next_plain_node_clears_it) {
    auto dialog = makeTwoEntryDialog();
    dialog->entries[0].camVidEffect = 0;
    _conversation->start(dialog, nullptr);
    const auto &effect = _game->videoEffect();
    EXPECT_EQ(0, effect.row);
    EXPECT_TRUE(effect.saturation);
    EXPECT_FLOAT_EQ(1.4f, effect.modulation.g);
    EXPECT_FLOAT_EQ(2.0f, effect.modulation.b);
    EXPECT_FLOAT_EQ(0.15f, effect.saturationAmount);
    EXPECT_TRUE(effect.scanNoise);

    _conversation->pickReplyForTest(0); // entry 1 has -1
    EXPECT_EQ(-1, _game->videoEffect().row);
}

TEST_F(C12Test, a_script_video_effect_survives_plain_nodes_but_not_the_conversation_end) {
    _game->enableVideoEffect(1);
    _conversation->start(makeTwoEntryDialog(), nullptr);
    EXPECT_EQ(1, _game->videoEffect().row);
    _conversation->pickReplyForTest(0);
    EXPECT_EQ(1, _game->videoEffect().row);
    _conversation->pickReplyForTest(0); // ends
    EXPECT_EQ(-1, _game->videoEffect().row);
    EXPECT_FALSE(_game->scriptVideoEffect());
}

TEST_F(C12Test, enable_and_disable_video_effect_outside_a_conversation) {
    _game->enableVideoEffect(0);
    EXPECT_EQ(0, _game->videoEffect().row);
    _game->enableVideoEffect(7); // no such row: nothing shows
    EXPECT_EQ(-1, _game->videoEffect().row);
    _game->enableVideoEffect(0);
    _game->disableVideoEffect();
    EXPECT_EQ(-1, _game->videoEffect().row);
}

// --- Foley tables ---------------------------------------------------------------

TEST(C12Foley, model_events_map_to_weaponsounds_column_families) {
    int variants = 0;
    EXPECT_EQ("swingshort", combatsounds::eventFamily("Swingshort", variants));
    EXPECT_EQ(3, variants);
    EXPECT_EQ("swingtwirl", combatsounds::eventFamily("SwingTwirl", variants));
    EXPECT_EQ(3, variants);
    EXPECT_EQ("clash", combatsounds::eventFamily("Clash", variants));
    EXPECT_EQ(2, variants);
    EXPECT_EQ("parry", combatsounds::eventFamily("HitParry", variants));
    EXPECT_EQ(2, variants);
    EXPECT_EQ("", combatsounds::eventFamily("snd_footstep", variants));
    EXPECT_EQ(0, variants);
}

TEST(C12Foley, body_fall_column_follows_the_surface_sound) {
    EXPECT_EQ("fallwater", combatsounds::fallColumn("WT"));
    EXPECT_EQ("fallmetal", combatsounds::fallColumn("MT"));
    EXPECT_EQ("fallhard", combatsounds::fallColumn("ST"));
    EXPECT_EQ("falldirt", combatsounds::fallColumn("GR"));
    EXPECT_EQ("falldirt", combatsounds::fallColumn(""));
}

TEST_F(C12Test, weaponsounds_cells_are_read_by_family_and_variant) {
    ON_CALL(_engine.resourceModule().twoDas(), get("weaponsounds")).WillByDefault(Return(makeTable(
        {"label", "leather0", "leather1", "parry0", "parry1", "swingshort0", "swingshort1", "swingshort2"},
        {{"Unarmed", "cb_ht_unarmleth1", "cb_ht_unarmleth1", "cb_pa_unarm1", "cb_pa_unarm1", "cb_sw_unarmed1", "cb_sw_unarmed1", "cb_sw_unarmed1"},
         {"Lightsaber", "cb_ls_hitsoft1", "cb_ls_hitsoft1", "cb_pa_saber1", "cb_pa_saber1", "cb_ls_swingshrt1", "cb_ls_swingshrt2", "cb_ls_swingshrt3"}})));
    auto &twoDas = _engine.services().resource.twoDas;
    EXPECT_EQ("cb_ls_swingshrt3", combatsounds::weaponSound(twoDas, 1, "swingshort", 2));
    EXPECT_EQ("cb_pa_saber1", combatsounds::weaponSound(twoDas, 1, "parry", 0));
    EXPECT_EQ("cb_ht_unarmleth1", combatsounds::weaponSound(twoDas, 0, "leather", 1));
    EXPECT_EQ("", combatsounds::weaponSound(twoDas, 9, "leather", 0));
}

TEST_F(C12Test, sound_cues_record_the_chosen_clip_and_tick) {
    ticks(2);
    _game->playSoundCue("stinger", "MUS_SBAT_Acad");
    ASSERT_EQ(1u, _game->soundCues().size());
    EXPECT_EQ("stinger", _game->soundCues().back().category);
    EXPECT_EQ("mus_sbat_acad", _game->soundCues().back().resRef);
    EXPECT_EQ(2u, _game->soundCues().back().tick);
    _game->playSoundCue("hit", ""); // no cell: nothing recorded
    EXPECT_EQ(1u, _game->soundCues().size());
}

TEST_F(C12Test, a_conversation_ambient_track_plays_beside_the_area_music) {
    auto dialog = makeTwoEntryDialog();
    dialog->ambientTrack = "31b";
    _conversation->start(dialog, nullptr);
    ASSERT_FALSE(_game->soundCues().empty());
    EXPECT_EQ("dialogambient", _game->soundCues().back().category);
    EXPECT_EQ("31b", _game->soundCues().back().resRef);
}
