#include <atomic>
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct insufficient_funds : std::exception {
    const char* what() const throw() override { return "Insufficient funds"; }
};

struct invalid_amount : std::exception {
    const char* what() const throw() override { return "Invalid amount (negative or zero)"; }
};

class Account {
   private:
    double balance;
    mutable std::mutex m;
    std::string name;  // Optional: add account identifier

   public:
    Account(double bal = 0.0, const std::string& name = "") : balance(bal), name(name) {}

    Account(const Account& other) {
        std::lock_guard<std::mutex> lock(other.m);
        balance = other.balance;
        name = other.name;
    }

    Account(Account&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.m);
        balance = other.balance;
        name = std::move(other.name);  // Only move the string, not the double
        other.balance = 0.0;           // Reset moved-from object
    }

    Account& operator=(const Account& other) = delete;

    Account& operator=(Account&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(m, other.m);  // Named lock variable
            balance = other.balance;
            name = std::move(other.name);
            other.balance = 0.0;
        }
        return *this;
    }

    void deposit(double amount) {
        if (amount <= 0) throw invalid_amount();
        std::lock_guard<std::mutex> lock(m);
        balance += amount;
    }

    double withdraw(double amount) {
        if (amount <= 0) throw invalid_amount();
        std::lock_guard<std::mutex> lock(m);
        if (amount > balance) throw insufficient_funds();
        balance -= amount;
        return amount;
    }

    double getBalance() const {
        std::lock_guard<std::mutex> lock(m);
        return balance;
    }

    std::string getName() const {
        std::lock_guard<std::mutex> lock(m);
        return name;
    }

    std::string toString() const {
        std::lock_guard<std::mutex> lock(m);
        std::stringstream ss;
        ss << "Account [" << (name.empty() ? "unnamed" : name) << "] Balance: $" << balance;
        return ss.str();
    }

    friend class Bank;
};

class Bank {
   private:
    std::string name;  // Optional: bank identifier

   public:
    Bank(const std::string& name = "") : name(name) {}

    void transfer(Account& from, Account& to, double amount) {
        if (amount <= 0) throw invalid_amount();

        // Lock both accounts to prevent deadlock
        std::scoped_lock lock(from.m, to.m);

        // Check balance after acquiring the lock
        if (from.balance < amount) throw insufficient_funds();

        // Perform the transfer
        from.balance -= amount;
        to.balance += amount;
    }

    std::string getName() const { return name; }
};

int main() {
    // Create a bank
    Bank bank("First National Bank");
    std::cout << "Bank Simulation: " << bank.getName() << std::endl;
    std::cout << "==================================\n" << std::endl;

    // Create accounts with initial balances
    Account accounts[] = {Account(1000.0, "Alice"), Account(2000.0, "Bob"),
                          Account(1500.0, "Charlie"), Account(3000.0, "Diana"),
                          Account(500.0, "Evan")};
    const int NUM_ACCOUNTS = sizeof(accounts) / sizeof(Account);

    // Display initial account states
    std::cout << "Initial Account Balances:" << std::endl;
    for (int i = 0; i < NUM_ACCOUNTS; ++i) {
        std::cout << accounts[i].toString() << std::endl;
    }
    std::cout << std::endl;

    // Set up random number generation
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> accountDist(0, NUM_ACCOUNTS - 1);
    std::uniform_int_distribution<> amountDist(50, 500);
    std::uniform_int_distribution<> operationDist(0, 2);  // 0=deposit, 1=withdraw, 2=transfer

    // Statistics tracking
    std::atomic<int> successfulDeposits(0);
    std::atomic<int> successfulWithdrawals(0);
    std::atomic<int> successfulTransfers(0);
    std::atomic<int> failedOperations(0);

    // Thread function for random operations
    auto threadFunc = [&](int threadId, int numOperations) {
        for (int i = 0; i < numOperations; ++i) {
            try {
                // Choose operation type
                int operation = operationDist(gen);

                if (operation == 0) {  // Deposit
                    int accountIdx = accountDist(gen);
                    double amount = amountDist(gen);
                    accounts[accountIdx].deposit(amount);
                    successfulDeposits++;

                    std::cout << "Thread " << threadId << ": Deposited $" << amount << " to "
                              << accounts[accountIdx].getName() << std::endl;
                } else if (operation == 1) {  // Withdraw
                    int accountIdx = accountDist(gen);
                    double amount =
                        amountDist(gen) / 2.0;  // Smaller withdrawals to avoid too many failures
                    accounts[accountIdx].withdraw(amount);
                    successfulWithdrawals++;

                    std::cout << "Thread " << threadId << ": Withdrew $" << amount << " from "
                              << accounts[accountIdx].getName() << std::endl;
                } else {  // Transfer
                    int fromIdx = accountDist(gen);
                    int toIdx;
                    do {
                        toIdx = accountDist(gen);
                    } while (toIdx == fromIdx);  // Ensure different accounts

                    double amount =
                        amountDist(gen) / 3.0;  // Even smaller transfers to avoid too many failures
                    bank.transfer(accounts[fromIdx], accounts[toIdx], amount);
                    successfulTransfers++;

                    std::cout << "Thread " << threadId << ": Transferred $" << amount << " from "
                              << accounts[fromIdx].getName() << " to " << accounts[toIdx].getName()
                              << std::endl;
                }
            } catch (const insufficient_funds& e) {
                failedOperations++;
                // Uncomment for more verbose output
                // std::cout << "Thread " << threadId << ": Operation failed - " << e.what() <<
                // std::endl;
            } catch (const invalid_amount& e) {
                failedOperations++;
                // Uncomment for more verbose output
                // std::cout << "Thread " << threadId << ": Operation failed - " << e.what() <<
                // std::endl;
            }

            // Pause briefly to make output readable
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    };

    // Create threads
    const int NUM_THREADS = 5;
    const int OPERATIONS_PER_THREAD = 20;
    std::vector<std::thread> threads;

    std::cout << "Starting " << NUM_THREADS << " threads with " << OPERATIONS_PER_THREAD
              << " operations each...\n"
              << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(threadFunc, i + 1, OPERATIONS_PER_THREAD);
    }

    // Join all threads
    for (auto& thread : threads) {
        thread.join();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Display final account states
    std::cout << "\nFinal Account Balances:" << std::endl;
    double totalMoney = 0.0;
    for (int i = 0; i < NUM_ACCOUNTS; ++i) {
        std::cout << accounts[i].toString() << std::endl;
        totalMoney += accounts[i].getBalance();
    }

    // Calculate initial total money
    double initialMoney = 1000.0 + 2000.0 + 1500.0 + 3000.0 + 500.0;

    // Display statistics
    std::cout << "\nOperation Statistics:" << std::endl;
    std::cout << "---------------------" << std::endl;
    std::cout << "Successful deposits:    " << successfulDeposits.load() << std::endl;
    std::cout << "Successful withdrawals: " << successfulWithdrawals.load() << std::endl;
    std::cout << "Successful transfers:   " << successfulTransfers.load() << std::endl;
    std::cout << "Failed operations:      " << failedOperations.load() << std::endl;
    std::cout << "Total operations:       "
              << (successfulDeposits + successfulWithdrawals + successfulTransfers +
                  failedOperations)
              << std::endl;

    std::cout << "\nSimulation completed in " << duration.count() << " milliseconds" << std::endl;

    return 0;
}