source ../../../dataset/config.sh
make clean
echo "=================================================================="
echo "Compiling..."
TIMEFORMAT='Compilation time: %2R sec'
time make || exit $?
for g in "${GRAPHS[@]}"; do
    if [[ "${CLEAR_CACHE}" == "true" ]]; then
        sudo -n sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null || :
    fi
    "${NUMACTL[@]}" ./test "${GRAPH_DIR}/${g}_sym_wghlog.bin" "${NUM_ROUNDS}" ../../../dataset/sources.csv
done
