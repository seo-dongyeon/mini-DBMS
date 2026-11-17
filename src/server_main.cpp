#include <httplib.h>

#include <csignal>
#include <iostream>
#include <string>

#include "database.h"
#include "http_api.h"
#include "thread_pool.h"

namespace {

// httplib가 연결을 넘겨줄 대기열 자리에 우리 스레드 풀을 끼우는 연결부.
// httplib는 연결이 들어올 때마다 enqueue()를 부르고, 서버를 끌 때 shutdown()을 부른다.
class ThreadPoolTaskQueue : public httplib::TaskQueue {
public:
    ThreadPoolTaskQueue(int workerCount, int queueCapacity) : pool_(workerCount, queueCapacity) {}

    // 연결 하나를 처리하는 일을 스레드 풀 대기열에 넣는다. 꽉 차 있으면 자리가 날 때까지 기다린다.
    void enqueue(std::function<void()> job) override { pool_.submit(std::move(job)); }

    // 남은 일을 다 처리하고 일꾼을 끝낸다.
    void shutdown() override { pool_.shutdown(); }

private:
    ThreadPool pool_;
};

httplib::Server* runningServer = nullptr;

// Ctrl+C를 누르면 서버를 멈춘다. 그러면 listen()이 돌아오고 스레드 풀이 정리된다.
void stopOnSignal(int) {
    if (runningServer != nullptr) runningServer->stop();
}

// 사용법을 출력한다.
void printUsage() {
    std::cerr << "사용법: dbms_server [--data 폴더] [--host 주소] [--port 번호] [--threads 개수] [--queue 개수]\n"
              << "  --data     스키마와 데이터 파일 폴더. 기본값 data\n"
              << "  --host     기다릴 주소. 기본값 0.0.0.0 (컨테이너 밖에서도 접속 가능)\n"
              << "  --port     기본값 8080\n"
              << "  --threads  스레드 풀의 일꾼 수. 기본값 8\n"
              << "  --queue    작업 대기열 크기. 기본값 256\n";
}

// 숫자 인자를 읽는다. 1 이상의 정수가 아니면 false.
bool readPositiveNumber(const std::string& text, int& value) {
    try {
        size_t used = 0;
        int parsed = std::stoi(text, &used);
        if (used != text.size() || parsed < 1) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

// dbms_server: SQL 처리기(Phase 1)와 B+ 트리(Phase 2)를 엔진으로 쓰는 HTTP API 서버
int main(int argc, char* argv[]) {
    std::string dataDir = "data";
    std::string host = "0.0.0.0";
    int port = 8080;
    int threads = 8;
    int queueCapacity = 256;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (i + 1 >= argc) {
            printUsage();
            return 2;
        }
        std::string value = argv[++i];
        bool ok = true;
        if (arg == "--data") {
            dataDir = value;
        } else if (arg == "--host") {
            host = value;
        } else if (arg == "--port") {
            ok = readPositiveNumber(value, port) && port <= 65535;
        } else if (arg == "--threads") {
            ok = readPositiveNumber(value, threads);
        } else if (arg == "--queue") {
            ok = readPositiveNumber(value, queueCapacity);
        } else {
            ok = false;
        }
        if (!ok) {
            printUsage();
            return 2;
        }
    }

    Database database(dataDir);
    httplib::Server server;
    server.new_task_queue = [threads, queueCapacity] { return new ThreadPoolTaskQueue(threads, queueCapacity); };
    server.set_tcp_nodelay(true);                     // 끄면 응답마다 약 40ms 지연이 생긴다 (팀 환경 메모)
    server.set_payload_max_length(MAX_QUERY_BYTES);   // 넘는 본문은 읽지 않고 413으로 거절한다
    registerRoutes(server, database);

    runningServer = &server;
    std::signal(SIGINT, stopOnSignal);
    std::signal(SIGTERM, stopOnSignal);

    std::cout << "dbms_server: http://" << host << ":" << port << " (일꾼 " << threads << "명, 대기열 "
              << queueCapacity << "칸, 데이터 폴더 " << dataDir << ")" << std::endl;
    if (!server.listen(host, port)) {
        std::cerr << "서버를 열 수 없다: " << host << ":" << port << " (포트를 이미 쓰고 있을 수 있다)\n";
        return 1;
    }
    std::cout << "dbms_server: 종료" << std::endl;
    return 0;
}
