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

#include "image.h"

namespace reone {

namespace texgen {

enum class OutputFormat {
    Tga, ///< 24-bit TGA, no mip maps
    Dds  ///< BC5 normal maps and BC7 (or BC1) material maps with full mip chains
};

enum class MaterialCodec {
    BC7, ///< near-lossless for smooth maps; needs a GPU with BPTC or a CPU decode at load
    BC1  ///< half the size of BC7, visibly coarser on gradients
};

/**
 * The DDS file of a tangent-space normal map: BC5 holding x and y (the shader rebuilds z),
 * with a mip chain whose levels are averaged as vectors and renormalised. Rows are written in
 * the order of Image (the first row is v = 0), as the engine uploads textures.
 */
ByteBuffer encodeNormalDds(const Image &normalMap);

/** The DDS file of a material map (red and green are the data, blue is carried too), BC7 or BC1, with a mip chain. */
ByteBuffer encodeMaterialDds(const Image &materialMap, MaterialCodec codec = MaterialCodec::BC7);

/** The mip chain of an image: level 0 is the image, each next level a 2x2 box average; `normals` renormalises xyz as a vector. */
std::vector<Image> buildMipChain(const Image &image, bool normals);

} // namespace texgen

} // namespace reone
