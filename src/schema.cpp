#include "schema.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace {

// 글자를 모두 대문자로 바꾼 복사본을 돌려준다.
std::string toUpper(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

// 컬럼 이름으로 쓸 수 있는 글자(영문, 숫자, _)로만 됐는지 확인한다.
bool isValidName(const std::string& name) {
    if (name.empty()) return false;
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

}  // namespace

bool loadSchema(const std::string& dataDir, const std::string& table, Schema& schema, std::string& error) {
    std::string path = dataDir + "/" + table + ".schema";
    std::ifstream file(path);
    if (!file) {
        error = "테이블이 없다: " + table + " (" + path + " 파일이 없다)";
        return false;
    }

    schema.table = table;
    schema.columns.clear();

    std::string line;
    int lineNumber = 0;
    while (std::getline(file, line)) {
        lineNumber++;
        // 한 줄은 "컬럼이름 타입". 공백으로 나눠 읽으면 윈도우 줄바꿈(\r)도 같이 걸러진다.
        std::istringstream parts(line);
        std::string name, typeText, extra;
        if (!(parts >> name)) continue;   // 빈 줄
        std::string where = path + " " + std::to_string(lineNumber) + "번째 줄";
        if (!(parts >> typeText) || (parts >> extra)) {
            error = "스키마 형식이 틀렸다 (" + where + "): \"컬럼이름 타입\" 모양이어야 한다";
            return false;
        }
        if (!isValidName(name)) {
            error = "컬럼 이름에 쓸 수 없는 글자가 있다 (" + where + "): " + name;
            return false;
        }
        if (findColumnIndex(schema, name) != -1) {
            error = "컬럼 이름이 겹친다 (" + where + "): " + name;
            return false;
        }

        Column column;
        column.name = name;
        std::string upperType = toUpper(typeText);
        if (upperType == "INT") {
            column.type = ColumnType::Int;
        } else if (upperType == "TEXT") {
            column.type = ColumnType::Text;
        } else {
            error = "모르는 타입이다 (" + where + "): " + typeText + " (INT, TEXT만 지원한다)";
            return false;
        }
        schema.columns.push_back(column);
    }

    // id는 프로그램이 자동으로 붙이므로 첫 컬럼으로 고정한다.
    if (schema.columns.empty() || schema.columns[0].name != "id" || schema.columns[0].type != ColumnType::Int) {
        error = "스키마의 첫 컬럼은 \"id INT\"여야 한다: " + path;
        return false;
    }
    if (schema.columns.size() < 2) {
        error = "스키마에 id 말고 컬럼이 하나 이상 있어야 한다: " + path;
        return false;
    }
    return true;
}

int findColumnIndex(const Schema& schema, const std::string& name) {
    for (size_t i = 0; i < schema.columns.size(); i++) {
        if (schema.columns[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

std::string columnTypeName(ColumnType type) {
    return type == ColumnType::Int ? "INT" : "TEXT";
}
