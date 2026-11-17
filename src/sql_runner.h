#pragma once

#include <string>
#include <vector>

#include "database.h"
#include "executor.h"
#include "parser.h"

// 문장 하나를 처리한 결과
struct StatementOutcome {
    int line = 0;           // 문장이 시작한 줄
    Statement statement;    // 파싱에 성공했을 때만 의미가 있다
    QueryResult result;     // ok가 false면 error에 이유 (토큰화·파싱·실행 어디서든)
    double totalMs = 0;     // 실행에 걸린 시간 (테이블 열기 포함)
};

// SQL 글자 전체를 처리한 결과. 문장 순서대로 담는다.
struct RunOutcome {
    std::vector<StatementOutcome> statements;
    bool hadError = false;
};

// SQL 글자 전체를 토큰화 → 문장 나누기 → 파싱 → 실행한다. 명령창(sqlproc)과 서버가 같이 쓴다.
// 문장 하나가 실패해도 다음 문장은 실행한다. 토큰화가 실패하면 그 앞의 완성된 문장까지만 실행한다.
RunOutcome runSql(Database& database, std::string sql);
