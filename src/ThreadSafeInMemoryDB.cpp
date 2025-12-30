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

        size_t shardIndex = shardForKey(key);
        Shard& shard = shards[shardIndex];
        std::unique_lock<std::shared_mutex> shardLock(shard.shardMutex);

        auto keyEntry = shard.keys.find(key);
        if (keyEntry == shard.keys.end()) continue;

        std::shared_ptr<KeyBucket> keyBucket = keyEntry->second;
        std::unique_lock<std::shared_mutex> keyLock(keyBucket->keyMutex);

        auto fieldMapEntry = keyBucket->fields.find(field);
        if (fieldMapEntry == keyBucket->fields.end()) continue;

        auto& fieldEntry = fieldMapEntry->second;
        auto nodeMapEntry = fieldEntry.node_map.find(timestamp);
        if (nodeMapEntry == fieldEntry.node_map.end()) continue;

        Node* nodeToDelete = nodeMapEntry->second;
        if (nodeToDelete && fieldEntry.dll) {
            fieldEntry.dll->deleteNode(nodeToDelete);
            fieldEntry.node_map.erase(nodeMapEntry);

            if (fieldEntry.dll->getLength() == 0) {
                keyBucket->fields.erase(fieldMapEntry);
                if (keyBucket->fields.empty()) {
                    keyLock.unlock();
                    shard.keys.erase(keyEntry);

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

bool ThreadSafeInMemoryDB::newInsert(const Record& record) {
    if (record.key.empty() || record.field.empty() || record.value.empty() || record.timestamp < 0) {
        return false;
    }

    size_t shardIndex = shardForKey(record.key);
    Shard& shard = shards[shardIndex];
    
    // Fast path: Try to find existing key with shared lock
    std::shared_ptr<KeyBucket> keyBucket;
    bool keyExists = false;
    {
        std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);
        auto keyEntry = shard.keys.find(record.key);
        if (keyEntry != shard.keys.end()) {
            keyBucket = keyEntry->second;
            keyExists = true;
        }
    }
    
    // Slow path: Key doesn't exist
    // Lock shard exclusively 
    bool keyWasJustInserted = false;
    if (!keyExists) {
        std::unique_lock<std::shared_mutex> shardLock(shard.shardMutex);
        auto [keyEntry, inserted] = shard.keys.emplace(record.key, std::make_shared<KeyBucket>());
        keyBucket = keyEntry->second;
        keyWasJustInserted = inserted;
    }

    std::unique_lock<std::shared_mutex> keyLock(keyBucket->keyMutex); // Lock key for writing

    FieldEntry& fieldEntry = keyBucket->fields[record.field];
    if (!fieldEntry.dll) {
        fieldEntry.dll = std::make_unique<DLL>();
    }

    if (fieldEntry.node_map.count(record.timestamp) > 0) {
        return false;
    }

    Node* newNode = new Node(record.value, record.timestamp, record.ttl);
    fieldEntry.dll->insertAtEnd(newNode);
    fieldEntry.node_map[record.timestamp] = newNode;

    // LRU eviction: if DLL exceeds max versions, remove oldest node
    if (static_cast<size_t>(fieldEntry.dll->getLength()) > MAX_VERSIONS_PER_FIELD) {
        Node* oldestNode = fieldEntry.dll->getOldest();
        if (oldestNode && !fieldEntry.dll->isDummy(oldestNode)) {
            fieldEntry.node_map.erase(oldestNode->timestamp);
            fieldEntry.dll->deleteNode(oldestNode);
        }
    }

    // Insert into Trie only if this is a newly inserted key (not just a new field value)
    if (keyWasJustInserted) {
        size_t trieShard = shardForTrieKey(record.key);
        std::unique_lock<std::shared_mutex> trieLock(trieShards[trieShard].trieMutex);
        trieShards[trieShard].trie.insert(record.key);
    }

    keyLock.unlock();

    if (record.ttl.has_value()) {
        std::lock_guard<std::mutex> ttlLock(ttlMutex);
        minHeapTTLData.emplace(record.timestamp + record.ttl.value(), record.key, record.field, record.timestamp);
        cleanupCV.notify_all();
    }

    return true;
}

void ThreadSafeInMemoryDB::collectFieldsForKey(
    const std::shared_ptr<KeyBucket>& keyBucket,
    const std::string& key,
    int timestamp,
    std::vector<std::tuple<std::string, std::string, std::string>>& results) {
    
    for (const auto& [fieldName, fieldEntry] : keyBucket->fields) {
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
        size_t shardIndex = shardForKey(key);
        Shard& shard = shards[shardIndex];
        std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);

        auto keyEntry = shard.keys.find(key);
        if (keyEntry == shard.keys.end()) continue;

        std::shared_ptr<KeyBucket> keyBucket = keyEntry->second;
        std::shared_lock<std::shared_mutex> keyLock(keyBucket->keyMutex);

        collectFieldsForKey(keyBucket, key, timestamp, results);
    }

    std::sort(results.begin(), results.end());
    return results;
}

std::optional<std::string> ThreadSafeInMemoryDB::getValue(
    const std::string& key,
    const std::string& field,
    int timestamp) {

    size_t shardIndex = shardForKey(key);
    Shard& shard = shards[shardIndex];
    std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);

    auto keyEntry = shard.keys.find(key);
    if (keyEntry == shard.keys.end()) return std::nullopt;

    std::shared_ptr<KeyBucket> keyBucket = keyEntry->second;
    std::shared_lock<std::shared_mutex> keyLock(keyBucket->keyMutex);

    auto fieldMapEntry = keyBucket->fields.find(field);
    if (fieldMapEntry == keyBucket->fields.end()) return std::nullopt;

    const auto& fieldEntry = fieldMapEntry->second;
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

size_t ThreadSafeInMemoryDB::getVersionCount(const std::string& key, const std::string& field) const {
    size_t shardIndex = shardForKey(key);
    const Shard& shard = shards[shardIndex];
    std::shared_lock<std::shared_mutex> shardLock(shard.shardMutex);

    auto keyEntry = shard.keys.find(key);
    if (keyEntry == shard.keys.end()) return 0;

    std::shared_ptr<KeyBucket> keyBucket = keyEntry->second;
    std::shared_lock<std::shared_mutex> keyLock(keyBucket->keyMutex);

    auto fieldMapEntry = keyBucket->fields.find(field);
    if (fieldMapEntry == keyBucket->fields.end()) return 0;

    const auto& fieldEntry = fieldMapEntry->second;
    if (!fieldEntry.dll) return 0;

    return static_cast<size_t>(fieldEntry.dll->getLength());
}
