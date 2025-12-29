/**
 * Author: Aditya Tripuraneni
 * 
 * Benchmark Suite for In-Memory Database
 * 
 * Evaluates multi-threaded performance compared to synchronous implementations
 * across various workload patterns including inserts, prefix scans, mixed
 * read-write operations, and point lookups.
 * 
 * NOTE: This is a hot-cache microbenchmark with a small working set and no I/O.
 * Results reflect peak throughput under ideal conditions, not production workloads.
 */

#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <atomic>
#include <iomanip>
#include <functional>

#include "InMemoryDB.h"
#include "ThreadSafeInMemoryDB.h"

using Clock = std::chrono::steady_clock;
using Micros = std::chrono::microseconds;

// Constants
const unsigned int NUM_CORES = std::thread::hardware_concurrency();
constexpr size_t SHARD_COUNT = 16;  // Must match ThreadSafeInMemoryDB::SHARD_COUNT

//------------------------------------------------------------------------------
// Pre-generated keys to avoid string formatting in hot loops
//------------------------------------------------------------------------------

std::vector<std::string> pregenKeys;
std::vector<std::string> pregenUserKeys;
std::vector<std::string> pregenFields;

void initPregenKeys(int maxKeys) {
    pregenKeys.resize(maxKeys);
    pregenUserKeys.resize(maxKeys);
    pregenFields.resize(10);
    
    for (int i = 0; i < maxKeys; ++i) {
        pregenKeys[i] = "k" + std::to_string(i);
        pregenUserKeys[i] = "user" + std::to_string(i);
    }
    for (int i = 0; i < 10; ++i) {
        pregenFields[i] = "f" + std::to_string(i);
    }
}

//------------------------------------------------------------------------------
// Time Helpers
//------------------------------------------------------------------------------

inline long long elapsedMicros(Clock::time_point start, Clock::time_point end) {
    return std::chrono::duration_cast<Micros>(end - start).count();
}

inline double microsToMs(long long micros) {
    return micros / 1000.0;
}

inline double opsPerSecond(int ops, long long micros) {
    return (ops * 1e6) / micros;
}

inline double avgLatencyUs(int ops, long long micros) {
    return static_cast<double>(micros) / ops;
}


template<typename WorkerFn>
long long runThreaded(int numThreads, WorkerFn workerFn) {
    std::atomic<bool> startFlag{false};
    std::atomic<int> readyCount{0};
    
    std::vector<std::thread> threads;
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&, t]() {
            readyCount.fetch_add(1);
            while (!startFlag.load(std::memory_order_acquire)) {}
            workerFn(t);
        });
    }
    
    while (readyCount.load() < numThreads) {}
    
    auto start = Clock::now();
    startFlag.store(true, std::memory_order_release);
    for (auto& th : threads) th.join();
    auto end = Clock::now();
    
    return elapsedMicros(start, end);
}

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
    long long checksum;
    
    static BenchResult create(const std::string& name, int ops, int threads, 
                              long long micros, long long checksum = 0) {
        return {name, ops, threads, microsToMs(micros), 
                opsPerSecond(ops, micros), ::avgLatencyUs(ops, micros), checksum};
    }
};

void printHeader() {
    std::cout << std::left
              << std::setw(45) << "Test"
              << std::setw(10) << "Ops"
              << std::setw(8)  << "Threads"
              << std::setw(12) << "Time(ms)"
              << std::setw(15) << "Ops/sec"
              << std::setw(12) << "Latency(us)"
              << "\n";
    std::cout << std::string(102, '-') << "\n";
}

void printResult(const BenchResult& r) {
    std::cout << std::left
              << std::setw(45) << r.name
              << std::setw(10) << r.ops
              << std::setw(8)  << r.threads
              << std::setw(12) << std::fixed << std::setprecision(2) << r.timeMs
              << std::setw(15) << std::fixed << std::setprecision(0) << r.opsPerSec
              << std::setw(12) << std::fixed << std::setprecision(3) << r.avgLatencyUs
              << "\n";
    if (r.checksum != 0) {
        std::cout << "   Checksum: " << r.checksum << "\n";
    }
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
    const int KEY_MOD = 10000;  
    const int RESERVE_SIZE = 10200;  
    
    // Synchronous baseline
    {
        InMemoryDB db;
        db.reserveKeys(RESERVE_SIZE);
        long long successCount = 0;
        
        auto start = Clock::now();
        for (int i = 0; i < OPS; ++i) {
            if (db.newInsert(pregenKeys[i % KEY_MOD], pregenFields[0], "v", i)) {
                successCount++;
            }
        }
        auto end = Clock::now();
        long long micros = elapsedMicros(start, end);
        printResult(BenchResult::create("Sync Insert (baseline)", OPS, 1, micros, successCount));
    }
    
    // Multi-threaded with varying thread counts
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        db.reserveKeys(RESERVE_SIZE / SHARD_COUNT + 10);  // Per-shard reserve
        std::atomic<long long> successCount{0};
        
        long long micros = runThreaded(numThreads, [&](int t) {
            long long localSuccess = 0;
            for (int i = t; i < OPS; i += numThreads) {
                if (db.newInsert(pregenKeys[i % KEY_MOD], pregenFields[0], "v", i)) {
                    localSuccess++;
                }
            }
            successCount.fetch_add(localSuccess, std::memory_order_relaxed);
        });
        
        printResult(BenchResult::create("MT Insert (" + std::to_string(numThreads) + " threads)", 
                                        OPS, numThreads, micros, successCount.load()));
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
    
    const int SETUP_KEYS = 500;
    const int FIELDS_PER_KEY = 3;
    const int READ_OPS = 100000;
    
    // Pre-generate key names for setup
    std::vector<std::string> keyNames(SETUP_KEYS);
    for (int i = 0; i < SETUP_KEYS; ++i) {
        keyNames[i] = "key" + std::to_string(i);
    }
    
    // Synchronous baseline
    {
        InMemoryDB db;
        db.reserveKeys(SETUP_KEYS + 100);
        for (int i = 0; i < SETUP_KEYS; ++i) {
            for (int f = 0; f < FIELDS_PER_KEY; ++f) {
                db.newInsert(keyNames[i], pregenFields[f], "v", i);
            }
        }

        long long totalResults = 0;
        auto start = Clock::now();
        for (int i = 0; i < READ_OPS; ++i) {
            auto results = db.scanByPrefix("key", SETUP_KEYS + i);
            totalResults += static_cast<long long>(results.size());
        }
        auto end = Clock::now();
        long long micros = elapsedMicros(start, end);
        printResult(BenchResult::create("Sync Prefix Scan (baseline)", READ_OPS, 1, micros, totalResults));
    }
    
    // Multi-threaded reads
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        db.reserveKeys((SETUP_KEYS / SHARD_COUNT) + 10);
        for (int i = 0; i < SETUP_KEYS; ++i) {
            for (int f = 0; f < FIELDS_PER_KEY; ++f) {
                db.newInsert(keyNames[i], pregenFields[f], "v", i);
            }
        }

        std::atomic<long long> totalResults{0};
        
        long long micros = runThreaded(numThreads, [&](int t) {
            long long localResults = 0;
            for (int i = t; i < READ_OPS; i += numThreads) {
                auto results = db.scanByPrefix("key", SETUP_KEYS + i);
                localResults += static_cast<long long>(results.size());
            }
            totalResults.fetch_add(localResults, std::memory_order_relaxed);
        });
        
        printResult(BenchResult::create("MT Prefix Scan (" + std::to_string(numThreads) + " threads)", 
                                        READ_OPS, numThreads, micros, totalResults.load()));
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
        db.reserveKeys(150);  // 100 user keys + margin
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert(pregenUserKeys[i], pregenFields[0], "v", i);
        }
        
        long long checksum = 0;
        auto start = Clock::now();
        for (int i = 0; i < MIXED_OPS; ++i) {
            if (i % 5 == 0) {
                if (db.newInsert(pregenUserKeys[i % 100], pregenFields[0], "v", SETUP_OPS + i)) {
                    checksum++;
                }
            } else {
                auto results = db.scanByPrefix("user", SETUP_OPS + i);
                checksum += static_cast<long long>(results.size());
            }
        }
        auto end = Clock::now();
        long long micros = elapsedMicros(start, end);
        printResult(BenchResult::create("Sync Mixed (baseline)", MIXED_OPS, 1, micros, checksum));
    }
    
    // Multi-threaded mixed
    std::vector<int> threadCounts = {2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        db.reserveKeys(10);  // ~100/16 shards + margin
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert(pregenUserKeys[i], pregenFields[0], "v", i);
        }
        
        std::atomic<int> writeTimestamp{SETUP_OPS};
        std::atomic<long long> checksum{0};
        
        long long micros = runThreaded(numThreads, [&](int t) {
            long long localChecksum = 0;
            for (int i = t; i < MIXED_OPS; i += numThreads) {
                if (i % 5 == 0) {
                    int ts = writeTimestamp.fetch_add(1);
                    if (db.newInsert(pregenUserKeys[i % 100], pregenFields[0], "v", ts)) {
                        localChecksum++;
                    }
                } else {
                    auto results = db.scanByPrefix("user", SETUP_OPS + i);
                    localChecksum += static_cast<long long>(results.size());
                }
            }
            checksum.fetch_add(localChecksum, std::memory_order_relaxed);
        });
        
        printResult(BenchResult::create("MT Mixed (" + std::to_string(numThreads) + " threads)", 
                                        MIXED_OPS, numThreads, micros, checksum.load()));
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
        db.reserveKeys(SETUP_OPS + 50);
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert(pregenKeys[i], pregenFields[0], "val", i);
        }
        
        long long checksum = 0;
        auto start = Clock::now();
        for (int i = 0; i < LOOKUP_OPS; ++i) {
            auto result = db.getValue(pregenKeys[i % SETUP_OPS], pregenFields[0], SETUP_OPS);
            if (result.has_value()) {
                checksum += static_cast<long long>(result->size());
            }
        }
        auto end = Clock::now();
        long long micros = elapsedMicros(start, end);
        printResult(BenchResult::create("Sync getValue (baseline)", LOOKUP_OPS, 1, micros, checksum));
    }
    
    // Multi-threaded getValue
    std::vector<int> threadCounts = {1, 2, 4, 8};
    if (NUM_CORES >= 16) threadCounts.push_back(16);
    
    for (int numThreads : threadCounts) {
        ThreadSafeInMemoryDB db(false);
        db.reserveKeys((SETUP_OPS / SHARD_COUNT) + 10);
        for (int i = 0; i < SETUP_OPS; ++i) {
            db.newInsert(pregenKeys[i], pregenFields[0], "val", i);
        }
        
        std::atomic<long long> checksum{0};
        
        long long micros = runThreaded(numThreads, [&](int t) {
            long long localChecksum = 0;
            for (int i = t; i < LOOKUP_OPS; i += numThreads) {
                auto result = db.getValue(pregenKeys[i % SETUP_OPS], pregenFields[0], SETUP_OPS);
                if (result.has_value()) {
                    localChecksum += static_cast<long long>(result->size());
                }
            }
            checksum.fetch_add(localChecksum, std::memory_order_relaxed);
        });
        
        printResult(BenchResult::create("MT getValue (" + std::to_string(numThreads) + " threads)", 
                                        LOOKUP_OPS, numThreads, micros, checksum.load()));
    }
}



int main() {
    std::cout << "\n";
    std::cout << "╔═══════════════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                 IN-MEMORY DATABASE BENCHMARK SUITE                    ║\n";
    std::cout << "║                    Sync vs Multi-threaded Performance                 ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════════════════════╝\n";
    
    std::cout << "\nSystem Info:\n";
    std::cout << "   Hardware threads: " << NUM_CORES << "\n";
    std::cout << "   C++ Standard:     C++17\n";
    std::cout << "   Clock:            steady_clock (monotonic)\n";
    std::cout << "\nNote: Hot-cache microbenchmark, small working set, no I/O.\n";
    std::cout << "      Results reflect peak throughput under ideal conditions.\n";
    
    // Pre-generate keys to avoid string formatting in hot loops
    initPregenKeys(100000);
    
    benchmarkInserts();
    benchmarkReads();
    benchmarkMixed();
    benchmarkPointLookups();
    
    std::cout << "\nBenchmark suite complete.\n\n";
    return 0;
}
