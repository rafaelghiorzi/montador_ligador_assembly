CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2

all: montador ligador

montador: src/montador.cpp src/assembler.cpp src/assembler.hpp
	$(CXX) $(CXXFLAGS) src/montador.cpp src/assembler.cpp -o montador

ligador: src/ligador.cpp src/assembler.cpp src/assembler.hpp
	$(CXX) $(CXXFLAGS) src/ligador.cpp src/assembler.cpp -o ligador

clean:
	rm -f montador ligador
