source config.sh

for g in "${GRAPHS[@]}"; do
    f="${GRAPH_DIR}/${g}_sym.bin"
    [ -f "$f" ] && continue
    [[ "$g" == *Cosmo* || "$g" == *GeoLifeNoScale* || "$g" == *CHEM* ]] && d=knn || d=bin
    wget -O "$f" "https://pasgal-bs.cs.ucr.edu/${d}/${g}_sym.bin" || rm -f "$f"
done
