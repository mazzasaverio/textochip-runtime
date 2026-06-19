CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I../src
SRC = main.cpp hal_host.cpp ../src/isa.cpp ../src/vm.cpp ../src/runtime.cpp ../src/registry.cpp
BIN = sakura_vm_host

$(BIN): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(BIN) $(SRC)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: run clean
