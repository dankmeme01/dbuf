#include <gtest/gtest.h>
#include <dbuf/ByteWriter.hpp>

using namespace dbuf;

void expectBytes(std::span<const uint8_t> actual, const std::vector<uint8_t>& expected) {
    ASSERT_EQ(actual.size(), expected.size());
    for (size_t i = 0; i < actual.size(); ++i) {
        EXPECT_EQ(actual[i], expected[i]) << "Difference at index " << i;
    }
}

TEST(SinkTest, HeapSinkBasic) {
    HeapSink sink;
    uint8_t data[] = { 0x01, 0x02, 0x03 };

    sink.write(data, 3);
    EXPECT_EQ(sink.position(), 3);

    sink.writeZeroes(2);
    EXPECT_EQ(sink.position(), 5);

    auto slice = sink.slice(0, 5);
    expectBytes(slice, {0x01, 0x02, 0x03, 0x00, 0x00});
}

TEST(SinkTest, HeapSinkBounds) {
    HeapSink sink;
    sink.writeZeroes(10);

    EXPECT_TRUE(sink.setPosition(5).isOk());
    EXPECT_TRUE(sink.setPosition(11).isErr());
}

TEST(SinkTest, ArraySinkOverflow) {
    ArraySink<3> sink;
    uint8_t data[] = { 0xAA, 0xBB, 0xCC, 0xDD };

    auto result = sink.write(data, 4);
    EXPECT_TRUE(result.isErr());

    EXPECT_TRUE(sink.write(data, 2).isOk());
    EXPECT_EQ(sink.position(), 2);
}

TEST(WriterTest, Primitives) {
    ByteWriter writer;

    writer.writeU8(0xFF);
    writer.writeU16(0x0102);
    writer.writeBool(true);

    auto written = writer.written();
    ASSERT_EQ(written.size(), 4);
    EXPECT_EQ(written[0], 0xFF);

    EXPECT_EQ(written[1], 0x02);
    EXPECT_EQ(written[2], 0x01);
    EXPECT_EQ(written[3], 0x01);
}

TEST(WriterTest, VarUintEncoding) {
    ByteWriter writer;

    writer.writeVarUint(0x45);
    writer.writeVarUint(128);

    auto out = writer.written();
    expectBytes(out, { 0x45, 0x80, 0x01 });
}

TEST(WriterTest, Strings) {
    ByteWriter writer;
    std::string_view msg = "Hi";

    writer.writeStringU8(msg);
    writer.writeStringU16(msg);

    auto out = writer.written();
    // 1 (len) + 2 (data) + 2 (len) + 2 (data)
    ASSERT_EQ(out.size(), 7);

    EXPECT_EQ(out[0], 2);
    EXPECT_EQ(out[1], 'H');
    EXPECT_EQ(out[3], 2);
}

TEST(WriterTest, PerformAt) {
    ByteWriter writer;
    writer.writeU32(0);
    writer.writeU8(0xAA);

    auto res = writer.performAt(0, [](auto& w) {
        w.writeU32(1);
    });

    ASSERT_TRUE(res.isOk());
    EXPECT_EQ(writer.position(), 5);

    auto out = writer.written();
    EXPECT_EQ(out[0], 1);
    EXPECT_EQ(out[4], 0xAA);
}

TEST(WriterTest, ArrayWriterErrorHandling) {
    ArrayByteWriter<2> writer; // Fixed size of 2

    auto res1 = writer.writeU8(0x01);
    EXPECT_TRUE(res1.isOk());

    auto res2 = writer.writeU32(0x02030405);
    EXPECT_TRUE(res2.isErr());
}
