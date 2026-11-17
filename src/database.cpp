#include "database.h"

#include <filesystem>

#include "storage.h"

namespace {

// 데이터 파일을 처음부터 읽어 B+ 트리(id → 줄 위치)를 만들고 다음 id를 정한다.
// 인덱스는 메모리에만 있어서 테이블을 열 때마다 이렇게 다시 만든다.
bool buildIndex(Table& table, std::string& error) {
    RecordReader reader;
    if (!reader.open(table.dataPath, error)) return false;

    long long maxId = 0;
    std::vector<std::string> fields;
    while (true) {
        ReadStatus status = reader.readNext(fields);
        if (status == ReadStatus::End) break;

        std::string where = "데이터 파일 " + std::to_string(reader.lineNumber()) + "번째 줄";
        long long id = 0;
        bool broken = status == ReadStatus::Corrupt || fields.size() != table.schema.columns.size() ||
                      !parseInteger(fields[0], id);
        if (broken) {
            error = where + "이 깨졌다: " + table.dataPath;
            return false;
        }
        if (!table.index.insert(id, reader.recordOffset())) {
            error = where + "의 id가 앞 줄과 겹친다 (id=" + std::to_string(id) + "): " + table.dataPath;
            return false;
        }
        if (id > maxId) maxId = id;
    }
    table.nextId = maxId + 1;
    return true;
}

// 데이터 파일 크기를 재고, 마지막 글자가 줄바꿈인지 확인한다. 다음 INSERT가 쓸 위치를 알기 위해서다.
void measureDataFile(Table& table) {
    std::error_code ignored;
    if (!std::filesystem::is_regular_file(table.dataPath, ignored)) return;

    table.dataSize = static_cast<long long>(std::filesystem::file_size(table.dataPath, ignored));
    if (table.dataSize == 0) return;

    std::ifstream file(table.dataPath, std::ios::binary);
    file.seekg(-1, std::ios::end);
    char last = '\n';
    file.get(last);
    // 손으로 고친 파일이 줄바꿈 없이 끝나 있으면, 이어 쓴 줄이 마지막 줄에 붙어 버린다.
    table.needsNewline = last != '\n';
}

}  // namespace

Table* Database::openTable(const std::string& name, std::string& error, bool& justOpened) {
    justOpened = false;
    // shortcut: 인덱스를 만드는 동안(100만 건에 약 0.3초) 다른 테이블 열기도 기다린다. 테이블이 많아지면 테이블별로 나눈다.
    std::lock_guard<std::mutex> guard(tablesMutex_);
    for (const auto& table : tables_) {
        if (table->schema.table == name) return table.get();
    }

    auto table = std::make_unique<Table>();
    if (!loadSchema(dataDir_, name, table->schema, error)) return nullptr;
    table->dataPath = dataDir_ + "/" + name + ".db";
    if (!buildIndex(*table, error)) return nullptr;
    measureDataFile(*table);

    justOpened = true;
    tables_.push_back(std::move(table));
    return tables_.back().get();
}

bool appendToTable(Table& table, const std::string& line, long long& offset, std::string& error) {
    if (!table.writer.is_open()) {
        table.writer.clear();   // 전에 열기를 실패했으면 남아 있는 실패 표시를 지운다
        table.writer.open(table.dataPath, std::ios::app | std::ios::binary);
        if (!table.writer) {
            error = "데이터 파일을 열 수 없다: " + table.dataPath;
            table.writer.close();
            return false;
        }
    }

    if (table.needsNewline) {
        table.writer << '\n';
        table.writer.flush();
        if (!table.writer) {
            error = "데이터 파일에 쓰지 못했다: " + table.dataPath;
            return false;
        }
        table.dataSize++;
        table.needsNewline = false;
    }

    // app 모드라서 항상 파일 끝에 쓴다. 쓰기 전의 파일 크기가 곧 이 줄의 시작 위치다.
    offset = table.dataSize;
    if (!appendRecord(table.writer, line, error)) {
        error += ": " + table.dataPath;
        return false;
    }
    table.dataSize += static_cast<long long>(line.size()) + 1;
    return true;
}
