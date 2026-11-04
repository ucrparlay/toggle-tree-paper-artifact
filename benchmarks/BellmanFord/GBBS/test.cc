#include "BellmanFord.h"
#include "toggle/benchmark_utils/verify.h"

namespace gbbs {
template <class Graph>
double BellmanFord_runner(Graph& G, commandLine P) {
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
    for (uint32_t round = 0; round < num_rounds; round++) {
        auto source = sources[round];
        {
            t.start();
            parlay::sequence<int32_t> result = BellmanFord(G, source);
            tt = t.stop();
            std::cout << "    Round " << round + 1 << "  source: " << source << "  Warmup: "  << std::setprecision(2) << tt << std::setprecision(6);
        }
        {
            t.start();
            parlay::sequence<int32_t> result = BellmanFord(G, source);
            tt = t.stop();
            std::cout << "  time = " << tt << " sec\n";
            ttt += tt;
            if (round == num_rounds - 1) {
                auto output = parlay::sequence<uint32_t>::from_function(result.size(), [&](size_t i) {
                    return result[i] == INT32_MAX ? UINT32_MAX : uint32_t(result[i]);
                });
                toggle::process_result(ttt / num_rounds, output, "..", toggle::extract_graph_name(P.getArgument(0)), "GBBS", dumppath);
            }
        }
    }
    std::exit(0);
    return ttt;
}
}  // namespace gbbs

generate_weighted_main(gbbs::BellmanFord_runner, false);
