/* Copyright (c) 2026 The reone project contributors */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "reone/net/bytes.h"
#include "reone/net/fingerprint.h"
#include "reone/net/joincode.h"
#include "reone/net/protocol.h"
#include "reone/net/transport.h"

using namespace reone::net;

namespace {

EntityState sampleEntity() {
    EntityState s;
    s.id = 42;
    s.type = 5;
    s.key = "5:end_sith2:end_sith02:123:456";
    s.tag = "end_sith2";
    s.resref = "end_sith02";
    s.position = {1.5f, -2.25f, 0.125f};
    s.facing = dequantizeFacing(quantizeFacing(1.0f));
    s.anim = "g8a1";
    s.animTime = 0.5f;
    s.animFlags = 3;
    s.animSpeed = 1.25f;
    s.hp = 12;
    s.maxHp = 20;
    s.dead = false;
    s.rightWeapon = "g_w_blstrpstl001";
    s.leftWeapon = "";
    s.armor = "g_a_clothes01";
    s.headgear = "g_i_mask01";
    s.open = true;
    s.locked = false;
    s.doorState = 1;
    s.doorTransition = 0;
    s.flags = kFlagVisible | kFlagSelectable;
    s.attackTarget = 7;
    s.faction = 1; // Hostile_1
    s.progress = kProgressLevelUp;
    s.queue = {{kQueueAttack, 0}, {kQueueFeat, 28}, {kQueueSpell, 70}};
    return s;
}

Snapshot sampleSnapshot() {
    Snapshot snapshot;
    snapshot.tick = 1234;
    snapshot.generation = 2;
    snapshot.baseTick = 0; // full
    snapshot.worldFields = kWorldAllMask;
    snapshot.world.screen = 4;
    snapshot.world.fadeOpacity = 0.5f;
    snapshot.world.fadeColor = {0.0f, 0.0f, 0.0f};
    snapshot.world.movie = false;
    snapshot.world.cameraType = 1;
    snapshot.world.conversation.active = true;
    snapshot.world.conversation.dialog = "end_trask01";
    snapshot.world.conversation.entry = 3;
    snapshot.world.conversation.owner = 9;
    snapshot.world.conversation.speaker = "end_trask";
    snapshot.world.conversation.replies = {90, 92};
    snapshot.world.conversation.entryEnded = true;
    snapshot.world.conversation.placeableCamera = 22;
    snapshot.world.party = {{-1, 3}, {0, 9}};
    snapshot.world.leader = 3;
    snapshot.world.movieName = "01a";
    snapshot.world.paused = true;
    snapshot.world.pausedBy = "Guest";
    EntityRecord full;
    full.fields = static_cast<uint16_t>(kFieldAllMask & ~kFieldRemoved);
    full.state = sampleEntity();
    snapshot.entities.push_back(full);
    EntityRecord moved;
    moved.fields = kFieldPosition;
    moved.state.id = 43;
    moved.state.position = {10.0f, 20.0f, 0.0f};
    snapshot.entities.push_back(moved);
    EntityRecord removed;
    removed.fields = kFieldRemoved;
    removed.state.id = 44;
    snapshot.entities.push_back(removed);
    return snapshot;
}

EventBatch sampleEvents() {
    EventBatch snapshot;
    snapshot.generation = 2;
    snapshot.tick = 1234;
    NetEvent vfx;
    vfx.kind = EventKind::VfxBegin;
    vfx.handle = 1;
    vfx.row = 1007;
    vfx.a = 42;
    vfx.location = {1.0f, 2.0f, 3.0f};
    vfx.facing = 0.5f;
    snapshot.events.push_back(vfx);
    NetEvent end;
    end.kind = EventKind::VfxEnd;
    end.handle = 1;
    snapshot.events.push_back(end);
    NetEvent shot;
    shot.kind = EventKind::Projectile;
    shot.a = 42;
    shot.b = 3;
    shot.flags = 2;
    snapshot.events.push_back(shot);
    return snapshot;
}

template <class T>
void expectEveryTruncationRejected(const std::vector<uint8_t> &bytes) {
    for (size_t length = 0; length < bytes.size(); ++length) {
        std::vector<uint8_t> prefix(bytes.begin(), bytes.begin() + length);
        T out;
        EXPECT_FALSE(decode(prefix, out)) << "a " << length << "-byte prefix of " << bytes.size() << " decoded";
    }
    auto longer = bytes;
    longer.push_back(0);
    T out;
    EXPECT_FALSE(decode(longer, out)) << "a trailing byte was accepted";
}

} // namespace

TEST(NetBytes, round_trips_every_width_little_endian) {
    ByteWriter w;
    w.u8(0xab);
    w.u16(0x1234);
    w.u32(0xdeadbeef);
    w.u64(0x0102030405060708ull);
    w.i16(-2);
    w.i32(-3);
    w.f32(1.5f);
    w.boolean(true);
    w.str("tag");
    EXPECT_EQ(0x34, w.bytes()[1]); // little-endian
    ByteReader r(w.bytes());
    EXPECT_EQ(0xab, r.u8());
    EXPECT_EQ(0x1234, r.u16());
    EXPECT_EQ(0xdeadbeefu, r.u32());
    EXPECT_EQ(0x0102030405060708ull, r.u64());
    EXPECT_EQ(-2, r.i16());
    EXPECT_EQ(-3, r.i32());
    EXPECT_EQ(1.5f, r.f32());
    EXPECT_TRUE(r.boolean());
    EXPECT_EQ("tag", r.str());
    EXPECT_TRUE(r.ok());
    EXPECT_TRUE(r.atEnd());
}

TEST(NetBytes, reading_past_the_end_fails_and_stays_failed) {
    ByteWriter w;
    w.u16(7);
    ByteReader r(w.bytes());
    EXPECT_EQ(0u, r.u32());
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(0, r.u8());
    EXPECT_FALSE(r.ok());
}

TEST(NetBytes, rejects_long_strings_non_finite_floats_and_bad_booleans) {
    ByteWriter w;
    w.str(std::string(300, 'x'));
    ByteReader longString(w.bytes(), 256);
    longString.str();
    EXPECT_FALSE(longString.ok());

    ByteWriter nan;
    nan.f32(std::numeric_limits<float>::quiet_NaN());
    ByteReader nanReader(nan.bytes());
    nanReader.f32();
    EXPECT_FALSE(nanReader.ok());

    ByteWriter inf;
    inf.f32(std::numeric_limits<float>::infinity());
    ByteReader infReader(inf.bytes());
    infReader.f32();
    EXPECT_FALSE(infReader.ok());

    std::vector<uint8_t> two {2};
    ByteReader boolReader(two);
    boolReader.boolean();
    EXPECT_FALSE(boolReader.ok());
}

TEST(NetBytes, fnv1a_matches_the_reference_vectors) {
    Fnv1a64 empty;
    EXPECT_EQ(14695981039346656037ull, empty.value());
    Fnv1a64 a;
    a.update("a");
    EXPECT_EQ(0xaf63dc4c8601ec8cull, a.value());
}

TEST(NetProtocol, hello_round_trips) {
    Hello hello;
    hello.build = "reone-coop/p1/0123456789abcdef";
    hello.mode = "story";
    hello.playerName = "Revan";
    hello.k1 = {true, 1, 2, 3};
    hello.k2 = {false, 0, 0, 0};
    auto bytes = encode(hello);
    Hello out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(kProtocolVersion, out.protocol);
    EXPECT_EQ(hello.build, out.build);
    EXPECT_EQ("story", out.mode);
    EXPECT_EQ("Revan", out.playerName);
    EXPECT_EQ(hello.k1, out.k1);
    EXPECT_EQ(hello.k2, out.k2);
    expectEveryTruncationRejected<Hello>(bytes);
}

TEST(NetProtocol, handshake_replies_round_trip) {
    Welcome welcome {7, 1000, 30, "story", 0x1122334455667788ull};
    Welcome welcomeOut;
    ASSERT_TRUE(decode(encode(welcome), welcomeOut));
    EXPECT_EQ(0x1122334455667788ull, welcomeOut.rejoinToken);
    EXPECT_EQ(7u, welcomeOut.clientId);
    EXPECT_EQ(1000u, welcomeOut.hostTick);
    EXPECT_EQ(30, welcomeOut.tickRate);
    expectEveryTruncationRejected<Welcome>(encode(welcome));

    Reject reject {RejectReason::FilesMismatchK1, "K1 install differs: tlk"};
    Reject rejectOut;
    ASSERT_TRUE(decode(encode(reject), rejectOut));
    EXPECT_EQ(RejectReason::FilesMismatchK1, rejectOut.reason);
    EXPECT_EQ(reject.detail, rejectOut.detail);

    SlotRequest request {"end_trask"};
    SlotRequest requestOut;
    ASSERT_TRUE(decode(encode(request), requestOut));
    EXPECT_EQ("end_trask", requestOut.preferredTag);

    SlotAssign slot {SlotStatus::Assigned, 0, 9, "end_trask"};
    SlotAssign slotOut;
    ASSERT_TRUE(decode(encode(slot), slotOut));
    EXPECT_EQ(SlotStatus::Assigned, slotOut.status);
    EXPECT_EQ(9u, slotOut.creatureId);
    EXPECT_EQ("end_trask", slotOut.tag);
    expectEveryTruncationRejected<SlotAssign>(encode(slot));
}

TEST(NetProtocol, handshake_decoders_reject_invalid_values) {
    Reject reject {RejectReason::Declined, ""};
    auto bytes = encode(reject);
    bytes[1] = 0;
    Reject rejectOut;
    EXPECT_FALSE(decode(bytes, rejectOut));
    bytes[1] = 200;
    EXPECT_FALSE(decode(bytes, rejectOut));

    // An assignment must name a creature.
    SlotAssign slot {SlotStatus::Assigned, 0, 0, "end_trask"};
    SlotAssign slotOut;
    EXPECT_FALSE(decode(encode(slot), slotOut));

    Welcome welcome {1, 0, 0, "story"};
    Welcome welcomeOut;
    EXPECT_FALSE(decode(encode(welcome), welcomeOut));

    // The wrong message type in the first byte.
    Hello hello;
    auto helloBytes = encode(hello);
    Welcome wrong;
    EXPECT_FALSE(decode(helloBytes, wrong));
}

TEST(NetProtocol, module_info_accepts_only_resref_names) {
    ModuleInfo info;
    info.generation = 3;
    info.module = "end_m01aa";
    info.player = {5, 91, 0, "Harness"};
    ModuleInfo out;
    ASSERT_TRUE(decode(encode(info), out));
    EXPECT_EQ(3u, out.generation);
    EXPECT_EQ("end_m01aa", out.module);
    EXPECT_EQ(5u, out.player.hostId);
    EXPECT_EQ(91, out.player.appearance);
    EXPECT_EQ("Harness", out.player.name);
    expectEveryTruncationRejected<ModuleInfo>(encode(info));

    for (auto bad : {"", "../../boot", "end m01aa", "a_very_long_module_name", "c:\\x"}) {
        info.module = bad;
        EXPECT_FALSE(decode(encode(info), out)) << bad;
    }
    info.module = "end_m01aa";
    info.player.appearance = -1;
    EXPECT_FALSE(decode(encode(info), out));
}

TEST(NetProtocol, snapshot_round_trips_every_field) {
    auto snapshot = sampleSnapshot();
    auto bytes = encode(snapshot);
    Snapshot out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(1234u, out.tick);
    EXPECT_EQ(2u, out.generation);
    EXPECT_TRUE(out.full());
    EXPECT_EQ(kWorldAllMask, out.worldFields);
    EXPECT_EQ(4, out.world.screen);
    EXPECT_EQ(0.5f, out.world.fadeOpacity);
    EXPECT_EQ(snapshot.world.conversation, out.world.conversation);
    EXPECT_EQ(snapshot.world.party, out.world.party);
    EXPECT_EQ(3u, out.world.leader);
    EXPECT_EQ("01a", out.world.movieName);
    EXPECT_TRUE(out.world.paused);
    EXPECT_EQ("Guest", out.world.pausedBy);

    ASSERT_EQ(3u, out.entities.size());
    const auto &a = out.entities[0].state;
    auto b = sampleEntity();
    EXPECT_EQ(0, diffEntity(b, a));
    EXPECT_EQ(b.key, a.key);
    EXPECT_EQ(b.animTime, a.animTime);
    EXPECT_EQ(b.animSpeed, a.animSpeed);
    EXPECT_EQ(b.armor, a.armor);
    EXPECT_EQ(b.headgear, a.headgear);
    EXPECT_EQ(b.faction, a.faction);
    EXPECT_EQ(b.progress, a.progress);
    EXPECT_EQ(b.queue, a.queue);
    EXPECT_EQ(kFieldPosition, out.entities[1].fields);
    EXPECT_EQ(10.0f, out.entities[1].state.position.x);
    EXPECT_EQ(kFieldRemoved, out.entities[2].fields);
    EXPECT_EQ(44u, out.entities[2].state.id);

    // N3: a delta names its base, an earlier tick of the same visit.
    snapshot.baseTick = 1200;
    ASSERT_TRUE(decode(encode(snapshot), out));
    EXPECT_EQ(1200u, out.baseTick);
    EXPECT_FALSE(out.full());
    snapshot.baseTick = 1234;
    EXPECT_FALSE(decode(encode(snapshot), out)) << "a base must be older than the snapshot";
    snapshot.baseTick = 0;
    snapshot.tick = 0;
    EXPECT_FALSE(decode(encode(snapshot), out));
}

TEST(NetProtocol, events_travel_on_their_own_and_are_checked) {
    auto batch = sampleEvents();
    auto bytes = encode(batch);
    EventBatch out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(2u, out.generation);
    EXPECT_EQ(1234u, out.tick);
    ASSERT_EQ(3u, out.events.size());
    EXPECT_EQ(EventKind::VfxBegin, out.events[0].kind);
    EXPECT_EQ(1007u, out.events[0].row);
    EXPECT_EQ(42u, out.events[0].a);
    EXPECT_EQ(EventKind::VfxEnd, out.events[1].kind);
    EXPECT_EQ(EventKind::Projectile, out.events[2].kind);
    EXPECT_EQ(2, out.events[2].flags);
    expectEveryTruncationRejected<EventBatch>(bytes);
    batch.events[2].flags = 7;
    EXPECT_FALSE(decode(encode(batch), out));
}

TEST(NetProtocol, a_vfx_begin_carries_its_beam_source_and_body_node) {
    // Design note v14_programmed_effects, section 6: guests draw beams.
    EventBatch batch;
    batch.generation = 1;
    batch.tick = 5;
    NetEvent beam;
    beam.kind = EventKind::VfxBegin;
    beam.handle = 9;
    beam.row = 2038;
    beam.a = 42;
    beam.b = 77;
    beam.flags = 4; // BodyNode::HandRight
    batch.events.push_back(beam);
    auto bytes = encode(batch);
    EventBatch out;
    ASSERT_TRUE(decode(bytes, out));
    ASSERT_EQ(1u, out.events.size());
    EXPECT_EQ(2038u, out.events[0].row);
    EXPECT_EQ(42u, out.events[0].a);
    EXPECT_EQ(77u, out.events[0].b);
    EXPECT_EQ(4, out.events[0].flags);
    expectEveryTruncationRejected<EventBatch>(bytes);
    batch.events[0].flags = kMaxVfxBodyNode + 1;
    EXPECT_FALSE(decode(encode(batch), out)) << "no such body node";
    EXPECT_GE(kProtocolVersion, 4) << "older peers would misread the event";
}

TEST(NetProtocol, snapshot_acks_are_small_and_checked) {
    SnapshotAck ack {3, 99};
    auto bytes = encode(ack);
    EXPECT_EQ(9u, bytes.size());
    SnapshotAck out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(3u, out.generation);
    EXPECT_EQ(99u, out.tick);
    expectEveryTruncationRejected<SnapshotAck>(bytes);
    SnapshotAck zero {3, 0};
    EXPECT_FALSE(decode(encode(zero), out));
    EXPECT_TRUE(isClientMessage(MessageType::SnapshotAck));
    EXPECT_TRUE(isHostMessage(MessageType::Events));
}

TEST(NetProtocol, a_combat_queue_change_is_its_own_field) {
    auto a = sampleEntity();
    auto b = a;
    b.queue.push_back({kQueueFeat, 29});
    EXPECT_EQ(kFieldQueue, exactDiffEntity(a, b));
    EXPECT_EQ(kFieldQueue, diffEntity(a, b));
    auto merged = a;
    mergeEntity(merged, b, kFieldQueue);
    EXPECT_EQ(b.queue, merged.queue);
    b.queue.clear(); // the fight ended
    EXPECT_EQ(kFieldQueue, exactDiffEntity(a, b));
    EXPECT_LE(kFieldQueue, kFieldAllMask) << "the field is inside the mask the reader accepts";
}

TEST(NetProtocol, exact_diffs_see_every_change_and_restarts) {
    auto a = sampleEntity();
    auto b = a;
    EXPECT_EQ(0, exactDiffEntity(a, b));
    b.position.x += 0.0001f; // below the replication tolerance, but a change
    EXPECT_EQ(kFieldPosition, exactDiffEntity(a, b));
    EXPECT_EQ(0, diffEntity(a, b));
    b = a;
    b.animSerial = a.animSerial + 1; // the same clip started again
    EXPECT_EQ(kFieldAnim, exactDiffEntity(a, b));
    b = a;
    b.animSentTick = 77; // host bookkeeping only
    EXPECT_EQ(0, exactDiffEntity(a, b));

    WorldState w;
    WorldState v = w;
    EXPECT_EQ(0, exactDiffWorld(w, v));
    v.fadeOpacity = 0.001f;
    v.pausedBy = "Guest";
    EXPECT_EQ(kWorldFade | kWorldPause, exactDiffWorld(w, v));
    mergeWorld(w, v, kWorldPause);
    EXPECT_EQ("Guest", w.pausedBy);
    EXPECT_EQ(0.0f, w.fadeOpacity);
}

TEST(NetJoinCode, codes_ignore_case_and_one_dash_and_refuse_look_alikes) {
    EXPECT_EQ("K7QM4TXR", normalizeJoinCode("k7qm-4txr"));
    EXPECT_EQ("K7QM4TXR", normalizeJoinCode(" K7QM4TXR  "));
    EXPECT_EQ("K7QM-4TXR", displayJoinCode("K7QM4TXR"));
    EXPECT_EQ("", normalizeJoinCode("K7QM-4TX"));   // seven characters
    EXPECT_EQ("", normalizeJoinCode("K7QM-4TXO"));  // O is not in the alphabet
    EXPECT_EQ("", normalizeJoinCode("K7QM-4TX1"));
    EXPECT_EQ("", normalizeJoinCode("K7Q-M4-TXR")); // two dashes
    EXPECT_EQ("", normalizeJoinCode("127.0.0.1:27015"));
    for (char c : std::string("01OIL")) {
        EXPECT_EQ(nullptr, std::strchr(kJoinCodeAlphabet, c)) << c;
    }
    EXPECT_TRUE(looksLikeAddress("127.0.0.1:27015"));
    EXPECT_TRUE(looksLikeAddress("host.example:1"));
    EXPECT_FALSE(looksLikeAddress("K7QM-4TXR"));
    EXPECT_FALSE(looksLikeAddress("host:"));
}

TEST(NetJoinCode, default_port_for_bare_hosts) {
    EXPECT_EQ("1.2.3.4:27015", withDefaultPort("1.2.3.4", 27015));
    EXPECT_EQ("host.example:27015", withDefaultPort("host.example", 27015));
    EXPECT_EQ("localhost:27015", withDefaultPort("localhost", 27015));
    EXPECT_EQ("1.2.3.4:5000", withDefaultPort("1.2.3.4:5000", 27015));
    EXPECT_EQ("K7QM-4TXR", withDefaultPort("K7QM-4TXR", 27015));
    EXPECT_EQ("k7qm4txr", withDefaultPort("k7qm4txr", 27015));
    EXPECT_EQ("", withDefaultPort("", 27015));
}

TEST(NetJoinCode, service_addresses_and_base64) {
    std::string host;
    uint16_t port = 0;
    ASSERT_TRUE(parseServiceAddress("coop.example.org", host, port));
    EXPECT_EQ("coop.example.org", host);
    EXPECT_EQ(kDefaultServicePort, port);
    ASSERT_TRUE(parseServiceAddress("kc://10.0.0.5:27300/", host, port));
    EXPECT_EQ("10.0.0.5", host);
    EXPECT_EQ(27300, port);
    ASSERT_TRUE(parseServiceAddress("tcp://[::1]:9", host, port));
    EXPECT_EQ("::1", host);
    EXPECT_FALSE(parseServiceAddress("", host, port));
    EXPECT_FALSE(parseServiceAddress("host:0", host, port));
    EXPECT_FALSE(parseServiceAddress("host:99999", host, port));
    EXPECT_FALSE(parseServiceAddress("bad host", host, port));

    const uint8_t data[] {0, 1, 2, 250, 251, 252, 253};
    for (size_t n = 0; n <= sizeof(data); ++n) {
        auto text = base64Encode(data, n);
        std::vector<uint8_t> back;
        ASSERT_TRUE(base64Decode(text, back)) << text;
        EXPECT_EQ(std::vector<uint8_t>(data, data + n), back);
    }
    EXPECT_EQ("aGk=", base64Encode(reinterpret_cast<const uint8_t *>("hi"), 2));
    std::vector<uint8_t> out;
    EXPECT_FALSE(base64Decode("aGk", out));
    EXPECT_FALSE(base64Decode("a=Gk", out));
    EXPECT_FALSE(base64Decode("aG!k", out));
    EXPECT_FALSE(base64Decode("aGk=aGk=", out));
}

TEST(NetProtocol, snapshot_rejects_every_truncation_and_trailing_byte) {
    expectEveryTruncationRejected<Snapshot>(encode(sampleSnapshot()));
}

TEST(NetProtocol, snapshot_rejects_invalid_fields) {
    auto snapshot = sampleSnapshot();
    Snapshot out;

    auto zeroId = snapshot;
    zeroId.entities[1].state.id = 0;
    EXPECT_FALSE(decode(encode(zeroId), out));

    auto unknownField = snapshot;
    unknownField.entities[1].fields = 0x8000;
    EXPECT_FALSE(decode(encode(unknownField), out));

    auto badDoor = snapshot;
    badDoor.entities[0].state.doorState = 9;
    EXPECT_FALSE(decode(encode(badDoor), out));

    auto badFaction = snapshot;
    badFaction.entities[0].state.faction = static_cast<uint8_t>(kMaxFactions);
    EXPECT_FALSE(decode(encode(badFaction), out));
    auto noFaction = snapshot;
    noFaction.entities[0].state.faction = kNoFaction;
    EXPECT_TRUE(decode(encode(noFaction), out));
    EXPECT_EQ(kNoFaction, out.entities[0].state.faction);

    auto badFade = snapshot;
    badFade.world.fadeOpacity = 2.0f;
    EXPECT_FALSE(decode(encode(badFade), out));

    auto nanPosition = snapshot;
    nanPosition.entities[1].state.position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(decode(encode(nanPosition), out));

    auto tooManyReplies = snapshot;
    tooManyReplies.world.conversation.replies.assign(kMaxReplies + 1, 1);
    EXPECT_FALSE(decode(encode(tooManyReplies), out));
}

TEST(NetProtocol, command_round_trips_and_is_bounded) {
    Command command;
    command.seq = 17;
    command.type = CommandType::UseItem;
    command.actor = 9;
    command.target = 42;
    command.point = {1.0f, 2.0f, 3.0f};
    command.run = false;
    command.param = 5;
    command.item = "g_i_medeqpmnt01";
    auto bytes = encode(command);
    Command out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(17u, out.seq);
    EXPECT_EQ(CommandType::UseItem, out.type);
    EXPECT_EQ(9u, out.actor);
    EXPECT_EQ(42u, out.target);
    EXPECT_EQ(command.point, out.point);
    EXPECT_FALSE(out.run);
    EXPECT_EQ(5, out.param);
    EXPECT_EQ("g_i_medeqpmnt01", out.item);
    expectEveryTruncationRejected<Command>(bytes);

    for (uint8_t type : {0, 40, 255}) {
        auto bad = bytes;
        bad[5] = type; // type byte follows the u32 sequence number
        EXPECT_FALSE(decode(bad, out)) << int(type);
    }
    command.point.x = 1e9f;
    EXPECT_FALSE(decode(encode(command), out));
    command.point.x = 1.0f;
    command.item = std::string(100, 'i');
    EXPECT_FALSE(decode(encode(command), out));

    std::vector<uint8_t> oversized(kMaxClientMessageBytes + 1, 0);
    oversized[0] = static_cast<uint8_t>(MessageType::Command);
    EXPECT_FALSE(decode(oversized, out));
}

TEST(NetProtocol, command_result_must_be_consistent) {
    CommandResult ok {3, true, CommandReject::None, 100};
    CommandResult out;
    ASSERT_TRUE(decode(encode(ok), out));
    EXPECT_TRUE(out.accepted);
    CommandResult rejected {4, false, CommandReject::TargetTooFar, 101};
    ASSERT_TRUE(decode(encode(rejected), out));
    EXPECT_EQ(CommandReject::TargetTooFar, out.reason);
    CommandResult contradictory {5, true, CommandReject::Busy, 0};
    EXPECT_FALSE(decode(encode(contradictory), out));
    CommandResult unknownReason {6, false, static_cast<CommandReject>(99), 0};
    EXPECT_FALSE(decode(encode(unknownReason), out));
}

TEST(NetProtocol, message_directions_partition_the_types) {
    for (uint8_t value = 1; value <= 15; ++value) {
        auto type = static_cast<MessageType>(value);
        if (type == MessageType::Bye) {
            EXPECT_TRUE(isClientMessage(type) && isHostMessage(type));
            continue;
        }
        EXPECT_NE(isClientMessage(type), isHostMessage(type)) << messageTypeName(type);
    }
    EXPECT_FALSE(peekType({}).has_value());
    EXPECT_FALSE(peekType({0}).has_value());
    EXPECT_FALSE(peekType({16}).has_value());
    EXPECT_EQ(MessageType::Snapshot, *peekType({7}));
}

TEST(NetProtocol, n2_session_commands_round_trip) {
    for (auto type : {CommandType::Ready, CommandType::Pause, CommandType::TakeItem, CommandType::CloseContainer,
                      CommandType::PickSlot}) {
        Command command;
        command.seq = 1;
        command.type = type;
        command.target = 77;
        command.param = 1;
        command.item = "end_trask";
        Command out;
        ASSERT_TRUE(decode(encode(command), out)) << commandTypeName(type);
        EXPECT_EQ(type, out.type);
        EXPECT_EQ(77u, out.target);
    }
    CommandResult refused {9, false, CommandReject::HostLeadsConversation, 5};
    CommandResult out;
    ASSERT_TRUE(decode(encode(refused), out));
    EXPECT_STREQ("host_leads_conversation", commandRejectName(out.reason));
    CommandResult last {9, false, CommandReject::ContainerNotOpen, 5};
    EXPECT_TRUE(decode(encode(last), out));
}

TEST(NetProtocol, notice_round_trips_and_rejects_control_characters) {
    Notice notice {NoticeKind::Refusal, "The host leads conversations"};
    auto bytes = encode(notice);
    Notice out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(NoticeKind::Refusal, out.kind);
    EXPECT_EQ(notice.text, out.text);
    expectEveryTruncationRejected<Notice>(bytes);
    notice.text = "bad\nline";
    EXPECT_FALSE(decode(encode(notice), out));
    bytes[1] = 9;
    EXPECT_FALSE(decode(bytes, out));
}

TEST(NetProtocol, container_view_round_trips_and_is_bounded) {
    ContainerView view;
    view.container = 55;
    view.items.push_back({101, "g_w_blstrpstl001", "G_W_BLSTRPSTL001", 1});
    view.items.push_back({102, "g_i_medeqpmnt01", "G_I_MEDEQPMNT01", 3});
    auto bytes = encode(view);
    ContainerView out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(55u, out.container);
    EXPECT_TRUE(out.open);
    ASSERT_EQ(2u, out.items.size());
    EXPECT_EQ(102u, out.items[1].id);
    EXPECT_EQ(3, out.items[1].stack);
    expectEveryTruncationRejected<ContainerView>(bytes);

    auto badRef = view;
    badRef.items[0].resref = "../x";
    EXPECT_FALSE(decode(encode(badRef), out));
    auto zeroStack = view;
    zeroStack.items[0].stack = 0;
    EXPECT_FALSE(decode(encode(zeroStack), out));
    auto zeroId = view;
    zeroId.items[1].id = 0;
    EXPECT_FALSE(decode(encode(zeroId), out));
    auto tooMany = view;
    tooMany.items.assign(kMaxContainerItems + 1, view.items[0]);
    EXPECT_FALSE(decode(encode(tooMany), out));
    ContainerView closed;
    closed.container = 55;
    closed.open = false;
    ASSERT_TRUE(decode(encode(closed), out));
    EXPECT_FALSE(out.open);
}

TEST(NetProtocol, ready_check_round_trips_and_is_bounded) {
    ReadyCheck check;
    check.id = 3;
    check.state = ReadyState::Active;
    check.initiator = "Host";
    check.destination = "end_m01ab";
    check.secondsLeft = 27;
    check.players.push_back({"Host", ReadyAnswer::Ready, false});
    check.players.push_back({"Guest", ReadyAnswer::Pending, true});
    auto bytes = encode(check);
    ReadyCheck out;
    ASSERT_TRUE(decode(bytes, out));
    EXPECT_EQ(ReadyState::Active, out.state);
    EXPECT_EQ("end_m01ab", out.destination);
    EXPECT_EQ(27, out.secondsLeft);
    ASSERT_EQ(2u, out.players.size());
    EXPECT_EQ(ReadyAnswer::Pending, out.players[1].answer);
    EXPECT_TRUE(out.players[1].busy);
    expectEveryTruncationRejected<ReadyCheck>(bytes);
    auto badState = check;
    badState.state = static_cast<ReadyState>(7);
    EXPECT_FALSE(decode(encode(badState), out));
    auto badAnswer = check;
    badAnswer.players[0].answer = static_cast<ReadyAnswer>(5);
    EXPECT_FALSE(decode(encode(badAnswer), out));
    auto crowd = check;
    crowd.players.assign(kMaxReadyPlayers + 1, check.players[0]);
    EXPECT_FALSE(decode(encode(crowd), out));
}

TEST(NetProtocol, facing_quantizes_within_a_hundredth_of_a_degree) {
    for (float f : {0.0f, 0.001f, 1.0f, 3.14159f, 6.2f, -1.0f, 7.0f}) {
        float wrapped = std::fmod(f, 6.28318530718f);
        if (wrapped < 0.0f) {
            wrapped += 6.28318530718f;
        }
        float back = dequantizeFacing(quantizeFacing(f));
        float error = std::fabs(back - wrapped);
        error = std::min(error, 6.28318530718f - error);
        EXPECT_LT(error, 1e-4f) << f;
    }
}

TEST(NetProtocol, diff_names_each_changed_field_and_ignores_noise) {
    auto base = sampleEntity();
    EXPECT_EQ(0, diffEntity(base, base));

    auto moved = base;
    moved.position.x += 0.0005f;
    EXPECT_EQ(0, diffEntity(base, moved)) << "sub-millimetre motion is noise";
    moved.position.x += 0.01f;
    EXPECT_EQ(kFieldPosition, diffEntity(base, moved));

    auto hurt = base;
    hurt.hp = 3;
    EXPECT_EQ(kFieldVitals, diffEntity(base, hurt));

    auto dead = base;
    dead.dead = true;
    EXPECT_EQ(kFieldVitals, diffEntity(base, dead));

    auto swung = base;
    swung.anim = "g8a2";
    EXPECT_EQ(kFieldAnim, diffEntity(base, swung));

    auto rearmed = base;
    rearmed.armor = "g_a_class4001";
    EXPECT_EQ(kFieldWeapons, diffEntity(base, rearmed));

    auto opening = base;
    opening.doorTransition = 1;
    EXPECT_EQ(kFieldDoor, diffEntity(base, opening));

    auto unlocked = base;
    unlocked.locked = true;
    EXPECT_EQ(kFieldOpenLock, diffEntity(base, unlocked));

    auto turned = base;
    turned.facing += 0.1f;
    EXPECT_EQ(kFieldFacing, diffEntity(base, turned));

    auto retarget = base;
    retarget.attackTarget = 8;
    EXPECT_EQ(kFieldTarget, diffEntity(base, retarget));

    // ChangeToStandardFaction on the host: only the faction travels.
    auto turned_hostile = base;
    turned_hostile.faction = 3;
    EXPECT_EQ(kFieldFaction, diffEntity(base, turned_hostile));
    EXPECT_EQ(kFieldFaction, exactDiffEntity(base, turned_hostile));
    EntityState merged = base;
    mergeEntity(merged, turned_hostile, kFieldFaction);
    EXPECT_EQ(3, merged.faction);
    mergeEntity(merged, base, kFieldPosition);
    EXPECT_EQ(3, merged.faction) << "a delta without the faction keeps it";
}

TEST(NetProtocol, merge_copies_only_the_masked_fields) {
    EntityState dst;
    dst.id = 42;
    auto src = sampleEntity();
    mergeEntity(dst, src, kFieldPosition | kFieldVitals);
    EXPECT_EQ(src.position, dst.position);
    EXPECT_EQ(src.hp, dst.hp);
    EXPECT_TRUE(dst.anim.empty());
    EXPECT_TRUE(dst.tag.empty());
    mergeEntity(dst, src, kFieldIdentity);
    EXPECT_EQ(src.tag, dst.tag);
    EXPECT_EQ(src.key, dst.key);
}

TEST(NetFingerprint, covers_key_modules_and_tlk) {
    auto root = std::filesystem::temp_directory_path() / "reone_coop_fingerprint_test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "Modules");
    auto write = [](const std::filesystem::path &path, const std::string &text) {
        std::ofstream out(path, std::ios::binary);
        out << text;
    };
    write(root / "chitin.key", "KEY V1  data");
    write(root / "dialog.tlk", "TLK V3.0 lines");
    write(root / "Modules" / "END_M01AA.rim", "rim");
    write(root / "Modules" / "end_m01ab.rim", "rim2");

    auto a = fingerprintInstall(root);
    EXPECT_TRUE(a.present);
    EXPECT_NE(0u, a.key);
    EXPECT_NE(0u, a.tlk);
    EXPECT_EQ(a, fingerprintInstall(root)) << "fingerprints are stable";
    EXPECT_EQ("", fingerprintDifference(a, a));

    write(root / "Modules" / "end_m01ab.rim", "rim22");
    auto sized = fingerprintInstall(root);
    EXPECT_EQ(a.key, sized.key);
    EXPECT_NE(a.modules, sized.modules);
    EXPECT_EQ("modules", fingerprintDifference(a, sized));

    write(root / "dialog.tlk", "TLK V3.0 other");
    auto tlk = fingerprintInstall(root);
    EXPECT_EQ("tlk", fingerprintDifference(sized, tlk));

    write(root / "chitin.key", "KEY V1  more");
    EXPECT_EQ("key", fingerprintDifference(tlk, fingerprintInstall(root)));

    Fingerprint absent;
    EXPECT_EQ("presence", fingerprintDifference(a, absent));
    EXPECT_EQ("absent", describeFingerprint(absent));
    std::filesystem::remove_all(root);
}

TEST(NetFingerprint, installs_of_other_languages_are_compatible_but_flag_the_tlk) {
    Fingerprint en;
    en.present = true;
    en.key = 1;
    en.modules = 2;
    en.tlk = 3;
    auto fr = en;
    fr.tlk = 4;
    EXPECT_TRUE(installsCompatible(en, fr));
    EXPECT_TRUE(tlkDiffers(en, fr));
    EXPECT_FALSE(tlkDiffers(en, en));

    auto otherKey = en;
    otherKey.key = 9;
    EXPECT_FALSE(installsCompatible(en, otherKey));
    auto otherModules = en;
    otherModules.modules = 9;
    EXPECT_FALSE(installsCompatible(en, otherModules));

    Fingerprint absent;
    EXPECT_FALSE(installsCompatible(en, absent));
    EXPECT_FALSE(installsCompatible(absent, absent));
    EXPECT_FALSE(tlkDiffers(en, absent));
}

TEST(NetTransport, loopback_delivers_in_order_and_reports_close) {
    auto [host, client] = createLoopbackPair();
    std::vector<TransportEvent> events;
    host->poll(events);
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ(TransportEventType::Connected, events[0].type);
    auto connection = events[0].connection;
    events.clear();
    client->poll(events);
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ(TransportEventType::Connected, events[0].type);

    EXPECT_TRUE(client->send(connection, {1, 2}, SendMode::Reliable));
    EXPECT_TRUE(client->send(connection, {3}, SendMode::Reliable));
    events.clear();
    host->poll(events);
    ASSERT_EQ(2u, events.size());
    EXPECT_EQ((std::vector<uint8_t> {1, 2}), events[0].data);
    EXPECT_EQ((std::vector<uint8_t> {3}), events[1].data);
    EXPECT_EQ(3u, client->stats().bytesSent);
    EXPECT_EQ(3u, host->stats().bytesReceived);

    host->close(connection, "kicked");
    events.clear();
    client->poll(events);
    ASSERT_EQ(1u, events.size());
    EXPECT_EQ(TransportEventType::Disconnected, events[0].type);
    EXPECT_EQ("kicked", events[0].reason);
    EXPECT_FALSE(client->send(connection, {4}, SendMode::Reliable));
}

TEST(NetTransport, socket_transport_rejects_a_malformed_address) {
    if (!socketTransportAvailable()) {
        GTEST_SKIP() << "built without GameNetworkingSockets";
    }
    std::string error;
    EXPECT_EQ(nullptr, createClientTransport("not an address", error));
    EXPECT_FALSE(error.empty());
}

// A real GameNetworkingSockets host and client over UDP on the loopback address:
// connect, exchange reliable messages both ways in order, then close.
TEST(NetTransport, socket_host_and_client_exchange_messages_over_loopback_udp) {
    if (!socketTransportAvailable()) {
        GTEST_SKIP() << "built without GameNetworkingSockets";
    }
    std::string error;
    auto host = createHostTransport(27931, 2, error);
    ASSERT_TRUE(host) << error;
    auto client = createClientTransport("127.0.0.1:27931", error);
    ASSERT_TRUE(client) << error;

    ConnectionId clientOnHost = 0;
    ConnectionId hostOnClient = 0;
    bool hostSawClient = false;
    std::vector<std::vector<uint8_t>> atHost;
    std::vector<std::vector<uint8_t>> atClient;
    bool sent = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && (atHost.size() < 3 || atClient.size() < 3)) {
        std::vector<TransportEvent> events;
        host->poll(events);
        for (auto &event : events) {
            if (event.type == TransportEventType::Connected) {
                clientOnHost = event.connection;
                hostSawClient = true;
            } else if (event.type == TransportEventType::Message) {
                atHost.push_back(event.data);
            }
        }
        events.clear();
        client->poll(events);
        for (auto &event : events) {
            if (event.type == TransportEventType::Connected) {
                hostOnClient = event.connection;
            } else if (event.type == TransportEventType::Message) {
                atClient.push_back(event.data);
            }
        }
        if (!sent && hostSawClient && hostOnClient) {
            for (uint8_t i = 1; i <= 3; ++i) {
                EXPECT_TRUE(host->send(clientOnHost, {i, 'h'}, SendMode::Reliable));
                EXPECT_TRUE(client->send(hostOnClient, {i, 'c'}, SendMode::Reliable));
            }
            sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    ASSERT_TRUE(hostSawClient) << "the client never connected";
    ASSERT_EQ(3u, atHost.size());
    ASSERT_EQ(3u, atClient.size());
    for (uint8_t i = 0; i < 3; ++i) {
        EXPECT_EQ((std::vector<uint8_t> {static_cast<uint8_t>(i + 1), 'c'}), atHost[i]) << "client to host, in order";
        EXPECT_EQ((std::vector<uint8_t> {static_cast<uint8_t>(i + 1), 'h'}), atClient[i]) << "host to client, in order";
    }
}

// Review R3 with the real transport: a client floods the host; each poll takes
// a bounded amount, the rest arrives on later polls in order, and a message
// over the client limit is refused without its bytes being copied.
TEST(NetTransport, socket_host_drains_a_flood_within_its_per_poll_budget) {
    if (!socketTransportAvailable()) {
        GTEST_SKIP() << "built without GameNetworkingSockets";
    }
    std::string error;
    auto host = createHostTransport(27097, 2, error);
    ASSERT_TRUE(host) << error;
    auto client = createClientTransport("127.0.0.1:27097", error);
    ASSERT_TRUE(client) << error;

    constexpr int kFlood = 1500;
    ConnectionId hostOnClient = 0;
    bool hostSawClient = false;
    bool sent = false;
    std::vector<int> order;
    size_t refused = 0;
    size_t refusedBytes = 0;
    size_t largestPoll = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && (order.size() < kFlood || !refused)) {
        std::vector<TransportEvent> events;
        host->poll(events);
        size_t messages = 0;
        size_t bytes = 0;
        for (auto &event : events) {
            if (event.type == TransportEventType::Connected) {
                hostSawClient = true;
            } else if (event.type == TransportEventType::Message) {
                ++messages;
                bytes += event.data.size();
                if (event.oversizedBytes) {
                    ++refused;
                    refusedBytes = event.oversizedBytes;
                    EXPECT_TRUE(event.data.empty()) << "an oversized message is not copied";
                } else {
                    ASSERT_GE(event.data.size(), 3u);
                    order.push_back(event.data[1] | (event.data[2] << 8));
                }
            }
        }
        EXPECT_LE(messages, 512u);
        EXPECT_LE(bytes, 1024u * 1024u);
        largestPoll = std::max(largestPoll, messages);
        events.clear();
        client->poll(events);
        for (auto &event : events) {
            if (event.type == TransportEventType::Connected) {
                hostOnClient = event.connection;
            }
        }
        if (!sent && hostSawClient && hostOnClient) {
            for (int i = 0; i < kFlood; ++i) {
                std::vector<uint8_t> message(300, static_cast<uint8_t>(MessageType::Command));
                message[1] = static_cast<uint8_t>(i & 0xff);
                message[2] = static_cast<uint8_t>(i >> 8);
                ASSERT_TRUE(client->send(hostOnClient, message, SendMode::Reliable)) << i;
                if (i == kFlood / 2) {
                    ASSERT_TRUE(client->send(hostOnClient, std::vector<uint8_t>(kMaxClientMessageBytes + 1000, static_cast<uint8_t>(MessageType::Command)), SendMode::Reliable));
                }
            }
            sent = true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_TRUE(hostSawClient);
    ASSERT_EQ(static_cast<size_t>(kFlood), order.size()) << "legitimate messages all arrive";
    for (int i = 0; i < kFlood; ++i) {
        ASSERT_EQ(i, order[i]) << "in order";
    }
    EXPECT_EQ(1u, refused);
    EXPECT_EQ(kMaxClientMessageBytes + 1000, refusedBytes);
    EXPECT_LE(largestPoll, 512u);
}

TEST(NetEntityDiff, a_look_change_is_its_own_field) {
    EntityState base;
    base.id = 1;
    EntityState named = base;
    named.look.playerName = "John";
    EXPECT_EQ(kFieldLook, diffEntity(base, named));
    EXPECT_EQ(kFieldLook, exactDiffEntity(base, named));
    EntityState equipped = named;
    equipped.look.slots.push_back({9, "g_i_implant01"});
    EXPECT_EQ(kFieldLook, diffEntity(named, equipped));
    EntityState merged = base;
    mergeEntity(merged, equipped, kFieldLook);
    EXPECT_EQ(equipped.look, merged.look);
    EXPECT_EQ(0, diffEntity(equipped, merged));
}
