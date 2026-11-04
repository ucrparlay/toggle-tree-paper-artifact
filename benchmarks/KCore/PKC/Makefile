CXXFLAGS := -O3 -march=native -std=c++20 -pthread -fopenmp
INCLUDES := -I../../../include -I../../../baselines/parlaylib/include
all:
	g++ $(CXXFLAGS) $(INCLUDES) test.cpp -o test
clean:
	rm -f test
