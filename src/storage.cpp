#include "storage.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>

std::string encodeRecord(const std::vector<std::string>& fields) {
    std::string line;
    for (size_t i = 0; i < fields.size(); i++) {
        if (i > 0) line += ',';
        for (char c : fields[i]) {
            switch (c) {
                case '\\': line += "\\\\"; break;
                case ',':  line += "\\,";  break;
                case '\n': line += "\\n";  break;
                case '\r': line += "\\r";  break;
                default:   line += c;      break;
            }
        }
    }
    return line;
}

bool decodeRecord(const std::string& line, std::vector<std::string>& fields) {
    fields.clear();
    std::string current;

    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (c == ',') {
            // 역슬래시가 안 붙은 쉼표만 칸을 나눈다.
            fields.push_back(current);
            current.clear();
            continue;
        }
        if (c != '\\') {
            current += c;
            continue;
        }
        // 역슬래시 다음 글자를 보고 원래 글자로 되돌린다.
        if (i + 1 >= line.size()) return false;
        char next = line[++i];
        switch (next) {
            case '\\': current += '\\'; break;
            case ',':  current += ',';  break;
            case 'n':  current += '\n'; break;
            case 'r':  current += '\r'; break;
            default:   return false;
        }
    }
    fields.push_back(current);
    return true;
}

bool appendRecord(std::ofstream& file, const std::string& line, std::string& error) {
    file << line << '\n';
    file.flush();
    if (!file) {
        error = "데이터 파일에 쓰지 못했다";
        return false;
    }
    return true;
}

bool readRecordAt(const std::string& path, long long offset, std::vector<std::string>& fields, std::string& error) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        error = "데이터 파일을 열 수 없다: " + path;
        return false;
    }
    file.seekg(offset);
    std::string line;
    if (!std::getline(file, line)) {
        error = "데이터 파일의 " + std::to_string(offset) + "번째 바이트에서 줄을 읽지 못했다: " + path;
        return false;
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!decodeRecord(line, fields)) {
        error = "데이터 파일의 " + std::to_string(offset) + "번째 바이트에서 시작하는 줄이 깨졌다: " + path;
        return false;
    }
    return true;
}

bool parseInteger(const std::string& text, long long& value) {
    if (text.empty()) return false;
    errno = 0;
    char* end = nullptr;
    long long parsed = std::strtoll(text.c_str(), &end, 10);
    // 끝까지 다 숫자로 읽혔고 범위를 넘지 않았을 때만 인정한다.
    if (errno == ERANGE || end != text.c_str() + text.size()) return false;
    value = parsed;
    return true;
}

bool RecordReader::open(const std::string& path, std::string& error) {
    lineNumber_ = 0;
    recordOffset_ = 0;
    nextOffset_ = 0;
    hasFile_ = false;
    if (!std::filesystem::exists(path)) return true;

    file_.open(path, std::ios::binary);
    if (!file_) {
        error = "데이터 파일을 열 수 없다: " + path;
        return false;
    }
    hasFile_ = true;
    return true;
}

ReadStatus RecordReader::readNext(std::vector<std::string>& fields) {
    if (!hasFile_) return ReadStatus::End;

    std::string line;
    while (std::getline(file_, line)) {
        lineNumber_++;
        recordOffset_ = nextOffset_;
        nextOffset_ += static_cast<long long>(line.size()) + 1;   // getline이 떼어 낸 '\n' 한 바이트까지
        // 값 안의 \r은 "\r" 두 글자로 저장되므로, 줄 끝의 진짜 \r은 윈도우 줄바꿈이다.
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        return decodeRecord(line, fields) ? ReadStatus::Ok : ReadStatus::Corrupt;
    }
    return ReadStatus::End;
}
