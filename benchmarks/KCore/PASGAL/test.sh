source ../../../dataset/config.sh
make clean
echo "=================================================================="
echo "Compiling..."
TIMEFORMAT='Compilation time: %2R sec'
time make || exit $?
if [[ "${CLEAR_CACHE}" == "true" ]]; then
    sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || :
fi
"${NUMACTL[@]}" ./test -i "${GRAPH_DIR}/${TEST}_sym.bin" -s -n "${NUM_ROUNDS}"
