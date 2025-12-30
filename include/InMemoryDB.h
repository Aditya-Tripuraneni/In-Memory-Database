#pragma once

#include <memory>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "DLL.h"
#include "TRIE.h"
#include "Record.h"

/**
 * @brief Single-threaded in-memory key-value database with versioning and TTL support.
 * 
 * Features:
 * - Multi-versioned records per (key, field) pair
 * - Chronological version history via doubly linked list
 * - TTL-based expiration with min-heap for efficient cleanup
 * - Prefix-based key scanning via Trie
 * 
 * Data Structure:
 * - db: key -> field -> FieldEntry (DLL + timestamp map)
 * - keyTrie: Enables prefix queries
 * - minHeapTTLData: Priority queue for TTL expiration
 * 
 * @note This class is NOT thread-safe. Use ThreadSafeInMemoryDB for concurrent access.
 */
class InMemoryDB {
private:
    /**
     * @brief Stores all versions of a single field.
     * 
     * Contains:
     * - dll: Chronologically ordered versions (oldest to newest)
     * - node_map: timestamp -> Node* for O(1) lookup by timestamp
     */
    struct FieldEntry {
        std::unique_ptr<DLL> dll;                // owns the DLL tracking chronological versions
        std::unordered_map<int, Node*> node_map; // timestamp -> node (non-owning view into dll)
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

    std::unordered_map<std::string, FieldMap> db; ///< Primary storage: key -> field -> FieldEntry
    std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, MinHeapComparator> minHeapTTLData; ///< TTL expiration queue
    Trie keyTrie; ///< Prefix tree for all keys

    /**
     * @brief Removes all expired records up to currentTime.
     * @param currentTime Current timestamp for expiration check
     */
    void cleanExpiredData(int currentTime);
    
    /**
     * @brief Checks if a node has expired based on TTL.
     * @param currentTimeStamp Current time
     * @param node Node to check
     * @return true if node has TTL and currentTime >= timestamp + TTL
     */
    bool isExpired(int currentTimeStamp, Node* node);

public:
    InMemoryDB() = default;
    ~InMemoryDB() = default;

    /**
     * @brief Inserts a new versioned record.
     * @param record Record entity containing key, field, value, timestamp, and optional ttl
     * @return true if inserted successfully, false if timestamp already exists or params invalid
     * @note Triggers cleanup of expired data before insertion
     */
    bool newInsert(const Record& record);

    /**
     * @brief Retrieves all (key, field, value) tuples matching a prefix.
     * @param prefix Key prefix to search for
     * @param timestamp Current timestamp for expiration checks
     * @return Sorted vector of tuples (key, field, latest_record)
     * @note Only returns non-expired latest versions per field
     */
    std::vector<std::tuple<std::string, std::string, std::string>>
    scanByPrefix(const std::string& prefix, int timestamp);

    /**
     * @brief Retrieves the latest value for a specific (key, field).
     * @param key Primary key
     * @param field Field name
     * @param timestamp Current timestamp for expiration checks
     * @return Latest non-expired record value, or nullopt if not found/expired
     */
    std::optional<std::string> getValue(const std::string& key,
                                        const std::string& field,
                                        int timestamp);

    /**
     * @brief Pre-allocates space in the key hash map.
     * @param capacity Expected number of keys (optimization for bulk inserts)
     */
    void reserveKeys(size_t capacity) {
        db.reserve(capacity);
    }
};
