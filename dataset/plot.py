import csv
from pathlib import Path
import matplotlib.pyplot as plt
import logging
# Newer matplotlib logs a warning when generic families such as "cursive" are
# absent; it is harmless noise for these figures.
logging.getLogger("matplotlib.font_manager").setLevel(logging.ERROR)
import numpy as np
from matplotlib.font_manager import FontProperties
from matplotlib.lines import Line2D
from matplotlib.offsetbox import AnnotationBbox, HPacker, TextArea
from matplotlib.patches import Rectangle
from matplotlib.textpath import TextPath
from matplotlib.transforms import ScaledTranslation

"""Plot the main experiment figure from the per-benchmark CSV files.

Data is collected from ``benchmarks/<Algo>/benchmark.csv`` (one file per
algorithm, produced by the run scripts) instead of a single merged table.
The drawing code is identical to the paper's ``experiments/plot.py``.
"""

ROOT_DIR = Path(__file__).resolve().parent.parent
BENCHMARK_DIR = ROOT_DIR / "benchmarks"
OUTPUT_DIR = Path(__file__).resolve().parent
OUTPUT_NAME = "main_exp.pdf"

# Graph order and categories used in the figure (top to bottom = left to right).
GRAPH_ORDER = [
    ("Social", "friendster_sym"),
    ("Social", "twitter_sym"),
    ("Social", "com-orkut_sym"),
    ("Social", "socfb-konect_sym"),
    ("Social", "soc-LiveJournal1_sym"),
    ("Web", "clueweb_sym"),
    ("Web", "sd_arc_sym"),
    ("Web", "arabic_sym"),
    ("Web", "uk-2002_sym"),
    ("Web", "indochina_sym"),
    ("Road", "planet_sym"),
    ("Road", "europe_sym"),
    ("Road", "asia_sym"),
    ("Road", "africa_sym"),
    ("Road", "RoadUSA_sym"),
    ("Synthetic", "Cosmo50_5_sym"),
    ("Synthetic", "GeoLifeNoScale_10_sym"),
    ("Synthetic", "hugebubbles-00020_sym"),
    ("Synthetic", "hugetrace-00020_sym"),
    ("Synthetic", "CHEM_5_sym"),
]

# Suffixes appended to graph names in the weighted benchmarks' CSVs.
GRAPH_NAME_SUFFIXES = ("_wghlog",)

# algorithm name used in the figure -> (benchmark sub-directory,
#   {method name used in the figure: column name in benchmark.csv})
ALGO_SOURCES = {
    "BFS": (
        "BFS",
        {
            "GAPBS": "GAPBS",
            "Galois": "Galois",
            "GBBS": "GBBS",
            "PASGAL": "PASGAL",
            "ToT+VGC": "Ours+VGC",
        },
    ),
    "KCore": (
        "KCore",
        {
            "ParK": "ParK",
            "PKC": "PKC",
            "GBBS": "GBBS",
            "PASGAL": "PASGAL",
            "ToT+VGC+SPL": "Ours+VGC+SPL",
        },
    ),
    "WeightedBFS": (
        "wBFS",
        {
            "GBBS": "GBBS",
            "ToT(IS)": "Ours-IS",
            "ToT(IM)": "Ours-IM",
        },
    ),
    "BellmanFord": (
        "BellmanFord",
        {
            "GBBS": "GBBS",
            "ToT": "Ours",
        },
    ),
    "Coloring": (
        "Coloring",
        {
            "GBBS": "GBBS",
            "ToT": "Ours",
        },
    ),
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
    "GeoLifeNoScale_10_sym": "GL",
    "CHEM_5_sym": "CH",
    "Cosmo50_5_sym": "COS",
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

METHOD_COLORS = {
    "GBBS": "#7EAAF6",
    "ToT(IS)": "#E41A1C",
    "PASGAL": "#E69F00",
    "GAPBS": "#984EA3",
    "Galois": "#FCC4E2",
    "ParK": "#984EA3",
    "PKC": "#FCC4E2",
}

OURS_COLOR = "red"
OURS_LINE_STYLE = "--"
OURS_LINE_WIDTH = 6

# Font settings (edit here to change all text in the figure).
# "STIXGeneral" ships with matplotlib, so no extra font or TeX install is
# needed. The paper used "Linux Libertine O" (needs fonts-linuxlibertine).
FONT_FAMILY = "STIXGeneral"
LEGEND_FONT_SIZE = 60
CATEGORY_FONT_SIZE = 60
CATEGORY_FONT_STYLE = "italic"
VALUE_FONT_SIZE = 27
VALUE_LABEL_MIN_GAP_POINTS = 4
AXIS_TICK_FONT_SIZE = 60
# Extra vertical shift (in points, negative = down) for individual rotated
# x tick labels on the short bottom plots, to avoid overlaps.
ROTATED_TICK_LABEL_SHIFT_POINTS = {"CH": -14, "GL": -8, "TRCE": 6}
AXIS_TICK_FONT_WEIGHT = "normal"
TITLE_FONT_SIZE = 60
TITLE_FONT_WEIGHT = "bold"
# Keep each plot title and legend on one shared header row.
PLOT_HEADER_Y = 1.02
LEGEND_HEADER_Y = 1.06
TABLE_FONT_SIZE = 60
TABLE_HEADER_FONT_SIZE = 60
TABLE_FONT_FAMILY = FONT_FAMILY
TABLE_FONT_WEIGHT = "normal"
TABLE_HEADER_FONT_WEIGHT = "normal"
PLOT_TEXT_FONT_FAMILY = TABLE_FONT_FAMILY

plt.rcParams["font.family"] = FONT_FAMILY
plt.rcParams["mathtext.fontset"] = "custom"
plt.rcParams["mathtext.rm"] = FONT_FAMILY
plt.rcParams["mathtext.it"] = f"{FONT_FAMILY}:italic"
plt.rcParams["mathtext.bf"] = f"{FONT_FAMILY}:bold"
# Small-caps suffix of split headers (e.g. "G" + "BBS") is imitated with
# upper-case letters at this fraction of the header font size; no TeX needed.
SMALL_CAPS_SCALE = 0.78

# Figure layout: retain the original 40-inch plot area and append a table area.
PLOT_AREA_WIDTH = 40
TABLE_AREA_WIDTH = PLOT_AREA_WIDTH / 3
FIGURE_HEIGHT = 30
TABLE_AXIS_LEFT_SHIFT = 0.035
TABLE_GROUP_LINE_INSET = 0.022
TABLE_BBOX_LEFT = 0.00
TABLE_BBOX_WIDTH = 1.00
TABLE_BBOX_BOTTOM = -0.1
TABLE_BBOX_HEIGHT = 1.08
TABLE_CELL_PAD = 0.02
TABLE_CATEGORY_WIDTH_RATIO = 0.08
TABLE_CATEGORY_EXTRA_DIGITS = 1

SPLIT_METHOD_HEADERS = {
    "GBBS": ("G", "BBS"),
    "GAPBS": ("G", "APBS"),
    "PASGAL": ("P", "ASGAL"),
}

CATEGORY_ABBREV = {
    "Social": "S",
    "Web": "W",
    "Road": "R",
    "Synthetic": "K",
}


def display_text(value):
    if value in {"ToT+VGC", "ToT+VGC+SPL"}:
        return "Ours"
    return value.replace("ToT", "Ours")


def display_category(value, split_synthetic=False):
    if value == "Synthetic":
        label = "$k$-NN &\nSynthetic" if split_synthetic else r"$k$-NN & Synthetic"
    else:
        label = value
    abbreviation = CATEGORY_ABBREV.get(value)
    return f"{label} ({abbreviation})" if abbreviation else label


def display_algorithm(value):
    return {
        "KCore": r"$k$-core",
        "BellmanFord": "Bellman-Ford",
        "WeightedBFS": "weighted-BFS",
    }.get(value, value)


def table_category_width_ratio(ax):
    """Widen the category column by the rendered width of one digit."""
    font = FontProperties(
        family=TABLE_FONT_FAMILY,
        size=TABLE_FONT_SIZE,
        weight=TABLE_FONT_WEIGHT,
    )
    digit_width_points = TextPath((0, 0), "0", prop=font).get_extents().width
    axis_width_points = ax.bbox.width * 72 / ax.figure.dpi
    return TABLE_CATEGORY_WIDTH_RATIO + (
        TABLE_CATEGORY_EXTRA_DIGITS * digit_width_points / axis_width_points
    )


def add_split_method_header(ax, x, y, prefix, suffix):
    common_props = {
        "fontfamily": TABLE_FONT_FAMILY,
        "fontweight": TABLE_HEADER_FONT_WEIGHT,
        "color": "black",
    }
    label = HPacker(
        children=[
            TextArea(
                prefix,
                textprops={**common_props, "fontsize": TABLE_HEADER_FONT_SIZE},
            ),
            TextArea(
                suffix.upper(),
                textprops={
                    **common_props,
                    "fontsize": TABLE_HEADER_FONT_SIZE * SMALL_CAPS_SCALE,
                },
            ),
        ],
        align="baseline",
        pad=0,
        sep=0,
    )
    ax.add_artist(
        AnnotationBbox(
            label,
            (x, y),
            xycoords=ax.transAxes,
            box_alignment=(0.5, 0.5),
            frameon=False,
            pad=0,
        )
    )


ALGO_DEFS = [
    {
        "name": "BFS",
        "baseline_methods": [
            {"name": "GAPBS", "legend_style": "solid"},
            {"name": "Galois", "legend_style": "solid"},
            {"name": "GBBS", "legend_style": "solid"},
            {"name": "PASGAL", "legend_style": "solid"},
        ],
        "ours_method": {"name": "ToT+VGC", "legend_style": "dashed"},
    },
    {
        "name": "KCore",
        "baseline_methods": [
            {"name": "ParK", "legend_style": "solid"},
            {"name": "PKC", "legend_style": "solid"},
            {"name": "GBBS", "legend_style": "solid"},
            {"name": "PASGAL", "legend_style": "solid"},
        ],
        "ours_method": {"name": "ToT+VGC+SPL", "legend_style": "dashed"},
    },
    {
        "name": "WeightedBFS",
        "baseline_methods": [
            {"name": "GBBS", "legend_style": "solid"},
            {"name": "ToT(IS)", "legend_style": "solid"},
        ],
        "ours_method": {"name": "ToT(IM)", "legend_style": "dashed"},
    },
    {
        "name": "BellmanFord",
        "baseline_methods": [
            {"name": "GBBS", "legend_style": "solid"},
        ],
        "ours_method": {"name": "ToT", "legend_style": "dashed"},
    },
    {
        "name": "Coloring",
        "baseline_methods": [
            {"name": "GBBS", "legend_style": "solid"},
        ],
        "ours_method": {"name": "ToT", "legend_style": "dashed"},
    },
]

CAP = 5.0
SHORT_Y_MAX = 4.0
SHORT_ALGORITHMS = {"BellmanFord", "Coloring"}
SHORT_AXIS_HEIGHT_RATIO = SHORT_Y_MAX / CAP


def safe_float(value):
    try:
        return float(value)
    except (TypeError, ValueError):
        return np.nan


def normalize_graph_name(name):
    name = name.strip()
    for suffix in GRAPH_NAME_SUFFIXES:
        if name.endswith(suffix):
            return name[: -len(suffix)]
    return name


def load_benchmark_csv(csv_path):
    """Return {graph: {column: float}} for one benchmark.csv."""
    if not csv_path.is_file():
        print(f"Warning: {csv_path} not found; its algorithm will be empty")
        return {}
    with csv_path.open(newline="", encoding="utf-8") as file:
        reader = csv.DictReader(file)
        rows = {}
        for row in reader:
            graph = normalize_graph_name(row.get("graph", ""))
            if not graph:
                continue
            rows[graph] = {
                key.strip(): safe_float((value or "").strip())
                for key, value in row.items()
                if key is not None and key.strip() != "graph"
            }
    return rows


def load_benchmarks(benchmark_dir):
    """Assemble the (algorithm, method) -> values table from the per-algorithm CSVs."""
    categories = [category for category, _graph in GRAPH_ORDER]
    graphs = [graph for _category, graph in GRAPH_ORDER]

    data = {}
    for algo_name, (sub_dir, method_columns) in ALGO_SOURCES.items():
        rows = load_benchmark_csv(benchmark_dir / sub_dir / "benchmark.csv")
        unknown = set(rows) - set(graphs)
        if unknown:
            print(f"Warning: {algo_name}: ignoring graphs not in GRAPH_ORDER: {sorted(unknown)}")
        for method_name, column in method_columns.items():
            values = []
            for graph in graphs:
                values.append(rows.get(graph, {}).get(column, np.nan))
            if rows and all(column not in row for row in rows.values()):
                print(f"Warning: {algo_name}: column '{column}' not found in {sub_dir}/benchmark.csv")
            data[(algo_name, method_name)] = values

    graph_labels = [ABBREV.get(graph, graph) for graph in graphs]
    return data, categories, graphs, graph_labels


def build_category_ranges(categories):
    unique_cats = []
    cat_ranges = []
    prev = None
    start = 0

    for index, category in enumerate(categories):
        if category != prev:
            if prev is not None:
                cat_ranges.append((start, index - 1))
                unique_cats.append(prev)
            prev = category
            start = index

    cat_ranges.append((start, len(categories) - 1))
    unique_cats.append(prev)
    return unique_cats, cat_ranges


def get_series(data, algorithm, method):
    key = (algorithm, method)
    if key not in data:
        raise KeyError(f"Missing column: {algorithm} / {method}")
    return np.array(data[key], dtype=float)


def geometric_mean(values):
    """Geometric mean of the finite, positive entries (NaN entries are skipped)."""
    values = np.asarray(values, dtype=float)
    valid = values[np.isfinite(values) & (values > 0)]
    if len(valid) == 0:
        return np.nan
    return float(np.exp(np.mean(np.log(valid))))


def format_geometric_mean(value):
    if np.isnan(value):
        return "NA"
    if value == 0:
        return "0.00"
    if abs(value) < 1:
        formatted = f"{value:.3f}"
        if formatted.startswith("0"):
            return formatted[1:]
        if formatted.startswith("-0"):
            return "-" + formatted[2:]
        return formatted
    exponent = int(np.floor(np.log10(abs(value))))
    decimal_places = 2 - exponent
    if decimal_places > 0:
        return f"{value:.{decimal_places}f}"
    return f"{round(value, -decimal_places):.0f}"


def draw_geometric_mean_table(
    ax, data, algo_defs, unique_cats, cat_ranges
):
    method_columns = []
    algorithm_groups = []
    for algo_def in algo_defs:
        methods = algo_def["baseline_methods"] + [algo_def["ours_method"]]
        start_column = len(method_columns)
        for method in methods:
            method_columns.append((algo_def["name"], method["name"]))
        algorithm_groups.append(
            (algo_def["name"], start_column, len(methods))
        )

    table_category_labels = [
        CATEGORY_ABBREV.get(category, category) for category in unique_cats
    ]
    column_labels = [""] + [
        "" if display_text(method_name) in SPLIT_METHOD_HEADERS else display_text(method_name)
        for _algo_name, method_name in method_columns
    ]
    cell_text = []

    series_by_column = {
        (algo_name, method_name): get_series(data, algo_name, method_name)
        for algo_name, method_name in method_columns
    }
    # A graph only contributes to an algorithm's geometric means when every
    # method of that algorithm has a value for it; otherwise that graph is
    # dropped for all of the algorithm's columns (other graphs still count).
    for algo_name, start_column, column_count in algorithm_groups:
        group_columns = method_columns[start_column : start_column + column_count]
        complete = np.all(
            [np.isfinite(series_by_column[column]) for column in group_columns],
            axis=0,
        )
        for column in group_columns:
            series_by_column[column] = np.where(
                complete, series_by_column[column], np.nan
            )
    for category_label, (start, end) in zip(
        table_category_labels, cat_ranges
    ):
        cell_text.append(
            [category_label]
            + [
                format_geometric_mean(
                    geometric_mean(
                        series_by_column[(algo_name, method_name)][start : end + 1]
                    )
                )
                for algo_name, method_name in method_columns
            ]
        )
    cell_text.append(
        ["All"]
        + [
            format_geometric_mean(
                geometric_mean(series_by_column[(algo_name, method_name)])
            )
            for algo_name, method_name in method_columns
        ]
    )

    ax.axis("off")
    category_width_ratio = table_category_width_ratio(ax)
    method_width_ratio = (1.0 - category_width_ratio) / len(method_columns)
    table = ax.table(
        cellText=cell_text,
        colLabels=column_labels,
        cellLoc="center",
        colLoc="center",
        bbox=[
            TABLE_BBOX_LEFT,
            TABLE_BBOX_BOTTOM,
            TABLE_BBOX_WIDTH,
            TABLE_BBOX_HEIGHT,
        ],
        colWidths=[category_width_ratio]
        + [method_width_ratio] * len(method_columns),
    )
    table.auto_set_font_size(False)
    table.set_fontsize(TABLE_FONT_SIZE)

    synth_separator_row = 1 + table_category_labels.index(
        CATEGORY_ABBREV["Synthetic"]
    )
    for (row, column), cell in table.get_celld().items():
        cell.PAD = TABLE_CELL_PAD
        cell.visible_edges = ""
        cell.set_text_props(
            fontfamily=TABLE_FONT_FAMILY,
            fontweight=TABLE_FONT_WEIGHT,
        )
        if row == 0:
            cell.set_text_props(
                fontsize=TABLE_HEADER_FONT_SIZE,
                fontfamily=TABLE_FONT_FAMILY,
                fontweight=TABLE_HEADER_FONT_WEIGHT,
            )
        if row in {0, synth_separator_row}:
            cell.visible_edges += "B"
        if column == 0:
            cell.visible_edges += "R"
        if cell.visible_edges:
            cell.set_edgecolor("black")
            cell.set_linewidth(1.5)

    category_width = TABLE_BBOX_WIDTH * category_width_ratio
    method_width = (
        TABLE_BBOX_WIDTH * method_width_ratio
    )
    table_top = TABLE_BBOX_BOTTOM + TABLE_BBOX_HEIGHT
    header_row_height = TABLE_BBOX_HEIGHT / (len(cell_text) + 1)
    header_center_y = table_top - header_row_height / 2
    for column_index, (_algo_name, method_name) in enumerate(method_columns):
        displayed_name = display_text(method_name)
        if displayed_name not in SPLIT_METHOD_HEADERS:
            continue
        prefix, suffix = SPLIT_METHOD_HEADERS[displayed_name]
        header_center_x = (
            TABLE_BBOX_LEFT
            + category_width
            + (column_index + 0.5) * method_width
        )
        add_split_method_header(
            ax,
            header_center_x,
            header_center_y,
            prefix,
            suffix,
        )
    group_line_y = table_top + 0.02
    group_label_y = table_top + 0.07
    ax.plot(
        [TABLE_BBOX_LEFT + category_width, TABLE_BBOX_LEFT + category_width],
        [table_top, group_label_y + 0.04],
        color="black",
        linewidth=1.5,
        transform=ax.transAxes,
        clip_on=False,
    )
    for algo_name, start_column, column_count in algorithm_groups:
        left = TABLE_BBOX_LEFT + category_width + start_column * method_width
        right = left + column_count * method_width
        ax.text(
            (left + right) / 2,
            group_label_y,
            display_algorithm(algo_name),
            ha="center",
            va="center",
            fontsize=TABLE_HEADER_FONT_SIZE,
            fontfamily=TABLE_FONT_FAMILY,
            fontweight=TABLE_HEADER_FONT_WEIGHT,
            transform=ax.transAxes,
        )
        ax.plot(
            [left + TABLE_GROUP_LINE_INSET, right - TABLE_GROUP_LINE_INSET],
            [group_line_y, group_line_y],
            color="black",
            linewidth=1.5,
            transform=ax.transAxes,
            clip_on=False,
        )


def add_category_guides(ax, cat_ranges, unique_cats):
    for index in range(len(cat_ranges) - 1):
        mid = (cat_ranges[index][1] + cat_ranges[index + 1][0]) / 2
        ax.axvline(x=mid, color="grey", linestyle=":", linewidth=1.5, label="_nolegend_")


def add_category_labels(ax, cat_ranges, unique_cats, split_synthetic=False):
    for (start, end), category in zip(cat_ranges, unique_cats):
        mid = (start + end) / 2
        ax.text(
            mid,
            0.97,
            display_category(category, split_synthetic=split_synthetic),
            ha="center",
            va="top",
            fontsize=CATEGORY_FONT_SIZE,
            fontfamily=PLOT_TEXT_FONT_FAMILY,
            transform=ax.get_xaxis_transform(),
            fontstyle=CATEGORY_FONT_STYLE,
        )


def add_horizontal_guides(ax, axis_cap):
    for y in range(2, int(axis_cap) + 1):
        ax.axhline(y=y, color="black", linestyle="--", linewidth=0.8, alpha=0.12, zorder=0)


def draw_relative_bars(
    ax, x_base, ours, baseline_methods, series_by_method, cap, bar_width=None
):
    n_methods = len(baseline_methods)
    if bar_width is None:
        bar_width = min(0.8 / max(n_methods, 1), 0.35)
    offsets = np.linspace(
        -(n_methods - 1) / 2 * bar_width,
        (n_methods - 1) / 2 * bar_width,
        n_methods,
    )

    relative_by_method = {}
    for method_name in baseline_methods:
        raw_vals = series_by_method[method_name]
        relative_by_method[method_name] = np.where(
            (ours > 0) & ~np.isnan(ours) & ~np.isnan(raw_vals),
            raw_vals / ours,
            np.nan,
        )

    value_labels = []
    for offset, method_name in zip(offsets, baseline_methods):
        rel = relative_by_method[method_name]
        capped = np.array([min(value, cap) if not np.isnan(value) else 0 for value in rel])
        positions = x_base + offset
        bars = ax.bar(
            positions,
            capped,
            bar_width,
            label=method_name,
            color=METHOD_COLORS.get(method_name, "#CCCCCC"),
            edgecolor="black",
            linewidth=0.8,
        )

        for bar, value in zip(bars, rel):
            if np.isnan(value):
                ax.plot(
                    bar.get_x() + bar.get_width() / 2,
                    0.15,
                    "kx",
                    markersize=14,
                    markeredgewidth=2.5,
                    label="_nolegend_",
                )
            '''
            elif value > cap:
                label = ax.annotate(
                    f"{value:.1f}",
                    xy=(
                        bar.get_x() + bar.get_width() / 2,
                        bar.get_height() - 0.15,
                    ),
                    xytext=(1, 0),
                    textcoords="offset pixels",
                    ha="center",
                    va="top",
                    fontsize=VALUE_FONT_SIZE,
                    fontfamily=PLOT_TEXT_FONT_FAMILY,
                    color=(
                        "white"
                        if METHOD_COLORS.get(method_name, "").lower() == "#984ea3"
                        else "black"
                    ),
                    rotation=90,
                )
                value_labels.append(label)
            '''

    return value_labels


def resolve_label_collisions(fig, labels):
    """Move overlapping value labels sideways after their sizes are known."""
    if len(labels) < 2:
        return

    gap_pixels = VALUE_LABEL_MIN_GAP_POINTS * fig.dpi / 72
    for _ in range(5):
        fig.canvas.draw()
        renderer = fig.canvas.get_renderer()
        items = sorted(
            ((label, label.get_window_extent(renderer)) for label in labels),
            key=lambda item: item[1].x0,
        )

        clusters = []
        cluster = [items[0]]
        cluster_right = items[0][1].x1
        for item in items[1:]:
            if item[1].x0 < cluster_right + gap_pixels:
                cluster.append(item)
                cluster_right = max(cluster_right, item[1].x1)
            else:
                clusters.append(cluster)
                cluster = [item]
                cluster_right = item[1].x1
        clusters.append(cluster)

        overlapping_clusters = [cluster for cluster in clusters if len(cluster) > 1]
        if not overlapping_clusters:
            break

        for cluster in overlapping_clusters:
            boxes = [item[1] for item in cluster]
            packed_width = sum(box.width for box in boxes) + gap_pixels * (
                len(boxes) - 1
            )
            original_center = (boxes[0].x0 + boxes[-1].x1) / 2
            next_left = original_center - packed_width / 2

            for label, box in cluster:
                shift_pixels = next_left - box.x0
                old_x, old_y = label.get_position()
                label.set_position((old_x + shift_pixels * 72 / fig.dpi, old_y))
                next_left += box.width + gap_pixels


def build_legend_handles(baseline_methods, ours_method):
    handles = []
    for method in baseline_methods + [ours_method]:
        method_name = method["name"]
        if method["legend_style"] == "solid":
            handles.append(
                Rectangle(
                    (0, 0),
                    1,
                    1,
                    facecolor=METHOD_COLORS[method_name],
                    edgecolor="black",
                    linewidth=2,
                    label=display_text(method_name),
                )
            )
        else:
            handles.append(
                Line2D(
                    [0],
                    [0],
                    linestyle=OURS_LINE_STYLE,
                    linewidth=OURS_LINE_WIDTH,
                    color=OURS_COLOR,
                    label=display_text(method_name),
                )
            )
    return handles


def save_figure(data, graph_labels, unique_cats, cat_ranges):
    n_graphs = len(graph_labels)
    x_base = np.arange(n_graphs, dtype=float)
    plt.close("all")
    fig = plt.figure(
        figsize=(PLOT_AREA_WIDTH + TABLE_AREA_WIDTH, FIGURE_HEIGHT)
    )
    grid = fig.add_gridspec(
        nrows=4,
        ncols=2,
        width_ratios=[3.6, 1],
        left=0.01875,
        right=0.98875,
        bottom=0.035,
        top=0.95,
        hspace=0.42,
        wspace=0.08,
    )
    bottom_plots = grid[3, 0].subgridspec(nrows=1, ncols=2, wspace=0.08)
    axes = [
        fig.add_subplot(grid[0, 0]),
        fig.add_subplot(grid[1, 0]),
        fig.add_subplot(grid[2, 0]),
        fig.add_subplot(bottom_plots[0, 0]),
        fig.add_subplot(bottom_plots[0, 1]),
    ]
    for ax in axes[-2:]:
        position = ax.get_position()
        shortened_height = position.height * SHORT_AXIS_HEIGHT_RATIO
        ax.set_position(
            [
                position.x0,
                position.y1 - shortened_height,
                position.width,
                shortened_height,
            ]
        )
    table_axes = [
        fig.add_subplot(grid[0, 1]),
        fig.add_subplot(grid[1, 1]),
        fig.add_subplot(grid[2, 1]),
        fig.add_subplot(grid[3, 1]),
    ]
    for table_ax in table_axes:
        position = table_ax.get_position()
        table_ax.set_position(
            [
                position.x0 - TABLE_AXIS_LEFT_SHIFT,
                position.y0,
                position.width + TABLE_AXIS_LEFT_SHIFT,
                position.height,
            ]
        )
    value_labels_by_axis = []

    for ax, algo_def in zip(axes, ALGO_DEFS):
        algo_name = algo_def["name"]
        baseline_methods = algo_def["baseline_methods"]
        ours_method = algo_def["ours_method"]
        baseline_method_names = [method["name"] for method in baseline_methods]
        ours_method_name = ours_method["name"]
        short_axis = algo_name in SHORT_ALGORITHMS
        axis_cap = SHORT_Y_MAX if short_axis else CAP
        handles = build_legend_handles(baseline_methods, ours_method)

        legend = ax.legend(
            handles=handles,
            loc="center right",
            bbox_to_anchor=(1.0, LEGEND_HEADER_Y),
            prop={
                "family": PLOT_TEXT_FONT_FAMILY,
                "size": LEGEND_FONT_SIZE,
            },
            frameon=True,
            facecolor="white",
            framealpha=1.0,
            ncol=len(handles),
            handletextpad=0.4,
            columnspacing=0.7,
            borderpad=0.4,
            edgecolor="none",
        )
        legend.set_zorder(0)

        series_by_method = {
            method: get_series(data, algo_name, method)
            for method in baseline_method_names + [ours_method_name]
        }
        ours_vals = series_by_method[ours_method_name]

        value_labels = draw_relative_bars(
            ax,
            x_base,
            ours_vals,
            baseline_method_names,
            series_by_method,
            axis_cap,
            bar_width=0.2 if algo_name == "WeightedBFS" else None,
        )
        value_labels_by_axis.append(value_labels)
        ax.axhline(
            y=1,
            color=OURS_COLOR,
            linestyle=OURS_LINE_STYLE,
            linewidth=OURS_LINE_WIDTH,
            zorder=5,
            alpha=0.7,
        )
        add_horizontal_guides(ax, axis_cap)
        add_category_guides(ax, cat_ranges, unique_cats)
        # Keep the same headroom above the numbered scale as the full-height
        # plots; the shorter last grid row removes the omitted 4--5 section.
        y_max = axis_cap + 1.5 * axis_cap / CAP
        ax.set_ylim([0, y_max])
        ax.set_xlim([x_base[0] - 0.6, x_base[-1] + 0.6])
        ax.set_yticks(np.arange(0, axis_cap + 1, 1))
        tick_font_size = AXIS_TICK_FONT_SIZE
        ax.tick_params(axis="y", labelsize=tick_font_size)
        ax.set_title(
            display_algorithm(algo_name),
            loc="left",
            x=0.0,
            y=PLOT_HEADER_Y,
            fontsize=TITLE_FONT_SIZE,
            fontfamily=FONT_FAMILY,
            fontweight=TITLE_FONT_WEIGHT,
            pad=0,
        )
        ax.set_xticks(x_base)
        add_category_labels(
            ax,
            cat_ranges,
            unique_cats,
            split_synthetic=algo_name in {"BellmanFord", "Coloring"},
        )
        rotated_labels = algo_name in {"BellmanFord", "Coloring"}
        ax.set_xticklabels(
            graph_labels,
            rotation=45 if rotated_labels else 0,
            ha="center",
            fontsize=tick_font_size,
            fontfamily=PLOT_TEXT_FONT_FAMILY,
            fontweight=AXIS_TICK_FONT_WEIGHT,
        )
        if rotated_labels:
            for label in ax.get_xticklabels():
                shift = ROTATED_TICK_LABEL_SHIFT_POINTS.get(label.get_text())
                if shift:
                    label.set_transform(
                        label.get_transform()
                        + ScaledTranslation(0, shift / 72, fig.dpi_scale_trans)
                    )
        plt.setp(
            ax.get_yticklabels(),
            fontfamily=PLOT_TEXT_FONT_FAMILY,
            fontweight=AXIS_TICK_FONT_WEIGHT,
        )

    table_groups = [
        [ALGO_DEFS[0]],
        [ALGO_DEFS[1]],
        [ALGO_DEFS[2]],
        [ALGO_DEFS[3], ALGO_DEFS[4]],
    ]
    for table_ax, algo_defs in zip(table_axes, table_groups):
        draw_geometric_mean_table(
            table_ax,
            data,
            algo_defs,
            unique_cats,
            cat_ranges,
        )

    fig.canvas.draw()
    for value_labels in value_labels_by_axis:
        resolve_label_collisions(fig, value_labels)
    out_name = OUTPUT_NAME
    fig.savefig(OUTPUT_DIR / out_name, bbox_inches="tight", pad_inches=0)
    print(f"Saved {out_name}")
    plt.close(fig)


def main():
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    data, categories, _graphs, graph_labels = load_benchmarks(BENCHMARK_DIR)
    unique_cats, cat_ranges = build_category_ranges(categories)
    print(
        f"Loaded {len(graph_labels)} graphs, {len(unique_cats)} categories "
        f"from {BENCHMARK_DIR}"
    )
    save_figure(data, graph_labels, unique_cats, cat_ranges)


if __name__ == "__main__":
    main()
