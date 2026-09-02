#!/bin/bash

./build/charge gbench \
    --benchmark_perf_counters=CYCLES,INSTRUCTIONS,BRANCHES,BRANCH-MISSES \
    --benchmark_repetitions=100 \
    --benchmark_min_time=0.05s \
    --benchmark_report_aggregates_only \
    --benchmark_format=json \
    --benchmark_filter="benchmarkExprImpl/($1)" \
    | python3 "$(dirname "$0")/benchmark-table.py" "${@:2}"
