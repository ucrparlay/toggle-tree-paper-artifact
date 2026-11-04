#include "galois/Galois.h"
#include "galois/Reduction.h"
#include "galois/Timer.h"
#include "galois/Version.h"
#include "galois/graphs/LCGraph.h"
#include "galois/graphs/TypeTraits.h"
#include "Lonestar/BFS_SSSP.h"
#include "toggle/benchmark_utils/verify.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

std::string galois::getVersion() { return "6.0.0"; }
std::string galois::getRevision() { return "unknown"; }
int galois::getVersionMajor() { return 6; }
int galois::getVersionMinor() { return 0; }
int galois::getVersionPatch() { return 0; }
int galois::getCopyrightYear() { return 2018; }

static std::string inputFile;
static std::string sourcePath  = "disabled";
static unsigned int reportNode = 1;
static unsigned int numThreads = 1;
static unsigned int numRounds  = 1;
static bool skipVerify         = false;

enum Exec { SERIAL, PARALLEL };
enum Algo { AsyncTile = 0, Async, SyncTile, Sync };

static Exec execution = PARALLEL;
static Algo algo      = SyncTile;

const char* const ALGO_NAMES[] = {"AsyncTile", "Async", "SyncTile", "Sync"};

static void usage(const char* argv0) {
  std::cerr << "usage: " << argv0
            << " [options] <input graph>\n\n"
               "options:\n"
               "  -f FILE            input graph\n"
               "  -n N               number of benchmark rounds\n"
               "  -S FILE            sources csv file\n"
               "  -reportNode=N      node id to report (default 1)\n"
               "  -t=N               number of worker threads (default 1)\n"
               "  -exec=SERIAL|PARALLEL\n"
               "  -algo=AsyncTile|Async|SyncTile|Sync\n"
               "  -skipVerify        skip verification\n"
               "  -h, --help         show this help\n";
}

static bool consumePrefix(const std::string& arg, const char* prefix,
                          std::string& value) {
  const std::string p(prefix);
  if (arg.rfind(p, 0) != 0)
    return false;
  value = arg.substr(p.size());
  return true;
}

static unsigned parseUnsigned(const std::string& value, const char* option) {
  char* end = nullptr;
  unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed > std::numeric_limits<unsigned>::max()) {
    std::cerr << "invalid value for " << option << ": " << value << "\n";
    std::exit(1);
  }
  return static_cast<unsigned>(parsed);
}

static void parseArgs(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    std::string value;

    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      std::exit(0);
    } else if (arg == "-skipVerify") {
      skipVerify = true;
    } else if (arg == "-f") {
      if (++i >= argc) {
        std::cerr << "-f requires a filename\n";
        std::exit(1);
      }
      inputFile = argv[i];
    } else if (arg == "-n") {
      if (++i >= argc) {
        std::cerr << "-n requires a round count\n";
        std::exit(1);
      }
      numRounds = parseUnsigned(argv[i], "-n");
    } else if (arg == "-S") {
      if (++i >= argc) {
        std::cerr << "-S requires a filename\n";
        std::exit(1);
      }
      sourcePath = argv[i];
    } else if (consumePrefix(arg, "-f=", value)) {
      inputFile = value;
    } else if (consumePrefix(arg, "-n=", value)) {
      numRounds = parseUnsigned(value, "-n");
    } else if (consumePrefix(arg, "-reportNode=", value)) {
      reportNode = parseUnsigned(value, "-reportNode");
    } else if (consumePrefix(arg, "-t=", value)) {
      numThreads = parseUnsigned(value, "-t");
    } else if (consumePrefix(arg, "-exec=", value)) {
      if (value == "SERIAL") {
        execution = SERIAL;
      } else if (value == "PARALLEL") {
        execution = PARALLEL;
      } else {
        std::cerr << "invalid -exec value: " << value << "\n";
        std::exit(1);
      }
    } else if (consumePrefix(arg, "-algo=", value)) {
      if (value == "AsyncTile") {
        algo = AsyncTile;
      } else if (value == "Async") {
        algo = Async;
      } else if (value == "SyncTile") {
        algo = SyncTile;
      } else if (value == "Sync") {
        algo = Sync;
      } else {
        std::cerr << "invalid -algo value: " << value << "\n";
        std::exit(1);
      }
    } else if (!arg.empty() && arg[0] == '-') {
      std::cerr << "unknown option: " << arg << "\n";
      usage(argv[0]);
      std::exit(1);
    } else if (inputFile.empty()) {
      inputFile = arg;
    } else {
      std::cerr << "unexpected extra positional argument: " << arg << "\n";
      usage(argv[0]);
      std::exit(1);
    }
  }

  if (inputFile.empty()) {
    usage(argv[0]);
    std::exit(1);
  }

  if (numThreads == 0) {
    std::cerr << "-t must be greater than 0\n";
    std::exit(1);
  }
  if (numRounds == 0) {
    std::cerr << "-n must be greater than 0\n";
    std::exit(1);
  }
}

using Graph =
    galois::graphs::LC_CSR_Graph<unsigned, void>::with_no_lockable<true>::type;
using GNode = Graph::GraphNode;

constexpr static const bool TRACK_WORK          = false;
constexpr static const unsigned CHUNK_SIZE      = 256U;
constexpr static const ptrdiff_t EDGE_TILE_SIZE = 256;

using BFS = BFS_SSSP<Graph, unsigned int, false, EDGE_TILE_SIZE>;

using UpdateRequest       = BFS::UpdateRequest;
using Dist                = BFS::Dist;
using SrcEdgeTile         = BFS::SrcEdgeTile;
using SrcEdgeTilePushWrap = BFS::SrcEdgeTilePushWrap;
using ReqPushWrap         = BFS::ReqPushWrap;
using OutEdgeRangeFn      = BFS::OutEdgeRangeFn;
using TileRangeFn         = BFS::TileRangeFn;

namespace toggle {

struct BinaryGraph {
  uint64_t n = 0;
  uint64_t m = 0;
  std::vector<uint64_t> offsets;
  std::vector<uint32_t> edges;
};

inline BinaryGraph read_binary_format(const std::string& filename) {
  struct stat sb;
  int fd = open(filename.c_str(), O_RDONLY);
  if (fd == -1 || fstat(fd, &sb) == -1) {
    std::cerr << "Error: Failed in opening file " << filename << "\n";
    std::abort();
  }
  char* data =
      static_cast<char*>(mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0));
  if (data == MAP_FAILED) {
    std::cerr << "Error: mmap failed " << filename << "\n";
    close(fd);
    std::abort();
  }

  BinaryGraph graph;
  graph.n = reinterpret_cast<uint64_t*>(data)[0];
  graph.m = reinterpret_cast<uint64_t*>(data)[1];
  uint64_t bytes = reinterpret_cast<uint64_t*>(data)[2];
  uint64_t expected = 24 + (graph.n + 1) * sizeof(uint64_t) +
                      graph.m * sizeof(uint32_t);
  if (bytes != expected || static_cast<uint64_t>(sb.st_size) < expected) {
    std::cerr << "Error: Incorrect or weighted .bin file format.\n";
    munmap(data, sb.st_size);
    close(fd);
    std::abort();
  }

  const auto* raw_offsets = reinterpret_cast<uint64_t*>(data + 24);
  const auto* raw_edges =
      reinterpret_cast<uint32_t*>(data + 24 + (graph.n + 1) * sizeof(uint64_t));
  graph.offsets.assign(raw_offsets, raw_offsets + graph.n + 1);
  graph.edges.assign(raw_edges, raw_edges + graph.m);

  munmap(data, sb.st_size);
  close(fd);
  return graph;
}

} // namespace toggle

struct EdgeTile {
  Graph::edge_iterator beg;
  Graph::edge_iterator end;
};

struct EdgeTileMaker {
  EdgeTile operator()(Graph::edge_iterator beg,
                      Graph::edge_iterator end) const {
    return EdgeTile{beg, end};
  }
};

struct NodePushWrap {
  template <typename C>
  void operator()(C& cont, const GNode& n, const char* const) const {
    (*this)(cont, n);
  }

  template <typename C>
  void operator()(C& cont, const GNode& n) const {
    cont.push(n);
  }
};

struct EdgeTilePushWrap {
  Graph& graph;

  template <typename C>
  void operator()(C& cont, const GNode& n, const char* const) const {
    BFS::pushEdgeTilesParallel(cont, graph, n, EdgeTileMaker{});
  }

  template <typename C>
  void operator()(C& cont, const GNode& n) const {
    BFS::pushEdgeTiles(cont, graph, n, EdgeTileMaker{});
  }
};

template <bool CONCURRENT, typename T, typename P, typename R>
void asyncAlgo(Graph& graph, GNode source, const P& pushWrap,
               const R& edgeRange) {
  namespace gwl = galois::worklists;
  using FIFO = gwl::PerSocketChunkFIFO<CHUNK_SIZE>;
  using BSWL = gwl::BulkSynchronous<gwl::PerSocketChunkLIFO<CHUNK_SIZE>>;
  using WL   = FIFO;

  using Loop =
      typename std::conditional<CONCURRENT, galois::ForEach,
                                galois::WhileQ<galois::SerFIFO<T>>>::type;

  GALOIS_GCC7_IGNORE_UNUSED_BUT_SET
  constexpr bool useCAS = CONCURRENT && !std::is_same<WL, BSWL>::value;
  GALOIS_END_GCC7_IGNORE_UNUSED_BUT_SET

  Loop loop;

  galois::GAccumulator<size_t> BadWork;
  galois::GAccumulator<size_t> WLEmptyWork;

  graph.getData(source) = 0;
  galois::InsertBag<T> initBag;

  if (CONCURRENT) {
    pushWrap(initBag, source, 1, "parallel");
  } else {
    pushWrap(initBag, source, 1);
  }

  loop(
      galois::iterate(initBag),
      [&](const T& item, auto& ctx) {
        constexpr galois::MethodFlag flag = galois::MethodFlag::UNPROTECTED;

        const auto& sdist = graph.getData(item.src, flag);

        if (TRACK_WORK) {
          if (item.dist != sdist) {
            WLEmptyWork += 1;
            return;
          }
        }

        const auto newDist = item.dist;

        for (auto ii : edgeRange(item)) {
          GNode dst   = graph.getEdgeDst(ii);
          auto& ddata = graph.getData(dst, flag);

          while (true) {
            Dist oldDist = ddata;

            if (oldDist <= newDist) {
              break;
            }

            if (!useCAS ||
                __sync_bool_compare_and_swap(&ddata, oldDist, newDist)) {
              if (!useCAS) {
                ddata = newDist;
              }

              if (TRACK_WORK) {
                if (oldDist != BFS::DIST_INFINITY) {
                  BadWork += 1;
                }
              }

              pushWrap(ctx, dst, newDist + 1);
              break;
            }
          }
        }
      },
      galois::wl<WL>(), galois::loopname("runBFS"),
      galois::disable_conflict_detection());

  if (TRACK_WORK) {
    galois::runtime::reportStat_Single("BFS", "BadWork", BadWork.reduce());
    galois::runtime::reportStat_Single("BFS", "EmptyWork",
                                       WLEmptyWork.reduce());
  }
}

template <bool CONCURRENT, typename T, typename P, typename R>
void syncAlgo(Graph& graph, GNode source, const P& pushWrap,
              const R& edgeRange) {
  using Cont = typename std::conditional<CONCURRENT, galois::InsertBag<T>,
                                         galois::SerStack<T>>::type;
  using Loop = typename std::conditional<CONCURRENT, galois::DoAll,
                                         galois::StdForEach>::type;

  constexpr galois::MethodFlag flag = galois::MethodFlag::UNPROTECTED;

  Loop loop;

  auto curr = std::make_unique<Cont>();
  auto next = std::make_unique<Cont>();

  Dist nextLevel              = 0U;
  graph.getData(source, flag) = 0U;

  if (CONCURRENT) {
    pushWrap(*next, source, "parallel");
  } else {
    pushWrap(*next, source);
  }

  assert(!next->empty());

  while (!next->empty()) {
    std::swap(curr, next);
    next->clear();
    ++nextLevel;

    loop(
        galois::iterate(*curr),
        [&](const T& item) {
          for (auto e : edgeRange(item)) {
            auto dst      = graph.getEdgeDst(e);
            auto& dstData = graph.getData(dst, flag);

            if (dstData == BFS::DIST_INFINITY) {
              dstData = nextLevel;
              pushWrap(*next, dst);
            }
          }
        },
        galois::steal(), galois::chunk_size<CHUNK_SIZE>(),
        galois::loopname("Sync"));
  }
}

template <bool CONCURRENT>
void runAlgo(Graph& graph, const GNode& source) {
  switch (algo) {
  case AsyncTile:
    asyncAlgo<CONCURRENT, SrcEdgeTile>(
        graph, source, SrcEdgeTilePushWrap{graph}, TileRangeFn());
    break;
  case Async:
    asyncAlgo<CONCURRENT, UpdateRequest>(graph, source, ReqPushWrap(),
                                         OutEdgeRangeFn{graph});
    break;
  case SyncTile:
    syncAlgo<CONCURRENT, EdgeTile>(graph, source, EdgeTilePushWrap{graph},
                                   TileRangeFn());
    break;
  case Sync:
    syncAlgo<CONCURRENT, GNode>(graph, source, NodePushWrap(),
                                OutEdgeRangeFn{graph});
    break;
  default:
    std::cerr << "ERROR: unknown algorithm type\n";
  }
}

int main(int argc, char** argv) {
  parseArgs(argc, argv);

  galois::SharedMemSys G;
  numThreads = galois::setActiveThreads(numThreads);

  Graph graph;

  std::cout << "Reading from file: " << inputFile << "\n";
  if (inputFile.size() >= 4 &&
      inputFile.substr(inputFile.size() - 4) == ".bin") {
    toggle::BinaryGraph bin_graph = toggle::read_binary_format(inputFile);
    if (bin_graph.n > std::numeric_limits<uint32_t>::max()) {
      std::cerr << "Galois BFS only supports uint32_t vertex ids\n";
      std::abort();
    }
    galois::graphs::FileGraph file_graph;
    file_graph.fromHostArrays(bin_graph.offsets.data() + 1, bin_graph.n,
                              bin_graph.edges.data(), bin_graph.m, 0, 1);
    typename Graph::read_tag tag;
    galois::graphs::readGraphDispatch(graph, tag, file_graph, true);
  } else {
    galois::graphs::readGraph(graph, inputFile);
  }
  std::cout << "Read " << graph.size() << " nodes, " << graph.sizeEdges()
            << " edges\n";

  if (reportNode >= graph.size()) {
    std::cerr << "failed to set report: " << reportNode << "\n";
    abort();
  }

  size_t approxNodeData = 4 * (graph.size() + graph.sizeEdges());
  galois::preAlloc(8 * numThreads +
                   approxNodeData / galois::runtime::pagePoolSize());

  galois::reportPageAlloc("MeminfoPre");

  std::cout << "Running " << ALGO_NAMES[algo] << " algorithm with "
            << (execution == PARALLEL ? "PARALLEL" : "SERIAL")
            << " execution\n";

  std::string graph_name = toggle::extract_graph_name(inputFile);
  uint32_t num_rounds = std::min<size_t>(graph.size(), numRounds);
  auto sources = toggle::get_sources(sourcePath, graph_name, num_rounds);

  double trial_seconds = 0.0;
  double total_seconds = 0.0;
  for (uint32_t round = 0; round < num_rounds; round++) {
    GNode source = sources[round];
    GNode report = reportNode;

    auto run_once = [&]() {
      galois::do_all(
          galois::iterate(graph),
          [&graph](GNode n) { graph.getData(n) = BFS::DIST_INFINITY; },
          galois::loopname("Reset"), galois::no_stats());
      graph.getData(source) = 0;
      galois::Timer trial_timer;
      trial_timer.start();
      if (execution == SERIAL) {
        runAlgo<false>(graph, source);
      } else {
        runAlgo<true>(graph, source);
      }
      trial_timer.stop();
      return trial_timer.get_usec() / 1000000.0;
    };

    trial_seconds = run_once();
    std::cout << "    Round " << round + 1 << "  source: " << source
              << "  Warmup: " << std::setprecision(2) << trial_seconds
              << std::setprecision(6);

    trial_seconds = run_once();
    std::cout << "  time = " << trial_seconds << " sec\n";
    total_seconds += trial_seconds;

    std::cout << "Node " << reportNode << " has distance "
              << graph.getData(report, galois::MethodFlag::UNPROTECTED)
              << "\n";

    if (!skipVerify) {
      if (BFS::verify(graph, source)) {
        std::cout << "Verification successful.\n";
      } else {
        GALOIS_DIE("verification failed");
      }
    }

    if (round == num_rounds - 1) {
      std::vector<uint32_t> result(graph.size());
      #pragma omp parallel for
      for (size_t i = 0; i < graph.size(); ++i)
        result[i] = graph.getData(static_cast<GNode>(i),
                                  galois::MethodFlag::UNPROTECTED) == BFS::DIST_INFINITY
                        ? UINT32_MAX   // unreached -> type max, as process_result expects
                        : graph.getData(static_cast<GNode>(i), galois::MethodFlag::UNPROTECTED);
      toggle::process_result(total_seconds / num_rounds, result,
                               "..", graph_name, "Galois");
    }
  }

  galois::reportPageAlloc("MeminfoPost");
  return 0;
}
