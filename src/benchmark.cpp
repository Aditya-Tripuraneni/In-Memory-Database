/**
 * Author: Aditya Tripuraneni
 * 
 * Benchmark Suite for In-Memory Database
 * 
 * Evaluates multi-threaded performance compared to synchronous implementations
 * across various workload patterns including inserts, prefix scans, mixed
 * read-write operations, and point lookups.
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <future>
#include <functional>

#include "InMemoryDB.h"
#include "ThreadSafeInMemoryDB.h"

// Configurations
constexpr int BENCHMARK_TIMEOUT_MS = 60000;  
constexpr int WARMUP_OPS = 100;

// Hardware info
const unsigned int NUM_CORES = std::thread::hardware_concurrency();

//------------------------------------------------------------------------------
// Utilities
//------------------------------------------------------------------------------

struct BenchResult {
    std::string name;
    int ops;
    int threads;
    double timeMs;
    double opsPerSec;
    double avgLatencyUs;
    bool timedOut;
};

void printHeader() {
    std::cout << std::left
              << std::setw(45) << "Test"
              << std::setw(10) << "Ops"
              << std::setw(8)  << "Threads"
              << std::setw(12) << "Time(ms)"
              << std::setw(15) << "Ops/sec"
              << std::setw(12) << "Latency(µs)"
              << std::setw(10) << "Status"
              << "\n";
    std::cout << std::string(112, '-') << "\n";
}

void printResult(const BenchResult& r) {
    std::cout << std::left
              << std::setw(45) << r.name
              << std::setw(10) << r.ops
              << std::setw(8)  << r.threads
              << std::setw(12) << std::fixed << std::setprecision(2) << r.timeMs
              << std::setw(15) << std::fixed << std::setprecision(0) << r.opsPerSec
              << std::setw(12) << std::fixed << std::setprecision(3) << r.avgLatencyUs
              << std::setw(10) << (r.timedOut ? "TIMEOUT" : "OK")
              << "\n";
}

// Run a benchmark with timeout protection
template<typename Func>
BenchResult runWithTimeout(const std::string& name, int ops, int threads, Func&& fn) {
    BenchResult result{name, ops, threads, 0, 0, 0, false};
    
    auto task = std::async(std::launch::async, [&]() {
        auto start = std::chrono::high_resolution_clock::now();
        fn();
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    });
    
    auto status = task.wait_for(std::chrono::milliseconds(BENCHMARK_TIMEOUT_MS));
    
    if (status == std::future_status::timeout) {
        result.timedOut = true;
        result.timeMs = BENCHMARK_TIMEOUT_MS;
        result.opsPerSec = 0;
        result.avgLatencyUs = 0;
    } else {
        long long micros = task.get();
        result.timeMs = micros / 1000.0;
        result.opsPerSec = (ops * 1e6) / micros;
        result.avgLatencyUs = static_cast<double>(micros) / ops;
    }
    
    return result;
}

//------------------------------------------------------------------------------
// Benchmark 1: Insert Performance
//------------------------------------------------------------------------------

void benchmarkInserts() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  BENCHMARK 1: Insert Performance                              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    printHeader();
    
    const int OPS = 100000;
    
    // Synchronous baseline
    {
        InMemoryDB db;
        auto result = runWithTimeout("Sync Insert (baseline)", OPS, 1, [&]() {
            for (int i = 0; i < OPS; ++i) {
                db.newInsert("k" + std::to_string(i % 100), "f", "v", i);
            }
        });
        printResult(result);
    }
    
    // Multi-threaded with varying thread counts
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);  // Disable background cleanup
        
        auto result = runWithTimeout(
            "MT Insert (" + std::to_string(numThreads) + " threads)",
            OPS, numThreads, [&]() {
                std::vector<std::thread> threads;
                std::atomic<int> counter{0};
                
                for (int t = 0; t < numThreads; ++t) {
                    threads.emplace_back([&, t]() {
                        int opsPerThread = OPS / numThreads;
                        for (int i = 0; i < opsPerThread; ++i) {
                            int idx = counter.fetch_add(1);
                            db.newInsert("k" + std::to_string(idx % 100), "f", "v", idx);
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });
        printResult(result);
    }
}

//------------------------------------------------------------------------------
// Benchmark 2: Read Performance (Prefix Scans)
//------------------------------------------------------------------------------

void benchmarkReads() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  BENCHMARK 2: Read Performance (Prefix Scans)                 ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    printHeader();
    
    const int SETUP_KEYS = 500; //! to be tweaked later to bench mark on larger keyspace
    const int FIELDS_PER_KEY = 3;
    const int READ_OPS = 100000;  // Reduced from 100k for reasonable runtime
    
    // Synchronous baseline
    {
        InMemoryDB db;
        for (int i = 0; i < SETUP_KEYS; ++i) {
            for (int f = 0; f < FIELDS_PER_KEY; ++f) {
                db.newInsert("key" + std::to_string(i), "f" + std::to_string(f), "v" + std::to_string(f), i);
            }
        }

        long long totalResults = 0;
        auto result = runWithTimeout("Sync Prefix Scan (baseline)", READ_OPS, 1, [&]() {
            for (int i = 0; i < READ_OPS; ++i) {
                auto results = db.scanByPrefix("key", SETUP_KEYS + i);
                totalResults += static_cast<long long>(results.size());
            }
        });
        printResult(result);
        std::cout << "   Checksum (results counted): " << totalResults << "\n";
    }
    
    // Multi-threaded reads
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        for (int i = 0; i < SETUP_KEYS; ++i) {
            for (int f = 0; f < FIELDS_PER_KEY; ++f) {
                db.newInsert("key" + std::to_string(i), "f" + std::to_string(f), "v" + std::to_string(f), i);
            }
        }

        std::atomic<long long> totalResults{0};
        auto result = runWithTimeout(
            "MT Prefix Scan (" + std::to_string(numThreads) + " threads)",
            READ_OPS, numThreads, [&]() {
                std::vector<std::thread> threads;
                
                for (int t = 0; t < numThreads; ++t) {
                    threads.emplace_back([&, t]() {
                        int opsPerThread = READ_OPS / numThreads;
                        for (int i = 0; i < opsPerThread; ++i) {
                            auto results = db.scanByPrefix("key", SETUP_KEYS + t * opsPerThread + i);
                            totalResults.fetch_add(static_cast<long long>(results.size()), std::memory_order_relaxed);
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });
        printResult(result);
        std::cout << "   Checksum (results counted): " << totalResults.load() << "\n";
    }
}

//------------------------------------------------------------------------------
// Benchmark 3: Mixed Workload (Read-Heavy: 80% reads, 20% writes)
//------------------------------------------------------------------------------

void benchmarkMixed() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  BENCHMARK 3: Mixed Workload (80% Read, 20% Write)            ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    printHeader();
    
    const int SETUP_OPS = 100;
    const int MIXED_OPS = 100000;
    
    // Synchronous baseline
    {
        InMemoryDB db;
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert("user" + std::to_string(i), "f", "v", i);
        }
        
        auto result = runWithTimeout("Sync Mixed (baseline)", MIXED_OPS, 1, [&]() {
            for (int i = 0; i < MIXED_OPS; ++i) {
                if (i % 5 == 0) {
                    db.newInsert("user" + std::to_string(i % 100), "f", "v", SETUP_OPS + i);
                } else {
                    db.scanByPrefix("user", SETUP_OPS + i);
                }
            }
        });
        printResult(result);
    }
    
    // Multi-threaded mixed
    std::vector<int> threadCounts = {2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert("user" + std::to_string(i), "f", "v", i);
        }
        
        std::atomic<int> writeCounter{SETUP_OPS};
        
        auto result = runWithTimeout(
            "MT Mixed (" + std::to_string(numThreads) + " threads)",
            MIXED_OPS, numThreads, [&]() {
                std::vector<std::thread> threads;
                
                for (int t = 0; t < numThreads; ++t) {
                    threads.emplace_back([&, t]() {
                        int opsPerThread = MIXED_OPS / numThreads;
                        for (int i = 0; i < opsPerThread; ++i) {
                            int globalIdx = t * opsPerThread + i;
                            if (globalIdx % 5 == 0) {
                                int ts = writeCounter.fetch_add(1);
                                db.newInsert("user" + std::to_string(globalIdx % 100), "f", "v", ts);
                            } else {
                                db.scanByPrefix("user", SETUP_OPS + globalIdx);
                            }
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });
        printResult(result);
    }
}

//------------------------------------------------------------------------------
// Benchmark 4: Point Lookups (getValue)
//------------------------------------------------------------------------------

void benchmarkPointLookups() {
    std::cout << "\n╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║  BENCHMARK 4: Point Lookups (getValue)                        ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";
    
    printHeader();
    
    const int SETUP_OPS = 500;
    const int LOOKUP_OPS = 100000;
    
    // Synchronous baseline
    {
        InMemoryDB db;
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert("k" + std::to_string(i), "f", "val" + std::to_string(i), i);
        }
        
        auto result = runWithTimeout("Sync getValue (baseline)", LOOKUP_OPS, 1, [&]() {
            for (int i = 0; i < LOOKUP_OPS; ++i) {
                db.getValue("k" + std::to_string(i % SETUP_OPS), "f", SETUP_OPS);
            }
        });
        printResult(result);
    }
    
    // Multi-threaded getValue
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert("k" + std::to_string(i), "f", "val" + std::to_string(i), i);
        }
        
        auto result = runWithTimeout(
            "MT getValue (" + std::to_string(numThreads) + " threads)",
            LOOKUP_OPS, numThreads, [&]() {
                std::vector<std::thread> threads;
                
                for (int t = 0; t < numThreads; ++t) {
                    threads.emplace_back([&, t]() {
                        int opsPerThread = LOOKUP_OPS / numThreads;
                        for (int i = 0; i < opsPerThread; ++i) {
                            db.getValue("k" + std::to_string((t * opsPerThread + i) % SETUP_OPS), "f", SETUP_OPS);
                        }
                    });
                }
                for (auto& th : threads) th.join();
            });
        printResult(result);
    }
}



int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                 IN-MEMORY DATABASE BENCHMARK SUITE                     ║\n";
    std::cout << "║                    Sync vs Multi-threaded Performance                  ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\nSystem Info:\n";
    std::cout << "   Hardware threads: " << NUM_CORES << "\n";
    std::cout << "   Timeout per test: " << BENCHMARK_TIMEOUT_MS << " ms\n";
    std::cout << "   C++ Standard:     C++17\n";
    
    benchmarkInserts();
    benchmarkReads();
    benchmarkMixed();
    benchmarkPointLookups();
    
    std::cout << "\nBenchmark suite complete.\n\n";
    return 0;
}
