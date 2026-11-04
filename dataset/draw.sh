#!/usr/bin/env bash
# Draw whichever figures have data available:
#   main_exp.pdf          <- benchmarks/<Algo>/benchmark.csv   (plot.py)
#   main_exp_hashbag.pdf  <- benchmarks/<Algo>/benchmark.csv   (plot_hashbag.py)
#   speedup.pdf           <- dataset/speedup.csv               (plot_speedup.py)

set -u
cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
DATASET_DIR=$(pwd)
ROOT=$(cd .. && pwd)
VENV="${DATASET_DIR}/.venv"

# ---- decide what can be drawn --------------------------------------------
MAIN_ALGOS=(BFS KCore wBFS BellmanFord Coloring)
HASHBAG_ALGOS=(BFS KCore BellmanFord Coloring)
has_csv() { local a; for a in "$@"; do [[ -f "${ROOT}/benchmarks/${a}/benchmark.csv" ]] && return 0; done; return 1; }

SCRIPTS=()
has_csv "${MAIN_ALGOS[@]}"     && SCRIPTS+=(plot.py)
has_csv "${HASHBAG_ALGOS[@]}"  && SCRIPTS+=(plot_hashbag.py)
[[ -f "${DATASET_DIR}/speedup.csv" ]] && SCRIPTS+=(plot_speedup.py)

if (( ${#SCRIPTS[@]} == 0 )); then
    echo "No data found: neither benchmarks/*/benchmark.csv nor dataset/speedup.csv exists."
    echo "Run the benchmarks first (see README), then re-run this script."
    exit 0
fi
echo "Figures to draw: ${#SCRIPTS[@]} (${SCRIPTS[*]})"

# ---- pick a python with matplotlib + numpy -------------------------------
has_mpl() { "$1" -c "import matplotlib, numpy" >/dev/null 2>&1; }
py_minor() { "$1" -c "import sys; print(sys.version_info[1])" 2>/dev/null || echo 0; }

# Candidate interpreters, newest first. The plain "python3" may be an old
# release (e.g. 3.6 on RHEL 8) for which current matplotlib has no wheels.
CANDIDATES=(python3.13 python3.12 python3.11 python3.10 python3.9 python3.8 python3)

PY=""
if [[ -z "${PLOT_FORCE_VENV:-}" ]]; then   # PLOT_FORCE_VENV=1 is a testing hook
    for c in "${CANDIDATES[@]}"; do
        command -v "$c" >/dev/null 2>&1 && has_mpl "$c" && { PY="$c"; break; }
    done
fi

if [[ -z "${PY}" ]]; then
    if [[ -x "${VENV}/bin/python" ]] && has_mpl "${VENV}/bin/python"; then
        PY="${VENV}/bin/python"
    else
        BASE=""
        for c in "${CANDIDATES[@]}"; do
            command -v "$c" >/dev/null 2>&1 && (( $(py_minor "$c") >= 8 )) && { BASE="$c"; break; }
        done
        if [[ -z "${BASE}" ]]; then
            echo "No python3 >= 3.8 found; matplotlib needs at least 3.8."
            exit 1
        fi
        echo "matplotlib/numpy not found; creating ${VENV} with ${BASE} ($("${BASE}" --version 2>&1)) ..."
        rm -rf "${VENV}"
        if ! "${BASE}" -m venv "${VENV}"; then
            rm -rf "${VENV}"
            echo "Failed to create a virtual environment."
            echo "On Debian/Ubuntu install it with:  sudo apt install python3-venv"
            echo "or install the packages system-wide: sudo apt install python3-matplotlib python3-numpy"
            exit 1
        fi
        # An old pip cannot select modern binary wheels; upgrade it first.
        "${VENV}/bin/python" -m pip install --quiet --disable-pip-version-check --upgrade pip >/dev/null 2>&1 || true
        if ! "${VENV}/bin/python" -m pip install --quiet --disable-pip-version-check --only-binary=:all: matplotlib numpy; then
            rm -rf "${VENV}"
            echo "pip install failed (network access is required the first time)."
            exit 1
        fi
        PY="${VENV}/bin/python"
    fi
fi
echo "Using python: ${PY} ($("${PY}" --version 2>&1), matplotlib $("${PY}" -c 'import matplotlib; print(matplotlib.__version__)'))"

# ---- draw -----------------------------------------------------------------
status=0
for script in "${SCRIPTS[@]}"; do
    echo "------------------------------------------------------------------"
    echo "Running ${script}"
    "${PY}" "${DATASET_DIR}/${script}" || { echo "${script} failed"; status=1; }
done
echo "=================================================================="
ls -1 "${DATASET_DIR}"/*.pdf 2>/dev/null | sed 's|^|Output: |'
exit "${status}"
