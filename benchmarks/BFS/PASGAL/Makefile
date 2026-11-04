CXXFLAGS := -O3 -march=native -std=c++20 -pthread
INCLUDES := -I../../../include -I../../../baselines/parlaylib/include -I../../../baselines/PASGAL/src
all:
	g++ $(CXXFLAGS) $(INCLUDES) test.cpp -o test
clean:
	rm -f test
