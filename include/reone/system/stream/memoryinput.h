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

#include "input.h"

namespace reone {

class MemoryInputStream : public IInputStream {
public:
    MemoryInputStream(std::string &str) :
        _data(!str.empty() ? &str[0] : nullptr),
        _length(str.length()) {
    }

    MemoryInputStream(ByteBuffer &bytes) :
        _data(!bytes.empty() ? &bytes[0] : nullptr),
        _length(bytes.size()) {
    }

    MemoryInputStream(char *data, size_t length) :
        _data(data), _length(length) {
    }

    void seek(int64_t off, SeekOrigin origin) override {
        int64_t base;
        if (origin == SeekOrigin::Begin) {
            base = 0;
        } else if (origin == SeekOrigin::Current) {
            base = static_cast<int64_t>(_position);
        } else if (origin == SeekOrigin::End) {
            base = static_cast<int64_t>(_length);
            off = -off;
        } else {
            throw std::invalid_argument("Invalid origin: " + std::to_string(static_cast<int>(origin)));
        }
        // Positions outside the buffer are clamped to it, so a later read sees
        // end of stream instead of an out-of-range offset.
        int64_t target = base + off;
        if (target < 0) {
            target = 0;
        } else if (static_cast<uint64_t>(target) > _length) {
            target = static_cast<int64_t>(_length);
        }
        _position = static_cast<size_t>(target);
    }

    int readByte() override {
        if (_position >= _length) {
            return -1;
        }
        return _data[_position++];
    }

    int read(char *buf, int length) override;

    size_t position() override { return _position; }
    size_t length() override { return _length; }

protected:
    char *_data;
    size_t _length;

    size_t _position {0};
};

} // namespace reone
