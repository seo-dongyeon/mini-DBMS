#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

#include "executor.h"
#include "test_util.h"

class ExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = makeTempDataDir();
        writeTextFile(dir + "/users.schema", "id INT\nname TEXT\nage INT\n");
    }
    void TearDown() override { std::filesystem::remove_all(dir); }

    // SQL 한 문장을 토큰화, 파싱, 실행까지 한다. 파싱까지는 성공해야 한다.
    QueryResult run(Database& database, const std::string& sql) {
        TokenizeResult tokenized = tokenize(sql);
        EXPECT_TRUE(tokenized.ok) << tokenized.error;
        Statement statement;
        std::string error;
        EXPECT_TRUE(parseStatement(tokenized.tokens, statement, error)) << error;
        return executeStatement(database, statement);
    }

    std::string dir;
};

using Rows = std::vector<std::vector<std::string>>;

TEST_F(ExecutorTest, InsertAssignsIncreasingIds) {
    Database database(dir);
    QueryResult first = run(database, "INSERT INTO users VALUES ('민수', 30);");
    QueryResult second = run(database, "INSERT INTO users VALUES ('동연', 25);");
    ASSERT_TRUE(first.ok) << first.error;
    ASSERT_TRUE(second.ok) << second.error;
    EXPECT_EQ(first.insertedId, 1);
    EXPECT_EQ(second.insertedId, 2);
    EXPECT_EQ(readTextFile(dir + "/users.db"), "1,민수,30\n2,동연,25\n");
}

TEST_F(ExecutorTest, SelectFromEmptyTable) {
    Database database(dir);
    QueryResult result = run(database, "SELECT * FROM users;");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_TRUE(result.rows.empty());
    std::vector<std::string> expectedColumns = {"id", "name", "age"};
    EXPECT_EQ(result.columnNames, expectedColumns);
}

TEST_F(ExecutorTest, SelectAll) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('민수', 30);");
    run(database, "INSERT INTO users VALUES ('동연', 25);");
    QueryResult result = run(database, "SELECT * FROM users;");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.rows, (Rows{{"1", "민수", "30"}, {"2", "동연", "25"}}));
}

TEST_F(ExecutorTest, SelectWhereOnIntAndText) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('민수', 30);");
    run(database, "INSERT INTO users VALUES ('동연', 25);");
    run(database, "INSERT INTO users VALUES ('민수', 41);");

    QueryResult byId = run(database, "SELECT * FROM users WHERE id = 2;");
    EXPECT_EQ(byId.rows, (Rows{{"2", "동연", "25"}}));

    QueryResult byName = run(database, "SELECT id FROM users WHERE name = '민수';");
    EXPECT_EQ(byName.rows, (Rows{{"1"}, {"3"}}));

    QueryResult noMatch = run(database, "SELECT * FROM users WHERE age = 99;");
    ASSERT_TRUE(noMatch.ok);
    EXPECT_TRUE(noMatch.rows.empty());
}

TEST_F(ExecutorTest, SelectColumnsInRequestedOrder) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('민수', 30);");
    QueryResult result = run(database, "SELECT age, id, age FROM users;");
    ASSERT_TRUE(result.ok) << result.error;
    std::vector<std::string> expectedColumns = {"age", "id", "age"};
    EXPECT_EQ(result.columnNames, expectedColumns);
    EXPECT_EQ(result.rows, (Rows{{"30", "1", "30"}}));
}

TEST_F(ExecutorTest, SpecialCharactersSurviveInsertAndSelect) {
    Database database(dir);
    std::string tricky = "서울,강남\\ 'x'\n둘째 줄;";
    QueryResult inserted = run(database, "INSERT INTO users VALUES ('서울,강남\\ ''x''\n둘째 줄;', -5);");
    ASSERT_TRUE(inserted.ok) << inserted.error;

    QueryResult result = run(database, "SELECT name, age FROM users WHERE id = 1;");
    EXPECT_EQ(result.rows, (Rows{{tricky, "-5"}}));

    // 값에 줄바꿈이 있어도 파일에서는 한 줄이다.
    std::string stored = readTextFile(dir + "/users.db");
    EXPECT_EQ(std::count(stored.begin(), stored.end(), '\n'), 1);
}

TEST_F(ExecutorTest, EmptyTextIsStored) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('', 0);");
    QueryResult result = run(database, "SELECT id FROM users WHERE name = '';");
    EXPECT_EQ(result.rows, (Rows{{"1"}}));
}

TEST_F(ExecutorTest, IdContinuesAfterRestart) {
    {
        Database database(dir);
        run(database, "INSERT INTO users VALUES ('민수', 30);");
        run(database, "INSERT INTO users VALUES ('동연', 25);");
    }
    // 새 Database는 프로그램을 껐다 켠 것과 같다.
    Database database(dir);
    QueryResult result = run(database, "INSERT INTO users VALUES ('지수', 22);");
    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.insertedId, 3);
}

TEST_F(ExecutorTest, UnknownTableFails) {
    Database database(dir);
    EXPECT_FALSE(run(database, "SELECT * FROM nothing;").ok);
    EXPECT_FALSE(run(database, "INSERT INTO nothing VALUES (1);").ok);
}

TEST_F(ExecutorTest, UnknownColumnFails) {
    Database database(dir);
    EXPECT_FALSE(run(database, "SELECT nope FROM users;").ok);
    EXPECT_FALSE(run(database, "SELECT * FROM users WHERE nope = 1;").ok);
}

TEST_F(ExecutorTest, WrongValueCountFails) {
    Database database(dir);
    EXPECT_FALSE(run(database, "INSERT INTO users VALUES ('민수');").ok);
    EXPECT_FALSE(run(database, "INSERT INTO users VALUES ('민수', 30, 1, 2);").ok);

    // id까지 적은 경우에는 왜 틀렸는지 알려 준다.
    QueryResult withId = run(database, "INSERT INTO users VALUES (7, '민수', 30);");
    EXPECT_FALSE(withId.ok);
    EXPECT_NE(withId.error.find("id는 자동"), std::string::npos) << withId.error;
}

TEST_F(ExecutorTest, TypeMismatchFails) {
    Database database(dir);
    EXPECT_FALSE(run(database, "INSERT INTO users VALUES (30, '민수');").ok);
    EXPECT_FALSE(run(database, "INSERT INTO users VALUES ('민수', '서른');").ok);
    EXPECT_FALSE(run(database, "SELECT * FROM users WHERE id = '1';").ok);
    EXPECT_FALSE(run(database, "SELECT * FROM users WHERE name = 1;").ok);
}

TEST_F(ExecutorTest, FailedInsertWritesNothingAndKeepsId) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('민수', 30);");
    EXPECT_FALSE(run(database, "INSERT INTO users VALUES ('동연', '스물다섯');").ok);
    EXPECT_EQ(readTextFile(dir + "/users.db"), "1,민수,30\n");

    QueryResult next = run(database, "INSERT INTO users VALUES ('지수', 22);");
    EXPECT_EQ(next.insertedId, 2);
}

TEST_F(ExecutorTest, CorruptDataFileFails) {
    writeTextFile(dir + "/users.db", "1,민수,30\n2,칸이 모자람\n");
    Database database(dir);
    QueryResult result = run(database, "SELECT * FROM users;");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("2번째 줄"), std::string::npos) << result.error;
}

TEST_F(ExecutorTest, FormatsSelectAndInsert) {
    Database database(dir);
    QueryResult inserted = run(database, "INSERT INTO users VALUES ('민수', 30);");
    EXPECT_EQ(formatResult(inserted), "INSERT OK (id=1)\n");

    QueryResult selected = run(database, "SELECT * FROM users;");
    EXPECT_EQ(formatResult(selected), "id | name | age\n1 | 민수 | 30\n(1 rows)\n");
}

// ---- Phase 2: B+ 트리 인덱스 ----

TEST_F(ExecutorTest, WhereIdUsesIndexAndOtherColumnsScan) {
    Database database(dir);
    run(database, "INSERT INTO users VALUES ('민수', 30);");
    run(database, "INSERT INTO users VALUES ('동연', 25);");

    QueryResult byId = run(database, "SELECT * FROM users WHERE id = 2;");
    EXPECT_TRUE(byId.usedIndex);
    EXPECT_EQ(byId.rows, (Rows{{"2", "동연", "25"}}));

    QueryResult missing = run(database, "SELECT * FROM users WHERE id = 99;");
    ASSERT_TRUE(missing.ok) << missing.error;
    EXPECT_TRUE(missing.usedIndex);
    EXPECT_TRUE(missing.rows.empty());

    QueryResult byName = run(database, "SELECT id FROM users WHERE name = '동연';");
    EXPECT_FALSE(byName.usedIndex);
    EXPECT_EQ(byName.rows, (Rows{{"2"}}));

    QueryResult all = run(database, "SELECT * FROM users;");
    EXPECT_FALSE(all.usedIndex);
}

TEST_F(ExecutorTest, IndexIsRebuiltFromFileOnOpen) {
    writeTextFile(dir + "/users.db", "5,가,1\n2,나,2\n9,다,3\n");
    Database database(dir);

    QueryResult first = run(database, "SELECT name FROM users WHERE id = 9;");
    EXPECT_TRUE(first.openedTable);
    EXPECT_EQ(first.indexedRecords, 3);
    EXPECT_EQ(first.rows, (Rows{{"다"}}));

    // 이미 연 테이블은 다시 만들지 않는다.
    QueryResult second = run(database, "SELECT name FROM users WHERE id = 2;");
    EXPECT_FALSE(second.openedTable);
    EXPECT_EQ(second.rows, (Rows{{"나"}}));

    // 다음 id는 가장 큰 id(9) + 1이다. 줄 순서와 상관없다.
    QueryResult inserted = run(database, "INSERT INTO users VALUES ('라', 4);");
    EXPECT_EQ(inserted.insertedId, 10);
}

TEST_F(ExecutorTest, InsertedRowIsFoundByIndexRightAway) {
    Database database(dir);
    for (int i = 0; i < 300; i++) run(database, "INSERT INTO users VALUES ('사람', 1);");
    run(database, "INSERT INTO users VALUES ('마지막,쉼표\n줄바꿈', 77);");

    QueryResult result = run(database, "SELECT name, age FROM users WHERE id = 301;");
    EXPECT_TRUE(result.usedIndex);
    EXPECT_EQ(result.rows, (Rows{{"마지막,쉼표\n줄바꿈", "77"}}));
}

TEST_F(ExecutorTest, IndexWorksWithWindowsLineEndingsAndMissingLastNewline) {
    // 손으로 고친 파일: 윈도우 줄바꿈, 빈 줄, 마지막 줄에 줄바꿈 없음
    writeTextFile(dir + "/users.db", "1,민수,30\r\n\r\n2,동연,25");
    Database database(dir);
    QueryResult inserted = run(database, "INSERT INTO users VALUES ('지수', 22);");
    ASSERT_TRUE(inserted.ok) << inserted.error;
    EXPECT_EQ(inserted.insertedId, 3);

    // 새 줄이 마지막 줄에 붙지 않고 따로 써졌어야 한다.
    EXPECT_EQ(readTextFile(dir + "/users.db"), "1,민수,30\r\n\r\n2,동연,25\n3,지수,22\n");
    EXPECT_EQ(run(database, "SELECT name FROM users WHERE id = 1;").rows, (Rows{{"민수"}}));
    EXPECT_EQ(run(database, "SELECT name FROM users WHERE id = 2;").rows, (Rows{{"동연"}}));
    EXPECT_EQ(run(database, "SELECT name FROM users WHERE id = 3;").rows, (Rows{{"지수"}}));

    // 껐다 켜도 같다.
    Database reopened(dir);
    EXPECT_EQ(run(reopened, "SELECT name FROM users WHERE id = 3;").rows, (Rows{{"지수"}}));
}

TEST_F(ExecutorTest, DuplicateIdInFileFails) {
    writeTextFile(dir + "/users.db", "1,민수,30\n1,동연,25\n");
    Database database(dir);
    QueryResult result = run(database, "SELECT * FROM users;");
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("겹친다"), std::string::npos) << result.error;
}

TEST_F(ExecutorTest, WriteFailureLeavesIndexAndIdUntouched) {
    // 데이터 파일 자리에 폴더가 있으면 쓰기가 실패한다.
    std::filesystem::create_directory(dir + "/users.db");
    Database database(dir);
    QueryResult failed = run(database, "INSERT INTO users VALUES ('민수', 30);");
    EXPECT_FALSE(failed.ok);
    EXPECT_NE(failed.error.find("열 수 없다"), std::string::npos) << failed.error;
    QueryResult lookup = run(database, "SELECT * FROM users WHERE id = 1;");
    EXPECT_TRUE(lookup.rows.empty());
}
