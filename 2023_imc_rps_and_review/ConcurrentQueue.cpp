#include <cmath>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <assert.h>

template <typename T, uint64_t SIZE = 4096, uint64_t MAX_SPIN_ON_BUSY = 40000000>
class ConcurrentQueue
{
public: // made it public to implement test-cases
    static constexpr unsigned Log2(unsigned n, unsigned p = 0)
    {
        return (n <= 1) ? p : Log2(n / 2, p + 1);
    }

    // overflow with SIZE = 0
    static constexpr uint64_t closestExponentOf2(uint64_t x)
    {
        return (1UL << ((uint64_t)(Log2(SIZE - 1)) + 1)); // using SIZE instead of x, wrong value if SIZE == 1
    }
    static constexpr uint64_t mRingModMask = closestExponentOf2(SIZE) - 1;
    // can be twice bigger than expected (with SIZE=4097 for example)
    // using % to get the right pointer is about 10% slower (mem vs speed tradeoff)
    static constexpr uint64_t mSize = closestExponentOf2(SIZE);

    static const T mEmpty;

    T mMem[mSize];
    std::mutex mLock;
    uint64_t mReadPtr = 0;
    uint64_t mWritePtr = 0;

public:
    const T &pop()
    { // returning const ref for mutable data doesn't look safe
        // if:
        // 1) only 1 consumer
        // 2) peek is public (means it's supposed to be used from external code)
        // then no need for double-checking inside the pop func
        // it can be threated as 'lightweight check without sync' with a hope that non-atomic counters might get sync eventually without explicit sync mechanism (such as mutex),
        // but it's UB
        if (!peek())
        {
            return mEmpty;
        }

        std::lock_guard<std::mutex> lock(mLock); // auto lock = std::lock_guard(mLock);

        if (!peek())
        {
            return mEmpty;
        }

        T &ret = mMem[mReadPtr & mRingModMask];

        mReadPtr++;
        return ret;
    }

    bool peek() const
    { // rename to hasData (if it's not DSL standard)
        return (mWritePtr != mReadPtr);
    }

    uint64_t getCount() const
    {
        // reading access by producer without mutex
        // (consumer can change the value and break busyWaitForPush loop)

        // result doesn't look right:
        // consider the case after first uint64-loop:
        // mWrite = 5, mRead = 2^64 - 1. getCount = 2^64 - 1 - 5?? should be about 6
        return mWritePtr > mReadPtr ? mWritePtr - mReadPtr : mReadPtr - mWritePtr;
    }

    bool busyWaitForPush()
    {
        uint64_t start = 0;
        while (getCount() == mSize)
        {
            if (start++ > MAX_SPIN_ON_BUSY)
            {
                return false;
            }
        }
        return true;
    }

    void push(const T &pItem)
    {
        if (!busyWaitForPush())
        {
            throw std::runtime_error("Concurrent queue full cannot write to it!");
        }

        std::lock_guard<std::mutex> lock(mLock);
        mMem[mWritePtr & mRingModMask] = pItem;
        mWritePtr++;
    }

    void push(T &&pItem)
    {
        // no double-checking - if many producer threads are spinning, all of them could pass the barrier
        // but there is a space for 1 object only (unconsumed data will be overwritten)
        if (!busyWaitForPush())
        {
            throw std::runtime_error("Concurrent queue full cannot write to it!");
        }

        std::lock_guard<std::mutex> lock(mLock);
        mMem[mWritePtr & mRingModMask] = std::move(pItem);
        mWritePtr++;
    }
};

template <typename T, uint64_t SIZE, uint64_t MAX_SPIN_ON_BUSY>
const T ConcurrentQueue<T, SIZE, MAX_SPIN_ON_BUSY>::mEmpty = T{}; // makes some limitations to storable objects

void reproduceMultiPushProblem()
{
    // that's the idea. Doesn't work.
    using Functor = std::function<int()>;
    ConcurrentQueue<Functor *, 1, 120000000> queue;

    // fill the queue
    queue.push(new Functor([=]
                           { return 1; }));
    queue.push(new Functor([=]
                           { return 2; }));

    // start 2 producer-threads, they'll spin on push
    std::thread producer1([&]
                          { queue.push(new Functor([=]
                                                   { return 3; })); });
    std::thread producer2([&]
                          { queue.push(new Functor([=]
                                                   { return 4; })); });

    auto task = queue.pop(); // receive first task from queue (it returns 1)
    int val = (*task)();
    std::cout << "val: " << val << std::endl;
    assert((*task)() == 1);

    // wait until queues done
    producer1.join();
    producer2.join();
    task = queue.pop(); // receive second task from queue (it suddenly returns 4, because producer overwrote 2)
    val = (*task)();
    std::cout << "val: " << val << std::endl;
    assert(val == 4);
}

void reproduceWrongSize()
{
    using Functor = std::function<void()>;

    ConcurrentQueue<Functor *, 4096> queue;

    std::atomic_bool producerDone = false;
    std::thread producer([&]
                         {
        uint64_t counter = 0;
        while (counter++ < 4090) {
            queue.push(new Functor([ = ] {}));
        }
        producerDone = true; });

    std::thread consumer([&]
                         {
        uint64_t consumeCounter = 0;
        while (!producerDone) {
            std::cout << "unsynced count=" << queue.getCount() << "\n";
        }
        std::cout << "synced count=" << queue.getCount() << std::endl; });
    // I'm not sure it's a honest test...
    // unsynced count=2048
    // unsynced count=2054
    // synced count=4090

    consumer.join();
    producer.join();
}

void reproduceBrokenLink()
{
    // task return some number to check
    using Functor = std::function<int()>;

    ConcurrentQueue<Functor *, 1> queue;
    queue.push(new Functor([=]
                           { return 1; }));

    // get task
    const auto &receivedTask = queue.pop();

    // override task (size = 1 => mSize = 2, so push 2 tasks)
    queue.push(new Functor([=]
                           { return 2; }));
    queue.push(new Functor([=]
                           { return 3; }));

    // execute received task
    auto res = (*receivedTask)();
    assert(res == 3); // expect 1, but got 3
}

// tests for closestExponentOf2()
// static_assert(closestExponentOf2(0) == 0) // is not compiled (as expected?)
// static_assert(ConcurrentQueue<std::function<void()>>::closestExponentOf2(1) == 1);
// static_assert(ConcurrentQueue<std::function<void()>>::closestExponentOf2(2) == 2);
// static_assert(ConcurrentQueue<std::function<void()>>::closestExponentOf2(4) == 4);
// static_assert(ConcurrentQueue<std::function<void()>>::closestExponentOf2(5) == 8);
static_assert(ConcurrentQueue<std::function<void()>>::closestExponentOf2(4096) == 4096);

int main(int, char **)
{
    // reproduceMultiPushProblem();
    // reproduceWrongSize();
    reproduceBrokenLink();
    return 0;
    using Functor = std::function<void()>;

    ConcurrentQueue<Functor *> queue;
    std::thread consumer([&]
                         {
        while (true) {
            if (queue.peek()) {
                auto task = queue.pop();
                (*task)();
                delete task;
            }
        } });

    std::thread producer([&]
                         {
        uint64_t counter = 0;
        while (true) {
            auto taskId = counter++;
            auto newTask = new Functor([ = ] {
                std::cout << "Running task " << taskId << std::endl << std::flush;
            });
            queue.push(newTask);
        } });

    consumer.join();
    producer.join();
    return 0;
}

// So generally I see 4 problems here:
// 1) returning a ref to mutable variable (const T& pop())
// 2) non-synchronized access to non-atomic variables
// 3) wrong getCount() calculation
// 4) mistake in closestExponentOf2() func
// 5) default constuctor for std::function
// According to documentation, "The assumption is that the consumer can handle events faster than that the producers can produce them, otherwise behaviour is undefined."
// technically, if we enter busyWaitForPush() - that means consumer is already too slow to handle all tasks, but I expect it's a feature and we can't just remove this check

// Answers for the questions:

// What changes would you suggest in a code review? Would you check in your code like this? Do you think the code is clean?
// Everything is explained in the comments to the code

// Do you see room for improving the performance without breaking thread safety?
// There are few UB places, so it's hard to test the performance...

// What steps would you take to find the bug?
// Read the code attentively -> reproduce (run the example) -> detect suspicious places -> write a tests to confirm there is a bug

// If you can find the bug, what fix would you suggest? Write a proper test case to prove that the bug is solved (may be in pseudo code), preferably in a deterministic matter?
// there is a couple of tests for reproducing (reproduceBrokenLink(), reproduceWrongSize(), reproduceMultiPushProblem(), static_asserts for closestExponentOf2()). Only reproduceBrokenLink() reproduce the problem determinately.

// If you cannot find the bug, how would you go about testing the thread safety of the class
// For thread-safe problem, the best option I know is sanitizers (-fsanitize=thread) and load testings - run many threads, compare initial and final state (all produced tasks were received).
// Aditionally, some metrics and side-effect can be done to check internal state/behaviour.
// It's hard to write proper load tests here without updating the code (we usually get a segfault very soon)