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

// The enhanced-texture generator (docs/texgen.md): image decoding, normal and material
// maps, the classification rules, the manifest and the resumable job. Every texture
// here is synthetic.

#include <gtest/gtest.h>

#include "../fixtures/archive.h"

#include "reone/graphics/format/ddsreader.h"
#include "reone/graphics/format/tgareader.h"
#include "reone/graphics/format/tgawriter.h"
#include "reone/graphics/texture.h"
#include "reone/system/exception/validation.h"
#include "reone/system/stream/memoryinput.h"
#include "reone/texgen/generate.h"
#include "reone/texgen/image.h"
#include "reone/texgen/job.h"
#include "reone/texgen/manifest.h"
#include "reone/texgen/rules.h"
#include "reone/texgen/sources.h"

using namespace reone;
using namespace reone::graphics;
using namespace reone::resource;
using namespace reone::texgen;

namespace {

constexpr float kPi = 3.14159265358979f;

// A scratch folder that goes away with the test.
class TempDir {
public:
    TempDir() {
        static int counter = 0;
        _path = std::filesystem::temp_directory_path() / ("texgen_test_" + std::to_string(std::random_device {}()) + "_" + std::to_string(counter++));
        std::filesystem::remove_all(_path);
        std::filesystem::create_directories(_path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(_path, ec);
    }
    const std::filesystem::path &path() const { return _path; }

private:
    std::filesystem::path _path;
};

Image makeImage(int w, int h, const std::function<glm::vec3(int, int)> &fn) {
    Image image(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            auto c = fn(x, y);
            float *p = image.at(x, y);
            p[0] = c.r;
            p[1] = c.g;
            p[2] = c.b;
            p[3] = 1.0f;
        }
    }
    return image;
}

ByteBuffer tgaBytes(const Image &image) {
    return TgaWriter(image.width, image.height, PixelFormat::RGB8, toBytes(image, 3)).toBytes();
}

std::string asString(const ByteBuffer &bytes) {
    return std::string(bytes.begin(), bytes.end());
}

ByteBuffer flatTga(int size, float gray) {
    return tgaBytes(makeImage(size, size, [gray](int, int) { return glm::vec3(gray); }));
}

ByteBuffer noisyTga(int size, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return tgaBytes(makeImage(size, size, [&](int, int) { return glm::vec3(dist(rng)); }));
}

Image readTga(const std::filesystem::path &path, int *channelsOut = nullptr) {
    std::ifstream in(path, std::ios::binary);
    EXPECT_TRUE(in.good()) << path;
    ByteBuffer bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    MemoryInputStream stream(bytes);
    TgaReader reader(stream, "out", TextureUsage::Default);
    reader.load();
    EXPECT_NE(nullptr, reader.texture());
    return imageFromTexture(*reader.texture());
}

// A TPC with uncompressed RGB pixels (rows bottom to top) and an optional TXI after them.
ByteBuffer tpcBytes(int w, int h, const ByteBuffer &rgb, const std::string &txi = "") {
    ByteBuffer out(128, 0);
    auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&out[at], &v, 2); };
    put32(0, 0); // uncompressed
    put16(8, static_cast<uint16_t>(w));
    put16(10, static_cast<uint16_t>(h));
    out[12] = 2; // RGB
    out[13] = 1; // one mip map
    out.insert(out.end(), rgb.begin(), rgb.end());
    out.insert(out.end(), txi.begin(), txi.end());
    return out;
}

float nx(const Image &n, int x, int y) { return n.at(x, y)[0] * 2.0f - 1.0f; }
float ny(const Image &n, int x, int y) { return n.at(x, y)[1] * 2.0f - 1.0f; }
float nz(const Image &n, int x, int y) { return n.at(x, y)[2] * 2.0f - 1.0f; }

} // namespace

// Decoding -----------------------------------------------------------------------------

TEST(TexgenImage, decodes_the_pixel_formats_textures_load_in) {
    ByteBuffer rgb {char(255), char(0), char(0), char(0), char(255), char(0)}; // red, green
    auto a = imageFromPixels(2, 1, PixelFormat::RGB8, rgb);
    EXPECT_FLOAT_EQ(1.0f, a.at(0, 0)[0]);
    EXPECT_FLOAT_EQ(1.0f, a.at(1, 0)[1]);
    EXPECT_FLOAT_EQ(1.0f, a.at(0, 0)[3]);
    auto b = imageFromPixels(2, 1, PixelFormat::BGR8, rgb); // blue, green
    EXPECT_FLOAT_EQ(1.0f, b.at(0, 0)[2]);
    EXPECT_FLOAT_EQ(0.0f, b.at(0, 0)[0]);
    ByteBuffer rgba {char(10), char(20), char(30), char(128)};
    auto c = imageFromPixels(1, 1, PixelFormat::RGBA8, rgba);
    EXPECT_NEAR(30.0f / 255.0f, c.at(0, 0)[2], 1e-6f);
    EXPECT_NEAR(128.0f / 255.0f, c.at(0, 0)[3], 1e-6f);
    ByteBuffer gray {char(51)};
    auto d = imageFromPixels(1, 1, PixelFormat::R8, gray);
    EXPECT_NEAR(0.2f, d.at(0, 0)[1], 1e-6f);
    // One DXT1 block of solid white (both endpoints 0xffff, all indices 0).
    ByteBuffer dxt {char(0xff), char(0xff), char(0xff), char(0xff), 0, 0, 0, 0};
    auto e = imageFromPixels(4, 4, PixelFormat::DXT1, dxt);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            EXPECT_GT(e.at(x, y)[0], 0.97f);
            EXPECT_GT(e.at(x, y)[1], 0.97f);
            EXPECT_GT(e.at(x, y)[2], 0.97f);
        }
    }
}

TEST(TexgenImage, refuses_short_data_and_unknown_formats) {
    EXPECT_THROW(imageFromPixels(2, 2, PixelFormat::RGB8, ByteBuffer(5)), ValidationException);
    EXPECT_THROW(imageFromPixels(4, 4, PixelFormat::DXT5, ByteBuffer(8)), ValidationException);
    EXPECT_THROW(imageFromPixels(0, 4, PixelFormat::RGB8, ByteBuffer()), ValidationException);
    EXPECT_THROW(imageFromPixels(1, 1, PixelFormat::Depth24, ByteBuffer(8)), ValidationException);
}

// Normal maps ------------------------------------------------------------------------------

TEST(TexgenNormals, a_flat_image_gives_a_flat_normal_map) {
    auto flat = makeImage(16, 16, [](int, int) { return glm::vec3(0.4f); });
    auto normals = generateNormalMap(flat, NormalOptions {});
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            EXPECT_NEAR(0.0f, nx(normals, x, y), 1e-5f);
            EXPECT_NEAR(0.0f, ny(normals, x, y), 1e-5f);
            EXPECT_NEAR(1.0f, nz(normals, x, y), 1e-5f);
        }
    }
    // The byte form of "straight out": 128, 128, 255.
    auto bytes = toBytes(normals, 3);
    EXPECT_EQ(static_cast<char>(128), bytes[0]);
    EXPECT_EQ(static_cast<char>(128), bytes[1]);
    EXPECT_EQ(static_cast<char>(255), bytes[2]);
}

TEST(TexgenNormals, a_ramp_gives_the_slope_it_rises_at) {
    int w = 17;
    std::vector<float> height(static_cast<size_t>(w) * 8);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < w; ++x) {
            height[static_cast<size_t>(y) * w + x] = static_cast<float>(x) * 0.1f; // up by 0.1 per pixel to the right
        }
    }
    auto normals = normalsFromHeight(height, w, 8, 2.0f, /*wrap=*/false);
    float slope = 2.0f * 0.1f;
    float expected = -slope / std::sqrt(1.0f + slope * slope);
    for (int x = 1; x < w - 1; ++x) {
        EXPECT_NEAR(expected, nx(normals, x, 4), 1e-5f) << x;
        EXPECT_NEAR(0.0f, ny(normals, x, 4), 1e-5f);
    }
    // The normal leans away from the rising side.
    EXPECT_LT(nx(normals, 8, 4), 0.0f);
    // Strength scales the slope; zero strength is flat.
    auto steeper = normalsFromHeight(height, w, 8, 4.0f, false);
    EXPECT_LT(nx(steeper, 8, 4), nx(normals, 8, 4));
    auto none = normalsFromHeight(height, w, 8, 0.0f, false);
    EXPECT_NEAR(0.0f, nx(none, 8, 4), 1e-6f);
    // A ramp up the image leans the normal in y.
    std::vector<float> up(static_cast<size_t>(8) * w);
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < 8; ++x) {
            up[static_cast<size_t>(y) * 8 + x] = static_cast<float>(y) * 0.1f;
        }
    }
    auto upNormals = normalsFromHeight(up, 8, w, 2.0f, false);
    EXPECT_NEAR(expected, ny(upNormals, 4, 8), 1e-5f);
    EXPECT_NEAR(0.0f, nx(upNormals, 4, 8), 1e-5f);
}

TEST(TexgenNormals, a_dome_has_normals_that_point_away_from_its_centre) {
    int n = 33;
    float cx = 16.0f;
    float radius = 14.0f;
    std::vector<float> height(static_cast<size_t>(n) * n, 0.0f);
    for (int y = 0; y < n; ++y) {
        for (int x = 0; x < n; ++x) {
            float d2 = (x - cx) * (x - cx) + (y - cx) * (y - cx);
            if (d2 < radius * radius) {
                height[static_cast<size_t>(y) * n + x] = std::sqrt(radius * radius - d2) / radius;
            }
        }
    }
    auto normals = normalsFromHeight(height, n, n, 8.0f, false);
    // Straight above the centre the surface is level.
    EXPECT_NEAR(0.0f, nx(normals, 16, 16), 1e-5f);
    EXPECT_NEAR(0.0f, ny(normals, 16, 16), 1e-5f);
    EXPECT_NEAR(1.0f, nz(normals, 16, 16), 1e-5f);
    // To the right it leans right, above it leans up, and mirror positions mirror.
    EXPECT_GT(nx(normals, 24, 16), 0.1f);
    EXPECT_LT(nx(normals, 8, 16), -0.1f);
    EXPECT_NEAR(nx(normals, 24, 16), -nx(normals, 8, 16), 1e-4f);
    EXPECT_GT(ny(normals, 16, 24), 0.1f);
    EXPECT_LT(ny(normals, 16, 8), -0.1f);
    // On a diagonal the lean is split evenly between x and y.
    EXPECT_NEAR(nx(normals, 22, 22), ny(normals, 22, 22), 1e-4f);
    // Every normal has unit length.
    for (int y = 0; y < n; y += 4) {
        for (int x = 0; x < n; x += 4) {
            float len = std::sqrt(nx(normals, x, y) * nx(normals, x, y) + ny(normals, x, y) * ny(normals, x, y) + nz(normals, x, y) * nz(normals, x, y));
            EXPECT_NEAR(1.0f, len, 1e-4f);
        }
    }
}

TEST(TexgenNormals, a_tiling_texture_is_seamless_with_wrapping) {
    int w = 32;
    auto tile = makeImage(w, w, [&](int x, int y) {
        float v = 0.5f + 0.4f * std::sin(2.0f * kPi * static_cast<float>(x) / w) * std::cos(2.0f * kPi * static_cast<float>(y) / w);
        return glm::vec3(v);
    });
    NormalOptions wrapping;
    wrapping.wrap = true;
    auto a = generateNormalMap(tile, wrapping);
    // The same texture slid sideways by one pixel (and wrapped round) has the map slid by one pixel.
    auto slid = makeImage(w, w, [&](int x, int y) { return glm::vec3(tile.at((x + w - 1) % w, y)[0]); });
    auto b = generateNormalMap(slid, wrapping);
    float worst = 0.0f;
    for (int y = 0; y < w; ++y) {
        for (int x = 0; x < w; ++x) {
            worst = std::max(worst, std::abs(nx(b, x, y) - nx(a, (x + w - 1) % w, y)));
            worst = std::max(worst, std::abs(ny(b, x, y) - ny(a, (x + w - 1) % w, y)));
        }
    }
    EXPECT_LT(worst, 1e-4f) << "no seam at the edges";

    // Without wrapping the edge columns see a clamped neighbour: they differ from the wrapped map there
    // (and only there).
    NormalOptions strongWrap;
    strongWrap.wrap = true;
    strongWrap.strength = 4.0f;
    strongWrap.lowWeight = 1.0f;
    NormalOptions strongClamp = strongWrap;
    strongClamp.wrap = false;
    auto wrapped = generateNormalMap(tile, strongWrap);
    auto clamped = generateNormalMap(tile, strongClamp);
    float seam = 0.0f;
    float inside = 0.0f;
    for (int y = 0; y < w; ++y) {
        seam = std::max(seam, std::abs(nx(clamped, 0, y) - nx(wrapped, 0, y)));
        seam = std::max(seam, std::abs(nx(clamped, w - 1, y) - nx(wrapped, w - 1, y)));
        inside = std::max(inside, std::abs(nx(clamped, 16, 8) - nx(wrapped, 16, 8)));
    }
    EXPECT_GT(seam, 0.05f);
    EXPECT_LT(inside, 1e-4f) << "away from the edge the two agree";
}

TEST(TexgenNormals, detail_stands_out_from_the_low_frequencies) {
    // A broad gradient plus fine stripes: the stripes drive the normals, so they lean both ways.
    int w = 32;
    auto image = makeImage(w, w, [&](int x, int) {
        float v = 0.2f + 0.6f * static_cast<float>(x) / (w - 1) + (((x / 2) % 2) ? 0.05f : -0.05f);
        return glm::vec3(v);
    });
    NormalOptions options;
    options.wrap = false;
    options.lowWeight = 0.0f;
    auto normals = generateNormalMap(image, options);
    bool left = false;
    bool right = false;
    for (int x = 4; x < w - 4; ++x) {
        left = left || nx(normals, x, 16) < -0.01f;
        right = right || nx(normals, x, 16) > 0.01f;
    }
    EXPECT_TRUE(left && right);
}

// Material maps ----------------------------------------------------------------------------

TEST(TexgenMaterial, roughness_rises_with_contrast_and_a_shiny_texture_is_smoother_and_metallic) {
    auto flat = makeImage(16, 16, [](int, int) { return glm::vec3(0.5f); });
    auto checker = makeImage(16, 16, [](int x, int y) { return glm::vec3(((x + y) % 2) ? 0.9f : 0.1f); });
    MaterialOptions plain;
    auto flatMap = generateMaterialMap(flat, plain);
    auto checkerMap = generateMaterialMap(checker, plain);
    EXPECT_NEAR(0.8f, flatMap.at(8, 8)[0], 1e-5f);
    EXPECT_GT(checkerMap.at(8, 8)[0], flatMap.at(8, 8)[0]);
    EXPECT_FLOAT_EQ(0.0f, flatMap.at(8, 8)[1]) << "no metalness hint without a shiny texture";

    MaterialOptions shiny;
    shiny.shiny = true;
    auto shinyMap = generateMaterialMap(flat, shiny);
    EXPECT_LT(shinyMap.at(8, 8)[0], flatMap.at(8, 8)[0]);
    EXPECT_GT(shinyMap.at(8, 8)[1], 0.5f);
    for (int i = 0; i < 16 * 16; ++i) {
        EXPECT_GE(checkerMap.rgba[i * 4], 0.05f);
        EXPECT_LE(checkerMap.rgba[i * 4], 1.0f);
    }
    // Brighter surfaces are a little smoother.
    auto bright = makeImage(16, 16, [](int, int) { return glm::vec3(0.9f); });
    EXPECT_LT(generateMaterialMap(bright, plain).at(8, 8)[0], flatMap.at(8, 8)[0]);
}

// Rules -----------------------------------------------------------------------------------------

TEST(TexgenRules, globs_match_without_regard_to_case) {
    EXPECT_TRUE(globMatch("*_lm*", "M01AA_lm0"));
    EXPECT_TRUE(globMatch("p_*", "P_Bastila01"));
    EXPECT_FALSE(globMatch("p_*", "lp_x"));
    EXPECT_TRUE(globMatch("a?c", "abc"));
    EXPECT_FALSE(globMatch("a?c", "ac"));
    EXPECT_TRUE(globMatch("*", ""));
    EXPECT_TRUE(globMatch("a*b*c", "aXXbYYc"));
    EXPECT_FALSE(globMatch("a*b*c", "aXXbYY"));
}

TEST(TexgenRules, the_default_rules_follow_public_naming_conventions) {
    auto rules = Rules::defaults();
    auto classify = [&](const std::string &name, std::function<void(TextureInfo &)> adjust = nullptr) {
        TextureInfo info;
        info.name = name;
        info.width = info.height = 256;
        if (adjust) {
            adjust(info);
        }
        return rules.classify(info, 2.0f);
    };
    auto wall = classify("lda_wall01");
    EXPECT_EQ("surface", wall.className);
    EXPECT_EQ(Action::Generate, wall.action);
    EXPECT_FLOAT_EQ(2.0f, wall.strength);

    auto head = classify("p_bastilah01");
    EXPECT_EQ("face", head.className);
    EXPECT_EQ(Action::Generate, head.action);
    EXPECT_FLOAT_EQ(0.7f, head.strength) << "softened";
    EXPECT_EQ("face", classify("N_Carth01").className);

    for (const char *name : {"m01aa_lm0", "lbl_menu", "ui_button", "fx_flame", "fnt_d16x16", "gui_hud", "i_adrnaline", "po_pbastila"}) {
        EXPECT_EQ(Action::Skip, classify(name).action) << name;
    }
    EXPECT_EQ("lightmap", classify("m01aa_lm0").className);
    EXPECT_EQ("ui", classify("lbl_menu").className);
    EXPECT_EQ("effect", classify("fx_flame").className);

    auto decal = classify("somedecal", [](TextureInfo &i) { i.decal = true; });
    EXPECT_EQ(Action::Skip, decal.action);
    EXPECT_EQ("decal", decal.className);
    auto text = classify("whatever", [](TextureInfo &i) { i.font = true; });
    EXPECT_EQ("text", text.className);
    auto leaf = classify("tree_leaf", [](TextureInfo &i) { i.cutout = true; });
    EXPECT_EQ("foliage", leaf.className);
    EXPECT_EQ(Action::Generate, leaf.action);
    EXPECT_FLOAT_EQ(1.0f, leaf.strength);
    // The base strength scales the softening.
    TextureInfo info;
    info.name = "p_x";
    EXPECT_FLOAT_EQ(0.35f, rules.classify(info, 1.0f).strength);
}

TEST(TexgenRules, a_rules_file_overrides_in_order_and_round_trips) {
    const char *json = R"({"version": 1, "rules": [
        {"match": "special_*", "class": "special", "strength": 5.0},
        {"match": "big_*", "class": "big", "minSize": 512, "scale": 0.5},
        {"match": "shiny_*", "shiny": true, "class": "metal", "action": "generate"},
        {"match": "*", "class": "everything", "action": "skip"} ]})";
    auto rules = Rules::parse(json);
    ASSERT_EQ(4u, rules.rules().size());
    TextureInfo info;
    info.name = "SPECIAL_one";
    info.width = info.height = 64;
    auto special = rules.classify(info, 2.0f);
    EXPECT_EQ("special", special.className);
    EXPECT_FLOAT_EQ(5.0f, special.strength) << "an absolute strength ignores the base";
    info.name = "big_wall";
    EXPECT_EQ("everything", rules.classify(info, 2.0f).className) << "too small for the size condition";
    info.width = 1024;
    auto big = rules.classify(info, 2.0f);
    EXPECT_EQ("big", big.className);
    EXPECT_FLOAT_EQ(1.0f, big.strength);
    info.name = "shiny_plate";
    EXPECT_EQ("everything", rules.classify(info, 2.0f).className);
    info.shiny = true;
    EXPECT_EQ("metal", rules.classify(info, 2.0f).className);
    info.name = "other";
    EXPECT_EQ(Action::Skip, rules.classify(info, 2.0f).action);

    auto again = Rules::parse(boost::json::serialize(rules.toJson()));
    EXPECT_EQ(rules.fingerprint(), again.fingerprint());
    EXPECT_EQ(Rules::defaults().fingerprint(), Rules::parse(boost::json::serialize(Rules::defaults().toJson())).fingerprint());
    EXPECT_NE(rules.fingerprint(), Rules::defaults().fingerprint());
}

TEST(TexgenRules, refuses_malformed_rules_files) {
    for (const char *bad : {"", "[]", "{}", "{\"rules\": 3}", "{\"rules\": [1]}", "{\"rules\": [{\"class\": \"x\"}]}",
                            "{\"rules\": [{\"match\": \"*\", \"action\": \"explode\"}]}",
                            "{\"rules\": [{\"match\": \"*\", \"strength\": -1}]}",
                            "{\"rules\": [{\"match\": \"*\", \"cutout\": \"yes\"}]}"}) {
        EXPECT_THROW(Rules::parse(bad), ValidationException) << bad;
    }
}

// Manifest ------------------------------------------------------------------------------------------

TEST(TexgenManifest, round_trips_and_notices_damage) {
    Manifest manifest;
    manifest.entries["wall"] = {"wall", "surface", "generated", "", 2.0f, "00ff00ff00ff00ff"};
    manifest.entries["head"] = {"head", "face", "skipped", "some reason", 0.0f, "1234567812345678"};
    auto again = Manifest::fromText(manifest.toText());
    ASSERT_EQ(2u, again.entries.size());
    EXPECT_EQ("generated", again.entries["wall"].status);
    EXPECT_EQ("00ff00ff00ff00ff", again.entries["wall"].sourceHash);
    EXPECT_EQ("some reason", again.entries["head"].reason);
    EXPECT_FLOAT_EQ(2.0f, again.entries["wall"].strength);

    TempDir dir;
    auto path = dir.path() / "m.json";
    EXPECT_TRUE(Manifest::load(path).entries.empty()) << "no file: empty";
    manifest.save(path);
    EXPECT_EQ(2u, Manifest::load(path).entries.size());
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "m.json.tmp"));
    {
        std::ofstream(path) << "{ not json";
    }
    EXPECT_THROW(Manifest::load(path), ValidationException);
    EXPECT_THROW(Manifest::fromText("{\"textures\": [{\"class\": \"x\"}]}"), ValidationException);
}

TEST(TexgenManifest, the_hash_changes_with_the_bytes) {
    Hasher a;
    a.add("hello");
    Hasher b;
    b.add("hello");
    Hasher c;
    c.add("hellp");
    EXPECT_EQ(a.hex(), b.hex());
    EXPECT_NE(a.hex(), c.hex());
    EXPECT_EQ(16u, a.hex().size());
}

// Sources ----------------------------------------------------------------------------------------------

TEST(TexgenSources, finds_textures_in_an_install_in_the_engines_order) {
    TempDir dir;
    auto root = dir.path();
    // A KEY with a BIF, a texture pack, a module archive and the override folder (in odd case).
    test::writeKeyBif(root, "data\\textures.bif",
                      {{"from_bif", ResType::Tga, asString(flatTga(4, 0.1f))},
                       {"shared", ResType::Tga, asString(flatTga(4, 0.2f))},
                       {"bif_notes", ResType::Txi, "envmaptexture x"}});
    std::filesystem::create_directories(root / "TexturePacks");
    auto pack = flatTga(4, 0.3f);
    test::writeErf(root / "TexturePacks" / "swpc_tex_tpa.erf", ErfWriter::FileType::ERF,
                   {{"from_pack", ResType::Tga, std::string(pack.begin(), pack.end())},
                    {"shared", ResType::Tga, std::string(pack.begin(), pack.end())}});
    std::filesystem::create_directories(root / "modules");
    auto mod = flatTga(4, 0.4f);
    test::writeRim(root / "modules" / "m01aa.rim", {{"from_rim", ResType::Tga, std::string(mod.begin(), mod.end())}});
    std::filesystem::create_directories(root / "OVERRIDE");
    auto over = flatTga(8, 0.5f);
    {
        std::ofstream out(root / "OVERRIDE" / "Shared.TGA", std::ios::binary);
        out.write(over.data(), over.size());
        std::ofstream txi(root / "OVERRIDE" / "shared.txi");
        txi << "bumpmaptexture foo\n";
    }

    auto set = scanSources(root);
    for (const auto &w : set.warnings) {
        ADD_FAILURE() << w;
    }
    for (const char *name : {"from_bif", "shared", "from_pack", "from_rim"}) {
        EXPECT_TRUE(set.has(name)) << name;
    }
    EXPECT_EQ(4u, set.textures.size());
    // The override's copy of "shared" wins: it is the 8 by 8 one.
    auto bytes = set.textures.at("shared").read();
    EXPECT_EQ(over, bytes);
    EXPECT_TRUE(set.txis.count("shared"));
    EXPECT_TRUE(set.txis.count("bif_notes"));
    auto notes = set.txis.at("bif_notes")();
    ASSERT_TRUE(notes.has_value());
    EXPECT_EQ(std::string("envmaptexture x"), std::string(notes->begin(), notes->end()));
    // A copy from the texture pack, read back through its archive offsets.
    EXPECT_EQ(pack, set.textures.at("from_pack").read());
}

TEST(TexgenSources, a_single_archive_or_loose_file_can_be_named_and_damage_is_a_warning) {
    TempDir dir;
    auto tga = flatTga(4, 0.2f);
    test::writeErf(dir.path() / "one.erf", ErfWriter::FileType::ERF, {{"inside", ResType::Tga, std::string(tga.begin(), tga.end())}});
    auto set = scanSources(dir.path() / "one.erf");
    EXPECT_TRUE(set.has("inside"));
    {
        std::ofstream out(dir.path() / "loose.tga", std::ios::binary);
        out.write(tga.data(), tga.size());
    }
    EXPECT_TRUE(scanSources(dir.path() / "loose.tga").has("loose"));
    {
        std::ofstream(dir.path() / "broken.erf") << "this is not an archive";
    }
    auto broken = scanSources(dir.path() / "broken.erf");
    EXPECT_TRUE(broken.textures.empty());
    EXPECT_FALSE(broken.warnings.empty());
    EXPECT_FALSE(scanSources(dir.path() / "missing").warnings.empty());
}

// The job ------------------------------------------------------------------------------------------------

namespace {

SourceSet syntheticSources() {
    SourceSet set;
    set.add(memorySource("tex_wall", ResType::Tga, noisyTga(16, 1)));
    set.add(memorySource("tex_flat", ResType::Tga, flatTga(16, 0.5f)));
    set.add(memorySource("p_head01", ResType::Tga, noisyTga(16, 2)));
    set.add(memorySource("fx_beam", ResType::Tga, noisyTga(16, 3)));
    set.add(memorySource("m01_lm0", ResType::Tga, noisyTga(16, 4)));
    set.add(memorySource("tex_bumped", ResType::Tga, noisyTga(16, 5)));
    set.addTxi("tex_bumped", []() -> std::optional<ByteBuffer> {
        std::string text = "bumpmaptexture cm_bump\n";
        return ByteBuffer(text.begin(), text.end());
    });
    set.add(memorySource("tex_with_companion", ResType::Tga, noisyTga(16, 6)));
    auto companion = memorySource("tex_with_companion_n", ResType::Tga, noisyTga(16, 7));
    companion.fromOverride = true; // a map sitting in the override folder is a companion, not a source
    set.add(std::move(companion));
    return set;
}

JobOptions optionsFor(const TempDir &dir, int threads = 2) {
    JobOptions options;
    options.outputDir = dir.path();
    options.threads = threads;
    options.format = OutputFormat::Tga; // the TGA path; the DDS path has its own tests below
    return options;
}

} // namespace

TEST(TexgenJob, treats_a_game_texture_ending_in_m_as_a_source_but_skips_override_companions) {
    TempDir dir;
    SourceSet sources;
    // n_comm_b_m is a real game texture whose name ends in _m.
    sources.add(memorySource("n_comm_b_m", ResType::Tga, noisyTga(16, 21)));
    auto stray = memorySource("tex_stray_m", ResType::Tga, noisyTga(16, 22));
    stray.fromOverride = true;
    sources.add(std::move(stray));

    auto summary = runJob(sources, Rules::defaults(), optionsFor(dir));

    EXPECT_EQ(2u, summary.total);
    EXPECT_EQ(1u, summary.generated);
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "n_comm_b_m_n.tga"));
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "n_comm_b_m_m.tga"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_stray_m_n.tga"));
}

TEST(TexgenJob, generates_what_the_rules_and_the_game_allow_and_writes_a_manifest) {
    TempDir dir;
    auto sources = syntheticSources();
    std::vector<Progress> progress;
    auto summary = runJob(sources, Rules::defaults(), optionsFor(dir), [&](const Progress &p) { progress.push_back(p); });

    EXPECT_EQ(8u, summary.total);
    EXPECT_EQ(0u, summary.failed);
    // Generated: wall, flat, head. Skipped: fx, lightmap, bumped, the one with a game "_n" texture, and that "_n" itself.
    EXPECT_EQ(3u, summary.generated);
    EXPECT_EQ(5u, summary.skipped);
    ASSERT_EQ(8u, progress.size());
    EXPECT_EQ(8u, progress.back().done);
    EXPECT_EQ(8u, progress.back().total);

    for (const char *name : {"tex_wall", "tex_flat", "p_head01"}) {
        EXPECT_TRUE(std::filesystem::exists(dir.path() / (std::string(name) + "_n.tga"))) << name;
        EXPECT_TRUE(std::filesystem::exists(dir.path() / (std::string(name) + "_m.tga"))) << name;
    }
    for (const char *name : {"fx_beam", "m01_lm0", "tex_bumped"}) {
        EXPECT_FALSE(std::filesystem::exists(dir.path() / (std::string(name) + "_n.tga"))) << name;
    }
    // tex_with_companion has a game "_n" texture: the game's own wins.
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_with_companion_n.tga"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_with_companion_m.tga"));

    auto manifest = Manifest::load(dir.path() / kManifestName);
    EXPECT_EQ("generated", manifest.entries.at("tex_wall").status);
    EXPECT_EQ("surface", manifest.entries.at("tex_wall").className);
    EXPECT_FLOAT_EQ(2.0f, manifest.entries.at("tex_wall").strength);
    EXPECT_EQ("face", manifest.entries.at("p_head01").className);
    EXPECT_FLOAT_EQ(0.7f, manifest.entries.at("p_head01").strength);
    EXPECT_EQ("skipped", manifest.entries.at("fx_beam").status);
    EXPECT_NE(std::string::npos, manifest.entries.at("tex_bumped").reason.find("game bump map"));
    EXPECT_NE(std::string::npos, manifest.entries.at("tex_with_companion").reason.find("tex_with_companion_n"));
    EXPECT_FALSE(manifest.entries.at("tex_wall").sourceHash.empty());

    // The maps are real TGAs of the texture's size; the flat texture gets a flat normal map.
    auto flatNormals = readTga(dir.path() / "tex_flat_n.tga");
    EXPECT_EQ(16, flatNormals.width);
    EXPECT_NEAR(0.5f, flatNormals.at(8, 8)[0], 1.0f / 255.0f);
    EXPECT_NEAR(1.0f, flatNormals.at(8, 8)[2], 1.0f / 255.0f);
    auto wallMaterial = readTga(dir.path() / "tex_wall_m.tga");
    EXPECT_EQ(16, wallMaterial.height);
    EXPECT_GT(wallMaterial.at(8, 8)[0], 0.3f) << "roughness";
}

TEST(TexgenJob, a_second_run_does_nothing_until_something_changes) {
    TempDir dir;
    auto sources = syntheticSources();
    runJob(sources, Rules::defaults(), optionsFor(dir));

    auto second = runJob(sources, Rules::defaults(), optionsFor(dir));
    EXPECT_EQ(0u, second.generated);
    EXPECT_EQ(7u, second.unchanged) << "skipped textures are remembered too (but not a name that is a companion map)";

    // A changed source texture is regenerated, and only that one.
    sources.textures.erase("tex_wall");
    sources.add(memorySource("tex_wall", ResType::Tga, noisyTga(16, 99)));
    auto third = runJob(sources, Rules::defaults(), optionsFor(dir));
    EXPECT_EQ(1u, third.generated);
    EXPECT_EQ(6u, third.unchanged);

    // A deleted output is made again.
    std::filesystem::remove(dir.path() / "tex_flat_m.tga");
    auto fourth = runJob(sources, Rules::defaults(), optionsFor(dir));
    EXPECT_EQ(1u, fourth.generated);
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_flat_m.tga"));

    // A different strength, or different rules, redo everything affected.
    auto stronger = optionsFor(dir);
    stronger.baseStrength = 4.0f;
    auto fifth = runJob(sources, Rules::defaults(), stronger);
    EXPECT_EQ(0u, fifth.unchanged) << "the settings are part of the hash";
    EXPECT_EQ(3u, fifth.generated);
    Rules rules;
    Rule everything;
    everything.match = "p_*";
    everything.className = "face";
    everything.action = Action::Skip;
    rules.add(everything);
    auto sixth = runJob(sources, rules, stronger);
    EXPECT_EQ(0u, sixth.unchanged);
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "p_head01_n.tga")) << "a texture that now gets no maps gives its old ones up";
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_wall_n.tga"));

    // --force redoes the lot.
    auto forced = stronger;
    forced.force = true;
    auto seventh = runJob(sources, rules, forced);
    EXPECT_EQ(0u, seventh.unchanged);
}

TEST(TexgenJob, the_result_does_not_depend_on_the_thread_count) {
    TempDir one;
    TempDir many;
    auto sources = syntheticSources();
    runJob(sources, Rules::defaults(), optionsFor(one, 1));
    runJob(sources, Rules::defaults(), optionsFor(many, 6));
    for (const auto &entry : std::filesystem::directory_iterator(one.path())) {
        auto other = many.path() / entry.path().filename();
        ASSERT_TRUE(std::filesystem::exists(other)) << entry.path();
        std::ifstream a(entry.path(), std::ios::binary);
        std::ifstream b(other, std::ios::binary);
        std::string x((std::istreambuf_iterator<char>(a)), std::istreambuf_iterator<char>());
        std::string y((std::istreambuf_iterator<char>(b)), std::istreambuf_iterator<char>());
        EXPECT_EQ(x, y) << entry.path().filename();
    }
}

TEST(TexgenJob, a_broken_texture_fails_alone_and_is_tried_again_next_time) {
    TempDir dir;
    auto sources = syntheticSources();
    sources.add(memorySource("tex_broken", ResType::Tga, ByteBuffer {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20}));
    std::vector<Progress> failures;
    auto summary = runJob(sources, Rules::defaults(), optionsFor(dir), [&](const Progress &p) {
        if (p.outcome == Outcome::Failed) {
            failures.push_back(p);
        }
    });
    EXPECT_EQ(1u, summary.failed);
    EXPECT_EQ(3u, summary.generated);
    ASSERT_EQ(1u, failures.size());
    EXPECT_EQ("tex_broken", failures[0].texture);
    EXPECT_FALSE(failures[0].detail.empty());
    EXPECT_EQ(0u, Manifest::load(dir.path() / kManifestName).entries.count("tex_broken"));
}

TEST(TexgenJob, only_and_dry_run_limit_what_is_looked_at_and_written) {
    TempDir dir;
    auto sources = syntheticSources();
    auto options = optionsFor(dir);
    options.only = "tex_w*";
    auto summary = runJob(sources, Rules::defaults(), options);
    EXPECT_EQ(3u, summary.total) << "tex_wall, tex_with_companion and its _n";
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_wall_n.tga"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_flat_n.tga"));

    TempDir dry;
    auto dryOptions = optionsFor(dry);
    dryOptions.dryRun = true;
    auto dryRun = runJob(sources, Rules::defaults(), dryOptions);
    EXPECT_EQ(3u, dryRun.generated) << "what would be made";
    EXPECT_TRUE(std::filesystem::is_empty(dry.path())) << "nothing is written";
}

TEST(TexgenJob, reads_tpc_textures_with_their_embedded_txi) {
    // A 4 by 4 TPC whose TXI names an environment map: the material map gets a metalness hint.
    ByteBuffer rgb(4 * 4 * 3, char(128));
    SourceSet set;
    set.add(memorySource("shiny_floor", ResType::Tpc, tpcBytes(4, 4, rgb, "envmaptexture cm_sky\n")));
    set.add(memorySource("plain_floor", ResType::Tpc, tpcBytes(4, 4, rgb)));
    set.add(memorySource("bumpy_floor", ResType::Tpc, tpcBytes(4, 4, rgb, "bumpmaptexture some_bump\n")));
    set.add(memorySource("decal_thing", ResType::Tpc, tpcBytes(4, 4, rgb, "decal 1\n")));
    TempDir dir;
    auto summary = runJob(set, Rules::defaults(), optionsFor(dir));
    EXPECT_EQ(0u, summary.failed);
    EXPECT_EQ(2u, summary.generated);
    EXPECT_EQ(2u, summary.skipped);
    auto shiny = readTga(dir.path() / "shiny_floor_m.tga");
    auto plain = readTga(dir.path() / "plain_floor_m.tga");
    EXPECT_GT(shiny.at(2, 2)[1], 0.5f);
    EXPECT_LT(plain.at(2, 2)[1], 0.05f);
    EXPECT_LT(shiny.at(2, 2)[0], plain.at(2, 2)[0]);
    auto manifest = Manifest::load(dir.path() / kManifestName);
    EXPECT_NE(std::string::npos, manifest.entries.at("bumpy_floor").reason.find("some_bump"));
    EXPECT_EQ("decal", manifest.entries.at("decal_thing").className);
}

TEST(TexgenJob, writes_dds_maps_by_default_and_they_load_as_bc5_and_bc7) {
    TempDir dir;
    SourceSet sources;
    sources.add(memorySource("tex_wall", ResType::Tga, noisyTga(32, 5)));
    JobOptions options;
    options.outputDir = dir.path();
    options.threads = 1;
    EXPECT_EQ(OutputFormat::Dds, options.format) << "dds is the default";

    auto summary = runJob(sources, Rules::defaults(), options);
    ASSERT_EQ(1u, summary.generated);
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_wall_n.dds"));
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_wall_m.dds"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_wall_n.tga"));

    auto load = [&](const char *name) {
        std::ifstream file(dir.path() / name, std::ios::binary);
        ByteBuffer bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        MemoryInputStream stream(bytes);
        graphics::DdsReader reader(stream, name, TextureUsage::BumpMap, true);
        reader.load();
        return reader.texture();
    };
    auto normal = load("tex_wall_n.dds");
    auto material = load("tex_wall_m.dds");
    EXPECT_EQ(PixelFormat::BC5, normal->pixelFormat());
    EXPECT_EQ(PixelFormat::BC7, material->pixelFormat());
    EXPECT_EQ(32, normal->width());
    EXPECT_EQ(5u, normal->layers().front().mipMaps.size());
}

TEST(TexgenJob, switching_formats_replaces_the_other_files_and_regenerates) {
    TempDir dir;
    SourceSet sources;
    sources.add(memorySource("tex_wall", ResType::Tga, noisyTga(16, 5)));
    JobOptions dds;
    dds.outputDir = dir.path();
    dds.threads = 1;
    ASSERT_EQ(1u, runJob(sources, Rules::defaults(), dds).generated);

    auto tga = dds;
    tga.format = OutputFormat::Tga;
    auto summary = runJob(sources, Rules::defaults(), tga);
    EXPECT_EQ(1u, summary.generated) << "another format is not up to date";
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "tex_wall_n.tga"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_wall_n.dds")) << "a stale DDS would shadow the new TGA in the engine";
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_wall_m.dds"));

    EXPECT_EQ(1u, runJob(sources, Rules::defaults(), tga).unchanged);
    EXPECT_EQ(1u, runJob(sources, Rules::defaults(), dds).generated);
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "tex_wall_n.tga"));

    auto bc1 = dds;
    bc1.materialCodec = MaterialCodec::BC1;
    EXPECT_EQ(1u, runJob(sources, Rules::defaults(), bc1).generated) << "another material codec regenerates";
}
