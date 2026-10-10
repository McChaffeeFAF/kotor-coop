/*
 * Copyright (c) 2020-2023 The reone project contributors
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

// Round trips for every message type, at the edges of what each field allows:
// empty and longest strings, the largest ids, the smallest and largest numbers,
// every enum value, and every list at its limit. Each message is also checked to
// reject every truncation and any trailing byte.

#include <gtest/gtest.h>

#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <string>

#include "reone/net/protocol.h"

using namespace reone::net;

namespace {

const std::string kLongest(kMaxStringBytes, 'x');
const std::string kPlayerNameMax(32, 'n');
const std::string kResRefMax(16, 'r');

// The message must decode, re-encode to the same bytes, and reject every strict
// prefix and any trailing byte. Long messages check a spread of prefixes.
template <class T>
void expectRoundTrip(const T &message, const char *what) {
    auto bytes = encode(message);
    ASSERT_FALSE(bytes.empty()) << what;
    T back;
    ASSERT_TRUE(decode(bytes, back)) << what;
    EXPECT_EQ(bytes, encode(back)) << what;

    size_t step = bytes.size() > 4096 ? bytes.size() / 2048 : 1;
    for (size_t n = 0; n < bytes.size(); n += step) {
        std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + n);
        T partial;
        EXPECT_FALSE(decode(prefix, partial)) << what << ": accepted a prefix of " << n << " bytes";
    }
    auto longer = bytes;
    longer.push_back(0);
    T extra;
    EXPECT_FALSE(decode(longer, extra)) << what << ": accepted a trailing byte";
}

Fingerprint fingerprint(uint64_t v) {
    Fingerprint f;
    f.present = true;
    f.key = v;
    f.modules = v - 1;
    f.tlk = v / 3;
    return f;
}

} // namespace

TEST(NetRoundTrip, hello_at_its_limits) {
    Hello empty;
    empty.protocol = 0;
    expectRoundTrip(empty, "empty hello");

    Hello full;
    full.protocol = UINT16_MAX;
    full.build = kLongest;
    full.mode = "free";
    full.playerName = kLongest;
    full.k1 = fingerprint(UINT64_MAX);
    full.k2 = fingerprint(1);
    full.rejoinToken = UINT64_MAX;
    expectRoundTrip(full, "full hello");
}

TEST(NetRoundTrip, welcome_at_its_limits) {
    Welcome w;
    w.clientId = UINT32_MAX;
    w.hostTick = UINT32_MAX;
    w.tickRate = 1000;
    w.mode = kLongest;
    w.rejoinToken = UINT64_MAX;
    expectRoundTrip(w, "welcome");
    w.tickRate = 1;
    expectRoundTrip(w, "slowest tick rate");
    expectRoundTrip(Welcome {}, "default welcome");

    // A tick rate outside 1..1000 is not a host this client can follow.
    Welcome out;
    for (uint16_t rate : {uint16_t(0), uint16_t(1001), uint16_t(UINT16_MAX)}) {
        w.tickRate = rate;
        EXPECT_FALSE(decode(encode(w), out)) << rate;
    }
}

TEST(NetRoundTrip, reject_for_every_reason) {
    for (int reason = static_cast<int>(RejectReason::ProtocolMismatch); reason <= static_cast<int>(RejectReason::Kicked); ++reason) {
        Reject r;
        r.reason = static_cast<RejectReason>(reason);
        r.detail = reason % 2 ? kLongest : "";
        expectRoundTrip(r, rejectReasonName(r.reason));
    }
}

TEST(NetRoundTrip, slot_request_and_assign) {
    expectRoundTrip(SlotRequest {}, "empty request");
    expectRoundTrip(SlotRequest {kLongest}, "longest tag");
    for (int status = static_cast<int>(SlotStatus::Assigned); status <= static_cast<int>(SlotStatus::Guest); ++status) {
        for (int32_t npc : {-1, 0, INT32_MAX, INT32_MIN}) {
            SlotAssign a;
            a.status = static_cast<SlotStatus>(status);
            a.npc = npc;
            a.creatureId = UINT32_MAX;
            a.tag = kLongest;
            expectRoundTrip(a, slotStatusName(a.status));
        }
    }
}

TEST(NetRoundTrip, module_info_at_its_limits) {
    ModuleInfo m;
    m.generation = UINT32_MAX;
    m.module = kResRefMax;
    m.player.hostId = UINT32_MAX;
    m.player.appearance = 99999;
    m.player.gender = UINT8_MAX;
    m.player.name = kLongest;
    m.player.portrait = 99999;
    expectRoundTrip(m, "module info");
    m.player.appearance = 0;
    m.player.portrait = -1;
    expectRoundTrip(m, "module info, lowest appearance and no portrait");

    // Just outside what a 2da row can be, and a module name that is not a resref.
    ModuleInfo out;
    for (int32_t appearance : {-1, 100000, INT32_MIN, INT32_MAX}) {
        auto bad = m;
        bad.player.appearance = appearance;
        EXPECT_FALSE(decode(encode(bad), out)) << "appearance " << appearance;
    }
    for (int32_t portrait : {-2, 100000, INT32_MIN, INT32_MAX}) {
        auto bad = m;
        bad.player.portrait = portrait;
        EXPECT_FALSE(decode(encode(bad), out)) << "portrait " << portrait;
    }
    for (const char *name : {"", "a b", "a-b", "../x", "seventeen_chars__"}) {
        auto bad = m;
        bad.module = name;
        EXPECT_FALSE(decode(encode(bad), out)) << "module '" << name << "'";
    }
}

TEST(NetRoundTrip, snapshot_with_every_field_at_an_extreme) {
    Snapshot s;
    s.tick = UINT32_MAX;
    s.generation = UINT32_MAX;
    s.baseTick = 0;
    s.worldFields = kWorldAllMask;
    s.world.screen = UINT8_MAX;
    s.world.fadeOpacity = 1.0f;
    s.world.fadeColor = {FLT_MAX, -FLT_MAX, FLT_MIN};
    s.world.movie = true;
    s.world.movieName = kLongest;
    s.world.cameraType = UINT8_MAX;
    s.world.cameraId = UINT32_MAX;
    s.world.conversation.active = true;
    s.world.conversation.kind = 1;
    s.world.conversation.dialog = kLongest;
    s.world.conversation.entry = INT32_MIN;
    s.world.conversation.owner = UINT32_MAX;
    s.world.conversation.speaker = kLongest;
    s.world.conversation.replies.assign(kMaxReplies, INT32_MAX);
    s.world.conversation.entryEnded = true;
    for (size_t i = 0; i < kMaxPartyEntries; ++i) {
        s.world.party.push_back({static_cast<int8_t>(i % 2 ? INT8_MIN : INT8_MAX), UINT32_MAX});
    }
    s.world.leader = UINT32_MAX;
    s.world.paused = true;
    s.world.pausedBy = kLongest;

    EntityRecord e;
    e.fields = kFieldAllMask;
    e.state.id = UINT32_MAX;
    e.state.type = UINT8_MAX;
    e.state.key = kLongest;
    e.state.tag = kLongest;
    e.state.resref = kLongest;
    e.state.position = {FLT_MAX, -FLT_MAX, 0.0f};
    e.state.facing = dequantizeFacing(quantizeFacing(3.0f));
    e.state.anim = kLongest;
    e.state.animTime = 3600.0f;
    e.state.animFlags = UINT16_MAX;
    e.state.animSpeed = 100.0f;
    e.state.animSerial = UINT8_MAX;
    e.state.hp = INT16_MIN;
    e.state.maxHp = INT16_MAX;
    e.state.dead = true;
    e.state.rightWeapon = kLongest;
    e.state.leftWeapon = kLongest;
    e.state.armor = kLongest;
    e.state.headgear = kLongest;
    e.state.open = true;
    e.state.locked = true;
    e.state.doorState = 3;
    e.state.doorTransition = 2;
    e.state.flags = 0x3f;
    e.state.attackTarget = UINT32_MAX;
    s.entities.push_back(e);
    EntityRecord removed;
    removed.fields = kFieldRemoved;
    removed.state.id = 1;
    s.entities.push_back(removed);
    expectRoundTrip(s, "extreme snapshot");

    Snapshot delta;
    delta.tick = 10;
    delta.baseTick = 9;
    delta.worldFields = 0;
    expectRoundTrip(delta, "empty delta");
}

TEST(NetRoundTrip, snapshot_with_the_most_entities) {
    Snapshot s;
    s.tick = 5;
    s.worldFields = 0;
    for (size_t i = 0; i < kMaxEntities; ++i) {
        EntityRecord e;
        e.fields = kFieldRemoved;
        e.state.id = static_cast<uint32_t>(i + 1);
        s.entities.push_back(e);
    }
    expectRoundTrip(s, "4096 entities");
}

TEST(NetRoundTrip, event_batch_with_every_kind) {
    EventBatch b;
    b.generation = UINT32_MAX;
    b.tick = UINT32_MAX;
    for (size_t i = 0; i < kMaxEvents; ++i) {
        NetEvent e;
        e.kind = static_cast<EventKind>(1 + i % 3);
        e.handle = UINT32_MAX;
        e.row = UINT32_MAX;
        e.a = UINT32_MAX;
        e.b = UINT32_MAX;
        e.location = {FLT_MAX, -FLT_MAX, 1.0f};
        e.facing = 0.0f;
        e.flags = static_cast<uint8_t>(i % 4);
        b.events.push_back(e);
    }
    expectRoundTrip(b, "events");
    expectRoundTrip(EventBatch {}, "no events");
}

TEST(NetRoundTrip, snapshot_ack) {
    expectRoundTrip(SnapshotAck {UINT32_MAX, UINT32_MAX}, "max ack");
    expectRoundTrip(SnapshotAck {0, 1}, "smallest ack");
    // Tick 0 means "against nothing" and is never a snapshot the client holds.
    SnapshotAck out;
    EXPECT_FALSE(decode(encode(SnapshotAck {}), out));
}

TEST(NetRoundTrip, every_command_type_with_extreme_values) {
    for (int type = static_cast<int>(CommandType::MoveTo); type <= static_cast<int>(CommandType::BenchClose); ++type) {
        Command c;
        c.seq = UINT32_MAX;
        c.type = static_cast<CommandType>(type);
        c.actor = UINT32_MAX;
        c.target = UINT32_MAX;
        c.point = {100000.0f, -100000.0f, 0.0f};
        c.run = type % 2 == 0;
        c.param = type % 2 ? INT32_MIN : INT32_MAX;
        c.item = type % 3 == 0 ? std::string(64, 'i') : "";
        expectRoundTrip(c, commandTypeName(c.type));
    }
}

TEST(NetRoundTrip, command_fields_just_outside_their_limits_are_rejected) {
    Command c;
    c.type = CommandType::MoveTo;
    Command out;
    ASSERT_TRUE(decode(encode(c), out));

    c.item = std::string(65, 'i'); // a command's item is at most 64 bytes, not the 128 other messages allow
    EXPECT_FALSE(decode(encode(c), out));
    c.item.clear();

    for (float v : {100000.5f, -100000.5f, FLT_MAX, -FLT_MAX}) {
        for (int axis = 0; axis < 3; ++axis) {
            auto bad = c;
            (axis == 0 ? bad.point.x : axis == 1 ? bad.point.y : bad.point.z) = v;
            EXPECT_FALSE(decode(encode(bad), out)) << v << " on axis " << axis;
        }
    }
}

TEST(NetRoundTrip, non_finite_floats_are_rejected_wherever_they_appear) {
    const float bad[] = {NAN, INFINITY, -INFINITY};
    for (float v : bad) {
        Command c;
        c.type = CommandType::MoveTo;
        c.point.y = v;
        Command cout;
        EXPECT_FALSE(decode(encode(c), cout)) << "command point";

        Bark bark {"hello", v};
        Bark bout;
        EXPECT_FALSE(decode(encode(bark), bout)) << "bark duration";

        EventBatch batch;
        NetEvent e;
        e.kind = EventKind::VfxBegin;
        e.location.z = v;
        batch.events.push_back(e);
        EventBatch eout;
        EXPECT_FALSE(decode(encode(batch), eout)) << "event location";
        batch.events[0].location.z = 0.0f;
        batch.events[0].facing = v;
        EXPECT_FALSE(decode(encode(batch), eout)) << "event facing";

        for (int field = 0; field < 5; ++field) {
            Snapshot s;
            s.tick = 1;
            EntityRecord rec;
            rec.state.id = 1;
            switch (field) {
            case 0:
                rec.fields = kFieldPosition;
                rec.state.position.x = v;
                break;
            case 1:
                rec.fields = kFieldAnim;
                rec.state.animTime = v;
                break;
            case 2:
                rec.fields = kFieldAnim;
                rec.state.animSpeed = v;
                break;
            case 3:
                s.worldFields = kWorldFade;
                s.world.fadeOpacity = v;
                break;
            default:
                s.worldFields = kWorldFade;
                s.world.fadeColor.z = v;
                break;
            }
            if (rec.fields) {
                s.entities.push_back(rec);
            }
            Snapshot sout;
            EXPECT_FALSE(decode(encode(s), sout)) << "snapshot field " << field;
        }
    }
}

TEST(NetRoundTrip, command_result_for_every_reason) {
    for (int reason = 0; reason <= static_cast<int>(CommandReject::NotBreakable); ++reason) {
        CommandResult r;
        r.seq = UINT32_MAX;
        r.accepted = reason == 0;
        r.reason = static_cast<CommandReject>(reason);
        r.hostTick = UINT32_MAX;
        expectRoundTrip(r, commandRejectName(r.reason));
    }
}

TEST(NetRoundTrip, bye_and_bark_and_notice) {
    expectRoundTrip(Bye {}, "empty bye");
    expectRoundTrip(Bye {kLongest}, "longest bye");
    expectRoundTrip(Bark {"", 0.0f}, "empty bark");
    expectRoundTrip(Bark {kLongest, 600.0f}, "longest bark");
    for (int kind = 0; kind <= static_cast<int>(NoticeKind::TerminalControl); ++kind) {
        Notice n;
        n.kind = static_cast<NoticeKind>(kind);
        n.text = kind % 2 ? kLongest : "";
        expectRoundTrip(n, "notice");
    }
}

TEST(NetRoundTrip, container_view_at_its_limit) {
    ContainerView v;
    v.container = UINT32_MAX;
    v.open = true;
    for (size_t i = 0; i < kMaxContainerItems; ++i) {
        v.items.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, kLongest, UINT16_MAX});
    }
    expectRoundTrip(v, "256 items");
    v.open = false;
    v.items.clear();
    expectRoundTrip(v, "closed");
}

TEST(NetRoundTrip, ready_check_for_every_state) {
    for (int state = 0; state <= static_cast<int>(ReadyState::Cancelled); ++state) {
        ReadyCheck c;
        c.id = UINT32_MAX;
        c.state = static_cast<ReadyState>(state);
        c.initiator = kLongest;
        c.destination = kLongest;
        c.secondsLeft = UINT16_MAX;
        for (size_t i = 0; i < kMaxReadyPlayers; ++i) {
            c.players.push_back({kLongest, static_cast<ReadyAnswer>(i % 3), i % 2 == 0});
        }
        expectRoundTrip(c, "ready check");
    }
}

TEST(NetRoundTrip, shared_inventory_at_its_limit) {
    SharedInventory inv;
    inv.owner = UINT32_MAX;
    inv.credits = INT32_MAX;
    for (size_t i = 0; i < kMaxInventoryItems; ++i) {
        inv.items.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, UINT16_MAX, kInventoryUsable});
    }
    expectRoundTrip(inv, "1024 items");
    expectRoundTrip(SharedInventory {1, 0, {}}, "no items");
}

TEST(NetRoundTrip, character_sheet_at_its_limits) {
    CharacterSheet s;
    s.creature = UINT32_MAX;
    s.xp = INT32_MAX;
    s.levelUp = true;
    s.classes = {{INT16_MAX, 60}, {0, 0}, {5, 30}};
    for (int i = 0; i < 6; ++i) {
        s.abilities[i] = UINT8_MAX;
    }
    for (int i = 0; i < 8; ++i) {
        s.skills[i] = UINT8_MAX;
    }
    s.feats.assign(kMaxSheetEntries, UINT16_MAX);
    s.spells.assign(kMaxSheetEntries, UINT16_MAX);
    s.baseHp = INT16_MAX;
    s.baseFp = INT16_MIN;
    for (size_t i = 0; i < kMaxSheetEquipment; ++i) {
        s.equipment.push_back({static_cast<uint8_t>(i % 31), UINT32_MAX - static_cast<uint32_t>(i), kResRefMax});
    }
    expectRoundTrip(s, "full sheet");
}

TEST(NetRoundTrip, guest_character_at_its_limits) {
    GuestCharacter g;
    g.name = kPlayerNameMax;
    g.appearance = 100000;
    g.gender = 4;
    g.portrait = 100000;
    g.classes = {{INT16_MAX, 50}, {1, 1}, {2, 25}};
    for (int i = 0; i < 6; ++i) {
        g.abilities[i] = UINT8_MAX;
    }
    for (int i = 0; i < 8; ++i) {
        g.skills[i] = UINT8_MAX;
    }
    g.feats.assign(kMaxSheetEntries, UINT16_MAX);
    g.spells.assign(kMaxSheetEntries, UINT16_MAX);
    g.xp = INT32_MAX;
    g.credits = INT32_MAX;
    g.baseHp = INT16_MAX;
    g.currentHp = INT16_MIN;
    g.baseFp = INT16_MAX;
    g.goodEvil = 100;
    g.items.assign(kMaxGuestItems, GuestItem {kResRefMax, UINT16_MAX, kGuestInventory});
    auto bytes = encode(g);
    // A maximal guest is larger than the guest-character limit by design of this
    // test (two 1024-entry lists); trim it so the edge under test is the item list.
    g.feats.resize(64);
    g.spells.resize(64);
    for (size_t i = 0; i < g.items.size(); ++i) {
        g.items[i].upgrades = UINT32_MAX - static_cast<uint32_t>(i);
        g.items[i].modelVariation = static_cast<int16_t>(i % 6) - 1;
    }
    g.components = INT32_MAX;
    g.chemicals = 1;
    auto full = encode(g);
    GuestCharacter back;
    ASSERT_TRUE(decode(full, back));
    EXPECT_EQ(full, encode(back));
    EXPECT_EQ(g.items.back().upgrades, back.items.back().upgrades);
    EXPECT_EQ(INT32_MAX, back.components);
    EXPECT_LE(full.size(), kMaxGuestCharacterBytes);
    // Every prefix is refused, except the one that ends where files saved before workbenches
    // end: those stay valid, with no upgrades and no material.
    size_t legacy = full.size() - (1 + 4 + 4 + g.items.size() * 6);
    for (size_t n = 0; n < full.size(); n += 7) {
        std::vector<uint8_t> prefix(full.begin(), full.begin() + n);
        GuestCharacter partial;
        EXPECT_FALSE(decode(prefix, partial)) << "prefix of " << n;
    }
    std::vector<uint8_t> old(full.begin(), full.begin() + legacy);
    GuestCharacter oldBack;
    ASSERT_TRUE(decode(old, oldBack)) << "a file from before the workbenches";
    EXPECT_EQ(g.items.size(), oldBack.items.size());
    EXPECT_EQ(0u, oldBack.items[0].upgrades);
    EXPECT_EQ(-1, oldBack.items[0].modelVariation);
    EXPECT_EQ(0, oldBack.components);
    auto longer = full;
    longer.push_back(0);
    EXPECT_FALSE(decode(longer, back)) << "a trailing byte";
    auto wrongTag = full;
    wrongTag[legacy] = 2;
    EXPECT_FALSE(decode(wrongTag, back)) << "an unknown extension";
    (void)bytes;
}

TEST(NetRoundTrip, bench_view_at_its_limits_and_closed) {
    BenchView v;
    v.bench = UINT32_MAX;
    v.kind = BenchKind::Lab;
    v.open = true;
    v.components = INT32_MAX;
    v.chemicals = INT32_MAX;
    for (size_t i = 0; i < kMaxBenchItems; ++i) {
        BenchItem item;
        item.id = UINT32_MAX - static_cast<uint32_t>(i);
        item.resref = kResRefMax;
        item.category = 1 + i % 4;
        item.equipped = i % 2 == 0;
        item.upgrades = UINT32_MAX;
        item.modelVariation = 1000;
        for (size_t s = 0; s < kMaxBenchSlots; ++s) {
            BenchSlot slot;
            slot.type = static_cast<uint8_t>(s);
            slot.color = s == 0;
            slot.installedRow = static_cast<int16_t>(s % 2 ? 31 : -1);
            for (size_t c = 0; c < 8; ++c) {
                slot.candidates.push_back(UINT32_MAX - static_cast<uint32_t>(c));
            }
            item.slots.push_back(std::move(slot));
        }
        v.items.push_back(std::move(item));
    }
    for (size_t i = 0; i < kMaxBenchParts; ++i) {
        v.parts.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, UINT16_MAX, 0});
    }
    for (size_t i = 0; i < kMaxBenchBreakdown; ++i) {
        v.breakdown.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, UINT16_MAX, UINT16_MAX, static_cast<uint8_t>(i % 2)});
    }
    for (size_t i = 0; i < kMaxBenchRecipes; ++i) {
        v.recipes.push_back({kResRefMax, static_cast<uint8_t>(i % 2), INT32_MAX, i % 2 == 0, i % 3 == 0});
    }
    expectRoundTrip(v, "full bench");
    v.kind = BenchKind::Workbench;
    v.open = false;
    v.items.clear();
    v.parts.clear();
    v.breakdown.clear();
    v.recipes.clear();
    expectRoundTrip(v, "closed bench");
}

TEST(NetRoundTrip, bench_view_rejects_what_the_host_would_never_send) {
    BenchView good;
    good.bench = 5;
    good.items.push_back({7, "g_w_blaster", 2, false, 0, -1, {}});
    good.breakdown.push_back({8, "g_w_blaster", 1, 3, 0});
    good.recipes.push_back({"g_w_crafted", 0, 4, true, true});
    BenchView out;
    ASSERT_TRUE(decode(encode(good), out));

    auto bad = [&](const char *what, auto change) {
        auto v = good;
        change(v);
        EXPECT_FALSE(decode(encode(v), out)) << what;
    };
    bad("no bench", [](BenchView &v) { v.bench = 0; });
    bad("negative material", [](BenchView &v) { v.components = -1; });
    bad("negative chemicals", [](BenchView &v) { v.chemicals = -1; });
    bad("item without id", [](BenchView &v) { v.items[0].id = 0; });
    bad("item category 0", [](BenchView &v) { v.items[0].category = 0; });
    bad("item category 5", [](BenchView &v) { v.items[0].category = 5; });
    bad("bad resref", [](BenchView &v) { v.items[0].resref = "has space"; });
    bad("too many slots", [](BenchView &v) { v.items[0].slots.resize(kMaxBenchSlots + 1); });
    bad("candidate id 0", [](BenchView &v) { v.items[0].slots = {BenchSlot {0, false, -1, {0}}}; });
    bad("installed row out of range", [](BenchView &v) { v.items[0].slots = {BenchSlot {0, false, 32, {}}}; });
    bad("too many items", [](BenchView &v) { v.items.resize(kMaxBenchItems + 1, v.items[0]); });
    bad("too many candidates", [](BenchView &v) { v.items[0].slots = {BenchSlot {0, false, -1, std::vector<uint32_t>(kMaxBenchCandidates + 1, 1)}}; });
    bad("zero stack", [](BenchView &v) { v.breakdown[0].stack = 0; });
    bad("breakdown material 2", [](BenchView &v) { v.breakdown[0].material = 2; });
    bad("recipe material 2", [](BenchView &v) { v.recipes[0].material = 2; });
    bad("negative cost", [](BenchView &v) { v.recipes[0].cost = -1; });
    bad("too many recipes", [](BenchView &v) { v.recipes.resize(kMaxBenchRecipes + 1, v.recipes[0]); });
    bad("too many parts", [](BenchView &v) { v.parts.resize(kMaxBenchParts + 1, StoreEntry {1, "g_i_up", 1, 0}); });
    auto bytes = encode(good);
    bytes[5] = 9; // the kind byte
    EXPECT_FALSE(decode(bytes, out)) << "unknown kind";
    EXPECT_TRUE(isHostMessage(MessageType::BenchView));
    EXPECT_FALSE(isClientMessage(MessageType::BenchView));
    EXPECT_STREQ("bench_view", messageTypeName(MessageType::BenchView));
    ASSERT_TRUE(peekType(encode(good)));
    EXPECT_EQ(MessageType::BenchView, *peekType(encode(good)));
}

TEST(NetRoundTrip, store_view_at_its_limits) {
    StoreView v;
    v.store = UINT32_MAX;
    v.open = true;
    v.credits = INT32_MAX;
    for (size_t i = 0; i < kMaxStoreItems; ++i) {
        v.stock.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, UINT16_MAX, INT32_MAX});
        v.sellable.push_back({UINT32_MAX - static_cast<uint32_t>(i), kResRefMax, 1, 0});
    }
    expectRoundTrip(v, "full store");
    v.open = false;
    v.stock.clear();
    v.sellable.clear();
    expectRoundTrip(v, "closed store");
}

TEST(NetRoundTrip, guest_roster_offer) {
    GuestRoster r;
    for (int i = 0; i < 16; ++i) {
        r.entries.push_back({kPlayerNameMax, INT16_MIN, INT16_MAX, 50, i % 2 == 0});
    }
    expectRoundTrip(r, "full roster");
    expectRoundTrip(GuestRoster {}, "empty roster");

    for (int kind = static_cast<int>(OfferKind::Store); kind <= static_cast<int>(OfferKind::MiniGame); ++kind) {
        Offer o;
        o.kind = static_cast<OfferKind>(kind);
        o.target = UINT32_MAX;
        o.name = kLongest;
        o.maximumWager = INT32_MAX;
        expectRoundTrip(o, "offer");
    }
}

#include "reone/net/joincode.h"

// Found by fuzzing: base64Decode accepted text whose padding bits were not zero
// ("QR==" for the byte 'A'), although it is documented as accepting only the
// canonical encoding, so two texts named the same bytes.
TEST(NetRoundTrip, base64_decoding_is_canonical) {
    std::vector<uint8_t> out;
    ASSERT_TRUE(base64Decode("QQ==", out));
    EXPECT_EQ((std::vector<uint8_t> {'A'}), out);
    EXPECT_FALSE(base64Decode("QR==", out)) << "non-zero padding bits after one byte";
    ASSERT_TRUE(base64Decode("QUI=", out));
    EXPECT_EQ((std::vector<uint8_t> {'A', 'B'}), out);
    EXPECT_FALSE(base64Decode("QUJ=", out)) << "non-zero padding bits after two bytes";

    for (size_t length = 0; length < 12; ++length) {
        std::vector<uint8_t> bytes(length);
        for (size_t i = 0; i < length; ++i) {
            bytes[i] = static_cast<uint8_t>(i * 37 + 11);
        }
        auto text = base64Encode(bytes.data(), bytes.size());
        ASSERT_TRUE(base64Decode(text, out)) << length;
        EXPECT_EQ(bytes, out) << length;
    }
}
