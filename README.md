# Toggle Tree — Artifact

This artifact reproduces the experiments of our paper *Toggle Tree: A New Data
Structure for Efficient Parallel Graph Processing* (PPoPP 2027).
It contains the toggle tree library, our implementations of five
graph algorithms built on it, all baselines we compare against, and scripts
that run the experiments and redraw the paper's figures.

| Figure / table in the paper | Produced by | Output file |
|---|---|---|
| Fig. 3 (raw data in Tab. 4): running time of all baselines relative to ours, with geometric means per graph category | `03_run_fast.sh`, `05_run_slow.sh` | `dataset/main_exp.pdf` |
| Tab. 3 and Fig. 6 (raw data in Tab. 5): comparison with the hash bag (same algorithms, only the frontier data structure replaced) | same data as above | `dataset/main_exp_hashbag.pdf` |
| Fig. 5: self-relative speedup of ours, GBBS and PASGAL | `06_speedup.sh` | `dataset/speedup.pdf` |

Figure and table numbers refer to the paper PDF submitted with this artifact.
Fig. 4 (per-round running time of the hash bag and the toggle tree) is a
diagnostic study based on instrumented runs and is not part of this artifact.

## Changes in the submitted paper

The paper PDF submitted for artifact evaluation differs from the originally
submitted version as follows:

- We fixed the independence of the random edge-weight function (xor → min/max), chose sources from the largest connected component,
  and re-ran all experiments.
- The supplemental material was merged into the paper as Appendices A–F.

## Requirements

Note: We can provide access to a machine that satisfies all the following
requirements. Please request access through the AE chairs, who will forward
the credentials, so that evaluators remain anonymous. The same applies if you
cannot reproduce the data on your own machine.

Hard requirements (nothing runs without them):

- Linux on x86-64.
- GCC >= 11.
- `make`, `bash`, `wget`.
- Free Disk: 256 GB
- Memory: 128 GB

Optional (the scripts detect and skip them):

- `numactl`: used to interleave memory across NUMA nodes. Without it results on multi-socket machines may be noisier.
- Passwordless `sudo`: used to drop the page cache. Without it results on machines with memory < 1 TB may be noisier.
- Python 3.8+ with `matplotlib` and `numpy`: used to draw figures.

## Configuration

Edit `dataset/config.sh` before running anything:

```bash
GRAPH_DIR=/data/graphs/bin   # where the graphs are downloaded to / read from (256 GB)
NUM_ROUNDS=5                 # timed runs per test (the paper's setting)
CLEAR_CACHE=true             # drop the page cache before each test (needs sudo)
```

## Getting started (about 1 minute)

```bash
cd dataset
bash ./01_start.sh
```

This compiles all 23 benchmark programs, runs each once on a tiny
graph, as a smoke test.

## Full evaluation

Run everything with one command (from the repository root):

```bash
bash ./run_all.sh
```

or step by step from `dataset/`. The steps are ordered so that the most data
is available earliest; the figures can be redrawn at any point with
`bash ./draw.sh`, which draws whatever data exists.

| Step | What it does | Approx. time (96-core machine) |
|---|---|---|
| `01_start.sh` | smoke test | 1 min |
| `02_download.sh` | Download the 19 unweighted graphs into `GRAPH_DIR` | 77 GB download, depends on network bandwidth |
| `03_run_fast.sh` | BFS (7 implementations), k-core (7), coloring (3) on all 19 graphs | 1 h |
| `draw.sh` | Main figure and hash bag figure with three of five algorithms | seconds |
| `04_gen_weight.sh` | Generate the weighted graphs (`*_sym_wghlog.bin`, 142 GB) | 20 min, mostly disk I/O |
| `05_run_slow.sh` | Weighted BFS (3 implementations) and Bellman-Ford (3) on all 19 graphs | 5 h |
| `draw.sh` | Both figures complete | seconds |
| `06_speedup.sh` | Thread scaling of ours, GBBS, PASGAL on five graphs | 1 h |
| `draw.sh` | Speedup figure | seconds |

Total: about 7 hours plus the download. Times for 03 and 05 are derived from
the measured running times and scale with the machine; fewer cores means
proportionally longer. Step 05 is dominated by the road and k-NN graphs (`planet` through `CHEM_5` in the graph list). To shorten it, comment those graphs out in `dataset/config.sh` before running this step.

Results are stored as CSV files, one per algorithm:
`benchmarks/<Algorithm>/benchmark.csv` (average running time in seconds, one
column per implementation) and `verify.csv` (result hashes, which must agree
across implementations). The
figure scripts compute all derived numbers (relative times, geometric means,
speedups) from these files.

Each `benchmarks/<Algorithm>/<Implementation>/` directory can also be run on
its own: `bench.sh` runs all graphs, `test.sh` runs the single graph named by
`TEST` in `config.sh`.

## Interpreting the results

- Main figure: bars are the running time of each baseline divided by ours, so
  bars above the red line at 1 mean we are faster. Bars are clipped at 5. The
  tables on the right give geometric means per category (S: social, W: web,
  R: road, K: k-NN and synthetic) and over all graphs.
- Hash bag figure: same layout, comparing our frontier data structure against
  the PASGAL hash bag inside otherwise identical code.
- Speedup figure: running time with one thread divided by running time with
  *t* threads, for *t* on the x axis, computed per implementation.
- Absolute times depend on the machine; the claims of the paper concern the
  relative numbers (the ratios, geometric means and the shape of the speedup
  curves). We consider deviations of up to about 20 % in individual ratios
  normal across machines. Graphs that cannot be run (for example because of
  memory) are left empty and excluded from the geometric means of that
  algorithm; `draw.sh` works with any subset of the data.

## The clueweb graph

The paper's 20th graph, clueweb (978 M vertices, 74.7 B undirected edges), is
the ClueWeb12 web graph published by the Laboratory for Web Algorithmics
(https://law.di.unimi.it/webdata/clueweb12/). It is public, but only in the
compressed WebGraph format (about 12 GB), and the host of the other graphs,
pasgal-bs.cs.ucr.edu, does not carry it. Running on it needs more than 1.5 TB of memory, and takes around another 5 hours.

If you want to evaluate this graph, please request access through the AE
chairs: we can provide access to a machine that has the converted files and
sufficient memory.

## Reusing and adapting the artifact

**Running on your own graph.** The programs read the binary CSR format used by
GBBS and PASGAL: three 64-bit values *n*, *m*, file size, then *n*+1 64-bit
offsets and *m* 32-bit neighbour ids (weighted graphs add a 32-bit weight per
edge). Put `<name>_sym.bin` into `GRAPH_DIR`, add `<name>` to the `GRAPHS`
list in `dataset/config.sh`, and the whole pipeline picks it up:
`04_gen_weight.sh` generates `<name>_sym_wghlog.bin`, `generate_source/`
appends random source vertices to `sources.csv`, every `bench.sh` runs it, and
the figures get a new column once you add its name and category to the graph
lists at the top of `dataset/plot.py`. `read_graph<Symmetrized, Weight>`
symmetrizes a directed input on the fly if the template argument asks for it,
so directed `.bin` files work as well. To run a single graph, set `TEST` in
`config.sh` and use `test.sh` instead of `bench.sh`.

**Changing the configuration.** Thread count: the benchmarks use the ParlayLib
scheduler, so `PARLAY_NUM_THREADS=16 bash ./bench.sh` runs on 16 threads
(`06_speedup.sh` does exactly this). Number of timed rounds: `NUM_ROUNDS` in
`config.sh`. Sources: edit `sources.csv` or regenerate it with
`generate_source/generate.sh`. Edge weights: the default `generate_weight/generate.sh -1` draws integer
weights uniformly from [1, ⌊log2 *n*⌋ + 1] (the `wghlog` suffix); `generate.sh R`
draws them from [1, 2^*R*] instead.
Cache clearing: `CLEAR_CACHE=false`. Compiler: `CXX` in `config.sh`.

**Using the toggle tree in your own code.** The library is header-only:
`#include <toggle/toggle.h>` and compile with
`g++ -std=c++20 -O3 -march=native -pthread -I include` (add
`-I baselines/parlaylib/include -DTOGGLE_TREE_USE_PARLAY` to use the ParlayLib
scheduler, as the experiments do; otherwise the built-in scheduler is used and
`TOGGLE_NUM_WORKERS` sets its thread count). The two data structures are
`toggle::IndexSet` (a set of vertex ids with `insert`, `remove`, `contains`,
`for_each`, `consume_each`, `reduce`, `reduce_size`, `empty`) and
`toggle::IndexMap` (a set keyed by a value sequence with `insert`, `repair` and
`extract_prefix_min`, used by the weighted algorithms).

## Repository layout

```
include/toggle/         the toggle tree library (header-only)
benchmarks/<Algo>/<Impl>/  one directory per algorithm and implementation (bench.sh, test.sh)
baselines/              vendored baselines: GBBS, PASGAL, GAPBS, Galois, ParlayLib
dataset/                configuration, experiment scripts, figure scripts, sources.csv
run_all.sh              runs every step in order
```

## Archival version (DOI)

The permanent archival record of this artifact is
[10.5281/zenodo.24042824](https://doi.org/10.5281/zenodo.24042824).
It is archived from a separate repository, which differs from this one:

- **Archived repository (DOI):** contains only the header-only toggle tree
  library. It is developed entirely on the C++20 standard library and has no required third-party dependencies (ParlayLib can optionally be used as the scheduler).
- **This repository (used for artifact evaluation):** additionally contains
  the baselines we compare against and the one-command scripts that run all
  experiments and reproduce the paper's figures.

## License

MIT, see `LICENSE`. Third-party code under `baselines/` keeps its own license.
