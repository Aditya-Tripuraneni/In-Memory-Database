#include "ThreadSafeInMemoryDB.h"
#include <algorithm>
#include <chrono>

bool ThreadSafeInMemoryDB::MinHeapComparator::operator()(
    const ThreadSafeInMemoryDB::ExpiryEntry& a,
    const ThreadSafeInMemoryDB::ExpiryEntry& b) const {
    return std::get<0>(a) > std::get<0>(b);
}

ThreadSafeInMemoryDB::ThreadSafeInMemoryDB() 
    : ThreadSafeInMemoryDB(true) {}

ThreadSafeInMemoryDB::ThreadSafeInMemoryDB(bool enableBackground)
    : shards(SHARD_COUNT), trieShards(SHARD_COUNT), backgroundEnabled(enableBackground) {
    if (backgroundEnabled) {
        cleanupRunning = true;
        cleanupThread = std::thread(&ThreadSafeInMemoryDB::backgroundCleanupWorker, this);
    }
}

ThreadSafeInMemoryDB::~ThreadSafeInMemoryDB() {
    if (backgroundEnabled) {
        cleanupRunning = false;
        cleanupCV.notify_all();
        if (cleanupThread.joinable()) {
            cleanupThread.join();
        }
    }
}

void ThreadSafeInMemoryDB::backgroundCleanupWorker() {
    while (cleanupRunning) {
        auto waitDuration = std::chrono::milliseconds(500);
        
        {
            std::lock_guard<std::mutex> ttlLock(ttlMutex);
            if (!minHeapTTLData.empty()) {
                int nextExpiry = std::get<0>(minHeapTTLData.top());
                int currentTime = static_cast<int>(std::time(nullptr));
                int timeUntilExpiry = nextExpiry - currentTime;
                
                if (timeUntilExpiry > 0) {
                    waitDuration = std::chrono::milliseconds(
                        std::min(500, timeUntilExpiry * 1000)
                    );
                } else {
                    waitDuration = std::chrono::milliseconds(0);
                }
            }
        }
        
        {
            std::unique_lock<std::mutex> cvLock(cleanupCVMutex);
            cleanupCV.wait_for(cvLock, waitDuration, [&]{
                return !cleanupRunning;
            });
        }

        if (!cleanupRunning) break;

        int currentTime = static_cast<int>(std::time(nullptr));
        cleanExpiredDataLocked(currentTime);
    }
}

size_t ThreadSafeInMemoryDB::shardForKey(const std::string& key) const {
    return std::hash<std::string>{}(key) % SHARD_COUNT;
}

size_t ThreadSafeInMemoryDB::shardForTrieKey(const std::string& key) const {
    return std::hash<std::string>{}(key) % SHARD_COUNT;
}

void ThreadSafeInMemoryDB::cleanExpiredDataLocked(int currentTime) {
    std::lock_guard<std::mutex> ttlLock(ttlMutex);

    while (!minHeapTTLData.empty() && std::get<0>(minHeapTTLData.top()) <= currentTime) {
        auto entry = minHeapTTLData.top();
        minHeapTTLData.pop();

        const std::string& key = std::get<1>(entry);
        const std::string& field = std::get<2>(entry);
        int timestamp = std::get<3>(entry);

        size_t shardIdx = shardForKey(key);
        Shard& shard = shards[shardIdx];
        std::unique_lock<std::shared_mutex> shardLock(shard.shardMutex);

        auto keyIt = shard.keys.find(key);
        if (keyIt == shard.keys.end()) continue;

        std::shared_ptr<KeyBucket> bucketPtr = keyIt->second;
        std::unique_lock<std::shared_mutex> keyLock(bucketPtr->keyMutex);

        auto fieldIt = bucketPtr->fields.find(field);
        if (fieldIt == bucketPtr->fields.end()) continue;

        auto& fieldEntry = fieldIt->second;
        auto nodeIt = fieldEntry.node_map.find(timestamp);
        if (nodeIt == fieldEntry.node_map.end()) continue;

        Node* nodeToDelete = nodeIt->second;
        if (nodeToDelete && fieldEntry.dll) {
            fieldEntry.dll->deleteNode(nodeToDelete);
            fieldEntry.node_map.erase(nodeIt);

            if (fieldEntry.dll->getLength() == 0) {
                bucketPtr->fields.erase(fieldIt);
                if (bucketPtr->fields.empty()) {
                    keyLock.unlock();
                    shard.keys.erase(keyIt);

                    size_t trieShard = shardForTrieKey(key);
                    std::unique_lock<std::shared_mutex> trieLock(trieShards[trieShard].trieMutex);
                    trieShards[trieShard].trie.remove(key);
                }
            }
        }
    }
}

bool ThreadSafeInMemoryDB::isExpired(int currentTimeStamp, Node* node) const {
    if (!node || !node->ttl.has_value()) return false;
    return currentTimeStamp >= node->timestamp + node->ttl.value();
}

bool ThreadSafeInMemoryDB::newInsert(const std::string& key,
                                     const std::string& field,
                                     const std::string& record,
                                     int timestamp,
                                     std::optional<int> ttl) {
    if (key.empty() || field.empty() || record.empty() || timestamp < 0) {
        return false;
    }

    size_t shardIdx = shardForKey(key);
    Shard& shard = shards[shardIdx];
    std::unique_lock<std::shared_mutex> shardLock(shard.shardMutex);

    auto [keyIt, inserted] = shard.keys.emplace(key, std::make_shared<KeyBucket>());
    std::shared_ptr<KeyBucket> bucketPtr = keyIt->second;

    std::unique_lock<std::shared_mutex> keyLock(bucketPtr->keyMutex);

    FieldEntry& fieldEntry = bucketPtr->fields[field];
    if (!fieldEntry.dll) {
        fieldEntry.dll = std::make_unique<DLL>();
    }

    if (fieldEntry.node_map.count(timestamp) > 0) {
        return false;
    }

    Node* newNode = new Node(record, timestamp, ttl);
    fieldEntry.dll->insertAtEnd(newNode);
    fieldEntry.node_map[timestamp] = newNode;

    if (inserted) {
        size_t trieShard = shardForTrieKey(key);
        std::unique_lock<std::shared_mutex> trieLock(trieShards[trieShard].trieMutex);
        trieShards[trieShard].trie.insert(key);
    }

    keyLock.unlock();
    shardLock.unlock();

    if (ttl.has_value()) {
        std::lock_guard<std::mutex> ttlLock(ttlMutex);
        minHeapTTLData.emplace(timestamp + ttl.value(), key, field, timestamp);
        cleanupCV.notify_all();
    }

    return true;
}

void ThreadSafeInMemoryDB::collectFieldsForKey(
    const std::shared_ptr<KeyBucket>& bucketPtr,
    const std::string& key,
    int timestamp,
    std::vector<std::tuple<std::string, std::string, std::string>>& results) {
    
    for (const auto& [fieldName, fieldEntry] : bucketPtr->fields) {
        if (!fieldEntry.dll) continue;

        Node* latestNode = fieldEntry.dll->getLatest();
        if (!latestNode || fieldEntry.dll->isDummy(latestNode)) continue;
        if (isExpired(timestamp, latestNode)) continue;

        results.emplace_back(key, fieldName, latestNode->record);
    }
}

std::vector<std::tuple<std::string, std::string, std::string>>
ThreadSafeInMemoryDB::scanByPrefix(const std::string& prefix, int timestamp) {
    std::vector<std::tuple<std::string, std::string, std::string>> results;
    
    if (prefix.empty()) return results;

    std::list<std::string> candidateKeys;
    for (size_t i = 0; i < SHARD_COUNT; ++i) {
        std::shared_lock<std::shared_mutex> trieLock(trieShards[i].trieMutex);
        auto shardResults = trieShards[i].trie.getWordsWithPrefix(prefix);
        candidateKeys.insert(candidateKeys.end(), shardResults.begin(), shardResults.end());
    }

    for (const auto& key : candidateKeys) {
        size_t shardIdx = shardForKey(key);
        Shard& shard = shards[shardIdx];
        std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);

        auto keyIt = shard.keys.find(key);
        if (keyIt == shard.keys.end()) continue;

        std::shared_ptr<KeyBucket> bucketPtr = keyIt->second;
        std::shared_lock<std::shared_mutex> keyLock(bucketPtr->keyMutex);

        collectFieldsForKey(bucketPtr, key, timestamp, results);
    }

    std::sort(results.begin(), results.end());
    return results;
}

std::optional<std::string> ThreadSafeInMemoryDB::getValue(
    const std::string& key,
    const std::string& field,
    int timestamp) {

    size_t shardIdx = shardForKey(key);
    Shard& shard = shards[shardIdx];
    std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);

    auto keyIt = shard.keys.find(key);
    if (keyIt == shard.keys.end()) return std::nullopt;

    std::shared_ptr<KeyBucket> bucketPtr = keyIt->second;
    std::shared_lock<std::shared_mutex> keyLock(bucketPtr->keyMutex);

    auto fieldIt = bucketPtr->fields.find(field);
    if (fieldIt == bucketPtr->fields.end()) return std::nullopt;

    const auto& fieldEntry = fieldIt->second;
    if (!fieldEntry.dll) return std::nullopt;

    Node* latestNode = fieldEntry.dll->getLatest();
    if (!latestNode || fieldEntry.dll->isDummy(latestNode)) return std::nullopt;
    if (isExpired(timestamp, latestNode)) return std::nullopt;

    return latestNode->record;
}

size_t ThreadSafeInMemoryDB::getKeyCount() const {
    size_t total = 0;
    for (const auto& shard : shards) {
        std::shared_lock<std::shared_mutex> lock(shard.shardMutex);
        total += shard.keys.size();
    }
    return total;
}

size_t ThreadSafeInMemoryDB::getTTLQueueSize() const {
    std::lock_guard<std::mutex> lock(ttlMutex);
    return minHeapTTLData.size();
}
