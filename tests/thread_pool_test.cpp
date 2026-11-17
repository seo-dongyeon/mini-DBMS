#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "thread_pool.h"

using namespace std::chrono_literals;

TEST(JobQueue, FirstInFirstOut) {
    JobQueue queue(10);
    std::vector<int> order;
    for (int i = 0; i < 5; i++) ASSERT_TRUE(queue.push([&order, i] { order.push_back(i); }));
    EXPECT_EQ(queue.size(), 5);

    Job job;
    for (int i = 0; i < 5; i++) {
        ASSERT_TRUE(queue.pop(job));
        job();
    }
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));
    EXPECT_EQ(queue.size(), 0);
}

TEST(JobQueue, PushWaitsWhileFull) {
    JobQueue queue(1);
    ASSERT_TRUE(queue.push([] {}));

    std::atomic<bool> secondPushed{false};
    std::thread pusher([&] {
        queue.push([] {});   // 자리가 없어서 여기서 기다린다
        secondPushed = true;
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(secondPushed) << "꽉 찬 대기열에 넣는 쪽은 기다려야 한다";

    Job job;
    ASSERT_TRUE(queue.pop(job));   // 자리를 하나 비운다
    pusher.join();
    EXPECT_TRUE(secondPushed);
    EXPECT_EQ(queue.size(), 1);
}

TEST(JobQueue, PopWaitsUntilJobArrives) {
    JobQueue queue(4);
    std::atomic<bool> gotJob{false};
    std::thread worker([&] {
        Job job;
        if (queue.pop(job)) gotJob = true;
    });

    std::this_thread::sleep_for(50ms);
    EXPECT_FALSE(gotJob) << "빈 대기열에서 꺼내는 쪽은 잠들어야 한다";
    queue.push([] {});
    worker.join();
    EXPECT_TRUE(gotJob);
}

TEST(JobQueue, CloseWakesWaitersAndKeepsRemainingJobs) {
    JobQueue queue(4);
    queue.push([] {});
    queue.close();

    EXPECT_FALSE(queue.push([] {})) << "닫은 뒤에는 넣지 못한다";
    Job job;
    EXPECT_TRUE(queue.pop(job)) << "닫기 전에 들어온 일은 꺼낼 수 있다";
    EXPECT_FALSE(queue.pop(job)) << "닫혔고 비었으면 기다리지 않고 false";

    // 비어 있는 대기열에서 잠든 스레드도 close()로 깨어나야 한다.
    JobQueue empty(4);
    std::thread sleeper([&] {
        Job nothing;
        EXPECT_FALSE(empty.pop(nothing));
    });
    std::this_thread::sleep_for(20ms);
    empty.close();
    sleeper.join();
}

TEST(ThreadPool, RunsEveryJobOnce) {
    std::atomic<int> counter{0};
    {
        ThreadPool pool(4, 8);   // 대기열이 일 수보다 작아도 submit이 기다렸다가 넣는다
        for (int i = 0; i < 1000; i++) ASSERT_TRUE(pool.submit([&counter] { counter++; }));
        pool.shutdown();   // 남은 일을 다 처리하고 끝난다
    }
    EXPECT_EQ(counter, 1000);
}

TEST(ThreadPool, WorkersRunAtTheSameTime) {
    // 일 4개가 "넷 다 도착할 때까지" 서로 기다린다. 일꾼이 4명 동시에 돌지 않으면 영원히 안 끝난다.
    ThreadPool pool(4, 8);
    std::mutex mutex;
    std::condition_variable allArrived;
    int arrived = 0;
    std::atomic<int> finished{0};

    for (int i = 0; i < 4; i++) {
        pool.submit([&] {
            std::unique_lock<std::mutex> lock(mutex);
            arrived++;
            allArrived.notify_all();
            if (allArrived.wait_for(lock, 2s, [&] { return arrived == 4; })) finished++;
        });
    }
    pool.shutdown();
    EXPECT_EQ(finished, 4);
}

TEST(ThreadPool, SubmitAfterShutdownFails) {
    ThreadPool pool(2, 4);
    pool.shutdown();
    pool.shutdown();   // 두 번 불러도 괜찮다
    EXPECT_FALSE(pool.submit([] {}));
}

TEST(ThreadPool, ThrowingJobDoesNotKillWorker) {
    std::atomic<int> counter{0};
    ThreadPool pool(1, 4);
    pool.submit([] { throw std::runtime_error("일부러 낸 오류"); });
    pool.submit([&counter] { counter++; });
    pool.shutdown();
    EXPECT_EQ(counter, 1) << "앞의 일이 실패해도 같은 일꾼이 다음 일을 처리해야 한다";
}
