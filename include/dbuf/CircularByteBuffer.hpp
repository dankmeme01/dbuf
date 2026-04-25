#pragma once

#include <cstring>
#include <cassert>
#include <stdexcept>
#include <span>
#include <stdint.h>

namespace dbuf {

// Circular byte buffer implementation.
// This buffer allows efficient amortized read/write operations, which are a simple memcpy,
// while also efficiently storing the data and only reallocating when the reader is significantly behind the writer.
// Due to this, random access is not easy, `peek(size_t)` may return 2 spans if the requested data wraps around the end of the buffer.

class CircularByteBuffer {
public:
    CircularByteBuffer() : CircularByteBuffer(0) {}
    CircularByteBuffer(size_t capacity) {
        if (capacity == 0) return;

        m_data = new uint8_t[capacity];
        m_start = m_data;
        m_end = m_data;
        m_endAlloc = m_data + capacity;
        m_size = 0;
    }

    ~CircularByteBuffer() {
        delete[] m_data;
    }

    CircularByteBuffer(const CircularByteBuffer& other) {
        *this = other;
    }

    CircularByteBuffer& operator=(const CircularByteBuffer& other) {
        if (this != &other) {
            delete[] m_data;

            size_t cap = other.capacity();
            m_data = new uint8_t[cap];
            m_endAlloc = m_data + cap;

            m_start = m_data + std::distance(other.m_data, other.m_start);
            m_end = m_data + std::distance(other.m_data, other.m_end);
            m_size = other.m_size;

            std::memcpy(m_data, other.m_data, cap);
        }

        return *this;
    }

    CircularByteBuffer(CircularByteBuffer&& other) noexcept {
        *this = std::move(other);
    }

    CircularByteBuffer& operator=(CircularByteBuffer&& other) noexcept {
        if (this != &other) {
            delete[] m_data;

            m_data = other.m_data;
            m_start = other.m_start;
            m_end = other.m_end;
            m_endAlloc = other.m_endAlloc;
            m_size = other.m_size;

            other.m_data = nullptr;
            other.m_start = nullptr;
            other.m_end = nullptr;
            other.m_endAlloc = nullptr;
            other.m_size = 0;
        }

        return *this;
    }

    // Clears the buffer, but does not deallocate memory.
    void clear() {
        m_start = m_data;
        m_end = m_data;
        m_size = 0;
    }

    // Reserves extra capacity in the buffer.
    // After calling this, `capacity()` will be greater than or equal to previous value of `capacity()` + `extraCap`.
    // There is also a guarantee that `writeWindow().size()` will be at least `extraCap` bytes after this call.
    void reserve(size_t extraCap) {
        this->growUntilAtLeast(this->capacity() + extraCap);
        assert(this->writeWindow().size() >= extraCap);
    }

    size_t capacity() const {
        return m_endAlloc - m_data;
    }

    size_t size() const {
        return m_size;
    }

    bool empty() const {
        return m_size == 0;
    }

    // Appends more data to the end of the buffer. This will reallocate if `size() + len > capacity()`.
    void write(const void* data, size_t len) {
        return this->write({(const uint8_t*)data, len});
    }

    void write(std::span<const uint8_t> data) {
        if (data.empty()) {
            return; // nothing to write
        }

        size_t remSpace = this->capacity() - this->size();

        if (data.size() > remSpace) {
            this->growUntilAtLeast(this->size() + data.size());
        }

        assert(this->capacity() >= this->size() + data.size());

        auto wnd1 = this->writeWindow();
        size_t write1Len = std::min<size_t>(data.size(), wnd1.size());

        std::memcpy(wnd1.data(), data.data(), write1Len);
        this->advanceWrite(write1Len);

        if (write1Len < data.size()) {
            // wrapping around
            auto wnd2 = this->writeWindow();
            size_t write2Len = data.size() - write1Len;

            assert(wnd2.size() >= write2Len && "CircularByteBuffer::write: not enough space in the second window");

            std::memcpy(wnd2.data(), data.data() + write1Len, write2Len);
            this->advanceWrite(write2Len);
        }
    }

    // Returns the window where the next write can happen.
    // This allows zero-copy writes, for example calling `recv` on a socket to write directly into the buffer.
    // Make sure to call `advanceWrite(size)` after writing to the window.
    std::span<uint8_t> writeWindow() {
        size_t size = m_start < m_end ?
                m_endAlloc - m_end
                : m_start - m_end;

        if (size == 0) {
            // this means m_start == m_end, which means buffer is either full or empty.
            if (m_size == 0) {
                assert(m_start == m_data);
                assert(m_end == m_data);

                return std::span<uint8_t>{
                    m_data,
                    this->capacity()
                };
            } else {
                // buffer is full
                return std::span<uint8_t>{m_end, 0};
            }
        }

        return std::span<uint8_t>{
            m_end,
            size
        };
    }

    void advanceWrite(size_t len) {
        if (len > this->capacity() - this->size()) {
            throw std::out_of_range("CircularByteBuffer::advanceWrite called with len > available space");
        }

        m_end += len;
        if (m_end >= m_endAlloc) {
            m_end -= m_endAlloc - m_data; // wrap around
        }

        m_size += len;
    }

    // Reads the next unread data from the buffer. Throws if `len > size()`.
    void read(void* dest, size_t len) {
        if (dest) {
            this->peek(dest, len);
        }

        this->skip(len);
    }

    // Like `read`, but does not remove data from the buffer.
    void peek(void* dest, size_t len, size_t skip = 0) const {
        if (len == 0) return;

        assert(dest && "CircularByteBuffer::peek called with null destination");

        auto bufs = this->peek(len, skip);
        std::memcpy(dest, bufs.first.data(), bufs.first.size());

        if (!bufs.second.empty()) {
            std::memcpy((uint8_t*)dest + bufs.first.size(), bufs.second.data(), bufs.second.size());
        }
    }

    struct WrappedRead {
        std::span<const uint8_t> first;
        std::span<const uint8_t> second;

        inline size_t size() const {
            return first.size() + second.size();
        }

        inline void skip(size_t len) {
            if (len <= first.size()) {
                first = first.subspan(len);
                return;
            }

            len -= first.size();
            first = std::span<const uint8_t>{};

            if (len <= second.size()) {
                second = second.subspan(len);
            } else {
                second = std::span<const uint8_t>{};
            }
        }
    };

    // Returns a span of the next unread data. If the data wraps around the end of the buffer, it will return two spans,
    // otherwise `.second` will be empty.
    // Throws if `len > size()`.
    WrappedRead peek(size_t len, size_t skip = 0) const {
        if (len + skip > this->size()) {
            throw std::out_of_range("CircularByteBuffer::peek called with len + skip > size()");
        }

        WrappedRead out{};

        // handle simple case
        if (m_start + skip < m_end) {
            out.first = std::span<const uint8_t>{
                m_start + skip,
                len
            };
            return out;
        }

        const uint8_t* start = m_start + skip;
        if (start >= m_endAlloc) {
            start = m_data + (start - m_endAlloc); // wrap around
        }

        out.first = std::span<const uint8_t>{
            start,
            std::min<size_t>(len, m_endAlloc - start)
        };

        size_t remaining = len - out.first.size();

        if (remaining > 0) {
            // wrap around
            out.second = std::span<const uint8_t>{
                m_data,
                remaining
            };
        }

        assert(out.first.size() + out.second.size() == len);
        assert(out.first.data() >= m_data && out.first.data() < m_endAlloc);
        assert(out.second.empty() || (out.second.data() >= m_data && out.second.data() < m_endAlloc));

        return out;
    }

    // Skips the next `len` bytes in the buffer. Throws if `len > size()`.
    // Identical to `read(nullptr, len)`.
    void skip(size_t len) {
        if (len > this->size()) {
            throw std::out_of_range("CircularByteBuffer::skip called with len > size()");
        }

        m_start += len;
        if (m_start >= m_endAlloc) {
            m_start -= m_endAlloc - m_data; // wrap around
        }

        m_size -= len;

        // if size is 0, reset start and end to the beginning of the buffer
        if (m_size == 0) {
            m_start = m_data;
            m_end = m_data;
        }
    }

private:
    uint8_t* m_data = nullptr;
    uint8_t* m_start = nullptr;
    uint8_t* m_end = nullptr;
    uint8_t* m_endAlloc = nullptr;
    size_t m_size = 0;

    void growUntilAtLeast(size_t capacity) {
        size_t curcap = this->capacity();

        if (curcap == 0) {
            // start with a reasonable default capacity
            curcap = 64;
        }

        while (curcap < capacity) {
            curcap *= 2;
        }

        this->growTo(curcap);
    }

    void growTo(size_t newCap) {
        auto newData = new uint8_t[newCap];

        // Reallocation may sound like a simple idea, but we need to handle the circular nature of the buffer.
        // we have four cases:
        // 1. m_start < m_end, we can just copy the data as is, and new space will be at the end
        // 2. m_start > m_end, we need to copy the data in two parts, so that new space will be in the middle
        // 3. m_start == m_end AND m_size == 0, here integrity of m_start and m_end is not important and they can even be reset to m_data
        // 4. m_start == m_end AND m_size == capacity(), just like 2, the new space has to be in the middle.

        // for simplicity sake:
        // * 1 and 3 will be handled the same way, just like 2 and 4.
        // * this function *always* makes the buffer contigous,
        // for cases 2 and 4 rather than expanding space in the middle, it will just rearrange the data to be contiguous.
        // in all cases, m_start will be set to the beginning of the allocated memory.

        if (m_start < m_end || (m_start == m_end && m_size == 0)) {
            // case 1 and 3
            std::memcpy(newData, m_start, m_size);
        } else {
            // case 2 and 4
            size_t startPartSize = m_end - m_data;
            size_t endPartSize = m_endAlloc - m_start;

            std::memcpy(newData, m_start, endPartSize);
            std::memcpy(newData + endPartSize, m_data, startPartSize);
        }

        m_start = newData;
        m_end = newData + m_size;
        m_endAlloc = newData + newCap;

        delete[] m_data;
        m_data = newData;
    }
};

}
