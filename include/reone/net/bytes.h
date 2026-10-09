/* Copyright (c) 2026 The reone project contributors */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace reone {

namespace net {

/**
 * Little-endian writer for co-op wire messages.
 *
 * Every field has a fixed width, so a message decodes the same on every
 * platform and compiler. Strings are a u16 length followed by the bytes.
 */
class ByteWriter {
public:
    void u8(uint8_t value) { _bytes.push_back(value); }
    void boolean(bool value) { u8(value ? 1 : 0); }
    void u16(uint16_t value);
    void u32(uint32_t value);
    void u64(uint64_t value);
    void i16(int16_t value) { u16(static_cast<uint16_t>(value)); }
    void i32(int32_t value) { u32(static_cast<uint32_t>(value)); }
    void f32(float value);
    void str(const std::string &value);

    size_t size() const { return _bytes.size(); }
    const std::vector<uint8_t> &bytes() const { return _bytes; }
    std::vector<uint8_t> take() { return std::move(_bytes); }

    /** Overwrite a u16 written earlier, e.g. a count known only at the end. */
    void patchU16(size_t offset, uint16_t value);

private:
    std::vector<uint8_t> _bytes;
};

/**
 * Bounds-checked reader. A read past the end, or a string longer than the
 * limit, puts the reader in the failed state: every later read returns zero
 * and ok() stays false. Decoders read everything, then check ok() once.
 */
class ByteReader {
public:
    ByteReader(const uint8_t *data, size_t size, size_t maxString = 256) :
        _data(data),
        _size(size),
        _maxString(maxString) {
    }

    explicit ByteReader(const std::vector<uint8_t> &bytes, size_t maxString = 256) :
        ByteReader(bytes.data(), bytes.size(), maxString) {
    }

    uint8_t u8();
    bool boolean();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int16_t i16() { return static_cast<int16_t>(u16()); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    /** A non-finite float fails the reader: no NaN reaches the game. */
    float f32();
    std::string str();

    bool ok() const { return !_failed; }
    bool atEnd() const { return _offset == _size; }
    size_t remaining() const { return _failed ? 0 : _size - _offset; }
    void fail() { _failed = true; }

private:
    const uint8_t *_data;
    size_t _size;
    size_t _maxString;
    size_t _offset {0};
    bool _failed {false};

    bool need(size_t count);
};

/** FNV-1a, 64-bit. Stable across builds; used for file fingerprints. */
class Fnv1a64 {
public:
    void update(const void *data, size_t size);
    void update(const std::string &text) { update(text.data(), text.size()); }
    void updateU64(uint64_t value);
    uint64_t value() const { return _hash; }

private:
    uint64_t _hash {14695981039346656037ull};
};

} // namespace net

} // namespace reone
