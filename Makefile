# Compiler and flags:
CXX = g++
CXXFLAGS = -Wall -Wextra

server:
	$(CXX) $(CXXFLAGS) -o server server.cpp utilities.cpp buffer.cpp 

client:
	$(CXX) $(CXXFLAGS) -o client client.cpp utilities.cpp buffer.cpp

clean:
	rm -f server client