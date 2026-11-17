#pragma once

#include <fstream>
#include <string>
#include <vector>

// 저장 형식: 레코드 하나가 한 줄. 칸은 쉼표로 나눈다.
// 값 안의 특수 글자는 역슬래시를 붙여 적는다: \\  \,  \n  \r
// 그래서 파일 안의 진짜 줄바꿈은 항상 레코드의 끝이다.

// 칸들을 저장 형식의 한 줄(줄바꿈 제외)로 만든다.
std::string encodeRecord(const std::vector<std::string>& fields);

// 저장된 한 줄을 칸들로 되돌린다. 형식이 깨졌으면 false.
bool decodeRecord(const std::string& line, std::vector<std::string>& fields);

// 열려 있는 파일 끝에 레코드 한 줄과 줄바꿈을 쓰고 바로 내보낸다(flush).
bool appendRecord(std::ofstream& file, const std::string& line, std::string& error);

// 파일의 offset번째 바이트에서 시작하는 레코드 한 줄을 읽어 칸들로 되돌린다. B+ 트리로 찾은 줄을 읽을 때 쓴다.
bool readRecordAt(const std::string& path, long long offset, std::vector<std::string>& fields, std::string& error);

// 글자가 정수 하나로만 이루어졌는지 확인하고 값으로 바꾼다.
bool parseInteger(const std::string& text, long long& value);

enum class ReadStatus { Ok, End, Corrupt };

// 데이터 파일을 처음부터 한 레코드씩 읽는다.
class RecordReader {
public:
    // 파일을 연다. 파일이 아직 없으면 빈 테이블로 본다(에러 아님).
    bool open(const std::string& path, std::string& error);

    // 다음 레코드를 읽는다. 끝이면 End, 형식이 깨졌으면 Corrupt.
    ReadStatus readNext(std::vector<std::string>& fields);

    // 방금 읽은 레코드가 파일의 몇 번째 줄인지 (1부터)
    long long lineNumber() const { return lineNumber_; }

    // 방금 읽은 레코드가 파일의 몇 번째 바이트에서 시작하는지 (0부터). B+ 트리에 넣는 값이다.
    long long recordOffset() const { return recordOffset_; }

private:
    std::ifstream file_;
    bool hasFile_ = false;
    long long lineNumber_ = 0;
    long long recordOffset_ = 0;
    long long nextOffset_ = 0;   // 다음 줄이 시작하는 바이트
};
