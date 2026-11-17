#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "sql_runner.h"
#include "test_util.h"

// 여러 스레드가 같은 Database를 동시에 쓰는 상황. Phase 3 서버의 엔진이 이렇게 불린다.
class ConcurrencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        dir = makeTempDataDir();
        writeTextFile(dir + "/users.schema", "id INT\nname TEXT\nage INT\n");
    }
    void TearDown() override { std::filesystem::remove_all(dir); }

    std::string dir;
};

TEST_F(ConcurrencyTest, ParallelInsertsGetUniqueIdsAndAllAreFindable) {
    const int threadCount = 8;
    const int insertsPerThread = 200;
    Database database(dir);

    // 스레드마다 자기가 받은 (id, name)을 적어 둔다.
    std::vector<std::vector<std::pair<long long, std::string>>> inserted(threadCount);
    std::vector<std::thread> threads;
    for (int t = 0; t < threadCount; t++) {
        threads.emplace_back([&, t] {
            for (int i = 0; i < insertsPerThread; i++) {
                std::string name = "t" + std::to_string(t) + "_" + std::to_string(i);
                RunOutcome run = runSql(database, "INSERT INTO users VALUES ('" + name + "', " + std::to_string(t) + ");");
                if (run.hadError) continue;
                inserted[t].push_back({run.statements[0].result.insertedId, name});
            }
        });
    }
    for (std::thread& thread : threads) thread.join();

    // id가 겹치지 않고 1부터 빈틈없이 붙었어야 한다.
    std::set<long long> ids;
    for (const auto& list : inserted) {
        for (const auto& pair : list) ids.insert(pair.first);
    }
    const long long total = threadCount * insertsPerThread;
    ASSERT_EQ(static_cast<long long>(ids.size()), total) << "id가 겹쳤거나 INSERT가 실패했다";
    EXPECT_EQ(*ids.begin(), 1);
    EXPECT_EQ(*ids.rbegin(), total);

    // 파일에 정확히 total줄이 있어야 한다. 줄이 섞여 쓰였으면 수가 달라지거나 깨진다.
    std::string content = readTextFile(dir + "/users.db");
    EXPECT_EQ(std::count(content.begin(), content.end(), '\n'), total);

    // 받은 id로 찾으면 자기가 넣은 이름이 나와야 한다 (인덱스가 맞는 줄을 가리키는지).
    for (const auto& list : inserted) {
        for (const auto& pair : list) {
            RunOutcome run = runSql(database, "SELECT name FROM users WHERE id = " + std::to_string(pair.first) + ";");
            ASSERT_FALSE(run.hadError);
            ASSERT_EQ(run.statements[0].result.rows.size(), 1u);
            ASSERT_EQ(run.statements[0].result.rows[0][0], pair.second);
        }
    }

    // 껐다 켜도 파일에서 인덱스가 똑같이 만들어져야 한다.
    Database reopened(dir);
    RunOutcome run = runSql(reopened, "INSERT INTO users VALUES ('다시 켬', 0);");
    ASSERT_FALSE(run.hadError);
    EXPECT_EQ(run.statements[0].result.insertedId, total + 1);
}

TEST_F(ConcurrencyTest, ReadsDuringWritesSeeWholeRowsOnly) {
    Database database(dir);
    const int total = 600;
    std::atomic<long long> lastInserted{0};
    std::atomic<int> badReads{0};
    std::atomic<bool> done{false};

    // 쓰는 스레드 2개: 이름에 id를 넣어 둬서 읽는 쪽이 "반쯤 쓰인 줄"을 알아챌 수 있게 한다.
    std::vector<std::thread> threads;
    std::atomic<int> remaining{total};
    for (int w = 0; w < 2; w++) {
        threads.emplace_back([&] {
            while (remaining.fetch_sub(1) > 0) {
                RunOutcome run = runSql(database, "INSERT INTO users VALUES ('row', 7);");
                if (!run.hadError) {
                    long long id = run.statements[0].result.insertedId;
                    long long previous = lastInserted.load();
                    while (previous < id && !lastInserted.compare_exchange_weak(previous, id)) {
                    }
                }
            }
        });
    }

    // 읽는 스레드 4개: 이미 들어간 id를 인덱스로, 그리고 이름으로 계속 찾는다.
    for (int r = 0; r < 4; r++) {
        threads.emplace_back([&, r] {
            while (!done) {
                long long id = lastInserted.load();
                if (id == 0) continue;
                std::string sql = r % 2 == 0 ? "SELECT * FROM users WHERE id = " + std::to_string(id) + ";"
                                             : "SELECT id FROM users WHERE age = 7;";
                RunOutcome run = runSql(database, sql);
                if (run.hadError) {
                    badReads++;
                    continue;
                }
                const QueryResult& result = run.statements[0].result;
                if (r % 2 == 0 && (result.rows.size() != 1 || result.rows[0][1] != "row")) badReads++;
                if (r % 2 == 1 && result.rows.empty()) badReads++;
            }
        });
    }

    threads[0].join();
    threads[1].join();
    done = true;
    for (size_t i = 2; i < threads.size(); i++) threads[i].join();

    EXPECT_EQ(badReads, 0) << "쓰는 도중에 읽어서 깨진 줄이나 없는 줄을 봤다";
    RunOutcome all = runSql(database, "SELECT id FROM users;");
    EXPECT_EQ(all.statements[0].result.rows.size(), static_cast<size_t>(total));
}

TEST_F(ConcurrencyTest, SameTableOpenedOnceEvenWhenRacing) {
    writeTextFile(dir + "/users.db", "1,a,1\n2,b,2\n");
    Database database(dir);
    std::atomic<int> opened{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; i++) {
        threads.emplace_back([&] {
            RunOutcome run = runSql(database, "SELECT * FROM users WHERE id = 2;");
            if (!run.hadError && run.statements[0].result.openedTable) opened++;
        });
    }
    for (std::thread& thread : threads) thread.join();
    EXPECT_EQ(opened, 1) << "테이블(인덱스)은 한 번만 만들어져야 한다";
}
