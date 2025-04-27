CXX = g++
CXXFLAGS = -g -std=c++14 -Wall -Wextra
LDFLAGS = -Wl,--wrap=malloc,--wrap=free -lmemkind -lpthread

all: test_hbm_manager

test_hbm_manager: test_hbm_manager.o HBMMemoryManager.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test_hbm_manager.o: test_hbm_manager.cpp HBMMemoryManager.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

HBMMemoryManager.o: HBMMemoryManager.cpp HBMMemoryManager.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f test_hbm_manager *.o

.PHONY: all clean