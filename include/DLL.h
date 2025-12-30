#pragma once

#include <optional>
#include <string>

/**
 * @brief Node representing a single version entry in the doubly linked list.
 * 
 * Each node stores a versioned record with a timestamp and optional TTL.
 * Nodes are arranged chronologically in the DLL (newest near dummyRight sentinel).
 */
struct Node {
    std::string record;        ///< The stored data value
    int timestamp;             ///< Insertion time (used for versioning and TTL calculation)
    std::optional<int> ttl;    ///< Time-to-live in seconds (nullopt means no expiration)
    Node* prev;                ///< Previous node in the list (older version)
    Node* next;                ///< Next node in the list (newer version)

    /**
     * @brief Constructs a new node with the given data.
     * @param record The data value to store
     * @param timestamp The insertion timestamp
     * @param ttl Optional time-to-live in seconds
     */
    Node(const std::string& record, int timestamp, std::optional<int> ttl);
    
    /**
     * @brief Updates this node's data in place.
     * @param record New data value
     * @param timestamp New timestamp
     * @param ttl New TTL value
     */
    void update(const std::string& record, int timestamp, std::optional<int> ttl);
};

/**
 * @brief Doubly linked list with dummy sentinels for managing chronological record versions.
 * 
 * Structure: [dummyLeft] <-> [oldest] <-> ... <-> [newest] <-> [dummyRight]
 * - New nodes are inserted at the end (before dummyRight)
 * - Oldest nodes are near dummyLeft
 * - Supports O(1) insertion at end and O(1) deletion of any node
 */
class DLL {
public:
    /**
     * @brief Constructs an empty DLL with dummy sentinels.
     */
    DLL();
    
    /**
     * @brief Destroys the DLL and frees all nodes including sentinels.
     */
    ~DLL();

    DLL(const DLL&) = delete;
    DLL& operator=(const DLL&) = delete;

    /**
     * @brief Inserts a node at the end of the list (before dummyRight).
     * @param newNode Pointer to the node to insert (caller owns memory)
     * @note This operation is O(1)
     */
    void insertAtEnd(Node* newNode);
    
    /**
     * @brief Deletes a node from the list and frees its memory.
     * @param nodeToDelete Pointer to the node to delete
     * @note This operation is O(1). Do not use the pointer after calling this.
     */
    void deleteNode(Node* nodeToDelete);

    /**
     * @brief Returns the most recently inserted node (newest version).
     * @return Pointer to the latest node, or dummyRight if list is empty
     */
    Node* getLatest() const;
    
    /**
     * @brief Returns the oldest node in the list.
     * @return Pointer to the oldest node, or dummyLeft if list is empty
     */
    Node* getOldest() const;
    
    /**
     * @brief Checks if a node is a dummy sentinel.
     * @param node Pointer to check
     * @return true if node is dummyLeft or dummyRight
     */
    bool isDummy(const Node* node) const;

    /**
     * @brief Returns the number of data nodes (excludes dummy sentinels).
     * @return Count of actual data nodes
     */
    int getLength() const {return length;}

private:
    Node* dummyRight;  ///< Right sentinel (marks end of list)
    Node* dummyLeft;   ///< Left sentinel (marks beginning of list)
    int length;        ///< Count of data nodes (excludes sentinels)
};
