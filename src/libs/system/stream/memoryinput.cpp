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

#include "reone/system/stream/memoryinput.h"

namespace reone {

int MemoryInputStream::read(char *outData, int length) {
    if (length <= 0 || _position >= _length) {
        return 0;
    }
    size_t numRead = std::min(_length - _position, static_cast<size_t>(length));
    std::memcpy(outData, &_data[_position], numRead);
    _position += numRead;
    return static_cast<int>(numRead);
}

} // namespace reone
