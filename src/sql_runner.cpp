#include "sql_runner.h"

#include <chrono>

#include "tokenizer.h"

namespace {

// 실패한 문장 결과를 만든다.
StatementOutcome makeFailure(int line, const std::string& message) {
    StatementOutcome outcome;
    outcome.line = line;
    outcome.result.ok = false;
    outcome.result.error = message;
    return outcome;
}

}  // namespace

RunOutcome runSql(Database& database, std::string sql) {
    RunOutcome run;

    // 윈도우 메모장이 파일 맨 앞에 붙이는 표시(UTF-8 BOM)는 SQL이 아니므로 뗀다.
    if (sql.compare(0, 3, "\xEF\xBB\xBF") == 0) sql.erase(0, 3);

    TokenizeResult tokenized = tokenize(sql);
    std::vector<std::vector<Token>> statements = splitStatements(tokenized.tokens);

    // 토큰화가 중간에 실패했으면 마지막 문장은 잘린 조각이다. 실행하지 않는다.
    if (!tokenized.ok && !statements.empty() && statements.back().back().type != TokenType::Semicolon) {
        statements.pop_back();
    }

    for (const std::vector<Token>& tokens : statements) {
        int line = tokens.front().line;

        Statement statement;
        std::string error;
        if (!parseStatement(tokens, statement, error)) {
            run.statements.push_back(makeFailure(line, error));
            run.hadError = true;
            continue;
        }

        StatementOutcome outcome;
        outcome.line = line;
        outcome.statement = statement;
        auto start = std::chrono::steady_clock::now();
        outcome.result = executeStatement(database, statement);
        outcome.totalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!outcome.result.ok) run.hadError = true;
        run.statements.push_back(outcome);
    }

    if (!tokenized.ok) {
        run.statements.push_back(makeFailure(tokenized.errorLine, tokenized.error));
        run.hadError = true;
    }
    return run;
}
