#include <gtest/gtest.h>

#include "tokenizer.h"

// 토큰들의 text만 뽑아 비교하기 쉽게 만든다.
static std::vector<std::string> textsOf(const std::vector<Token>& tokens) {
    std::vector<std::string> texts;
    for (const Token& token : tokens) texts.push_back(token.text);
    return texts;
}

TEST(Tokenizer, SplitsSelectIntoTokens) {
    TokenizeResult result = tokenize("SELECT * FROM users WHERE id = 1;");
    ASSERT_TRUE(result.ok);
    std::vector<std::string> expected = {"SELECT", "*", "FROM", "users", "WHERE", "id", "=", "1", ";"};
    EXPECT_EQ(textsOf(result.tokens), expected);
    EXPECT_EQ(result.tokens[1].type, TokenType::Star);
    EXPECT_EQ(result.tokens[7].type, TokenType::Number);
    EXPECT_EQ(result.tokens[7].number, 1);
}

TEST(Tokenizer, SpacingDoesNotChangeTokens) {
    std::vector<std::string> a = textsOf(tokenize("WHERE id = 1").tokens);
    std::vector<std::string> b = textsOf(tokenize("WHERE id=1").tokens);
    std::vector<std::string> c = textsOf(tokenize("WHERE \t id  =\r\n  1").tokens);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
}

TEST(Tokenizer, StringKeepsSpacesAndSemicolons) {
    TokenizeResult result = tokenize("('안녕; 반가워, 친구')");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.tokens.size(), 3u);
    EXPECT_EQ(result.tokens[1].type, TokenType::String);
    EXPECT_EQ(result.tokens[1].text, "안녕; 반가워, 친구");
}

TEST(Tokenizer, DoubledQuoteIsOneQuote) {
    TokenizeResult result = tokenize("'it''s'");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.tokens[0].text, "it's");
}

TEST(Tokenizer, EmptyString) {
    TokenizeResult result = tokenize("''");
    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.tokens.size(), 1u);
    EXPECT_EQ(result.tokens[0].type, TokenType::String);
    EXPECT_EQ(result.tokens[0].text, "");
}

TEST(Tokenizer, NegativeNumber) {
    TokenizeResult result = tokenize("-42");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.tokens[0].type, TokenType::Number);
    EXPECT_EQ(result.tokens[0].number, -42);
}

TEST(Tokenizer, CommentIsSkipped) {
    TokenizeResult result = tokenize("-- 주석 ; 'x\nSELECT -- 뒤 주석\n*");
    ASSERT_TRUE(result.ok);
    std::vector<std::string> expected = {"SELECT", "*"};
    EXPECT_EQ(textsOf(result.tokens), expected);
}

TEST(Tokenizer, TracksLineNumbers) {
    TokenizeResult result = tokenize("SELECT\n*\n\nFROM");
    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.tokens[0].line, 1);
    EXPECT_EQ(result.tokens[1].line, 2);
    EXPECT_EQ(result.tokens[2].line, 4);
}

TEST(Tokenizer, UnterminatedStringFails) {
    TokenizeResult result = tokenize("SELECT 1;\nINSERT 'abc");
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.errorLine, 2);
    // 에러 앞의 토큰은 남아 있다.
    EXPECT_EQ(result.tokens.size(), 4u);
}

TEST(Tokenizer, UnknownCharacterFails) {
    EXPECT_FALSE(tokenize("SELECT @").ok);
    EXPECT_FALSE(tokenize("SELECT 이름").ok);
}

TEST(Tokenizer, NumberGluedToLettersFails) {
    EXPECT_FALSE(tokenize("12abc").ok);
}

TEST(Tokenizer, TooBigNumberFails) {
    EXPECT_FALSE(tokenize("99999999999999999999999").ok);
}

TEST(SplitStatements, SplitsOnSemicolon) {
    TokenizeResult result = tokenize("SELECT 1; ;; SELECT 2;");
    std::vector<std::vector<Token>> statements = splitStatements(result.tokens);
    ASSERT_EQ(statements.size(), 2u);
    EXPECT_EQ(statements[0].back().type, TokenType::Semicolon);
    EXPECT_EQ(statements[1][1].number, 2);
}

TEST(SplitStatements, SemicolonInsideStringDoesNotSplit) {
    TokenizeResult result = tokenize("INSERT INTO memo VALUES ('안녕; 반가워');");
    EXPECT_EQ(splitStatements(result.tokens).size(), 1u);
}

TEST(SplitStatements, LastStatementWithoutSemicolonIsKept) {
    TokenizeResult result = tokenize("SELECT 1; SELECT 2");
    std::vector<std::vector<Token>> statements = splitStatements(result.tokens);
    ASSERT_EQ(statements.size(), 2u);
    EXPECT_NE(statements[1].back().type, TokenType::Semicolon);
}

TEST(SplitStatements, EmptyInputHasNoStatements) {
    EXPECT_TRUE(splitStatements(tokenize("  \n -- 주석만\n").tokens).empty());
}
