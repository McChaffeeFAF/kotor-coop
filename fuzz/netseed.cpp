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

    emitText("ABCD-2345");
    emitText("example.org:27200");
    emitText("kc://host:27200");
    emitText("AAAA");
    return 0;
}
