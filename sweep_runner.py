"""
sweep_runner.py -- automate UAV swarm task-allocation sweeps.

Drives the release `UavSwarmTA` binary headlessly (Cmdenv), iterating over
2 or 3 ini parameters and collecting per-task CSVs into a sweep folder
under `sim_data/`. Sequential execution; each sweep point is run N times.

The allocator algorithm is controlled by a dedicated `algorithm` argument
(not by `sweep_params`). Pass `algorithm="FIFO"` (default), `"COST"`, or
`"both"`. With `"both"`, the full sweep is run twice -- once per algorithm --
and the two result trees land in sibling subfolders so they can be loaded
side-by-side for comparison plots. The algorithm folder is always created,
even for a single algorithm, so downstream tooling can rely on a uniform
layout.

Layout produced for `sweep_params={"*.numDrones": [10, 15], "*.numMbs": [3, 5]}`
with `repetitions = 3` and `algorithm = "both"`::

    sim_data/<sweep_label>/
      combined_manifest.json           # lists algorithms run + per-algo summary
      FIFO/
        sweep_manifest.json            # full spec (params + value lists + rep count)
        sweep.log                      # top-level log (one line per run)
        numDrones=10/
          numMbs=3/
            sweep.ini                  # exact ini handed to the binary (one per cell)
            rep0-tasks.csv
            rep0.log                   # stdout/stderr of that run
            rep1-tasks.csv
            rep1.log
            ...
          numMbs=5/
            ...
        numDrones=15/
          ...
      COST/
        ...                            # mirrors FIFO/ subtree

Usage as a library::

    from sweep_runner import setup_environment, run_sweep
    setup_environment()  # once per Python session
    run_sweep(
        sweep_params={"*.numDrones": [10, 15, 20],
                      "*.numMbs":    [3, 5, 7]},
        repetitions=5,
        algorithm="both",
        label="drones_x_mbs",
    )

CLI::

    python sweep_runner.py --param '*.numDrones=[10,15,20]' \
                           --param '*.numMbs=[3,5,7]' \
                           --algorithm both \
                           --repetitions 5 --label drones_x_mbs
"""

from __future__ import annotations

import argparse
import ast
import itertools
import json
import logging
import os
import shlex
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# Paths -- everything is relative to the directory THIS file lives in, i.e.
# the project root (the folder containing `src/`, `simulations/`, etc.).
# ---------------------------------------------------------------------------
PROJECT_ROOT = Path(__file__).resolve().parent
SRC_DIR      = PROJECT_ROOT / "src"
SIM_DATA_DIR = PROJECT_ROOT / "sim_data"
BASE_INI     = SRC_DIR / "omnetpp.ini"
RELEASE_BIN  = SRC_DIR / "UavSwarmTA"            # MODE=release
DEBUG_BIN    = SRC_DIR / "UavSwarmTA_dbg"        # MODE=debug (fallback only)
OPP_ENV      = Path("/home/opp_env/.venv/bin/opp_env")

# Defaults that don't need bare-name prefixing (already explicit patterns).
_MAINNODE_PREFIX = "*.mainNode.app[0]."

# opp_env shell env-capture: which projects to load, which vars to inject,
# and where to cache the captured environment between calls.
#
# OPP_ENV_VERSION + OMNETPP_ROOT together are the marker the simulator
# binary checks for at startup (see omnetpp src/envir/evmain.cc); if either
# is missing it aborts with `<!> Error: This OMNeT++ installation cannot
# be used outside an opp_env shell.` -- so both MUST be in this list.
_OPP_ENV_PROJECTS = ["omnetpp-6.3.0", "inet-4.5.4"]
_OPP_ENV_VARS     = ["INET_ROOT", "OMNETPP_ROOT", "OMNETPP_VERSION",
                     "OMNETPP_RELEASE", "OMNETPP_IMAGE_PATH",
                     "OPP_ENV_VERSION", "PATH", "LD_LIBRARY_PATH",
                     "PYTHONPATH", "QT_PLUGIN_PATH", "TCL_LIBRARY",
                     "BUILD_MODES"]
_OPP_ENV_CACHE    = PROJECT_ROOT / ".opp_env_vars.json"

# Flipped to True by setup_environment(); checked by _check_environment().
_env_setup_done = False

logger = logging.getLogger("sweep_runner")


# ---------------------------------------------------------------------------
# Environment bootstrap -- replicates `opp_env shell <projects>` inside
# the current Python process so subprocesses (the simulator binary) see
# the right INET_ROOT / PATH / LD_LIBRARY_PATH / QT_PLUGIN_PATH etc.
# ---------------------------------------------------------------------------
def _capture_opp_env(projects: Sequence[str]) -> Dict[str, str]:
    """Run `opp_env shell <projects>` once and return its env as a dict."""
    sentinel = "<<<OPP_ENV_BEGIN>>>"
    script   = f"echo '{sentinel}'; env; exit\n"
    cmd = [str(OPP_ENV), "shell", "-w", str(PROJECT_ROOT.parent), *projects]
    proc = subprocess.run(cmd, input=script, capture_output=True,
                          text=True, timeout=180, check=True)
    _, _, payload = proc.stdout.partition(sentinel)
    env: Dict[str, str] = {}
    for line in payload.splitlines():
        k, sep, v = line.partition("=")
        if sep and k.isidentifier():
            env[k] = v
    return env


def setup_environment(force: bool = False, verbose: bool = True) -> Dict[str, str]:
    """Inject opp_env's environment into this Python process.

    Required when calling `run_sweep` from a kernel/process that wasn't
    launched inside an `opp_env shell` (e.g. a Jupyter notebook). Captures
    the env via `opp_env shell`, caches it to `.opp_env_vars.json` next to
    this file, and merges the relevant vars into `os.environ`.

    Parameters
    ----------
    force : bool
        Re-run `opp_env shell` even if the cache file already exists.
    verbose : bool
        Print a short summary of which vars were applied.

    Returns
    -------
    dict
        The subset of vars that were applied to `os.environ`.
    """
    global _env_setup_done
    if not force and _OPP_ENV_CACHE.exists():
        env = json.loads(_OPP_ENV_CACHE.read_text())
        source = f"cache ({_OPP_ENV_CACHE.name})"
    else:
        if verbose:
            print("Running `opp_env shell` to capture environment (~30s)...")
        env = _capture_opp_env(_OPP_ENV_PROJECTS)
        _OPP_ENV_CACHE.write_text(json.dumps(env, indent=2))
        source = "opp_env shell (cached)"

    applied: Dict[str, str] = {}
    for k in _OPP_ENV_VARS:
        if k in env:
            os.environ[k] = env[k]
            applied[k] = env[k]

    _env_setup_done = True
    if verbose:
        print(f"opp_env loaded from {source}:")
        for k, v in applied.items():
            short = v if len(v) < 80 else v[:77] + "..."
            print(f"  {k}={short}")
    return applied


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
def _sanitize_for_path(s: str) -> str:
    """Make a value safe to use as a folder name."""
    bad = ' /\\:"\'<>|?*\t\n'
    out = "".join("_" if c in bad else c for c in str(s))
    # Drop leading dots / dashes that confuse shell tools.
    return out.lstrip(".-") or "_"


def _display_name_for_key(key: str) -> str:
    """Strip ini-pattern noise from a sweep key for use in folder names.

    `*.numDrones`         -> `numDrones`
    `**.commTimeoutDuration` -> `commTimeoutDuration`
    `*.mainNode.app[0].taskLimit` -> `taskLimit`
    `algorithmType`       -> `algorithmType`

    The full pattern is still written verbatim INSIDE sweep.ini; this is
    purely a cosmetic shortening so cell folders don't start with `*` (a
    shell-glob character that breaks `ls some/path/*`).
    """
    for prefix in (_MAINNODE_PREFIX, "**.", "*."):
        if key.startswith(prefix):
            return key[len(prefix):]
    return key


def _full_param_name(bare_name: str) -> str:
    """Map a bare ini param name to its mainNode-app pattern.

    Sweep keys are passed as bare names per the spec; this prepends the
    standard `*.mainNode.app[0].` prefix. Two exceptions:
      * names containing `.` or `*` are assumed to be full ini patterns
        already (e.g. `**.drone_comm_range`) -- left alone.
      * names containing `-` are OMNeT++ GLOBAL config options
        (`sim-time-limit`, `seed-set`, `cmdenv-interactive`, ...) -- they
        live at the section level, not on a module, so leave them alone.
    """
    if any(c in bare_name for c in ".*"):
        return bare_name
    if "-" in bare_name:
        return bare_name
    return _MAINNODE_PREFIX + bare_name


def _algorithm_for_cell(cell_overrides: Dict[str, Any]) -> str:
    """Pick the algorithm string for the {algorithm} placeholder in the
    per-task CSV path. Defaults to FIFO if the sweep doesn't pin it.
    """
    for k, v in cell_overrides.items():
        if k == "algorithmType":
            return str(v).strip('"')
    return "FIFO"


def _validate_sweep(sweep_params: Dict[str, Sequence[Any]]) -> None:
    n = len(sweep_params)
    if n not in (2, 3):
        raise ValueError(
            f"sweep_params must have 2 or 3 entries (got {n}). "
            "More than 3 dimensions makes the folder tree unwieldy; if you "
            "really need it, edit run_sweep() to lift this check."
        )
    for k, vs in sweep_params.items():
        if not isinstance(vs, (list, tuple)) or len(vs) == 0:
            raise ValueError(
                f"sweep_params['{k}'] must be a non-empty list of values."
            )


# ---------------------------------------------------------------------------
# Per-cell ini generation
# ---------------------------------------------------------------------------
def _write_cell_ini(
    cell_dir: Path,
    cell_overrides: Dict[str, Any],
    extra_overrides: Dict[str, Any],
    repetitions: int,
) -> Path:
    """Write a sweep.ini that = base ini + overrides for this sweep cell.

    Notes:
      * `include` pulls in the project's main ini verbatim, so any future
        tweak there (e.g. new params) propagates automatically.
      * `ask_user = false` is forced so the GUI prompt never fires under
        Cmdenv.
      * `csvOutputPath` is pinned to `rep{runnumber}-tasks.csv` (relative
        to the cell folder). The binary is launched with cwd=cell_dir, so
        each repetition lands a single CSV right next to its sweep.ini.
      * Values from `cell_overrides` and `extra_overrides` are written
        VERBATIM after `=`. The caller is responsible for quoting strings,
        adding units, etc. (per spec).
    """
    cell_dir.mkdir(parents=True, exist_ok=True)
    ini_path = cell_dir / "sweep.ini"

    # The base ini lives at <project>/src/omnetpp.ini. `include` paths are
    # resolved relative to the including file, so compute that here.
    rel_to_base = os.path.relpath(BASE_INI, start=cell_dir)

    with ini_path.open("w") as f:
        f.write("# Auto-generated by sweep_runner.py -- do not hand-edit.\n")
        f.write(f"# Generated: {datetime.now().isoformat(timespec='seconds')}\n")
        f.write("\n")
        # OMNeT++ ini precedence: FIRST matching assignment wins. So the
        # overrides MUST appear above the `include` line, otherwise the
        # base ini's values would shadow them.
        f.write("[General]\n")
        f.write("# Hard overrides that the sweep runner always sets.\n")
        f.write(f"{_MAINNODE_PREFIX}ask_user = false\n")
        f.write(f"{_MAINNODE_PREFIX}csvOutputPath = \"rep{{runnumber}}-tasks.csv\"\n")
        # Make every repetition use a different random seed; otherwise -r 0,
        # -r 1, ... would all produce identical runs.
        f.write(f"repeat = {repetitions}\n")
        f.write("seed-set = ${runnumber}\n")
        f.write("\n")
        f.write("# Sweep-point overrides.\n")
        for k, v in cell_overrides.items():
            f.write(f"{_full_param_name(k)} = {v}\n")
        if extra_overrides:
            f.write("\n# Caller-supplied extra overrides (held constant).\n")
            for k, v in extra_overrides.items():
                f.write(f"{_full_param_name(k)} = {v}\n")
        f.write("\n")
        f.write("# Inherit everything else (network setup, mobility, weights, ...).\n")
        f.write(f"include {rel_to_base}\n")

    return ini_path


# ---------------------------------------------------------------------------
# Running the binary
# ---------------------------------------------------------------------------
def _check_environment() -> None:
    """Best-effort sanity checks. Raises with actionable error messages."""
    if not BASE_INI.exists():
        raise FileNotFoundError(f"Base ini not found: {BASE_INI}")
    if not RELEASE_BIN.exists():
        raise FileNotFoundError(
            f"Release binary not found: {RELEASE_BIN}\n"
            f"  Build it with: cd {SRC_DIR} && make MODE=release -j$(nproc)\n"
            f"  (inside an `opp_env shell omnetpp-6.3.0 inet-4.5.4` session)"
        )
    inet_root = os.environ.get("INET_ROOT")
    if not inet_root:
        if _env_setup_done:
            raise EnvironmentError(
                "setup_environment() ran but did not populate $INET_ROOT. "
                "Delete the cache file and retry: "
                f"`rm {_OPP_ENV_CACHE}` then `setup_environment(force=True)`."
            )
        raise EnvironmentError(
            "$INET_ROOT is not set. Either (a) call "
            "`sweep_runner.setup_environment()` first (works from any Python "
            "process, including Jupyter), or (b) launch this script from "
            "inside `opp_env shell omnetpp-6.3.0 inet-4.5.4`."
        )


def _build_run_command(ini_path: Path, repetition: int) -> List[str]:
    """Build the argv to launch a single Cmdenv run.

    Layout: cwd = cell folder, so relative csvOutputPath = "rep{runnumber}-tasks.csv"
    lands inside the cell. The binary is invoked by absolute path. NED paths
    are passed absolute too (since cwd is the cell, not src/).
    """
    inet_src = Path(os.environ["INET_ROOT"]) / "src"
    inet_lib = inet_src / "INET"
    return [
        str(RELEASE_BIN),
        "-u", "Cmdenv",
        "-n", f"{SRC_DIR}:{inet_src}",
        "-l", str(inet_lib),
        "-c", "General",
        "-r", str(repetition),
        str(ini_path),
    ]


def _run_one(
    ini_path: Path,
    cell_dir: Path,
    repetition: int,
    timeout_sec: Optional[float],
) -> Tuple[bool, float, Path]:
    """Run a single repetition. Returns (success, wall_seconds, log_path)."""
    log_path = cell_dir / f"rep{repetition}.log"
    argv = _build_run_command(ini_path, repetition)
    start = time.monotonic()
    with log_path.open("w") as logf:
        logf.write(f"# Command: {' '.join(shlex.quote(a) for a in argv)}\n")
        logf.write(f"# Cwd:     {cell_dir}\n")
        logf.write(f"# Started: {datetime.now().isoformat(timespec='seconds')}\n")
        logf.write("# ----- begin simulator stdout/stderr -----\n")
        logf.flush()
        try:
            proc = subprocess.run(
                argv, cwd=cell_dir, stdout=logf, stderr=subprocess.STDOUT,
                timeout=timeout_sec, check=False,
            )
            ok = (proc.returncode == 0)
            logf.write(f"\n# ----- end (exit={proc.returncode}) -----\n")
        except subprocess.TimeoutExpired:
            ok = False
            logf.write(f"\n# ----- TIMEOUT after {timeout_sec}s -----\n")
    return ok, time.monotonic() - start, log_path


# ---------------------------------------------------------------------------
# Public entry point
# ---------------------------------------------------------------------------
_VALID_ALGORITHMS = ("FIFO", "FIFO_PRIO", "FIFO_PREEMPT", "COST")
_ALGORITHM_GROUPS = {
    "BOTH":     ["FIFO", "COST"],                            # legacy: baseline pair
    "ALL":      list(_VALID_ALGORITHMS),                     # every implemented variant
    "FIFO_ALL": ["FIFO", "FIFO_PRIO", "FIFO_PREEMPT"],       # the three FIFO families
}


def _normalize_algorithm(algorithm: Any) -> List[str]:
    """Turn the user-facing `algorithm` arg into a list of algo names.

    Accepts:
      * `"FIFO"`, `"FIFO_PRIO"`, `"FIFO_PREEMPT"`, `"COST"` (case-insensitive)
        -> single-algo list
      * `"both"`     -> ["FIFO", "COST"]                (legacy baseline pair)
      * `"all"`      -> every implemented variant       (4 algos)
      * `"fifo_all"` -> the three FIFO variants
      * an iterable of any of the above -> deduped & order-preserved
    """
    if algorithm is None:
        return ["FIFO"]
    if isinstance(algorithm, str):
        s = algorithm.strip().upper()
        if s in _ALGORITHM_GROUPS:
            return list(_ALGORITHM_GROUPS[s])
        if s in _VALID_ALGORITHMS:
            return [s]
        raise ValueError(
            f"Unknown algorithm {algorithm!r}. "
            f"Expected one of: {list(_VALID_ALGORITHMS)}, "
            f"or a group keyword: {sorted(_ALGORITHM_GROUPS)} "
            f"(or a list of any of those)."
        )
    # Iterable case.
    out: List[str] = []
    for item in algorithm:
        for a in _normalize_algorithm(item):
            if a not in out:
                out.append(a)
    if not out:
        raise ValueError("algorithm list is empty.")
    return out


def run_sweep(
    sweep_params: Dict[str, Sequence[Any]],
    repetitions: int = 5,
    label: Optional[str] = None,
    algorithm: Any = "FIFO",
    extra_overrides: Optional[Dict[str, Any]] = None,
    timeout_sec: Optional[float] = None,
    output_root: Optional[Path] = None,
) -> Path:
    """Run a sweep over `sweep_params` (2 or 3 keys), `repetitions` times each.

    Output layout (uniform whether one or both algorithms are selected)::

        sim_data/<label>/
          combined_manifest.json   # algorithms run, sweep spec, paths
          FIFO/
            sweep.log
            sweep_manifest.json
            <dim1>=.../<dim2>=.../repN-tasks.csv
          COST/
            ...

    Parameters
    ----------
    sweep_params : dict[str, list]
        Keys are bare ini param names (e.g. "taskLimit"); the runner prepends
        `*.mainNode.app[0].` automatically (see `_full_param_name`). Values
        are written VERBATIM after `=`, so string params need embedded
        quotes ('"FIFO"') and numeric params need units ("10s").
    repetitions : int
        Number of runs per sweep cell, per algorithm. Each uses a different
        OMNeT++ runnumber (-r 0, -r 1, ...) so seed-set differs.
    label : str, optional
        Short name for the sweep; becomes the folder under sim_data/.
        Defaults to a timestamp.
    algorithm : str or list, default "FIFO"
        Which allocator(s) to sweep with. Independent of `sweep_params` so
        the same dimensions are run for each algorithm and the two trees
        can be plotted side-by-side. Accepted:
          * "FIFO" or "COST"             -> single algorithm
          * "both" / "all"               -> both, sequentially
          * ["FIFO", "COST"]             -> explicit list
        Conflicts with `algorithmType` in `extra_overrides`.
    extra_overrides : dict, optional
        Additional ini overrides held constant across all sweep cells
        (same value/format rules as sweep_params). NOTE: passing
        `algorithmType` here is rejected -- use the `algorithm` arg instead.
    timeout_sec : float, optional
        Per-run wall-clock cap. None = no limit.
    output_root : Path, optional
        Override the default sim_data/ root.

    Returns
    -------
    Path
        The top-level sweep folder. Contains one subfolder per algorithm,
        plus a combined manifest pointing into each.
    """
    _validate_sweep(sweep_params)
    extra_overrides = dict(extra_overrides or {})
    algorithms = _normalize_algorithm(algorithm)

    # Reject ambiguous spec: the algorithm arg owns algorithmType.
    for k in list(extra_overrides):
        if _full_param_name(k) == f"{_MAINNODE_PREFIX}algorithmType":
            raise ValueError(
                f"extra_overrides contains '{k}', which collides with the "
                f"`algorithm` argument. Drop it from extra_overrides and "
                f"pass `algorithm={extra_overrides[k]!r}` (or 'both') instead."
            )

    _check_environment()

    root = (output_root or SIM_DATA_DIR).resolve()
    sweep_name = label or datetime.now().strftime("sweep_%Y-%m-%d_%H-%M-%S")
    top_dir = root / _sanitize_for_path(sweep_name)
    top_dir.mkdir(parents=True, exist_ok=True)

    combined = {
        "label":         sweep_name,
        "generated_at":  datetime.now().isoformat(timespec="seconds"),
        "algorithms":    algorithms,
        "sweep_params":  {k: list(v) for k, v in sweep_params.items()},
        "repetitions":   repetitions,
        "extra_overrides": extra_overrides,
        "runs":          {},   # algo -> {sweep_dir, n_failures, elapsed_sec}
    }

    start_all = time.monotonic()
    for algo in algorithms:
        algo_dir = top_dir / algo
        algo_dir.mkdir(parents=True, exist_ok=True)
        algo_overrides = dict(extra_overrides)
        # Re-add quotes for ini parse() (string param).
        algo_overrides["algorithmType"] = f'"{algo}"'

        summary = _run_single_sweep(
            sweep_params=sweep_params,
            repetitions=repetitions,
            sweep_dir=algo_dir,
            sweep_name=f"{sweep_name}/{algo}",
            extra_overrides=algo_overrides,
            timeout_sec=timeout_sec,
        )
        combined["runs"][algo] = {
            "sweep_dir":   str(algo_dir.relative_to(top_dir)),
            "n_failures":  summary["n_failures"],
            "elapsed_sec": summary["elapsed_sec"],
            "n_runs":      summary["n_runs"],
        }

    combined["total_elapsed_sec"] = round(time.monotonic() - start_all, 3)
    (top_dir / "combined_manifest.json").write_text(json.dumps(combined, indent=2))

    return top_dir


def _run_single_sweep(
    sweep_params: Dict[str, Sequence[Any]],
    repetitions: int,
    sweep_dir: Path,
    sweep_name: str,
    extra_overrides: Dict[str, Any],
    timeout_sec: Optional[float],
) -> Dict[str, Any]:
    """Execute one full sweep (all cells x all reps) for a single algorithm.

    Writes `sweep.log` + `sweep_manifest.json` inside `sweep_dir`, plus the
    per-cell tree underneath. Returns a small summary dict for the combined
    manifest at the parent level.
    """
    _configure_top_log(sweep_dir / "sweep.log")
    logger.info("Sweep '%s' starting -- output dir: %s", sweep_name, sweep_dir)
    logger.info("Sweep dimensions: %s", {k: list(v) for k, v in sweep_params.items()})
    logger.info("Repetitions per cell: %d", repetitions)
    if extra_overrides:
        logger.info("Extra (constant) overrides: %s", extra_overrides)

    manifest = {
        "label":            sweep_name,
        "generated_at":     datetime.now().isoformat(timespec="seconds"),
        "project_root":     str(PROJECT_ROOT),
        "base_ini":         str(BASE_INI),
        "binary":           str(RELEASE_BIN),
        "binary_mtime":     datetime.fromtimestamp(RELEASE_BIN.stat().st_mtime).isoformat(timespec="seconds"),
        "sweep_params":     {k: list(v) for k, v in sweep_params.items()},
        "repetitions":      repetitions,
        "extra_overrides":  extra_overrides,
        "results": [],      # filled in below
    }

    keys = list(sweep_params.keys())
    value_lists = [list(sweep_params[k]) for k in keys]

    n_cells = 1
    for vs in value_lists:
        n_cells *= len(vs)
    total_runs = n_cells * repetitions
    logger.info("Total cells: %d (%d runs)", n_cells, total_runs)

    start_wall = time.monotonic()
    run_idx = 0
    n_failures = 0

    for combo in itertools.product(*value_lists):
        cell_overrides = dict(zip(keys, combo))
        # Folder per dimension, in the order keys were declared. This gives
        # the user a predictable tree (first key = outermost folder). The
        # `*.` / `**.` / `*.mainNode.app[0].` prefix is stripped from the
        # folder name only -- the full pattern is still written in sweep.ini.
        cell_path_parts = [
            f"{_display_name_for_key(k)}={_sanitize_for_path(v)}"
            for k, v in cell_overrides.items()
        ]
        cell_dir = sweep_dir.joinpath(*cell_path_parts)
        ini_path = _write_cell_ini(cell_dir, cell_overrides, extra_overrides,
                                   repetitions)
        logger.info("[cell] %s", " | ".join(cell_path_parts))

        for rep in range(repetitions):
            run_idx += 1
            logger.info("  -> run %d/%d (rep=%d) starting",
                        run_idx, total_runs, rep)
            ok, secs, log_path = _run_one(ini_path, cell_dir, rep, timeout_sec)
            csv_name = f"rep{rep}-tasks.csv"
            csv_path = cell_dir / csv_name
            result = {
                "cell":       cell_overrides,
                "repetition": rep,
                "success":    ok,
                "wall_sec":   round(secs, 3),
                "log":        str(log_path.relative_to(sweep_dir)),
                "csv":        str(csv_path.relative_to(sweep_dir)) if csv_path.exists() else None,
            }
            manifest["results"].append(result)
            if not ok:
                n_failures += 1
                logger.warning("     FAILED (%.2fs) -- see %s",
                               secs, log_path.relative_to(sweep_dir))
            elif not csv_path.exists():
                logger.warning("     OK (%.2fs) but CSV missing at %s",
                               secs, csv_path.relative_to(sweep_dir))
            else:
                logger.info("     ok (%.2fs) -> %s",
                            secs, csv_path.relative_to(sweep_dir))

    elapsed = time.monotonic() - start_wall
    logger.info("Sweep done in %.1fs (%d/%d failed)",
                elapsed, n_failures, total_runs)

    manifest["elapsed_sec"] = round(elapsed, 3)
    manifest["n_failures"]  = n_failures
    (sweep_dir / "sweep_manifest.json").write_text(json.dumps(manifest, indent=2))

    return {
        "n_failures":  n_failures,
        "elapsed_sec": round(elapsed, 3),
        "n_runs":      total_runs,
    }


# ---------------------------------------------------------------------------
# Logging setup -- file + console, idempotent across re-imports
# ---------------------------------------------------------------------------
def _configure_top_log(log_path: Path) -> None:
    # Remove any handlers from a previous call so re-imports don't pile up.
    for h in list(logger.handlers):
        logger.removeHandler(h)
    logger.setLevel(logging.INFO)
    fmt = logging.Formatter("%(asctime)s %(levelname)s %(message)s",
                            datefmt="%H:%M:%S")
    fh = logging.FileHandler(log_path, mode="w")
    fh.setFormatter(fmt)
    logger.addHandler(fh)
    sh = logging.StreamHandler(sys.stdout)
    sh.setFormatter(fmt)
    logger.addHandler(sh)


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
def _parse_param_arg(spec: str) -> Tuple[str, List[Any]]:
    """Parse `name=[v1, v2, ...]` (Python-literal value list)."""
    if "=" not in spec:
        raise argparse.ArgumentTypeError(
            f"--param must be 'name=[v1,v2,...]', got: {spec}")
    name, _, raw = spec.partition("=")
    name = name.strip()
    try:
        values = ast.literal_eval(raw)
    except (ValueError, SyntaxError) as e:
        raise argparse.ArgumentTypeError(
            f"could not parse value list for {name!r}: {e}")
    if not isinstance(values, (list, tuple)):
        raise argparse.ArgumentTypeError(
            f"value list for {name!r} must be a Python list, got {type(values).__name__}")
    return name, list(values)


def _main(argv: Optional[Sequence[str]] = None) -> int:
    p = argparse.ArgumentParser(
        description="Run a UAV swarm parameter sweep (2 or 3 dimensions).")
    p.add_argument(
        "--param", action="append", required=True, metavar="NAME=[v1,v2,...]",
        help="Bare ini param name and the list of values to sweep. Repeat "
             "the flag for each dimension (2 or 3 total). Values are written "
             "verbatim, so quote strings and include units, e.g. "
             "--param 'algorithmType=[\"FIFO\",\"COST\"]' --param 'taskLimit=[10,50]'")
    p.add_argument("--repetitions", "-r", type=int, default=5,
                   help="Repetitions per sweep cell (default: 5).")
    p.add_argument("--label", "-l", default=None,
                   help="Sweep folder name under sim_data/ (default: timestamp).")
    p.add_argument("--timeout", type=float, default=None,
                   help="Per-run wall-clock timeout in seconds (default: none).")
    p.add_argument("--extra", action="append", default=[], metavar="NAME=VALUE",
                   help="Extra ini override held constant across all cells. "
                        "Value is written verbatim. Repeatable.")
    p.add_argument("--algorithm", "-a", default="FIFO",
                   choices=["FIFO", "FIFO_PRIO", "FIFO_PREEMPT", "COST",
                            "both", "all", "fifo_all"],
                   help="Allocator(s) to sweep with. Group keywords: "
                        "'both' = FIFO+COST (legacy baseline pair), "
                        "'all' = every implemented variant (4 algos), "
                        "'fifo_all' = the three FIFO variants. "
                        "Each algorithm's results land under "
                        "sim_data/<label>/<ALGO>/ for side-by-side "
                        "comparison (default: FIFO).")
    args = p.parse_args(argv)

    sweep_params: Dict[str, List[Any]] = {}
    for spec in args.param:
        name, values = _parse_param_arg(spec)
        sweep_params[name] = values

    extra_overrides: Dict[str, str] = {}
    for spec in args.extra:
        if "=" not in spec:
            p.error(f"--extra must be NAME=VALUE, got: {spec}")
        k, _, v = spec.partition("=")
        extra_overrides[k.strip()] = v

    try:
        sweep_dir = run_sweep(
            sweep_params=sweep_params,
            repetitions=args.repetitions,
            label=args.label,
            algorithm=args.algorithm,
            extra_overrides=extra_overrides,
            timeout_sec=args.timeout,
        )
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2
    print(f"\nSweep done. Results in: {sweep_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(_main())
