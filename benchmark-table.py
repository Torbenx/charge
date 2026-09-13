#!/usr/bin/env python3
"""Pretty-print Google Benchmark JSON as a table.

Usage:  ./benchmark.bash <filter> | python benchmark-table.py [options]

Google benchmark writes its JSON incrementally while the run is still in
progress, so the input is usually a truncated document: the trailing entry,
the closing ']' and the closing '}' are missing.  The table is repaired,
rendered from whatever is complete so far and redrawn as new results arrive.
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


def closeTruncatedJson(text):
    """Repair a prefix of a JSON document.

    Drops the value google benchmark is still in the middle of writing and
    closes every bracket it has not closed yet.  Returns None when not even a
    single value has been completed so far.
    """
    stack = []
    inString = False
    escaped = False
    cut = None
    cutStack = None

    for index, character in enumerate(text):
        if inString:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                inString = False
            continue
        if character == '"':
            inString = True
        elif character in "{[":
            stack.append(character)
        elif character in "}]":
            if not stack:
                return None
            stack.pop()
            if stack:
                # a nested value just closed: truncating here keeps the
                # document well formed once the open brackets are closed
                cut, cutStack = index + 1, list(stack)

    if cut is None:
        return None
    closers = "".join("}" if bracket == "{" else "]" for bracket in reversed(cutStack))
    return text[:cut] + closers


def parseDocument(text):
    """Parse a complete or still incomplete benchmark document, else None."""
    start = text.find("{")
    if start < 0:
        return None
    body = text[start:]
    for candidate in (body, closeTruncatedJson(body)):
        if candidate is None:
            continue
        try:
            return json.loads(candidate)
        except json.JSONDecodeError:
            continue
    return None


def collectResults(document):
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


def tableLines(results, columns, style):
    cells = [[column.render(result) for column in columns] for result in results]

    widths = [max(len(column.title), *(len(row[i]) for row in cells))
              for i, column in enumerate(columns)]

    def line(values):
        return "  ".join(f"{text:{column.align}{widths[i]}}"
                         for i, (column, text) in enumerate(zip(columns, values)))

    lines = [style(line([column.title for column in columns]), BOLD),
             style("-" * (sum(widths) + 2 * (len(widths) - 1)), DIM)]
    lines += [line(values) for values in cells]
    return lines


def contextLines(context, results, style):
    lines = []
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
        lines.append(style(" | ".join(parts), DIM))
    if context.get("cpu_scaling_enabled"):
        lines.append(style("warning: CPU frequency scaling is enabled, timings may be noisy", RED))
    if context.get("library_build_type") not in (None, "release"):
        lines.append(style(f"warning: benchmark library built as {context['library_build_type']}", RED))
    return lines


class Table:
    """Renders the table, redrawing the previous one when the output is live."""

    def __init__(self, style, sort, live):
        self.style = style
        self.sort = sort
        self.live = live
        self.drawn = 0

    def show(self, context, results):
        stripCommonPrefix(results)
        if self.sort == "time":
            results.sort(key=lambda result: (result.cpuTime is None, result.cpuTime))
        elif self.sort == "name":
            results.sort(key=lambda result: result.name)

        lines = contextLines(context, results, self.style)
        lines.append("")
        lines += tableLines(results, buildColumns(results), self.style)

        if self.drawn:
            sys.stdout.write(f"\033[{self.drawn}F\033[J")
        sys.stdout.write("\n".join(lines) + "\n")
        sys.stdout.flush()
        self.drawn = len(lines) if self.live else 0


# ---------------------------------------------------------------------- main

def readStream(source, table):
    """Read incrementally, redrawing the table whenever a result completes."""
    text = ""
    shown = None
    while True:
        try:
            chunk = source.readline()
        except KeyboardInterrupt:
            # the benchmark was cancelled: report what did finish
            break
        if not chunk:
            break
        text += chunk
        # an entry of the "benchmarks" array just ended: cheap redraw trigger
        if not table.live or chunk.strip() not in ("}", "},"):
            continue
        document = parseDocument(text)
        if document is None:
            continue
        count = len(document.get("benchmarks", []))
        if count == shown or count == 0:
            continue
        shown = count
        context, results = collectResults(document)
        if results:
            table.show(context, results)
    return text


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("file", nargs="?", default="-", help="JSON file (default: stdin)")
    parser.add_argument("-s", "--sort", choices=("none", "time", "name"), default="none",
                        help="row order (default: order of definition)")
    parser.add_argument("--color", choices=("auto", "always", "never"), default="auto")
    parser.add_argument("--live", choices=("auto", "always", "never"), default="auto",
                        help="redraw the table while benchmarks are still running")
    arguments = parser.parse_args()

    style = Style(arguments.color == "always" or (arguments.color == "auto" and sys.stdout.isatty()))
    live = arguments.live == "always" or (arguments.live == "auto" and sys.stdout.isatty())
    table = Table(style, arguments.sort, live)

    if arguments.file == "-":
        text = readStream(sys.stdin, table)
    else:
        text = open(arguments.file).read()

    document = parseDocument(text)
    if document is None:
        sys.exit("benchmark-table: no usable JSON found in input "
                 "(was --benchmark_format=json passed?)")

    context, results = collectResults(document)
    if not results:
        sys.exit("benchmark-table: no benchmarks in input")

    table.show(context, results)


if __name__ == "__main__":
    main()
