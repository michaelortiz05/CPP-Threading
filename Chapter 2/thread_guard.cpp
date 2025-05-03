#include <iostream>
#include <thread>
#include <vector>

/**
 * Basic thread guard implementation and example
 * usage
 */
class thread_guard {
   private:
    std::thread &t;

   public:
    explicit thread_guard(std::thread &t_) : t(t_) {}
    ~thread_guard() {
        if (t.joinable()) t.join();
    }
    thread_guard(const thread_guard &) = delete;
    thread_guard &operator=(const thread_guard &) = delete;
};

int main() {
    {
        std::cout << "Starting thread 1..." << std::endl;
        std::thread t1([] {
            for (int i = 0; i < 100; i++) std::cout << i << std::endl;
        });
        thread_guard tg(t1);
    }
    return 0;
}