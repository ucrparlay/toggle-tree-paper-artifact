import argparse
import os
import re
from pathlib import Path
from typing import Dict, List, Optional

# Font sizes (edit these values to resize all text in the figure).
TITLE_FONT_SIZE = 26
LABEL_FONT_SIZE = 26
TICK_FONT_SIZE = 26
LEGEND_FONT_SIZE = 26

"""Plot the self-relative speedup figure from dataset/speedup.csv.

The CSV is produced by ``06_speedup.sh``: one row per (algorithm, system, graph),
one column per thread count, values in seconds. Speedup is computed relative
to the single-thread time of the same row. The drawing code is identical to
the paper's ``experiments/speedup.py``.
"""

import csv

SCRIPT_DIR = Path(__file__).resolve().parent
CSV_PATH = SCRIPT_DIR / "speedup.csv"
OUT_DIR = SCRIPT_DIR

import matplotlib.pyplot as plt
import logging
# Newer matplotlib logs a warning when generic families such as "cursive" are
# absent; it is harmless noise for these figures.
logging.getLogger("matplotlib.font_manager").setLevel(logging.ERROR)
# "STIXGeneral" ships with matplotlib; the paper used "Linux Libertine O".
FONT_FAMILY = "STIXGeneral"
plt.rcParams["font.family"] = FONT_FAMILY
plt.rcParams["mathtext.fontset"] = "custom"
plt.rcParams["mathtext.rm"] = FONT_FAMILY
plt.rcParams["mathtext.it"] = f"{FONT_FAMILY}:italic"
plt.rcParams["mathtext.bf"] = f"{FONT_FAMILY}:bold"

# Thread counts; filled from the CSV header when it is read.
CORES: List[int] = [1, 2, 4, 12, 24, 48, 96, 192]
# algorithm / system names in speedup.csv -> names used by the plotting code
ALGO_KEYS = {"KCore": "kcore", "BFS": "bfs", "Coloring": "coloring"}
SYSTEM_KEYS = {"Ours": "ToT", "GBBS": "GBBS", "PASGAL": "PASGAL"}
TASKS = ["kcore", "bfs", "coloring"]
TASK_LABELS = {"kcore": r"$k$-core", "bfs": "BFS", "coloring": "Coloring"}
SYSTEM_TASKS = {
    "ToT": ["kcore", "bfs", "coloring"],
    "GBBS": ["kcore", "bfs", "coloring"],
    "PASGAL": ["kcore", "bfs"],
}
SYSTEM_RENDER_ORDER = ["GBBS", "PASGAL", "ToT"]
SYSTEM_STYLES = {
    "ToT": {"marker": "o", "color": "#d62728"},
    "GBBS": {"marker": "s", "color": "#6baed6"},
    "PASGAL": {"marker": "^", "color": "#b276b2"},
}
ABBREV = {
    "twitter_sym": "TW",
    "friendster_sym": "FS",
    "com-orkut_sym": "OK",
    "soc-LiveJournal1_sym": "LJ",
    "socfb-konect_sym": "FK",
    "socfb-uci-uni_sym": "FU",
    "eu-2015-host_sym": "EH",
    "clueweb_sym": "CW",
    "sd_arc_sym": "SD",
    "enwiki-2023_sym": "EW",
    "arabic_sym": "AR",
    "uk-2002_sym": "UK",
    "indochina_sym": "IC",
    "GeoLifeNoScale_10_sym": "GL10",
    "CHEM_5_sym": "CH5",
    "Cosmo50_5_sym": "COS5",
    "planet_sym": "PL",
    "europe_sym": "EU",
    "asia_sym": "AS",
    "north-america_sym": "NA",
    "africa_sym": "AF",
    "RoadUSA_sym": "RU",
    "Germany_sym": "DE",
    "hugebubbles-00020_sym": "BBL",
    "hugetrace-00020_sym": "TRCE",
}


def display_graph_name(graph: str) -> str:
    return ABBREV.get(graph, graph)


def collect_all_data(csv_path: Path) -> Dict[str, Dict[str, Dict[str, List[float]]]]:
    """Read speedup.csv and return {system: {task: {graph: [speedup per core]}}}."""
    global CORES
    all_data: Dict[str, Dict[str, Dict[str, List[float]]]] = {
        system: {task: {} for task in tasks} for system, tasks in SYSTEM_TASKS.items()
    }
    with csv_path.open(newline="", encoding="utf-8") as file:
        reader = csv.reader(file)
        header = next(reader)
        CORES = [int(value) for value in header[3:]]
        for row in reader:
            if len(row) < 4:
                continue
            algo, system, graph = row[0].strip(), row[1].strip(), row[2].strip()
            task = ALGO_KEYS.get(algo)
            system_key = SYSTEM_KEYS.get(system)
            if task is None or system_key is None or task not in all_data[system_key]:
                continue
            times = []
            for value in row[3:]:
                try:
                    times.append(float(value))
                except ValueError:
                    times.append(float("nan"))
            base = times[0]
            if not base or base != base:
                print(f"Warning: no single-thread time for {algo}/{system}/{graph}; skipped")
                continue
            all_data[system_key][task][graph] = [base / t if t and t == t else float("nan") for t in times]
    return all_data


def get_graph_order(all_data: Dict[str, Dict[str, Dict[str, List[float]]]]) -> List[str]:
    for system in SYSTEM_TASKS:
        for task in TASKS:
            task_data = all_data.get(system, {}).get(task)
            if task_data:
                return list(task_data.keys())
    return []


def lookup_values(
    all_data: Dict[str, Dict[str, Dict[str, List[float]]]],
    system: str,
    task: str,
    graph: str,
) -> Optional[List[float]]:
    return all_data.get(system, {}).get(task, {}).get(graph)


def draw_speedup_grid(
    all_data: Dict[str, Dict[str, Dict[str, List[float]]]],
    output_path: Path,
) -> None:
    graphs = get_graph_order(all_data)[:5]
    num_rows = len(TASKS)
    num_cols = len(graphs)
    fig, axes = plt.subplots(num_rows, num_cols, sharex=True, sharey=True, figsize=(11.5, 7.2))

    legend_handles = {}
    x_ticks = sorted({1, 12 if 12 in CORES else CORES[len(CORES) // 2], CORES[-1]})
    x_grid_ticks = [core for core in CORES if core not in x_ticks]
    y_ticks = [1, 4, 16, 64]

    for row_idx, task in enumerate(TASKS):
        for col_idx, graph in enumerate(graphs):
            ax = axes[row_idx][col_idx]
            for system in SYSTEM_RENDER_ORDER:
                values = lookup_values(all_data, system, task, graph)
                if values is None:
                    continue

                style = SYSTEM_STYLES[system]
                (line,) = ax.plot(
                    CORES,
                    values,
                    label=system,
                    marker=style["marker"],
                    color=style["color"],
                    linewidth=1.8,
                    markersize=5,
                )
                legend_handles.setdefault(system, line)

            try:  # matplotlib >= 3.3
                ax.set_xscale("log", base=2)
                ax.set_yscale("log", base=2)
            except (ValueError, TypeError):  # older matplotlib uses basex/basey
                ax.set_xscale("log", basex=2)
                ax.set_yscale("log", basey=2)
            ax.set_xticks(x_ticks)
            ax.set_xticks(x_grid_ticks, minor=True)
            ax.set_yticks(y_ticks)
            ax.grid(True, axis="y", which="both", linestyle="--", linewidth=0.5, alpha=0.35)
            ax.grid(True, axis="x", which="both", linestyle="--", linewidth=0.5, alpha=0.35)
            ax.tick_params(axis="both", which="major", labelsize=TICK_FONT_SIZE)
            ax.xaxis.get_offset_text().set_fontsize(TICK_FONT_SIZE)
            ax.yaxis.get_offset_text().set_fontsize(TICK_FONT_SIZE)
            ax.tick_params(axis="x", which="minor", bottom=False, labelbottom=False)

            if row_idx == 0:
                ax.set_title(display_graph_name(graph), fontsize=TITLE_FONT_SIZE)
            if col_idx == 0:
                ax.set_ylabel(
                    TASK_LABELS.get(task, task),
                    fontsize=LABEL_FONT_SIZE,
                    fontstyle="italic",
                )
            if row_idx == num_rows - 1:
                ax.set_xticklabels(
                    [str(tick) for tick in x_ticks], fontsize=TICK_FONT_SIZE, rotation=0
                )
            else:
                ax.tick_params(labelbottom=False)

            if col_idx == 0:
                ax.set_yticklabels(
                    ["1", "4", "16", "64"],
                    fontsize=TICK_FONT_SIZE,
                )
            else:
                ax.tick_params(labelleft=False)

    fig.legend(
        [legend_handles[system] for system in SYSTEM_RENDER_ORDER if system in legend_handles],
        [re.sub(r"\bToT\b", "Ours", system) for system in SYSTEM_RENDER_ORDER if system in legend_handles],
        loc="upper right",
        bbox_to_anchor=(0.985, 0.13),
        ncol=3,
        fontsize=LEGEND_FONT_SIZE,
        frameon=False,
        handletextpad=0.4,
        columnspacing=1.0,
    )

    fig.tight_layout(rect=(0.04, 0.11, 1.0, 0.95))
    fig.savefig(output_path, bbox_inches="tight")
    plt.close(fig)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Generate the speedup figure from speedup.csv.")
    parser.add_argument("--csv", type=Path, default=CSV_PATH, help="Input CSV written by 06_speedup.sh.")
    parser.add_argument("--output-dir", type=Path, default=OUT_DIR, help="Directory to write the figure.")
    parser.add_argument("--output-format", choices=["pdf", "png"], default="pdf")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    all_data = collect_all_data(args.csv.resolve())
    output_path = output_dir / f"speedup.{args.output_format}"
    draw_speedup_grid(all_data, output_path)
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
