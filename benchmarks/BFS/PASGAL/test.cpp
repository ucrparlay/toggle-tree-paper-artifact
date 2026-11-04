#include "bfs.h"

#include <queue>

#include "graph.h"
#include "toggle/benchmark_utils/verify.h"

constexpr int NUM_SRC = 5;
constexpr int NUM_ROUND = 3;

template <class Algo, class Graph, class NodeId = typename Graph::NodeId>
void run(Algo &algo, const Graph &G, bool verify, NodeId num_rounds, const char* input_path, const std::string& source_path) {
    num_rounds = std::min<NodeId>(G.n, num_rounds);
    auto sources = toggle::get_sources(source_path, toggle::extract_graph_name(input_path), num_rounds);
    parlay::internal::timer t; double tt = 0, ttt = 0;
    for (uint32_t round = 0; round < num_rounds; round++) {
        auto source = sources[round];
        {
            t.start();
            parlay::sequence<uint32_t> result = algo.bfs(source);
            tt = t.stop();
            std::cout << "    Round " << round + 1 << "  source: " << source << "  Warmup: "  << std::setprecision(2) << tt << std::setprecision(6);
        }
        {
            t.start();
            parlay::sequence<uint32_t> result = algo.bfs(source);
            tt = t.stop();
            std::cout << "  time = " << tt << " sec\n";
            ttt += tt;
            if (round == num_rounds - 1) {
                toggle::process_result(ttt / num_rounds, result, "..", toggle::extract_graph_name(input_path), "PASGAL"); 
            }
        }
    }
    printf("\n");
}

int main(int argc, char *argv[]) {
  if (argc == 1) {
    fprintf(stderr,
            "Usage: %s [-i input_file] [-s] [-v]\n"
            "Options:\n"
            "\t-i,\tinput file path\n"
            "\t-s,\tsymmetrized input graph\n"
            "\t-v,\tverify result\n"
            "\t-S,\tsources csv file\n",
            argv[0]);
    exit(EXIT_FAILURE);
  }
  int c;
  char const *input_path = nullptr;
  bool symmetrized = false;
  bool verify = false;
  uint32_t source = 0;
  uint32_t num_rounds = 3;
  std::string source_path = "disabled";
  while ((c = getopt(argc, argv, "i:svr:n:S:")) != -1) {
    switch (c) {
      case 'i':
        input_path = optarg;
        break;
      case 's':
        symmetrized = true;
        break;
      case 'v':
        verify = true;
        break;
      case 'r':
        source = atol(optarg);
        break;
      case 'n':
        num_rounds = atol(optarg);
        break;
      case 'S':
        source_path = optarg;
    }
  }

  printf("Reading graph...\n");
  Graph G;
  G.read_graph(input_path);
  G.symmetrized = symmetrized;
  if (!G.symmetrized) {
    G.make_inverse();
  }

  fprintf(stdout, "Running on %s: |V|=%zu, |E|=%zu, num_src=%d, num_round=%d\n",
          input_path, G.n, G.m, NUM_SRC, NUM_ROUND);

  BFS solver(G);
  run(solver, G, verify, num_rounds, input_path, source_path);
  return 0;
}
