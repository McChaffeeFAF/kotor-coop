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
#include <limits>
#include <string>
#include <vector>

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
    e.state.flags = 0xff; // every flag, kFlagStealthed (protocol 9) and kFlagAutoCombat (protocol 11) included
    e.state.attackTarget = UINT32_MAX;
    e.state.faction = kNoFaction;
    s.entities.push_back(e);
    EntityRecord faction;
    faction.fields = kFieldFaction;
    faction.state.id = 2;
    faction.state.faction = static_cast<uint8_t>(kMaxFactions - 1);
    s.entities.push_back(faction);
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
    for (int type = 0; type < 256; ++type) {
        if (std::string(commandTypeName(static_cast<CommandType>(type))) == "unknown") {
            continue; // not a type the protocol names
        }
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
    for (int reason = 0; reason < 256; ++reason) {
        if (std::string(commandRejectName(static_cast<CommandReject>(reason))) == "unknown") {
            continue;
        }
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

TEST(NetRoundTrip, minigame_control_at_its_limits) {
    MiniGameControl c;
    c.seq = UINT32_MAX;
    c.steer = -1.0f;
    c.fire = true;
    c.pitchRate = 1.0f;
    c.yawRate = -1.0f;
    c.pitchDelta = kMaxMiniGameDelta;
    c.yawDelta = -kMaxMiniGameDelta;
    expectRoundTrip(c, "extreme control");
    expectRoundTrip(MiniGameControl {}, "neutral control");
    EXPECT_TRUE(isClientMessage(MessageType::MiniGameControl));
    EXPECT_FALSE(isHostMessage(MessageType::MiniGameControl));
    EXPECT_STREQ("minigame_control", messageTypeName(MessageType::MiniGameControl));
    ASSERT_TRUE(peekType(encode(c)));
    EXPECT_EQ(MessageType::MiniGameControl, *peekType(encode(c)));
    EXPECT_LE(encode(c).size(), kMaxClientMessageBytes);
}

TEST(NetRoundTrip, steer_moves_a_guest_s_creature_directly) {
    EXPECT_GE(kProtocolVersion, 11);
    EXPECT_EQ(130, static_cast<int>(MessageType::Steer));
    Steer moving;
    moving.seq = UINT32_MAX;
    moving.actor = 0xfedcba98u;
    moving.moving = true;
    moving.facing = -3.1f;
    moving.run = false;
    expectRoundTrip(moving, "moving walk");
    expectRoundTrip(Steer {}, "stop");
    Steer back;
    ASSERT_TRUE(decode(encode(moving), back));
    EXPECT_EQ(moving.seq, back.seq);
    EXPECT_EQ(moving.actor, back.actor);
    EXPECT_TRUE(back.moving);
    EXPECT_FLOAT_EQ(-3.1f, back.facing);
    EXPECT_FALSE(back.run);
    EXPECT_TRUE(isClientMessage(MessageType::Steer));
    EXPECT_FALSE(isHostMessage(MessageType::Steer));
    EXPECT_STREQ("steer", messageTypeName(MessageType::Steer));
    ASSERT_TRUE(peekType(encode(moving)));
    EXPECT_EQ(MessageType::Steer, *peekType(encode(moving)));
    EXPECT_FALSE(peekType(std::vector<uint8_t> {131})) << "only 130 is taken";

    auto refused = [&](const char *what, float facing) {
        Steer c;
        c.moving = true;
        c.facing = facing;
        EXPECT_FALSE(decode(encode(c), back)) << what;
    };
    refused("facing NaN", std::numeric_limits<float>::quiet_NaN());
    refused("facing infinite", std::numeric_limits<float>::infinity());
    refused("facing far beyond a few turns", 1000.0f);
}

TEST(NetRoundTrip, minigame_control_refuses_values_a_player_could_not_produce) {
    MiniGameControl out;
    auto refused = [&](const char *what, auto change) {
        MiniGameControl c;
        change(c);
        EXPECT_FALSE(decode(encode(c), out)) << what;
    };
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    refused("steer above 1", [](MiniGameControl &c) { c.steer = 1.01f; });
    refused("steer below -1", [](MiniGameControl &c) { c.steer = -1.5f; });
    refused("steer NaN", [&](MiniGameControl &c) { c.steer = nan; });
    refused("steer infinite", [&](MiniGameControl &c) { c.steer = inf; });
    refused("pitch rate above 1", [](MiniGameControl &c) { c.pitchRate = 2.0f; });
    refused("yaw rate infinite", [&](MiniGameControl &c) { c.yawRate = -inf; });
    refused("yaw rate NaN", [&](MiniGameControl &c) { c.yawRate = nan; });
    refused("pitch delta above the limit", [](MiniGameControl &c) { c.pitchDelta = kMaxMiniGameDelta + 1.0f; });
    refused("yaw delta below the limit", [](MiniGameControl &c) { c.yawDelta = -kMaxMiniGameDelta - 1.0f; });
    refused("delta NaN", [&](MiniGameControl &c) { c.pitchDelta = nan; });
    refused("delta infinite", [&](MiniGameControl &c) { c.yawDelta = inf; });
}

TEST(NetRoundTrip, minigame_state_at_its_limits_and_final) {
    MiniGameState s;
    s.active = true;
    s.kind = MiniGameKind::Turret;
    s.module = kResRefMax;
    s.owner = kPlayerNameMax;
    s.tick = UINT32_MAX;
    s.elapsed = 12345.5f;
    s.speed = 900.0f;
    s.offset = {100000.0f, -100000.0f, 3.0f};
    s.hp = INT16_MAX;
    s.maxHp = INT16_MAX;
    for (size_t i = 0; i < kMaxMiniGameActors; ++i) {
        s.actors.push_back({static_cast<MiniGameRole>(i % 3), static_cast<int16_t>(i), i % 2 == 0, {1.0f * i, -2.0f * i, 0.5f},
                            {0.0f, 0.0f, i % 2 == 0 ? 0.70710678f : -1.0f, i % 2 == 0 ? 0.70710678f : 0.0f}});
    }
    s.firing = true;
    s.controlSeq = UINT32_MAX;
    for (size_t i = 0; i < kMaxMiniGameAnimations; ++i) {
        s.animations.push_back({static_cast<uint16_t>(kMaxMiniGameActors - 1 - i), std::string(32, 'a' + i % 26), static_cast<uint8_t>(i % 16)});
    }
    for (size_t i = 0; i < kMaxMiniGameSounds; ++i) {
        s.sounds.push_back(i % 2 == 0 ? std::string(kResRefMax) : "mgs_beep");
    }
    expectRoundTrip(s, "full minigame state");
    s.animations.clear();
    s.sounds.clear();
    s.active = false;
    s.finished = true;
    s.quit = true;
    s.prize = INT32_MAX;
    s.actors.clear();
    expectRoundTrip(s, "final minigame state");
    s.kind = MiniGameKind::SwoopRace;
    s.owner.clear();
    expectRoundTrip(s, "the host's own minigame");
    EXPECT_TRUE(isHostMessage(MessageType::MiniGameState));
    EXPECT_FALSE(isClientMessage(MessageType::MiniGameState));
    EXPECT_STREQ("minigame_state", messageTypeName(MessageType::MiniGameState));
    ASSERT_TRUE(peekType(encode(s)));
    EXPECT_EQ(MessageType::MiniGameState, *peekType(encode(s)));
}

TEST(NetRoundTrip, minigame_state_refuses_what_the_host_would_never_send) {
    MiniGameState good;
    good.module = "tar_m03mg";
    good.actors.push_back({MiniGameRole::Player, 5, false, {1, 2, 3}});
    MiniGameState out;
    ASSERT_TRUE(decode(encode(good), out));
    auto refused = [&](const char *what, auto change) {
        auto s = good;
        change(s);
        EXPECT_FALSE(decode(encode(s), out)) << what;
    };
    refused("module too long", [](MiniGameState &s) { s.module = std::string(17, 'm'); });
    refused("owner too long", [](MiniGameState &s) { s.owner = std::string(33, 'n'); });
    refused("negative time", [](MiniGameState &s) { s.elapsed = -1.0f; });
    refused("NaN time", [](MiniGameState &s) { s.elapsed = std::numeric_limits<float>::quiet_NaN(); });
    refused("negative prize", [](MiniGameState &s) { s.prize = -1; });
    refused("negative max hp", [](MiniGameState &s) { s.maxHp = -1; });
    refused("offset out of the world", [](MiniGameState &s) { s.offset.x = 100001.0f; });
    refused("too many actors", [](MiniGameState &s) { s.actors.resize(kMaxMiniGameActors + 1, s.actors[0]); });
    refused("an unknown kind", [](MiniGameState &s) { s.kind = static_cast<MiniGameKind>(7); });
    refused("an unknown role", [](MiniGameState &s) { s.actors[0].kind = static_cast<MiniGameRole>(3); });
    refused("an animation on no actor", [](MiniGameState &s) { s.animations.push_back({1, "gear1", 0}); });
    refused("an animation with no name", [](MiniGameState &s) { s.animations.push_back({0, "", 0}); });
    refused("an animation name too long", [](MiniGameState &s) { s.animations.push_back({0, std::string(33, 'n'), 0}); });
    refused("unknown animation flags", [](MiniGameState &s) { s.animations.push_back({0, "gear1", 16}); });
    refused("too many animations", [](MiniGameState &s) { s.animations.resize(kMaxMiniGameAnimations + 1, {0, "gear1", 0}); });
    refused("an empty sound", [](MiniGameState &s) { s.sounds.push_back(""); });
    refused("a sound name too long", [](MiniGameState &s) { s.sounds.push_back(std::string(17, 's')); });
    refused("too many sounds", [](MiniGameState &s) { s.sounds.resize(kMaxMiniGameSounds + 1, "mgs_beep"); });
    refused("a zero rotation", [](MiniGameState &s) { s.actors[0].rotation = {0.0f, 0.0f, 0.0f, 0.0f}; });
    refused("a rotation that is not one", [](MiniGameState &s) { s.actors[0].rotation = {0.5f, 0.0f, 0.0f, 0.5f}; });
}

// The protocol accepts exactly the enum values it names: a byte the host's code never sends is a
// malformed message, and every value it does name round-trips. Scanning all 256 bytes means a value
// added to an enum is covered without anyone touching this test.

TEST(NetCoverage, a_command_type_decodes_exactly_when_the_protocol_names_it) {
    int named = 0;
    for (int type = 0; type < 256; ++type) {
        Command c;
        c.seq = 1;
        c.type = static_cast<CommandType>(type);
        Command out;
        bool known = std::string(commandTypeName(c.type)) != "unknown";
        EXPECT_EQ(known, decode(encode(c), out)) << "command type " << type;
        named += known ? 1 : 0;
    }
    EXPECT_GE(named, 28) << "the protocol names more command types than that";
}

TEST(NetCoverage, a_refusal_decodes_exactly_when_the_protocol_names_it) {
    int named = 0;
    for (int reason = 0; reason < 256; ++reason) {
        CommandResult r;
        r.seq = 1;
        r.reason = static_cast<CommandReject>(reason);
        r.accepted = reason == 0;
        CommandResult out;
        bool known = std::string(commandRejectName(r.reason)) != "unknown";
        EXPECT_EQ(known, decode(encode(r), out)) << "reject " << reason;
        named += known ? 1 : 0;
    }
    EXPECT_GE(named, 38);
}

TEST(NetCoverage, a_join_refusal_and_a_slot_status_decode_exactly_when_named) {
    for (int value = 0; value < 256; ++value) {
        Reject reject;
        reject.reason = static_cast<RejectReason>(value);
        Reject rejectOut;
        EXPECT_EQ(std::string(rejectReasonName(reject.reason)) != "unknown", decode(encode(reject), rejectOut)) << "reject reason " << value;
        SlotAssign slot;
        slot.status = static_cast<SlotStatus>(value);
        slot.creatureId = 5;
        SlotAssign slotOut;
        EXPECT_EQ(std::string(slotStatusName(slot.status)) != "unknown", decode(encode(slot), slotOut)) << "slot status " << value;
    }
}

TEST(NetCoverage, notice_offer_event_and_ready_values_decode_only_in_their_ranges) {
    for (int value = 0; value < 256; ++value) {
        Notice notice {static_cast<NoticeKind>(value), "x"};
        Notice noticeOut;
        EXPECT_EQ(value <= 5, decode(encode(notice), noticeOut)) << "notice kind " << value;

        Offer offer {static_cast<OfferKind>(value), 8, "Merchant", 0};
        Offer offerOut;
        EXPECT_EQ(value >= 1 && value <= 3, decode(encode(offer), offerOut)) << "offer kind " << value;

        EventBatch batch;
        batch.tick = 1;
        NetEvent event;
        event.kind = static_cast<EventKind>(value);
        event.handle = 1;
        batch.events.push_back(event);
        EventBatch batchOut;
        EXPECT_EQ(value >= 1 && value <= 3, decode(encode(batch), batchOut)) << "event kind " << value;

        ReadyCheck check;
        check.id = 1;
        check.state = static_cast<ReadyState>(value);
        ReadyCheck checkOut;
        EXPECT_EQ(value <= 3, decode(encode(check), checkOut)) << "ready state " << value;
        check.state = ReadyState::Active;
        check.players.push_back({"Guest", static_cast<ReadyAnswer>(value), false});
        EXPECT_EQ(value <= 2, decode(encode(check), checkOut)) << "ready answer " << value;
    }
}

TEST(NetRoundTrip, reputation_table_at_its_limits) {
    ReputationTable empty;
    expectRoundTrip(empty, "empty reputation table");

    ReputationTable table;
    table.values.assign(kMaxFactions, std::vector<uint8_t>(kMaxFactions, 100));
    table.values[0][1] = 0;
    table.values[1][0] = 0;
    expectRoundTrip(table, "largest reputation table");
    ReputationTable back;
    ASSERT_TRUE(decode(encode(table), back));
    EXPECT_EQ(table.values, back.values);

    // Values past 100, more factions than the limit: never from a host.
    auto bytes = encode(table);
    bytes[2] = 101;
    EXPECT_FALSE(decode(bytes, back));
    std::vector<uint8_t> oversized {static_cast<uint8_t>(MessageType::ReputationTable), static_cast<uint8_t>(kMaxFactions + 1)};
    oversized.resize(2 + (kMaxFactions + 1) * (kMaxFactions + 1), 50);
    EXPECT_FALSE(decode(oversized, back));
}

TEST(NetCoverage, a_message_type_byte_is_a_message_exactly_when_the_protocol_names_it) {
    // Every named type is a host message, a client message or both, and nothing unnamed is either.
    int named = 0;
    for (int value = 0; value < 256; ++value) {
        std::vector<uint8_t> bytes {static_cast<uint8_t>(value)};
        auto type = peekType(bytes);
        if (!type) {
            continue;
        }
        ++named;
        EXPECT_NE(std::string("unknown"), messageTypeName(*type)) << "type " << value;
        EXPECT_TRUE(isHostMessage(*type) || isClientMessage(*type)) << messageTypeName(*type) << " goes nowhere";
    }
    EXPECT_GE(named, 30);
    EXPECT_TRUE(isHostMessage(MessageType::ReputationTable));
    EXPECT_FALSE(isClientMessage(MessageType::ReputationTable));
}

TEST(NetCoverage, protocol_9_carries_factions_and_stealth_without_colliding) {
    // Factions and Stealth Mode were each protocol 8 on their own branches.
    EXPECT_GE(kProtocolVersion, 9);
    EXPECT_EQ(1 << 11, static_cast<int>(kFieldFaction));
    EXPECT_EQ((1 << 15) - 1, static_cast<int>(kFieldAllMask)); // protocol 11 added kFieldLook, 13 kFieldProgress, the queue bit 14
    EXPECT_EQ(120, static_cast<int>(MessageType::ReputationTable));
    EXPECT_EQ(1 << 6, static_cast<int>(kFlagStealthed));
    EXPECT_EQ(33, static_cast<int>(CommandType::Stealth));
    EXPECT_EQ(40, static_cast<int>(CommandReject::StealthUnavailable));
    EXPECT_EQ(41, static_cast<int>(CommandReject::StealthInCombat));

    // A stealthed creature with a faction: both survive one snapshot.
    Snapshot s;
    s.tick = 5;
    s.worldFields = 0;
    EntityRecord e;
    e.fields = kFieldFlags | kFieldFaction;
    e.state.id = 7;
    e.state.flags = kFlagVisible | kFlagStealthed;
    e.state.faction = 3;
    s.entities.push_back(e);
    Snapshot back;
    ASSERT_TRUE(decode(encode(s), back));
    ASSERT_EQ(1u, back.entities.size());
    EXPECT_EQ(e.state.flags, back.entities[0].state.flags);
    EXPECT_EQ(3, back.entities[0].state.faction);
}

TEST(NetCoverage, auto_combat_flag_and_the_bench_refusal_round_trip) {
    EXPECT_GE(kProtocolVersion, 11);
    EXPECT_EQ(1 << 7, static_cast<int>(kFlagAutoCombat));
    EXPECT_EQ(42, static_cast<int>(CommandReject::ItemEquippedByPlayer));
    EXPECT_STREQ("item_equipped_by_player", commandRejectName(CommandReject::ItemEquippedByPlayer));

    // Auto-combat on a stealthed creature: every flag bit survives.
    Snapshot s;
    s.tick = 5;
    EntityRecord e;
    e.fields = kFieldFlags;
    e.state.id = 7;
    e.state.flags = kFlagVisible | kFlagStealthed | kFlagAutoCombat;
    s.entities.push_back(e);
    Snapshot back;
    ASSERT_TRUE(decode(encode(s), back));
    ASSERT_EQ(1u, back.entities.size());
    EXPECT_EQ(e.state.flags, back.entities[0].state.flags);

    CommandResult refused;
    refused.seq = 3;
    refused.reason = CommandReject::ItemEquippedByPlayer;
    CommandResult out;
    ASSERT_TRUE(decode(encode(refused), out));
    EXPECT_EQ(CommandReject::ItemEquippedByPlayer, out.reason);
    refused.reason = static_cast<CommandReject>(43);
    EXPECT_FALSE(decode(encode(refused), out)) << "nothing past the last named refusal";
}

TEST(NetCoverage, a_creatures_look_round_trips) {
    EXPECT_GE(kProtocolVersion, 11);
    EXPECT_EQ(1 << 12, static_cast<int>(kFieldLook));

    Snapshot s;
    s.tick = 5;
    s.worldFields = 0;
    EntityRecord e;
    e.fields = kFieldLook;
    e.state.id = 7;
    e.state.look.playerName = "John";
    e.state.look.colour = 3;
    e.state.look.headRow = 12;
    e.state.look.bodyVariation = 2;
    e.state.look.textureVariation = 4;
    e.state.look.slots = {{3, "g_i_gloves01"}, {10, "g_i_belt01"}};
    s.entities.push_back(e);
    EntityRecord plain;
    plain.fields = kFieldLook;
    plain.state.id = 8;
    plain.state.look.playerName = "Host";
    s.entities.push_back(plain);
    expectRoundTrip(s, "look");

    Snapshot back;
    ASSERT_TRUE(decode(encode(s), back));
    ASSERT_EQ(2u, back.entities.size());
    EXPECT_EQ(e.state.look, back.entities[0].state.look);
    EXPECT_EQ(-1, back.entities[1].state.look.headRow);
    EXPECT_TRUE(back.entities[1].state.look.slots.empty());
}

TEST(NetCoverage, a_look_with_too_many_slots_or_bad_values_is_rejected) {
    auto build = [](const EntityLook &look) {
        Snapshot s;
        s.tick = 1;
        s.worldFields = 0;
        EntityRecord e;
        e.fields = kFieldLook;
        e.state.id = 1;
        e.state.look = look;
        s.entities.push_back(e);
        return encode(s);
    };
    Snapshot out;
    EntityLook ok;
    ok.playerName = "A";
    EXPECT_TRUE(decode(build(ok), out));
    EntityLook badBody = ok;
    badBody.bodyVariation = 26;
    EXPECT_FALSE(decode(build(badBody), out));
    EntityLook badHead = ok;
    badHead.headRow = -2;
    EXPECT_FALSE(decode(build(badHead), out));
    EntityLook many = ok;
    for (size_t i = 0; i < kMaxLookSlots + 4; ++i) {
        many.slots.push_back({static_cast<uint8_t>(i), "x"});
    }
    // The encoder caps the list, so the decoder sees the maximum.
    ASSERT_TRUE(decode(build(many), out));
    EXPECT_EQ(kMaxLookSlots, out.entities[0].state.look.slots.size());
    auto truncated = build(many);
    truncated.resize(truncated.size() - 3);
    EXPECT_FALSE(decode(truncated, out));
}

TEST(NetCoverage, protocol_11_carries_control_scenes_and_presence_without_colliding) {
    // Guest control, co-op scenes and presence were each protocol 10 on their own branches.
    // 12 adds a stand-in template to the template field (design note companion_standins), no new ids.
    // 13 was two branch builds: the host's level-up state (kFieldProgress, design note guest_npc_scripts)
    // and MapState (id 170), the shared area map; 14 carries both (pinned below).
    EXPECT_GE(kProtocolVersion, 14);

    // Entity fields: one bit each, all inside the mask, the mask exactly the fields.
    const uint16_t fields[] = {kFieldIdentity, kFieldPosition, kFieldFacing, kFieldAnim, kFieldVitals, kFieldWeapons, kFieldOpenLock,
                               kFieldDoor, kFieldFlags, kFieldRemoved, kFieldTarget, kFieldFaction, kFieldLook, kFieldProgress, kFieldQueue};
    uint32_t fieldBits = 0;
    for (auto f : fields) {
        EXPECT_TRUE(f != 0 && (f & (f - 1)) == 0) << "field " << f << " is not one bit";
        EXPECT_EQ(0u, fieldBits & f) << "field bit " << f << " taken twice";
        fieldBits |= f;
    }
    EXPECT_EQ(1 << 12, static_cast<int>(kFieldLook));
    EXPECT_EQ(1 << 13, static_cast<int>(kFieldProgress));
    EXPECT_EQ(fieldBits, static_cast<uint32_t>(kFieldAllMask));
    EXPECT_EQ((1 << 15) - 1, static_cast<int>(kFieldAllMask)); // + kFieldQueue (bit 14)

    // Entity flags: all eight bits, stealth 6 and auto-combat 7.
    const uint8_t flags[] = {kFlagVisible, kFlagSelectable, kFlagInCombat, kFlagInConversation, kFlagPartyMember, kFlagPlayer, kFlagStealthed, kFlagAutoCombat};
    uint32_t flagBits = 0;
    for (auto f : flags) {
        EXPECT_EQ(0u, flagBits & f) << "flag bit " << static_cast<int>(f) << " taken twice";
        flagBits |= f;
    }
    EXPECT_EQ(0xffu, flagBits);
    EXPECT_EQ(1 << 6, static_cast<int>(kFlagStealthed));
    EXPECT_EQ(1 << 7, static_cast<int>(kFlagAutoCombat));

    // Messages: 120 factions, 130 steering, nothing in between or after.
    EXPECT_EQ(120, static_cast<int>(MessageType::ReputationTable));
    EXPECT_EQ(130, static_cast<int>(MessageType::Steer));
    for (int value = 121; value <= 140; ++value) {
        if (value == 130) {
            continue;
        }
        EXPECT_FALSE(peekType(std::vector<uint8_t> {static_cast<uint8_t>(value)})) << value;
    }

    // Refusals: 40-42 named and distinct, nothing past 42.
    EXPECT_EQ(40, static_cast<int>(CommandReject::StealthUnavailable));
    EXPECT_EQ(41, static_cast<int>(CommandReject::StealthInCombat));
    EXPECT_EQ(42, static_cast<int>(CommandReject::ItemEquippedByPlayer));
    EXPECT_STREQ("stealth_unavailable", commandRejectName(CommandReject::StealthUnavailable));
    EXPECT_STREQ("stealth_in_combat", commandRejectName(CommandReject::StealthInCombat));
    EXPECT_STREQ("item_equipped_by_player", commandRejectName(CommandReject::ItemEquippedByPlayer));
    CommandResult refused;
    refused.seq = 1;
    refused.reason = static_cast<CommandReject>(43);
    CommandResult result;
    EXPECT_FALSE(decode(encode(refused), result));

    // One guest's stand-in with everything at once: stealthed, on auto-combat,
    // a faction and a look; and a steer naming it.
    Snapshot s;
    s.tick = 9;
    s.worldFields = 0;
    EntityRecord e;
    e.fields = kFieldFlags | kFieldFaction | kFieldLook;
    e.state.id = 617;
    e.state.flags = kFlagVisible | kFlagPlayer | kFlagStealthed | kFlagAutoCombat;
    e.state.faction = 2;
    e.state.look.playerName = "John";
    e.state.look.colour = 1;
    e.state.look.headRow = 5;
    e.state.look.slots = {{10, "g_i_belt01"}};
    s.entities.push_back(e);
    expectRoundTrip(s, "combined");
    Snapshot back;
    ASSERT_TRUE(decode(encode(s), back));
    ASSERT_EQ(1u, back.entities.size());
    EXPECT_EQ(e.state.flags, back.entities[0].state.flags);
    EXPECT_EQ(2, back.entities[0].state.faction);
    EXPECT_EQ(e.state.look, back.entities[0].state.look);

    Steer steer;
    steer.seq = 4;
    steer.actor = 617;
    steer.moving = true;
    steer.facing = 1.0f;
    Steer steerBack;
    ASSERT_TRUE(decode(encode(steer), steerBack));
    EXPECT_EQ(617u, steerBack.actor);
}

TEST(NetRoundTrip, map_state_at_its_limits) {
    MapState empty;
    expectRoundTrip(empty, "empty map state");

    MapState state;
    state.area = kLongest;
    state.columns = 89;
    state.rows = 44;
    state.cells.assign((89 * 44 + 7) / 8, 0xA5);
    state.notes = {"wp_a", "wp_b"};
    expectRoundTrip(state, "map state");
    MapState back;
    ASSERT_TRUE(decode(encode(state), back));
    EXPECT_EQ(state.cells, back.cells);
    EXPECT_EQ(state.notes, back.notes);

    MapState largest;
    largest.area = "a";
    largest.columns = kMaxMapColumns;
    largest.rows = kMaxMapRows;
    largest.cells.assign(kMaxMapColumns * kMaxMapRows / 8, 0xFF);
    for (size_t i = 0; i < kMaxMapNotes; ++i) {
        largest.notes.push_back(kLongest);
    }
    expectRoundTrip(largest, "largest map state");
    EXPECT_LT(encode(largest).size(), kMaxHostMessageBytes);

    // A grid past the limit, or a note list past it: never from a host.
    auto tooWide = state;
    tooWide.columns = kMaxMapColumns + 1;
    EXPECT_FALSE(decode(encode(tooWide), back));
    auto bytes = encode(state);
    bytes.push_back(0);
    EXPECT_FALSE(decode(bytes, back));
    EXPECT_TRUE(isHostMessage(MessageType::MapState));
    EXPECT_FALSE(isClientMessage(MessageType::MapState));
}

TEST(NetCoverage, protocol_14_carries_level_up_state_and_the_shared_map_without_colliding) {
    // Guest NPC scripts (kFieldProgress) and the shared map (MapState) were each
    // protocol 13 on their own branches; the merged build (alpha-0.4.9) is 14.
    EXPECT_GE(kProtocolVersion, 14);

    // Entity fields: bit 13 is the level-up state; bit 14 (the combat queue,
    // fix/combat-queue, waits for the integrator's version bump) ends the mask.
    EXPECT_EQ(1 << 13, static_cast<int>(kFieldProgress));
    EXPECT_EQ(0, static_cast<int>(kFieldProgress & (kFieldLook | kFieldFaction | kFieldTarget | kFieldRemoved)));
    EXPECT_EQ(1 << 14, static_cast<int>(kFieldQueue));
    EXPECT_EQ((1 << 15) - 1, static_cast<int>(kFieldAllMask));
    EXPECT_EQ(1, static_cast<int>(kProgressLevelUp));

    // Messages: 170 is the map, host to client only; 130 steering is unchanged,
    // and nothing else between 131 and 200 decodes as a type.
    EXPECT_EQ(170, static_cast<int>(MessageType::MapState));
    EXPECT_EQ(130, static_cast<int>(MessageType::Steer));
    EXPECT_TRUE(isHostMessage(MessageType::MapState));
    EXPECT_FALSE(isClientMessage(MessageType::MapState));
    EXPECT_STREQ("map_state", messageTypeName(MessageType::MapState));
    for (int value = 131; value <= 200; ++value) {
        bool named = peekType(std::vector<uint8_t> {static_cast<uint8_t>(value)}).has_value();
        EXPECT_EQ(value == 170, named) << value;
    }

    // Rejects: neither branch added one; anything past 42 still fails to decode.
    CommandResult refused;
    refused.seq = 1;
    refused.reason = static_cast<CommandReject>(43);
    CommandResult result;
    EXPECT_FALSE(decode(encode(refused), result));

    // A creature with a look and a level due, beside a map of the same area.
    Snapshot s;
    s.tick = 11;
    s.worldFields = 0;
    EntityRecord e;
    e.fields = kFieldLook | kFieldProgress;
    e.state.id = 618;
    e.state.look.playerName = "John";
    e.state.look.colour = 2;
    e.state.progress = kProgressLevelUp;
    s.entities.push_back(e);
    expectRoundTrip(s, "combined 14");
    Snapshot back;
    ASSERT_TRUE(decode(encode(s), back));
    ASSERT_EQ(1u, back.entities.size());
    EXPECT_EQ(kProgressLevelUp, back.entities[0].state.progress);
    EXPECT_EQ(e.state.look, back.entities[0].state.look);

    MapState map;
    map.area = "end_m01aa";
    map.columns = 8;
    map.rows = 2;
    map.cells = {0x0F, 0xF0};
    map.notes = {"wp_end_map01"};
    MapState mapBack;
    ASSERT_TRUE(decode(encode(map), mapBack));
    EXPECT_EQ(map.cells, mapBack.cells);
    EXPECT_EQ(map.notes, mapBack.notes);
}
