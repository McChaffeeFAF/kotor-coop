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

#pragma once

#include "reone/system/types.h"

#include "types.h"

namespace reone {

namespace graphics {

/** Bytes per 4x4 block of a block-compressed format (8 for BC1, 16 for BC3, BC5 and BC7); 0 for other formats. */
int blockBytes(PixelFormat format);

/** Bytes of one mip level of a block-compressed format, rounding the size up to whole blocks. */
size_t compressedLevelSize(PixelFormat format, int width, int height);

/**
 * Decodes one level of BC1 (DXT1), BC3 (DXT5), BC5 or BC7 data to RGBA8, rows in the order
 * they are stored. BC5 gives (r, g, 0, 255), as a GPU samples it. Throws ValidationException
 * for another format or for data shorter than the level needs.
 */
std::vector<uint8_t> decodeBlockCompressed(PixelFormat format, int width, int height, const uint8_t *data, size_t size);

} // namespace graphics

} // namespace reone
