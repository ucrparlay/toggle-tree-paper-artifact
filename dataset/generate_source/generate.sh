ROOT=../..
source "${ROOT}/dataset/config.sh"
RUN_BENCH_GRAPH "${ROOT}" true void _sym.bin
RUN_BENCH_GRAPH "${ROOT}" true uint32_t _sym_wghlog.bin
