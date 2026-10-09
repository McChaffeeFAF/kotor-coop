// Writes tiny synthetic seed inputs for the fuzz targets, one directory per
// target:  fuzz_seedgen <out-dir>
// The bytes are built here from the public format layouts; no game file is ever
// read or embedded.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Bytes {
    std::string s;
    Bytes &str(const std::string &v) { return s += v, *this; }
    Bytes &fixed(const std::string &v, size_t n) { return s += v + std::string(n - v.size(), '\0'), *this; }
    Bytes &zeros(size_t n) { return s += std::string(n, '\0'), *this; }
    Bytes &u8(uint8_t v) { return s += static_cast<char>(v), *this; }
    Bytes &u16(uint16_t v) { return u8(v & 0xff).u8(v >> 8); }
    Bytes &u32(uint32_t v) { return u16(v & 0xffff).u16(v >> 16); }
    Bytes &be32(uint32_t v) { return u8(v >> 24).u8((v >> 16) & 0xff).u8((v >> 8) & 0xff).u8(v & 0xff); }
    Bytes &f32(float v) {
        uint32_t b;
        std::memcpy(&b, &v, 4);
        return u32(b);
    }
    Bytes &raw(const char *p, size_t n) { return s.append(p, n), *this; }
};

fs::path outDir;
int counter = 0;

void emit(const std::string &target, const Bytes &b) {
    auto dir = outDir / target;
    fs::create_directories(dir);
    std::ofstream(dir / ("seed" + std::to_string(counter++) + ".bin"), std::ios::binary) << b.s;
}

} // namespace

int main(int argc, char **argv) {
    outDir = argc > 1 ? argv[1] : "fuzz-corpus";

    emit("key", Bytes().str("KEY V1  ").u32(2).u32(2).u32(0x40).u32(0x5e).u32(0).u32(0).zeros(32)
                    .u32(0x80).u32(0x58).u16(2).u16(0)
                    .u32(0x100).u32(0x5b).u16(2).u16(0)
                    .str(std::string("Aa\0", 3)).str(std::string("Bb\0", 3))
                    .fixed("Cc", 16).u16(0x07e1).u32(0x00c007d3)
                    .fixed("Dd", 16).u16(0x07f5).u32(0x00c007d3));

    emit("bif", Bytes().str("BIFFV1  ").u32(1).u32(0).u32(0x14)
                    .u32(0).u32(0x24).u32(13).u32(0x07e6)
                    .str("Hello, world!"));

    emit("erf", Bytes().str("ERF V1.0").u32(0).u32(0).u32(1).u32(0xa0).u32(0xa0).u32(0xb8).u32(0).u32(0).u32(0xffffffff).zeros(116)
                    .fixed("Aa", 16).u32(0).u16(0x07e6).u16(0)
                    .u32(0xc0).u32(2)
                    .str("Bb"));

    emit("rim", Bytes().str("RIM V1.0").u32(0).u32(1).u32(0x78).zeros(100)
                    .fixed("Aa", 16).u32(0x07e6).u32(0).u32(0x98).u32(2)
                    .str("Bb"));

    emit("tlk", Bytes().str("TLK V3.0").u32(0).u32(2).u32(0x64)
                    .u32(7).fixed("", 16).u32(0).u32(0).u32(0).u32(4).u32(0)
                    .u32(7).fixed("jane", 16).u32(0).u32(0).u32(4).u32(4).u32(0)
                    .str("JohnJane"));

    emit("2da", Bytes().str("2DA V2.b").u8(0x0a).str("key\t").str("value\t").u8(0).u32(2)
                    .str("0\t1\t").u16(0).u16(7).u16(7).u16(7).u16(0x0c)
                    .str(std::string("unique\0", 7)).str(std::string("same\0", 5)));
    emit("2da", Bytes().str("2DA V2.0\n\n  key value\n0 a b\n1 c d\n"));

    // Minimal GFF: one struct holding one INT field.
    emit("gff", Bytes().str("UTC V3.2").u32(56).u32(1).u32(68).u32(1).u32(80).u32(1).u32(96).u32(0).u32(96).u32(0).u32(96).u32(0)
                    .u32(0xffffffff).u32(0).u32(1)
                    .u32(5).u32(0).u32(42)
                    .fixed("Num", 16));

    emit("ssf", Bytes().str("SSF V1.1").u32(12).u32(0).u32(1).u32(2).u32(0xffffffff));

    emit("lip", Bytes().str("LIP V1.0").f32(1.0f).u32(2).f32(0.0f).u8(0).f32(0.5f).u8(3));

    emit("lyt", Bytes().str("beginlayout\n  roomcount 1\n    room_a 0 0 0\n  doorhookcount 0\ndonelayout\n"));
    emit("lyt", Bytes().str("beginlayout\n  roomcount 2\n    a 1.5 2.5 0\n    b 0 0 0\n  trackcount 1\n    t 0 0 0\n  obstaclecount 1\n    o 0 0 0\ndonelayout\n"));

    emit("vis", Bytes().str("room_a 1\n  room_b\nroom_b 1\n  room_a\n"));

    emit("txi", Bytes().str("blending additive\nspacingr 0.015625"));
    emit("txi", Bytes().str("proceduretype cycle\nnumx 2\nnumy 2\nfps 8\nupperleftcoords 1\n0.0 0.0 0.0\nlowerrightcoords 1\n1.0 1.0 0.0\n"));

    emit("tga", Bytes().u8(0).u8(0).u8(3).zeros(9).u16(1).u16(1).u8(8).u8(0).u8(0xff));
    emit("tga", Bytes().u8(0).u8(0).u8(2).zeros(9).u16(1).u16(1).u8(32).u8(8).u8(1).u8(2).u8(3).u8(255));
    emit("tga", Bytes().u8(0).u8(0).u8(10).zeros(9).u16(2).u16(1).u8(24).u8(0).u8(0x81).u8(1).u8(2).u8(3));

    emit("tpc", Bytes().u32(0).u32(0).u16(1).u16(1).u8(1).u8(0).u8(1).u8(0).zeros(114 - 2 + 0).u8(0xff));
    emit("tpc", Bytes().u32(8).u32(0).u16(4).u16(4).u8(2).u8(0).u8(1).u8(0).zeros(112).zeros(8));

    emit("bwm", Bytes().str("BWM V1.0").u32(0).zeros(12 * 4).zeros(3 * 4).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0));

    {
        Bytes mdl;
        mdl.u32(0).u32(0x12e).u32(0)
            .u32(0).u32(0).fixed("some_model", 32).u32(0xd2).u32(1).zeros(24).u32(0).u32(0)
            .u8(0).u8(0).u8(0).u8(0).u32(0).u32(0).u32(0).u32(0).u32(0)
            .zeros(7 * 4).u32(0).fixed("NULL", 32).u32(0).u32(0).u32(0).u32(0).u32(0xc4).u32(1).u32(1)
            .u32(0xc8)
            .str(std::string("root_node\0", 10))
            .u16(0).u16(0).u16(0).u16(0).u32(0).u32(0).zeros(3 * 4).zeros(4 * 4).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        // Fuzz input = u16 MDX size, then MDL bytes, then MDX bytes.
        emit("mdl", Bytes().u16(0).str(mdl.s));
    }

    emit("ncs", Bytes().str("NCS V1.0").u8(0x42).be32(0x4a)
                    .u8(0x04).u8(0x03).be32(1)
                    .u8(0x04).u8(0x04).be32(0x3f800000)
                    .u8(0x1b).u8(0).be32(0xfffffffc)
                    .u8(0x20).u8(0)); // RETN

    return 0;
}
