#pragma once

#include <string>
#include <vector>

#include "database.h"
#include "parser.h"

// 문장 하나를 실행한 결과
struct QueryResult {
    bool ok = true;
    std::string error;

    bool isSelect = false;
    long long insertedId = 0;                     // INSERT: 붙인 id
    std::vector<std::string> columnNames;         // SELECT: 출력할 컬럼 이름
    std::vector<bool> columnIsInt;                // SELECT: 그 컬럼이 INT인지 (JSON에서 숫자로 낼지 정한다)
    std::vector<std::vector<std::string>> rows;   // SELECT: 조건에 맞은 줄들
    bool usedIndex = false;                       // SELECT: B+ 트리로 찾았으면 true, 파일 전체를 읽었으면 false

    // 이 문장에서 테이블을 처음 열었으면(인덱스를 새로 만들었으면) 그 시간과 건수. 성능 측정용.
    bool openedTable = false;
    double openMs = 0;
    long long indexedRecords = 0;
};

// 파싱된 문장 하나를 실행한다.
QueryResult executeStatement(Database& database, const Statement& statement);

// 성공한 결과를 화면에 찍을 글자로 만든다.
std::string formatResult(const QueryResult& result);
