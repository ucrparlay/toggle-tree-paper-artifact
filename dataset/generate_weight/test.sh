source ../config.sh
COMPILE ../.. || exit 1
RANGE=${1:--1}
./test "${GRAPH_DIR}/${TEST}_sym.bin" "${GRAPH_DIR}" "${RANGE}" ".bin"
rm -f test
