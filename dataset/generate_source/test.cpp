#include <toggle/toggle.h>

#ifndef GRAPH_SYMMETRIZED
#define GRAPH_SYMMETRIZED true
#endif

#ifndef GRAPH_WEIGHT_TYPE
#define GRAPH_WEIGHT_TYPE void
#endif

int main(int argc, char** argv) {
    std::string file_path = argv[1];
    std::string graph_name = toggle::extract_graph_name(file_path);
    uint32_t num_rounds = (argc <= 2) ? 5 : std::atoi(argv[2]);

    std::cout << "==================================================================\n";
    std::cout << std::right << std::setw(66) << ("Graph: " + graph_name) << "\n";
    auto G = toggle::read_graph<GRAPH_SYMMETRIZED, GRAPH_WEIGHT_TYPE>(file_path);

    std::vector<uint32_t> sources;
    auto cc_label = toggle::Array<uint32_t>(G.n(), [&](uint64_t i) { return i; });
    toggle::parallel_for(0, G.n(), [&](uint64_t s) {
        toggle::parallel_for(G.out_offset(s), G.out_offset(s+1), [&] (size_t j){
            uint64_t d = G.out_edge(j);
            if (s < d) toggle::unite(cc_label, s, d);
        }, 256);
    }, 1);
    toggle::parallel_for(0, G.n(), [&](uint64_t s) {
        cc_label[s] = toggle::find(cc_label, s);
    }, 1024);
    auto sorted_label = toggle::Array<uint32_t>(cc_label);
    toggle::parallel_sort(sorted_label);
    auto run_start = toggle::scan<toggle::max, uint32_t(0)>(0, G.n(), [&](uint64_t i) {
        return (i == 0 || sorted_label[i] != sorted_label[i - 1]) ? uint32_t(i) : 0u;
    });
    uint32_t most_label = toggle::lo32(toggle::reduce<toggle::max, uint64_t(0)>(0, G.n(), [&](uint64_t i) {
        bool run_end = i + 1 == G.n() || sorted_label[i + 1] != sorted_label[i];
        return run_end ? toggle::glue32(i + 1 - run_start[i + 1], sorted_label[i]) : 0;
    }));
    for (uint32_t i = 0; sources.size() < num_rounds && i < G.n(); i++) {
        uint32_t s = toggle::hash32(i) % G.n();
        if (cc_label[s] == most_label) sources.push_back(s);
    }

    std::cout << "sources: ";
    for (uint32_t i = 0; i < sources.size(); i++) {
        std::cout << sources[i] << " ";
        toggle::internal::update_csv_cell("../sources.csv", graph_name, "source_" + std::to_string(i + 1), std::to_string(sources[i]));
        toggle::internal::update_csv_cell("../sources.csv", graph_name, "source_" + std::to_string(i + 1), std::to_string(sources[i]));
    }
    std::cout << "\n";

    return 0;
}

