#include <gtest/gtest.h>

#include "parser.h"

// SQL 한 문장을 토큰화해서 파싱한다.
static bool parseSql(const std::string& sql, Statement& statement, std::string& error) {
    TokenizeResult tokenized = tokenize(sql);
    EXPECT_TRUE(tokenized.ok) << tokenized.error;
    return parseStatement(tokenized.tokens, statement, error);
}

// 파싱이 실패해야 하는 SQL인지 확인한다.
static void expectParseError(const std::string& sql) {
    Statement statement;
    std::string error;
    EXPECT_FALSE(parseSql(sql, statement, error)) << sql;
    EXPECT_FALSE(error.empty()) << sql;
}

TEST(Parser, Insert) {
    Statement statement;
    std::string error;
    ASSERT_TRUE(parseSql("INSERT INTO users VALUES ('동연', 25);", statement, error)) << error;
    EXPECT_EQ(statement.type, StatementType::Insert);
    EXPECT_EQ(statement.table, "users");
    ASSERT_EQ(statement.values.size(), 2u);
    EXPECT_TRUE(statement.values[0].isText);
    EXPECT_EQ(statement.values[0].text, "동연");
    EXPECT_FALSE(statement.values[1].isText);
    EXPECT_EQ(statement.values[1].number, 25);
}

TEST(Parser, SelectStar) {
    Statement statement;
    std::string error;
    ASSERT_TRUE(parseSql("SELECT * FROM users;", statement, error)) << error;
    EXPECT_EQ(statement.type, StatementType::Select);
    EXPECT_TRUE(statement.selectAll);
    EXPECT_FALSE(statement.hasWhere);
    EXPECT_EQ(statement.table, "users");
}

TEST(Parser, SelectColumnsWithWhere) {
    Statement statement;
    std::string error;
    ASSERT_TRUE(parseSql("SELECT name, id FROM users WHERE age = 25;", statement, error)) << error;
    EXPECT_FALSE(statement.selectAll);
    std::vector<std::string> expected = {"name", "id"};
    EXPECT_EQ(statement.columns, expected);
    EXPECT_TRUE(statement.hasWhere);
    EXPECT_EQ(statement.whereColumn, "age");
    EXPECT_EQ(statement.whereValue.number, 25);
}

TEST(Parser, WhereWithText) {
    Statement statement;
    std::string error;
    ASSERT_TRUE(parseSql("SELECT * FROM users WHERE name = '동연';", statement, error)) << error;
    EXPECT_TRUE(statement.whereValue.isText);
    EXPECT_EQ(statement.whereValue.text, "동연");
}

TEST(Parser, KeywordsIgnoreCase) {
    Statement statement;
    std::string error;
    EXPECT_TRUE(parseSql("select * from users where id = 1;", statement, error)) << error;
    EXPECT_TRUE(parseSql("Insert Into users Values (1);", statement, error)) << error;
}

TEST(Parser, TableNameKeepsCase) {
    Statement statement;
    std::string error;
    ASSERT_TRUE(parseSql("SELECT * FROM Users;", statement, error)) << error;
    EXPECT_EQ(statement.table, "Users");
}

TEST(Parser, RejectsBrokenGrammar) {
    expectParseError("SELECT * users;");                          // FROM 없음
    expectParseError("SELECT FROM users;");                       // 컬럼 없음
    expectParseError("SELECT name, FROM users;");                 // 쉼표 뒤에 컬럼 없음
    expectParseError("SELECT * FROM users WHERE id 1;");          // = 없음
    expectParseError("SELECT * FROM users WHERE id = ;");         // 값 없음
    expectParseError("SELECT * FROM users WHERE id = 1 id = 2;"); // 뒤에 남는 토큰
    expectParseError("INSERT users VALUES (1);");                 // INTO 없음
    expectParseError("INSERT INTO users VALUES ();");             // 값 없음
    expectParseError("INSERT INTO users VALUES (1,);");           // 쉼표 뒤에 값 없음
    expectParseError("INSERT INTO users VALUES (1;");             // ) 없음
    expectParseError("INSERT INTO users VALUES (name);");         // 값 자리에 이름
    expectParseError("INSERT INTO 'users' VALUES (1);");          // 테이블 이름이 글자 값
}

TEST(Parser, RejectsMissingSemicolon) {
    Statement statement;
    std::string error;
    EXPECT_FALSE(parseSql("SELECT * FROM users", statement, error));
    EXPECT_NE(error.find(";"), std::string::npos);
}

TEST(Parser, RejectsUnsupportedCommand) {
    expectParseError("DELETE FROM users;");
    expectParseError("CREATE TABLE users (id INT);");
    expectParseError("42;");
}

TEST(Parser, EmptyTokensFail) {
    Statement statement;
    std::string error;
    EXPECT_FALSE(parseStatement({}, statement, error));
}
