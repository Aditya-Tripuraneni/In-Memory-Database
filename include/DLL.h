#pragma once

#include <optional>
#include <string>

// Node representing a single version entry in the DLL (newest near dummyRight).
struct Node {
    std::string record;
    int timestamp;
    std::optional<int> ttl;
    Node* prev;
    Node* next;

    Node(const std::string& record, int timestamp, std::optional<int> ttl);
    void update(const std::string& record, int timestamp, std::optional<int> ttl);
};

// Doubly linked list with dummy sentinels used to maintain chronological record versions.
class DLL {
public:
    DLL();
    ~DLL();

    DLL(const DLL&) = delete;
    DLL& operator=(const DLL&) = delete;

    void insertAtEnd(Node* newNode);
    void deleteNode(Node* nodeToDelete);

    Node* getLatest() const;
    Node* getOldest() const;
    bool isDummy(const Node* node) const;

    int getLength() const {return length;}

private:
    Node* dummyRight;
    Node* dummyLeft;
    int length;
};
