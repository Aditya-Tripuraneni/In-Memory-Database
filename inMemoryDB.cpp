#include <unordered_map>
#include <string>
#include <queue>
#include <tuple>
#include <vector>
#include <memory>
#include <optional>
#include <algorithm>
#include "DLL.h"
#include "TRIE.h"

using ExpiryEntry = std::tuple<int, std::string, std::string, int>; // (expiryTime, key, field, timestamp)

struct FieldEntry {
    std::unique_ptr<DLL> dll;                // owns the DLL tracking chronological versions
    std::unordered_map<int, Node*> node_map; // timestamp -> node (non-owning view into dll)
};

// Orders expiry entries by soonest expiry timestamp so the heap acts as a min-heap.
struct MinHeapComparator {
    bool operator()(const ExpiryEntry& a, const ExpiryEntry& b) const {
        return std::get<0>(a) > std::get<0>(b);
    }
};

class InMemoryDB {
    private:
        using FieldMap = std::unordered_map<std::string, FieldEntry>;
        std::unordered_map<std::string, FieldMap> db; // key -> field -> field entry
        std::priority_queue<ExpiryEntry, std::vector<ExpiryEntry>, MinHeapComparator> minHeapTTLData;
        Trie keyTrie = Trie();

        // Removes every TTL entry that expired at or before currentTime, cascading into dll/node_map cleanup.
        void cleanExpiredData(int currentTime){
            while (!minHeapTTLData.empty() && std::get<0>(minHeapTTLData.top()) <= currentTime){
                auto entry = minHeapTTLData.top();
                minHeapTTLData.pop();

                const std::string& key = std::get<1>(entry);
                const std::string& field = std::get<2>(entry);

                auto keyIt = db.find(key);
                if (keyIt == db.end()){
                    continue;
                }

                auto& fieldmap = keyIt->second;

                auto fieldIt = fieldmap.find(field);
                if (fieldIt == fieldmap.end()){
                    continue;
                }

                int timestamp = std::get<3>(entry);

                auto& fieldEntry = fieldIt->second;

                auto nodeIt = fieldEntry.node_map.find(timestamp);

                if (nodeIt == fieldEntry.node_map.end()){
                    continue;
                }

                Node* nodeToDelete = nodeIt->second;

                if (nodeToDelete != nullptr && fieldEntry.dll){
                    fieldEntry.dll->deleteNode(nodeToDelete);
                    fieldEntry.node_map.erase(nodeIt);

                    if (fieldEntry.dll->getLength() == 0){
                        keyIt->second.erase(fieldIt);

                        if (keyIt->second.empty()){
                            db.erase(keyIt);
                            keyTrie.remove(key); // prevent stale keys in TRIE
                        }
                    }
                }
            }
        }

        // Helper to determine whether a node should be considered expired at the provided timestamp.
        bool isExpired(int currentTimeStamp, Node* node){
            if (!node || !node->ttl.has_value()){
                return false;
            }
            return currentTimeStamp >= node->timestamp + node->ttl.value();
        }
    
    public: 
        InMemoryDB() = default;
        ~InMemoryDB() = default;

    // Inserts a new version for key/field, recording optional TTL metadata when supplied.
    bool newInsert(const std::string& key,
                       const std::string& field,
                       const std::string& record,
                       int timestamp,
                       std::optional<int> ttl = std::nullopt){
            // Basic validation
            if (key.empty() || field.empty() || record.empty() || timestamp < 0) {
                return false;
            }

            cleanExpiredData(timestamp); // Clean expired data before insertion

            auto& fieldMap = db[key];
            FieldEntry& fieldEntry = fieldMap[field];
            if (!fieldEntry.dll){
                fieldEntry.dll = std::make_unique<DLL>();
                if (!fieldEntry.dll) return false; // allocation failed
            }

            // Check for duplicate timestamp
            if (fieldEntry.node_map.find(timestamp) != fieldEntry.node_map.end()) {
                return false;
            }

            Node* newNode = new Node(record, timestamp, ttl);
            if (!newNode) return false; // allocation failed

            fieldEntry.dll->insertAtEnd(newNode);
            fieldEntry.node_map[timestamp] = newNode;

            keyTrie.insert(key);

            if (ttl.has_value()){
                int expiryTime = timestamp + ttl.value();
                minHeapTTLData.emplace(expiryTime, key, field, timestamp);
            }

            return true;
        }
    

    // Returns (key, field, value) triples for the freshest unexpired records whose keys share the prefix.
    std::vector<std::tuple<std::string, std::string, std::string>> scanByPrefix(const std::string& prefix, int timestamp){
            cleanExpiredData(timestamp); // Clean expired data before scanning

            std::vector<std::tuple<std::string, std::string, std::string>> results;
            if (prefix.empty()){
                return results;
            }

            auto candidateKeys = keyTrie.getWordsWithPrefix(prefix);

            for (const auto& key : candidateKeys){
                auto keyIt = db.find(key);
                if (keyIt == db.end()){
                    continue;
                }

                for (const auto& [fieldName, fieldEntry] : keyIt->second){
                    if (!fieldEntry.dll){
                        continue;
                    }

                    Node* latestNode = fieldEntry.dll->getLatest();
                    if (!latestNode || fieldEntry.dll->isDummy(latestNode)){
                        continue;
                    }

                    if (isExpired(timestamp, latestNode)){
                        continue;
                    }

                    results.emplace_back(key, fieldName, latestNode->record);
                }
            }

            std::sort(results.begin(), results.end(), [](const auto& lhs, const auto& rhs){
                if (std::get<0>(lhs) != std::get<0>(rhs)){
                    return std::get<0>(lhs) < std::get<0>(rhs); // sort by key
                }
                if (std::get<1>(lhs) != std::get<1>(rhs)){
                    return std::get<1>(lhs) < std::get<1>(rhs); // sort by field
                }
                return std::get<2>(lhs) < std::get<2>(rhs); // sort by record
            });

            return results;
        }


};