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

#include "reone/texgen/compress.h"

#include <mutex>

#include "reone/graphics/bcutil.h"
#include "reone/graphics/format/ddswriter.h"

#include "bc7enc.h"
#include "rgbcx.h"

using namespace reone::graphics;

namespace reone {

namespace texgen {

namespace {

void initEncoders() {
    static std::once_flag once;
    std::call_once(once, []() {
        rgbcx::init();
        bc7enc_compress_block_init();
    });
}

Image downsample(const Image &src, bool normals) {
    Image dst(std::max(1, src.width / 2), std::max(1, src.height / 2));
    for (int y = 0; y < dst.height; ++y) {
        for (int x = 0; x < dst.width; ++x) {
            float sum[4] = {0, 0, 0, 0};
            int n = 0;
            // An odd edge pixel is folded into the last block rather than dropped.
            int x0 = x * 2, x1 = std::min(src.width - 1, x * 2 + 1);
            int y0 = y * 2, y1 = std::min(src.height - 1, y * 2 + 1);
            if (x == dst.width - 1) {
                x1 = src.width - 1;
            }
            if (y == dst.height - 1) {
                y1 = src.height - 1;
            }
            for (int sy = y0; sy <= y1; ++sy) {
                for (int sx = x0; sx <= x1; ++sx) {
                    const float *p = src.at(sx, sy);
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += normals && c < 3 ? p[c] * 2.0f - 1.0f : p[c];
                    }
                    ++n;
                }
            }
            float *out = dst.at(x, y);
            if (normals) {
                float len = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                for (int c = 0; c < 3; ++c) {
                    out[c] = len > 1e-6f ? (sum[c] / len) * 0.5f + 0.5f : (c == 2 ? 1.0f : 0.5f);
                }
                out[3] = sum[3] / n;
            } else {
                for (int c = 0; c < 4; ++c) {
                    out[c] = sum[c] / n;
                }
            }
        }
    }
    return dst;
}

// 4x4 block of RGBA8 at block (bx, by), edge pixels repeated past the image.
void extractBlock(const Image &image, int bx, int by, uint8_t out[64]) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const float *p = image.at(std::min(image.width - 1, bx * 4 + x), std::min(image.height - 1, by * 4 + y));
            for (int c = 0; c < 4; ++c) {
                out[(y * 4 + x) * 4 + c] = static_cast<uint8_t>(std::lround(std::clamp(p[c], 0.0f, 1.0f) * 255.0f));
            }
        }
    }
}

ByteBuffer encodeLevel(const Image &image, PixelFormat format) {
    int blocksX = (image.width + 3) / 4;
    int blocksY = (image.height + 3) / 4;
    int bytes = blockBytes(format);
    ByteBuffer out(static_cast<size_t>(blocksX) * blocksY * bytes);
    bc7enc_compress_block_params params;
    bc7enc_compress_block_params_init(&params);
    bc7enc_compress_block_params_init_linear_weights(&params);
    params.m_max_partitions = 16; // plenty for smooth maps, and 4x faster than the default
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            uint8_t px[64];
            extractBlock(image, bx, by, px);
            void *dst = &out[(static_cast<size_t>(by) * blocksX + bx) * bytes];
            switch (format) {
            case PixelFormat::BC5:
                rgbcx::encode_bc5_hq(dst, px, 0, 1, 4);
                break;
            case PixelFormat::DXT1:
                rgbcx::encode_bc1(rgbcx::MAX_LEVEL / 2, dst, px, false, false);
                break;
            default:
                bc7enc_compress_block(dst, px, &params);
                break;
            }
        }
    }
    return out;
}

ByteBuffer encode(const Image &image, bool normals, PixelFormat format) {
    initEncoders();
    std::vector<ByteBuffer> levels;
    for (const auto &level : buildMipChain(image, normals)) {
        levels.push_back(encodeLevel(level, format));
    }
    return writeDds(format, image.width, image.height, levels);
}

} // namespace

std::vector<Image> buildMipChain(const Image &image, bool normals) {
    std::vector<Image> chain {image};
    while (chain.back().width > 1 || chain.back().height > 1) {
        chain.push_back(downsample(chain.back(), normals));
    }
    return chain;
}

ByteBuffer encodeNormalDds(const Image &normalMap) {
    return encode(normalMap, true, PixelFormat::BC5);
}

ByteBuffer encodeMaterialDds(const Image &materialMap, MaterialCodec codec) {
    return encode(materialMap, false, codec == MaterialCodec::BC1 ? PixelFormat::DXT1 : PixelFormat::BC7);
}

} // namespace texgen

} // namespace reone
