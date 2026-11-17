#include "bplus_tree.h"

// 노드 하나. 잎과 이정표가 같은 구조를 쓰고 isLeaf로 구분한다.
// 배열이 한도보다 한 칸 큰 이유: 넣자마자 넘친 상태를 잠깐 담았다가 쪼개기 때문이다.
struct BPlusTree::Node {
    bool isLeaf = true;
    int count = 0;                                  // 지금 들어 있는 키 개수
    long long keys[BPLUS_MAX_KEYS + 1];             // 정렬된 키
    long long offsets[BPLUS_MAX_KEYS + 1];          // 잎: keys[i]의 파일 위치 (keys[i]와 짝)
    Node* children[BPLUS_MAX_KEYS + 2];             // 이정표: 자식. 키보다 하나 많다
    Node* next = nullptr;                           // 잎: 오른쪽 옆 잎
};

// 자식 노드가 쪼개졌을 때 부모에게 알려 주는 내용
struct BPlusTree::SplitResult {
    bool happened = false;
    long long separator = 0;   // 부모에 끼울 경계 키 ("오른쪽 노드는 이 키부터")
    Node* right = nullptr;     // 새로 생긴 오른쪽 노드
};

namespace {

// 정렬된 keys[0..count)에서 key 이상인 첫 칸을 이진 탐색으로 찾는다. 잎에서 키를 찾거나 넣을 자리.
int lowerBound(const long long* keys, int count, long long key) {
    int low = 0;
    int high = count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (keys[mid] < key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

// 정렬된 keys[0..count)에서 key보다 큰 첫 칸을 이진 탐색으로 찾는다. 이정표에서 내려갈 자식 번호.
// 키 [5 | 9]에서 5 미만은 0번, 5 이상 9 미만은 1번, 9 이상은 2번 자식이 된다.
int upperBound(const long long* keys, int count, long long key) {
    int low = 0;
    int high = count;
    while (low < high) {
        int mid = low + (high - low) / 2;
        if (keys[mid] <= key) {
            low = mid + 1;
        } else {
            high = mid;
        }
    }
    return low;
}

}  // namespace

BPlusTree::BPlusTree(int maxKeys) {
    if (maxKeys < 3) maxKeys = 3;
    if (maxKeys > BPLUS_MAX_KEYS) maxKeys = BPLUS_MAX_KEYS;
    maxKeys_ = maxKeys;
}

BPlusTree::~BPlusTree() {
    freeNode(root_);
}

// 노드와 그 아래 자식들을 전부 지운다.
void BPlusTree::freeNode(Node* node) {
    if (node == nullptr) return;
    if (!node->isLeaf) {
        for (int i = 0; i <= node->count; i++) freeNode(node->children[i]);
    }
    delete node;
}

bool BPlusTree::find(long long key, long long& offset) const {
    const Node* node = root_;
    if (node == nullptr) return false;

    // 층마다 노드 안에서 이진 탐색으로 자식 하나만 골라 내려간다.
    while (!node->isLeaf) {
        node = node->children[upperBound(node->keys, node->count, key)];
    }
    int index = lowerBound(node->keys, node->count, key);
    if (index < node->count && node->keys[index] == key) {
        offset = node->offsets[index];
        return true;
    }
    return false;
}

bool BPlusTree::insert(long long key, long long offset) {
    if (root_ == nullptr) {
        root_ = new Node();
        height_ = 1;
    }

    SplitResult split;
    if (!insertInto(root_, key, offset, split)) return false;
    size_++;

    // 뿌리가 쪼개졌으면 그 위에 새 뿌리를 만든다. 트리는 이렇게 위로만 자란다.
    if (split.happened) {
        Node* newRoot = new Node();
        newRoot->isLeaf = false;
        newRoot->count = 1;
        newRoot->keys[0] = split.separator;
        newRoot->children[0] = root_;
        newRoot->children[1] = split.right;
        root_ = newRoot;
        height_++;
    }
    return true;
}

// node 아래에 키를 넣는다. node가 넘쳐서 쪼개졌으면 split에 결과를 남긴다.
bool BPlusTree::insertInto(Node* node, long long key, long long offset, SplitResult& split) {
    if (node->isLeaf) {
        int position = lowerBound(node->keys, node->count, key);
        if (position < node->count && node->keys[position] == key) return false;

        // 자리를 비우려고 뒤쪽을 한 칸씩 민다. 키와 위치는 짝이라 같이 민다.
        for (int i = node->count; i > position; i--) {
            node->keys[i] = node->keys[i - 1];
            node->offsets[i] = node->offsets[i - 1];
        }
        node->keys[position] = key;
        node->offsets[position] = offset;
        node->count++;

        if (node->count > maxKeys_) splitLeaf(node, split);
        return true;
    }

    int childIndex = upperBound(node->keys, node->count, key);
    SplitResult childSplit;
    if (!insertInto(node->children[childIndex], key, offset, childSplit)) return false;
    if (!childSplit.happened) return true;

    // 자식이 쪼개졌다. childIndex 자리에 경계 키를, 그 오른쪽에 새 자식을 끼운다.
    for (int i = node->count; i > childIndex; i--) {
        node->keys[i] = node->keys[i - 1];
        node->children[i + 1] = node->children[i];
    }
    node->keys[childIndex] = childSplit.separator;
    node->children[childIndex + 1] = childSplit.right;
    node->count++;

    if (node->count > maxKeys_) splitInternal(node, split);
    return true;
}

// 넘친 잎을 반으로 나눈다. 오른쪽 첫 키를 부모로 "복사"한다. 잎에는 모든 키가 남아야 하기 때문이다.
// shortcut: 항상 반으로 나눠서, id처럼 커지기만 하는 키를 넣으면 왼쪽 잎들이 반만 찬 채로 남는다.
//           메모리가 부족하면 맨 오른쪽 잎은 한쪽으로 몰아 쪼개는 방식으로 바꾼다.
void BPlusTree::splitLeaf(Node* leaf, SplitResult& split) {
    Node* right = new Node();
    int leftCount = leaf->count / 2;
    right->count = leaf->count - leftCount;
    for (int i = 0; i < right->count; i++) {
        right->keys[i] = leaf->keys[leftCount + i];
        right->offsets[i] = leaf->offsets[leftCount + i];
    }
    leaf->count = leftCount;

    // 잎 연결 사이에 새 잎을 끼운다: leaf → right → (원래 leaf의 오른쪽)
    right->next = leaf->next;
    leaf->next = right;

    split.happened = true;
    split.separator = right->keys[0];
    split.right = right;
}

// 넘친 이정표를 나눈다. 가운데 키는 부모로 "이동"하고 양쪽에는 남지 않는다. 길 안내용이라 중복이 필요 없다.
void BPlusTree::splitInternal(Node* node, SplitResult& split) {
    int middle = node->count / 2;
    Node* right = new Node();
    right->isLeaf = false;
    right->count = node->count - middle - 1;
    for (int i = 0; i < right->count; i++) {
        right->keys[i] = node->keys[middle + 1 + i];
    }
    for (int i = 0; i <= right->count; i++) {
        right->children[i] = node->children[middle + 1 + i];
    }

    split.happened = true;
    split.separator = node->keys[middle];
    split.right = right;
    node->count = middle;
}

std::string BPlusTree::describe() const {
    if (root_ == nullptr) return "(빈 트리)";
    std::string out;
    for (int depth = 0; depth < height_; depth++) {
        if (depth > 0) out += '\n';
        appendLevel(root_, 0, depth, out);
    }
    return out;
}

// targetDepth 층에 있는 노드들을 왼쪽부터 out에 이어 붙인다.
void BPlusTree::appendLevel(const Node* node, int depth, int targetDepth, std::string& out) const {
    if (depth == targetDepth) {
        if (!out.empty() && out.back() != '\n') out += ' ';
        out += '[';
        for (int i = 0; i < node->count; i++) {
            if (i > 0) out += node->isLeaf ? " " : " | ";
            out += std::to_string(node->keys[i]);
        }
        out += ']';
        return;
    }
    for (int i = 0; i <= node->count; i++) appendLevel(node->children[i], depth + 1, targetDepth, out);
}

bool BPlusTree::validate(std::string& error) const {
    if (root_ == nullptr) {
        if (size_ != 0) {
            error = "뿌리가 없는데 크기가 0이 아니다";
            return false;
        }
        return true;
    }

    int leafDepth = -1;
    if (!validateNode(root_, 0, false, 0, false, 0, leafDepth, error)) return false;
    if (leafDepth + 1 != height_) {
        error = "잎의 깊이와 높이가 다르다";
        return false;
    }

    // 맨 왼쪽 잎부터 next를 따라가면 모든 키가 정렬된 순서로 한 번씩 나와야 한다.
    const Node* leaf = root_;
    while (!leaf->isLeaf) leaf = leaf->children[0];
    long long seen = 0;
    bool hasPrevious = false;
    long long previous = 0;
    for (; leaf != nullptr; leaf = leaf->next) {
        for (int i = 0; i < leaf->count; i++) {
            if (hasPrevious && leaf->keys[i] <= previous) {
                error = "잎 연결을 따라가면 키가 정렬돼 있지 않다";
                return false;
            }
            previous = leaf->keys[i];
            hasPrevious = true;
            seen++;
        }
    }
    if (seen != size_) {
        error = "잎 연결로 센 키 개수가 크기와 다르다";
        return false;
    }
    return true;
}

// node의 키가 [low, high) 범위 안에서 정렬돼 있는지, 잎이 모두 같은 깊이인지 아래로 내려가며 검사한다.
bool BPlusTree::validateNode(const Node* node, int depth, bool hasLow, long long low, bool hasHigh, long long high,
                             int& leafDepth, std::string& error) const {
    if (node->count < 1 || node->count > maxKeys_) {
        error = "노드의 키 개수가 범위를 벗어났다: " + std::to_string(node->count);
        return false;
    }
    for (int i = 0; i < node->count; i++) {
        long long key = node->keys[i];
        if (i > 0 && node->keys[i - 1] >= key) {
            error = "노드 안의 키가 정렬돼 있지 않다";
            return false;
        }
        if ((hasLow && key < low) || (hasHigh && key >= high)) {
            error = "키 " + std::to_string(key) + "가 이정표가 정한 범위 밖에 있다";
            return false;
        }
    }

    if (node->isLeaf) {
        if (leafDepth == -1) leafDepth = depth;
        if (leafDepth != depth) {
            error = "잎의 깊이가 서로 다르다";
            return false;
        }
        return true;
    }

    // i번 자식의 키는 keys[i-1] 이상, keys[i] 미만이어야 한다.
    for (int i = 0; i <= node->count; i++) {
        bool childHasLow = i == 0 ? hasLow : true;
        long long childLow = i == 0 ? low : node->keys[i - 1];
        bool childHasHigh = i == node->count ? hasHigh : true;
        long long childHigh = i == node->count ? high : node->keys[i];
        if (!validateNode(node->children[i], depth + 1, childHasLow, childLow, childHasHigh, childHigh, leafDepth,
                          error)) {
            return false;
        }
    }
    return true;
}
