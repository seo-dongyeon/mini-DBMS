#pragma once

#include <string>
#include <vector>

enum class ColumnType { Int, Text };

struct Column {
    std::string name;
    ColumnType type = ColumnType::Int;
};

// 테이블의 설계도. 컬럼이 어떤 순서로 있고 각각 무슨 타입인지 담는다.
struct Schema {
    std::string table;
    std::vector<Column> columns;   // columns[0]은 항상 "id INT"
};

// <dataDir>/<table>.schema 파일을 읽어 Schema를 만든다. 파일이 없으면 "테이블이 없다" 에러.
bool loadSchema(const std::string& dataDir, const std::string& table, Schema& schema, std::string& error);

// 컬럼 이름으로 순서(0부터)를 찾는다. 없으면 -1.
int findColumnIndex(const Schema& schema, const std::string& name);

// 에러 메시지에 쓸 타입 이름을 돌려준다.
std::string columnTypeName(ColumnType type);
