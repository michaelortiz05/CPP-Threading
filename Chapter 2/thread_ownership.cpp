#include <iostream>
#include <thread>
#include <vector>
/**
 * Demonstrates basic concepts related to thread ownership
 * from chapter 2. Implements a basic task queue to hold threads
 */
class task_queue {
   private:
    std::vector<std::thread> threads;
    void join_all() {
        for (auto& t : threads) {
            if (t.joinable()) t.join();
        }
    }

   public:
    task_queue() {}
    ~task_queue() { join_all(); }
    task_queue(const task_queue&) = delete;
    task_queue& operator=(const task_queue&) = delete;
    task_queue(task_queue&& other) noexcept : threads(std::move(other.threads)) {}
    task_queue& operator=(task_queue&& other) noexcept {
        if (this != &other) {
            join_all();  // Join existing threads before moving
            threads = std::move(other.threads);
        }
        return *this;
    }
    void add_thread(std::thread t) { threads.emplace_back(std::move(t)); }
    std::thread pop_thread() {
        if (threads.empty()) {
            throw std::runtime_error("No threads in queue");
        }

        std::thread t = std::move(threads.front());
        threads.erase(threads.begin());
        return t;
    }
};

int main() {
    task_queue q1;
    for (int i = 5; i < 8; i++) {
        q1.add_thread(std::thread([=] {
            for (int num = 0; num < 100; num++) {
                if (num % i == 0) {
                    std::cout << std::this_thread::get_id() << ": " << num << "/" << i << std::endl;
                }
            }
        }));
    }
    task_queue q2 = std::move(q1);
    std::thread t = q2.pop_thread();
    t.join();
    return 0;
}