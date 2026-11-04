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
    GALOIS_DEBUG_SKIP=1 "${NUMACTL[@]}" ./test -t=$(nproc) -algo=SyncTile -exec=PARALLEL -f "${GRAPH_DIR}/${g}_sym.bin" -n "${NUM_ROUNDS}" -S ../../../dataset/sources.csv -skipVerify
done
