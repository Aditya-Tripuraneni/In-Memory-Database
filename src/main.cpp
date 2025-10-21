#include <iostream>
#include "InMemoryDB.h"

int main() {
    std::cout << "InMemoryDB demo: basic insert and scanByPrefix" << std::endl;
    InMemoryDB db;
    db.newInsert("user1", "name", "Alice", 100);
    db.newInsert("user1", "age", "30", 101);
    db.newInsert("user2", "name", "Bob", 90);

    auto results = db.scanByPrefix("user", 150);
    for (const auto& [key, field, value] : results) {
        std::cout << key << ":" << field << " = " << value << std::endl;
    }
    return 0;
}
