#include <iostream>
#include <thread>
#include <vector>
#include <utility>
#include <cmath>
#include <algorithm>
#include <numeric>

struct Work
{
    std::vector<int> &arr;
    std::pair<int, int> bounds;
    int sum;

    Work(std::vector<int> &arr_, int s, int e) : arr(arr_), bounds({s, e}), sum(0) {}

    void operator()()
    {
        for (int i = bounds.first; i < bounds.second; i++)
        {
            sum += arr[i];
        }
    }
};

int sum_vector(std::vector<int> &arr, int numThreads)
{
    if (arr.empty())
        return 0;
    if (numThreads <= 0)
        numThreads = 1;

    // Create worker objects first and keep them alive
    std::vector<Work> workers;
    workers.reserve(numThreads);

    int chunkSize = (int)std::ceil(arr.size() / static_cast<double>(numThreads));

    for (size_t i = 0; i < arr.size(); i += chunkSize)
    {
        size_t end = std::min(i + chunkSize, arr.size());
        workers.emplace_back(arr, i, end);
    }

    // Now create threads, using references to our worker objects
    std::vector<std::thread> threads;
    threads.reserve(workers.size());

    for (auto &worker : workers)
    {
        // Use std::ref to pass a reference to the worker
        // threads.emplace_back([&worker]()
        //                      { worker(); });
        threads.emplace_back(std::ref(worker));
    }

    // Join all threads
    for (auto &t : threads)
    {
        if (t.joinable())
        {
            t.join();
        }
    }

    // Sum up results
    int total = 0;
    for (const auto &w : workers)
    {
        total += w.sum;
    }

    return total;
}

int main()
{
    // Create test vector with known sum
    std::vector<int> test_vector(1000);
    std::iota(test_vector.begin(), test_vector.end(), 1); // Fill with 1 to 1000
    int expected_sum = 500500;                            // Sum of 1 to 1000 is n*(n+1)/2

    int thread_count = 4;
    int threaded_sum = sum_vector(test_vector, thread_count);

    std::cout << "Vector size: " << test_vector.size() << std::endl;
    std::cout << "Number of threads: " << thread_count << std::endl;
    std::cout << "Threaded sum: " << threaded_sum << std::endl;
    std::cout << "Expected sum: " << expected_sum << std::endl;
    std::cout << "Correct: " << (threaded_sum == expected_sum ? "Yes" : "No") << std::endl;

    return 0;
}