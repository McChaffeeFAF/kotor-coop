/* Copyright (c) 2026 The reone project contributors */

#include "reone/net/bytes.h"

#include <cmath>
#include <cstring>

namespace reone {

namespace net {

void ByteWriter::u16(uint16_t value) {
    _bytes.push_back(static_cast<uint8_t>(value & 0xff));
    _bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void ByteWriter::u32(uint32_t value) {
    for (int i = 0; i < 4; ++i) {
        _bytes.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void ByteWriter::u64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        _bytes.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void ByteWriter::f32(float value) {
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    u32(bits);
}

void ByteWriter::str(const std::string &value) {
    size_t length = value.size() > 0xffff ? 0xffff : value.size();
    u16(static_cast<uint16_t>(length));
    _bytes.insert(_bytes.end(), value.begin(), value.begin() + length);
}

void ByteWriter::patchU16(size_t offset, uint16_t value) {
    if (offset + 2 > _bytes.size()) {
        return;
    }
    _bytes[offset] = static_cast<uint8_t>(value & 0xff);
    _bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

bool ByteReader::need(size_t count) {
    if (_failed || _size - _offset < count) {
        _failed = true;
        return false;
    }
    return true;
}

uint8_t ByteReader::u8() {
    if (!need(1)) {
        return 0;
    }
    return _data[_offset++];
}

bool ByteReader::boolean() {
    uint8_t value = u8();
    if (value > 1) {
        _failed = true;
        return false;
    }
    return value == 1;
}

uint16_t ByteReader::u16() {
    if (!need(2)) {
        return 0;
    }
    uint16_t value = static_cast<uint16_t>(_data[_offset] | (_data[_offset + 1] << 8));
    _offset += 2;
    return value;
}

uint32_t ByteReader::u32() {
    if (!need(4)) {
        return 0;
    }
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(_data[_offset + i]) << (8 * i);
    }
    _offset += 4;
    return value;
}

uint64_t ByteReader::u64() {
    if (!need(8)) {
        return 0;
    }
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(_data[_offset + i]) << (8 * i);
    }
    _offset += 8;
    return value;
}

float ByteReader::f32() {
    uint32_t bits = u32();
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) {
        _failed = true;
        return 0.0f;
    }
    return value;
}

std::string ByteReader::str() {
    uint16_t length = u16();
    if (_failed) {
        return std::string();
    }
    if (length > _maxString || !need(length)) {
        _failed = true;
        return std::string();
    }
    std::string value(reinterpret_cast<const char *>(_data + _offset), length);
    _offset += length;
    return value;
}

void Fnv1a64::update(const void *data, size_t size) {
    auto bytes = static_cast<const uint8_t *>(data);
    uint64_t hash = _hash;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    _hash = hash;
}

void Fnv1a64::updateU64(uint64_t value) {
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) {
        bytes[i] = static_cast<uint8_t>((value >> (8 * i)) & 0xff);
    }
    update(bytes, sizeof(bytes));
}

} // namespace net

} // namespace reone
