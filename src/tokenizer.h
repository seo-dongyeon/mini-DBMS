#pragma once

#include <string>
#include <vector>

// 토큰의 종류. Word는 키워드와 이름(테이블, 컬럼)을 모두 담는다.
enum class TokenType {
    Word,
    Number,
    String,
    Star,
    Comma,
    LeftParen,
    RightParen,
    Equal,
    Semicolon,
};

// SQL 글자를 쪼갠 조각 하나
struct Token {
    TokenType type = TokenType::Word;
    std::string text;       // Word: 단어, String: 따옴표를 뗀 내용, 나머지: 원래 글자
    long long number = 0;   // Number일 때의 값
    int line = 1;           // 이 토큰이 시작한 줄 번호 (에러 메시지용)
};

// 토큰화 결과. 실패해도 tokens에는 에러 지점 앞까지의 토큰이 남는다.
struct TokenizeResult {
    std::vector<Token> tokens;
    bool ok = true;
    std::string error;
    int errorLine = 0;
};

// SQL 글자 전체를 토큰 목록으로 쪼갠다.
TokenizeResult tokenize(const std::string& sql);

// 토큰 목록을 ';' 기준으로 문장 단위로 나눈다. 각 문장 끝에는 ';' 토큰이 붙어 있다.
// 마지막 문장이 ';' 없이 끝났으면 ';' 없이 그대로 담긴다.
std::vector<std::vector<Token>> splitStatements(const std::vector<Token>& tokens);
