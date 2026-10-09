// libFuzzer harness for the co-op wire format and the join-code helpers.
//
// Every decoder is fed the raw input. A decoder must never crash or allocate
// from a length field, and anything it accepts must be canonical: encoding the
// decoded value and decoding that again has to give the same bytes. A message
// the host would later send but the peer would refuse is a bug in the pair.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "reone/net/joincode.h"
#include "reone/net/protocol.h"

using namespace reone::net;

namespace {

template <class T>
void tryDecode(const std::vector<uint8_t> &bytes) {
    T message;
    if (!decode(bytes, message)) {
        return;
    }
    auto again = encode(message);
    T second;
    if (!decode(again, second) || encode(second) != again) {
        __builtin_trap();
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    std::vector<uint8_t> bytes(data, data + size);

    (void)peekType(bytes);
    tryDecode<Hello>(bytes);
    tryDecode<Welcome>(bytes);
    tryDecode<Reject>(bytes);
    tryDecode<SlotRequest>(bytes);
    tryDecode<SlotAssign>(bytes);
    tryDecode<ModuleInfo>(bytes);
    tryDecode<Snapshot>(bytes);
    tryDecode<Command>(bytes);
    tryDecode<CommandResult>(bytes);
    tryDecode<Bye>(bytes);
    tryDecode<Notice>(bytes);
    tryDecode<ContainerView>(bytes);
    tryDecode<ReadyCheck>(bytes);
    tryDecode<SnapshotAck>(bytes);
    tryDecode<EventBatch>(bytes);
    tryDecode<SharedInventory>(bytes);
    tryDecode<CharacterSheet>(bytes);
    tryDecode<Bark>(bytes);
    tryDecode<GuestCharacter>(bytes);
    tryDecode<StoreView>(bytes);
    tryDecode<GuestRoster>(bytes);
    tryDecode<Offer>(bytes);

    std::string text(reinterpret_cast<const char *>(data), size);
    (void)normalizeJoinCode(text);
    (void)looksLikeAddress(text);
    std::string host;
    uint16_t port = 0;
    (void)parseServiceAddress(text, host, port);
    std::vector<uint8_t> decoded;
    if (base64Decode(text, decoded)) {
        // Strict base64 is canonical: it must encode back to the same text.
        if (base64Encode(decoded.data(), decoded.size()) != text) {
            __builtin_trap();
        }
    }
    return 0;
}
