#pragma once

#include <cstddef>
#include <string>

#include "database.h"
#include "sql_runner.h"

namespace httplib {
class Server;
}

// POST /query 본문의 최대 크기. 넘으면 읽지 않고 413으로 거절한다.
constexpr size_t MAX_QUERY_BYTES = 8 * 1024 * 1024;

// 글자를 JSON 문자열 안에 넣을 수 있게 바꾼다. 따옴표, 역슬래시, 줄바꿈 같은 제어 글자를 이스케이프한다.
std::string escapeJson(const std::string& text);

// SQL 실행 결과를 응답 JSON으로 만든다.
// {"ok": 전부 성공했나, "results": [문장마다 {"line", "ok", "type", ...}]}
std::string buildQueryJson(const RunOutcome& run);

// 서버에 API 경로를 등록한다.
//   GET  /health  서버가 살아 있는지 확인
//   POST /query   본문의 SQL을 실행하고 결과를 JSON으로 돌려준다
void registerRoutes(httplib::Server& server, Database& database);
