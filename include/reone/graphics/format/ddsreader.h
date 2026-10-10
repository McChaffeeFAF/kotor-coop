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

#include "reone/system/stream/input.h"

#include "../types.h"

namespace reone {

namespace graphics {

class Texture;

/**
 * Reads the DDS files the surface-detail folder holds: one 2D image in BC1 (DXT1), BC3 (DXT5),
 * BC5 or BC7, with its mip chain. DX10-header and legacy FourCC files are both accepted.
 * Rows are taken in the order the file stores them: texgen writes them in the order the
 * engine uploads textures (the first row is v = 0).
 *
 * Anything else (uncompressed or other block formats, cube maps, volumes, arrays, a
 * truncated or inconsistent file) throws ValidationException; nothing is read past the
 * end of the data.
 */
class DdsReader : boost::noncopyable {
public:
    /**
     * \param nativeBC7 whether the GPU takes BC7 as it is; when false a BC7 image is decoded
     *        to RGBA8 on the CPU (every level).
     */
    DdsReader(IInputStream &dds, std::string resRef, TextureUsage usage, bool nativeBC7) :
        _dds(dds),
        _resRef(std::move(resRef)),
        _usage(usage),
        _nativeBC7(nativeBC7) {
    }

    void load();

    std::shared_ptr<Texture> texture() const { return _texture; }

private:
    IInputStream &_dds;
    std::string _resRef;
    TextureUsage _usage;
    bool _nativeBC7;

    std::shared_ptr<Texture> _texture;
};

} // namespace graphics

} // namespace reone
