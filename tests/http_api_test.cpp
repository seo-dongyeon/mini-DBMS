#include <gtest/gtest.h>
#include <httplib.h>

#include <filesystem>
#include <thread>

#include "http_api.h"
#include "test_util.h"

TEST(HttpApi, EscapeJson) {
    EXPECT_EQ(escapeJson("plain"), "plain");
    EXPECT_EQ(escapeJson("say \"hi\""), "say \\\"hi\\\"");
    EXPECT_EQ(escapeJson("a\\b"), "a\\\\b");
    EXPECT_EQ(escapeJson("줄\n바꿈\r\t"), "줄\\n바꿈\\r\\t");
    EXPECT_EQ(escapeJson(std::string("\x01", 1)), "\\u0001");
    EXPECT_EQ(escapeJson("한글"), "한글");
}

class HttpApiTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = makeTempDataDir();
        writeTextFile(dir + "/users.schema", "id INT\nname TEXT\nage INT\n");
    }
    void TearDown() override { std::filesystem::remove_all(dir); }

    std::string dir;
};

TEST_F(HttpApiTest, InsertAndSelectJson) {
    Database database(dir);
    RunOutcome run = runSql(database,
                            "INSERT INTO users VALUES ('민\"수', 30);\n"
                            "SELECT * FROM users WHERE id = 1;\n"
                            "SELECT name FROM users WHERE age = 99;");
    EXPECT_EQ(buildQueryJson(run),
              "{\"ok\":true,\"results\":["
              "{\"line\":1,\"ok\":true,\"type\":\"insert\",\"id\":1},"
              "{\"line\":2,\"ok\":true,\"type\":\"select\",\"columns\":[\"id\",\"name\",\"age\"],"
              "\"rows\":[[1,\"민\\\"수\",30]],\"rowCount\":1,\"usedIndex\":true},"
              "{\"line\":3,\"ok\":true,\"type\":\"select\",\"columns\":[\"name\"],"
              "\"rows\":[],\"rowCount\":0,\"usedIndex\":false}"
              "]}");
}

TEST_F(HttpApiTest, ErrorsAreReportedPerStatement) {
    Database database(dir);
    RunOutcome run = runSql(database, "SELECT * FROM nothing;\nINSERT INTO users VALUES ('a', 1);");
    std::string json = buildQueryJson(run);
    EXPECT_EQ(json.rfind("{\"ok\":false,", 0), 0u) << json;
    EXPECT_NE(json.find("\"line\":1,\"ok\":false,\"error\":\"테이블이 없다"), std::string::npos) << json;
    EXPECT_NE(json.find("\"line\":2,\"ok\":true,\"type\":\"insert\",\"id\":1"), std::string::npos) << json;
}

TEST_F(HttpApiTest, BrokenIntCellIsSentAsString) {
    // INT 컬럼인데 파일에 숫자가 아닌 값이 있으면, JSON이 깨지지 않게 문자열로 보낸다.
    writeTextFile(dir + "/users.db", "1,a,abc\n");
    Database database(dir);
    RunOutcome run = runSql(database, "SELECT age FROM users;");
    EXPECT_NE(buildQueryJson(run).find("\"rows\":[[\"abc\"]]"), std::string::npos);
}

// 실제 HTTP 서버를 이 프로세스 안에 띄우고 요청을 보내 본다. 경로 등록과 상태 코드를 확인한다.
class HttpServerTest : public HttpApiTest {
protected:
    void SetUp() override {
        HttpApiTest::SetUp();
        database = std::make_unique<Database>(dir);
        server.set_payload_max_length(MAX_QUERY_BYTES);
        registerRoutes(server, *database);
        port = server.bind_to_any_port("127.0.0.1");
        ASSERT_GT(port, 0);
        serverThread = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    void TearDown() override {
        server.stop();
        serverThread.join();
        HttpApiTest::TearDown();
    }

    // POST /query를 보내고 응답을 돌려준다.
    httplib::Result post(const std::string& body) {
        httplib::Client client("127.0.0.1", port);
        return client.Post("/query", body, "text/plain");
    }

    std::unique_ptr<Database> database;
    httplib::Server server;
    int port = 0;
    std::thread serverThread;
};

TEST_F(HttpServerTest, QueryReturnsStatusByResult) {
    httplib::Result ok = post("INSERT INTO users VALUES ('a', 1);");
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->status, 200);

    httplib::Result failed = post("SELECT * FROM nothing;");
    ASSERT_TRUE(failed);
    EXPECT_EQ(failed->status, 400);

    httplib::Result empty = post("");
    ASSERT_TRUE(empty);
    EXPECT_EQ(empty->status, 400);
    EXPECT_NE(empty->body.find("실행할 SQL 문장이 없다"), std::string::npos);
}

TEST_F(HttpServerTest, TooLargeBodyIs413NotEmptySql) {
    httplib::Result result = post(std::string(MAX_QUERY_BYTES + 1, 'x'));
    ASSERT_TRUE(result) << "연결이 끊겼다: " << httplib::to_string(result.error());
    EXPECT_EQ(result->status, 413);
    EXPECT_NE(result->body.find("본문이 너무 크다"), std::string::npos) << result->body;

    // 딱 한도까지는 받는다 (SQL로는 틀려서 400이지만 413은 아니다).
    httplib::Result atLimit = post(std::string(MAX_QUERY_BYTES, 'x'));
    ASSERT_TRUE(atLimit);
    EXPECT_EQ(atLimit->status, 400);

    // 거절한 뒤에도 서버는 계속 동작한다.
    httplib::Result after = post("SELECT * FROM users;");
    ASSERT_TRUE(after);
    EXPECT_EQ(after->status, 200);
}

TEST_F(HttpServerTest, UnknownPathIs404Json) {
    httplib::Client client("127.0.0.1", port);
    httplib::Result result = client.Get("/query");
    ASSERT_TRUE(result);
    EXPECT_EQ(result->status, 404);
    EXPECT_NE(result->body.find("POST /query"), std::string::npos) << result->body;
}
