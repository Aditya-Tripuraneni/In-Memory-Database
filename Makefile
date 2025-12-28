CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude -pthread

SRC_DIR = src
INCLUDE_DIR = include

CORE_SOURCES = $(SRC_DIR)/DLL.cpp \
               $(SRC_DIR)/TRIE.cpp \
               $(SRC_DIR)/inMemoryDB.cpp

THREADSAFE_SOURCES = $(CORE_SOURCES) $(SRC_DIR)/ThreadSafeInMemoryDB.cpp

HEADERS = $(INCLUDE_DIR)/DLL.h \
          $(INCLUDE_DIR)/TRIE.h \
          $(INCLUDE_DIR)/InMemoryDB.h \
          $(INCLUDE_DIR)/ThreadSafeInMemoryDB.h

MAIN = $(SRC_DIR)/main.cpp
BENCHMARK = $(SRC_DIR)/benchmark.cpp

all: main benchmark

main: $(MAIN) $(CORE_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(MAIN) $(CORE_SOURCES) -o $@

benchmark: $(BENCHMARK) $(THREADSAFE_SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(BENCHMARK) $(THREADSAFE_SOURCES) -o $@

clean:
	rm -f main benchmark $(SRC_DIR)/*.o $(SRC_DIR)/*.out

.PHONY: all clean
