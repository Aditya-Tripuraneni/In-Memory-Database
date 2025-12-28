#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
#include <shared_mutex>
#include <mutex>
#include <thread>
#include <atomic>
#include <condition_variable>

#include "DLL.h"
#include "TRIE.h"

/**
 * Thread-safe In-Memory Database with reader-writer locking.
 * 
 * Locking Strategy (sharded):
 * - N shards (default 16); each shard has its own mutex and key map
 * - Per-key mutex inside each shard to isolate writers to the same key
 * - Separate mutex for TTL heap (async cleanup)
 * - Trie protected by its own mutex for inserts/removals
 *
 * Concurrency:
 * - Reads of different keys run in parallel (shared locks per shard/key)
 * - Writes collide only on the same key or shard bucket creation
 */
class ThreadSafeInMemoryDB {
private:
    static constexpr size_t SHARD_COUNT = 16;

    struct FieldEntry {
        std::unique_ptr<DLL> dll;
        std::unordered_map<int, Node*> node_map;
    };

    struct KeyBucket {
        std::unordered_map<std::string, FieldEntry> fields;
        mutable std::shared_mutex keyMutex;
    };

    struct Shard {
        std::unordered_map<std::string, std::shared_ptr<KeyBucket>> keys;
        mutable std::shared_mutex shardMutex;
    };

    using FieldMap = std::unordered_map<std::string, FieldEntry>;
    using ExpiryEntry = std::tuple<int, std::string, std::string, int>;

    struct MinHeapComparator {
        bool operator()(const ExpiryEntry& a, const ExpiryEntry& b) const;
    };

    // Sharded data
    std::vector<Shard> shards;

    // Trie protected separately
    mutable std::shared_mutex trieMutex;
    Trie keyTrie;
    
    // TTL heap with its own lock
    mutable std::mutex ttlMutex;
    std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, MinHeapComparator> minHeapTTLData;

    // Background cleanup (optional)
    bool backgroundEnabled;
    std::atomic<bool> cleanupRunning{false};
    std::thread cleanupThread;
    std::mutex cleanupCVMutex;
    std::condition_variable cleanupCV;
    
    size_t shardForKey(const std::string& key) const;
    void cleanExpiredDataLocked(int currentTime);
    bool isExpired(int currentTimeStamp, Node* node) const;
    void backgroundCleanupWorker();

public:
    ThreadSafeInMemoryDB();
    
    explicit ThreadSafeInMemoryDB(bool enableBackground);
    
    ~ThreadSafeInMemoryDB();

    ThreadSafeInMemoryDB(const ThreadSafeInMemoryDB&) = delete;
    ThreadSafeInMemoryDB& operator=(const ThreadSafeInMemoryDB&) = delete;

    bool newInsert(const std::string& key,
                   const std::string& field,
                   const std::string& record,
                   int timestamp,
                   std::optional<int> ttl = std::nullopt);

    std::vector<std::tuple<std::string, std::string, std::string>>
    scanByPrefix(const std::string& prefix, int timestamp);

    std::optional<std::string> getValue(const std::string& key,
                                        const std::string& field,
                                        int timestamp);

    size_t getKeyCount() const;
    size_t getTTLQueueSize() const;
};
