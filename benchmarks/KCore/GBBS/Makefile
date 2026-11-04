ROOT := ../../../baselines/GBBS
CXXFLAGS := -std=c++20 -O3 -DNDEBUG -march=native -pthread -DAMORTIZEDPD -Wno-deprecated
INCLUDES := -I$(ROOT) -I../../../baselines/parlaylib/include -I../../../include
LINKS    := $(ROOT)/gbbs/*.cc $(ROOT)/gbbs/encodings/*.cc $(ROOT)/gbbs/helpers/parse_command_line.cc
all:
	g++ $(CXXFLAGS) $(INCLUDES) test.cc $(LINKS) -o test
clean:
	rm -f test
