// clang++ -std=c++17 FixedConcurrentQueue.cpp
#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <assert.h>

namespace {
    template<uint64_t x>
    constexpr uint64_t closestExponentOf2() {
        if (x == 0 || x == std::numeric_limits<uint64_t>::max()) {
            static_assert("incorrect value");
        }
        uint64_t res = 1;
        while (res < x) res <<= 1;
        return res;
    }
}

// seems about 10-20% slower then the quick-fixed original, but it's hard to compare because of UB in originals
// https://quick-bench.com/q/s0Tm7vbCHDfAD6WrxLkF7dVjbag
template<typename T, uint64_t SIZE = 4096, uint64_t MAX_SPIN_ON_BUSY = 40000000>
class FixedConcurrentQueue {
private:
    static constexpr uint64_t mRingModMask = closestExponentOf2<SIZE>() - 1;
    static constexpr uint64_t mSize = closestExponentOf2<SIZE>();

    T mMem[mSize];
    std::mutex mMtx;
    std::atomic<uint64_t> mReadPtr = 0;
    std::atomic<uint64_t> mWritePtr = 0;

public:
    bool pop(T& dest) {
        if (mReadPtr == mWritePtr) {
            return false;
        }

        dest = std::move(mMem[mReadPtr & mRingModMask]);
        mReadPtr++; // can't increment it in previous line because then producer can overwrite the value
        return true;
    }

    template<typename U>
    void push(U&& pItem) {
        auto lock = std::unique_lock(mMtx);
        if (size() == mSize && !busyWaitForPush()) { // quick check without func call first
            throw std::runtime_error("Concurrent queue full cannot write to it!");
        }
        mMem[mWritePtr & mRingModMask] = std::forward<U>(pItem);
        mWritePtr++;
    }

    uint64_t size() const {
        return mWritePtr - mReadPtr;
    }

private:
    bool busyWaitForPush() {
        uint64_t start = 0;
        while (size() == mSize) {
            if (start++ > MAX_SPIN_ON_BUSY) {
                return false;
            }
        }
        return true;
    }
};

// tests for closestExponentOf2()
// static_assert(closestExponentOf2<0>() == 0); // should not compile
// static_assert(closestExponentOf2<std::numeric_limits<uint64_t>::max()>() == 0); // should not compile, but max() is not constexpr so ¯\_(ツ)_/¯
static_assert(closestExponentOf2<1>() == 1);
static_assert(closestExponentOf2<2>() == 2);
static_assert(closestExponentOf2<4>() == 4);
static_assert(closestExponentOf2<5>() == 8);
static_assert(closestExponentOf2<4096>() == 4096);

void reproduceWrongSize() {
    using Functor = std::function<void()>;

    FixedConcurrentQueue<Functor*, 4096> queue;

    std::atomic_bool producerDone = false;
    std::thread producer([ & ] {
        uint64_t counter = 0;
        while (counter++ < 4090) {
            queue.push(new Functor([ = ] {}));
        }
        producerDone = true;
    });

    std::thread consumer([ & ] {
        uint64_t consumeCounter = 0;
        while (!producerDone) {
            std::cout << "unsynced count=" << queue.size() << "\n";
        }
        std::cout << "synced count=" << queue.size() << std::endl;
    });
    // I'm not sure it's a honest test...
    // unsynced count=2048
    // unsynced count=2054
    // synced count=4090

    consumer.join();
    producer.join();
}

void reproduceBrokenLink() {
    // task return some number to check
    using Functor = std::function<int()>;

    FixedConcurrentQueue<Functor*, 1> queue;
    queue.push(new Functor([ = ] {return 1;}));

    // get task
    Functor* receivedTask = nullptr;
    queue.pop(receivedTask);

    queue.push(new Functor([ = ] {return 2;}));

    //execute received task
    auto res = (*receivedTask)();
    assert(res == 1);
}

using Functor = std::function<void()>;

void push_tasks(FixedConcurrentQueue<Functor*>& queue, size_t count, std::string_view tag) {
    auto counter = 0;
    while (counter < count) {
        auto newTask = new Functor([ = ] {
            std::this_thread::yield(); // some delay to give consumer a chance
            std::cout << "Running " << tag << " " << counter << std::endl << std::flush;
        });
        counter++;
        queue.push(newTask);
    }
}

int main(int, char**) {
    reproduceBrokenLink();
    // reproduceWrongSize();
    // return 0;
    FixedConcurrentQueue<Functor*> queue;

    auto tasks_per_producer = 1000000;

    std::jthread consumer([ & ] {
        Functor* task = nullptr;
        uint64_t counter = 0;
        while (counter < tasks_per_producer * 2) {
            if (queue.pop(task)) {
                (*task)();
                delete task;
                counter++;
            }
        }
    });

    std::jthread producer1(&push_tasks, std::ref(queue), tasks_per_producer, "task1");
    std::jthread producer2(&push_tasks, std::ref(queue), tasks_per_producer, "task2");
    return 0;
}
