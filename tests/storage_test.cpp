#include <gtest/gtest.h>

#include <filesystem>

#include "storage.h"
#include "test_util.h"

// 칸들을 저장 형식으로 바꿨다가 되돌렸을 때 원래대로 나오는지 확인한다.
static void expectRoundTrip(const std::vector<std::string>& fields) {
    std::string line = encodeRecord(fields);
    EXPECT_EQ(line.find('\n'), std::string::npos) << "저장된 줄 안에 진짜 줄바꿈이 있으면 안 된다";
    std::vector<std::string> decoded;
    ASSERT_TRUE(decodeRecord(line, decoded)) << line;
    EXPECT_EQ(decoded, fields);
}

TEST(Storage, EncodesPlainRecord) {
    EXPECT_EQ(encodeRecord({"1", "민수", "30"}), "1,민수,30");
}

TEST(Storage, RoundTripsSpecialCharacters) {
    expectRoundTrip({"1", "민수", "30"});
    expectRoundTrip({"1", "서울,강남", "30"});          // 쉼표
    expectRoundTrip({"1", "a\\b", "30"});               // 역슬래시
    expectRoundTrip({"1", "첫 줄\n둘째 줄\r\n", "30"});  // 줄바꿈
    expectRoundTrip({"1", "\\,", "30"});                // 역슬래시 뒤에 쉼표
    expectRoundTrip({"1", "\\n", "30"});                // 글자 그대로의 역슬래시+n
    expectRoundTrip({"1", "", ""});                     // 빈 값
    expectRoundTrip({"1", ",,,", ","});
}

TEST(Storage, DecodeRejectsBrokenEscape) {
    std::vector<std::string> fields;
    EXPECT_FALSE(decodeRecord("1,abc\\", fields));    // 역슬래시로 끝남
    EXPECT_FALSE(decodeRecord("1,a\\xb", fields));    // 모르는 이스케이프
}

TEST(Storage, ParseIntegerIsStrict) {
    long long value = 0;
    EXPECT_TRUE(parseInteger("42", value));
    EXPECT_EQ(value, 42);
    EXPECT_TRUE(parseInteger("-7", value));
    EXPECT_EQ(value, -7);
    EXPECT_FALSE(parseInteger("", value));
    EXPECT_FALSE(parseInteger("12a", value));
    EXPECT_FALSE(parseInteger("a12", value));
    EXPECT_FALSE(parseInteger("99999999999999999999999", value));
}

class StorageFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = makeTempDataDir();
        path = dir + "/users.db";
    }
    void TearDown() override { std::filesystem::remove_all(dir); }

    std::string dir;
    std::string path;
};

TEST_F(StorageFileTest, MissingFileIsEmptyTable) {
    std::string error;
    RecordReader reader;
    ASSERT_TRUE(reader.open(path, error));
    std::vector<std::string> fields;
    EXPECT_EQ(reader.readNext(fields), ReadStatus::End);
}

TEST_F(StorageFileTest, AppendThenReadBack) {
    std::string error;
    {
        std::ofstream file(path, std::ios::app | std::ios::binary);
        ASSERT_TRUE(appendRecord(file, encodeRecord({"1", "민수", "30"}), error)) << error;
        ASSERT_TRUE(appendRecord(file, encodeRecord({"2", "줄\n바꿈", "25"}), error)) << error;
    }
    EXPECT_EQ(readTextFile(path), "1,민수,30\n2,줄\\n바꿈,25\n");

    RecordReader reader;
    ASSERT_TRUE(reader.open(path, error));
    std::vector<std::string> fields;
    ASSERT_EQ(reader.readNext(fields), ReadStatus::Ok);
    EXPECT_EQ(fields[1], "민수");
    ASSERT_EQ(reader.readNext(fields), ReadStatus::Ok);
    EXPECT_EQ(fields[1], "줄\n바꿈");
    EXPECT_EQ(reader.lineNumber(), 2);
    EXPECT_EQ(reader.readNext(fields), ReadStatus::End);
}

TEST_F(StorageFileTest, ReadsWindowsLineEndingsAndBlankLines) {
    writeTextFile(path, "1,민수,30\r\n\r\n2,동연,25\r\n");
    std::string error;
    RecordReader reader;
    ASSERT_TRUE(reader.open(path, error));
    std::vector<std::string> fields;
    ASSERT_EQ(reader.readNext(fields), ReadStatus::Ok);
    EXPECT_EQ(fields[2], "30");
    ASSERT_EQ(reader.readNext(fields), ReadStatus::Ok);
    EXPECT_EQ(fields[0], "2");
    EXPECT_EQ(reader.readNext(fields), ReadStatus::End);
}

// B+ 트리에 넣는 값(줄 시작 바이트)이 정확한지, 그 위치에서 바로 그 줄을 읽을 수 있는지 본다.
TEST_F(StorageFileTest, RecordOffsetPointsToLineStart) {
    // 한글은 한 글자가 3바이트다. 윈도우 줄바꿈과 빈 줄이 섞여도 위치가 맞아야 한다.
    std::string content = "1,민수,30\r\n\n2,동연,25\n3,a\\,b,7\n";
    writeTextFile(path, content);
    std::string error;
    RecordReader reader;
    ASSERT_TRUE(reader.open(path, error));

    std::vector<std::string> fields;
    std::vector<long long> offsets;
    while (reader.readNext(fields) == ReadStatus::Ok) offsets.push_back(reader.recordOffset());

    std::vector<long long> expected = {
        0,
        static_cast<long long>(content.find("2,동연")),
        static_cast<long long>(content.find("3,a")),
    };
    ASSERT_EQ(offsets, expected);

    ASSERT_TRUE(readRecordAt(path, offsets[1], fields, error)) << error;
    EXPECT_EQ(fields, (std::vector<std::string>{"2", "동연", "25"}));
    ASSERT_TRUE(readRecordAt(path, offsets[0], fields, error)) << error;
    EXPECT_EQ(fields[2], "30");   // 줄 끝의 \r은 떼어 낸다
    ASSERT_TRUE(readRecordAt(path, offsets[2], fields, error)) << error;
    EXPECT_EQ(fields[1], "a,b");
}

TEST_F(StorageFileTest, ReadRecordAtFailsOutsideFileOrOnBrokenLine) {
    writeTextFile(path, "1,a\\x,1\n");
    std::vector<std::string> fields;
    std::string error;
    EXPECT_FALSE(readRecordAt(path, 0, fields, error));      // 깨진 이스케이프
    EXPECT_FALSE(readRecordAt(path, 1000, fields, error));   // 파일 밖
    EXPECT_FALSE(readRecordAt(dir + "/nothing.db", 0, fields, error));
}

TEST_F(StorageFileTest, AppendFailsOnClosedFile) {
    std::ofstream notOpened;
    std::string error;
    EXPECT_FALSE(appendRecord(notOpened, "1,a,1", error));
    EXPECT_FALSE(error.empty());
}
