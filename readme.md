# UAV Swarm Task Allocation (Swarm + MBS)

OMNeT++/INET simulation of a UAV swarm coordinated by a stationary main node and
a small fleet of Mobile Base Stations (MBSs). Four task-allocation strategies
are implemented and can be swapped at run time via the `algorithmType` ini
parameter:

| Variant | Queue order | Drone pick | Preempts busy drones? |
|---|---|---|---|
| `FIFO`         | insertion order   | closest idle           | no |
| `FIFO_PRIO`    | priority-ordered  | closest idle           | no |
| `FIFO_PREEMPT` | priority-ordered  | closest idle, then closest busy on a strictly lower-priority task | yes |
| `COST`         | priority-ordered  | full auction (energy + halt-penalty + group-size weights) | yes, including MBS relocation |

The four make a natural progression — each adds one capability (priority
queueing, then drone preemption, then cost-weighted auction) so sweeps
that compare all four show which feature buys what.

The repo supports two workflows:

1. **Interactive single runs** in the OMNeT++ IDE / Qtenv (visual feedback, GUI
   parameter dialog, per-task CSV).
2. **Automated parameter sweeps** from a Python script or Jupyter notebook
   (headless, multi-run, organized CSV output under `sim_data/`).

---

## 1. Prerequisites

This project targets the Nix-managed `opp_env` toolchain:

| Component | Version |
|---|---|
| OMNeT++   | 6.3.0   |
| INET      | 4.5.4   |
| `opp_env` | 0.35+   |

All builds and runs must happen inside an `opp_env shell` (or be wrapped by
`opp_env run`); the OMNeT++ binaries refuse to start otherwise.

Open an interactive shell once with:

```bash
opp_env shell omnetpp-6.3.0 inet-4.5.4
```

---

## 2. Building

From inside the opp_env shell:

```bash
cd src
make MODE=release -j$(nproc)   # produces src/UavSwarmTA
make MODE=debug   -j$(nproc)   # produces src/UavSwarmTA_dbg  (optional)
```

If you ever regenerate the makefiles (`make makefiles` in the project root),
the top-level [Makefile](Makefile) preserves the INET include/lib flags
automatically.

---

## 3. Manual use through the OMNeT++ IDE

A convenience launcher script is provided so the IDE inherits the
`opp_env` environment:

```bash
./launch-omnetpp.sh
```

That runs `opp_env run omnetpp-6.3.0 inet-4.5.4 -c omnetpp`, opening the IDE
with the correct `OMNETPP_ROOT`, `INET_ROOT`, `PATH`, Qt plugin paths, etc.

### Running a simulation

1. In the IDE, open [src/omnetpp.ini](src/omnetpp.ini).
2. Right-click → **Run As → OMNeT++ Simulation** (or click the green ▶).
3. Pick **Qtenv** when prompted.

### The ask-user parameter dialog

If `*.mainNode.app[0].ask_user = true` (the default), a custom modal Qt
dialog pops up at sim start with one field per tunable parameter:

| Parameter | Notes |
|---|---|
| `algorithmType` | Dropdown: `FIFO`, `FIFO_PRIO`, `FIFO_PREEMPT`, `COST` |
| `taskLimit` | Maximum concurrent tasks |
| `taskGenerationInterval` | Dropdown of common generators (`uniform`, `normal`, `exponential`, fixed); also editable for any NED expression |
| `taskDuration` | Constant per-task duration (seconds) |
| `bumpDroppedPriority` | Re-queue dropped tasks at higher priority |
| `priorityLevels` | Number of priority classes (1 = HIGH .. N = LOW). Default 3 |
| `priorityWeights` | Dropdown / freeform: empty string = uniform; otherwise space- or comma-separated weights, one per class (leftmost = priority 1). Presets: `"5 2 1"` (mostly high-pri), `"1 2 5"` (mostly low-pri), `"1 3 1"` (peaked middle). Weights are normalised internally |
| `taskDeadlineWeights` | Dropdown / freeform: per-class **queue-wait deadlines in seconds**. Empty = no expiry. Otherwise exactly `priorityLevels` positive values, leftmost = priority 1. A task that sits queued longer than its deadline is dropped with outcome `EXPIRED`; once dispatched the deadline no longer applies. Presets: `"10 30 120"` (high-pri stale fast, low-pri patient), `"30 60 180"` (looser SLA ladder) |
| `costEnergyWeight`, `costHaltPriorityWeight`, `costHaltGroupSizeWeight`, `costMbsRelocationWeight` | Weights used only in `COST` mode (the FIFO variants ignore them). `costHaltPriorityWeight` is a scalar that gets multiplied by a linear ramp `(priorityLevels + 1 - p)` so priority 1 (HIGH) is most expensive and priority `priorityLevels` (LOW) is weight 1 |
| `costHaltPriorityWeights` | Dropdown / freeform: empty string = use the scalar above with a linear ramp; otherwise space- or comma-separated **per-class halt weights** (must have exactly `priorityLevels` entries, leftmost = priority 1). Use this when you want non-linear scaling — e.g. `"1000 100 10"` for an order-of-magnitude gap between classes, or `"1000 500 100 50 10"` for a 5-level setup |
| `drone_comm_range`, `mbs_comm_range`, `commTimeoutDuration` | Comms parameters (broadcast to every drone / MBS) |
| `drone_speed`, `mbs_speed` | Mobility speeds (mps) |

Edit any field, leave the rest, click **OK** to start. Values flow back into
the matching `cPar`s before initialization continues.

Set `ask_user = false` in the ini to use the static values as-is (the
sweep runner does this automatically — see §4).

### Visualization

- `mainNode` is positioned off-canvas (`p=-10000,-10000`) so the topology view
  only shows the swarm; it still runs the allocator as usual.
- Drones and MBSs use a custom `DroneMobility` that draws velocity vectors
  and lets the allocator call `setTarget()` directly.
- Display refresh interval: `**.mobility.updateInterval` in the ini (lower =
  smoother playback, more events per simulated second).

### Output (interactive runs)

By default a per-task CSV is written to:

```
src/results/General-run0-FIFO-tasks.csv
```

The path is `*.mainNode.app[0].csvOutputPath` and supports the placeholders
`{configname}`, `{runnumber}`, `{algorithm}`. Parent directories are
created automatically. Set the param to `""` to disable CSV output.

OMNeT++'s own scalar/vector recordings land in `simulations/results/` as
usual.

---

## 4. Automated sweeps with `sweep_runner.py`

[sweep_runner.py](sweep_runner.py) drives the release binary headlessly
(Cmdenv), iterating over 2 or 3 ini parameters and collecting per-task CSVs
into a sweep folder under `sim_data/`. Each cell is run `repetitions` times
with different seeds.

### Output layout

For a sweep `{numDrones: [10, 20], numMbs: [3, 5]}` with `repetitions = 3`
and `algorithm = "both"`:

```
sim_data/<label>/
  combined_manifest.json       # algorithms run + per-algo summary (failures, elapsed)
  FIFO/
    sweep_manifest.json        # full spec + per-run results (JSON)
    sweep.log                  # one line per run, with timings + status
    numDrones=10/
      numMbs=3/
        sweep.ini              # exact ini handed to the simulator
        rep0-tasks.csv         # per-task CSV from repetition 0
        rep0.log               # stdout/stderr of that run
        rep1-tasks.csv
        rep1.log
        rep2-tasks.csv
        rep2.log
      numMbs=5/
        ...
    numDrones=20/
      ...
  COST/
    ...                        # mirrors FIFO/ subtree
```

For `algorithm = "FIFO"` (or `"COST"`) you still get the algorithm folder
in the middle -- a single `FIFO/` or `COST/` subtree -- so downstream
loaders can use the same path scheme either way. The dictionary insertion
order of `sweep_params` controls which key becomes the outermost folder
*inside* the algorithm subtree.

### From a Jupyter notebook (recommended)

See [sim_test.ipynb](sim_test.ipynb) for a working example. Two cells:

```python
# Cell 1: inject opp_env's env vars into this kernel (cached after first run).
from sweep_runner import setup_environment
setup_environment()
```

```python
# Cell 2: define and run the sweep.
from sweep_runner import run_sweep

sweep_dir = run_sweep(
    sweep_params={
        "*.numDrones": [10, 15, 20, 25],
        "*.numMbs":    [3, 4, 5, 6, 7],
    },
    extra_overrides={
        "taskLimit":              50,
        "taskGenerationInterval": "exponential(5s)",
        "sim-time-limit":         "300s",     # cap simulated time per run
    },
    repetitions=3,
    algorithm="both",                          # see table below for valid values
    label="drones_x_mbs",
)
print(sweep_dir)
```

`algorithm` is a dedicated argument -- do **not** put `algorithmType` in
`sweep_params` or `extra_overrides`. The entire sweep is run once per chosen
algorithm and each tree lands under `sim_data/<label>/<ALGO>/` for
side-by-side comparison plots. Accepted values:

| Value | Resolves to |
|---|---|
| `"FIFO"` / `"FIFO_PRIO"` / `"FIFO_PREEMPT"` / `"COST"` | the single named allocator (default: `"FIFO"`) |
| `"both"` | `["FIFO", "COST"]` -- legacy baseline pair (kept for backward compat) |
| `"all"` | every implemented variant: `["FIFO", "FIFO_PRIO", "FIFO_PREEMPT", "COST"]` |
| `"fifo_all"` | the three FIFO variants: `["FIFO", "FIFO_PRIO", "FIFO_PREEMPT"]` |
| any list | explicit subset, e.g. `["FIFO", "FIFO_PREEMPT"]` |

The kernel does **not** need to be launched from inside an `opp_env shell`;
`setup_environment()` captures the right env from `opp_env shell` once and
caches it to `.opp_env_vars.json`. Call `setup_environment(force=True)` (or
delete the cache file) after an `opp_env` upgrade.

### From the command line

```bash
opp_env shell omnetpp-6.3.0 inet-4.5.4
python sweep_runner.py \
    --param '*.numDrones=[10,15,20,25]' \
    --param '*.numMbs=[3,5,7]' \
    --extra 'taskLimit=50' \
    --extra 'taskGenerationInterval=exponential(5s)' \
    --extra 'sim-time-limit=300s' \
    --algorithm both \
    --repetitions 3 \
    --label drones_x_mbs
```

Values after `=` are parsed with `ast.literal_eval` (for `--param`) or used
verbatim (for `--extra`), so:

- String parameters need embedded quotes (`'"FIFO"'`).
- Numeric parameters with units need units (`'10s'`, `'250m'`).

### Sweep-key conventions

| Key form | Interpreted as | Example |
|---|---|---|
| bare name | `*.mainNode.app[0].<name>` | `taskLimit` → `*.mainNode.app[0].taskLimit` |
| contains `.` or `*` | already-full ini pattern | `*.numDrones`, `**.drone_comm_range` |
| contains `-` | OMNeT++ global config option (section level) | `sim-time-limit`, `seed-set` |

### What the runner writes into each `sweep.ini`

```ini
[General]
# Hard overrides the runner always sets:
*.mainNode.app[0].ask_user = false
*.mainNode.app[0].csvOutputPath = "rep{runnumber}-tasks.csv"
repeat = 3
seed-set = ${runnumber}

# Your sweep-point overrides, then your extra_overrides...

# Inherit everything else (network setup, mobility, weights, ...).
include ../../../src/omnetpp.ini
```

The `include` is last on purpose: OMNeT++ ini precedence is **first-match
wins**, so overrides above the include shadow the base ini's values.

### Failure handling

Failed runs are logged as `WARNING` in both `sweep.log` and the per-run
`repN.log`, and recorded in `sweep_manifest.json` with `"success": false`.
The sweep does **not** abort on failures — it finishes all cells and reports
the failure count at the end.

---

## 5. Comparison plots with `sweep_plotter.py`

[sweep_plotter.py](sweep_plotter.py) loads a finished sweep folder, reduces
each `repN-tasks.csv` to a small set of per-run metrics, and renders a
FIFO-vs-COST comparison figure.

```python
from sweep_plotter import plot_sweep, load_sweep

fig, df = plot_sweep("sim_data/drones_x_mbs", metric="completion_rate")
# Same metric, but all algorithms overlaid as mean +/- std lines instead
# of side-by-side boxes:
fig, df = plot_sweep("sim_data/drones_x_mbs", metric="completion_rate",
                     kind="line")
# df is the long-format DataFrame (one row per repetition) -- use it for
# custom plots / further analysis.
```

Layout is picked automatically from the sweep's dimensionality, and the
`kind` argument selects the visual style:

| Sweep dims | `kind="box"` (default) | `kind="line"` |
|---|---|---|
| 2 | Side-by-side grouped box plots, one panel per algorithm; x = dim1, hue = dim2, box = distribution across reps. Shared y-axis. | One panel per value of dim2; x = dim1; one line per algorithm (mean across reps, shaded ±std band). All algorithms overlaid in the same axes. Shared y-axis. |
| 3 | Heatmap grid: rows = algorithm, cols = values of dim3. Each cell colour = mean metric over reps; shared colour scale. | Panel grid (rows = dim2, cols = dim3) of mean ± std line plots; x = dim1; one line per algorithm. Shared y-axis. |

Use `kind="box"` when you care about the per-rep spread / outliers, and
`kind="line"` when you want a direct algorithm-vs-algorithm overlay (the
boxes can crowd each other once you have 4 algorithms).

Available metrics (computed per repetition):

| Metric | Definition | Direction |
|---|---|---|
| `completion_rate` | fraction of generated tasks with `outcome == COMPLETED` | higher = better |
| `drop_rate` | fraction NOT completed (`DROPPED` from preemption / drone failure, `EXPIRED` from a missed wait deadline, or `QUEUED` / `UNFINISHED` leftovers at sim end) | lower = better |
| `expired_rate` | fraction with `outcome == EXPIRED` — tasks that missed their per-priority queue-wait deadline. Zero when `taskDeadlineWeights` is empty | lower = better |
| `preempt_drop_rate` | fraction with `outcome == DROPPED` — preemption, MBS relocation, drone failure, etc. (everything that isn't a deadline miss). `drop_rate = expired_rate + preempt_drop_rate + (any QUEUED / UNFINISHED leftovers)` | lower = better |
| `mean_wait` | mean `waitTime` (dispatch − generation) over dispatched tasks | lower = better |
| `mean_turnaround` | mean `turnaroundTime` (finalize − generation) over completed tasks | lower = better |
| `total_drops` | sum of `dropEvents` across all tasks | lower = better |
| `n_tasks` | total tasks generated in the run | informational |
| `throughput` | `n_completed / sim_duration` (tasks/sec); `sim_duration` is taken as the max `finalizedAt` in the CSV — a tight lower bound on sim end | higher = better |

`save="path/to/plot.png"` writes the figure (extension picks the format).
For "lower is better" metrics the heatmap uses an inverted colour map and
the title is annotated so brightness still means "better".

### Priority-aware view

The per-task CSV records each task's `priority` (drawn at task-generation
time from the distribution configured via `priorityLevels` and
`priorityWeights` — empty weights = `intuniform(1, priorityLevels)`,
otherwise a weighted draw using the per-class ratios). To see whether an
allocator privileges high- vs low-priority tasks, use `plot_by_priority`:

```python
from sweep_plotter import plot_by_priority

# Default: kind="grid" -- one panel per sweep cell, x = priority, one line
# per algorithm (mean +/- std over reps). For a 2-D sweep the layout is
# rows = dim2, cols = dim1, so the priority effect is plotted *against*
# both swept parameters at once. This is what you want when the priority
# behaviour itself depends on capacity.
plot_by_priority("sim_data/drones_x_mbs", metric="completion_rate")

# Shrink the grid to a single row by pinning one dim:
plot_by_priority("sim_data/drones_x_mbs", metric="drop_rate",
                 where={"numMbs": 4})

# kind="pooled" reproduces the older single-panel summary: dodged box
# groups per (algorithm, priority), distribution pooled across the whole
# sweep. Compact but hides per-cell variation.
plot_by_priority("sim_data/drones_x_mbs", metric="completion_rate",
                 kind="pooled")

# Pin to a single operating point (works with both kinds):
plot_by_priority("sim_data/drones_x_mbs", metric="mean_wait", kind="pooled",
                 where={"numDrones": 20, "numMbs": 5})
```

The grid view makes it easy to spot capacity-dependent priority effects
(e.g. FIFO_PREEMPT / COST only show elevated drop rates for priority-3
tasks once there are enough drones and MBSs to actually pick those tasks
up before being preempted). The pooled view is a compact summary suitable
for a single comparison snapshot. For a fully tidy DataFrame stratified
by priority (e.g. for custom plots) use `load_sweep_by_priority(sweep_dir)`.

---

## 6. Key files

| Path | Purpose |
|---|---|
| [src/omnetpp.ini](src/omnetpp.ini) | Base configuration (network, params, defaults) |
| [src/SwarmNetwork.ned](src/SwarmNetwork.ned) | Top-level network: main node, drones, MBSs |
| [src/MainNodeApp.cc](src/MainNodeApp.cc), [src/MainNodeApp.h](src/MainNodeApp.h) | Allocator (FIFO + COST), task generator, CSV writer, ask-user dialog |
| [src/DroneApp.cc](src/DroneApp.cc), [src/DroneApp.h](src/DroneApp.h) | Per-drone app: movement, task execution |
| [src/MbsApp.cc](src/MbsApp.cc), [src/MbsApp.h](src/MbsApp.h) | MBS app: relay, repositioning |
| [src/ParamDialog.cc](src/ParamDialog.cc), [src/ParamDialog.h](src/ParamDialog.h) | Custom Qt dialog used by `ask_user = true` |
| [src/TaskMessages.msg](src/TaskMessages.msg) | OMNeT++ messages exchanged between apps |
| [launch-omnetpp.sh](launch-omnetpp.sh) | Wrapper that starts the IDE in the opp_env environment |
| [sweep_runner.py](sweep_runner.py) | Batch sweep driver (library + CLI) |
| [sweep_plotter.py](sweep_plotter.py) | Load sweep outputs + render FIFO-vs-COST comparison plots |
| [sim_test.ipynb](sim_test.ipynb) | Notebook example for sweeps + plots |
| `sim_data/`     | Sweep outputs (CSVs + logs + manifests), one folder per sweep |
| `src/results/`  | Single-run CSVs from interactive runs |
| `simulations/results/` | OMNeT++ scalar/vector recordings |
