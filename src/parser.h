#pragma once

#include <string>
#include <vector>

#include "tokenizer.h"

// SQL에 적힌 값 하나. 숫자거나 글자다.
struct Value {
    bool isText = false;
    long long number = 0;
    std::string text;
};

enum class StatementType { Insert, Select };

// 파싱이 끝난 문장 하나. 실행기는 이 구조만 보고 동작한다.
struct Statement {
    StatementType type = StatementType::Select;
    std::string table;

    // INSERT: 넣을 값들 (id는 자동이라 들어 있지 않다)
    std::vector<Value> values;

    // SELECT: selectAll이 true면 '*', 아니면 columns 순서대로 출력
    bool selectAll = false;
    std::vector<std::string> columns;

    // SELECT: WHERE 컬럼 = 값
    bool hasWhere = false;
    std::string whereColumn;
    Value whereValue;
};

// 문장 하나의 토큰을 문법에 맞춰 Statement로 만든다. 문법이 틀리면 false와 error.
bool parseStatement(const std::vector<Token>& tokens, Statement& statement, std::string& error);
