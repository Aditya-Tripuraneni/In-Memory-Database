#pragma once

#include <array>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>

/// Supported character set: a-z (26) + A-Z (26) + 0-9 (10) + '-' (1) = 63 characters
inline constexpr int ASCII_SIZE = 63;

/**
 * @brief Prefix tree (Trie) for efficient string storage and prefix-based retrieval.
 * 
 * Supports alphanumeric characters (a-z, A-Z, 0-9) and hyphen (-).
 * Each node stores all keys that pass through it for O(1) prefix query results.
 * 
 * Time Complexity:
 * - Insert: O(m) where m = key length
 * - Search: O(m)
 * - Remove: O(m)
 * - Prefix query: O(m + k) where k = number of matching keys
 */
class Trie {
private:
    /**
     * @brief Internal node structure for the Trie.
     * 
     * Each node stores:
     * - children: array of 63 possible next characters
     * - isEndOfWord: marks if this node completes a valid key
     * - keys: all complete keys that pass through this node (for fast prefix retrieval)
     */
    struct TrieNode {
        std::array<std::unique_ptr<TrieNode>, ASCII_SIZE> children{};
        bool isEndOfWord = false;
        std::unordered_set<std::string> keys;  ///< All keys with this prefix
    };

    std::unique_ptr<TrieNode> root;

    /**
     * @brief Maps a character to its index in the children array.
     * @param c Character to map (a-z, A-Z, 0-9, or '-')
     * @return Index [0, 62] or -1 if character is unsupported
     */
    int charToIndex(char c) const;
    
    /**
     * @brief Recursively deletes a key from the Trie and prunes empty nodes.
     * @param node Current node in the traversal
     * @param key Key to delete
     * @param index Current position in the key
     * @return true if this node should be deleted (has no children and no keys)
     */
    bool helperDelete(TrieNode* node, const std::string& key, int index);

public:
    /**
     * @brief Constructs an empty Trie with a root node.
     */
    Trie();

    /**
     * @brief Inserts a word into the Trie.
     * @param word String to insert (must contain only a-z, A-Z, 0-9, or '-')
     * @note Invalid characters cause the insertion to fail silently
     * @note Each node along the path stores this word in its keys set
     */
    void insert(const std::string& word);
    
    /**
     * @brief Checks if an exact word exists in the Trie.
     * @param word Word to search for
     * @return true if the word exists as a complete entry
     */
    bool search(const std::string& word) const;
    
    /**
     * @brief Removes a word from the Trie.
     * @param word Word to remove
     * @return true if the word was found and removed
     * @note Prunes nodes that become empty after deletion
     */
    bool remove(const std::string& word);
    
    /**
     * @brief Checks if a prefix exists in the Trie.
     * @param prefix Prefix to check
     * @return true if at least one word starts with this prefix
     */
    bool isPrefix(const std::string& prefix) const;
    
    /**
     * @brief Retrieves all words that start with the given prefix.
     * @param prefix Prefix to search for
     * @return List of all matching words (empty if prefix not found)
     * @note Returns O(1) result by accessing the keys set at the prefix node
     */
    std::list<std::string> getWordsWithPrefix(const std::string& prefix) const;
};
