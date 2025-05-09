#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stack>
#include <string>
#include <thread>
#include <vector>

struct empty_stack : std::exception {
    const char* what() const throw() override { return "stack is empty"; }
};

template <typename T>
class ThreadSafeStack {
   private:
    std::stack<T> stack;
    mutable std::mutex m;

   public:
    ThreadSafeStack() {}

    ThreadSafeStack(const ThreadSafeStack& other) {
        std::lock_guard<std::mutex> lock(other.m);
        stack = other.stack;
    }

    ThreadSafeStack(ThreadSafeStack&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m);
        stack = std::move(other.stack);
    }

    ThreadSafeStack& operator=(const ThreadSafeStack& other) = delete;

    ThreadSafeStack& operator=(ThreadSafeStack&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m, other.m);
            stack = std::move(other.stack);
        }
        return *this;
    }

    void push(T value) {
        std::lock_guard<std::mutex> lock(m);
        stack.push(std::move(value));
    }

    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> lock(m);
        if (stack.empty()) throw empty_stack();
        std::shared_ptr<T> res(std::make_shared<T>(stack.top()));
        stack.pop();
        return res;
    }

    void pop(T& value) {
        std::lock_guard<std::mutex> lock(m);
        if (stack.empty()) throw empty_stack();
        value = stack.top();
        stack.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m);
        return stack.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m);
        return stack.size();
    }
};

int main() {
    // Create a thread-safe stack of integers
    ThreadSafeStack<int> int_stack;

    std::cout << "Testing basic push and pop operations:" << std::endl;

    // Push some values
    std::cout << "Pushing values 1, 2, 3, 4, 5" << std::endl;
    for (int i = 1; i <= 5; ++i) {
        int_stack.push(i);
    }

    // Check if stack is empty
    std::cout << "Is stack empty? " << (int_stack.empty() ? "Yes" : "No") << std::endl;

    // Pop values using the shared_ptr version
    std::cout << "Popping values using shared_ptr:" << std::endl;
    try {
        for (int i = 0; i < 3; ++i) {
            auto val = int_stack.pop();
            std::cout << "Popped: " << *val << std::endl;
        }
    } catch (const empty_stack& e) {
        std::cout << "Exception: " << e.what() << std::endl;
    }

    // Pop values using the reference version
    std::cout << "Popping values using reference parameter:" << std::endl;
    try {
        for (int i = 0; i < 3; ++i) {
            int val;
            int_stack.pop(val);
            std::cout << "Popped: " << val << std::endl;
        }
    } catch (const empty_stack& e) {
        std::cout << "Exception: " << e.what() << std::endl;
    }

    // Test exception handling
    std::cout << "\nTesting exception handling (trying to pop from empty stack):" << std::endl;
    try {
        int_stack.pop();
    } catch (const empty_stack& e) {
        std::cout << "Caught expected exception: " << e.what() << std::endl;
    }

    // Testing with multiple threads
    std::cout << "\nTesting multi-threaded operations:" << std::endl;

    ThreadSafeStack<int> shared_stack;
    std::vector<std::thread> threads;
    const int num_threads = 10;
    const int pushes_per_thread = 1000;

    // Launch producer threads
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, pushes_per_thread, &shared_stack]() {
            for (int i = 0; i < pushes_per_thread; ++i) {
                shared_stack.push(t * 10000 + i);
            }
        });
    }

    // Launch consumer threads
    std::vector<int> popped_values[num_threads];
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([t, &shared_stack, &popped_values]() {
            for (int i = 0; i < 900; ++i) {  // Slightly fewer pops than pushes
                try {
                    auto val = shared_stack.pop();
                    popped_values[t].push_back(*val);
                } catch (const empty_stack&) {
                    // Stack might be temporarily empty during concurrent operations
                    std::this_thread::yield();
                    i--;  // Try again
                }
            }
        });
    }

    // Wait for all threads to complete
    for (auto& t : threads) {
        t.join();
    }

    std::cout << "All threads completed" << std::endl;
    std::cout << "Items remaining in stack: "
              << (shared_stack.empty() ? "stack is empty" : "stack has items") << std::endl;

    // Count total popped values
    size_t total_popped = 0;
    for (int t = 0; t < num_threads; ++t) {
        total_popped += popped_values[t].size();
    }
    std::cout << "Total values pushed: " << (num_threads * pushes_per_thread) << std::endl;
    std::cout << "Total values popped: " << total_popped << std::endl;

    // Test move operations
    std::cout << "\nTesting move operations:" << std::endl;

    // Push some new values
    ThreadSafeStack<std::string> string_stack;
    string_stack.push("Hello");
    string_stack.push("World");
    string_stack.push("C++");

    // Test move constructor
    std::cout << "Testing move constructor..." << std::endl;
    ThreadSafeStack<std::string> moved_stack(std::move(string_stack));

    std::cout << "Original stack is " << (string_stack.empty() ? "empty" : "not empty")
              << std::endl;
    std::cout << "New stack is " << (moved_stack.empty() ? "empty" : "not empty") << std::endl;

    // Test move assignment
    std::cout << "Testing move assignment..." << std::endl;
    ThreadSafeStack<std::string> assigned_stack;
    assigned_stack.push("Test");

    assigned_stack = std::move(moved_stack);

    std::cout << "Moved-from stack is " << (moved_stack.empty() ? "empty" : "not empty")
              << std::endl;
    std::cout << "Assigned-to stack contains:" << std::endl;

    // Pop all values from the assigned stack
    try {
        while (true) {
            std::cout << "  " << *assigned_stack.pop() << std::endl;
        }
    } catch (const empty_stack&) {
        std::cout << "  (end of stack)" << std::endl;
    }

    return 0;
}