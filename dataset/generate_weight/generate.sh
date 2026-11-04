source ../config.sh
COMPILE ../.. || exit 1
RANGE=${1:--1}
for g in "${GRAPHS[@]}"; do
    ./test "${GRAPH_DIR}/${g}_sym.bin" "${GRAPH_DIR}" "${RANGE}" ".bin"
done
rm -f test
