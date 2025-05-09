#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>

class ThreadSafeCounter {
   private:
    int counter;
    mutable std::mutex m;

   public:
    ThreadSafeCounter(int init = 0) : counter(init) {}
    ThreadSafeCounter(const ThreadSafeCounter& other) {
        std::lock_guard<std::mutex> lock(other.m);
        counter = other.counter;
    }
    ThreadSafeCounter(ThreadSafeCounter&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m);
        counter = other.counter;
        other.counter = 0;
    }
    ThreadSafeCounter& operator=(const ThreadSafeCounter&) = delete;
    ThreadSafeCounter& operator=(ThreadSafeCounter&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m, other.m);
            counter = other.counter;
            other.counter = 0;
        }
        return *this;
    }
    int getValue() const {
        std::lock_guard<std::mutex> lock(m);
        return counter;
    }
    void increment() {
        std::lock_guard<std::mutex> lock(m);
        counter++;
    }
    void decrement() {
        std::lock_guard<std::mutex> lock(m);
        counter--;
    }
};

int main() {
    // Create a counter initialized to 0
    ThreadSafeCounter counter(0);

    // Create a vector to hold our threads
    std::vector<std::thread> threads;

    const int numThreads = 10;
    const int iterationsPerThread = 1000;

    std::cout << "Initial counter value: " << counter.getValue() << std::endl;

    // Launch increment threads
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([&counter, iterationsPerThread]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                counter.increment();
            }
        });
    }

    // Launch decrement threads
    for (int i = 0; i < numThreads / 2; ++i) {
        threads.emplace_back([&counter, iterationsPerThread]() {
            for (int j = 0; j < iterationsPerThread; ++j) {
                counter.decrement();
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    // Expected final value: 10 threads incrementing 1000 times each = +10000
    //                       5 threads decrementing 1000 times each = -5000
    //                       Net result should be +5000
    std::cout << "Final counter value: " << counter.getValue() << std::endl;
    std::cout << "Expected value: " << (numThreads - numThreads / 2) * iterationsPerThread
              << std::endl;

    // Demonstrate move operations
    std::cout << "\nDemonstrating move operations:" << std::endl;

    // Move constructor
    ThreadSafeCounter counter2(std::move(counter));
    std::cout << "After move constructor:" << std::endl;
    std::cout << "Original counter: " << counter.getValue() << std::endl;
    std::cout << "New counter: " << counter2.getValue() << std::endl;

    // Move assignment
    ThreadSafeCounter counter3(100);
    std::cout << "Counter3 before move assignment: " << counter3.getValue() << std::endl;

    counter3 = std::move(counter2);
    std::cout << "After move assignment:" << std::endl;
    std::cout << "Counter2 (moved-from): " << counter2.getValue() << std::endl;
    std::cout << "Counter3 (moved-to): " << counter3.getValue() << std::endl;

    return 0;
}