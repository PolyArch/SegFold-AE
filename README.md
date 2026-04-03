# SegFold: Artifact Evaluation

SegFold is a cycle-accurate simulator for a sparse-sparse matrix multiplication (SpMSpM) accelerator that uses dynamic routing networks with segment-and-fold mapping. This repository contains the C++ simulator, benchmark matrices, and scripts to reproduce the paper's experimental results.

## Quick Start

```bash
# 1. Build the simulator
./scripts/setup.sh

# 2. Download SuiteSparse matrices
python3 scripts/download_matrices.py

# 3. Run all experiments and generate plots
./scripts/run_all.sh --skip-build
```

Results are written to `output/ae_<timestamp>/`.

Or run everything in one command (includes build + download + experiments):

```bash
./scripts/run_all.sh
```

### Docker

```bash
docker compose build
docker compose run artifact ./scripts/run_all.sh
```

## Hardware Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU      | 4 cores | 8+ cores    |
| RAM      | 8 GB    | 16 GB       |
| Disk     | 2 GB    | 5 GB        |
| OS       | Ubuntu 22.04 | Ubuntu 22.04+ |

Python >= 3.8 is required. Install dependencies with:

```bash
pip install -r requirements.txt
```

See [INSTALL.md](INSTALL.md) for detailed dependency and RAM requirements.

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

Downloads 21 matrices from the [SuiteSparse Matrix Collection](https://sparse.tamu.edu/) into `benchmarks/data/suitesparse/`. Matrices already present are skipped. These cover all three paper experiments.

### Step 3: Run Overall Performance Experiment

Reproduces the paper's overall speedup comparison (SegFold vs Spada vs Flexagon) on 11 SuiteSparse matrices.

```bash
python3 scripts/run_overall.py output/my_run
```

**Matrices:** fv1, flowmeter0, delaunay_n13, ca-GrQc, ca-CondMat, poisson3Da, bcspwr06, tols4000, rdb5000, psse1, gemat1

**Config:** `configs/segfold.yaml` (16x16 PE array, 1.5 MB L1 cache, HBM2 DRAM)

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

### Step 6: Collect Results

```bash
python3 scripts/collect_results.py output/my_run
```

Parses all `*_stats.json` files and produces:
- `overall_results.csv` — SegFold cycle counts for overall performance
- `nonsquare_results.csv` — SegFold cycle counts for non-square matrices
- `breakdown_results.csv` — Cycle counts per config per matrix (pivoted)

### Step 7: Generate Plots

```bash
python3 scripts/plot_overall.py output/my_run
python3 scripts/plot_nonsquare.py output/my_run
python3 scripts/plot_breakdown.py output/my_run
```

Generates PDF and PNG figures in `output/my_run/plots/`:
- `overall_speedup.pdf` — Bar chart: SegFold vs Spada vs Flexagon (normalized to Spada)
- `nonsquare_speedup.pdf` — Bar chart: SegFold vs Spada on rectangular matrices
- `breakdown_speedup.pdf` — Stacked bars: incremental speedup per optimization

## Experiment-to-Paper Mapping

| Script | Paper Figure | Description |
|--------|-------------|-------------|
| `run_overall.py` | Overall performance | SegFold vs Spada vs Flexagon on 11 matrices |
| `run_nonsquare.py` | Non-square performance | SegFold vs Spada on 6 rectangular matrices |
| `run_breakdown.py` | Speedup breakdown | Incremental ablation (5 configs x 12 matrices) |

## Configuration

### Command-Line Options

All experiment scripts accept these options:

| Option | Default | Description |
|--------|---------|-------------|
| `--jobs N` | 2 | Max parallel simulations |
| `--config PATH` | `configs/segfold.yaml` | SegFold configuration file |
| `--matrix-dir PATH` | `benchmarks/data/suitesparse` | SuiteSparse matrix directory |
| `--timeout SEC` | 3600 | Timeout per simulation in seconds |

Example: run with 4 parallel jobs:

```bash
python3 scripts/run_overall.py output/my_run --jobs 4
```

### Running a Single Matrix

```bash
./csegfold/build/csegfold \
    --config configs/segfold.yaml \
    --mtx-file benchmarks/data/suitesparse/ca-GrQc/ca-GrQc.mtx
```

## Repository Structure

```
SegFold-AE/
├── README.md
├── INSTALL.md
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
│   ├── segfold.yaml                 # Full SegFold (1.5 MB L1, HBM2)
│   ├── breakdown-base.yaml          # All optimizations OFF
│   ├── breakdown-plus-tiling.yaml   # + dynamic tiling
│   ├── breakdown-plus-folding.yaml  # + spatial folding
│   ├── breakdown-plus-dynmap.yaml   # + dynamic routing
│   ├── ramulator2-hbm.yaml          # HBM2 DRAM config
│   ├── baseline.yaml                # No memory hierarchy
│   └── ablation-*.yaml              # Per-feature ablation configs
├── benchmarks/data/
│   ├── suitesparse/                 # SuiteSparse .mtx files
│   └── cnn_llm/                     # Pre-generated DNN matrices
├── data/baselines/
│   ├── overall_baselines.csv        # Pre-computed Spada/Flexagon cycles
│   └── nonsquare_baselines.csv      # Pre-computed Spada cycles
├── scripts/
│   ├── setup.sh                     # Build & verify
│   ├── run_all.sh                   # One-command full reproduction
│   ├── download_matrices.py         # Download SuiteSparse matrices
│   ├── run_overall.py               # Overall performance (11 matrices)
│   ├── run_nonsquare.py             # Non-square performance (6 matrices)
│   ├── run_breakdown.py             # Speedup breakdown (5 x 12)
│   ├── collect_results.py           # JSON stats -> CSV
│   ├── plot_overall.py              # Overall speedup figure
│   ├── plot_nonsquare.py            # Non-square speedup figure
│   └── plot_breakdown.py            # Breakdown stacked bar figure
├── hardware/                        # RTL & synthesis reports
│   ├── rtl/
│   └── reports/
└── expected_results/                # Reference outputs for validation
```

## Expected Runtime

With default settings on a machine with 16 GB RAM and 8 cores:

| Experiment | Runs | Est. Time |
|------------|------|-----------|
| Overall performance (11 matrices) | 11 | 10-20 min |
| Non-square performance (6 matrices) | 6 | 5-15 min |
| Speedup breakdown (5 configs x 12 matrices) | 60 | 30-60 min |
| **Total** | **77** | **~1 hour** |

## License

See [LICENSE](LICENSE).
