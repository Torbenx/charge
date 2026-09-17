# Charge Parser Presentation

This branch contains the benchmarks and slides for the CppCon 2026 talk "Writing High Performance Parsers Using State Machines".
For details about charge or the layout of the project check out the `master` branch.

## Slides

The slides are in the `presentation` directory. To host them locally run:
```sh
cd presentation
npm install     # Only required once
npm start       # Slides will be available at localhost:8000
```

## Results

Detailed benchmark results are in the `results` directory. To print the Google Benchmark JSON as a nice table run:
```sh
cat results/<architecture>/<file>_gbench.json | python3 benchmark-table.py
```
The `.txt` files contain records of `perf_ctl.bash` runs with different implementations, files and repetition counts. The `plot-benchmark.py` script can be used to create branch miss rate vs. repetition count plots using matplotlib. See `python3 plot-benchmark.py --help` for details.

## Building

The project requires Python 3 and a C++23-compatible compiler and standard library. Building is tested with Clang 22 and GCC 15 on Ubuntu 26.04. The build produces a single binary `charge` that runs the test suite by default. Clang should be used for optimal performance.
```sh
cmake -S . -B build -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22
cmake --build build && ./build/charge
```

`./build/charge gbench` exposes Google Benchmark and supports all normal Google Benchmark options. Note that misspelled parameters are unfortunately not diagnosed and silently ignored.

`./build/charge benchmark <impl> <file> [-r <repeats>]` exposes a custom benchmarking interface that runs the implementation a fixed number of times. It supports instrumentation with the `perf_ctl.bash` script which perf-measures only the actual benchmark loop. It forwards all arguments to the charge binary, so use as `./perf_ctl.bash benchmark <impl> <file> [-r <repeats>]`.

> [!NOTE]
> Ubuntu 26.04 ships with `kernel.perf_event_paranoid=4` by default, which means that accessing any performance counters requires root privileges. To run the benchmarks with performance counters as a normal user this must be lowered to at most 2. To do this temporarily run `sudo sysctl --write kernel.perf_event_paranoid=2`.