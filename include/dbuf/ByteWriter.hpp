#pragma once

#include "Internal.hpp"
#include <cstring>
#include <vector>
#include <array>

namespace dbuf {

/// Heap sink, uses a vector as the underlying storage.
/// Grows automatically, impls WriteSink, WriteZeroesSink, SeekWriteSink.
/// This sink overwrites data in-place, and does not shift the buffer if attempting to write in the middle.
struct HeapSink {
    void write(const uint8_t* data, size_t size) {
        size_t endPos = m_pos + size;

        // grow if there isn't enough space
        if (endPos > m_buffer.size()) {
            m_buffer.resize(endPos);
        }

        std::memcpy(m_buffer.data() + m_pos, data, size);
        m_pos = endPos;
    }

    void writeZeroes(size_t size) {
        size_t endPos = m_pos + size;

        if (endPos > m_buffer.size()) {
            m_buffer.resize(endPos);
        }

        std::memset(m_buffer.data() + m_pos, 0, size);
        m_pos = endPos;
    }

    Result<> setPosition(size_t pos) {
        if (pos > m_buffer.size()) {
            return Err("Position out of bounds");
        }
        m_pos = pos;
        return Ok();
    }

    size_t position() const { return m_pos; }

    std::span<const uint8_t> slice(size_t pos, size_t size) const {
        if (pos + size > m_buffer.size()) {
            return std::span<const uint8_t>{};
        }

        return std::span<const uint8_t>(m_buffer.data() + pos, size);
    }

    std::span<const uint8_t> written() const {
        return m_buffer;
    }

    std::vector<uint8_t> intoInner() && {
        return std::move(m_buffer);
    }

private:
    std::vector<uint8_t> m_buffer;
    size_t m_pos = 0;
};

/// Array sink, uses a fixed-size array as the underlying storage.
/// Does not grow, impls TryWriteSink, WriteZeroesSink, SeekWriteSink.
/// This sink overwrites data in-place, and does not shift the buffer if attempting to write in the middle.
template <size_t N = 1024>
struct ArraySink {
    Result<> write(const uint8_t* data, size_t size) {
        if (m_pos + size > N) {
            return Err("Not enough space to write");
        }

        std::memcpy(m_buffer.data() + m_pos, data, size);
        m_pos += size;
        m_written = std::max(m_written, m_pos);

        return Ok();
    }

    void writeZeroes(size_t size) {
        std::memset(m_buffer.data() + m_pos, 0, size);
        m_pos += size;
        m_written = std::max(m_written, m_pos);
    }

    Result<> setPosition(size_t pos) {
        if (pos > m_written) {
            return Err("Position out of bounds");
        }
        m_pos = pos;
        return Ok();
    }

    size_t position() const { return m_pos; }

    std::span<const uint8_t> slice(size_t pos, size_t size) const {
        if (pos + size > m_written) {
            return std::span<const uint8_t>{};
        }

        return std::span<const uint8_t>(m_buffer.data() + pos, size);
    }

    std::span<const uint8_t> written() const {
        return std::span<const uint8_t>(m_buffer.data(), m_written);
    }

    std::array<uint8_t, N> intoInner() && {
        return m_buffer;
    }

private:
    std::array<uint8_t, N> m_buffer;
    size_t m_pos = 0;
    size_t m_written = 0;
};

template <AnyWriteSink S = HeapSink>
class ByteWriter {
public:
    static constexpr bool IsTry = TryWriteSink<S>;

    ByteWriter(S sink = S{}) : m_sink(std::move(sink)) {}

    ByteWriter(const ByteWriter& other) = default;
    ByteWriter& operator=(const ByteWriter& other) = default;
    ByteWriter(ByteWriter&& other) noexcept = default;
    ByteWriter& operator=(ByteWriter&& other) noexcept = default;

    S& sink() { return m_sink; }
    const S& sink() const { return m_sink; }

    auto writeBytes(const uint8_t* data, size_t size) {
        return m_sink.write(data, size);
    }

    auto writeBytes(std::span<const uint8_t> data) {
        return m_sink.write(data.data(), data.size());
    }

    auto writeZeroes(size_t size) requires WriteZeroesSink<S> {
        return m_sink.writeZeroes(size);
    }

    auto writeU8(uint8_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeU16(uint16_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeU32(uint32_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeU64(uint64_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeI8(int8_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeI16(int16_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeI32(int32_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeI64(int64_t value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeBool(bool value) {
        return this->writeU8(value ? 1 : 0);
    }

    auto writeF32(float value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeF64(double value) {
        return this->writeBytes((const uint8_t*)&value, sizeof(value));
    }

    auto writeFloat(float value) {
        return this->writeF32(value);
    }

    auto writeDouble(double value) {
        return this->writeF64(value);
    }

    Result<> writeVarInt(int64_t value) {
        return Err("varing encoding not yet implemented");
    }

    Result<> writeVarUint(uint64_t value) {
        size_t written = 0;

        while (true) {
            uint8_t byte = value & 0x7f;
            value >>= 7;

            if (value != 0) {
                // set continuation bit
                byte |= 0x80;
            }

            if constexpr (IsTry) {
                GEODE_UNWRAP(this->writeU8(byte));
            } else {
                this->writeU8(byte);
            }

            written++;

            if (value == 0) {
                break;
            }
        }

        return Ok();
    }

    auto writeStringVar(std::string_view str) {
        if constexpr (IsTry) {
            GEODE_UNWRAP(this->writeVarUint(str.size()));
            return this->writeBytes((const uint8_t*)str.data(), str.size());
        } else {
            this->writeVarUint(str.size());
            this->writeBytes((const uint8_t*)str.data(), str.size());
        }
    }

    auto writeStringU8(std::string_view str) {
        if constexpr (IsTry) {
            GEODE_UNWRAP(this->writeU8(str.size()));
            return this->writeBytes((const uint8_t*)str.data(), str.size());
        } else {
            this->writeU8(str.size());
            this->writeBytes((const uint8_t*)str.data(), str.size());
        }
    }

    auto writeStringU16(std::string_view str) {
        if constexpr (IsTry) {
            GEODE_UNWRAP(this->writeU16(str.size()));
            return this->writeBytes((const uint8_t*)str.data(), str.size());
        } else {
            this->writeU16(str.size());
            this->writeBytes((const uint8_t*)str.data(), str.size());
        }
    }

    auto writeStringU32(std::string_view str) {
        if constexpr (IsTry) {
            GEODE_UNWRAP(this->writeU32(str.size()));
            return this->writeBytes((const uint8_t*)str.data(), str.size());
        } else {
            this->writeU32(str.size());
            this->writeBytes((const uint8_t*)str.data(), str.size());
        }
    }

    Result<void> setPosition(size_t pos) requires SeekWriteSink<S> {
        return m_sink.setPosition(pos);
    }

    size_t position() const requires SeekWriteSink<S> {
        return m_sink.position();
    }

    std::span<const uint8_t> written() const requires SeekWriteSink<S> {
        return m_sink.written();
    }

    std::vector<uint8_t> writtenVec() const requires SeekWriteSink<S> {
        auto span = this->written();

        std::vector<uint8_t> vec;
        vec.assign(span.begin(), span.end());
        return vec;
    }

    std::span<const uint8_t> slice(size_t pos, size_t size) const requires SeekWriteSink<S> {
        return m_sink.slice(pos, size);
    }

    Result<void> performAt(size_t pos, auto&& func) requires SeekWriteSink<S> {
        size_t oldPos = m_sink.position();
        GEODE_UNWRAP(m_sink.setPosition(pos));
        func(*this);
        GEODE_UNWRAP(m_sink.setPosition(oldPos));
        return Ok();
    }

    auto intoInner() && requires IntoInnerSink<S> {
        return std::move(m_sink).intoInner();
    }

private:
    S m_sink;
};

template <size_t N = 1024>
using ArrayByteWriter = ByteWriter<ArraySink<N>>;

}
