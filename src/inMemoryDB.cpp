#include "InMemoryDB.h"

#include <algorithm>

bool InMemoryDB::MinHeapComparator::operator()(const InMemoryDB::ExpiryEntry& a,
                                               const InMemoryDB::ExpiryEntry& b) const {
    return std::get<0>(a) > std::get<0>(b);
}

void InMemoryDB::cleanExpiredData(int currentTime) {
    while (!minHeapTTLData.empty() && std::get<0>(minHeapTTLData.top()) <= currentTime) {
        auto entry = minHeapTTLData.top();
        minHeapTTLData.pop();

        const std::string& key = std::get<1>(entry);
        const std::string& field = std::get<2>(entry);

        auto keyIt = db.find(key);
        if (keyIt == db.end()) {
            continue;
        }

        auto& fieldmap = keyIt->second;

        auto fieldIt = fieldmap.find(field);
        if (fieldIt == fieldmap.end()) {
            continue;
        }

        int timestamp = std::get<3>(entry);

        auto& fieldEntry = fieldIt->second;

        auto nodeIt = fieldEntry.node_map.find(timestamp);

        if (nodeIt == fieldEntry.node_map.end()) {
            continue;
        }

        Node* nodeToDelete = nodeIt->second;

        if (nodeToDelete != nullptr && fieldEntry.dll) {
            fieldEntry.dll->deleteNode(nodeToDelete);
            fieldEntry.node_map.erase(nodeIt);

            if (fieldEntry.dll->getLength() == 0) {
                keyIt->second.erase(fieldIt);

                if (keyIt->second.empty()) {
                    db.erase(keyIt);
                    keyTrie.remove(key); // prevent stale keys in TRIE
                }
            }
        }
    }
}

bool InMemoryDB::isExpired(int currentTimeStamp, Node* node) {
    if (!node || !node->ttl.has_value()) {
        return false;
    }
    return currentTimeStamp >= node->timestamp + node->ttl.value();
}

bool InMemoryDB::newInsert(const Record& record) {
    if (record.key.empty() || record.field.empty() || record.value.empty() || record.timestamp < 0) {
        return false;
    }

    cleanExpiredData(record.timestamp);

    auto& fieldMap = db[record.key];
    FieldEntry& fieldEntry = fieldMap[record.field];
    if (!fieldEntry.dll) {
        fieldEntry.dll = std::make_unique<DLL>();
        if (!fieldEntry.dll) {
            return false;
        }
    }

    if (fieldEntry.node_map.find(record.timestamp) != fieldEntry.node_map.end()) {
        return false;
    }

    Node* newNode = new Node(record.value, record.timestamp, record.ttl);
    if (!newNode) {
        return false;
    }

    fieldEntry.dll->insertAtEnd(newNode);
    fieldEntry.node_map[record.timestamp] = newNode;

    keyTrie.insert(record.key);

    if (record.ttl.has_value()) {
        int expiryTime = record.timestamp + record.ttl.value();
        minHeapTTLData.emplace(expiryTime, record.key, record.field, record.timestamp);
    }

    return true;
}

std::vector<std::tuple<std::string, std::string, std::string>>
InMemoryDB::scanByPrefix(const std::string& prefix, int timestamp) {
    cleanExpiredData(timestamp);

    std::vector<std::tuple<std::string, std::string, std::string>> results;
    if (prefix.empty()) {
        return results;
    }

    auto candidateKeys = keyTrie.getWordsWithPrefix(prefix);

    for (const auto& key : candidateKeys) {
        auto keyIt = db.find(key);
        if (keyIt == db.end()) {
            continue;
        }

        for (const auto& [fieldName, fieldEntry] : keyIt->second) {
            if (!fieldEntry.dll) {
                continue;
            }

            Node* latestNode = fieldEntry.dll->getLatest();
            if (!latestNode || fieldEntry.dll->isDummy(latestNode)) {
                continue;
            }

            if (isExpired(timestamp, latestNode)) {
                continue;
            }

            results.emplace_back(key, fieldName, latestNode->record);
        }
    }

    std::sort(results.begin(), results.end(), [](const auto& lhs, const auto& rhs) {
        if (std::get<0>(lhs) != std::get<0>(rhs)) {
            return std::get<0>(lhs) < std::get<0>(rhs);
        }
        if (std::get<1>(lhs) != std::get<1>(rhs)) {
            return std::get<1>(lhs) < std::get<1>(rhs);
        }
        return std::get<2>(lhs) < std::get<2>(rhs);
    });

    return results;
}

std::optional<std::string> InMemoryDB::getValue(
    const std::string& key,
    const std::string& field,
    int timestamp) {
    
    cleanExpiredData(timestamp);
    
    auto keyIt = db.find(key);
    if (keyIt == db.end()) {
        return std::nullopt;
    }
    
    auto& fields = keyIt->second;
    auto fieldIt = fields.find(field);
    if (fieldIt == fields.end()) {
        return std::nullopt;
    }
    
    auto& fieldEntry = fieldIt->second;
    if (!fieldEntry.dll) {
        return std::nullopt;
    }
    
    Node* latestNode = fieldEntry.dll->getLatest();
    if (!latestNode || fieldEntry.dll->isDummy(latestNode)) {
        return std::nullopt;
    }
    
    if (isExpired(timestamp, latestNode)) {
        return std::nullopt;
    }
    
    return latestNode->record;
}