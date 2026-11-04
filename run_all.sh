#!/usr/bin/env bash
# Run the complete artifact evaluation, from the getting-started test to the
# figures. Every step can also be run on its own from the dataset/ directory;
# see README.md for what each step produces and how long it takes.
#
# The figures are redrawn after every stage, so partial results are visible
# early; a failing stage does not stop the later ones.

cd "$(dirname "${BASH_SOURCE[0]}")/dataset" || exit 1
source ./config.sh

run() { echo; echo "################ $*"; bash "$@"; }

run ./01_start.sh          # ~1 min: build everything, smoke test on a tiny graph
run ./02_download.sh       # download the 19 unweighted graphs (77 GB)
run ./03_run_fast.sh       # BFS, k-core, coloring: all implementations, all graphs
run ./draw.sh
run ./04_gen_weight.sh     # generate the weighted graphs (142 GB)
run ./05_run_slow.sh       # weighted BFS, Bellman-Ford: all implementations, all graphs
run ./draw.sh
run ./06_speedup.sh        # self-relative speedup (thread scaling) on five graphs
run ./draw.sh

# clueweb is not publicly downloadable; run it only if the graph is present.
if [[ -f "${GRAPH_DIR}/clueweb_sym.bin" && -f "${GRAPH_DIR}/clueweb_sym_wghlog.bin" ]]; then
    run ./07_run_clueweb.sh
    run ./draw.sh
else
    echo; echo "################ skipping 07_run_clueweb.sh: clueweb graphs not found in ${GRAPH_DIR}"
fi
