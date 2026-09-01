#!/usr/bin/env python3
"""Pretty-print Google Benchmark JSON as a table.

Usage:  ./benchmark.bash <filter> | python benchmark-table.py [options]
"""

import argparse
import json
import os
import sys

# ---------------------------------------------------------------- formatting

RESET = "\033[0m"
BOLD = "\033[1m"
DIM = "\033[2m"
RED = "\033[31m"


class Style:
    def __init__(self, enabled):
        self.enabled = enabled

    def __call__(self, text, *codes):
        if not self.enabled or not codes:
            return text
        return "".join(codes) + text + RESET


def humanCount(value):
    if value is None:
        return "-"
    for limit, suffix in ((1e12, "T"), (1e9, "G"), (1e6, "M"), (1e3, "k")):
        if abs(value) >= limit:
            scaled = value / limit
            digits = 1 if abs(scaled) >= 100 else 2
            return f"{scaled:.{digits}f}{suffix}"
    return f"{value:.0f}"


def percent(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}%"


def fixed(value, digits=2):
    return "-" if value is None else f"{value:.{digits}f}"


# ---------------------------------------------------------------- input model

COUNTERS = ("CYCLES", "INSTRUCTIONS", "BRANCHES", "BRANCH-MISSES")


class Result:
    """All aggregates of a single benchmark, plus the metrics derived from them."""

    def __init__(self, name, index):
        self.name = name
        self.index = index
        self.repetitions = 1
        self.stats = {}

    def stat(self, aggregate, field):
        entry = self.stats.get(aggregate)
        return entry.get(field) if entry else None

    def value(self, field):
        """Preferred point estimate: median, else mean, else the raw single run."""
        for aggregate in ("median", "mean", "single"):
            entry = self.stats.get(aggregate)
            if entry and field in entry:
                return entry[field]
        return None

    def counter(self, name):
        return self.value(name)

    @property
    def cpuTime(self):
        return self.value("cpu_time")

    @property
    def realTime(self):
        return self.value("real_time")

    def relativeStddev(self, field):
        """Stddev of a field as a percentage of its mean."""
        # google benchmark reports the coefficient of variation as a fraction
        cv = self.stat("cv", field)
        if cv is not None:
            return cv * 100.0
        stddev = self.stat("stddev", field)
        mean = self.stat("mean", field)
        if stddev is not None and mean:
            return stddev / mean * 100.0
        return None

    def perIteration(self, rateCounter):
        """Undo google benchmark's per-second rate counters."""
        rate = self.value(rateCounter)
        if rate is None or self.realTime is None:
            return None
        return rate * self.realTime / 1e9

    @property
    def ipc(self):
        cycles = self.counter("CYCLES")
        instructions = self.counter("INSTRUCTIONS")
        if not cycles or instructions is None:
            return None
        return instructions / cycles

    @property
    def missRate(self):
        branches = self.counter("BRANCHES")
        misses = self.counter("BRANCH-MISSES")
        if not branches or misses is None:
            return None
        return misses / branches * 100.0

    def perLine(self, counter):
        lines = self.perIteration("lines")
        value = self.counter(counter)
        if not lines or value is None:
            return None
        return value / lines


def parseResults(text):
    start = text.find("{")
    if start < 0:
        sys.exit("benchmark-table: no JSON found on stdin (was --benchmark_format=json passed?)")
    try:
        document = json.loads(text[start:])
    except json.JSONDecodeError as error:
        sys.exit(f"benchmark-table: could not parse benchmark JSON: {error}")

    results = {}
    for entry in document.get("benchmarks", []):
        name = entry.get("run_name", entry.get("name", "?"))
        result = results.get(name)
        if result is None:
            result = results[name] = Result(name, len(results))
        result.repetitions = entry.get("repetitions", result.repetitions)
        aggregate = entry.get("aggregate_name") if entry.get("run_type") == "aggregate" else "single"
        result.stats[aggregate or "single"] = entry

    return document.get("context", {}), list(results.values())


def stripCommonPrefix(results):
    names = [result.name for result in results]
    if len(names) < 2:
        return
    prefix = os.path.commonprefix(names)
    cut = prefix.rfind("/") + 1
    if cut:
        for result in results:
            result.name = result.name[cut:]


# ---------------------------------------------------------------- table layout

class Column:
    def __init__(self, title, render, align=">"):
        self.title = title
        self.render = render
        self.align = align


def buildColumns(results):
    hasCounters = any(result.counter(name) is not None for result in results for name in COUNTERS)
    hasLines = any(result.perIteration("lines") is not None for result in results)

    columns = [Column("impl", lambda r: r.name, align="<")]
    if hasLines:
        columns += [
            Column("lines/s", lambda r: humanCount(r.value("lines"))),
            Column("lines/s ±%", lambda r: percent(r.relativeStddev("lines"))),
        ]
    if hasCounters:
        if hasLines:
            columns += [
                Column("cyc/line", lambda r: fixed(r.perLine("CYCLES"))),
                Column("cyc/line ±%", lambda r: percent(r.relativeStddev("CYCLES"))),
            ]
        columns += [
            Column("cycles", lambda r: humanCount(r.counter("CYCLES"))),
            Column("instr", lambda r: humanCount(r.counter("INSTRUCTIONS"))),
            Column("branches", lambda r: humanCount(r.counter("BRANCHES"))),
            Column("br-miss", lambda r: humanCount(r.counter("BRANCH-MISSES"))),
            Column("miss%", lambda r: percent(r.missRate)),
            Column("IPC", lambda r: fixed(r.ipc)),
        ]
    return columns


def renderTable(results, columns, style):
    cells = [[column.render(result) for column in columns] for result in results]

    widths = [max(len(column.title), *(len(row[i]) for row in cells))
              for i, column in enumerate(columns)]

    def line(values):
        return "  ".join(f"{text:{column.align}{widths[i]}}"
                         for i, (column, text) in enumerate(zip(columns, values)))

    print(style(line([column.title for column in columns]), BOLD))
    print(style("-" * (sum(widths) + 2 * (len(widths) - 1)), DIM))
    for values in cells:
        print(line(values))


def printContext(context, results, style):
    parts = []
    if context.get("host_name"):
        parts.append(context["host_name"])
    if context.get("num_cpus"):
        parts.append(f"{context['num_cpus']} cpus @ {context.get('mhz_per_cpu', '?')} MHz")
    if context.get("date"):
        parts.append(context["date"])
    repetitions = {result.repetitions for result in results}
    if len(repetitions) == 1:
        parts.append(f"{repetitions.pop()} repetitions")
    if parts:
        print(style(" | ".join(parts), DIM))
    if context.get("cpu_scaling_enabled"):
        print(style("warning: CPU frequency scaling is enabled, timings may be noisy", RED))
    if context.get("library_build_type") not in (None, "release"):
        print(style(f"warning: benchmark library built as {context['library_build_type']}", RED))


# ---------------------------------------------------------------------- main

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", nargs="?", default="-", help="JSON file (default: stdin)")
    parser.add_argument("-s", "--sort", choices=("none", "time", "name"), default="none",
                        help="row order (default: order of definition)")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto")
    arguments = parser.parse_args()

    text = sys.stdin.read() if arguments.file == "-" else open(arguments.file).read()
    context, results = parseResults(text)
    if not results:
        sys.exit("benchmark-table: no benchmarks in input")

    stripCommonPrefix(results)

    if arguments.sort == "time":
        results.sort(key=lambda result: (result.cpuTime is None, result.cpuTime))
    elif arguments.sort == "name":
        results.sort(key=lambda result: result.name)

    style = Style(arguments.color == "always" or (arguments.color == "auto" and sys.stdout.isatty()))

    printContext(context, results, style)
    print()
    renderTable(results, buildColumns(results), style)


if __name__ == "__main__":
    main()
