#pragma once

#include "Internal.hpp"
#include <vector>

namespace dbuf {

struct SpanReadSource {
    SpanReadSource(std::span<const uint8_t> data) : m_data(data) {}
    SpanReadSource(const uint8_t* data, size_t size) : m_data(data, size) {}
    SpanReadSource(const std::vector<uint8_t>& data) : m_data(data) {}

    Result<void> read(uint8_t* buf, size_t size) {
        if (size > m_data.size() - m_pos) {
            return Err("Not enough data to read");
        }

        std::copy(m_data.begin() + m_pos, m_data.begin() + m_pos + size, buf);
        m_pos += size;
        return Ok();
    }

    Result<void> skip(size_t size) {
        if (size > m_data.size() - m_pos) {
            return Err("Not enough data to skip");
        }
        m_pos += size;
        return Ok();
    }

    size_t position() const { return m_pos; }
    size_t totalSize() const { return m_data.size(); }

    Result<void> setPosition(size_t pos) {
        if (pos >= m_data.size()) {
            return Err("Position out of bounds");
        }
        m_pos = pos;
        return Ok();
    }

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
};

template <ReadSource S = SpanReadSource>
class ByteReader {
public:
    ByteReader(S source) : m_source(std::move(source)) {}

    ByteReader(const ByteReader& other) = default;
    ByteReader& operator=(const ByteReader& other) = default;
    ByteReader(ByteReader&& other) noexcept = default;
    ByteReader& operator=(ByteReader&& other) noexcept = default;

    /// Reads bytes from the source into the given buffer.
    /// Returns an error if there is not enough data to read.
    Result<void> readBytes(uint8_t* data, size_t size) {
        return m_source.read(data, size);
    }

    /// Skips the given number of bytes in the source.
    /// Returns an error if there is not enough data to skip.
    Result<void> skip(size_t size) {
        if constexpr (SkipSource<S>) {
            return m_source.skip(size);
        } else {
            uint8_t buf[1024];
            while (size > 0) {
                size_t toRead = std::min(size, sizeof(buf));
                auto result = readBytes(buf, toRead);
                if (!result) return result;

                size -= toRead;
            }
            return Ok();
        }
    }

    /// Read methods - all in native endianness

    Result<uint8_t> readU8() { return readBytesAs<uint8_t>(); }
    Result<uint16_t> readU16() { return readBytesAs<uint16_t>(); }
    Result<uint32_t> readU32() { return readBytesAs<uint32_t>(); }
    Result<uint64_t> readU64() { return readBytesAs<uint64_t>(); }
    Result<int8_t> readI8() { return readBytesAs<int8_t>(); }
    Result<int16_t> readI16() { return readBytesAs<int16_t>(); }
    Result<int32_t> readI32() { return readBytesAs<int32_t>(); }
    Result<int64_t> readI64() { return readBytesAs<int64_t>(); }
    Result<float> readF32() { return readBytesAs<float>(); }
    Result<double> readF64() { return readBytesAs<double>(); }
    Result<float> readFloat() { return readBytesAs<float>(); }
    Result<double> readDouble() { return readBytesAs<double>(); }

    Result<bool> readBool() {
        return this->readU8().map([](uint8_t v) { return v != 0; });
    }

    /// Read a LEB128 signed variable length integer
    Result<int64_t> readVarInt() {
        int64_t value = 0;
        int shift = 0;
        int size = 64;
        uint8_t byte;

        while (true) {
            GEODE_UNWRAP_INTO(byte, this->readU8());

            if (shift >= 63 && byte != 0 && byte != 1) {
                return Err("Varint exceeds 63 bits");
            }

            value |= (uint64_t)(byte & 0x7f) << shift;
            shift += 7;

            if ((byte & 0x80) == 0) {
                break;
            }
        }

        if (shift < size && (byte & 0x40) != 0) {
            value |= (static_cast<uint64_t>(-1) << shift);
        }

        return Ok(value);
    }

    /// Read a ULEB128 unsigned variable length integer
    Result<uint64_t> readVarUint() {
        uint64_t value = 0;
        size_t shift = 0;

        while (true) {
            GEODE_UNWRAP_INTO(uint8_t byte, this->readU8());

            if (shift == 63 && byte > 1) {
                return Err("Varuint exceeds 64 bits");
            }

            value |= (static_cast<uint64_t>(byte & 0x7f) << shift);

            if (!(byte & 0x80)) {
                return Ok(value);
            }

            shift += 7;
        }
    }

    /// Read a string with the length encoded as a ULEB128 (varuint) prefix
    Result<std::string> readStringVar() {
        GEODE_UNWRAP_INTO(auto res, this->readVarUint());
        return readFixedString(res);
    }

    /// Read a string with the length encoded as a single byte (uint8_t) prefix.
    /// Returns an error if string is longer than 255 bytes.
    Result<std::string> readStringU8() {
        GEODE_UNWRAP_INTO(auto res, this->readU8());
        return readFixedString(res);
    }

    /// Read a string with the length encoded as a uint16_t prefix.
    /// Returns an error if string is longer than 2^16 - 1 bytes.
    Result<std::string> readStringU16() {
        GEODE_UNWRAP_INTO(auto res, this->readU16());
        return readFixedString(res);
    }

    /// Read a string with the length encoded as a uint32_t prefix.
    /// Returns an error if string is longer than 2^32 - 1 bytes.
    Result<std::string> readStringU32() {
        GEODE_UNWRAP_INTO(auto res, this->readU32());
        return readFixedString(res);
    }

    /// Alias to `readStringU16`
    Result<std::string> readString() {
        return readStringU16();
    }

    /// Reads a string of a fixed length, does not expect any prefix.
    Result<std::string> readFixedString(size_t len) {
        std::string out(len, '\0');
        GEODE_UNWRAP(this->readBytes((uint8_t*)out.data(), len));

        return Ok(std::move(out));
    }

    /// SeekSource-specific methods

    std::vector<uint8_t> readToEnd() requires (SeekSource<S>) {
        std::vector<uint8_t> out(remainingSize());
        (void) this->readBytes(out.data(), out.size());
        return out;
    }

    size_t remainingSize() const requires (SeekSource<S>) {
        return m_source.totalSize() - m_source.position();
    }

    size_t position() const requires (SeekSource<S>) {
        return m_source.position();
    }

    Result<void> setPosition(size_t pos) requires (SeekSource<S>) {
        return m_source.setPosition(pos);
    }


    S& source() { return m_source; }
    const S& source() const { return m_source; }

private:
    S m_source;

    template <typename T>
    Result<T> readBytesAs() {
        T out;
        GEODE_UNWRAP(this->readBytes((uint8_t*)&out, sizeof(out)));
        return Ok(out);
    }
};

}
