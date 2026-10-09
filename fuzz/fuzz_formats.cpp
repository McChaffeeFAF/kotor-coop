// libFuzzer harness for the file-format readers. One target is compiled per
// FUZZ_TARGET_<name> definition (see fuzz/CMakeLists.txt).
//
// A reader must reject malformed input by throwing a std::runtime_error (the
// engine's ValidationException / EndOfStreamException derive from it). Anything
// else (a crash, a sanitizer report, std::logic_error such as out_of_range or
// invalid_argument escaping, std::bad_alloc / length_error from a length field)
// is a finding.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "reone/graphics/format/bwmreader.h"
#include "reone/graphics/format/lipreader.h"
#include "reone/graphics/format/mdlmdxreader.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tpcreader.h"
#include "reone/graphics/format/txireader.h"
#include "reone/graphics/statistic.h"
#include "reone/resource/format/2dareader.h"
#include "reone/resource/format/bifreader.h"
#include "reone/resource/format/erfreader.h"
#include "reone/resource/format/gffreader.h"
#include "reone/resource/format/keyreader.h"
#include "reone/resource/format/lytreader.h"
#include "reone/resource/format/rimreader.h"
#include "reone/resource/format/ssfreader.h"
#include "reone/resource/format/tlkreader.h"
#include "reone/resource/format/visreader.h"
#include "reone/script/format/ncsreader.h"
#include "reone/system/stream/memoryinput.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::script;

namespace {

void run(const uint8_t *data, size_t size) {
    // Readers may keep pointers into the buffer; give each run its own copy.
    std::vector<char> bytes(reinterpret_cast<const char *>(data), reinterpret_cast<const char *>(data) + size);
    MemoryInputStream in(bytes.data(), bytes.size());

#if defined(FUZZ_TARGET_key)
    KeyReader(in).load();
#elif defined(FUZZ_TARGET_bif)
    BifReader(in).load();
#elif defined(FUZZ_TARGET_erf)
    ErfReader(in).load();
#elif defined(FUZZ_TARGET_rim)
    RimReader(in).load();
#elif defined(FUZZ_TARGET_gff)
    GffReader(in).load();
#elif defined(FUZZ_TARGET_2da)
    TwoDAReader(in).load();
#elif defined(FUZZ_TARGET_tlk)
    TlkReader(in).load();
#elif defined(FUZZ_TARGET_ssf)
    SsfReader(in).load();
#elif defined(FUZZ_TARGET_lip)
    LipReader(in, "fuzz").load();
#elif defined(FUZZ_TARGET_lyt)
    LytReader().load(in);
#elif defined(FUZZ_TARGET_vis)
    VisReader().load(in);
#elif defined(FUZZ_TARGET_mdl)
    // First two bytes choose how much of the rest is the MDX stream.
    if (size < 2) {
        return;
    }
    size_t body = size - 2;
    size_t mdxSize = (static_cast<size_t>(static_cast<uint8_t>(bytes[0])) | (static_cast<size_t>(static_cast<uint8_t>(bytes[1])) << 8)) % (body + 1);
    MemoryInputStream mdl(bytes.data() + 2, body - mdxSize);
    MemoryInputStream mdx(bytes.data() + 2 + (body - mdxSize), mdxSize);
    Statistic statistic;
    MdlMdxReader(mdl, mdx, statistic).load();
#elif defined(FUZZ_TARGET_tpc)
    TpcReader(in, "fuzz", TextureUsage::Default).load();
#elif defined(FUZZ_TARGET_tga)
    TgaReader(in, "fuzz", TextureUsage::Default).load();
#elif defined(FUZZ_TARGET_txi)
    TxiReader().load(in);
#elif defined(FUZZ_TARGET_bwm)
    BwmReader(in).load();
#elif defined(FUZZ_TARGET_ncs)
    NcsReader(in, "fuzz").load();
#else
#error "define one FUZZ_TARGET_<name>"
#endif
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    try {
        run(data, size);
    } catch (const std::runtime_error &) {
        // clean rejection
    }
    return 0;
}
