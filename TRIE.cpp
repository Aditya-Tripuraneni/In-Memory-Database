#include <memory>
#include "TRIE.h"

#include <algorithm>
#include <vector>

Trie::Trie() : root(std::make_unique<TrieNode>()) {}

int Trie::charToIndex(char c) const {
    if ('a' <= c && c <= 'z') {
        return c - 'a';
    }
    if ('A' <= c && c <= 'Z') {
        return c - 'A' + 26;
    }
    if ('0' <= c && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-') {
        return 62;
    }
    return -1;
}

bool Trie::helperDelete(TrieNode* node, const std::string& key, int index) {
    if (!node) {
        return false;
    }

    if (index == static_cast<int>(key.size())) {
        node->isEndOfWord = false;
        node->keys.erase(key);

        const bool noChildren = std::all_of(
            node->children.begin(), node->children.end(),
            [](const std::unique_ptr<TrieNode>& child) { return child == nullptr; }
        );

        return noChildren && node->keys.empty() && !node->isEndOfWord;
    }

    const int childIndex = charToIndex(key[index]);
    if (childIndex < 0 || childIndex >= ASCII_SIZE) {
        return false;
    }

    TrieNode* childNode = node->children[childIndex].get();
    if (childNode == nullptr) {
        return false;
    }

    const bool shouldDeleteChild = helperDelete(childNode, key, index + 1);
    if (shouldDeleteChild) {
        node->children[childIndex].reset();
    }

    node->keys.erase(key);

    const bool noChildren = std::all_of(
        node->children.begin(), node->children.end(),
        [](const std::unique_ptr<TrieNode>& child) { return child == nullptr; }
    );

    return noChildren && node->keys.empty() && !node->isEndOfWord;
}

void Trie::insert(const std::string& word) {
    std::vector<int> indices;
    indices.reserve(word.size());

    for (char c : word) {
        const int index = charToIndex(c);
        if (index < 0 || index >= ASCII_SIZE) {
            return;
        }
        indices.push_back(index);
    }

    TrieNode* curr = root.get();

    for (int index : indices) {
        if (!curr->children[index]) {
            curr->children[index] = std::make_unique<TrieNode>();
        }

        curr = curr->children[index].get();
        curr->keys.insert(word);
    }

    curr->isEndOfWord = true;
}

bool Trie::search(const std::string& word) const {
    const TrieNode* curr = root.get();

    for (char c : word) {
        const int index = charToIndex(c);
        if (index < 0 || index >= ASCII_SIZE) {
            return false;
        }

        if (!curr->children[index]) {
            return false;
        }

        curr = curr->children[index].get();
    }

    return curr->isEndOfWord;
}

bool Trie::remove(const std::string& word) {
    if (word.empty()) {
        if (!root->isEndOfWord) {
            return false;
        }
        root->isEndOfWord = false;
        return true;
    }

    if (!search(word)) {
        return false;
    }

    helperDelete(root.get(), word, 0);
    return true;
}

bool Trie::isPrefix(const std::string& prefix) const {
    const TrieNode* curr = root.get();

    for (char c : prefix) {
        const int index = charToIndex(c);
        if (index < 0 || index >= ASCII_SIZE) {
            return false;
        }

        if (!curr->children[index]) {
            return false;
        }

        curr = curr->children[index].get();
    }

    return true;
}

std::list<std::string> Trie::getWordsWithPrefix(const std::string& prefix) const {
    const TrieNode* curr = root.get();

    for (char c : prefix) {
        const int index = charToIndex(c);
        if (index < 0 || index >= ASCII_SIZE) {
            return {};
        }

        if (!curr->children[index]) {
            return {};
        }

        curr = curr->children[index].get();
    }

    return std::list<std::string>(curr->keys.begin(), curr->keys.end());
}

