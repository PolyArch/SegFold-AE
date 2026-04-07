# SegFold: Artifact Evaluation

SegFold is a cycle-accurate simulator for a sparse-sparse matrix multiplication (SpMSpM) accelerator that uses dynamic routing networks with segment-and-fold mapping. This repository contains the C++ simulator, benchmark matrices, and scripts to reproduce the paper's experimental results.

## Quick Start

Run everything in one command (includes build + download + experiments):

```bash
./scripts/run_all.sh
```

Results are written to `output/ae_<timestamp>/`.

The following steps could be used to build the simulator, download the benchmarks, and run the simulation separately.

```bash
# 1. Build the simulator
./scripts/setup.sh

# 2. Download SuiteSparse matrices
python3 scripts/download_matrices.py

# 3. Run all experiments and generate plots
./scripts/run_all.sh --skip-build
```

### Reproduce a Single Figure/Table

The e2e task takes around 2 hours with 16 parallelized cores. To do shorter test, each figure/table has a standalone script that handles everything end-to-end (build, download, simulate, collect, plot):

```bash
./scripts/run_figure_overall.sh         # Figure 8:  Overall performance (SegFold vs Spada vs Flexagon)
./scripts/run_figure_nonsquare.sh       # Figure 9:  Non-square matrix performance
./scripts/run_figure_mapping.sh         # Figure 10: Ablation mapping strategy comparison
./scripts/run_figure_breakdown.sh       # Figure 11: Speedup breakdown (incremental ablation)
./scripts/run_figure_crossbar_width.sh  # Figure 12(a): Crossbar width sweep
./scripts/run_figure_window_size.sh     # Figure 12(b): Window size sweep
./scripts/run_table_k_reordering.sh     # Table IV:  K-reordering ablation
```

All scripts support `--jobs N`, `--skip-build`, and `--output-dir DIR`. See the [Step-by-Step Guide](#step-by-step-guide) for detailed command-line options and experiment descriptions.

### Docker

A Docker environment is provided for ease of setup. The container comes with all dependencies pre-installed and the simulator pre-built.

```bash
# Build the Docker image
docker compose build

# Run all experiments (results are mounted to ./output on the host)
docker compose run artifact ./scripts/run_all.sh 

# Or reproduce a single figure
docker compose run artifact ./scripts/run_figure_overall.sh 
```

## Expected Results

After running the experiments, results are written to `output/ae_<timestamp>/`:
- **`plots/`** — Generated figures (PDF and PNG) for each experiment
- **`*.csv`** — Collected cycle counts and statistics for each experiment
- **`*_stats.json`** — Raw simulation output per matrix per configuration

Pre-generated reference results are provided in `expected_results/` for comparison:
- **`expected_results/plots/`** — Reference figures matching the paper
- **`expected_results/data/`** — Reference CSV files with expected cycle counts

Since the simulator is deterministic, the generated CSV files should match the expected results exactly. You can verify this with:

```bash
diff output/ae_<timestamp>/fig8_overall_results.csv expected_results/data/fig8_overall_results.csv
```

## Hardware Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU      | 4 cores | 16+ cores   |
| RAM      | 64 GB   | 256 GB      |
| Disk     | 2 GB    | 5 GB        |
| OS       | Ubuntu 22.04 | Ubuntu 22.04+ |

> **RAM Note:** The breakdown experiment (`breakdown-base` config with dense tiling) requires up to ~50 GB per process. The mapping ablation requires up to ~40 GB per process. With `--jobs 1`, 64 GB is sufficient. Higher parallelism requires proportionally more RAM (e.g., `--jobs 4` with mapping needs ~160 GB).

Python >= 3.8 is required. All dependencies (cmake, g++, Python packages) are checked and installed automatically by `setup.sh`. See [INSTALL.md](INSTALL.md) for detailed dependency and RAM requirements.

## Step-by-Step Guide

### Step 1: Build the Simulator

```bash
./scripts/setup.sh
```

This script:
- Checks system dependencies (CMake >= 3.15, `g++` with C++20, Python 3)
- Checks Python packages (numpy, scipy, matplotlib, pyyaml, pandas)
- Builds the C++ simulator with Ramulator2 HBM2 DRAM backend
- Runs a smoke test to verify the build

After building, the simulator binary is at `csegfold/build/csegfold`.

### Step 2: Download SuiteSparse Matrices

```bash
python3 scripts/download_matrices.py
```

Downloads 20 matrices from the [SuiteSparse Matrix Collection](https://sparse.tamu.edu/) into `benchmarks/data/suitesparse/`. Matrices already present are skipped. These cover all the paper experiments.

### Step 3: Run Overall Performance Experiment

Reproduces the paper's overall speedup comparison (SegFold vs Spada vs Flexagon) on 11 SuiteSparse matrices.

```bash
python3 scripts/run_overall.py output/my_run
python3 scripts/run_overall.py output/my_run --jobs 4  # parallel
```

**Matrices:** fv1, flowmeter0, delaunay_n13, ca-GrQc, ca-CondMat, poisson3Da, bcspwr06, tols4000, rdb5000, psse1, gemat1

**Configs:**
- Most matrices use `configs/segfold.yaml` (16x16 PE array, 1.5 MB L1 cache, HBM2 DRAM)
- Irregular matrices (ca-GrQc, ca-CondMat, poisson3Da) use `configs/segfold-ir.yaml` (row decomposition, larger tiles, demand scheduling)

**Output:** `output/my_run/overall/sim_{matrix}_stats.json`

> **Note:** Only SegFold is simulated. Spada and Flexagon cycle counts are pre-computed and stored in `data/baselines/overall_baselines.csv` for plotting.

### Step 4: Run Non-Square Performance Experiment

Reproduces the paper's non-square matrix evaluation on 6 rectangular SuiteSparse matrices.

```bash
python3 scripts/run_nonsquare.py output/my_run
```

**Matrices:** lp_woodw (1098x8418), pcb3000 (3960x7732), gemat1 (4929x10595), Franz6 (7576x3016), Franz8 (16728x7176), psse1 (14318x11028)

**Output:** `output/my_run/nonsquare/sim_{matrix}_stats.json`

> Spada baseline data is in `data/baselines/nonsquare_baselines.csv`.

### Step 5: Run Speedup Breakdown Experiment

Reproduces the paper's ablation study showing incremental contribution of each optimization on 12 SuiteSparse matrices.

```bash
python3 scripts/run_breakdown.py output/my_run
```

Five configurations are run per matrix, progressively enabling features:

| Config | SegmentBC | Spatial Folding | IPM LUT | SelectA |
|--------|:---------:|:---------------:|:-------:|:-------:|
| `breakdown-base` | | | | |
| `breakdown-plus-tiling` | X | | | |
| `breakdown-plus-folding` | X | X | | |
| `breakdown-plus-dynmap` | X | X | X | |
| `segfold` (full) | X | X | X | X |

**Matrices:** bcsstk03, bcspwr06, ca-GrQc, tols4000, olm5000, fv1, bcsstk18, lp_d2q06c, lp_woodw, gemat1, rosen10, pcb3000

**Output:** `output/my_run/breakdown/{config}/sim_{matrix}_stats.json`

### Step 6: Run Ablation Mapping Experiment

Evaluates the impact of different memory-to-PE mapping strategies on 16 SuiteSparse matrices.

```bash
python3 scripts/run_ablation.py output/my_run --ablation mapping-paper --jobs 4
```

Three mapping strategies are compared:

| Config | Strategy |
|--------|----------|
| `zero` | Zero-Offset |
| `ideal` | Ideal-Network |
| `segfold` | SegFold (Ours) |

**Matrices:** fv1, flowmeter0, delaunay_n13, ca-GrQc, ca-CondMat, poisson3Da, bcspwr06, tols4000, rdb5000, bcsstk03, bcsstk18, olm5000, lp_d2q06c, lp_woodw, pcb3000, rosen10

**Output:** `output/my_run/ablation/mapping-paper/{config}/sim_{matrix}_stats.json`

### Step 7: Run Synthetic Ablation Experiments

Evaluates the impact of window size, crossbar width, and K-reordering on synthetic matrices at sizes 256, 512, 1024 with densities 0.05 and 0.1.

```bash
# Window size sweep (1, 4, 8, 16, 32, 64)
python3 scripts/run_ablation.py output/my_run --ablation window-size --jobs 4

# Crossbar width sweep (1, 2, 4, 8, 16)
python3 scripts/run_ablation.py output/my_run --ablation crossbar-width --jobs 4

# K-reordering strategies
python3 scripts/run_ablation.py output/my_run --ablation k-reordering --jobs 4
```

**Output:** `output/my_run/ablation/{window-size,crossbar-width,k-reordering}/{config}/sim_*_stats.json`

### Step 8: Collect Results

```bash
python3 scripts/collect_results.py output/my_run
```

Parses all `*_stats.json` files and produces:
- `fig8_overall_results.csv` — Figure 8: SegFold cycle counts for overall performance
- `fig9_nonsquare_results.csv` — Figure 9: SegFold cycle counts for non-square matrices
- `fig10_ablation_mapping_results.csv` — Figure 10: Ablation mapping strategy comparison
- `fig11_breakdown_results.csv` — Figure 11: Cycle counts per config per matrix (pivoted)
- `fig12a_ablation_crossbar_width_results.csv` — Figure 12(a): Crossbar width sweep results
- `fig12b_ablation_window_size_results.csv` — Figure 12(b): Window size sweep results
- `tab4_k_reordering_results.csv` — Table IV: K-reordering strategy results

### Step 9: Generate Plots

```bash
python3 scripts/plot_overall.py output/my_run
python3 scripts/plot_nonsquare.py output/my_run
python3 scripts/plot_breakdown.py output/my_run
python3 scripts/plot_ablation_mapping.py output/my_run
python3 scripts/plot_ablation.py output/my_run
```

Generates PDF and PNG figures in `output/my_run/plots/`:
- `fig8_overall_speedup.pdf` — Figure 8: SegFold vs Spada vs Flexagon (normalized to Spada)
- `fig9_nonsquare_speedup.pdf` — Figure 9: SegFold vs Spada on rectangular matrices
- `fig10_ablation_mapping.pdf` — Figure 10: Mapping strategy comparison
- `fig11_breakdown_speedup.pdf` — Figure 11: Incremental speedup per optimization
- `fig12a_ablation_crossbar_width.pdf` — Figure 12(a): Crossbar width sweep (normalized cycles)
- `fig12b_ablation_window_size.pdf` — Figure 12(b): Window size sweep (normalized cycles)
- `tab4_k_reordering.txt` — Table IV: K-reordering summary

## Experiment-to-Paper Mapping

| Script | Paper Reference | Output Files | Description |
|--------|----------------|--------------|-------------|
| `run_overall.py` | Figure 8 | `fig8_overall_results.csv`, `fig8_overall_speedup.pdf` | SegFold vs Spada vs Flexagon on 11 matrices |
| `run_nonsquare.py` | Figure 9 | `fig9_nonsquare_results.csv`, `fig9_nonsquare_speedup.pdf` | SegFold vs Spada on 6 rectangular matrices |
| `run_ablation.py --ablation mapping-paper` | Figure 10 | `fig10_ablation_mapping_results.csv`, `fig10_ablation_mapping.pdf` | Mapping strategy comparison (3 x 16) |
| `run_breakdown.py` | Figure 11 | `fig11_breakdown_results.csv`, `fig11_breakdown_speedup.pdf` | Incremental ablation (5 configs x 12 matrices) |
| `run_ablation.py --ablation crossbar-width` | Figure 12(a) | `fig12a_ablation_crossbar_width.pdf` | B loader row limit (5 configs, synthetic) |
| `run_ablation.py --ablation window-size` | Figure 12(b) | `fig12b_ablation_window_size.pdf` | B loader window size (6 configs, synthetic) |
| `run_ablation.py --ablation k-reordering` | Table IV | `tab4_k_reordering.txt` | K-reorder strategies (3 configs, synthetic) |

## Configuration

### Command-Line Options

All experiment scripts accept these options:

| Option | Default | Description |
|--------|---------|-------------|
| `--jobs N` | 2 | Max parallel simulations (1 = sequential) |
| `--config PATH` | `configs/segfold.yaml` | SegFold configuration file |
| `--matrix-dir PATH` | `benchmarks/data/suitesparse` | SuiteSparse matrix directory |
| `--timeout SEC` | 3600 | Timeout per simulation in seconds |

`run_overall.py` also accepts `--config-ir PATH` (default: `configs/segfold-ir.yaml`) for irregular matrices.

### Running a Single Matrix

```bash
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --mtx-file benchmarks/data/suitesparse/ca-GrQc/ca-GrQc.mtx
```

Use `--tmp-dir <path>` to control where stats/config JSON files are saved (default: `csegfold/tmp/`).

## Repository Structure

```
SegFold-AE/
├── README.md
├── INSTALL.md                       # Detailed build & dependency guide
├── Dockerfile / docker-compose.yml
├── csegfold/                        # C++ simulator
│   ├── CMakeLists.txt
│   ├── src/
│   │   ├── main.cpp                 # Main simulator (csegfold binary)
│   │   ├── modules/                 # PE, switch, spad, mapper, LUT, ...
│   │   ├── simulator/               # Cycle-accurate core
│   │   ├── matrix/                  # Matrix generation & I/O
│   │   └── memory/                  # Cache + Ramulator2 backend
│   └── include/
├── configs/
│   ├── segfold.yaml                 # Full SegFold config
│   ├── segfold-ir.yaml              # Config for irregular matrices
│   ├── breakdown-base.yaml          # All optimizations OFF
│   ├── breakdown-plus-tiling.yaml   # + dynamic tiling
│   ├── breakdown-plus-folding.yaml  # + spatial folding
│   ├── breakdown-plus-dynmap.yaml   # + dynamic routing
│   └── ramulator2-hbm.yaml         # HBM2 DRAM config
├── benchmarks/data/
│   └── suitesparse/                 # SuiteSparse .mtx files
├── data/baselines/
│   ├── overall_baselines.csv        # Pre-computed Spada/Flexagon cycles
│   └── nonsquare_baselines.csv      # Pre-computed Spada cycles
├── scripts/
│   ├── setup.sh                     # Build & verify
│   ├── run_all.sh                   # One-command full reproduction
│   ├── run_figure_overall.sh        # Standalone: overall performance figure
│   ├── run_figure_nonsquare.sh      # Standalone: non-square performance figure
│   ├── run_figure_breakdown.sh      # Standalone: speedup breakdown figure
│   ├── run_figure_mapping.sh        # Standalone: ablation mapping figure
│   ├── run_figure_window_size.sh    # Standalone: window size ablation figure
│   ├── run_figure_crossbar_width.sh # Standalone: crossbar width ablation figure
│   ├── run_table_k_reordering.sh    # Standalone: k-reordering ablation table
│   ├── download_matrices.py         # Download SuiteSparse matrices
│   ├── run_overall.py               # Overall performance (11 matrices)
│   ├── run_nonsquare.py             # Non-square performance (6 matrices)
│   ├── run_breakdown.py             # Speedup breakdown (5 x 12)
│   ├── run_ablation.py              # Ablation experiments
│   ├── collect_results.py           # JSON stats -> CSV
│   ├── plot_overall.py              # Overall speedup figure
│   ├── plot_nonsquare.py            # Non-square speedup figure
│   ├── plot_breakdown.py            # Breakdown stacked bar figure
│   ├── plot_ablation_mapping.py     # Ablation mapping figure
│   └── plot_ablation.py             # Synthetic ablation figures
├── expected_results/                # Reference outputs
│   ├── plots/                       # Expected figures (PDF + PNG)
│   └── data/                        # Expected CSV results
└── hardware/                        # RTL & synthesis reports
    ├── rtl/
    └── reports/
```

## Expected Runtime

Measured on a 16-core machine with 256 GB RAM (`--jobs 16`, auto-detected):

| Experiment | Script | Runs | Time | Peak RAM / proc |
|------------|--------|------|------|-----------------|
| Overall performance | `run_figure_overall.sh` | 11 | ~26 min | 5 GB |
| Non-square performance | `run_figure_nonsquare.sh` | 6 | ~11 min | 5 GB |
| Speedup breakdown | `run_figure_breakdown.sh` | 60 | ~13 min | 50 GB |
| Ablation mapping | `run_figure_mapping.sh` | 48 | ~18 min | 40 GB |
| Window size ablation | `run_figure_window_size.sh` | 36 | ~14 min | 4 GB |
| Crossbar width ablation | `run_figure_crossbar_width.sh` | 30 | ~15 min | 4 GB |
| K-reordering ablation | `run_table_k_reordering.sh` | 18 | ~14 min | 2 GB |
| **Total** (`run_all.sh`) | | **209** | **~2 hours** | |

With fewer cores or lower `--jobs`, runtimes scale roughly linearly. The breakdown and mapping experiments are the most memory-intensive.

## Hardware Synthesis Reports

The `hardware/` directory contains RTL source files and synthesis reports for the SegFold architecture modules:

- **`hardware/rtl/`** — SystemVerilog source files for all SegFold modules (PE, switch, scratchpad, LUT, memory controller, etc.)
- **`hardware/reports/`** — Synthesis reports (area, power, timing, QoR) for each module, generated with Synopsys Design Compiler using the ASAP 7nm standard cell library
- **`hardware/reports/cacti/`** — CACTI SRAM modeling results for scratchpad and FIFO buffers (22nm/32nm)

These reports correspond to the area and power numbers presented in the paper. No simulation is required to view them.

## License

See [LICENSE](LICENSE).
