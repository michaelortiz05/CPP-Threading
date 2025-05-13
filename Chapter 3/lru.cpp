#include <atomic>
#include <chrono>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

template <typename K, typename V>
struct Node {
    K key;
    V value;
    std::shared_ptr<Node<K, V>> next;
    std::weak_ptr<Node<K, V>> prev;

    // Constructor accepting lvalue references
    Node(const K& k, const V& v) : key(k), value(v), next(nullptr) {}

    // Constructor accepting rvalue references for move semantics
    Node(K&& k, V&& v) : key(std::move(k)), value(std::move(v)), next(nullptr) {}

    // Constructor for sentinel nodes (used by head and tail)
    Node() : next(nullptr) {}
};

template <typename K, typename V>
class ThreadSafeLRU {
   private:
    mutable std::mutex m;
    unsigned capacity;
    unsigned size;
    std::unordered_map<K, std::shared_ptr<Node<K, V>>> map;
    std::shared_ptr<Node<K, V>> head;
    std::shared_ptr<Node<K, V>> tail;

    // Hit/miss statistics
    mutable unsigned hits;
    mutable unsigned misses;

    void removeNode(std::shared_ptr<Node<K, V>> node) {
        if (auto prev = node->prev.lock()) {
            prev->next = node->next;
        }
        if (node->next) {
            node->next->prev = node->prev;
        }
        node->next = nullptr;
        node->prev.reset();
    }

    void addToFront(std::shared_ptr<Node<K, V>> node) {
        node->next = head->next;
        node->prev = head;
        head->next->prev = node;
        head->next = node;
    }

    void moveToFront(std::shared_ptr<Node<K, V>> node) {
        removeNode(node);
        addToFront(node);
    }

    void evictLRU() {
        if (size == 0) return;

        auto lru = tail->prev.lock();
        if (lru && lru != head) {
            removeNode(lru);
            map.erase(lru->key);
            size--;
        }
    }

   public:
    explicit ThreadSafeLRU(unsigned cap = 10) : capacity(cap), size(0), hits(0), misses(0) {
        if (cap == 0) {
            throw std::invalid_argument("Capacity must be greater than zero");
        }
        head = std::make_shared<Node<K, V>>();
        tail = std::make_shared<Node<K, V>>();
        head->next = tail;
        tail->prev = head;
    }
    ThreadSafeLRU(const ThreadSafeLRU& other) = delete;
    ThreadSafeLRU(ThreadSafeLRU&& other) = delete;
    ThreadSafeLRU& operator=(const ThreadSafeLRU&) = delete;
    ThreadSafeLRU& operator=(ThreadSafeLRU&&) = delete;

    const V& get(const K& key) const {
        std::lock_guard<std::mutex> lock(m);
        auto it = map.find(key);
        if (it == map.end()) {
            misses++;
            throw std::out_of_range("Key not found");
        }

        auto node = it->second;
        const_cast<ThreadSafeLRU<K, V>*>(this)->moveToFront(node);
        hits++;
        return node->value;
    }

    bool contains(const K& key) const {
        std::lock_guard<std::mutex> lock(m);
        return map.find(key) != map.end();
    }

    void put(const K& key, const V& value) {
        std::lock_guard<std::mutex> lock(m);
        auto it = map.find(key);
        if (it != map.end()) {
            auto node = it->second;
            node->value = value;
            moveToFront(node);
            return;
        }

        auto newNode = std::make_shared<Node<K, V>>(key, value);
        map[key] = newNode;
        addToFront(newNode);
        size++;

        if (size > capacity) {
            evictLRU();
        }
    }

    void put(K&& key, V&& value) {
        std::lock_guard<std::mutex> lock(m);
        // Don't make a copy - use the key directly for lookup
        auto it = map.find(key);
        if (it != map.end()) {
            auto node = it->second;
            node->value = std::move(value);
            moveToFront(node);
            return;
        }

        auto newNode = std::make_shared<Node<K, V>>(std::move(key), std::move(value));
        // Use the key inside the node for the map
        map[newNode->key] = newNode;
        addToFront(newNode);
        size++;

        if (size > capacity) {
            evictLRU();
        }
    }

    bool remove(const K& key) {
        std::lock_guard<std::mutex> lock(m);
        auto it = map.find(key);
        if (it == map.end()) {
            return false;
        }

        auto node = it->second;
        removeNode(node);
        map.erase(it);
        size--;
        return true;
    }

    // Statistics methods
    std::pair<unsigned, unsigned> getHitMissStats() const {
        std::lock_guard<std::mutex> lock(m);
        return {hits, misses};
    }

    double getHitRatio() const {
        std::lock_guard<std::mutex> lock(m);
        unsigned total = hits + misses;
        return total > 0 ? static_cast<double>(hits) / total : 0.0;
    }

    void resetStats() {
        std::lock_guard<std::mutex> lock(m);
        hits = 0;
        misses = 0;
    }

    unsigned getSize() const {
        std::lock_guard<std::mutex> lock(m);
        return size;
    }

    unsigned getCapacity() const {
        std::lock_guard<std::mutex> lock(m);
        return capacity;
    }
};

int main() {
    // Create a thread-safe LRU cache with capacity 10
    ThreadSafeLRU<std::string, int> cache(10);

    std::cout << "LRU Cache Simulation" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "Initial capacity: " << cache.getCapacity() << std::endl;
    std::cout << "Initial size: " << cache.getSize() << std::endl << std::endl;

    // Setup random number generation
    std::random_device rd;
    std::mt19937 gen(rd());

    // Distribution for selecting operation type (0=get, 1=put, 2=remove)
    std::uniform_int_distribution<> opDist(0, 100);

    // Distribution for selecting keys (bias toward some keys to create hit patterns)
    std::uniform_int_distribution<> keyDist(0, 20);

    // Distribution for values
    std::uniform_int_distribution<> valDist(1, 1000);

    // Atomic counters for operations
    std::atomic<int> gets(0);
    std::atomic<int> puts(0);
    std::atomic<int> removes(0);
    std::atomic<int> successfulGets(0);
    std::atomic<int> failedGets(0);
    std::atomic<int> successfulRemoves(0);

    // Thread worker function
    auto worker = [&](int id, int operations) {
        // Each thread has its own random generator to avoid contention
        std::mt19937 localGen(rd());

        std::cout << "Thread " << id << " starting " << operations << " operations" << std::endl;

        for (int i = 0; i < operations; i++) {
            int op = opDist(localGen);
            std::string key = "key_" + std::to_string(keyDist(localGen));

            try {
                if (op < 60) {  // 60% chance to get
                    // Try to get a value
                    gets++;
                    try {
                        int value = cache.get(key);
                        successfulGets++;
                        if (id == 0 &&
                            i % 50 == 0) {  // Only log occasionally to keep output manageable
                            std::cout << "Thread " << id << ": GET " << key << " -> " << value
                                      << std::endl;
                        }
                    } catch (const std::out_of_range&) {
                        failedGets++;
                        if (id == 0 && i % 50 == 0) {
                            std::cout << "Thread " << id << ": GET " << key << " -> not found"
                                      << std::endl;
                        }
                    }
                } else if (op < 90) {  // 30% chance to put
                    // Add or update a value
                    int value = valDist(localGen);
                    cache.put(key, value);
                    puts++;
                    if (id == 0 && i % 50 == 0) {
                        std::cout << "Thread " << id << ": PUT " << key << " = " << value
                                  << std::endl;
                    }
                } else {  // 10% chance to remove
                    // Remove a value
                    bool removed = cache.remove(key);
                    removes++;
                    if (removed) {
                        successfulRemoves++;
                    }
                    if (id == 0 && i % 50 == 0) {
                        std::cout << "Thread " << id << ": REMOVE " << key << " -> "
                                  << (removed ? "success" : "not found") << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Thread " << id << " caught exception: " << e.what() << std::endl;
            }

            // Introduce small random delay to simulate processing time and increase thread
            // interleaving
            std::this_thread::sleep_for(std::chrono::microseconds(valDist(localGen) % 100));
        }

        std::cout << "Thread " << id << " completed " << operations << " operations" << std::endl;
    };

    // Run simulation with multiple threads
    const int numThreads = 5;
    const int opsPerThread = 1000;
    std::vector<std::thread> threads;

    auto startTime = std::chrono::high_resolution_clock::now();

    // Launch threads
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(worker, i, opsPerThread);
    }

    // Join threads
    for (auto& t : threads) {
        t.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Report statistics
    std::cout << "\nSimulation Complete" << std::endl;
    std::cout << "===================" << std::endl;
    std::cout << "Execution time: " << duration.count() << "ms" << std::endl;
    std::cout << "Final cache size: " << cache.getSize() << "/" << cache.getCapacity() << std::endl;

    // Operation statistics
    std::cout << "\nOperation Statistics:" << std::endl;
    std::cout << "-----------------" << std::endl;
    std::cout << "GET operations:    " << gets << std::endl;
    std::cout << "  - Successful:    " << successfulGets << " (" << std::fixed
              << std::setprecision(1) << (gets > 0 ? 100.0 * successfulGets / gets : 0) << "%)"
              << std::endl;
    std::cout << "  - Failed:        " << failedGets << std::endl;
    std::cout << "PUT operations:    " << puts << std::endl;
    std::cout << "REMOVE operations: " << removes << std::endl;
    std::cout << "  - Successful:    " << successfulRemoves << " (" << std::fixed
              << std::setprecision(1) << (removes > 0 ? 100.0 * successfulRemoves / removes : 0)
              << "%)" << std::endl;
    std::cout << "Total operations:  " << (gets + puts + removes) << std::endl;

    // Cache statistics
    auto [hits, misses] = cache.getHitMissStats();
    std::cout << "\nCache Performance:" << std::endl;
    std::cout << "-----------------" << std::endl;
    std::cout << "Cache hits:   " << hits << std::endl;
    std::cout << "Cache misses: " << misses << std::endl;
    std::cout << "Hit ratio:    " << std::fixed << std::setprecision(2)
              << (cache.getHitRatio() * 100) << "%" << std::endl;

    // Verify the content of the cache
    std::cout << "\nVerifying cache contents:" << std::endl;
    std::cout << "-----------------------" << std::endl;

    // Print the first few items in the cache for verification
    int itemsPrinted = 0;
    for (int i = 0; i < 30 && itemsPrinted < 10; i++) {
        std::string testKey = "key_" + std::to_string(i);
        if (cache.contains(testKey)) {
            try {
                int value = cache.get(testKey);
                std::cout << testKey << " -> " << value << std::endl;
                itemsPrinted++;
            } catch (const std::exception& e) {
                std::cerr << "Error accessing " << testKey << ": " << e.what() << std::endl;
            }
        }
    }

    // Test with a new thread to ensure the cache is still usable after the simulation
    std::cout << "\nPost-simulation verification:" << std::endl;
    std::cout << "---------------------------" << std::endl;

    // Add a known entry
    std::string testKey = "test_verification_key";
    int testValue = 12345;
    std::cout << "Adding new entry: " << testKey << " = " << testValue << std::endl;
    cache.put(testKey, testValue);

    // Verify we can retrieve it
    try {
        int retrievedValue = cache.get(testKey);
        std::cout << "Retrieved entry: " << testKey << " -> " << retrievedValue << std::endl;
        std::cout << "Verification " << (retrievedValue == testValue ? "PASSED" : "FAILED")
                  << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Verification FAILED: " << e.what() << std::endl;
    }

    std::cout << "\nFinal cache size: " << cache.getSize() << "/" << cache.getCapacity()
              << std::endl;
    std::cout << "Simulation completed successfully!" << std::endl;

    return 0;
}