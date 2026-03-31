#!/usr/bin/env python3
"""Helper script to run Python simulator for cycle count comparison"""

import sys
import os

# Add parent directory to path to import segfold modules
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../..'))

from segfold.modules import reset, update_cfg
from segfold.simulator import SegfoldSimulator
import numpy as np

def main():
    if len(sys.argv) < 2:
        print("Usage: compare_simulator.py <config_file>", file=sys.stderr)
        sys.exit(1)
    
    config_file = sys.argv[1]
    
    # Read configuration
    config = {}
    matrix_size = 8
    is_dense = True
    trace_filename = None
    workspace_root = "."
    
    # Define type mappings for config values
    int_keys = {
        'physical_pe_row_num', 'physical_pe_col_num', 'virtual_pe_row_num', 'virtual_pe_col_num',
        'b_loader_window_size', 'II', 'max_cycle', 'sw_pe_fifo_size',
        'c_col_update_per_row', 'b_loader_row_limit', 'max_push',
        'mapper_request_limit_per_cycle', 'num_split', 'max_updates_per_cycle',
        'cache_line_size', 'memory_server_port', 'a_pointer_offset', 'b_pointer_offset',
        'c_pointer_offset', 'random_state'
    }
    bool_keys = {
        'enable_memory_hierarchy', 'enable_sw_pe_fifo', 'enable_tile_eviction',
        'enable_spatial_folding', 'verbose', 'very_verbose', 'show_progress',
        'save_trace', 'run_check', 'enable_multi_b_row_loading', 'enable_b_row_reordering',
        'enable_dynamic_routing', 'enable_partial_b_load', 'enable_b_v_contention',
        'enable_dynamic_scheduling', 'enable_offset', 'disable_multi_b_row_per_row',
        'decouple_sw_and_pe', 'decouple_sw_and_controller', 'use_lookup_table',
        'reverse_lookup_table', 'update_on_move', 'update_with_round_robin',
        'enable_decompose_a_row', 'enable_dynamic_tiling', 'enable_a_csc',
        'use_external_memory', 'enable_filter', 'enable_outstanding_filter',
        'preprocess_only'
    }
    
    with open(config_file, 'r') as f:
        for line in f:
            line = line.strip()
            if not line or '=' not in line:
                continue
            key, value = line.split('=', 1)
            if key == 'matrix_size':
                matrix_size = int(value)
            elif key == 'is_dense':
                is_dense = (value == '1')
            elif key == 'trace_filename':
                trace_filename = value
            elif key == 'workspace_root':
                workspace_root = value
            elif key in int_keys:
                config[key] = int(value)
            elif key in bool_keys:
                # Convert string to bool: "true"/"1" -> True, "false"/"0" -> False
                config[key] = value.lower() in ('true', '1', 'yes')
            else:
                # Keep as string for other values
                config[key] = value
    
    # Create tmp directory if it doesn't exist
    import pathlib
    tmp_dir = pathlib.Path(workspace_root) / "tmp"
    tmp_dir.mkdir(parents=True, exist_ok=True)
    
    # Reset and update config
    reset()
    update_cfg(**config)
    
    # Create matrices
    if is_dense:
        A = np.ones((matrix_size, matrix_size), dtype=np.int8)
        B = np.ones((matrix_size, matrix_size), dtype=np.int8)
    else:
        A = np.zeros((matrix_size, matrix_size), dtype=np.int8)
        B = np.zeros((matrix_size, matrix_size), dtype=np.int8)
        for i in range(matrix_size):
            A[i, i] = 1
            B[i, i] = 1
            if i < matrix_size - 1:
                A[i, i+1] = 1
                B[i+1, i] = 1
    
    # Run simulation
    sim = SegfoldSimulator(A, B)
    
    # Print full config after MatrixLoader initialization to verify it matches C++
    print("PYTHON_CONFIG_FULL:", file=sys.stderr)
    config_dict = sim.cfg.to_dict()
    for key, value in sorted(config_dict.items()):
        print(f"  {key}: {value}", file=sys.stderr)
    
    sim.run()
    
    # Save trace, config, and stats if enabled
    if sim.cfg.save_trace:
        import pathlib
        base_path = pathlib.Path(trace_filename).parent if trace_filename else pathlib.Path(".")
        base_name = pathlib.Path(trace_filename).stem if trace_filename else "trace"
        
        if trace_filename:
            sim.dump_trace(trace_filename)
            print(f"PYTHON_TRACE={trace_filename}", file=sys.stderr)
        else:
            sim.dump_trace()
            print(f"PYTHON_TRACE=trace.json", file=sys.stderr)
        
        # Dump config and stats
        config_filename = str(base_path / f"{base_name.replace('trace', 'config')}.json")
        stats_filename = str(base_path / f"{base_name.replace('trace', 'stats')}.json")
        
        sim.cfg.dump_to_json(config_filename)
        sim.stats.dump_json(stats_filename)
        print(f"PYTHON_CONFIG_FILE={config_filename}", file=sys.stderr)
        print(f"PYTHON_STATS_FILE={stats_filename}", file=sys.stderr)
    
    # Output results
    print(f"PYTHON_CYCLES={sim.stats.cycle}")
    print(f"PYTHON_SUCCESS={sim.success}")

if __name__ == "__main__":
    main()

