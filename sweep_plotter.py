"""
sweep_plotter.py -- aggregate and visualise sweep_runner outputs.

Given a sweep folder produced by `sweep_runner.run_sweep` (one that contains
algorithm subfolders, e.g. `sim_data/<label>/FIFO/`, `.../COST/`, plus a
`combined_manifest.json` at the top), this module:

  1. Walks every algorithm subtree, reads each `repN-tasks.csv`, and reduces
     it to a small set of per-run summary metrics.
  2. Produces a comparison figure suitable for FIFO-vs-COST analysis:
       * 2-D sweeps (2 params)  -> side-by-side grouped box plots,
                                   one panel per algorithm.
       * 3-D sweeps (3 params)  -> heatmap grid (rows = algorithm,
                                   cols = values of the 3rd param);
                                   cell colour = mean metric across reps.

The per-run metrics are derived from MainNodeApp's CSV columns
(`run, algorithm, taskId, ..., waitTime, turnaroundTime, dropEvents, outcome`):

    n_tasks          total tasks generated in that run
    completion_rate  fraction with outcome == COMPLETED          (higher=better)
    drop_rate            fraction NOT completed -- DROPPED + EXPIRED +
                         never-finalized UNFINISHED leftovers          (lower=better)
    expired_rate         fraction with outcome == EXPIRED  -- queue-wait SLA
                         misses plus tasks still waiting in the queue at
                         sim end (both mean "never serviced in time")   (lower=better)
    preempt_drop_rate    fraction with outcome == DROPPED  -- preemption, MBS
                         relocation, drone failure, etc. (everything that
                         isn't a deadline miss)                          (lower=better)
    mean_wait        mean waitTime among dispatched tasks         (lower=better)
    mean_turnaround  mean turnaroundTime among COMPLETED tasks    (lower=better)
    total_drops      sum of dropEvents across all generated tasks (lower=better)
    throughput       n_completed / sim_duration (tasks/sec)       (higher=better)
    sim_finish_time  wall-clock sim end (max finalizedAt, seconds) (lower=better)
                     -- useful to compare which algorithm drains the
                     workload fastest overall

For priority-aware analysis use `plot_by_priority` -- it stratifies by
the per-task `priority` column (1..3 in the current generator) and plots
the chosen metric per priority class with FIFO/COST grouped side-by-side.

Library usage::

    from sweep_plotter import plot_sweep, plot_by_priority
    plot_sweep("sim_data/drones_x_mbs", metric="throughput")
    plot_by_priority("sim_data/drones_x_mbs", metric="completion_rate")
"""

from __future__ import annotations

import itertools
import json
import logging
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Dict, List, Optional, Sequence, Tuple

import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from matplotlib.figure import Figure
from matplotlib.patches import Patch

from sweep_runner import _display_name_for_key, _sanitize_for_path

logger = logging.getLogger(__name__)

# Higher is better => default; metrics with "lower is better" semantics are
# noted in this set so axes/colour bars can be flipped or annotated.
_LOWER_IS_BETTER = {
    "drop_rate",
    "expired_rate",
    "preempt_drop_rate",
    "mean_wait",
    "mean_turnaround",
    "total_drops",
    "sim_finish_time",
    "mean_wait_norm",
    "mean_turnaround_norm",
}

_VALID_METRICS = (
    "completion_rate",
    "drop_rate",
    "expired_rate",
    "preempt_drop_rate",
    "mean_wait",
    "mean_turnaround",
    "total_drops",
    "n_tasks",
    "throughput",
    "sim_finish_time",
)

# Metrics that only make sense on the per-priority view (they depend on
# the per-priority queue-wait deadline / nominal task duration, which are
# read from the sweep manifest, not from the per-task CSV). Accepted by
# `plot_by_priority` but not by `plot_sweep`.
_VALID_PRIORITY_ONLY_METRICS = (
    "mean_wait_norm",        # mean_wait / deadline_for_priority
    "mean_turnaround_norm",  # mean_turnaround / (deadline + taskDuration)
    "throughput_norm",       # n_completed_in_class / n_generated_in_class
)


# ---------------------------------------------------------------------------
# Per-run metric extraction
# ---------------------------------------------------------------------------
def _per_run_metrics(csv_path: Path) -> Dict[str, float]:
    """Reduce one repetition's per-task CSV to a single row of summary stats."""
    df = pd.read_csv(csv_path)
    n = len(df)
    if n == 0:
        return {
            "n_tasks":           0,
            "completion_rate":   float("nan"),
            "drop_rate":         float("nan"),
            "expired_rate":      float("nan"),
            "preempt_drop_rate": float("nan"),
            "mean_wait":         float("nan"),
            "mean_turnaround":   float("nan"),
            "total_drops":       0,
            "throughput":        float("nan"),
            "sim_finish_time":   float("nan"),
        }
    completed = df["outcome"] == "COMPLETED"
    # Anything that didn't COMPLETE counts as a drop (DROPPED from
    # preemption / MBS-relocation / drone failure, EXPIRED from a missed
    # wait deadline or from being left in the queue at sim end, or
    # never-finalized UNFINISHED leftovers).
    dropped       = ~completed
    # Split the not-completed pool into the two drop *causes* so the user
    # can see whether COST is leaking tasks via preemption or via SLA
    # expirations.
    expired       = df["outcome"] == "EXPIRED"
    preempt_drop  = df["outcome"] == "DROPPED"
    dispatched = df["waitTime"] >= 0    # waitTime == -1 means never dispatched
    # sim_duration estimate: the latest `finalizedAt` is the sim end (rows
    # for UNFINISHED/EXPIRED-at-end tasks are written at finish() with
    # now=simEnd), or the actual completion time of the last completed
    # task otherwise.
    sim_duration = float(df["finalizedAt"].max())
    n_completed = int(completed.sum())
    return {
        "n_tasks":           int(n),
        "completion_rate":   float(completed.mean()),
        "drop_rate":         float(dropped.mean()),
        "expired_rate":      float(expired.mean()),
        "preempt_drop_rate": float(preempt_drop.mean()),
        "mean_wait":         float(df.loc[dispatched, "waitTime"].mean())
                             if dispatched.any() else float("nan"),
        "mean_turnaround":   float(df.loc[completed, "turnaroundTime"].mean())
                             if completed.any() else float("nan"),
        "total_drops":       int(df["dropEvents"].sum()),
        "throughput":        (n_completed / sim_duration
                              if sim_duration > 0 else float("nan")),
        "sim_finish_time":   sim_duration,
    }


# ---------------------------------------------------------------------------
# Sweep tree discovery
# ---------------------------------------------------------------------------
@dataclass
class _SweepLayout:
    sweep_dir: Path
    algorithms: List[str]                       # e.g. ["FIFO", "COST"]
    sweep_params: Dict[str, List[Any]]          # raw keys (e.g. "*.numDrones")
    display_dims: List[str]                     # cleaned col names
    repetitions: int


def _read_layout(sweep_dir: Path) -> _SweepLayout:
    """Infer the sweep layout from manifests.

    Falls back to per-algorithm `sweep_manifest.json` if there's no
    `combined_manifest.json` (e.g. when the user re-ran one algorithm only).
    """
    combined = sweep_dir / "combined_manifest.json"
    if combined.is_file():
        meta = json.loads(combined.read_text())
        algorithms = list(meta["algorithms"])
        params = {k: list(v) for k, v in meta["sweep_params"].items()}
        repetitions = int(meta["repetitions"])
    else:
        # Discover algorithm folders by looking for sweep_manifest.json
        algorithms = sorted(
            p.name for p in sweep_dir.iterdir()
            if p.is_dir() and (p / "sweep_manifest.json").is_file()
        )
        if not algorithms:
            raise FileNotFoundError(
                f"{sweep_dir} contains no algorithm subfolders with a "
                "sweep_manifest.json. Was this folder produced by run_sweep?")
        meta = json.loads(
            (sweep_dir / algorithms[0] / "sweep_manifest.json").read_text())
        params = {k: list(v) for k, v in meta["sweep_params"].items()}
        repetitions = int(meta["repetitions"])

    display_dims = [_display_name_for_key(k) for k in params]
    return _SweepLayout(
        sweep_dir=sweep_dir,
        algorithms=algorithms,
        sweep_params=params,
        display_dims=display_dims,
        repetitions=repetitions,
    )


# ---------------------------------------------------------------------------
# Loading
# ---------------------------------------------------------------------------
def load_sweep(sweep_dir: str | Path) -> pd.DataFrame:
    """Build a long-format DataFrame, one row per repetition per cell.

    Columns: ``algorithm``, one column per swept parameter (cleaned name,
    e.g. `numDrones`), ``rep`` (0..N-1), and the per-run metrics from
    `_per_run_metrics`.

    Cells / reps whose CSV is missing are silently skipped with a warning.
    """
    sweep_dir = Path(sweep_dir).expanduser().resolve()
    layout = _read_layout(sweep_dir)

    rows: List[Dict[str, Any]] = []
    value_lists = list(layout.sweep_params.values())
    for algo in layout.algorithms:
        algo_dir = sweep_dir / algo
        if not algo_dir.is_dir():
            logger.warning("algorithm folder missing: %s", algo_dir)
            continue
        for value_combo in itertools.product(*value_lists):
            cell_dir = algo_dir
            for dn, v in zip(layout.display_dims, value_combo):
                cell_dir = cell_dir / f"{dn}={_sanitize_for_path(v)}"
            if not cell_dir.is_dir():
                logger.warning("cell folder missing: %s", cell_dir)
                continue
            for rep in range(layout.repetitions):
                csv = cell_dir / f"rep{rep}-tasks.csv"
                if not csv.is_file():
                    logger.warning("rep CSV missing: %s", csv)
                    continue
                metrics = _per_run_metrics(csv)
                row: Dict[str, Any] = {"algorithm": algo}
                for dn, v in zip(layout.display_dims, value_combo):
                    row[dn] = v
                row["rep"] = rep
                row.update(metrics)
                rows.append(row)

    if not rows:
        raise RuntimeError(f"No per-rep CSVs found under {sweep_dir}")

    df = pd.DataFrame(rows)
    df.attrs["sweep_dir"] = str(sweep_dir)
    df.attrs["dims"]       = list(layout.display_dims)
    df.attrs["algorithms"] = list(layout.algorithms)
    return df


# ---------------------------------------------------------------------------
# Priority-aware loader
# ---------------------------------------------------------------------------
def _parse_omnet_seconds(value: Any) -> Optional[float]:
    """Convert an OMNeT++ time literal (``"30s"``, ``"1.5min"``, plain
    number) into seconds. Returns None if the value can't be parsed."""
    if value is None:
        return None
    if isinstance(value, (int, float)):
        return float(value)
    s = str(value).strip().strip('"').strip("'").lower()
    if not s:
        return None
    units = (
        ("ms",  1e-3),
        ("us",  1e-6),
        ("ns",  1e-9),
        ("min", 60.0),
        ("h",   3600.0),
        ("s",   1.0),
    )
    for suffix, mult in units:
        if s.endswith(suffix):
            try:
                return float(s[: -len(suffix)]) * mult
            except ValueError:
                return None
    try:
        return float(s)
    except ValueError:
        return None


def _parse_weight_list(value: Any) -> List[float]:
    """Parse an OMNeT++ weight-list string (``'"30 120 300"'`` or
    ``'30, 120, 300'``) into a list of floats. Empty / missing returns []."""
    if value is None:
        return []
    s = str(value).strip().strip('"').strip("'")
    if not s:
        return []
    parts = [p for p in s.replace(",", " ").split() if p]
    out: List[float] = []
    for p in parts:
        try:
            out.append(float(p))
        except ValueError:
            return []
    return out


def _read_priority_budgets(
    sweep_dir: Path,
) -> Tuple[Dict[int, float], Optional[float]]:
    """Return (deadline_by_priority, task_duration_seconds).

    Reads `combined_manifest.json` (or the first algorithm's
    `sweep_manifest.json` as a fallback) and extracts the per-priority
    queue-wait deadlines (`taskDeadlineWeights`) and the nominal task
    execution time (`taskDuration`). Either may be empty: callers should
    treat missing entries as "no normalisation possible" (NaN).
    """
    combined = sweep_dir / "combined_manifest.json"
    if combined.is_file():
        meta = json.loads(combined.read_text())
    else:
        candidates = sorted(
            p for p in sweep_dir.iterdir()
            if p.is_dir() and (p / "sweep_manifest.json").is_file()
        )
        if not candidates:
            return ({}, None)
        meta = json.loads((candidates[0] / "sweep_manifest.json").read_text())
    overrides = (meta.get("extra_overrides") or {})

    deadlines_raw = _parse_weight_list(overrides.get("taskDeadlineWeights"))
    deadlines = {i + 1: v for i, v in enumerate(deadlines_raw)}
    task_duration = _parse_omnet_seconds(overrides.get("taskDuration"))
    return (deadlines, task_duration)


def _per_run_priority_metrics(csv_path: Path) -> pd.DataFrame:
    """Same metrics as `_per_run_metrics` but stratified by `priority`.

    Returns a DataFrame with one row per priority class observed in the run.
    `throughput` here is `n_completed_in_class / sim_duration` -- it shares
    the same denominator across classes so the columns add up to the run's
    overall throughput.
    """
    df = pd.read_csv(csv_path)
    if df.empty:
        return pd.DataFrame(columns=[
            "priority", "n_tasks", "completion_rate", "drop_rate",
            "expired_rate", "preempt_drop_rate",
            "mean_wait", "mean_turnaround", "total_drops", "throughput",
            "sim_finish_time",
        ])
    sim_duration = float(df["finalizedAt"].max())
    out_rows: List[Dict[str, Any]] = []
    for prio, grp in df.groupby("priority", sort=True):
        completed     = grp["outcome"] == "COMPLETED"
        # Match the aggregate definition: every non-COMPLETED outcome
        # (DROPPED + EXPIRED + leftover UNFINISHED) is a drop.
        dropped       = ~completed
        expired       = grp["outcome"] == "EXPIRED"
        preempt_drop  = grp["outcome"] == "DROPPED"
        dispatched = grp["waitTime"] >= 0
        n = len(grp)
        n_completed = int(completed.sum())
        out_rows.append({
            "priority":          int(prio),
            "n_tasks":           int(n),
            "completion_rate":   float(completed.mean()),
            "drop_rate":         float(dropped.mean()),
            "expired_rate":      float(expired.mean()),
            "preempt_drop_rate": float(preempt_drop.mean()),
            "mean_wait":         float(grp.loc[dispatched, "waitTime"].mean())
                                 if dispatched.any() else float("nan"),
            "mean_turnaround":   float(grp.loc[completed, "turnaroundTime"].mean())
                                 if completed.any() else float("nan"),
            "total_drops":       int(grp["dropEvents"].sum()),
            "throughput":        (n_completed / sim_duration
                                  if sim_duration > 0 else float("nan")),
            "sim_finish_time":   sim_duration,
        })
    return pd.DataFrame(out_rows)


def load_sweep_by_priority(sweep_dir: str | Path) -> pd.DataFrame:
    """Long-format DataFrame, one row per (algorithm, cell, rep, priority).

    Columns: ``algorithm``, the cleaned sweep-dim columns, ``rep``,
    ``priority``, plus the per-priority metrics. Useful for any analysis
    or plot that needs to break out behaviour by task priority class.
    """
    sweep_dir = Path(sweep_dir).expanduser().resolve()
    layout = _read_layout(sweep_dir)

    rows: List[Dict[str, Any]] = []
    value_lists = list(layout.sweep_params.values())
    for algo in layout.algorithms:
        algo_dir = sweep_dir / algo
        if not algo_dir.is_dir():
            continue
        for value_combo in itertools.product(*value_lists):
            cell_dir = algo_dir
            for dn, v in zip(layout.display_dims, value_combo):
                cell_dir = cell_dir / f"{dn}={_sanitize_for_path(v)}"
            if not cell_dir.is_dir():
                continue
            for rep in range(layout.repetitions):
                csv = cell_dir / f"rep{rep}-tasks.csv"
                if not csv.is_file():
                    continue
                prio_df = _per_run_priority_metrics(csv)
                for _, prow in prio_df.iterrows():
                    row: Dict[str, Any] = {"algorithm": algo}
                    for dn, v in zip(layout.display_dims, value_combo):
                        row[dn] = v
                    row["rep"] = rep
                    row.update(prow.to_dict())
                    rows.append(row)

    if not rows:
        raise RuntimeError(f"No per-rep CSVs found under {sweep_dir}")

    df = pd.DataFrame(rows)
    df["priority"] = df["priority"].astype(int)

    # ---- Normalised metrics ---------------------------------------------
    # Per-priority queue-wait deadlines and the nominal task duration come
    # from the sweep manifest. Without them the normalised metrics aren't
    # meaningful, so we emit NaNs (the plotter just shows empty axes).
    deadlines, task_duration = _read_priority_budgets(sweep_dir)
    deadline_series = df["priority"].map(deadlines).astype(float)
    df["mean_wait_norm"] = df["mean_wait"] / deadline_series
    denom_turn = (deadline_series + task_duration
                  if task_duration is not None else deadline_series)
    df["mean_turnaround_norm"] = df["mean_turnaround"] / denom_turn
    # throughput_norm = served fraction of the per-class demand
    #   = (n_completed_in_class / sim_duration) / (n_in_class / sim_duration)
    #   = n_completed_in_class / n_in_class
    # which is the per-class completion rate. Equivalent to completion_rate
    # but exposed as a "throughput" view (1.0 = perfectly served class).
    df["throughput_norm"] = df["completion_rate"]

    df.attrs["sweep_dir"] = str(sweep_dir)
    df.attrs["dims"]       = list(layout.display_dims)
    df.attrs["algorithms"] = list(layout.algorithms)
    df.attrs["deadlines_by_priority"] = dict(deadlines)
    df.attrs["task_duration_s"] = task_duration
    return df


# ---------------------------------------------------------------------------
# Tabular summaries / ranking
# ---------------------------------------------------------------------------
_SUMMARY_DEFAULT_METRICS = (
    "completion_rate",
    "drop_rate",
    "expired_rate",
    "mean_wait",
    "mean_turnaround",
)


def _apply_where(df: pd.DataFrame, where: Optional[Dict[str, Any]]) -> pd.DataFrame:
    """Filter a long-format sweep df by exact matches on cleaned dim columns.

    `where` keys are cleaned dim names (e.g. ``numMbs``,
    ``taskGenerationInterval``); values must match the stored value exactly
    (same string the sweep wrote, e.g. ``"exponential(15s)"``).
    """
    if not where:
        return df
    mask = pd.Series(True, index=df.index)
    for key, val in where.items():
        if key not in df.columns:
            raise KeyError(
                f"where key {key!r} is not a sweep dimension; "
                f"available: {[c for c in df.columns]}")
        mask &= (df[key] == val)
    out = df[mask]
    if out.empty:
        raise ValueError(f"where={where!r} selected no rows")
    return out


def summarize_sweep(
    sweep_dir: str | Path,
    metrics: Sequence[str] = _SUMMARY_DEFAULT_METRICS,
    where: Optional[Dict[str, Any]] = None,
    by: Sequence[str] | str = ("algorithm",),
) -> pd.DataFrame:
    """Mean-over-reps summary table for a sweep folder.

    Loads the aggregate per-run metrics (`load_sweep`), optionally slices to a
    single operating point via ``where``, then averages the requested
    ``metrics`` within each ``by`` group (default: one row per algorithm).

    Parameters
    ----------
    sweep_dir : path to a run_sweep output folder.
    metrics : aggregate metric columns to average (see `_per_run_metrics`).
    where : optional {dim: value} slice on the cleaned sweep dimensions.
    by : grouping columns; defaults to ``("algorithm",)``. Pass extra dims
        (e.g. ``("algorithm", "numDrones")``) to keep them un-collapsed.

    Returns
    -------
    DataFrame indexed by the ``by`` columns with one column per metric
    (plus ``n_tasks`` and ``n_runs`` for context).
    """
    if isinstance(by, str):
        by = (by,)
    by = list(by)
    df = load_sweep(sweep_dir)
    df = _apply_where(df, where)
    metrics = [m for m in metrics if m in df.columns]
    agg_cols = metrics + (["n_tasks"] if "n_tasks" in df.columns else [])
    grouped = df.groupby(by, dropna=False)
    out = grouped[agg_cols].mean()
    out["n_runs"] = grouped.size()
    return out.reset_index()


def score_sweep(
    sweep_dir: str | Path,
    priority_weights: Sequence[float] = (3, 2, 1),
    w_drop: float = 1.0,
    w_wait: float = 1.5,
    wait_budget: float = 120.0,
) -> pd.DataFrame:
    """Rank every (algorithm, cell) by a priority-weighted composite score.

    The score rewards completing important tasks, penalises drops, and
    penalises latency::

        wcompletion = sum_p( w_p * n_completed_p ) / sum_p( w_p * n_generated_p )
        wdrop       = sum_p( w_p * n_dropped_p   ) / sum_p( w_p * n_generated_p )
        score       = wcompletion - w_drop * wdrop - w_wait * (mean_wait / wait_budget)

    where ``w_p`` is the per-class importance from ``priority_weights``
    (index 0 = priority 1 = HIGH). Completion/drop counts are pooled across
    reps (sum of per-class ``completion_rate * n_tasks``); ``mean_wait`` is the
    aggregate dispatched-task mean wait per cell (from `load_sweep`).

    Parameters
    ----------
    priority_weights : per-class importance ramp; default ``(3, 2, 1)`` =
        ``priorityLevels + 1 - p``. Pass equal weights (e.g. ``(1, 1, 1)``)
        to recover the plain aggregate completion/drop.
    w_drop, w_wait : penalty weights for drop fraction and latency.
    wait_budget : seconds that map to a full ``w_wait`` latency penalty; a
        cell whose ``mean_wait == wait_budget`` loses exactly ``w_wait``.
        Default 120s = the MID-class deadline. Smaller budget => latency
        dominates the ranking.

    Returns
    -------
    DataFrame with columns ``algorithm``, the sweep dims, ``wcompletion``,
    ``wdrop``, ``mean_wait``, ``score`` -- sorted by ``score`` descending
    (row 0 is the winning cell).
    """
    dfp = load_sweep_by_priority(sweep_dir)
    dims = list(dfp.attrs.get("dims", []))
    keys = ["algorithm"] + dims

    weight_for = {
        i + 1: float(w) for i, w in enumerate(priority_weights)
    }
    w = dfp["priority"].map(weight_for).fillna(1.0).astype(float)
    n_gen = dfp["n_tasks"].astype(float)
    n_comp = dfp["completion_rate"].astype(float) * n_gen
    n_drop = dfp["drop_rate"].astype(float) * n_gen

    work = dfp[keys].copy()
    work["_wgen"] = w * n_gen
    work["_wcomp"] = w * n_comp
    work["_wdrop"] = w * n_drop
    pooled = work.groupby(keys, dropna=False)[["_wgen", "_wcomp", "_wdrop"]].sum()
    pooled["wcompletion"] = pooled["_wcomp"] / pooled["_wgen"]
    pooled["wdrop"] = pooled["_wdrop"] / pooled["_wgen"]

    # Aggregate latency per cell from the (non-stratified) loader.
    agg = load_sweep(sweep_dir)
    wait = agg.groupby(keys, dropna=False)["mean_wait"].mean()

    out = pooled[["wcompletion", "wdrop"]].join(wait)
    out["score"] = (
        out["wcompletion"]
        - w_drop * out["wdrop"]
        - w_wait * (out["mean_wait"] / wait_budget)
    )
    out = out.reset_index().sort_values("score", ascending=False, ignore_index=True)
    return out


# ---------------------------------------------------------------------------
# Plot helpers
# ---------------------------------------------------------------------------
def _metric_label(metric: str) -> str:
    return {
        "completion_rate": "Completion rate",
        "drop_rate":       "Drop rate",
        "mean_wait":       "Mean wait time (s)",
        "mean_turnaround": "Mean turnaround (s)",
        "total_drops":     "Total drop events",
        "n_tasks":         "Tasks generated",
        "throughput":      "Throughput (completed tasks / s)",
        "sim_finish_time": "Sim finish time (s)",
        "mean_wait_norm":       "Normalized mean wait (wait / deadline)",
        "mean_turnaround_norm": "Normalized mean turnaround (turnaround / (deadline + duration))",
        "throughput_norm":      "Normalized throughput (completed / generated)",
    }.get(metric, metric)


def _plot_2d_box(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
) -> Figure:
    """Side-by-side grouped box plots, one panel per algorithm."""
    d1, d2 = dims
    d1_vals = sorted(df[d1].unique())
    d2_vals = sorted(df[d2].unique())
    n_d1, n_d2 = len(d1_vals), len(d2_vals)
    group_width = 0.8
    box_width   = group_width / max(n_d2, 1)

    fig, axes = plt.subplots(
        1, len(algorithms),
        figsize=(max(6, 1.6 * n_d1 * len(algorithms)), 4.6),
        sharey=True, squeeze=False,
    )
    axes = axes[0]

    cmap = plt.get_cmap("tab10")
    colors = [cmap(j % 10) for j in range(n_d2)]

    for ax, algo in zip(axes, algorithms):
        algo_df = df[df["algorithm"] == algo]
        for j, d2v in enumerate(d2_vals):
            data = [
                algo_df[(algo_df[d1] == d1v) & (algo_df[d2] == d2v)][metric]
                       .dropna().values
                for d1v in d1_vals
            ]
            positions = [
                i + (j - (n_d2 - 1) / 2) * box_width
                for i in range(n_d1)
            ]
            bp = ax.boxplot(
                data, positions=positions, widths=box_width * 0.9,
                patch_artist=True, manage_ticks=False,
                medianprops={"color": "black"},
                flierprops={"marker": ".", "markersize": 3},
            )
            for box in bp["boxes"]:
                box.set_facecolor(colors[j])
                box.set_alpha(0.75)
                box.set_edgecolor("black")

        ax.set_xticks(range(n_d1))
        ax.set_xticklabels(d1_vals)
        ax.set_xlabel(d1)
        ax.set_title(algo)
        ax.grid(axis="y", linestyle=":", alpha=0.5)

    axes[0].set_ylabel(_metric_label(metric))

    legend_handles = [
        Patch(facecolor=colors[j], edgecolor="black", alpha=0.75,
              label=f"{d2}={d2_vals[j]}")
        for j in range(n_d2)
    ]
    fig.legend(
        handles=legend_handles,
        loc="lower center", ncol=min(n_d2, 6),
        bbox_to_anchor=(0.5, -0.02), frameon=False,
    )
    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(f"{_metric_label(metric)}{direction}  |  n_reps per box")
    fig.tight_layout(rect=(0, 0.05, 1, 0.95))
    return fig


def _plot_3d_heatmap(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
) -> Figure:
    """Heatmap grid: rows = algorithm, cols = values of the 3rd dim.

    Each panel shows mean(metric) over reps with `dims[0]` on the x-axis
    and `dims[1]` on the y-axis. A shared colour scale lets you read
    FIFO-vs-COST as "compare cell above to cell below".
    """
    d1, d2, d3 = dims
    d1_vals = sorted(df[d1].unique())
    d2_vals = sorted(df[d2].unique())
    d3_vals = sorted(df[d3].unique())

    # Aggregate once so we can compute a shared colour scale.
    agg = (df.groupby(["algorithm", d3, d2, d1])[metric]
             .mean().reset_index())
    vmin = float(np.nanmin(agg[metric].values))
    vmax = float(np.nanmax(agg[metric].values))
    if vmin == vmax:                   # all-equal -> avoid degenerate scale
        vmax = vmin + 1e-9

    n_rows = len(algorithms)
    n_cols = len(d3_vals)
    fig, axes = plt.subplots(
        n_rows, n_cols,
        figsize=(max(3.4 * n_cols, 5.5), max(3.0 * n_rows, 3.0)),
        squeeze=False,
    )

    cmap_name = "viridis_r" if metric in _LOWER_IS_BETTER else "viridis"
    cmap = plt.get_cmap(cmap_name)
    im = None
    for i, algo in enumerate(algorithms):
        for j, d3v in enumerate(d3_vals):
            ax = axes[i, j]
            sub = agg[(agg["algorithm"] == algo) & (agg[d3] == d3v)]
            # rows = d2 (y-axis), cols = d1 (x-axis)
            pivot = (sub.pivot(index=d2, columns=d1, values=metric)
                        .reindex(index=d2_vals, columns=d1_vals))
            im = ax.imshow(
                pivot.values, origin="lower", aspect="auto",
                cmap=cmap, vmin=vmin, vmax=vmax,
            )
            ax.set_xticks(range(len(d1_vals)))
            ax.set_xticklabels(d1_vals)
            ax.set_yticks(range(len(d2_vals)))
            ax.set_yticklabels(d2_vals)
            if i == n_rows - 1:
                ax.set_xlabel(d1)
            if j == 0:
                ax.set_ylabel(f"{algo}\n{d2}")
            ax.set_title(f"{d3}={d3v}", fontsize=10)

            # Annotate cells with their mean value.
            for yi, d2v in enumerate(d2_vals):
                for xi, d1v in enumerate(d1_vals):
                    val = pivot.iat[yi, xi]
                    if np.isnan(val):
                        continue
                    # white text on dark cells, black on light cells
                    norm_val = (val - vmin) / (vmax - vmin)
                    if metric in _LOWER_IS_BETTER:
                        norm_val = 1.0 - norm_val
                    txt_color = "white" if norm_val < 0.5 else "black"
                    ax.text(xi, yi, f"{val:.2g}",
                            ha="center", va="center",
                            fontsize=8, color=txt_color)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(f"{_metric_label(metric)}{direction}  |  mean over reps")
    fig.tight_layout(rect=(0, 0, 0.92, 0.96))
    cbar_ax = fig.add_axes((0.93, 0.12, 0.015, 0.76))
    fig.colorbar(im, cax=cbar_ax, label=_metric_label(metric))
    return fig


def _plot_3d_scatter(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
) -> Figure:
    """True 3-D scatter, one axes per algorithm.

    All three swept dimensions occupy the spatial axes (x=`dims[0]`,
    y=`dims[1]`, z=`dims[2]`); the metric is encoded as point colour with
    a shared colour bar so FIFO/COST/etc. are directly comparable. Each
    point is the per-cell mean over repetitions.
    """
    # Importing the projection class registers the "3d" projection.
    from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

    d1, d2, d3 = dims
    d1_vals = sorted(df[d1].unique())
    d2_vals = sorted(df[d2].unique())
    d3_vals = sorted(df[d3].unique())

    agg = (df.groupby(["algorithm", d1, d2, d3])[metric]
             .mean().reset_index())
    vmin = float(np.nanmin(agg[metric].values))
    vmax = float(np.nanmax(agg[metric].values))
    if vmin == vmax:                  # avoid a degenerate colour scale
        vmax = vmin + 1e-9

    cmap_name = "viridis_r" if metric in _LOWER_IS_BETTER else "viridis"
    cmap = plt.get_cmap(cmap_name)

    n = len(algorithms)
    fig = plt.figure(figsize=(max(4.6 * n, 6.0), 4.8))
    sc = None
    for i, algo in enumerate(algorithms):
        ax = fig.add_subplot(1, n, i + 1, projection="3d")
        sub = agg[agg["algorithm"] == algo]
        if not sub.empty:
            sc = ax.scatter(
                sub[d1].values, sub[d2].values, sub[d3].values,
                c=sub[metric].values, cmap=cmap, vmin=vmin, vmax=vmax,
                s=110, edgecolor="black", linewidths=0.4, depthshade=False,
            )
        ax.set_xlabel(d1)
        ax.set_ylabel(d2)
        ax.set_zlabel(d3)
        ax.set_title(algo)
        ax.set_xticks(d1_vals)
        ax.set_yticks(d2_vals)
        ax.set_zticks(d3_vals)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(f"{_metric_label(metric)}{direction}  |  mean over reps")
    fig.tight_layout(rect=(0, 0, 0.92, 0.94))
    cbar_ax = fig.add_axes((0.93, 0.15, 0.015, 0.72))
    if sc is not None:
        fig.colorbar(sc, cax=cbar_ax, label=_metric_label(metric))
    return fig


def _plot_2d_scatter3d(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
) -> Figure:
    """3-D view of a 2-D sweep: the metric itself is the Z axis.

    All algorithms are overlaid in a single 3-D axes so the surfaces can
    be compared directly (x = `dims[0]`, y = `dims[1]`, z = mean metric
    over reps; one colour per algorithm). A thin wireframe per algorithm
    is drawn under the markers to make the surface shape readable.
    """
    from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

    d1, d2 = dims
    d1_vals = sorted(df[d1].unique())
    d2_vals = sorted(df[d2].unique())

    agg = (df.groupby(["algorithm", d1, d2])[metric]
             .mean().reset_index())

    cmap = plt.get_cmap("tab10")
    colors = {algo: cmap(k % 10) for k, algo in enumerate(algorithms)}

    fig = plt.figure(figsize=(7.5, 5.5))
    ax = fig.add_subplot(1, 1, 1, projection="3d")

    # Build a (len(d2_vals), len(d1_vals)) mesh grid for the wireframes.
    X, Y = np.meshgrid(np.asarray(d1_vals, dtype=float),
                       np.asarray(d2_vals, dtype=float))

    for algo in algorithms:
        sub = agg[agg["algorithm"] == algo]
        if sub.empty:
            continue
        pivot = (sub.pivot(index=d2, columns=d1, values=metric)
                    .reindex(index=d2_vals, columns=d1_vals))
        Z = pivot.values.astype(float)
        # Wireframe so the shape is legible behind the scatter markers.
        ax.plot_wireframe(
            X, Y, Z,
            color=colors[algo], alpha=0.4, linewidth=0.9,
        )
        ax.scatter(
            sub[d1].values, sub[d2].values, sub[metric].values,
            color=colors[algo], s=45, edgecolor="black", linewidths=0.3,
            depthshade=False, label=algo,
        )

    ax.set_xlabel(d1)
    ax.set_ylabel(d2)
    ax.set_zlabel(_metric_label(metric))
    ax.set_xticks(d1_vals)
    ax.set_yticks(d2_vals)

    handles = [plt.Line2D([0], [0], color=colors[a], marker="o",
                          linestyle="-", linewidth=1.5, label=a)
               for a in algorithms]
    ax.legend(handles=handles, loc="upper left", frameon=False)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(f"{_metric_label(metric)}{direction}  |  mean over reps")
    fig.tight_layout()
    return fig


def _plot_line(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
    title_suffix: str = "",
) -> Figure:
    """Line plot: x = dims[0], one line per algorithm, panels for dims[1:].

    Each line shows mean(metric) over reps with a shaded \u00b1 std band.
    Layout:
      * 1 dim  -> single panel.
      * 2 dims -> 1 row of panels, one per value of dims[1].
      * 3 dims -> rows = dims[1] values, cols = dims[2] values.

    A shared y-axis across all panels makes the FIFO-vs-COST gap directly
    comparable cell-to-cell.
    """
    d1 = dims[0]
    d1_vals = sorted(df[d1].unique())
    panel_dims = list(dims[1:])

    if len(panel_dims) == 0:
        panel_combos: List[Tuple[Any, ...]] = [()]
        n_rows, n_cols = 1, 1
    elif len(panel_dims) == 1:
        d2 = panel_dims[0]
        d2_vals = sorted(df[d2].unique())
        panel_combos = [(v,) for v in d2_vals]
        n_rows, n_cols = 1, len(d2_vals)
    else:  # exactly 2 panel dims
        d2, d3 = panel_dims
        d2_vals = sorted(df[d2].unique())
        d3_vals = sorted(df[d3].unique())
        panel_combos = [(v2, v3) for v2 in d2_vals for v3 in d3_vals]
        n_rows, n_cols = len(d2_vals), len(d3_vals)

    fig, axes = plt.subplots(
        n_rows, n_cols,
        figsize=(max(3.6 * n_cols, 5.0), max(2.8 * n_rows, 3.4)),
        sharex=True, sharey=True, squeeze=False,
    )

    cmap = plt.get_cmap("tab10")
    colors = {algo: cmap(k % 10) for k, algo in enumerate(algorithms)}

    for idx, combo in enumerate(panel_combos):
        r, c = divmod(idx, n_cols)
        ax = axes[r, c]
        panel_df = df
        for pd_name, pd_val in zip(panel_dims, combo):
            panel_df = panel_df[panel_df[pd_name] == pd_val]

        for algo in algorithms:
            sub = panel_df[panel_df["algorithm"] == algo]
            if sub.empty:
                continue
            agg = (sub.groupby(d1)[metric]
                      .agg(["mean", "std", "count"])
                      .reindex(d1_vals))
            mean = agg["mean"].values
            std  = agg["std"].fillna(0.0).values
            ax.plot(d1_vals, mean,
                    marker="o", linewidth=1.7, color=colors[algo], label=algo)
            ax.fill_between(d1_vals, mean - std, mean + std,
                            color=colors[algo], alpha=0.15, linewidth=0)

        ax.grid(linestyle=":", alpha=0.5)
        if panel_dims:
            # For the task-load dimension the value is already a self-describing
            # RV distribution (e.g. "exponential(30s)"), so drop the verbose
            # "taskGenerationInterval=" prefix to keep panel titles short.
            title_bits = [
                f"{pd_val}" if pd_name == "taskGenerationInterval"
                else f"{pd_name}={pd_val}"
                for pd_name, pd_val in zip(panel_dims, combo)
            ]
            ax.set_title(" | ".join(title_bits), fontsize=10)
        if r == n_rows - 1:
            ax.set_xlabel(d1)
        if c == 0:
            ax.set_ylabel(_metric_label(metric))

    # Single legend at the top -- all panels share the same algorithm set.
    handles = [plt.Line2D([0], [0], color=colors[a], marker="o",
                          linewidth=1.7, label=a) for a in algorithms]
    fig.legend(handles=handles, loc="upper center",
               ncol=min(len(algorithms), 6),
               bbox_to_anchor=(0.5, 1.02), frameon=False)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(f"{_metric_label(metric)}{direction}{title_suffix}"
                 "  |  mean \u00b1 std over reps",
                 y=1.06)
    fig.tight_layout()
    return fig


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------
def plot_sweep(
    sweep_dir: str | Path,
    metric: str = "completion_rate",
    *,
    kind: str = "box",
    where: Optional[Dict[str, Any]] = None,
    save: Optional[str | Path] = None,
    show: bool = True,
) -> Tuple[Figure, pd.DataFrame]:
    """Render a comparison figure for a finished sweep.

    Parameters
    ----------
    sweep_dir : path
        Folder containing the algorithm subfolders (e.g. `sim_data/<label>/`).
    metric : str
        One of ``completion_rate, drop_rate, expired_rate,
        preempt_drop_rate, mean_wait, mean_turnaround, total_drops,
        n_tasks, throughput, sim_finish_time``. ``sim_finish_time`` is
        the wall-clock simulation end (max ``finalizedAt``) and is the
        right metric for comparing which algorithm drains the workload
        fastest overall.
    kind : {"box", "line", "scatter3d"}, default "box"
        Plot style.
          * ``"box"``  -- side-by-side grouped boxplots (one panel per
            algorithm, dim1 on x, dim2 as the box colour group). Shows the
            full per-rep distribution. Only used for 2-D sweeps; for 3-D
            sweeps it falls back to the heatmap grid (`_plot_3d_heatmap`).
          * ``"line"`` -- simple line chart with all algorithms overlaid
            in the same axes. For 2-D sweeps: one panel per value of dim2,
            x=dim1, one line per algorithm (mean over reps, shaded \u00b1 std).
            For 3-D sweeps: a grid of such line plots (rows = dim2,
            cols = dim3).
          * ``"scatter3d"`` -- true 3-D axes.
            For 2-D sweeps: one shared axes with `dim1`/`dim2` on the
            spatial axes and the metric on Z; algorithms are overlaid in
            different colours (point + wireframe per algo). For 3-D
            sweeps: one 3-D axes per algorithm with the three swept
            dimensions on the spatial axes and the metric encoded as
            point colour (shared colour bar).
    where : dict or None
        Optional slice on the cleaned sweep dimensions (e.g.
        ``{"numMbs": 3}``). Matching rows are kept and the fixed dims are
        dropped, so a 3-D sweep can be viewed as a 2-D plane. Values must
        match exactly (same string the sweep wrote).
    save : path or None
        If set, save the figure to this path (extension picks the format).
    show : bool
        Call `plt.show()` at the end (set False from scripts).

    Returns
    -------
    (Figure, DataFrame)
        The matplotlib figure and the long-format DataFrame used to build it.
    """
    if metric not in _VALID_METRICS:
        raise ValueError(
            f"metric={metric!r} not recognised. Options: {_VALID_METRICS}")
    if kind not in ("box", "line", "scatter3d"):
        raise ValueError(
            f"kind={kind!r} not recognised. "
            "Options: 'box', 'line', 'scatter3d'")

    df = load_sweep(sweep_dir)
    dims = list(df.attrs["dims"])
    algorithms = df.attrs["algorithms"]

    # Optional slice: filter rows to the given dim values and drop those dims,
    # reducing the sweep's dimensionality (e.g. a 3-D sweep sliced on numMbs=3
    # becomes the 2-D numDrones x gen plane).
    if where:
        df = _apply_where(df, where)
        dims = [d for d in dims if d not in where]
        if len(dims) not in (2, 3):
            raise ValueError(
                f"after where={where!r} the sweep has {len(dims)} free "
                "dimension(s); plot_sweep needs 2 or 3. Slice fewer dims.")

    if len(dims) == 2:
        if kind == "scatter3d":
            fig = _plot_2d_scatter3d(df, dims, algorithms, metric)
        elif kind == "line":
            fig = _plot_line(df, dims, algorithms, metric)
        else:
            fig = _plot_2d_box(df, dims, algorithms, metric)
    elif len(dims) == 3:
        if kind == "line":
            fig = _plot_line(df, dims, algorithms, metric)
        elif kind == "scatter3d":
            fig = _plot_3d_scatter(df, dims, algorithms, metric)
        else:
            fig = _plot_3d_heatmap(df, dims, algorithms, metric)
    else:
        raise ValueError(
            f"sweep has {len(dims)} dimensions; plot_sweep supports 2 or 3.")

    if save is not None:
        save = Path(save).expanduser()
        save.parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(save, bbox_inches="tight", dpi=120)
        logger.info("saved figure to %s", save)
    if show:
        plt.show()
    return fig, df


# ---------------------------------------------------------------------------
# Priority view
# ---------------------------------------------------------------------------
def _plot_priority_pooled(
    df: pd.DataFrame,
    algorithms: Sequence[str],
    metric: str,
    dims: Sequence[str],
    where: Optional[Dict[str, Any]],
) -> Figure:
    """Original pooled view: one dodged-box-per-algo group per priority.

    Distribution = one value per (rep, sweep cell) pair, pooled across the
    whole sweep (or whatever `where` filtered down to).
    """
    priorities = sorted(df["priority"].unique())
    n_p = len(priorities)
    n_a = len(algorithms)

    group_width = 0.8
    box_width   = group_width / max(n_a, 1)

    fig, ax = plt.subplots(figsize=(max(5.5, 1.3 * n_p * n_a), 4.4))
    cmap = plt.get_cmap("Set2")
    colors = [cmap(i % 8) for i in range(n_a)]

    for i, algo in enumerate(algorithms):
        data = [
            df[(df["algorithm"] == algo) & (df["priority"] == p)][metric]
              .dropna().values
            for p in priorities
        ]
        positions = [
            xi + (i - (n_a - 1) / 2) * box_width
            for xi in range(n_p)
        ]
        bp = ax.boxplot(
            data, positions=positions, widths=box_width * 0.9,
            patch_artist=True, manage_ticks=False,
            medianprops={"color": "black"},
            flierprops={"marker": ".", "markersize": 3},
        )
        for box in bp["boxes"]:
            box.set_facecolor(colors[i])
            box.set_alpha(0.8)
            box.set_edgecolor("black")

    ax.set_xticks(range(n_p))
    ax.set_xticklabels([f"priority={p}" for p in priorities])
    ax.set_ylabel(_metric_label(metric))
    ax.grid(axis="y", linestyle=":", alpha=0.5)

    legend_handles = [
        Patch(facecolor=colors[i], edgecolor="black", alpha=0.8, label=algo)
        for i, algo in enumerate(algorithms)
    ]
    ax.legend(handles=legend_handles, loc="best", frameon=False)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    if where:
        suffix = "  |  " + ", ".join(f"{k}={v}" for k, v in where.items())
    else:
        n_cells = (df[list(dims)].drop_duplicates().shape[0] if dims else 1)
        suffix = f"  |  pooled over {n_cells} sweep cells \u00d7 reps"
    ax.set_title(f"{_metric_label(metric)} by priority{direction}{suffix}")
    fig.tight_layout()
    return fig


def _plot_priority_grid(
    df: pd.DataFrame,
    dims: Sequence[str],
    algorithms: Sequence[str],
    metric: str,
) -> Figure:
    """Grid of per-sweep-cell panels showing priority breakdown.

    Layout (depends on sweep dimensionality):
      * 1 dim  -> 1 row of panels, one per value of dim[0].
      * 2 dims -> rows = dim[1] values, cols = dim[0] values
                  (matches the 2-D boxplot's x=dim1, y-direction=dim2).
      * 3 dims -> rows = (dim[1] x dim[2]), cols = dim[0] values.

    Inside each panel: x = priority, one line per algorithm, marker per
    priority class, mean across reps with a shaded \u00b1 std band.
    """
    if not dims:
        raise ValueError("priority grid requires at least one sweep dim")

    priorities = sorted(df["priority"].unique())
    n_p = len(priorities)

    if len(dims) == 1:
        d1 = dims[0]
        d1_vals = sorted(df[d1].unique())
        col_vals = [(v,) for v in d1_vals]
        row_vals: List[Tuple[Any, ...]] = [()]
        col_dims = [d1]
        row_dims: List[str] = []
    elif len(dims) == 2:
        d1, d2 = dims
        d1_vals = sorted(df[d1].unique())
        d2_vals = sorted(df[d2].unique())
        col_vals = [(v,) for v in d1_vals]
        row_vals = [(v,) for v in d2_vals]
        col_dims = [d1]
        row_dims = [d2]
    else:  # 3
        d1, d2, d3 = dims
        d1_vals = sorted(df[d1].unique())
        d2_vals = sorted(df[d2].unique())
        d3_vals = sorted(df[d3].unique())
        col_vals = [(v,) for v in d1_vals]
        row_vals = [(v2, v3) for v2 in d2_vals for v3 in d3_vals]
        col_dims = [d1]
        row_dims = [d2, d3]

    n_rows = len(row_vals)
    n_cols = len(col_vals)

    fig, axes = plt.subplots(
        n_rows, n_cols,
        figsize=(max(2.4 * n_cols, 4.5), max(2.0 * n_rows, 2.8)),
        sharex=True, sharey=True, squeeze=False,
    )

    cmap = plt.get_cmap("tab10")
    colors = {algo: cmap(k % 10) for k, algo in enumerate(algorithms)}

    for ri, row_combo in enumerate(row_vals):
        for ci, col_combo in enumerate(col_vals):
            ax = axes[ri, ci]
            panel_df = df
            for dn, dv in zip(col_dims, col_combo):
                panel_df = panel_df[panel_df[dn] == dv]
            for dn, dv in zip(row_dims, row_combo):
                panel_df = panel_df[panel_df[dn] == dv]

            for algo in algorithms:
                sub = panel_df[panel_df["algorithm"] == algo]
                if sub.empty:
                    continue
                agg = (sub.groupby("priority")[metric]
                          .agg(["mean", "std"])
                          .reindex(priorities))
                mean = agg["mean"].values
                std  = agg["std"].fillna(0.0).values
                ax.plot(priorities, mean,
                        marker="o", linewidth=1.6,
                        color=colors[algo], label=algo)
                ax.fill_between(priorities, mean - std, mean + std,
                                color=colors[algo], alpha=0.15, linewidth=0)

            ax.grid(linestyle=":", alpha=0.5)
            if ri == 0 and col_dims:
                ax.set_title(", ".join(f"{dn}={dv}"
                                       for dn, dv in zip(col_dims, col_combo)),
                             fontsize=9)
            if ci == 0 and row_dims:
                ax.set_ylabel(", ".join(f"{dn}={dv}"
                                        for dn, dv in zip(row_dims, row_combo))
                              + f"\n{_metric_label(metric)}",
                              fontsize=9)
            elif ci == 0:
                ax.set_ylabel(_metric_label(metric))
            if ri == n_rows - 1:
                ax.set_xlabel("priority")
            ax.set_xticks(priorities)

    # One legend for the whole figure.
    handles = [plt.Line2D([0], [0], color=colors[a], marker="o",
                          linewidth=1.6, label=a) for a in algorithms]
    fig.legend(handles=handles, loc="upper center",
               ncol=min(len(algorithms), 6),
               bbox_to_anchor=(0.5, 1.02), frameon=False)

    direction = " (lower is better)" if metric in _LOWER_IS_BETTER else ""
    fig.suptitle(
        f"{_metric_label(metric)} by priority \u00d7 sweep cell"
        f"{direction}  |  mean \u00b1 std over reps",
        y=1.06,
    )
    fig.tight_layout()
    return fig


# Class names for the standard 3-level priority scheme (1 = most urgent).
_PRIORITY_CLASS_NAMES = {1: "HIGH", 2: "MID", 3: "LOW"}


def _priority_panel_label(p: int, priorities: Sequence[int]) -> str:
    if set(priorities) <= set(_PRIORITY_CLASS_NAMES):
        return f"priority={p} ({_PRIORITY_CLASS_NAMES[p]})"
    return f"priority={p}"


def plot_by_priority(
    sweep_dir: str | Path,
    metric: str = "completion_rate",
    *,
    kind: str = "grid",
    where: Optional[Dict[str, Any]] = None,
    save: Optional[str | Path] = None,
    show: bool = True,
) -> Tuple[Figure | List[Figure], pd.DataFrame]:
    """Compare allocators broken out by task priority class.

    Parameters
    ----------
    sweep_dir : path
        Folder containing the algorithm subfolders.
    metric : str
        One of `_VALID_METRICS`. Computed PER priority class within each
        rep (e.g. ``completion_rate`` for priority=1 is the fraction of
        priority-1 tasks that completed in that run).
    kind : {"grid", "pooled", "matrix"}, default "grid"
        How to fold the sweep dimensions into the figure.
          * ``"grid"``   -- one panel per sweep cell; inside each panel
            x=priority, one line per algorithm (mean \u00b1 std over reps).
            Use this when you want to see how the priority effect varies
            across the parameter sweep. For 2-D sweeps the panels are laid
            out as rows = dim2, cols = dim1; for 3-D, rows = (dim2 x dim3),
            cols = dim1. Pair with `where=` to shrink the grid.
          * ``"pooled"`` -- the original pooled view: one dodged-box group
            per priority, distribution pooled across the whole sweep (or
            whatever `where` filtered down to). Loses the per-cell detail
            but is compact.
          * ``"matrix"`` -- one figure PER priority class, each laid out
            exactly like the aggregate ``plot_sweep(..., kind="line")``
            graph matrix (x = dim1, panel rows = dim2, panel cols = dim3,
            one line per algorithm, mean \u00b1 std over reps). Use this to
            read a per-class metric across the full sweep the same way as
            the aggregate metrics. Returns a list of figures.
    where : dict or None
        Optional filter to pin specific sweep dims, e.g.
        ``where={"numDrones": 20, "numMbs": 5}``. Useful for shrinking the
        ``"grid"``/``"matrix"`` views to a handful of cells, or for pinning
        the ``"pooled"`` view to a single operating point.
    save : path or None
        As in `plot_sweep`. For ``kind="matrix"`` the priority class is
        suffixed to the file stem (e.g. ``foo_p1.png``, ``foo_p2.png``).
    show : as in `plot_sweep`.
    """
    valid = _VALID_METRICS + _VALID_PRIORITY_ONLY_METRICS
    if metric not in valid:
        raise ValueError(
            f"metric={metric!r} not recognised. Options: {valid}")
    if kind not in ("grid", "pooled", "matrix"):
        raise ValueError(
            f"kind={kind!r} not recognised. Options: 'grid', 'pooled', 'matrix'")

    df = load_sweep_by_priority(sweep_dir)
    dims = df.attrs["dims"]
    algorithms = df.attrs["algorithms"]

    if where:
        unknown = set(where) - set(dims)
        if unknown:
            raise ValueError(
                f"where={where} references unknown dims {sorted(unknown)}; "
                f"sweep dims are {dims}")
        for k, v in where.items():
            df = df[df[k] == v]
        if df.empty:
            raise RuntimeError(f"where={where} matched no rows")

    remaining_dims = [d for d in dims if not (where and d in where)]

    if kind == "pooled":
        figs: List[Figure] = [
            _plot_priority_pooled(df, algorithms, metric, dims, where)]
    elif kind == "matrix":
        # One aggregate-style line matrix per priority class.
        priorities = sorted(df["priority"].unique())
        figs = [
            _plot_line(
                df[df["priority"] == p], remaining_dims, algorithms, metric,
                title_suffix=f"  |  {_priority_panel_label(p, priorities)}",
            )
            for p in priorities
        ]
    else:
        # If `where` pinned every sweep dim down to one value the grid
        # collapses to a single panel -- that's fine.
        figs = [_plot_priority_grid(df, remaining_dims, algorithms, metric)]

    if save is not None:
        save = Path(save).expanduser()
        save.parent.mkdir(parents=True, exist_ok=True)
        if len(figs) == 1:
            figs[0].savefig(save, bbox_inches="tight", dpi=120)
            logger.info("saved figure to %s", save)
        else:
            priorities = sorted(df["priority"].unique())
            for p, fig in zip(priorities, figs):
                path = save.with_stem(f"{save.stem}_p{p}")
                fig.savefig(path, bbox_inches="tight", dpi=120)
                logger.info("saved figure to %s", path)
    if show:
        plt.show()
    return (figs[0] if len(figs) == 1 else figs), df
