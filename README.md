# SegFold: Artifact Evaluation

SegFold is a cycle-accurate simulator for a sparse-sparse matrix multiplication (SpMSpM) accelerator that uses dynamic routing networks with segment-and-fold mapping. This repository contains the C++ simulator, benchmark matrices, and scripts to reproduce the paper's experimental results.

## Quick Start

```bash
# Build and verify
./scripts/setup.sh

# Run all experiments (auto-detects available RAM)
./scripts/run_all.sh

# Or run with Docker
docker compose build
docker compose run artifact ./scripts/run_all.sh
```

Results are written to `output/ae_<timestamp>/`.

## Hardware Requirements

| Resource | Minimum | Recommended |
|----------|---------|-------------|
| CPU      | 4 cores | 8+ cores    |
| RAM      | 8 GB    | 16 GB       |
| Disk     | 2 GB    | 5 GB        |
| OS       | Ubuntu 22.04 | Ubuntu 22.04+ |

See [INSTALL.md](INSTALL.md) for detailed RAM requirements per experiment.

## Repository Structure

```
segfold-artifact/
├── README.md                    # This file
├── INSTALL.md                   # Build & dependency instructions
├── LICENSE
├── Dockerfile                   # Docker-based evaluation
├── docker-compose.yml
├── csegfold/                    # C++ simulator
│   ├── CMakeLists.txt
│   ├── src/                     # Source files
│   │   ├── main.cpp
│   │   ├── runner/              # Experiment runners
│   │   ├── modules/             # Simulator modules
│   │   ├── simulator/           # Simulator core
│   │   ├── matrix/              # Matrix generation & I/O
│   │   └── memory/              # Memory hierarchy
│   ├── include/                 # Headers
│   └── tests/                   # Unit tests
├── configs/                     # Experiment configurations
│   ├── segfold.yaml             # Main SegFold config (1MB cache, HBM2)
│   ├── baseline.yaml            # Baseline config (no memory hierarchy)
│   └── ablation-*.yaml          # Ablation study configs
├── benchmarks/                  # Benchmark data
│   ├── generate_cnn_bench.py    # CNN/LLM benchmark generator
│   ├── generate_suitesparse_bench.py
│   └── data/
│       ├── cnn_llm/             # Pre-generated CNN/LLM matrices
│       └── suitesparse/         # SuiteSparse matrices (.mtx)
├── scripts/                     # Reproduction scripts
│   ├── setup.sh                 # Build & dependency check
│   ├── run_all.sh               # One-command full reproduction
│   ├── run_synthetic.sh         # Synthetic matrix experiments
│   ├── run_suitesparse.sh       # SuiteSparse experiments
│   ├── run_ablation.sh          # Ablation studies
│   ├── collect_results.py       # Parse JSON stats -> CSV
│   └── plot_results.py          # Generate figures
├── hardware/                    # RTL & synthesis reports
│   ├── rtl/
│   └── reports/
└── expected_results/            # Reference outputs for validation
```

## Running Experiments

### All experiments (recommended)

```bash
./scripts/run_all.sh [--jobs N] [--skip-build] [--config CONFIG]
```

Options:
- `--jobs N`: Maximum parallel simulations (default: auto-detect from RAM)
- `--skip-build`: Skip the build step if already built
- `--config CONFIG`: Configuration file (default: `configs/segfold.yaml`)

### Individual experiments

```bash
# Synthetic matrices (density/size sweep)
./scripts/run_synthetic.sh output/my_run

# SuiteSparse real-world matrices
./scripts/run_suitesparse.sh output/my_run

# Ablation studies (all config variants)
./scripts/run_ablation.sh output/my_run
```

### Single simulation

```bash
# Synthetic matrix
./csegfold/build/ablation_runner \
    --config configs/segfold.yaml \
    --out_dir output/test \
    --size 256 --densityA 0.1 --densityB 0.1 --random_state 1

# SuiteSparse matrix
./csegfold/build/ablation_runner \
    --config configs/segfold.yaml \
    --out_dir output/test \
    --suitesparse --matrix olm5000 \
    --matrix_dir benchmarks/data/suitesparse
```

## Experiment-to-Paper Mapping

| Script | Paper Section | Description |
|--------|---------------|-------------|
| `run_synthetic.sh` | Main evaluation | Cycle count and utilization vs. density/size |
| `run_suitesparse.sh` | Real-world workloads | Performance on SuiteSparse matrices |
| `run_ablation.sh` | Ablation study | Impact of individual optimizations |

## Collecting and Plotting Results

```bash
# Parse all stats JSON files into CSV tables
python3 scripts/collect_results.py --out_dir output/ae_*

# Generate plots
python3 scripts/plot_results.py --out_dir output/ae_*
```

Output CSVs:
- `synthetic_results.csv` — synthetic experiment data
- `suitesparse_results.csv` — SuiteSparse experiment data
- `ablation_results.csv` — ablation study data (with config_name column)

## SuiteSparse Matrix List

| Matrix | Rows | Cols | NNZ | Notes |
|--------|------|------|-----|-------|
| bcsstk01 | 48 | 48 | 224 | Small structural |
| bcsstk03 | 112 | 112 | 376 | |
| ash219 | 219 | 85 | 438 | Non-square |
| mbeacxc | 496 | 496 | 49,920 | |
| mbeaflw | 496 | 496 | 49,920 | |
| bcsstk08 | 1,074 | 1,074 | 7,017 | |
| gre_1107 | 1,107 | 1,107 | 5,664 | |
| 1138_bus | 1,138 | 1,138 | 2,596 | |
| bcspwr06 | 1,454 | 1,454 | 3,377 | |
| bcspwr09 | 1,723 | 1,723 | 4,117 | |
| orani678 | 2,529 | 2,529 | 90,158 | |
| qc2534 | 2,534 | 2,534 | 232,947 | Dense-ish |
| tols4000 | 4,000 | 4,000 | 8,784 | |
| bcsstk16 | 4,884 | 4,884 | 147,631 | |
| G57 | 5,000 | 5,000 | 10,000 | |
| olm5000 | 5,000 | 5,000 | 19,996 | |
| rdb5000 | 5,000 | 5,000 | 29,600 | |

## Expected Runtime

With default settings on a machine with 16 GB RAM and 8 cores:

| Experiment | Runs | Est. Time |
|------------|------|-----------|
| Synthetic (256, 512) | 192 | 20-40 min |
| SuiteSparse (17 matrices) | 17 | 15-30 min |
| Ablation (9 configs x 96) | 864 | 30-60 min |
| **Total** | **1,073** | **1-2 hours** |

## License

See [LICENSE](LICENSE).
