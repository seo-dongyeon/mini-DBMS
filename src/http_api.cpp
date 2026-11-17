#include "http_api.h"

#include <httplib.h>

#include <cstdio>

#include "storage.h"

namespace {

const char* JSON_TYPE = "application/json; charset=utf-8";
const char* PAYLOAD_TOO_LARGE_JSON = "{\"ok\":false,\"error\":\"본문이 너무 크다 (8MB까지)\"}";

// 칸 하나를 JSON 값으로 만든다. INT 컬럼이고 정수로 읽히면 숫자, 아니면 문자열.
std::string cellToJson(const std::string& cell, bool isInt) {
    long long number = 0;
    if (isInt && parseInteger(cell, number)) return std::to_string(number);
    return "\"" + escapeJson(cell) + "\"";
}

// 문장 하나의 결과를 JSON 객체로 만든다.
std::string statementToJson(const StatementOutcome& outcome) {
    const QueryResult& result = outcome.result;
    std::string json = "{\"line\":" + std::to_string(outcome.line);
    if (!result.ok) {
        return json + ",\"ok\":false,\"error\":\"" + escapeJson(result.error) + "\"}";
    }
    if (!result.isSelect) {
        return json + ",\"ok\":true,\"type\":\"insert\",\"id\":" + std::to_string(result.insertedId) + "}";
    }

    json += ",\"ok\":true,\"type\":\"select\",\"columns\":[";
    for (size_t i = 0; i < result.columnNames.size(); i++) {
        if (i > 0) json += ',';
        json += "\"" + escapeJson(result.columnNames[i]) + "\"";
    }
    json += "],\"rows\":[";
    for (size_t r = 0; r < result.rows.size(); r++) {
        if (r > 0) json += ',';
        json += '[';
        for (size_t c = 0; c < result.rows[r].size(); c++) {
            if (c > 0) json += ',';
            json += cellToJson(result.rows[r][c], result.columnIsInt[c]);
        }
        json += ']';
    }
    json += "],\"rowCount\":" + std::to_string(result.rows.size());
    json += std::string(",\"usedIndex\":") + (result.usedIndex ? "true" : "false") + "}";
    return json;
}

}  // namespace

std::string escapeJson(const std::string& text) {
    std::string out;
    for (unsigned char c : text) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    // 나머지 제어 글자는 JSON에 그대로 넣을 수 없어서 \u00XX로 적는다.
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out += static_cast<char>(c);   // 한글 같은 UTF-8 바이트는 그대로 둔다
                }
        }
    }
    return out;
}

std::string buildQueryJson(const RunOutcome& run) {
    std::string json = std::string("{\"ok\":") + (run.hadError ? "false" : "true") + ",\"results\":[";
    for (size_t i = 0; i < run.statements.size(); i++) {
        if (i > 0) json += ',';
        json += statementToJson(run.statements[i]);
    }
    return json + "]}";
}

void registerRoutes(httplib::Server& server, Database& database) {
    server.Get("/health", [](const httplib::Request&, httplib::Response& response) {
        response.set_content("{\"ok\":true}", JSON_TYPE);
    });

    // 본문 전체가 SQL이다. 문장 여러 개를 보내도 된다.
    // 상태 코드: 전부 성공하면 200, 실패한 문장이 있으면 400 (본문에 문장별 결과가 있다)
    // 본문이 MAX_QUERY_BYTES를 넘으면 httplib가 이 핸들러를 부르지 않고 413을 낸다. 본문은 아래 에러 처리에서 채운다.
    server.Post("/query", [&database](const httplib::Request& request, httplib::Response& response) {
        RunOutcome run = runSql(database, request.body);
        if (run.statements.empty()) {
            response.status = 400;
            response.set_content("{\"ok\":false,\"error\":\"실행할 SQL 문장이 없다\",\"results\":[]}", JSON_TYPE);
            return;
        }
        response.status = run.hadError ? 400 : 200;
        response.set_content(buildQueryJson(run), JSON_TYPE);
    });

    // httplib가 만든 에러 응답(없는 경로, 너무 큰 본문 등)도 JSON으로 알려 준다.
    server.set_error_handler([](const httplib::Request& request, httplib::Response& response) {
        if (!response.body.empty()) return;   // /query가 이미 본문을 채운 400은 그대로 둔다
        std::string message;
        if (response.status == 404) {
            message = "지원하지 않는 요청이다: " + request.method + " " + request.path +
                      " (GET /health, POST /query만 있다)";
        } else if (response.status == 413) {
            response.set_content(PAYLOAD_TOO_LARGE_JSON, JSON_TYPE);
            return;
        } else {
            message = "요청을 처리하지 못했다 (HTTP " + std::to_string(response.status) + ")";
        }
        response.set_content("{\"ok\":false,\"error\":\"" + escapeJson(message) + "\"}", JSON_TYPE);
    });
}
