#pragma once

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// 테스트 하나가 쓸 빈 임시 폴더를 만들고 경로를 돌려준다.
inline std::string makeTempDataDir() {
    char pattern[] = "/tmp/dbms_test_XXXXXX";
    char* path = mkdtemp(pattern);
    return path != nullptr ? std::string(path) : std::string();
}

// 파일에 내용을 그대로 쓴다.
inline void writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary);
    file << content;
}

// 파일 내용을 그대로 읽는다.
inline std::string readTextFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}
