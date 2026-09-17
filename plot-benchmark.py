#!/usr/bin/env python3
"""Plot branch miss rate vs. repetition count from perf_ctl.bash output.

Usage:
    for c in 1 2 4 8 16 32 64; do ./perf_ctl.bash benchmark simple benchmark/utf8_codec.chrg -r $c; done > simple.txt
    for c in 1 2 4 8 16 32 64; do ./perf_ctl.bash benchmark fast benchmark/utf8_codec.chrg -r $c; done > fast.txt
    python3 plot-benchmark.py simple.txt fast.txt

Each 'Performance counter stats for ...' block is labeled using the
implementation and benchmark file extracted from its command line, and all
files are plotted together, grouped by that label.
"""

import argparse
import os
import re
import shlex
import sys

import matplotlib.pyplot as plt

HEADER = re.compile(r"Performance counter stats for '(.*)'")
REPETITIONS = re.compile(r"-r\s+(\d+)")
BRANCHES = re.compile(r"^\s*([\d,]+)\s+branches:u\b")
BRANCH_MISSES = re.compile(r"^\s*([\d,]+)\s+branch-misses:u\b")


def parseCommand(command):
    """Extract (implementation, benchmarkFile) from a 'charge benchmark <impl> <file> ...' invocation."""
    tokens = shlex.split(command)[1:]  # drop the executable itself

    positional = []
    skipNext = False
    for token in tokens:
        if skipNext:
            skipNext = False
            continue
        if token == "-r":
            skipNext = True
            continue
        if token.startswith("-"):
            continue
        positional.append(token)

    if positional and positional[0] == "benchmark":
        positional = positional[1:]

    implementation = positional[0] if len(positional) > 0 else None
    benchmarkFile = positional[1] if len(positional) > 1 else None
    return implementation, benchmarkFile


class Run:
    def __init__(self, repetitions, implementation, benchmarkFile):
        self.repetitions = repetitions
        self.implementation = implementation
        self.benchmarkFile = benchmarkFile
        self.branches = None
        self.branchMisses = None

    @property
    def missRate(self):
        if not self.branches or self.branchMisses is None:
            return None
        return self.branchMisses / self.branches * 100.0

    @property
    def label(self):
        parts = []
        if self.implementation:
            parts.append(self.implementation)
        if self.benchmarkFile:
            parts.append(os.path.basename(self.benchmarkFile))
        return " / ".join(parts) if parts else "?"

    @property
    def seriesKey(self):
        return (self.implementation, self.benchmarkFile)


def parseRuns(text):
    runs = []
    current = None
    for line in text.splitlines():
        header = HEADER.search(line)
        if header:
            command = header.group(1)
            match = REPETITIONS.search(command)
            if not match:
                sys.exit(f"plot-benchmark: no '-r <count>' found in: {command}")
            implementation, benchmarkFile = parseCommand(command)
            current = Run(int(match.group(1)), implementation, benchmarkFile)
            runs.append(current)
            continue
        if current is None:
            continue
        match = BRANCHES.match(line)
        if match:
            current.branches = int(match.group(1).replace(",", ""))
            continue
        match = BRANCH_MISSES.match(line)
        if match:
            current.branchMisses = int(match.group(1).replace(",", ""))
    return runs


def filterRuns(runs, includeImpl, excludeImpl, includeFile, excludeFile):
    def keep(run):
        implementation = run.implementation or ""
        benchmarkFile = run.benchmarkFile or ""
        if includeImpl and not includeImpl.search(implementation):
            return False
        if excludeImpl and excludeImpl.search(implementation):
            return False
        if includeFile and not includeFile.search(benchmarkFile):
            return False
        if excludeFile and excludeFile.search(benchmarkFile):
            return False
        return True

    return [run for run in runs if keep(run)]


def groupRuns(runs):
    """Group runs by (implementation, benchmarkFile), preserving first-seen order."""
    groups = {}
    for run in runs:
        group = groups.setdefault(run.seriesKey, [])
        group.append(run)
    for group in groups.values():
        group.sort(key=lambda run: run.repetitions)
    return groups


def applyScale(axis, setScale, mode, base):
    if mode == "log":
        setScale("log", base=base)
    else:
        setScale("linear")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", help="perf_ctl.bash output file(s), '-' for stdin")
    parser.add_argument("-o", "--output", help="save plot to a file instead of showing it")
    parser.add_argument("--xscale", choices=("linear", "log"), default="log",
                        help="scaling of the repetition-count axis (default: log)")
    parser.add_argument("--yscale", choices=("linear", "log"), default="linear",
                        help="scaling of the branch-miss-rate axis (default: linear)")
    parser.add_argument("--xbase", type=float, default=2, help="log base for --xscale log (default: 2)")
    parser.add_argument("--ybase", type=float, default=10, help="log base for --yscale log (default: 10)")
    parser.add_argument("--include-impl", metavar="REGEX", help="only keep implementations matching REGEX")
    parser.add_argument("--exclude-impl", metavar="REGEX", help="drop implementations matching REGEX")
    parser.add_argument("--include-file", metavar="REGEX", help="only keep benchmark files matching REGEX")
    parser.add_argument("--exclude-file", metavar="REGEX", help="drop benchmark files matching REGEX")
    arguments = parser.parse_args()

    runs = []
    for path in arguments.files:
        text = sys.stdin.read() if path == "-" else open(path).read()
        runs.extend(parseRuns(text))
    runs = [run for run in runs if run.missRate is not None]
    runs = filterRuns(runs,
                       re.compile(arguments.include_impl) if arguments.include_impl else None,
                       re.compile(arguments.exclude_impl) if arguments.exclude_impl else None,
                       re.compile(arguments.include_file) if arguments.include_file else None,
                       re.compile(arguments.exclude_file) if arguments.exclude_file else None)
    if not runs:
        sys.exit("plot-benchmark: no branch-miss data found in input")

    groups = groupRuns(runs)

    figure, axis = plt.subplots()
    for group in groups.values():
        axis.plot([run.repetitions for run in group], [run.missRate for run in group],
                  marker="o", label=group[0].label)

    applyScale(axis, axis.set_xscale, arguments.xscale, arguments.xbase)
    applyScale(axis, axis.set_yscale, arguments.yscale, arguments.ybase)
    if arguments.yscale == "linear":
        axis.set_ylim(bottom=0)
    if arguments.xscale == "log":
        repetitions = sorted({run.repetitions for run in runs})
        axis.set_xticks(repetitions)
        axis.get_xaxis().set_major_formatter(plt.ScalarFormatter())

    axis.set_xlabel("repetitions (-r)")
    axis.set_ylabel("branch miss rate (%)")
    axis.set_title("Branch miss rate vs. repetition count")
    axis.grid(True, which="both", linestyle=":", alpha=0.5)
    if len(groups) > 1:
        axis.legend()
    figure.tight_layout()

    if arguments.output:
        figure.savefig(arguments.output)
    else:
        plt.show()


if __name__ == "__main__":
    main()
