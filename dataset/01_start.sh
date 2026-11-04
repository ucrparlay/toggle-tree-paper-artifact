#!/usr/bin/env bash
# Getting-started test (about a minute): build every benchmark target in
# parallel, run each one on the tiny HepPh graph, and print one table with
# the running times and result hashes.

set -u
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
DATASET_DIR=$(pwd)
ROOT=$(cd .. && pwd)
source "${DATASET_DIR}/config.sh"

GRAPH_DIR="${DATASET_DIR}/small_graph"
GRAPH="${GRAPH:-HepPh}"
BIN_DIR="${DATASET_DIR}/get_started_bin"
JOBS="${JOBS:-$(nproc)}"
SOURCES="${DATASET_DIR}/sources.csv"
SCRATCH_BENCH="${ROOT}/benchmark.csv"
SCRATCH_VERIFY="${ROOT}/verify.csv"

# Algorithms in the order of the paper's figure, and the paper's own
# implementation of each one (other implementations' result hashes are
# compared against it).
ALGORITHMS=(BFS KCore wBFS BellmanFord Coloring)
declare -A REFERENCE=(
    [BFS]="Ours+VGC" [KCore]="Ours+VGC+SPL" [wBFS]="Ours-IM" [BellmanFord]="Ours" [Coloring]="Ours"
)
# Implementations whose result encoding legitimately differs from the reference.
declare -A EXPECTED_DIFFERENT=( ["BFS/GAPBS"]="GAPBS reports a BFS parent array instead of depths" )

cleanup() { rm -rf "${BIN_DIR}" "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"; }
trap cleanup EXIT

# ---- inputs (the small graph and its sources ship with the repository) ----
for f in "${GRAPH_DIR}/${GRAPH}_sym.bin" "${GRAPH_DIR}/${GRAPH}_sym_wghlog.bin" "${SOURCES}"; do
    [[ -f "$f" ]] || { echo "Missing ${f}"; exit 1; }
done

# ---- discover targets, grouped by algorithm, reference implementation first
TARGETS=()   # "Algo/Implementation"
for algo in "${ALGORITHMS[@]}" $(ls "${ROOT}/benchmarks"); do
    [[ -d "${ROOT}/benchmarks/${algo}" ]] || continue
    [[ " ${TARGETS[*]} " == *" ${algo}/"* ]] && continue      # already added
    ref="${REFERENCE[${algo}]:-}"
    [[ -n "${ref}" && -f "${ROOT}/benchmarks/${algo}/${ref}/bench.sh" ]] && TARGETS+=("${algo}/${ref}")
    for d in "${ROOT}/benchmarks/${algo}"/*/; do
        d=${d%/}; impl=$(basename "$d")
        [[ -f "$d/bench.sh" && "${impl}" != "${ref}" ]] && TARGETS+=("${algo}/${impl}")
    done
done
echo "Targets      : ${#TARGETS[@]}"
echo "Graph        : ${GRAPH} (${GRAPH_DIR})"
echo "Rounds       : ${NUM_ROUNDS}"
echo "Build jobs   : ${JOBS}"

# ---- build all targets in parallel ---------------------------------------
build_one() {   # build_one Algo/Impl  -> ${BIN_DIR}/Algo__Impl (+ .build.log on failure)
    local target="$1" dir="${ROOT}/benchmarks/$1" name="${1/\//__}"
    local log="${BIN_DIR}/${name}.build.log" start=$SECONDS
    source "${ROOT}/dataset/config.sh" >/dev/null 2>&1
    if [[ -f "${dir}/Makefile" ]]; then
        ( cd "${dir}" && make clean >/dev/null 2>&1; make >"${log}" 2>&1 )
    else
        local args; args=$(grep -m1 'RUN_BENCH_GRAPH' "${dir}/bench.sh")
        local sym wt; sym=$(awk '{print $3}' <<<"${args}"); wt=$(awk '{print $4}' <<<"${args}")
        ( cd "${dir}" && COMPILE "${ROOT}" "${sym}" "${wt}" >"${log}" 2>&1 )
    fi
    if [[ -x "${dir}/test" ]]; then
        mv "${dir}/test" "${BIN_DIR}/${name}" && rm -f "${log}"
        echo "built   $(( SECONDS - start ))s  ${target}"
    else
        echo "FAILED  $(( SECONDS - start ))s  ${target}  (see ${log})"
    fi
}
export -f build_one
export ROOT BIN_DIR

rm -rf "${BIN_DIR}"; mkdir -p "${BIN_DIR}"
echo "=================================================================="
echo "Building ${#TARGETS[@]} targets with ${JOBS} parallel jobs ..."
build_start=$SECONDS
printf '%s\n' "${TARGETS[@]}" | xargs -P "${JOBS}" -I{} bash -c 'build_one "$@"' _ {}
echo "Build finished in $(( SECONDS - build_start ))s"

# ---- run each target once -------------------------------------------------
# Build the command line from the target's own bench.sh so that every
# implementation is invoked exactly as in the full benchmark.
run_command() {   # run_command Algo/Impl BIN  -> prints the command to eval
    local dir="${ROOT}/benchmarks/$1" bin="$2" line
    line=$(grep -m1 'NUMACTL\[@\]' "${dir}/bench.sh" | sed 's/^ *//')
    if [[ -n "${line}" ]]; then
        line=${line//\.\/test/\"${bin}\"}
        line=${line//..\/..\/..\/dataset\/sources.csv/${SOURCES}}
        line=${line//\$\{g\}/${GRAPH}}
        echo "${line}"
    else
        local subfix; subfix=$(grep -m1 'RUN_BENCH_GRAPH' "${dir}/bench.sh" | awk '{print $5}')
        echo "\"\${NUMACTL[@]}\" \"${bin}\" \"\${GRAPH_DIR}/${GRAPH}${subfix}\" \"\${NUM_ROUNDS}\" \"${SOURCES}\""
    fi
}
read_cell() {   # read_cell csv graph -> first non-empty value in the graph's row
    [[ -f "$1" ]] || return 0
    awk -F, -v g="$2" 'NR > 1 && $1 == g { for (i = 2; i <= NF; i++) if ($i != "") { print $i; exit } }' "$1"
}

echo "=================================================================="
echo "Running ..."
ROWS=()          # "algo|impl|graph|time|hash|same|status"
declare -A HASH_OF
run_start=$SECONDS
for target in "${TARGETS[@]}"; do
    algo=${target%%/*}; impl=${target#*/}; bin="${BIN_DIR}/${target/\//__}"
    graph_name="${GRAPH}_sym"; grep -q "wghlog" "${ROOT}/benchmarks/${target}/bench.sh" && graph_name="${GRAPH}_sym_wghlog"
    status=OK; time_sec=""; hash=""
    if [[ ! -x "${bin}" ]]; then
        status=BUILD_FAIL
    else
        rm -f "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"
        cmd=$(run_command "${target}" "${bin}")
        eval "${cmd}" >/dev/null 2>&1 || status=RUN_FAIL
        time_sec=$(read_cell "${SCRATCH_BENCH}" "${graph_name}")
        hash=$(read_cell "${SCRATCH_VERIFY}" "${graph_name}")
        rm -f "${SCRATCH_BENCH}" "${SCRATCH_VERIFY}"
        [[ -z "${time_sec}" && "${status}" == OK ]] && status=NO_RESULT
    fi
    same=""
    if [[ "${impl}" == "${REFERENCE[${algo}]:-}" ]]; then
        HASH_OF[${algo}]="${hash}"; same="reference"
    elif [[ -n "${hash}" && -n "${HASH_OF[${algo}]:-}" ]]; then
        if [[ "${hash}" == "${HASH_OF[${algo}]}" ]]; then same="yes"
        elif [[ -n "${EXPECTED_DIFFERENT[${target}]:-}" ]]; then same="no (expected)"
        else same="NO"; fi
    fi
    ROWS+=("${algo}|${impl}|${graph_name}|${time_sec}|${hash}|${same}|${status}")
    printf '  %-12s %-14s %-8s %s\n' "${algo}" "${impl}" "${status}" "${time_sec:+${time_sec}s}"
done
echo "Runs finished in $(( SECONDS - run_start )) s"

# ---- table ----------------------------------------------------------------
FMT='%-12s %-15s %-18s %10s  %-9s %-14s %s\n'
echo "=================================================================="
echo
printf "${FMT}" algorithm implementation graph time_sec hash same_as_ours status
printf "${FMT}" ------------ --------------- ------------------ ---------- --------- -------------- ------
ok=0; mismatch=0
for row in "${ROWS[@]}"; do
    IFS='|' read -r algo impl graph_name time_sec hash same status <<<"${row}"
    printf "${FMT}" "${algo}" "${impl}" "${graph_name}" "${time_sec}" "${hash}" "${same}" "${status}"
    [[ "${status}" == OK ]] && ok=$((ok + 1))
    [[ "${same}" == NO ]] && mismatch=$((mismatch + 1))
done
echo
for target in "${!EXPECTED_DIFFERENT[@]}"; do echo "Note: ${EXPECTED_DIFFERENT[${target}]} (${target})."; done
echo "${ok}/${#ROWS[@]} targets ran successfully; ${mismatch} unexpected result mismatch(es)."
[[ "${ok}" -eq "${#ROWS[@]}" && "${mismatch}" -eq 0 ]]
