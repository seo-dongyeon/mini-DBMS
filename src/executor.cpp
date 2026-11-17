#include "executor.h"

#include <chrono>
#include <mutex>
#include <shared_mutex>

#include "storage.h"

namespace {

// 실패 결과를 만든다.
QueryResult makeError(const std::string& message) {
    QueryResult result;
    result.ok = false;
    result.error = message;
    return result;
}

// SQL에 적힌 값의 종류(숫자/글자)가 컬럼 타입과 맞는지 확인한다.
bool valueMatchesColumn(const Value& value, const Column& column) {
    return value.isText == (column.type == ColumnType::Text);
}

// 타입이 안 맞을 때의 에러 메시지를 만든다.
std::string describeTypeMismatch(const Column& column, const Value& value) {
    std::string given = value.isText ? "글자 '" + value.text + "'" : "숫자 " + std::to_string(value.number);
    return "타입이 맞지 않는다: " + column.name + " 컬럼은 " + columnTypeName(column.type) + "인데 받은 값은 " + given;
}

// 테이블을 연다. 이번에 처음 열었으면(인덱스를 새로 만들었으면) 걸린 시간과 건수를 결과에 남긴다.
Table* openTableForQuery(Database& database, const std::string& name, QueryResult& result, std::string& error) {
    auto start = std::chrono::steady_clock::now();
    bool justOpened = false;
    Table* table = database.openTable(name, error, justOpened);
    if (table != nullptr && justOpened) {
        result.openedTable = true;
        result.openMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        result.indexedRecords = table->index.size();
    }
    return table;
}

// 읽은 레코드에서 출력할 칸만 골라 결과에 한 줄 추가한다.
void appendRow(const std::vector<std::string>& fields, const std::vector<int>& outputIndexes, QueryResult& result) {
    std::vector<std::string> row;
    for (int index : outputIndexes) row.push_back(fields[index]);
    result.rows.push_back(row);
}

// INSERT: id를 붙여 데이터 파일 끝에 한 줄 덧붙이고, B+ 트리에 (id, 줄 위치)를 넣는다.
QueryResult executeInsert(Database& database, const Statement& statement) {
    QueryResult result;
    std::string error;
    Table* table = openTableForQuery(database, statement.table, result, error);
    if (table == nullptr) return makeError(error);

    const std::vector<Column>& columns = table->schema.columns;
    size_t expectedCount = columns.size() - 1;   // id는 자동이라 뺀다
    if (statement.values.size() != expectedCount) {
        std::string message = "값의 개수가 맞지 않는다: " + statement.table + " 테이블은 id를 뺀 값 " +
                              std::to_string(expectedCount) + "개가 필요한데 " +
                              std::to_string(statement.values.size()) + "개가 왔다";
        if (statement.values.size() == columns.size()) message += " (id는 자동으로 붙으니 적지 않는다)";
        return makeError(message);
    }

    // 쓰기 열쇠: id 받기 → 파일 쓰기 → 트리 넣기 → 번호 올리기를 한 묶음으로 한다.
    // 중간에 다른 INSERT가 끼면 같은 id를 받고, SELECT가 끼면 반쯤 된 상태를 본다.
    std::unique_lock<std::shared_mutex> writeLock(table->lock);

    long long id = table->nextId;
    std::vector<std::string> fields;
    fields.push_back(std::to_string(id));
    for (size_t i = 0; i < statement.values.size(); i++) {
        const Value& value = statement.values[i];
        const Column& column = columns[i + 1];
        if (!valueMatchesColumn(value, column)) return makeError(describeTypeMismatch(column, value));
        fields.push_back(value.isText ? value.text : std::to_string(value.number));
    }

    // 순서가 중요하다: 파일에 먼저 쓰고, 성공한 뒤에만 인덱스에 넣고 번호를 올린다.
    // 반대로 하면 쓰기가 실패했을 때 인덱스가 없는 줄을 가리키고, 실패한 INSERT가 id를 먹는다.
    long long offset = 0;
    if (!appendToTable(*table, encodeRecord(fields), offset, error)) return makeError(error);
    if (!table->index.insert(id, offset)) return makeError("내부 오류: 새 id가 인덱스에 이미 있다");
    table->nextId++;

    result.insertedId = id;
    return result;
}

// WHERE id = 값: B+ 트리로 줄 위치를 찾아 그 줄 하나만 읽는다.
QueryResult selectById(const Table& table, long long id, const std::vector<int>& outputIndexes, QueryResult result) {
    result.usedIndex = true;
    long long offset = 0;
    if (!table.index.find(id, offset)) return result;   // 없는 id: 0줄

    std::vector<std::string> fields;
    std::string error;
    if (!readRecordAt(table.dataPath, offset, fields, error)) return makeError(error);

    // 인덱스가 가리킨 줄이 정말 그 id인지 한 번 더 확인한다. 파일을 밖에서 고쳤으면 어긋날 수 있다.
    long long storedId = 0;
    if (fields.size() != table.schema.columns.size() || !parseInteger(fields[0], storedId) || storedId != id) {
        return makeError("인덱스가 가리킨 줄이 id " + std::to_string(id) + "가 아니다 (" +
                         std::to_string(offset) + "번째 바이트): " + table.dataPath);
    }
    appendRow(fields, outputIndexes, result);
    return result;
}

// SELECT: WHERE id면 B+ 트리로, 아니면 데이터 파일을 처음부터 읽으며(선형 탐색) 조건에 맞는 줄을 고른다.
QueryResult executeSelect(Database& database, const Statement& statement) {
    QueryResult result;
    result.isSelect = true;
    std::string error;
    Table* table = openTableForQuery(database, statement.table, result, error);
    if (table == nullptr) return makeError(error);

    const Schema& schema = table->schema;

    // 출력할 컬럼이 파일의 몇 번째 칸인지 미리 구해 둔다.
    std::vector<int> outputIndexes;
    if (statement.selectAll) {
        for (size_t i = 0; i < schema.columns.size(); i++) {
            outputIndexes.push_back(static_cast<int>(i));
            result.columnNames.push_back(schema.columns[i].name);
        }
    } else {
        for (const std::string& name : statement.columns) {
            int index = findColumnIndex(schema, name);
            if (index == -1) return makeError("컬럼이 없다: " + name + " (" + statement.table + " 테이블)");
            outputIndexes.push_back(index);
            result.columnNames.push_back(name);
        }
    }

    int whereIndex = -1;
    if (statement.hasWhere) {
        whereIndex = findColumnIndex(schema, statement.whereColumn);
        if (whereIndex == -1) {
            return makeError("컬럼이 없다: " + statement.whereColumn + " (" + statement.table + " 테이블)");
        }
        const Column& column = schema.columns[whereIndex];
        if (!valueMatchesColumn(statement.whereValue, column)) {
            return makeError(describeTypeMismatch(column, statement.whereValue));
        }
    }

    for (size_t i = 0; i < outputIndexes.size(); i++) {
        result.columnIsInt.push_back(schema.columns[outputIndexes[i]].type == ColumnType::Int);
    }

    // 읽기 열쇠: SELECT끼리는 같이 들어가고, INSERT가 쓰는 동안에는 기다린다.
    // 파일은 SELECT마다 따로 연다(readRecordAt, RecordReader). 읽는 위치가 스레드끼리 섞이지 않게 하려는 것이다.
    std::shared_lock<std::shared_mutex> readLock(table->lock);

    // id는 스키마의 0번 컬럼으로 고정이다.
    if (statement.hasWhere && whereIndex == 0) {
        return selectById(*table, statement.whereValue.number, outputIndexes, result);
    }

    RecordReader reader;
    if (!reader.open(table->dataPath, error)) return makeError(error);

    std::vector<std::string> fields;
    while (true) {
        ReadStatus status = reader.readNext(fields);
        if (status == ReadStatus::End) break;

        std::string corruptMessage =
            "데이터 파일 " + std::to_string(reader.lineNumber()) + "번째 줄이 깨졌다: " + table->dataPath;
        if (status == ReadStatus::Corrupt || fields.size() != schema.columns.size()) {
            return makeError(corruptMessage);
        }

        if (statement.hasWhere) {
            bool matches = false;
            if (schema.columns[whereIndex].type == ColumnType::Int) {
                // 숫자는 글자가 아니라 값으로 비교한다. "007"과 7이 같아야 한다.
                long long stored = 0;
                if (!parseInteger(fields[whereIndex], stored)) return makeError(corruptMessage);
                matches = stored == statement.whereValue.number;
            } else {
                matches = fields[whereIndex] == statement.whereValue.text;
            }
            if (!matches) continue;
        }
        appendRow(fields, outputIndexes, result);
    }
    return result;
}

}  // namespace

QueryResult executeStatement(Database& database, const Statement& statement) {
    switch (statement.type) {
        case StatementType::Insert: return executeInsert(database, statement);
        case StatementType::Select: return executeSelect(database, statement);
    }
    return makeError("알 수 없는 문장 종류다");
}

std::string formatResult(const QueryResult& result) {
    if (!result.isSelect) return "INSERT OK (id=" + std::to_string(result.insertedId) + ")\n";

    std::string output;
    for (size_t i = 0; i < result.columnNames.size(); i++) {
        if (i > 0) output += " | ";
        output += result.columnNames[i];
    }
    output += '\n';
    for (const std::vector<std::string>& row : result.rows) {
        for (size_t i = 0; i < row.size(); i++) {
            if (i > 0) output += " | ";
            output += row[i];
        }
        output += '\n';
    }
    output += "(" + std::to_string(result.rows.size()) + " rows)\n";
    return output;
}
