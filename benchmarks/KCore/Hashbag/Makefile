ROOT := ../../..
CXXFLAGS := -O3 -march=native -std=c++20 -pthread -DTOGGLE_TREE_USE_PARLAY
INCLUDES := -I$(ROOT)/include -I$(ROOT)/baselines/parlaylib/include -I$(ROOT)/baselines/PASGAL/src

.PHONY: all clean
all:
	g++ $(CXXFLAGS) $(INCLUDES) test.cpp -o test

clean:
	rm -f test
