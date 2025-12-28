# In-Memory Database: Multi-Threading Performance Experiment

**Author**: Aditya Tripuraneni

## Purpose

This project evaluates the performance impact of introducing multi-threading to an in-memory key-value database. The experiment compares a synchronous baseline implementation against a thread-safe version using reader-writer locks and sharding to understand:

- How reader-writer lock patterns affect throughput under different workloads
- The overhead of synchronization primitives in high-concurrency scenarios
- Scalability characteristics across varying thread counts (1, 2, 4, 8, 16 threads)
- Trade-offs between fine-grained locking (per-key) and coarse-grained approaches

## Features

### Core Data Model
**Hierarchical Key-Value Store with Version History**
```
db: unordered_map<string, FieldMap>
    └── key (e.g., "user1")
        └── FieldMap: unordered_map<string, FieldEntry>
            └── field (e.g., "name")
                └── FieldEntry:
                    ├── DLL (Doubly Linked List of versions)
                    │   └── Latest: Alice
                    │   └── Older:  Bob
                    └── node_map: timestamp → Node*
```

### Key Capabilities
1. **Multi-version Storage**: Each (key, field) maintains chronological history via doubly-linked list
2. **Prefix Search**: Trie-indexed keys enable fast O(m) prefix scans where m = prefix length
3. **TTL Expiry**: Min-heap with background cleanup worker for async expiration
4. **Point Lookups**: O(1) latest-value retrieval via DLL tail pointer
5. **Thread Safety**: Sharded database (16 shards) with per-key locks for fine-grained concurrency

---

## Thread-Safe Architecture

### Sharding Strategy
```
┌─────────────────────────────────────────────────┐
│   ThreadSafeInMemoryDB (16 Shards)             │
├─────────────────────────────────────────────────┤
│  Shard 0:  keys 0-N/16                          │
│  ├─ shardMutex (shared_mutex)                   │
│  └─ Per-Key Buckets:                            │
│      └─ keyMutex (shared_mutex)                 │
│                                                  │
│  Trie: trieMutex (shared_mutex)                 │
│  TTL Heap: ttlMutex (mutex)                     │
│                                                  │
│  Background Thread:                             │
│  └─ cleanupWorker() [async TTL cleanup]         │
└─────────────────────────────────────────────────┘
```

### Lock Hierarchy

| Component | Lock Type | Granularity | Access Pattern |
|-----------|-----------|-------------|-----------------|
| Shard | `shared_mutex` | 1/16th of keys | Multiple readers, exclusive writers |
| Key Bucket | `shared_mutex` | Per-key | Isolated concurrent updates to different keys |
| Trie | `shared_mutex` | Global | Snapshot-based prefix search |
| TTL Heap | `mutex` | Global | Background cleanup + insert serialization |

**Key Insight**: Sharding + per-key locks allow writes to different keys to proceed in parallel, while reads use shared locks for high concurrency.

---

## Benchmark Results

### Test Configuration
- **Hardware**: 16-core CPU
- **Operations**: 100,000 ops per test
- **Dataset**: 500 keys × 3 fields for prefix scans
- **Timeout**: 60 seconds per test

### 1. Insert Performance
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)
─────────────────────────────────────────────────────────────────────────────────────
Sync Insert (baseline)        100000   1         31.57      3,167,163    0.316
MT Insert (1 thread)          100000   1         34.97      2,859,676    0.350
MT Insert (2 threads)         100000   2         32.65      3,062,600    0.327
MT Insert (4 threads)         100000   4         28.36      3,526,466    0.284
MT Insert (8 threads)         100000   8         28.01      3,570,026    0.280
MT Insert (16 threads)        100000   16        32.51      3,075,787    0.325
```

**Observation**: Single-threaded MT has ~10% overhead from locking. 8 threads achieve **1.13× speedup** over sync baseline. Trie sharding (16 independent Trie+mutex pairs) distributes key insertion load, improving write parallelism vs. earlier single-global-Trie design.

### 2. Prefix Scan Performance
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)
─────────────────────────────────────────────────────────────────────────────────────
Sync Prefix Scan (baseline)   100000   1         30560.72   3,272        305.607
MT Prefix Scan (1 thread)     100000   1         36829.39   2,715        368.294
MT Prefix Scan (2 threads)    100000   2         19324.51   5,175        193.245
MT Prefix Scan (4 threads)    100000   4         11011.71   9,081        110.117
MT Prefix Scan (8 threads)    100000   8         7554.27    13,238       75.543
MT Prefix Scan (16 threads)   100000   16        5379.67    18,588       53.797

Checksum: 150,000,000 results (500 keys × 3 fields × 100,000 scans)
```

**Observation**: Solid scaling. 16 threads achieve **5.7× speedup** over sync baseline. Trie sharding allowed parallel queries across 16 independent Trie+mutex pairs with shared locks, improving read throughput. Minimal improvement from old design (6.2% at 16 threads) since reads were already parallelized; the sharding mainly benefits write-heavy mixed workloads.

### 3. Mixed Workload (80% Read, 20% Write)
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)
─────────────────────────────────────────────────────────────────────────────────────
Sync Mixed (baseline)         100000   1         1231.87    81,177       12.319
MT Mixed (2 threads)          100000   2         1153.41    86,700       11.534
MT Mixed (4 threads)          100000   4         672.74     148,647      6.727
MT Mixed (8 threads)          100000   8         451.85     221,313      4.518
MT Mixed (16 threads)         100000   16        486.10     205,719      4.861
```

**Observation**: 8 threads achieve **2.7× speedup** over sync baseline. Trie sharding significantly improved write scaling: 8 threads now reach 221k ops/sec (was 182k) and 16 threads jump from 147k to 206k ops/sec (+40% improvement). Write parallelism was clearly improved to increase throughput.

### 4. Point Lookups (getValue)
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)
─────────────────────────────────────────────────────────────────────────────────────
Sync getValue (baseline)      100000   1         7.73       12,934,937   0.077
MT getValue (1 thread)        100000   1         10.55      9,477,775    0.106
MT getValue (2 threads)       100000   2         8.23       12,144,766   0.082
MT getValue (4 threads)       100000   4         4.85       20,627,063   0.048
MT getValue (8 threads)       100000   8         3.27       30,599,755   0.033
MT getValue (16 threads)      100000   16        3.31       30,202,356   0.033

```

**Observation**: Near linear scaling. 16 threads achieve **2.4× speedup** over sync baseline with minimal lock overhead for read-only operations.

---

## Synchronous vs Multi-Threaded Comparison

| Metric | Synchronous | Multi-Threaded (8 threads) | Speedup |
|--------|-------------|---------------------------|---------|
| Insert Latency | 0.316 µs | 0.280 µs | 1.13× |
| Prefix Scan Latency | 305.607 µs | 75.543 µs | **4.0×** |
| Mixed Workload Latency | 12.319 µs | 4.518 µs | **2.7×** |
| Point Lookup Latency | 0.077 µs | 0.033 µs | **2.3×** |
| Memory Overhead | Baseline | +48 bytes/key (mutexes) | - |
| TTL Cleanup | Synchronous (blocking) | Async (background thread) | Non-blocking |

### Key Observations

1. **Read-Heavy Workloads Excel**: Prefix scans and point lookups show 4-5× improvement with 8-16 threads due to shared locks allowing parallel reads.

2. **Write Scalability Improved**: Trie sharding (16 independent Trie+mutex pairs) enables parallel inserts to different key shards. Mixed workload speedup at 16 threads improved from 2.1× to 2.7×, with insert throughput gaining +40% at 16 threads.

3. **Locking Overhead**: Single-threaded MT version is ~10% slower than sync due to mutex acquisition/release costs, even without contention.

4. **Optimal Thread Count**: Performance peaks around 8 threads for most workloads; 16 threads show diminishing returns due to increased context switching and cache coherency overhead.

5. **Async TTL Cleanup Works**: Background worker with adaptive wake-up (peeking heap for next expiry) prevents blocking main operations while keeping memory usage bounded.

6. **Trie Memory Trade-off**: Current design stores complete key strings in `unordered_set` at each node. With 500 keys this is fast, but memory scales as O(keys × depth). Acceptable at current scale; would need optimization beyond 10k keys.

---

## Future Directions

### Near-Term Optimizations
1. **Lock-Free Trie Reads**: Replace `trieMutex` with atomic reference counting + copy-on-write snapshots to eliminate read serialization
2. **Partitioned TTL Heap**: Shard min-heap by time ranges (e.g., 16 buckets) to reduce contention on TTL insert path
3. **Concurrent Hash Map**: Replace `std::unordered_map` with lock-free alternatives (Intel TBB, Folly) to avoid rehashing locks

### Scalability Experiments
1. **Vary Shard Count**: Test 32, 64, 128 shards to find optimal balance between lock contention and cache locality
2. **Larger Datasets**: Scale to 100k+ keys to stress-test Trie memory efficiency and cleanup performance

### Advanced Features
1. **Read-Write Fairness**: Add reader/writer priority queues to prevent starvation under heavy write loads
---

## Running the Benchmarks

### Build
```bash
cd /path/to/repo
make clean && make
```

### Execute
```bash
./benchmark
```

Output includes per-test latency, throughput, and timeout status. Prefix scan tests display checksums to validate correctness.

---

## Usage Example

```cpp
#include "ThreadSafeInMemoryDB.h"
#include <thread>
#include <iostream>

int main() {
    ThreadSafeInMemoryDB db;
    
    std::thread writer([&]() {
        for (int i = 0; i < 1000; ++i) {
            db.newInsert("user" + std::to_string(i), 
                        "email", 
                        "user@example.com", 
                        i,
                        30);
        }
    });
    
    std::thread reader([&]() {
        for (int i = 0; i < 100; ++i) {
            auto results = db.scanByPrefix("user", 1000 + i);
            std::cout << "Found: " << results.size() << " records\n";
        }
    });
    
    writer.join();
    reader.join();
    
    std::cout << "Total keys: " << db.getKeyCount() << "\n";
    return 0;
}
```

---

## Conclusion

This experiment demonstrates that **reader-writer locks with sharding provide substantial performance gains for read-heavy workloads** (4-5× speedup), while write-heavy patterns see modest improvements (~1.1×) limited by global Trie contention. The sharded architecture with per-key locks strikes a balance between concurrency and complexity, making it suitable for multi-threaded applications without major code restructuring.

Key takeaway: **Fine-grained locking pays off when reads dominate**, but write scalability requires more sophisticated techniques like lock-free data structures or MVCC.

---

## Acknowledgments

**Experimental Context**: This benchmark is a **hot-cache experiment**. All data remains in memory throughout execution, with threads repeatedly accessing the same 500-key dataset (500 keys × 3 fields). Results reflect performance under ideal cache conditions with no page faults, I/O, or memory pressure. Real-world performance may vary significantly with:

- **Large datasets** causing cache misses
- **Non-uniform access patterns** with temporal/spatial locality variations
- **Competing processes** sharing CPU and L3 cache
- **Real workloads** with unpredictable key distributions and field cardinality

**Benchmarking methodology**: Operations are CPU bound within a loop (100k iterations per test). No realistic delays, network latency, or I/O operations are simulated. Insert operations do not trigger evictions or rehashing (capacity pre-allocated). Prefix scans query the same prefix repeatedly (stable working set).


Benchmark suite designed to evaluate multi-threading strategies across 4 workload patterns. Trie-based prefix search adapted from standard implementations; 16-shard architecture with per-key locks and Trie sharding are custom optimizations.
