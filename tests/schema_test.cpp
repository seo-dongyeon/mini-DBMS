#include <gtest/gtest.h>

#include <filesystem>

#include "schema.h"
#include "test_util.h"

class SchemaTest : public ::testing::Test {
protected:
    void SetUp() override { dir = makeTempDataDir(); }
    void TearDown() override { std::filesystem::remove_all(dir); }

    // 스키마 파일을 쓰고 읽어 본다.
    bool load(const std::string& content, Schema& schema, std::string& error) {
        writeTextFile(dir + "/users.schema", content);
        return loadSchema(dir, "users", schema, error);
    }

    // 읽기에 실패해야 하는 스키마인지 확인한다.
    void expectLoadError(const std::string& content) {
        Schema schema;
        std::string error;
        EXPECT_FALSE(load(content, schema, error)) << content;
        EXPECT_FALSE(error.empty()) << content;
    }

    std::string dir;
};

TEST_F(SchemaTest, LoadsColumnsInOrder) {
    Schema schema;
    std::string error;
    ASSERT_TRUE(load("id INT\nname TEXT\nage INT\n", schema, error)) << error;
    EXPECT_EQ(schema.table, "users");
    ASSERT_EQ(schema.columns.size(), 3u);
    EXPECT_EQ(schema.columns[1].name, "name");
    EXPECT_EQ(schema.columns[1].type, ColumnType::Text);
    EXPECT_EQ(findColumnIndex(schema, "age"), 2);
    EXPECT_EQ(findColumnIndex(schema, "nope"), -1);
    EXPECT_EQ(findColumnIndex(schema, "NAME"), -1);   // 컬럼 이름은 대소문자를 구분한다
}

TEST_F(SchemaTest, AcceptsWindowsLineEndingsBlankLinesAndLowercaseTypes) {
    Schema schema;
    std::string error;
    ASSERT_TRUE(load("id int\r\n\r\n  name   text  \r\n", schema, error)) << error;
    EXPECT_EQ(schema.columns.size(), 2u);
}

TEST_F(SchemaTest, MissingFileMeansNoTable) {
    Schema schema;
    std::string error;
    EXPECT_FALSE(loadSchema(dir, "nothing", schema, error));
    EXPECT_NE(error.find("테이블이 없다"), std::string::npos) << error;
}

TEST_F(SchemaTest, RejectsBrokenSchemas) {
    expectLoadError("");                              // 비어 있음
    expectLoadError("name TEXT\nid INT\n");           // 첫 컬럼이 id가 아님
    expectLoadError("id TEXT\nname TEXT\n");          // id가 INT가 아님
    expectLoadError("id INT\n");                      // id뿐
    expectLoadError("id INT\nname\n");                // 타입 없음
    expectLoadError("id INT\nname TEXT extra\n");     // 한 줄에 세 단어
    expectLoadError("id INT\nname FLOAT\n");          // 모르는 타입
    expectLoadError("id INT\nname TEXT\nname INT\n"); // 이름이 겹침
    expectLoadError("id INT\n1name TEXT\n");          // 숫자로 시작하는 이름
    expectLoadError("id INT\nna-me TEXT\n");          // 쓸 수 없는 글자
}
