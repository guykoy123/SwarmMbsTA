# UAV Swarm Task Allocation (Swarm + MBS)

OMNeT++/INET simulation of a UAV swarm coordinated by a stationary main node and
a small fleet of Mobile Base Stations (MBSs). Two task-allocation strategies are
implemented and can be swapped at run time:

- **FIFO** — first-fit greedy assignment in arrival order.
- **COST** — auction/cost-based allocator with four tunable weights (energy,
  halt priority, halt group size, MBS relocation).

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
| `algorithmType` | Dropdown: `FIFO`, `COST` |
| `taskLimit` | Maximum concurrent tasks |
| `taskGenerationInterval` | Dropdown of common generators (`uniform`, `normal`, `exponential`, fixed); also editable for any NED expression |
| `taskDuration` | Constant per-task duration (seconds) |
| `bumpDroppedPriority` | Re-queue dropped tasks at higher priority |
| `costEnergyWeight`, `costHaltPriorityWeight`, `costHaltGroupSizeWeight`, `costMbsRelocationWeight` | Weights used only in COST mode |
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

For a sweep `{numDrones: [10, 20], numMbs: [3, 5]}` with `repetitions = 3`:

```
sim_data/<label>/
  sweep_manifest.json          # full spec + per-run results (JSON)
  sweep.log                    # one line per run, with timings + status
  numDrones=10/
    numMbs=3/
      sweep.ini                # exact ini handed to the simulator
      rep0-tasks.csv           # per-task CSV from repetition 0
      rep0.log                 # stdout/stderr of that run
      rep1-tasks.csv
      rep1.log
      rep2-tasks.csv
      rep2.log
    numMbs=5/
      ...
  numDrones=20/
    ...
```

The dictionary insertion order of `sweep_params` controls which key becomes
the outermost folder, so you can group however your analysis expects.

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
        "algorithmType":          '"FIFO"',   # string params need embedded quotes
        "sim-time-limit":         "300s",     # cap simulated time per run
    },
    repetitions=3,
    label="drones_x_mbs",
)
print(sweep_dir)
```

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
    --extra 'algorithmType="FIFO"' \
    --extra 'sim-time-limit=300s' \
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

## 5. Key files

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
| [sim_test.ipynb](sim_test.ipynb) | Notebook example for sweeps |
| `sim_data/`     | Sweep outputs (CSVs + logs + manifests), one folder per sweep |
| `src/results/`  | Single-run CSVs from interactive runs |
| `simulations/results/` | OMNeT++ scalar/vector recordings |
