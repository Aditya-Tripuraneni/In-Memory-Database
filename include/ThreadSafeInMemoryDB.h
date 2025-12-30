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
#include "Record.h"

/**
 * Thread-safe In-Memory Database with reader-writer locking.
 * 
 * Locking Strategy (sharded):
 * - N shards (default 16); each shard has its own mutex and key map
 * - Per-key mutex inside each shard to isolate writers to the same key
 * - N Trie shards (default 16); each with its own mutex for parallel key insertions
 * - TTL heap protected by its own mutex (async cleanup)
 *
 * Concurrency:
 * - Reads of different keys run in parallel (shared locks per shard/key)
 * - Writes to different keys run in parallel (different key shards)
 * - Inserts to different Trie shards run in parallel (hash-based sharding)
 * - Prefix scans query all Trie shards in parallel via shared locks
 */
class ThreadSafeInMemoryDB {
private:
    static constexpr size_t SHARD_COUNT = 16;           ///< Number of hash shards for parallelism
    static constexpr size_t MAX_VERSIONS_PER_FIELD = 100;  ///< LRU eviction threshold per field

    /**
     * @brief Stores all versions of a single field.
     * Identical to InMemoryDB::FieldEntry.
     */
    struct FieldEntry {
        std::unique_ptr<DLL> dll;
        std::unordered_map<int, Node*> node_map;
    };

    /**
     * @brief Represents a single key with all its fields.
     * Each key has its own shared_mutex for fine-grained locking.
     */
    struct KeyBucket {
        std::unordered_map<std::string, FieldEntry> fields;
        mutable std::shared_mutex keyMutex;  ///< Protects fields map
    };

    /**
     * @brief One shard in the sharded hash table.
     * Contains a subset of keys and a shard-level mutex.
     */
    struct Shard {
        std::unordered_map<std::string, std::shared_ptr<KeyBucket>> keys;
        mutable std::shared_mutex shardMutex;  ///< Protects keys map
    };

    using FieldMap = std::unordered_map<std::string, FieldEntry>;
    
    /// Tuple: (expiryTime, key, field, timestamp) for TTL tracking
    using ExpiryEntry = std::tuple<int, std::string, std::string, int>;

    /**
     * @brief Min-heap comparator for TTL priority queue.
     * Sorts by expiry time (earliest expiration at top).
     */
    struct MinHeapComparator {
        bool operator()(const ExpiryEntry& a, const ExpiryEntry& b) const;
    };

    std::vector<Shard> shards;  ///< 16 sharded hash tables for parallel key access

    /**
     * @brief One Trie shard with its own mutex.
     * Sharding enables parallel key insertions into different Tries.
     */
    struct TrieShard {
        mutable std::shared_mutex trieMutex;
        Trie trie;
    };
    std::vector<TrieShard> trieShards;  ///< 16 sharded Tries for parallel prefix ops
    
    mutable std::mutex ttlMutex;  ///< Protects TTL heap
    std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, MinHeapComparator> minHeapTTLData;  ///< TTL expiration queue

    bool backgroundEnabled;                  ///< Whether background cleanup thread is active
    std::atomic<bool> cleanupRunning{false}; ///< Signals cleanup thread to stop
    std::thread cleanupThread;               ///< Background thread for async TTL cleanup
    std::mutex cleanupCVMutex;               ///< Mutex for cleanupCV
    std::condition_variable cleanupCV;       ///< Signals cleanup thread on TTL changes       
    
    /**
     * @brief Computes the shard index for a key.
     * @param key Key to hash
     * @return Shard index [0, SHARD_COUNT)
     */
    size_t shardForKey(const std::string& key) const;
    
    /**
     * @brief Computes the Trie shard index for a key.
     * @param key Key to hash
     * @return Trie shard index [0, SHARD_COUNT)
     */
    size_t shardForTrieKey(const std::string& key) const;
    
    /**
     * @brief Removes expired records while holding ttlMutex.
     * @param currentTime Current timestamp
     * @note Acquires shard and key locks as needed
     */
    void cleanExpiredDataLocked(int currentTime);
    
    /**
     * @brief Checks if a node has expired.
     * @param currentTimeStamp Current time
     * @param node Node to check
     * @return true if node has TTL and is expired
     */
    bool isExpired(int currentTimeStamp, Node* node) const;
    
    /**
     * @brief Background worker thread for async TTL cleanup.
     * Runs until cleanupRunning becomes false.
     */
    void backgroundCleanupWorker();
    
    /**
     * @brief Helper to collect all fields for a key during prefix scan.
     * @param bucketPtr Pointer to the KeyBucket
     * @param key Key name
     * @param timestamp Current timestamp for expiration checks
     * @param results Output vector to append results
     */
    void collectFieldsForKey(const std::shared_ptr<KeyBucket>& bucketPtr,
                            const std::string& key,
                            int timestamp,
                            std::vector<std::tuple<std::string, std::string, std::string>>& results);

public:
    /**
     * @brief Constructs database with background cleanup enabled.
     */
    ThreadSafeInMemoryDB();
    
    /**
     * @brief Constructs database with optional background cleanup.
     * @param enableBackground If true, starts background TTL cleanup thread
     */
    explicit ThreadSafeInMemoryDB(bool enableBackground);
    
    /**
     * @brief Stops background thread (if enabled) and cleans up resources.
     */
    ~ThreadSafeInMemoryDB();

    ThreadSafeInMemoryDB(const ThreadSafeInMemoryDB&) = delete;
    ThreadSafeInMemoryDB& operator=(const ThreadSafeInMemoryDB&) = delete;

    /**
     * @brief Thread-safe insert with fast-path shared lock optimization.
     * 
     * Locking strategy:
     * 1. Try shared lock on shard to check if key exists (fast path)
     * 2. If key missing, upgrade to exclusive shard lock to create key
     * 3. Lock the specific key exclusively for field update
     * 4. LRU eviction if MAX_VERSIONS_PER_FIELD exceeded
     * 
     * @param record Record entity containing key, field, value, timestamp, and optional ttl
     * @return true if inserted successfully
     */
    bool newInsert(const Record& record);

    /**
     * @brief Thread-safe prefix scan across all Trie shards.
     * @param prefix Key prefix to search
     * @param timestamp Current timestamp for expiration checks
     * @return Sorted vector of (key, field, latest_record) tuples
     */
    std::vector<std::tuple<std::string, std::string, std::string>>
    scanByPrefix(const std::string& prefix, int timestamp);

    /**
     * @brief Thread-safe point lookup with shared locks.
     * @param key Primary key
     * @param field Field name
     * @param timestamp Current timestamp
     * @return Latest non-expired value, or nullopt if not found
     */
    std::optional<std::string> getValue(const std::string& key,
                                        const std::string& field,
                                        int timestamp);

    /**
     * @brief Returns total number of keys across all shards.
     * @return Sum of keys in all shards
     */
    size_t getKeyCount() const;
    
    /**
     * @brief Returns size of TTL expiration queue.
     * @return Number of pending TTL entries
     */
    size_t getTTLQueueSize() const;
    
    /**
     * @brief Returns version count for a specific (key, field) pair.
     * @param key Primary key
     * @param field Field name
     * @return Number of versions stored for this field
     */
    size_t getVersionCount(const std::string& key, const std::string& field) const;
    
    /**
     * @brief Pre-allocates space in each shard's key map.
     * @param capacityPerShard Expected keys per shard (optimization for bulk inserts)
     */
    void reserveKeys(size_t capacityPerShard) {
        for (auto& shard : shards) {
            std::unique_lock lock(shard.shardMutex);
            shard.keys.reserve(capacityPerShard);
        }
    }
};
