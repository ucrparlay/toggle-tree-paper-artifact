#include <toggle/toggle.h>

int main(int argc, char** argv) {
    std::string file_path = argv[1];
    std::string store_path = argv[2];
    std::string range = argv[3];
    std::string subfix = argv[4];
    auto G = toggle::read_graph<true, uint32_t>(file_path);
    toggle::make_random_weight(G, std::atoi(range.c_str()));
    std::string name = store_path + "/" + toggle::extract_graph_name(file_path);
    if (name.find("_sym") != std::string::npos) { name = name.substr(0, name.find("_sym")); }
    if (name.find("_wgh") != std::string::npos) { name = name.substr(0, name.find("_wgh")); }
    name += "_sym_wgh";
    if (range != "-1") { name += range; }
    else { name += "log"; }
    if (subfix == ".bin") {
        name += ".bin";
        std::cerr << "New graph name = " << name << "\n";
        toggle::write_graph(G, name.c_str());
    }
    else { std::cerr << "Error: Invalid subfix. \n"; }
    return 0;
}
