#include <omp.h>
#include <string>
#include <vector>
#include "toggle/benchmark_utils/timer.h"
#include "toggle/benchmark_utils/verify.h"
#include "PKC.h"

int main(int argc, char** argv) {
    char* filepath = argv[1];
    uint32_t num_rounds = (argc == 2) ? 2 : std::atoi(argv[2]);
    const char* dumppath = (argc <= 3) ? "disabled" : argv[3];

    read_env();

    // Load the .bin (CSR) graph with load_graph_from_binary() (parallel copy, not timed).
    toggle::Timer lt;
    lt.start();
    graph_t g;
    if (load_graph_from_binary(filepath, &g) != 0) return 1;
    double load_time = lt.stop();

    // Graph name = basename without extension (same rule as graph_io::Graph).
    std::string name(filepath);
    name = name.substr(0, name.find_last_of('.'));
    if (name.find_last_of('/') != std::string::npos)
        name = name.substr(name.find_last_of('/') + 1);

    std::cout << "==================================================================\n";
    std::cout << std::right << std::setw(66) << ("Graph: " + name) << "\n";
    std::cout << "Load: " << load_time << "  Dump: " << dumppath << "\n";
    std::cout << "Threads: " << NUM_THREADS << "  Rounds: " << num_rounds << "\n";

    toggle::Timer t; t.start();
    int* core = PKC(&g);
    std::cout << "    Warmup: " << t.stop() << "\n";
    double ttt = 0;
    for (uint32_t round = 0; round < num_rounds; round++) {
        free(core);
        t.start();
        core = PKC(&g);
        double tt = t.stop();
        std::cout << "    Round " << round + 1 << " time = " << tt << " sec\n";
        ttt += tt;
    }
    ttt /= num_rounds;

    std::vector<uint32_t> result(g.n);
    for(uint32_t i=0; i<g.n; i++) { result[i] = core[i]; }
    toggle::process_result(ttt, result, "..", name, "PKC", dumppath);

    free(core);
    free_graph(&g);
    return 0;
}
