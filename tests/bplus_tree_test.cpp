#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <vector>

#include "bplus_tree.h"

// 키들을 순서대로 넣는다. 값(offset)은 키 × 10으로 해서 짝이 맞는지 확인할 수 있게 한다.
static void insertAll(BPlusTree& tree, const std::vector<long long>& keys) {
    for (long long key : keys) ASSERT_TRUE(tree.insert(key, key * 10)) << key;
}

// 트리 규칙이 다 맞는지 확인한다.
static void expectValid(const BPlusTree& tree) {
    std::string error;
    EXPECT_TRUE(tree.validate(error)) << error << "\n" << tree.describe();
}

TEST(BPlusTree, EmptyTree) {
    BPlusTree tree;
    long long offset = -1;
    EXPECT_FALSE(tree.find(1, offset));
    EXPECT_EQ(tree.size(), 0);
    EXPECT_EQ(tree.height(), 0);
    EXPECT_EQ(tree.describe(), "(빈 트리)");
    expectValid(tree);
}

TEST(BPlusTree, FindReturnsPairedOffset) {
    BPlusTree tree(3);
    insertAll(tree, {1, 5, 6, 3, 9, 14, 2});
    for (long long key : {1, 2, 3, 5, 6, 9, 14}) {
        long long offset = -1;
        ASSERT_TRUE(tree.find(key, offset)) << key;
        EXPECT_EQ(offset, key * 10);
    }
    long long offset = -1;
    EXPECT_FALSE(tree.find(4, offset));
    EXPECT_FALSE(tree.find(0, offset));
    EXPECT_FALSE(tree.find(100, offset));
}

// 학습 자료(07-b-plus-tree.md)의 그림과 같은 모양이 나오는지 확인한다.
TEST(BPlusTree, MatchesStudyExampleWithThreeKeys) {
    BPlusTree tree(3);
    insertAll(tree, {1, 5, 6});
    EXPECT_EQ(tree.describe(), "[1 5 6]");

    insertAll(tree, {3});   // [1 3 5 6] 넘침 → [1 3] [5 6], 5 복사
    EXPECT_EQ(tree.describe(), "[5]\n[1 3] [5 6]");

    insertAll(tree, {9, 14});   // [5 6 9 14] 넘침 → [5 6] [9 14], 9 복사
    EXPECT_EQ(tree.describe(), "[5 | 9]\n[1 3] [5 6] [9 14]");

    insertAll(tree, {2});
    EXPECT_EQ(tree.describe(), "[5 | 9]\n[1 2 3] [5 6] [9 14]");
    EXPECT_EQ(tree.height(), 2);
    expectValid(tree);
}

TEST(BPlusTree, MatchesStudyExampleWithFourKeys) {
    BPlusTree tree(4);
    insertAll(tree, {1, 5, 6, 3, 9, 14, 2});
    EXPECT_EQ(tree.describe(), "[5]\n[1 2 3] [5 6 9 14]");
    expectValid(tree);
}

TEST(BPlusTree, InternalNodeSplitMovesMiddleKeyUp) {
    BPlusTree tree(3);
    insertAll(tree, {1, 2, 3, 4, 5, 6, 7, 8, 9});
    EXPECT_EQ(tree.describe(), "[3 | 5 | 7]\n[1 2] [3 4] [5 6] [7 8 9]");

    insertAll(tree, {10});   // 뿌리 [3 | 5 | 7 | 9] 넘침 → 7이 위로 이동
    EXPECT_EQ(tree.describe(), "[7]\n[3 | 5] [9]\n[1 2] [3 4] [5 6] [7 8] [9 10]");
    EXPECT_EQ(tree.height(), 3);
    expectValid(tree);
}

TEST(BPlusTree, RejectsDuplicateKey) {
    BPlusTree tree(3);
    insertAll(tree, {1, 2, 3, 4});
    EXPECT_FALSE(tree.insert(3, 999));
    EXPECT_EQ(tree.size(), 4);
    long long offset = -1;
    ASSERT_TRUE(tree.find(3, offset));
    EXPECT_EQ(offset, 30);   // 처음 넣은 값이 그대로 남아 있다
}

TEST(BPlusTree, NegativeAndHugeKeys) {
    BPlusTree tree(3);
    std::vector<long long> keys = {-100, 0, -1, 9000000000000LL, 42, -9000000000000LL};
    insertAll(tree, keys);
    for (long long key : keys) {
        long long offset = 0;
        EXPECT_TRUE(tree.find(key, offset)) << key;
        EXPECT_EQ(offset, key * 10);
    }
    expectValid(tree);
}

TEST(BPlusTree, MaxKeysIsClamped) {
    EXPECT_EQ(BPlusTree(1).maxKeys(), 3);
    EXPECT_EQ(BPlusTree(100000).maxKeys(), BPLUS_MAX_KEYS);
}

// 순서대로, 거꾸로, 무작위로 많이 넣어도 규칙이 깨지지 않고 전부 찾아지는지 본다.
static void checkManyKeys(int maxKeys, std::vector<long long> keys) {
    BPlusTree tree(maxKeys);
    insertAll(tree, keys);
    EXPECT_EQ(tree.size(), static_cast<long long>(keys.size()));
    expectValid(tree);
    for (long long key : keys) {
        long long offset = -1;
        ASSERT_TRUE(tree.find(key, offset)) << key;
        ASSERT_EQ(offset, key * 10);
    }
    long long offset = -1;
    EXPECT_FALSE(tree.find(-1, offset));
    EXPECT_FALSE(tree.find(static_cast<long long>(keys.size()) * 3 + 1, offset));
}

TEST(BPlusTree, ManyKeysInEveryOrder) {
    std::vector<long long> ascending;
    for (long long i = 1; i <= 20000; i++) ascending.push_back(i * 3);   // 3, 6, 9... 사이에 빈 키를 둔다
    std::vector<long long> descending(ascending.rbegin(), ascending.rend());
    std::vector<long long> shuffled = ascending;
    std::mt19937 random(12345);
    std::shuffle(shuffled.begin(), shuffled.end(), random);

    for (int maxKeys : {3, 4, 5, 16, BPLUS_MAX_KEYS}) {
        SCOPED_TRACE("maxKeys=" + std::to_string(maxKeys));
        checkManyKeys(maxKeys, ascending);
        checkManyKeys(maxKeys, descending);
        checkManyKeys(maxKeys, shuffled);
    }
}

TEST(BPlusTree, HeightGrowsSlowly) {
    BPlusTree tree;   // 노드 하나에 키 128개
    for (long long i = 1; i <= 100000; i++) tree.insert(i, i);
    // 순서대로 넣으면 잎이 반씩 차서 한 노드에 약 64개. 64^3 = 26만이라 10만 건은 3층이면 된다.
    EXPECT_LE(tree.height(), 3);
    expectValid(tree);
}
