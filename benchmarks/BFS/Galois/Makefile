ROOT := ../../../baselines/Galois
INCLUDES := -isystem $(ROOT)/third_party/boost -I$(ROOT)/libgalois/include -I$(ROOT)/lonestar/liblonestar/include -I../../../include

GALOIS_SRCS := \
  $(ROOT)/libgalois/src/Barrier_Counting.cpp \
  $(ROOT)/libgalois/src/Barrier.cpp \
  $(ROOT)/libgalois/src/Barrier_Dissemination.cpp \
  $(ROOT)/libgalois/src/Barrier_MCS.cpp \
  $(ROOT)/libgalois/src/Barrier_Pthread.cpp \
  $(ROOT)/libgalois/src/Barrier_Simple.cpp \
  $(ROOT)/libgalois/src/Barrier_Topo.cpp \
  $(ROOT)/libgalois/src/Context.cpp \
  $(ROOT)/libgalois/src/Deterministic.cpp \
  $(ROOT)/libgalois/src/DynamicBitset.cpp \
  $(ROOT)/libgalois/src/EnvCheck.cpp \
  $(ROOT)/libgalois/src/FileGraph.cpp \
  $(ROOT)/libgalois/src/FileGraphParallel.cpp \
  $(ROOT)/libgalois/src/gIO.cpp \
  $(ROOT)/libgalois/src/GraphHelpers.cpp \
  $(ROOT)/libgalois/src/HWTopo.cpp \
  $(ROOT)/libgalois/src/HWTopoLinux.cpp \
  $(ROOT)/libgalois/src/Mem.cpp \
  $(ROOT)/libgalois/src/NumaMem.cpp \
  $(ROOT)/libgalois/src/OCFileGraph.cpp \
  $(ROOT)/libgalois/src/PageAlloc.cpp \
  $(ROOT)/libgalois/src/PagePool.cpp \
  $(ROOT)/libgalois/src/ParaMeter.cpp \
  $(ROOT)/libgalois/src/PerThreadStorage.cpp \
  $(ROOT)/libgalois/src/PreAlloc.cpp \
  $(ROOT)/libgalois/src/Profile.cpp \
  $(ROOT)/libgalois/src/PtrLock.cpp \
  $(ROOT)/libgalois/src/SharedMem.cpp \
  $(ROOT)/libgalois/src/SharedMemSys.cpp \
  $(ROOT)/libgalois/src/SimpleLock.cpp \
  $(ROOT)/libgalois/src/Statistics.cpp \
  $(ROOT)/libgalois/src/Substrate.cpp \
  $(ROOT)/libgalois/src/Support.cpp \
  $(ROOT)/libgalois/src/Termination.cpp \
  $(ROOT)/libgalois/src/ThreadPool.cpp \
  $(ROOT)/libgalois/src/Threads.cpp \
  $(ROOT)/libgalois/src/ThreadTimer.cpp \
  $(ROOT)/libgalois/src/Timer.cpp \
  $(ROOT)/libgalois/src/Tracer.cpp

SRCS := bfs.cpp $(GALOIS_SRCS)

all: $(SRCS)
	g++ -O3 -march=native -std=c++17 -pthread $(INCLUDES) $(SRCS) -o test

clean:
	rm -f test
