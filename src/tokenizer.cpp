#include "tokenizer.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace {

// 이름(단어)의 첫 글자가 될 수 있는지 확인한다.
bool isWordStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
}

// 이름(단어)의 둘째 글자부터 쓸 수 있는 글자인지 확인한다.
bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

// 숫자 글자인지 확인한다.
bool isDigit(char c) {
    return std::isdigit(static_cast<unsigned char>(c));
}

// 글자 하나짜리 기호의 토큰 종류를 찾는다. 기호가 아니면 false.
bool findSymbolType(char c, TokenType& type) {
    switch (c) {
        case '*': type = TokenType::Star; return true;
        case ',': type = TokenType::Comma; return true;
        case '(': type = TokenType::LeftParen; return true;
        case ')': type = TokenType::RightParen; return true;
        case '=': type = TokenType::Equal; return true;
        case ';': type = TokenType::Semicolon; return true;
        default: return false;
    }
}

}  // namespace

TokenizeResult tokenize(const std::string& sql) {
    TokenizeResult result;
    int line = 1;
    size_t i = 0;

    // 에러를 기록한다. 호출한 쪽은 바로 result를 돌려준다.
    auto fail = [&result](const std::string& message, int errorLine) {
        result.ok = false;
        result.error = message;
        result.errorLine = errorLine;
    };

    while (i < sql.size()) {
        char c = sql[i];

        if (c == '\n') {
            line++;
            i++;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            i++;
            continue;
        }

        // "--"부터 줄 끝까지는 주석이다. 음수(-1)보다 먼저 확인해야 한다.
        if (c == '-' && i + 1 < sql.size() && sql[i + 1] == '-') {
            while (i < sql.size() && sql[i] != '\n') i++;
            continue;
        }

        if (isWordStart(c)) {
            size_t start = i;
            while (i < sql.size() && isWordChar(sql[i])) i++;
            Token token;
            token.type = TokenType::Word;
            token.text = sql.substr(start, i - start);
            token.line = line;
            result.tokens.push_back(token);
            continue;
        }

        bool isNegativeNumber = c == '-' && i + 1 < sql.size() && isDigit(sql[i + 1]);
        if (isDigit(c) || isNegativeNumber) {
            size_t start = i;
            i++;
            while (i < sql.size() && isDigit(sql[i])) i++;
            std::string text = sql.substr(start, i - start);
            // "12abc"처럼 숫자에 글자가 붙으면 숫자도 이름도 아니다.
            if (i < sql.size() && isWordChar(sql[i])) {
                fail("숫자 뒤에 글자가 붙어 있다: " + text + sql[i], line);
                return result;
            }
            errno = 0;
            long long value = std::strtoll(text.c_str(), nullptr, 10);
            if (errno == ERANGE) {
                fail("숫자가 너무 크다: " + text, line);
                return result;
            }
            Token token;
            token.type = TokenType::Number;
            token.text = text;
            token.number = value;
            token.line = line;
            result.tokens.push_back(token);
            continue;
        }

        if (c == '\'') {
            int startLine = line;
            i++;
            std::string text;
            bool closed = false;
            while (i < sql.size()) {
                if (sql[i] == '\'') {
                    // 따옴표 두 개('')는 글자 따옴표 하나를 뜻한다.
                    if (i + 1 < sql.size() && sql[i + 1] == '\'') {
                        text += '\'';
                        i += 2;
                        continue;
                    }
                    closed = true;
                    i++;
                    break;
                }
                if (sql[i] == '\n') line++;
                text += sql[i];
                i++;
            }
            if (!closed) {
                fail("따옴표가 닫히지 않았다", startLine);
                return result;
            }
            Token token;
            token.type = TokenType::String;
            token.text = text;
            token.line = startLine;
            result.tokens.push_back(token);
            continue;
        }

        TokenType symbolType;
        if (findSymbolType(c, symbolType)) {
            Token token;
            token.type = symbolType;
            token.text = std::string(1, c);
            token.line = line;
            result.tokens.push_back(token);
            i++;
            continue;
        }

        // 한글 같은 글자는 여러 바이트라서 한 바이트만 찍으면 깨진다.
        if (static_cast<unsigned char>(c) >= 0x80) {
            fail("따옴표 밖에 한글 같은 글자가 있다 (글자 값은 '...' 안에 써야 한다)", line);
        } else {
            fail(std::string("알 수 없는 글자: ") + c, line);
        }
        return result;
    }

    return result;
}

std::vector<std::vector<Token>> splitStatements(const std::vector<Token>& tokens) {
    std::vector<std::vector<Token>> statements;
    std::vector<Token> current;

    for (const Token& token : tokens) {
        if (token.type != TokenType::Semicolon) {
            current.push_back(token);
            continue;
        }
        // ";;"처럼 내용 없는 문장은 버린다.
        if (!current.empty()) {
            current.push_back(token);
            statements.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) statements.push_back(current);

    return statements;
}
