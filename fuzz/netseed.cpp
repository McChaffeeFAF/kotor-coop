// Writes synthetic seed inputs for fuzz_net: one encoded sample of every
// co-op message type, plus a few join-code strings.  fuzz_netseed <out-dir>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "reone/net/protocol.h"

using namespace reone::net;
namespace fs = std::filesystem;

namespace {

fs::path outDir;
int counter = 0;

void emit(const std::vector<uint8_t> &bytes) {
    fs::create_directories(outDir / "net");
    std::ofstream f(outDir / "net" / ("seed" + std::to_string(counter++) + ".bin"), std::ios::binary);
    f.write(reinterpret_cast<const char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void emitText(const std::string &text) {
    emit(std::vector<uint8_t>(text.begin(), text.end()));
}

} // namespace

int main(int argc, char **argv) {
    outDir = argc > 1 ? argv[1] : "fuzz-corpus";

    Hello hello;
    hello.build = "alpha-0.2.6";
    hello.mode = "story";
    hello.playerName = "Guest";
    hello.k1.present = true;
    hello.k1.key = 1;
    emit(encode(hello));
    emit(encode(Welcome {7, 100, 30, "story", 99}));
    emit(encode(Reject {RejectReason::SessionFull, "full"}));
    emit(encode(SlotRequest {"bastila"}));
    emit(encode(SlotAssign {SlotStatus::Assigned, 1, 12, "bastila"}));
    ModuleInfo module;
    module.generation = 1;
    module.module = "end_m01aa";
    module.player.hostId = 5;
    module.player.name = "Host";
    emit(encode(module));

    Snapshot snapshot;
    snapshot.tick = 10;
    snapshot.generation = 1;
    snapshot.worldFields = kWorldScreen | kWorldFade | kWorldParty | kWorldPause;
    snapshot.world.party.push_back({0, 5});
    EntityRecord entity;
    entity.fields = kFieldIdentity | kFieldPosition | kFieldFacing | kFieldVitals | kFieldFlags;
    entity.state.id = 5;
    entity.state.type = 1;
    entity.state.key = "1:player";
    entity.state.tag = "player";
    entity.state.resref = "p_carth";
    entity.state.hp = 10;
    entity.state.maxHp = 10;
    snapshot.entities.push_back(entity);
    emit(encode(snapshot));

    Command command;
    command.seq = 1;
    command.type = CommandType::MoveTo;
    command.actor = 12;
    command.point = {1.0f, 2.0f, 0.0f};
    emit(encode(command));
    emit(encode(CommandResult {1, true, CommandReject::None, 10}));
    emit(encode(Bye {"bye"}));
    emit(encode(Notice {NoticeKind::Info, "hello"}));
    emit(encode(ContainerView {9, true, {{3, "g_i_credits", "tag", 5}}}));
    ReadyCheck ready;
    ready.id = 1;
    ready.state = ReadyState::Active;
    ready.initiator = "Host";
    ready.destination = "end_m01ab";
    ready.secondsLeft = 20;
    ready.players.push_back({"Guest", ReadyAnswer::Pending, false});
    emit(encode(ready));
    emit(encode(SnapshotAck {1, 10}));
    EventBatch events;
    events.tick = 10;
    NetEvent event;
    event.kind = EventKind::Projectile;
    event.handle = 1;
    event.a = 5;
    event.b = 6;
    events.events.push_back(event);
    emit(encode(events));
    emit(encode(SharedInventory {5, 100, {{4, "g_i_medeqpmnt01", 2, kInventoryUsable}}}));
    CharacterSheet sheet;
    sheet.creature = 12;
    sheet.classes = {{0, 3}};
    sheet.feats = {1, 2};
    emit(encode(sheet));
    emit(encode(Bark {"Hello there", 3.0f}));
    GuestCharacter guest;
    guest.name = "Guest";
    guest.classes = {{0, 1}};
    guest.baseHp = 10;
    guest.items.push_back({"g_w_blstrpstl001", 1, 4});
    emit(encode(guest));
    StoreView store;
    store.store = 3;
    store.stock.push_back({1, "g_i_medeqpmnt01", 1, 25});
    emit(encode(store));
    emit(encode(GuestRoster {{{"Guest", 10, 10, 1, true}}}));
    emit(encode(Offer {OfferKind::Store, 8, "Merchant", 0}));

    // Every command type and every refusal, so the fuzzer starts from each decode branch.
    for (int type = 0; type < 256; ++type) {
        if (std::string(commandTypeName(static_cast<CommandType>(type))) == "unknown") {
            continue;
        }
        Command each;
        each.seq = static_cast<uint32_t>(type);
        each.type = static_cast<CommandType>(type);
        each.actor = 12;
        each.target = 9;
        each.point = {1.0f, 2.0f, 3.0f};
        each.param = type;
        each.item = type % 2 ? "g_i_medeqpmnt01" : "c2|a-1|s|f|p";
        emit(encode(each));
    }
    for (int reason = 0; reason < 256; ++reason) {
        if (std::string(commandRejectName(static_cast<CommandReject>(reason))) == "unknown") {
            continue;
        }
        emit(encode(CommandResult {static_cast<uint32_t>(reason), reason == 0, static_cast<CommandReject>(reason), 77}));
    }
    for (int reason = 0; reason < 256; ++reason) {
        if (std::string(rejectReasonName(static_cast<RejectReason>(reason))) == "unknown") {
            continue;
        }
        emit(encode(Reject {static_cast<RejectReason>(reason), "detail"}));
    }
    for (int status = 0; status < 256; ++status) {
        if (std::string(slotStatusName(static_cast<SlotStatus>(status))) == "unknown") {
            continue;
        }
        emit(encode(SlotAssign {static_cast<SlotStatus>(status), 3, 44, "end_trask"}));
    }
    for (int kind = 0; kind <= 5; ++kind) {
        emit(encode(Notice {static_cast<NoticeKind>(kind), "a line for the player"}));
    }
    for (int kind = 1; kind <= 3; ++kind) {
        emit(encode(Offer {static_cast<OfferKind>(kind), 8, "Merchant", 100}));
    }
    for (int kind = 1; kind <= 3; ++kind) {
        NetEvent each;
        each.kind = static_cast<EventKind>(kind);
        each.handle = 9;
        each.row = 4;
        each.a = 5;
        each.b = 6;
        each.location = {1.0f, 2.0f, 3.0f};
        each.facing = 1.5f;
        each.flags = kind == 1 ? 3 : 1;
        EventBatch batch;
        batch.generation = 1;
        batch.tick = 10;
        batch.events.push_back(each);
        emit(encode(batch));
    }
    for (int state = 0; state < 6; ++state) {
        for (int answer = 0; answer < 4; ++answer) {
            ReadyCheck check;
            check.id = 2;
            check.state = static_cast<ReadyState>(state);
            check.initiator = "Host";
            check.destination = "end_m01ab";
            check.secondsLeft = 12;
            check.players.push_back({"Guest", static_cast<ReadyAnswer>(answer), answer % 2 == 0});
            emit(encode(check));
        }
    }

    // Snapshots: each entity field alone, then everything at once, and a delta against a base.
    for (uint16_t bit = 0; bit < 11; ++bit) {
        Snapshot each;
        each.tick = 20 + bit;
        each.generation = 1;
        each.baseTick = bit % 2 ? 10 : 0;
        EntityRecord record;
        record.fields = static_cast<uint16_t>(1u << bit);
        record.state.id = 5 + bit;
        record.state.type = 1 + bit % 6;
        record.state.key = "1:player";
        record.state.tag = "player";
        record.state.resref = "p_carth";
        record.state.position = {1.0f, 2.0f, 3.0f};
        record.state.facing = 1.0f;
        record.state.anim = "run";
        record.state.animTime = 0.5f;
        record.state.hp = 12;
        record.state.maxHp = 20;
        record.state.rightWeapon = "g_w_blstrpstl01";
        record.state.leftWeapon = "g_w_blstrpstl01";
        record.state.armor = "g_a_clothes01";
        record.state.headgear = "g_a_helmet01";
        record.state.open = true;
        record.state.doorState = 2;
        record.state.flags = 0x3f;
        record.state.attackTarget = 6;
        each.entities.push_back(record);
        emit(encode(each));
    }
    for (uint16_t bit = 0; bit < 7; ++bit) {
        Snapshot each;
        each.tick = 40 + bit;
        each.generation = 1;
        each.worldFields = static_cast<uint16_t>(1u << bit);
        each.world.screen = 3;
        each.world.fadeOpacity = 0.5f;
        each.world.movie = true;
        each.world.movieName = "legal";
        each.world.cameraType = 2;
        each.world.cameraId = 8;
        each.world.conversation.active = true;
        each.world.conversation.dialog = "end_trask";
        each.world.conversation.entry = 2;
        each.world.conversation.speaker = "Trask";
        each.world.conversation.replies = {0, 1, 2};
        each.world.party.push_back({0, 5});
        each.world.leader = 5;
        each.world.paused = true;
        each.world.pausedBy = "Guest";
        emit(encode(each));
    }

    // Hello with both installs and a rejoin token; a character with every kind of item slot.
    Hello full;
    full.build = "alpha-0.2.6";
    full.mode = "free";
    full.playerName = "Guest";
    full.k1 = {true, 1, 2, 3};
    full.k2 = {true, 4, 5, 6};
    full.rejoinToken = 0x1122334455667788ull;
    emit(encode(full));
    GuestCharacter many;
    many.name = "Guest";
    many.classes = {{0, 2}, {1, 1}};
    many.feats = {1, 2, 3};
    many.spells = {4, 5};
    many.xp = 1000;
    many.credits = 50;
    many.baseHp = 20;
    many.currentHp = 15;
    for (int slot = 0; slot < 14; ++slot) {
        many.items.push_back({"g_i_item" + std::to_string(slot), static_cast<uint16_t>(1 + slot), static_cast<uint8_t>(slot)});
    }
    many.items.push_back({"g_i_bag", 4, kGuestInventory});
    emit(encode(many));
    BenchView bench;
    bench.bench = 31;
    bench.kind = BenchKind::Workbench;
    bench.components = 40;
    bench.chemicals = 12;
    BenchItem benched;
    benched.id = 9;
    benched.resref = "g_w_blstrpstl01";
    benched.category = 2;
    benched.equipped = true;
    benched.upgrades = 0x14;
    BenchSlot benchSlot;
    benchSlot.type = 4;
    benchSlot.installedRow = 2;
    benchSlot.candidates = {5, 6};
    benched.slots.push_back(benchSlot);
    bench.items.push_back(benched);
    bench.parts.push_back({5, "g_i_upgrade001", 2, 0});
    bench.breakdown.push_back({7, "g_w_blstrpstl02", 1, 3, 0});
    bench.recipes.push_back({"g_w_crafted01", 0, 20, true, false});
    emit(encode(bench));
    bench.kind = BenchKind::Lab;
    bench.open = false;
    bench.items.clear();
    emit(encode(bench));
    MiniGameControl control;
    control.seq = 7;
    control.steer = -0.5f;
    control.fire = true;
    control.pitchDelta = 3.0f;
    emit(encode(control));
    MiniGameState race;
    race.kind = MiniGameKind::SwoopRace;
    race.module = "mg_seed";
    race.owner = "Guest";
    race.tick = 120;
    race.elapsed = 12.0f;
    race.speed = 80.0f;
    race.hp = 90;
    race.maxHp = 100;
    race.controlSeq = 7;
    MiniGameActorState rider;
    rider.kind = MiniGameRole::Player;
    rider.hp = 90;
    race.actors.push_back(rider);
    race.animations.push_back({0, "gear1", MiniGameAnimation::Looping});
    race.sounds.push_back("mgs_beep");
    emit(encode(race));
    race.active = false;
    race.finished = true;
    race.prize = 250;
    emit(encode(race));
    StoreView closed;
    closed.store = 3;
    closed.open = false;
    emit(encode(closed));
    emit(encode(ContainerView {9, false, {}}));

    emitText("ABCD-2345");
    emitText("example.org:27200");
    emitText("kc://host:27200");
    emitText("AAAA");
    return 0;
}
