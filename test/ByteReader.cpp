#include <gtest/gtest.h>
#include <dbuf/ByteReader.hpp>
#include <vector>
#include <cstring>

using namespace dbuf;

struct VectorSource {
    std::vector<uint8_t> data;
    size_t pos = 0;

    Result<void> read(uint8_t* out, size_t size) {
        if (pos + size > data.size()) return Err("EOF");
        std::memcpy(out, data.data() + pos, size);
        pos += size;
        return Ok();
    }

    size_t position() const { return pos; }
    size_t totalSize() const { return data.size(); }
    Result<void> setPosition(size_t p) {
        if (p > data.size()) return Err("OOB");
        pos = p;
        return Ok();
    }
};

TEST(ByteReaderTest, ReadPrimitives) {
    std::vector<uint8_t> data = { 0x01, 0x02, 0x03, 0x00, 0x04, 0x00, 0x00, 0x00 };
    ByteReader reader(data);

    EXPECT_EQ(*reader.readU8(), 0x01);
    EXPECT_EQ(*reader.readU16(), 0x0302);
    EXPECT_EQ(*reader.readU32(), 0x00000400);
}

TEST(ByteReaderTest, ReadBool) {
    ByteReader reader(VectorSource{{ 0x01, 0x00, 0x42 }});
    EXPECT_TRUE(*reader.readBool());
    EXPECT_FALSE(*reader.readBool());
    EXPECT_TRUE(*reader.readBool());
}

TEST(ByteReaderTest, Skip) {
    ByteReader reader(VectorSource{{ 0xAA, 0xBB, 0xCC, 0xDD }});
    ASSERT_TRUE(reader.skip(2));
    EXPECT_EQ(*reader.readU8(), 0xCC);
}

TEST(ByteReaderTest, VarUint) {
    // 300 in ULEB128 is [0xAC, 0x02]
    ByteReader reader(VectorSource{{ 0xAC, 0x02, 0x7F }});
    EXPECT_EQ(*reader.readVarUint(), 300);
    EXPECT_EQ(*reader.readVarUint(), 127);
}

TEST(ByteReaderTest, VarInt) {
    // -64 in SLEB128 is [0x40]
    // 64 in SLEB128 is [0xC0, 0x00]
    ByteReader reader(VectorSource{{ 0x40, 0xC0, 0x00 }});
    EXPECT_EQ(*reader.readVarInt(), -64);
    EXPECT_EQ(*reader.readVarInt(), 64);
}

TEST(ByteReaderTest, FixedString) {
    std::string str = "hello";
    std::vector<uint8_t> data(str.begin(), str.end());
    ByteReader reader(VectorSource{data});

    EXPECT_EQ(*reader.readFixedString(5), "hello");
}

TEST(ByteReaderTest, PrefixedStrings) {
    std::vector<uint8_t> dataU8 = { 0x05, 'w', 'o', 'r', 'l', 'd' };
    ByteReader readerU8(VectorSource{dataU8});
    EXPECT_EQ(*readerU8.readStringU8(), "world");

    std::vector<uint8_t> dataU16 = { 0x04, 0x00, 't', 'e', 's', 't' };
    ByteReader readerU16(VectorSource{dataU16});
    EXPECT_EQ(*readerU16.readStringU16(), "test");
}

TEST(ByteReaderTest, VarintString) {
    std::vector<uint8_t> data = { 0x03, 'b', 'u', 'f' };
    ByteReader reader(VectorSource{data});
    EXPECT_EQ(*reader.readStringVar(), "buf");
}

TEST(ByteReaderTest, SeekSourceMethods) {
    VectorSource src{{ 0x01, 0x02, 0x03, 0x04 }};
    ByteReader reader(std::move(src));

    EXPECT_EQ(reader.remainingSize(), 4);
    ASSERT_TRUE(reader.skip(2));
    EXPECT_EQ(reader.position(), 2);
    EXPECT_EQ(reader.remainingSize(), 2);

    auto end = reader.readToEnd();
    EXPECT_EQ(end.size(), 2);
    EXPECT_EQ(end[0], 0x03);
    EXPECT_EQ(end[1], 0x04);
}

TEST(ByteReaderTest, BoundsError) {
    ByteReader reader(VectorSource{{ 0x42 }});
    EXPECT_EQ(*reader.readU8(), 0x42);
    auto result = reader.readU8();
    EXPECT_FALSE(result);
}

TEST(ByteReaderTest, VarintOverflow) {
    // 10 bytes with MSB set = overflow for 64-bit
    std::vector<uint8_t> data(11, 0xFF);
    ByteReader reader(VectorSource{data});
    EXPECT_FALSE(reader.readVarUint());
}

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

TEST(ByteReaderTest, SequentialSource) {
    ByteReader reader(SequentialSource{});

    EXPECT_EQ(*reader.readU8(), 0);
    EXPECT_EQ(*reader.readU16(), 0x0201);
    EXPECT_TRUE(reader.skip(3));
    EXPECT_EQ(*reader.readU8(), 0x06);
}