#pragma once

#include <optional>
#include <string>
#include <utility>

/**
 * @brief Represents a single data piece in the database.
 * 
 * - key: Primary key identifying the record
 * - field: Field name within the key (enables multiple fields per key)
 * - value: The actual data stored
 * - timestamp: Version identifier (must be unique per field)
 * - ttl: Optional time-to-live in seconds (nullopt = no expiration)
 * 
 * Uses builder pattern for ergonomic construction without TTL.
 * 
 * @example
 * // Basic record without TTL
 * Record rec1 = Record::create("user123", "name", "Alice", 100);
 * 
 * // Record with TTL
 * Record rec2 = Record::create("session", "token", "abc123", 200)
 *                   .withTTL(3600);
 */
struct Record {
    std::string key;                ///< Primary key
    std::string field;              ///< Field name within the key
    std::string value;              ///< The stored data (renamed from "record" for clarity)
    int timestamp;                  ///< Version timestamp (must be unique per field)
    std::optional<int> ttl;         ///< Optional time-to-live in seconds
    
    /**
     * @brief Factory method to create a Record without TTL.
     * 
     * Uses move semantics to avoid unnecessary string copies.
     * 
     * @param key Primary key
     * @param field Field name
     * @param value Data value
     * @param timestamp Version timestamp
     * @return Record with ttl = nullopt
     */
    static Record create(std::string key, std::string field, 
                        std::string value, int timestamp) {
        return Record{std::move(key), std::move(field), std::move(value), timestamp, std::nullopt};
    }
    
    /**
     * @brief Builder method to add TTL to an existing record.
     * 
     * Enables fluent/chained API:
     * Record r = Record::create(...).withTTL(3600);
     * 
     * @param ttl_value Time-to-live in seconds
     * @return Reference to this Record (for method chaining)
     */
    Record& withTTL(int ttl_value) {
        ttl = ttl_value;
        return *this;
    }
};
