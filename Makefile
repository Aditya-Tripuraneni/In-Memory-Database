CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -Iinclude

SRC_DIR = src
INCLUDE_DIR = include

SOURCES = $(SRC_DIR)/DLL.cpp \
          $(SRC_DIR)/TRIE.cpp \
          $(SRC_DIR)/inMemoryDB.cpp

HEADERS = $(INCLUDE_DIR)/DLL.h \
          $(INCLUDE_DIR)/TRIE.h \
          $(INCLUDE_DIR)/InMemoryDB.h

MAIN = $(SRC_DIR)/main.cpp

all: main

main: $(MAIN) $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(MAIN) $(SOURCES) -o $@

clean:
	rm -f main $(SRC_DIR)/*.o $(SRC_DIR)/*.out

.PHONY: all clean
