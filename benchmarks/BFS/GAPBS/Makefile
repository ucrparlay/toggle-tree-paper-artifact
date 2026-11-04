CXXFLAGS := -O3 -march=native -std=c++20 -pthread -fopenmp
INCLUDES := -I../../../include -I../../../baselines/GAPBS/src
all:
	g++ $(CXXFLAGS) $(INCLUDES) bfs.cc -o test
clean:
	rm -f test
