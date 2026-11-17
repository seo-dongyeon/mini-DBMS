#pragma once

#include <string>

// 노드 하나에 들어갈 수 있는 키 개수의 상한(배열 크기). 트리마다 이보다 작게 정할 수 있다.
constexpr int BPLUS_MAX_KEYS = 128;

// 메모리 기반 B+ 트리. 키(id) → 값(데이터 파일에서 그 줄이 시작하는 바이트 위치).
// 핵심 자료구조라서 STL 그릇 없이 고정 크기 배열과 포인터로만 만든다.
// 삭제는 과제 범위가 아니라서 없다.
class BPlusTree {
public:
    // maxKeys: 노드 하나의 키 개수 한도. 이걸 넘으면 노드를 쪼갠다. 3 ~ BPLUS_MAX_KEYS로 맞춰진다.
    explicit BPlusTree(int maxKeys = BPLUS_MAX_KEYS);
    ~BPlusTree();

    // 노드를 포인터로 들고 있어서 복사하면 같은 노드를 두 번 지우게 된다. 복사를 막는다.
    BPlusTree(const BPlusTree&) = delete;
    BPlusTree& operator=(const BPlusTree&) = delete;

    // 키와 값을 넣는다. 이미 있는 키면 넣지 않고 false.
    bool insert(long long key, long long offset);

    // 키를 찾아 값을 돌려준다. 없으면 false.
    bool find(long long key, long long& offset) const;

    long long size() const { return size_; }
    int height() const { return height_; }
    int maxKeys() const { return maxKeys_; }

    // 층별 모양을 글자로 그린다. 이정표는 [5 | 9], 잎은 [1 2 3]. 테스트와 학습용.
    std::string describe() const;

    // 정렬, 이정표 범위, 잎 깊이, 잎 연결이 모두 맞는지 검사한다. 테스트용.
    bool validate(std::string& error) const;

private:
    struct Node;
    struct SplitResult;

    bool insertInto(Node* node, long long key, long long offset, SplitResult& split);
    void splitLeaf(Node* leaf, SplitResult& split);
    void splitInternal(Node* node, SplitResult& split);
    void freeNode(Node* node);
    void appendLevel(const Node* node, int depth, int targetDepth, std::string& out) const;
    bool validateNode(const Node* node, int depth, bool hasLow, long long low, bool hasHigh, long long high,
                      int& leafDepth, std::string& error) const;

    Node* root_ = nullptr;
    int maxKeys_;
    int height_ = 0;
    long long size_ = 0;
};
