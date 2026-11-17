#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

// 스레드 풀이 처리할 일 하나 (HTTP 연결 하나를 처리하는 함수 등)
using Job = std::function<void()>;

// 일이 줄 서는 곳. 핵심 자료구조라서 STL 그릇 없이 단일 연결 리스트로 만든다.
// 여러 스레드가 동시에 넣고 꺼내므로 모든 동작을 mutex 하나로 묶는다.
class JobQueue {
public:
    // capacity: 줄 설 수 있는 일의 최대 개수. 넘치면 넣는 쪽이 기다린다.
    explicit JobQueue(int capacity);
    ~JobQueue();

    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    // 일을 맨 뒤에 넣는다. 꽉 차 있으면 자리가 날 때까지 기다린다. 닫힌 뒤에는 넣지 않고 false.
    bool push(Job job);

    // 맨 앞의 일을 꺼낸다. 비어 있으면 일이 들어올 때까지 잠든다. 닫혔고 남은 일도 없으면 false.
    bool pop(Job& job);

    // 대기열을 닫고 기다리던 스레드를 모두 깨운다. 이미 들어 있는 일은 마저 꺼낼 수 있다.
    void close();

    int size() const;

private:
    struct Node {
        Job job;
        Node* next = nullptr;
    };

    Node* head_ = nullptr;   // 다음에 꺼낼 일
    Node* tail_ = nullptr;   // 마지막에 넣은 일
    int count_ = 0;
    int capacity_;
    bool closed_ = false;

    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;   // 일꾼을 깨운다: "일이 들어왔다"
    std::condition_variable notFull_;    // 넣는 쪽을 깨운다: "자리가 났다"
};

// 일꾼 스레드를 미리 정해진 수만큼 만들어 두고, 대기열에 들어온 일을 나눠 처리한다.
class ThreadPool {
public:
    ThreadPool(int workerCount, int queueCapacity);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 일을 맡긴다. 대기열이 꽉 차 있으면 자리가 날 때까지 기다린다. 멈춘 뒤에는 false.
    bool submit(Job job);

    // 새 일 받기를 멈추고, 이미 받은 일을 다 처리한 뒤 일꾼을 모두 끝낸다. 여러 번 불러도 된다.
    void shutdown();

    int workerCount() const { return workerCount_; }

private:
    // 일꾼 하나가 평생 도는 반복: 대기열에서 꺼내 실행하고 다시 꺼낸다.
    void runWorker();

    JobQueue queue_;
    int workerCount_;
    std::unique_ptr<std::thread[]> workers_;
    std::atomic<bool> stopped_{false};
};
