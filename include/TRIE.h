#pragma once

#include <array>
#include <list>
#include <memory>
#include <string>
#include <unordered_set>

inline constexpr int ASCII_SIZE = 63; // a-z (26) + A-Z (26) + 0-9 (10) + '-'

class Trie {
private:
    struct TrieNode {
        std::array<std::unique_ptr<TrieNode>, ASCII_SIZE> children{};
        bool isEndOfWord = false;
        std::unordered_set<std::string> keys;
    };

    std::unique_ptr<TrieNode> root;

    int charToIndex(char c) const;
    bool helperDelete(TrieNode* node, const std::string& key, int index);

public:
    Trie();

    void insert(const std::string& word);
    bool search(const std::string& word) const;
    bool remove(const std::string& word);
    bool isPrefix(const std::string& prefix) const;
    std::list<std::string> getWordsWithPrefix(const std::string& prefix) const;
};
