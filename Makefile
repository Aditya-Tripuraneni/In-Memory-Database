CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2
SRC = DLL.cpp TRIE.cpp inMemoryDB.cpp
HEADERS = DLL.h TRIE.h

# If you have a main.cpp, build main executable
MAIN = main.cpp

all: main

main: $(MAIN) $(SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(MAIN) $(SRC) -o main

clean:
	rm -f main *.o *.out

.PHONY: all clean
