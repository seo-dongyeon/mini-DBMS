#include "parser.h"

#include <cctype>

namespace {

// 토큰 목록을 앞에서부터 하나씩 읽어 나가는 커서
class TokenCursor {
public:
    explicit TokenCursor(const std::vector<Token>& tokens) : tokens_(tokens) {}

    // 지금 위치의 토큰을 돌려준다. 끝이면 nullptr.
    const Token* peek() const {
        return position_ < tokens_.size() ? &tokens_[position_] : nullptr;
    }

    // 다음 토큰으로 넘어간다.
    void advance() { position_++; }

private:
    const std::vector<Token>& tokens_;
    size_t position_ = 0;
};

// 에러 메시지에 넣을 토큰 설명을 만든다.
std::string describeToken(const Token* token) {
    if (token == nullptr) return "문장의 끝";
    return "'" + token->text + "'";
}

// 토큰이 주어진 키워드인지 대소문자 구분 없이 확인한다. keyword는 대문자로 준다.
bool isKeyword(const Token* token, const std::string& keyword) {
    if (token == nullptr || token->type != TokenType::Word) return false;
    if (token->text.size() != keyword.size()) return false;
    for (size_t i = 0; i < keyword.size(); i++) {
        if (std::toupper(static_cast<unsigned char>(token->text[i])) != keyword[i]) return false;
    }
    return true;
}

// 지금 토큰이 그 키워드면 넘어가고, 아니면 에러를 남긴다.
bool expectKeyword(TokenCursor& cursor, const std::string& keyword, std::string& error) {
    if (!isKeyword(cursor.peek(), keyword)) {
        error = keyword + " 자리에 다른 것이 왔다: " + describeToken(cursor.peek());
        return false;
    }
    cursor.advance();
    return true;
}

// 지금 토큰이 그 기호면 넘어가고, 아니면 에러를 남긴다.
bool expectSymbol(TokenCursor& cursor, TokenType type, const std::string& symbol, std::string& error) {
    const Token* token = cursor.peek();
    if (token == nullptr || token->type != type) {
        error = "'" + symbol + "' 자리에 다른 것이 왔다: " + describeToken(token);
        return false;
    }
    cursor.advance();
    return true;
}

// 테이블이나 컬럼 이름을 읽는다. what은 에러 메시지에 쓸 이름("테이블 이름" 등).
bool expectName(TokenCursor& cursor, const std::string& what, std::string& name, std::string& error) {
    const Token* token = cursor.peek();
    if (token == nullptr || token->type != TokenType::Word) {
        error = what + " 자리에 다른 것이 왔다: " + describeToken(token);
        return false;
    }
    name = token->text;
    cursor.advance();
    return true;
}

// 숫자나 글자 값 하나를 읽는다.
bool expectValue(TokenCursor& cursor, Value& value, std::string& error) {
    const Token* token = cursor.peek();
    if (token != nullptr && token->type == TokenType::Number) {
        value.isText = false;
        value.number = token->number;
    } else if (token != nullptr && token->type == TokenType::String) {
        value.isText = true;
        value.text = token->text;
    } else {
        error = "값(숫자나 '글자') 자리에 다른 것이 왔다: " + describeToken(token);
        return false;
    }
    cursor.advance();
    return true;
}

// 문장이 ';'로 끝나고 그 뒤에 남은 게 없는지 확인한다.
bool expectStatementEnd(TokenCursor& cursor, std::string& error) {
    const Token* token = cursor.peek();
    if (token == nullptr) {
        error = "문장 끝에 ';'가 없다";
        return false;
    }
    if (token->type != TokenType::Semicolon) {
        error = "문장이 끝나야 할 자리에 다른 것이 왔다: " + describeToken(token);
        return false;
    }
    return true;
}

// INSERT INTO 테이블 VALUES (값, 값, ...) ;
bool parseInsert(TokenCursor& cursor, Statement& statement, std::string& error) {
    statement.type = StatementType::Insert;
    if (!expectKeyword(cursor, "INTO", error)) return false;
    if (!expectName(cursor, "테이블 이름", statement.table, error)) return false;
    if (!expectKeyword(cursor, "VALUES", error)) return false;
    if (!expectSymbol(cursor, TokenType::LeftParen, "(", error)) return false;

    while (true) {
        Value value;
        if (!expectValue(cursor, value, error)) return false;
        statement.values.push_back(value);

        const Token* token = cursor.peek();
        if (token == nullptr || token->type != TokenType::Comma) break;
        cursor.advance();
    }

    if (!expectSymbol(cursor, TokenType::RightParen, ")", error)) return false;
    return expectStatementEnd(cursor, error);
}

// SELECT (* | 컬럼, 컬럼, ...) FROM 테이블 [WHERE 컬럼 = 값] ;
bool parseSelect(TokenCursor& cursor, Statement& statement, std::string& error) {
    statement.type = StatementType::Select;

    const Token* first = cursor.peek();
    if (first != nullptr && first->type == TokenType::Star) {
        statement.selectAll = true;
        cursor.advance();
    } else {
        while (true) {
            std::string column;
            if (!expectName(cursor, "컬럼 이름이나 '*'", column, error)) return false;
            statement.columns.push_back(column);

            const Token* token = cursor.peek();
            if (token == nullptr || token->type != TokenType::Comma) break;
            cursor.advance();
        }
    }

    if (!expectKeyword(cursor, "FROM", error)) return false;
    if (!expectName(cursor, "테이블 이름", statement.table, error)) return false;

    if (isKeyword(cursor.peek(), "WHERE")) {
        cursor.advance();
        statement.hasWhere = true;
        if (!expectName(cursor, "컬럼 이름", statement.whereColumn, error)) return false;
        if (!expectSymbol(cursor, TokenType::Equal, "=", error)) return false;
        if (!expectValue(cursor, statement.whereValue, error)) return false;
    }

    return expectStatementEnd(cursor, error);
}

}  // namespace

bool parseStatement(const std::vector<Token>& tokens, Statement& statement, std::string& error) {
    statement = Statement();
    TokenCursor cursor(tokens);

    // 첫 토큰으로 어떤 명령인지 정한다. Pintos의 syscall 번호 분기와 같은 자리다.
    const Token* first = cursor.peek();
    if (isKeyword(first, "INSERT")) {
        cursor.advance();
        return parseInsert(cursor, statement, error);
    }
    if (isKeyword(first, "SELECT")) {
        cursor.advance();
        return parseSelect(cursor, statement, error);
    }

    error = "지원하지 않는 명령이다: " + describeToken(first) + " (INSERT, SELECT만 지원한다)";
    return false;
}
