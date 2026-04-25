#include <gtest/gtest.h>
#include <dbuf/CircularByteBuffer.hpp>

using namespace dbuf;

class CircularBufferTest : public ::testing::Test {
protected:
    void ExpectBufferContent(const CircularByteBuffer& buf, const std::vector<uint8_t>& expected) {
        ASSERT_EQ(buf.size(), expected.size());
        std::vector<uint8_t> actual(buf.size());
        buf.peek(actual.data(), actual.size());
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(actual[i], expected[i]) << "Mismatch at index " << i;
        }
    }
};

TEST_F(CircularBufferTest, BasicWriteRead) {
    CircularByteBuffer buf(10);
    uint8_t data[] = {1, 2, 3, 4, 5};

    buf.write(data, 5);
    EXPECT_EQ(buf.size(), 5);

    uint8_t output[3];
    buf.read(output, 3);
    EXPECT_EQ(output[0], 1);
    EXPECT_EQ(output[2], 3);
    EXPECT_EQ(buf.size(), 2);
}

TEST_F(CircularBufferTest, WrapAround) {
    CircularByteBuffer buf(5);

    buf.write("AAAA", 4);
    buf.skip(2); // start is now at index 2, size 2

    // remaining space is 3, writing "BBB" should wrap
    buf.write("BBB", 3);

    EXPECT_EQ(buf.size(), 5);

    ExpectBufferContent(buf, {'A', 'A', 'B', 'B', 'B'});
}

TEST_F(CircularBufferTest, WrappedPeek) {
    CircularByteBuffer buf(5);
    buf.write("1234", 4);
    buf.skip(3);
    buf.write("56", 2);

    auto wrapped = buf.peek(3);

    ASSERT_EQ(wrapped.first.size(), 2);
    EXPECT_EQ(wrapped.first[0], '4');
    EXPECT_EQ(wrapped.first[1], '5');

    ASSERT_EQ(wrapped.second.size(), 1);
    EXPECT_EQ(wrapped.second[0], '6');
}

TEST_F(CircularBufferTest, AutoGrow) {
    CircularByteBuffer buf(2);

    buf.write("123456", 6);

    EXPECT_GE(buf.capacity(), 6);
    ExpectBufferContent(buf, {'1', '2', '3', '4', '5', '6'});
}

TEST_F(CircularBufferTest, WriteWindow) {
    CircularByteBuffer buf(10);

    auto window = buf.writeWindow();
    ASSERT_GE(window.size(), 5);

    std::memcpy(window.data(), "HELLO", 5);
    buf.advanceWrite(5);

    ExpectBufferContent(buf, {'H', 'E', 'L', 'L', 'O'});
}

TEST_F(CircularBufferTest, CopyAndMove) {
    CircularByteBuffer buf1(10);
    buf1.write("DATA", 4);

    CircularByteBuffer buf2 = buf1;
    ExpectBufferContent(buf2, {'D', 'A', 'T', 'A'});

    CircularByteBuffer buf3 = std::move(buf1);
    ExpectBufferContent(buf3, {'D', 'A', 'T', 'A'});
    EXPECT_EQ(buf1.size(), 0);
}

TEST_F(CircularBufferTest, SkipResetsPointers) {
    CircularByteBuffer buf(10);
    buf.write("TEST", 4);
    buf.skip(4);

    auto window = buf.writeWindow();
    EXPECT_EQ(window.data(), buf.writeWindow().data());
    EXPECT_EQ(window.size(), 10);
}

TEST_F(CircularBufferTest, ThrowsOnOverflow) {
    CircularByteBuffer buf(5);
    buf.write("123", 3);

    EXPECT_THROW(buf.peek(10), std::out_of_range);
    EXPECT_THROW(buf.skip(10), std::out_of_range);
}
