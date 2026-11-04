#define WEIGHTED 1

#include "wBFS.h"
#include "toggle/benchmark_utils/verify.h"

namespace gbbs {
template <class Graph>
double wBFS_runner(Graph& G, commandLine P) {
    std::cout << "==================================================================" << std::endl;
    const char* dumppath = P.getOptionValue("-dump") == nullptr ? "disabled" : P.getOptionValue("-dump");
    std::string source_path = P.getOptionValue("-S", "disabled");
    int num_rounds = std::atoi(P.getOptionValue("-num_rounds"));
    std::cout << std::right << std::setw(66) << ("Graph: " + toggle::extract_graph_name(P.getArgument(0))) << "\n";
    std::cout << "Dump: " << dumppath << "\n";
    std::cout << "Threads: " << num_workers() << "  Rounds: " << num_rounds << "\n";

    num_rounds = std::min<int>(G.n, num_rounds);
    auto sources = toggle::get_sources(source_path, toggle::extract_graph_name(P.getArgument(0)), num_rounds);
    parlay::internal::timer t; double tt = 0, ttt = 0;
    auto normalize = [&](auto& result) { parlay::parallel_for(0, result.size(), [&](size_t i) { if (result[i] == INT_E_MAX) result[i] = UINT32_MAX; }); };  // unreached -> type max
    for (uint32_t round = 0; round < num_rounds; round++) {
        auto source = sources[round];
        {
            t.start();
            parlay::sequence<uint32_t> result = wBFS(G, source, 32, false, false);
            tt = t.stop();
            normalize(result);
            std::cout << "    Round " << round + 1 << "  source: " << source << "  Warmup: "  << std::setprecision(2) << tt << std::setprecision(6);
        }
        {
            t.start();
            parlay::sequence<uint32_t> result = wBFS(G, source, 32, false, false);
            tt = t.stop();
            normalize(result);
            std::cout << "  time = " << tt << " sec\n";
            ttt += tt;
            if (round == num_rounds - 1) {
                toggle::process_result(ttt / num_rounds, result, "..", toggle::extract_graph_name(P.getArgument(0)), "GBBS", dumppath);
            }
        }
    }
    std::exit(0);
    return ttt;
}
}  // namespace gbbs

generate_weighted_main(gbbs::wBFS_runner, false);
