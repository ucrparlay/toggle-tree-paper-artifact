set -u
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
DATASET_DIR=$(pwd)
ROOT=$(cd .. && pwd)
source "${DATASET_DIR}/config.sh"

OUT="${OUT:-${DATASET_DIR}/speedup.csv}"

# Graphs used in the paper's speedup figure.
if [[ -n "${SPEEDUP_GRAPHS:-}" ]]; then
    read -r -a SPEEDUP_GRAPHS <<< "${SPEEDUP_GRAPHS}"
else
    SPEEDUP_GRAPHS=(com-orkut soc-LiveJournal1 indochina africa RoadUSA)
fi

# Thread counts. Default: the paper's list (1 2 4 12 24 48 96 192) clipped to
# the number of hardware threads on this machine, plus that number itself.
if [[ -n "${THREADS:-}" ]]; then
    read -r -a THREADS <<< "${THREADS}"
else
    MAX_THREADS=$(nproc)
    THREADS=()
    for t in 1 2 4 12 24 48 96 192; do
        if (( t < MAX_THREADS )); then THREADS+=("${t}"); fi
    done
    THREADS+=("${MAX_THREADS}")
fi

# algorithm  system  directory under benchmarks/  (what the paper plots)
TARGETS=(
    "BFS      Ours    BFS/Ours+VGC"
    "KCore    Ours    KCore/Ours+VGC+SPL"
    "Coloring Ours    Coloring/Ours"
    "BFS      GBBS    BFS/GBBS"
    "KCore    GBBS    KCore/GBBS"
    "Coloring GBBS    Coloring/GBBS"
    "BFS      PASGAL  BFS/PASGAL"
    "KCore    PASGAL  KCore/PASGAL"
)

SCRATCH_BENCH="${ROOT}/benchmark.csv"
SCRATCH_VERIFY="${ROOT}/verify.csv"
BIN="${DATASET_DIR}/speedup_test"

cleanup() { rm -f "${BIN}" "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"; }
trap cleanup EXIT

# Build the binary of one target in its own directory and copy it here.
build_target() {
    local dir="${ROOT}/benchmarks/${1}"
    local system="${2}"
    echo "=================================================================="
    echo "Compiling ${1} ..."
    rm -f "${BIN}"
    if [[ "${system}" == "Ours" ]]; then
        (cd "${dir}" && COMPILE "${ROOT}" true void) || return 1
        cp "${dir}/test" "${BIN}" && rm -f "${dir}/test"
    else
        (cd "${dir}" && make clean >/dev/null && make) || return 1
        cp "${dir}/test" "${BIN}" && (cd "${dir}" && make clean >/dev/null)
    fi
}

# Command line for one run; reads graph path from $1.
run_args() {
    local graph="${1}" system="${2}" algo="${3}"
    local sources="${DATASET_DIR}/sources.csv"
    case "${system}" in
        Ours)   echo "${graph} ${NUM_ROUNDS} ${sources}" ;;
        GBBS)   if [[ "${algo}" == "BFS" ]]; then
                    echo "-num_rounds ${NUM_ROUNDS} -S ${sources} -s -b ${graph}"
                else
                    echo "-num_rounds ${NUM_ROUNDS} -s -b ${graph}"
                fi ;;
        PASGAL) if [[ "${algo}" == "BFS" ]]; then
                    echo "-s -i ${graph} -n ${NUM_ROUNDS} -S ${sources}"
                else
                    echo "-s -i ${graph} -n ${NUM_ROUNDS}"
                fi ;;
    esac
}

# Read the single cell (row = graph, the only non-graph column) from the
# scratch benchmark.csv written by the binary.
read_time() {
    local graph="${1}"
    [[ -f "${SCRATCH_BENCH}" ]] || { echo ""; return; }
    awk -F, -v g="${graph}" 'NR > 1 && $1 == g { for (i = 2; i <= NF; i++) if ($i != "") { print $i; exit } }' "${SCRATCH_BENCH}"
}

echo "Output file : ${OUT}"
echo "Threads     : ${THREADS[*]}"
echo "Graphs      : ${SPEEDUP_GRAPHS[*]}"
echo "Rounds      : ${NUM_ROUNDS}"

if [[ ! -f "${OUT}" ]]; then
    { printf 'algorithm,system,graph'; printf ',%s' "${THREADS[@]}"; printf '\n'; } > "${OUT}"
fi

rm -f "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"
for target in "${TARGETS[@]}"; do
    read -r algo system dir <<< "${target}"
    build_target "${dir}" "${system}" || { echo "Compilation failed for ${dir}, skipping."; continue; }
    for g in "${SPEEDUP_GRAPHS[@]}"; do
        graph_file="${GRAPH_DIR}/${g}_sym.bin"
        graph_name="${g}_sym"
        if [[ ! -f "${graph_file}" ]]; then
            echo "Graph ${graph_file} not found, skipping."
            continue
        fi
        row="${algo},${system},${graph_name}"
        for t in "${THREADS[@]}"; do
            echo "------------------------------------------------------------------"
            echo "${algo} / ${system} / ${graph_name} / ${t} threads"
            if [[ "${CLEAR_CACHE}" == "true" ]]; then
                sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || :
            fi
            rm -f "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"
            # shellcheck disable=SC2046
            PARLAY_NUM_THREADS="${t}" "${NUMACTL[@]}" "${BIN}" $(run_args "${graph_file}" "${system}" "${algo}")
            time_sec=$(read_time "${graph_name}")
            rm -f "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"
            if [[ -z "${time_sec}" ]]; then
                echo "Warning: no result recorded for ${algo}/${system}/${graph_name}/${t}"
            fi
            row="${row},${time_sec}"
        done
        echo "${row}" >> "${OUT}"
        echo ">> ${row}"
    done
    rm -f "${BIN}"
done

echo "=================================================================="
echo "Done. Results in ${OUT}"
