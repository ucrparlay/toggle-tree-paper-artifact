#include "GraphColoring.h"
#include "toggle/benchmark_utils/verify.h"

namespace gbbs {

template <class Graph>
double Coloring_runner(Graph& G, commandLine P) {
    std::cout << "==================================================================" << std::endl;
    const char* dumppath = P.getOptionValue("-dump") == nullptr ? "disabled" : P.getOptionValue("-dump");
    int num_rounds = std::atoi(P.getOptionValue("-num_rounds"));
    std::cout << std::right << std::setw(66) << ("Graph: " + toggle::extract_graph_name(P.getArgument(0))) << "\n";
    std::cout << "Dump: " << dumppath << "\n";
    std::cout << "Threads: " << num_workers() << "  Rounds: " << num_rounds << "\n";

    parlay::internal::timer t; double ttt = 0;
    for (int round = 0; round <= num_rounds; round++) {
        t.start();
        auto result = Coloring(G, true);
        double tt = t.stop();
        if (round == 0) { std::cout << "    Warmup: " << tt << "\n"; }
        else { ttt += tt; std::cout << "    Round " << round << " time = " << tt << " sec\n"; }
        if (round == num_rounds) {
            toggle::process_result(ttt / num_rounds, result, "..", toggle::extract_graph_name(P.getArgument(0)), "GBBS", dumppath); 
        }
    }
    std::exit(0);
    return ttt;
}

}  // namespace gbbs
generate_main(gbbs::Coloring_runner, false);
