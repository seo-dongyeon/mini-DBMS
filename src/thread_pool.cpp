#include "thread_pool.h"

#include <iostream>

JobQueue::JobQueue(int capacity) : capacity_(capacity < 1 ? 1 : capacity) {}

JobQueue::~JobQueue() {
    while (head_ != nullptr) {
        Node* next = head_->next;
        delete head_;
        head_ = next;
    }
}

bool JobQueue::push(Job job) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 꽉 찼으면 잠든다. pop()이 자리를 비우거나 close()가 불리면 깨어난다.
    notFull_.wait(lock, [this] { return count_ < capacity_ || closed_; });
    if (closed_) return false;

    Node* node = new Node();
    node->job = std::move(job);
    if (tail_ == nullptr) {
        head_ = node;
    } else {
        tail_->next = node;
    }
    tail_ = node;
    count_++;

    lock.unlock();
    notEmpty_.notify_one();   // 자고 있는 일꾼 하나를 깨운다
    return true;
}

bool JobQueue::pop(Job& job) {
    std::unique_lock<std::mutex> lock(mutex_);
    // 비었으면 잠든다. 조건을 다시 확인하는 이유: 깨어났는데 다른 일꾼이 먼저 가져갔을 수 있다.
    notEmpty_.wait(lock, [this] { return head_ != nullptr || closed_; });
    if (head_ == nullptr) return false;   // 닫혔고 남은 일도 없다

    Node* node = head_;
    head_ = node->next;
    if (head_ == nullptr) tail_ = nullptr;
    count_--;
    job = std::move(node->job);
    delete node;

    lock.unlock();
    notFull_.notify_one();   // 자리가 났다고 넣는 쪽에 알린다
    return true;
}

void JobQueue::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
    }
    notEmpty_.notify_all();
    notFull_.notify_all();
}

int JobQueue::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
}

ThreadPool::ThreadPool(int workerCount, int queueCapacity)
    : queue_(queueCapacity), workerCount_(workerCount < 1 ? 1 : workerCount) {
    workers_ = std::make_unique<std::thread[]>(workerCount_);
    for (int i = 0; i < workerCount_; i++) {
        workers_[i] = std::thread([this] { runWorker(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::submit(Job job) {
    return queue_.push(std::move(job));
}

void ThreadPool::shutdown() {
    if (stopped_.exchange(true)) return;   // 이미 멈췄다
    queue_.close();
    for (int i = 0; i < workerCount_; i++) workers_[i].join();
}

void ThreadPool::runWorker() {
    Job job;
    while (queue_.pop(job)) {
        // 일 하나가 실패해도 일꾼이 죽지 않게 막는다. 일꾼이 죽으면 서버 전체가 꺼진다.
        try {
            job();
        } catch (const std::exception& error) {
            std::cerr << "[thread pool] 일 처리 중 오류: " << error.what() << "\n";
        } catch (...) {
            std::cerr << "[thread pool] 일 처리 중 알 수 없는 오류\n";
        }
        job = nullptr;   // 다 쓴 일이 붙잡고 있는 자원(소켓 등)을 바로 놓는다
    }
}
