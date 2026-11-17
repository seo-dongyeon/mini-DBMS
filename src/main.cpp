#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "sql_runner.h"

namespace {

// 사용법을 출력한다.
void printUsage() {
    std::cerr << "사용법: sqlproc [--data 데이터폴더] [--timing] SQL파일\n"
              << "  --data    스키마(.schema)와 데이터(.db) 파일이 있는 폴더. 기본값은 data\n"
              << "  --timing  문장마다 걸린 시간을 표준 에러로 출력한다\n";
}

// 에러 한 줄을 출력한다. 결과 출력과 순서가 섞이지 않게 표준 출력을 먼저 비운다.
void printError(int line, const std::string& message) {
    std::cout.flush();
    std::cerr << "ERROR (" << line << "번째 줄): " << message << "\n";
}

// 밀리초를 소수점 셋째 자리까지 글자로 만든다.
std::string formatMs(double ms) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", ms);
    return buffer;
}

// --timing일 때 문장 하나의 시간을 출력한다. 테이블 열기(인덱스 만들기)와 문장 실행을 따로 보여 준다.
void printTiming(int line, const Statement& statement, const QueryResult& result, double totalMs) {
    std::cout.flush();
    double queryMs = totalMs;
    if (result.openedTable) {
        std::cerr << "[timing] " << line << "번째 줄: " << statement.table << " 테이블 열기 + 인덱스 만들기 "
                  << formatMs(result.openMs) << " ms (" << result.indexedRecords << "건)\n";
        queryMs -= result.openMs;
    }
    std::string kind = "INSERT";
    if (result.isSelect) kind = result.usedIndex ? "SELECT (B+ 트리)" : "SELECT (선형 탐색)";
    std::cerr << "[timing] " << line << "번째 줄: " << kind << " " << formatMs(queryMs) << " ms";
    if (result.isSelect) std::cerr << ", 결과 " << result.rows.size() << "줄";
    std::cerr << "\n";
}

// 파일 전체를 글자로 읽는다.
bool readWholeFile(const std::string& path, std::string& content) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    content = buffer.str();
    return true;
}

}  // namespace

// sqlproc [--data 폴더] [--timing] 파일.sql : SQL 파일을 읽어 문장을 차례로 실행한다.
// 종료 코드: 0 전부 성공, 1 실패한 문장이 있음, 2 사용법이나 파일 문제
int main(int argc, char* argv[]) {
    std::string dataDir = "data";
    std::string sqlPath;
    bool showTiming = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data") {
            if (i + 1 >= argc) {
                printUsage();
                return 2;
            }
            dataDir = argv[++i];
        } else if (arg == "--timing") {
            showTiming = true;
        } else if (sqlPath.empty()) {
            sqlPath = arg;
        } else {
            printUsage();
            return 2;
        }
    }
    if (sqlPath.empty()) {
        printUsage();
        return 2;
    }

    std::string sql;
    if (!readWholeFile(sqlPath, sql)) {
        std::cerr << "SQL 파일을 열 수 없다: " << sqlPath << "\n";
        return 2;
    }

    Database database(dataDir);
    RunOutcome run = runSql(database, sql);

    for (const StatementOutcome& outcome : run.statements) {
        if (!outcome.result.ok) {
            printError(outcome.line, outcome.result.error);
            continue;
        }
        std::cout << formatResult(outcome.result);
        if (showTiming) printTiming(outcome.line, outcome.statement, outcome.result, outcome.totalMs);
    }
    return run.hadError ? 1 : 0;
}
