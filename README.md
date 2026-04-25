# dbuf

Small header-only C++20 library for easy binary serialization. Provided classes:

* `ByteReader` - for reading
* `ByteWriter` - for writing
* `CircularByteBuffer` - a collection akin to `std::deque<uint8_t>` but implemented as a ring buffer, providing fast writes at the back and reads at the front.

The binary buffers are fairly flexible and extensible, they allow creating custom sources/sinks in a fully compile-time fashion. Provided are a few basic sinks and sources that are good enough for most use cases.

## ByteReader

```cpp
#include <dbuf/ByteReader.hpp>

using namespace dbuf;

std::vector<uint8_t> data = { 0x01, 0x02 };
ByteReader reader{data}; // can be made from a span or a vector ref

uint16_t val = reader.readU16().unwrap();
assert(val == 0x0201); // all values are in little-endian, so swap the bytes!

(void) reader.setPosition(0);
assert(reader.readU8().unwrap() == 0x01);
assert(reader.readU8().unwrap() == 0x02);
```

Custom sources can be created by implementing the `ReadSource` trait:
```cpp
template <typename T>
concept ReadSource = requires(T reader, uint8_t* data, size_t size) {
    { reader.read(data, size) } -> std::same_as<Result<void>>;
};
```

For extra functionality (such as seeking), you can implement the following traits on your source as well:
```cpp
template <typename T>
concept SeekSource = ReadSource<T> && requires(T reader, size_t pos) {
    { reader.position() } -> std::same_as<size_t>;
    { reader.setPosition(pos) } -> std::same_as<Result<void>>;
    { reader.totalSize() } -> std::same_as<size_t>;
};

template <typename T>
concept SkipSource = ReadSource<T> && requires(T reader, size_t size) {
    { reader.skip(size) } -> std::same_as<Result<void>>;
};
```

For example, here's a basic custom source that simply generates sequential bytes (wrapping at `0xff`) forever, does not implement `SeekSource` but implements `SkipSource`:

```cpp
struct SequentialSource {
    uint8_t current = 0;

    Result<void> read(uint8_t* out, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            out[i] = current++;
        }
        return Ok();
    }

    Result<void> skip(size_t size) {
        current += size;
        return Ok();
    }
};

// use our custom SequentialSource instead of the default SpanReadSource
ByteReader reader(SequentialSource{});

assert(reader.readU8().unwrap() == 0);
assert(reader.readU16().unwrap() == 0x0201);
assert(reader.skip(3).isOk());
assert(reader.readU8() == 0x06);

// setPosition() / position() will not compile on this source!
```

## ByteWriter

```cpp
#include <dbuf/ByteWriter.hpp>

using namespace dbuf;

ByteWriter wr;
wr.writeU8(0xff);
wr.writeU16(0x0102);
wr.writeBool(true);

auto written = wr.written();
assert(written.size() == 4);
assert(written[0] == 0xff);
assert(written[1] == 0x02);
assert(written[2] == 0x01);
assert(written[3] == 0x01);
```

By default, `HeapSink` is used, which internally stores all the data in a `std::vector<uint8_t>` and grows as needed, meaning writes can never return an error. For encoding without any allocation, `ArrayByteWriter` can be used, which is simply an alias for `ByteWriter<ArraySink<N>>`:

```cpp
ArrayByteWriter<> wr;

// writeU32 and all other write methods now can fail, in case there's not enough space
auto res = wr.writeU32(42);
if (!res) {
    std::println("not enough space!");
}
```

You can also create custom sinks to customize the behavior, by implementing either `TryWriteSink` (if writing can fail) or `WriteSink` (if writing cannot fail):

```cpp
template <typename T>
concept TryWriteSink = AnyWriteSink<T> && requires(T writer, const uint8_t* data, size_t size) {
    { writer.write(data, size) } -> std::same_as<Result<void>>;
};

template <typename T>
concept WriteSink = AnyWriteSink<T> && requires(T writer, const uint8_t* data, size_t size) {
    { writer.write(data, size) } -> std::same_as<void>;
};
```

Additional traits may be implemented for extra functionality:
```cpp
template <typename T>
concept WriteZeroesSink = AnyWriteSink<T> && requires(T writer, size_t size) {
    { writer.writeZeroes(size) };
};

template <typename T>
concept SeekWriteSink = AnyWriteSink<T> && requires(T writer, size_t pos, size_t size) {
    { writer.setPosition(pos) } -> std::same_as<Result<void>>;
    { writer.position() } -> std::same_as<size_t>;
    { writer.slice(pos, size) } -> std::same_as<std::span<const uint8_t>>;
};
```

For example, here's a sink that simply voids all the data, implements `WriteSink` and `WriteZeroesSink`:

```cpp
struct VoidSink {
    void write(const uint8_t* data, size_t size) {}
    void writeZeroes(size_t size) {}
};
```
