#include "DLL.h"

Node::Node(const std::string& record, int timestamp, std::optional<int> ttl)
    : record(record), timestamp(timestamp), ttl(ttl), prev(nullptr), next(nullptr) {}

void Node::update(const std::string& record, int timestamp, std::optional<int> ttl) {
    this->record = record;
    this->timestamp = timestamp;
    this->ttl = ttl;
}

DLL::DLL() : dummyRight(nullptr), dummyLeft(nullptr), length(0) {
    dummyLeft = new Node("", 0, std::nullopt);
    dummyRight = new Node("", 0, std::nullopt);
    dummyLeft->next = dummyRight;
    dummyRight->prev = dummyLeft;
}

DLL::~DLL() {
    Node* curr = dummyLeft;
    while (curr != nullptr) {
        Node* next = curr->next;
        delete curr;
        curr = next;
    }
}

void DLL::insertAtEnd(Node* newNode) {
    Node* prev = dummyRight->prev;
    prev->next = newNode;
    newNode->next = dummyRight;
    dummyRight->prev = newNode;
    newNode->prev = prev;
    length++;
}

void DLL::deleteNode(Node* nodeToDelete) {
    Node* prev = nodeToDelete->prev;
    Node* nextNode = nodeToDelete->next;
    prev->next = nextNode;
    nextNode->prev = prev;
    delete nodeToDelete;
    length--;
}

Node* DLL::getLatest() const {
    return dummyRight->prev;
}

Node* DLL::getOldest() const {
    return dummyLeft->next;
}

bool DLL::isDummy(const Node* node) const {
    return node == dummyLeft || node == dummyRight;
}