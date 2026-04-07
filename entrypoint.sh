#!/bin/sh
# Entrypoint that delegates to bash for script execution.
# Uses /bin/sh shebang for WSL2 compatibility while ensuring
# bash scripts run correctly.

if [ $# -eq 0 ]; then
    # No arguments: show usage
    exec /bin/bash -c 'echo "SegFold Artifact Evaluation Container"; echo ""; echo "Usage:"; echo "  Run all experiments:"; echo "    docker compose run artifact ./scripts/run_all.sh"; echo ""; echo "  Run specific figure:"; echo "    docker compose run artifact ./scripts/run_figure_overall.sh"; echo "    docker compose run artifact ./scripts/run_figure_nonsquare.sh"; echo "    docker compose run artifact ./scripts/run_figure_breakdown.sh"'
elif [ "$1" = "-c" ]; then
    # Pass -c commands to bash
    shift
    exec /bin/bash -c "$@"
else
    # Execute the command directly (preserves shebang handling)
    exec "$@"
fi
