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

class InMemoryDB {
private:
    struct FieldEntry {
        std::unique_ptr<DLL> dll;                // owns the DLL tracking chronological versions
        std::unordered_map<int, Node*> node_map; // timestamp -> node (non-owning view into dll)
    };

    using FieldMap = std::unordered_map<std::string, FieldEntry>;
    using ExpiryEntry = std::tuple<int, std::string, std::string, int>; // (expiryTime, key, field, timestamp)

    struct MinHeapComparator {
        bool operator()(const ExpiryEntry& a, const ExpiryEntry& b) const;
    };

    std::unordered_map<std::string, FieldMap> db; // key -> field -> field entry
    std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, MinHeapComparator> minHeapTTLData;
    Trie keyTrie;

    void cleanExpiredData(int currentTime);
    bool isExpired(int currentTimeStamp, Node* node);

public:
    InMemoryDB() = default;
    ~InMemoryDB() = default;

    bool newInsert(const std::string& key,
                   const std::string& field,
                   const std::string& record,
                   int timestamp,
                   std::optional<int> ttl = std::nullopt);

    std::vector<std::tuple<std::string, std::string, std::string>>
    scanByPrefix(const std::string& prefix, int timestamp);
};
