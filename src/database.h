#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "bplus_tree.h"
#include "schema.h"

// 열어 둔 테이블 하나. 프로그램이 도는 동안 메모리에 유지된다.
// 서버에서는 여러 스레드가 같은 Table을 쓴다. 아래 schema 밖의 칸은 모두 lock을 잡고 만진다.
struct Table {
    // 읽기-쓰기 락. SELECT는 읽기 열쇠(여럿이 같이), INSERT는 쓰기 열쇠(혼자)를 잡는다.
    std::shared_mutex lock;

    Schema schema;
    std::string dataPath;       // <dataDir>/<table>.db
    long long nextId = 1;       // 다음 INSERT에 붙일 id
    BPlusTree index;            // id → 데이터 파일에서 그 줄이 시작하는 바이트 위치
    long long dataSize = 0;     // 데이터 파일 크기. 다음 레코드가 시작할 위치다
    bool needsNewline = false;  // 파일이 줄바꿈 없이 끝나 있으면, 다음 쓰기 전에 줄바꿈을 먼저 넣는다
    std::ofstream writer;       // INSERT용. 처음 쓸 때 열고 계속 열어 둔다
};

// 데이터 폴더 하나를 맡아 테이블을 열어 주는 창구
class Database {
public:
    explicit Database(const std::string& dataDir) : dataDir_(dataDir) {}

    // 테이블을 연다. 처음 열 때는 데이터 파일을 읽어 B+ 트리를 만들고 justOpened를 true로 한다.
    // 이미 열었으면 그걸 돌려준다. 스키마가 없거나 데이터 파일이 깨졌으면 nullptr과 error.
    Table* openTable(const std::string& name, std::string& error, bool& justOpened);

private:
    std::string dataDir_;
    // 테이블 수가 적어서 목록을 처음부터 훑어 찾는다.
    std::vector<std::unique_ptr<Table>> tables_;
    // 두 스레드가 같은 테이블을 동시에 처음 열면 두 번 만들어질 수 있다. 목록을 보고 고치는 동안 잠근다.
    std::mutex tablesMutex_;
};

// 테이블 데이터 파일 끝에 레코드 한 줄을 쓰고, 그 줄이 시작한 바이트 위치를 offset에 돌려준다.
bool appendToTable(Table& table, const std::string& line, long long& offset, std::string& error);
