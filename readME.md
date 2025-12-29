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
- **Clock**: `steady_clock` (monotonic, not affected by network time protocol drift)
- **Methodology**:
  - Pre-generated keys (no string formatting in hot loops)
  - Start gates (all threads begin simultaneously)
  - Strided loops (exact op distribution across threads)
  - Checksums (prevent dead-code elimination, validate correctness)

### 1. Insert Performance
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)  Checksum
────────────────────────────────────────────────────────────────────────────────────────────────
Sync Insert (baseline)        100000   1         30.12      3,319,833    0.301        100000
MT Insert (1 thread)          100000   1         77.96      1,282,726    0.780        100000
MT Insert (2 threads)         100000   2         77.14      1,296,260    0.771        100000
MT Insert (4 threads)         100000   4         47.24      2,116,626    0.472        100000
MT Insert (8 threads)         100000   8         45.45      2,200,123    0.455        100000
MT Insert (16 threads)        100000   16        74.51      1,342,030    0.745        100000
```

**Observation**: Single-threaded MT shows significant overhead (~2.5×) from lock acquisition and thread management. Peak performance at 8 threads achieves **2.2M ops/sec**. Write-heavy workloads are limited by lock contention on shared data structures. The checksum (successful insert count) validates all 100k operations completed.

### 2. Prefix Scan Performance
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)  Checksum
────────────────────────────────────────────────────────────────────────────────────────────────
Sync Prefix Scan (baseline)   100000   1         29334.81   3,409        293.348      150000000
MT Prefix Scan (1 thread)     100000   1         33084.67   3,023        330.847      150000000
MT Prefix Scan (2 threads)    100000   2         20160.06   4,960        201.601      150000000
MT Prefix Scan (4 threads)    100000   4         11333.93   8,823        113.339      150000000
MT Prefix Scan (8 threads)    100000   8         7082.81    14,119       70.828       150000000
MT Prefix Scan (16 threads)   100000   16        5283.84    18,926       52.838       150000000
```

**Observation**: Excellent scaling. 16 threads achieve **5.6× speedup** over sync baseline (18,926 vs 3,409 ops/sec). Shared locks enable parallel reads across Trie shards with minimal contention. Checksum validates correctness: 150M = 500 keys × 3 fields × 100k scans.

### 3. Mixed Workload (80% Read, 20% Write)
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)  Checksum
────────────────────────────────────────────────────────────────────────────────────────────────
Sync Mixed (baseline)         100000   1         1302.79    76,758       13.028       8020000
MT Mixed (2 threads)          100000   2         1079.88    92,603       10.799       8020000
MT Mixed (4 threads)          100000   4         646.16     154,761      6.462        8020000
MT Mixed (8 threads)          100000   8         484.31     206,481      4.843        8020000
MT Mixed (16 threads)         100000   16        513.96     194,570      5.140        8020000
```

**Observation**: 8 threads achieve **2.7× speedup** over sync baseline (206k vs 77k ops/sec). Performance peaks at 8 threads; 16 threads show slight regression due to write contention. Checksum validates mixed operation correctness across all thread configurations.

### 4. Point Lookups (getValue)
```
Test                          Ops      Threads   Time(ms)   Ops/sec      Latency(µs)  Checksum
────────────────────────────────────────────────────────────────────────────────────────────────
Sync getValue (baseline)      100000   1         4.09       24,437,928   0.041        300000
MT getValue (1 thread)        100000   1         6.94       14,407,146   0.069        300000
MT getValue (2 threads)       100000   2         4.69       21,303,792   0.047        300000
MT getValue (4 threads)       100000   4         2.99       33,444,816   0.030        300000
MT getValue (8 threads)       100000   8         2.61       38,358,266   0.026        300000
MT getValue (16 threads)      100000   16        3.01       33,255,737   0.030        300000
```

**Observation**: Excellent scaling for pure reads. 8 threads achieve **1.6× speedup** over sync baseline (38M vs 24M ops/sec). Sync baseline is already fast (~24M ops/sec) due to O(1) hash lookups. Checksum = 300k validates all lookups returned 3-char "val" strings.

---

## Synchronous vs Multi-Threaded Comparison

| Metric | Synchronous | Multi-Threaded (8 threads) | Speedup |
|--------|-------------|---------------------------|---------|
| Insert Throughput | 3.3M ops/sec | 2.2M ops/sec | 0.67× |
| Prefix Scan Latency | 293.3 µs | 70.8 µs | **4.1×** |
| Mixed Workload Latency | 13.0 µs | 4.8 µs | **2.7×** |
| Point Lookup Throughput | 24.4M ops/sec | 38.4M ops/sec | **1.6×** |
| Memory Overhead | Baseline | +48 bytes/key (mutexes) | - |
| TTL Cleanup | Synchronous (blocking) | Async (background thread) | Non-blocking |

### Key Observations

1. **Read-Heavy Workloads Excel**: Prefix scans show **5.6× speedup** at 16 threads due to shared locks allowing parallel Trie traversals. Point lookups achieve **38M ops/sec** at 8 threads.

2. **Write Contention Limits Scaling**: Pure insert workloads are slower with threading due to lock contention. The sync baseline (3.3M ops/sec) outperforms MT versions because hash-map inserts are already fast and locking adds overhead.

3. **Locking Overhead is Significant**: Single-threaded MT is ~2.5× slower than sync for inserts, ~1.7× slower for point lookups. This overhead is the cost of thread-safety even without contention.

4. **Optimal Thread Count is 8**: Performance peaks at 8 threads across all workloads. 16 threads show diminishing returns due to lock contention and cache coherency overhead.

5. **Mixed Workloads Benefit Most**: 80/20 read-write ratio achieves **2.7× speedup** because reads (majority) parallelize well while writes remain serialized.

6. **LRU Eviction Bounds Memory**: Each (key, field) pair maintains at most 100 versions via DLL eviction, preventing unbounded growth under high write loads.

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

This experiment demonstrates that **reader-writer locks with sharding provide substantial performance gains for read-heavy workloads** (4-6× speedup for prefix scans), while **write-heavy patterns actually weakens** due to lock contention overhead. The sync baseline remains faster for pure inserts (~3.3M vs ~2.2M ops/sec at 8 threads).

Key takeaway: **Multi-threading is worthwhile when reads dominate** (prefix scans, point lookups). For write-heavy workloads, consider lock-free data structures, batched writes, or accepting single-threaded performance. The sharded architecture with per-key locks provides correctness and reasonable read scaling without major complexity.

---

## Acknowledgments

**Experimental Context**: This benchmark is a **hot-cache microbenchmark**. All data remains in memory throughout execution, with threads repeatedly accessing the same 500-key dataset (500 keys × 3 fields). Results reflect peak throughput under ideal conditions with no page faults, I/O, or memory pressure. Real-world performance may vary significantly with:

- **Large datasets** causing cache misses
- **Non-uniform access patterns** with temporal/spatial locality variations
- **Competing processes** sharing CPU and L3 cache
- **Real workloads** with unpredictable key distributions and field cardinality

**Benchmarking Methodology**:
- **Timing**: `steady_clock` 
- **Pre-generated keys**: All key strings allocated before timing begins
- **Start gates**: Threads synchronize via atomic flag before measurement starts
- **Strided loops**: Exact operation count distributed evenly across threads
- **Checksums**: Consumed results prevent compiler dead-code elimination and validate correctness
- **No warmup contamination**: Fresh DB instance per test configuration

Benchmark suite designed to evaluate multi-threading strategies across 4 workload patterns. Trie-based prefix search adapted from standard implementations; 16-shard architecture with per-key locks and Trie sharding are custom optimizations.
