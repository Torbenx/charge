#!/usr/bin/env python3
"""Extract token frequencies from a charge source file and plot histograms,
split by which state of the two-state expression lexer (see
compiler/parse/lexer_expr2state.cpp) produced each token.

Token kinds are read straight from the real lexer via `charge dump-tokens`
(one LexerToken name per line, in source order) so the frequencies always
match the actual grammar/keyword table. The expression/after_expression
state assigned to each token is then replayed in Python against a transition
table transcribed from lexer_expr2state.cpp: every `goto expression$with_emit`
/ `goto after_expression$with_emit` in that file becomes one entry below.
Transitions the real lexer marks `error$with_emit` (VERIFY_NOT_REACHED) are
left out of the table on purpose - if the input ever needed one, that would
mean lexExpr2State could no longer lex it either, and this script deliberately
raises rather than guessing what state such a token belongs to.

Usage:
    ./token-state-histograms.py [source.chrg] [-o presentation/assets]
"""

import argparse
import os
import subprocess
import sys
from collections import Counter

import matplotlib.pyplot as plt

REPO_ROOT = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SOURCE = os.path.join(REPO_ROOT, "benchmark-expr", "benchmark-expr-nocomments.chrg")
DEFAULT_CHARGE_BIN = os.path.join(REPO_ROOT, "build", "charge")
DEFAULT_OUT_DIR = os.path.join(REPO_ROOT, "presentation", "assets")

EXPRESSION = "expression"
AFTER_EXPRESSION = "after_expression"

# Every identifier, keyword and "special identifier" is scanned by the same
# `readWord` case in both switches, so they all share one transition rule.
KEYWORDS = {
    "Assert", "Break", "Catch", "Const", "Continue", "Destroy", "Discard", "Do",
    "Elif", "Else", "For", "If", "Impl", "In", "Let", "Loop", "Prove", "Return",
    "Shared", "Static", "Try", "Unique", "Var", "While",
}
SPECIAL_IDENTIFIERS = {
    "Base", "Context", "Enum", "Fn", "Incomplete", "Namespace", "Open",
    "Struct", "Template", "Trait", "Virtual",
}
WORD_TOKENS = {"Identifier"} | KEYWORDS | SPECIAL_IDENTIFIERS

# state -> token kind -> next state. Transcribed case-by-case from the two
# switches in lexer_expr2state.cpp (`expression$no_emit` starting at line 22,
# `after_expression$no_emit` starting at line 351). A token is *lexed under*
# the state whose switch matched its case; the goto target it ends in decides
# the state for the token that follows.
TRANSITIONS = {
    EXPRESSION: {
        "CharacterLiteral": AFTER_EXPRESSION,
        "StringLiteral": AFTER_EXPRESSION,
        "SlashEqual": AFTER_EXPRESSION,
        "Slash": AFTER_EXPRESSION,
        "LeftParen": EXPRESSION,
        "RightParen": AFTER_EXPRESSION,
        "LeftSquare": EXPRESSION,
        "RightSquare": AFTER_EXPRESSION,
        "RightBrace": AFTER_EXPRESSION,
        "Comma": AFTER_EXPRESSION,
        "Point": AFTER_EXPRESSION,
        "Tilde": EXPRESSION,
        "Exclaim": EXPRESSION,
        "Star": EXPRESSION,
        "Hat": EXPRESSION,
        "Percent": EXPRESSION,
        "Amp": EXPRESSION,
        "PlusPlus": EXPRESSION,
        "Plus": EXPRESSION,
        "MinusMinus": EXPRESSION,
        "Minus": EXPRESSION,
        "NumericLiteral": AFTER_EXPRESSION,
        **{word: AFTER_EXPRESSION for word in WORD_TOKENS},
        # LeftBrace, SemiColon, Less/LessEqual/LessLess/LessLessEqual/LessEqualGreater,
        # ExclaimEqual, StarEqual, HatEqual, PercentEqual, AmpAmp/AmpAmpEqual/AmpEqual,
        # Vert*, Greater*, PlusEqual, MinusEqual, MinusGreater, Equal*, Colon* never
        # validly start an expression - the real lexer treats them as
        # `error$with_emit` (VERIFY_NOT_REACHED) in this state.
    },
    AFTER_EXPRESSION: {
        "SlashEqual": EXPRESSION,
        "Slash": EXPRESSION,
        "Less": EXPRESSION,
        "LessEqual": EXPRESSION,
        "LessLess": EXPRESSION,
        "LessLessEqual": EXPRESSION,
        "LessEqualGreater": EXPRESSION,
        "LeftParen": EXPRESSION,
        "RightParen": AFTER_EXPRESSION,
        "LeftSquare": EXPRESSION,
        "RightSquare": AFTER_EXPRESSION,
        "LeftBrace": EXPRESSION,
        "RightBrace": AFTER_EXPRESSION,
        "Comma": EXPRESSION,
        "Point": EXPRESSION,
        "SemiColon": EXPRESSION,
        "ExclaimEqual": EXPRESSION,
        "StarEqual": EXPRESSION,
        "Star": EXPRESSION,
        "HatEqual": EXPRESSION,
        "Hat": EXPRESSION,
        "PercentEqual": EXPRESSION,
        "Percent": EXPRESSION,
        "AmpAmpEqual": EXPRESSION,
        "AmpAmp": EXPRESSION,
        "AmpEqual": EXPRESSION,
        "Amp": EXPRESSION,
        "VertVertEqual": EXPRESSION,
        "VertVert": EXPRESSION,
        "VertEqual": EXPRESSION,
        "Vert": EXPRESSION,
        "GreaterGreaterEqual": EXPRESSION,
        "GreaterGreater": EXPRESSION,
        "GreaterEqual": EXPRESSION,
        "Greater": EXPRESSION,
        "PlusPlus": AFTER_EXPRESSION,
        "PlusEqual": EXPRESSION,
        "Plus": EXPRESSION,
        "MinusMinus": AFTER_EXPRESSION,
        "MinusEqual": EXPRESSION,
        "MinusGreater": EXPRESSION,
        "Minus": EXPRESSION,
        "EqualEqual": EXPRESSION,
        "EqualGreater": EXPRESSION,
        "Equal": EXPRESSION,
        "ColonColon": EXPRESSION,
        # CharacterLiteral, StringLiteral, identifiers/keywords, NumericLiteral,
        # Tilde, Exclaim and Colon can't validly follow a value without an
        # operator between them - `error$with_emit` in this state too.
    },
}


def dumpTokenKinds(chargeBin, sourceFile):
    result = subprocess.run([chargeBin, "dump-tokens", sourceFile], check=True, capture_output=True, text=True)
    return [line for line in result.stdout.splitlines() if line]


def assignStates(tokenKinds):
    """Replay TRANSITIONS across the real token stream, yielding (kind, state) pairs.

    lexExpr2State always starts in the `expression` state (see the initial
    `goto expression$no_emit;`).  EOS is emitted identically from either
    switch's '\\0' case and carries no meaningful state of its own, so it is
    dropped rather than force-fit into one bucket.
    """
    state = EXPRESSION
    for kind in tokenKinds:
        if kind == "EOS":
            continue
        yield kind, state
        try:
            state = TRANSITIONS[state][kind]
        except KeyError:
            sys.exit(
                f"token-state-histograms: lexExpr2State has no valid transition for "
                f"{kind!r} while in state {state!r} - the transition table is out of "
                f"sync with compiler/parse/lexer_expr2state.cpp"
            )


# Colors chosen to read on reveal.js's `black` theme (dark background).
# All three are kept noticeably distinct from one another so the overall
# chart's bars, and each state in the by-state/state-share charts, are never
# confused for each other.
COLOR_OVERALL = "#8AB4F8"
COLOR_EXPRESSION = "#BA68C8"
COLOR_AFTER_EXPRESSION = "#FFB74D"
TEXT_COLOR = "#E8E8E8"
GRID_COLOR = "#444444"


# All three charts share these so a fixed number of bars always comes out the
# same height/spacing, and the same margin (in inches) is reserved for the
# title/x-label regardless of how many bars a chart has - this is the layout
# plotOverall used originally, generalized to a variable bar count.
FIG_WIDTH_INCH = 9
BAR_HEIGHT_INCH = 6.5 / 20
MARGIN_TOP_INCH = 0.6
MARGIN_BOTTOM_INCH = 0.7
LEGEND_ROW_INCH = 0.35
LEFT_MARGIN_FRACTION = 0.16
RIGHT_MARGIN_FRACTION = 0.97


def makeFigure(numBars, withLegendRow=False):
    """Bar count -> figure size/margins, all in fixed inches so a given number
    of bars always renders at the same thickness/spacing no matter which
    chart it's in - this is the layout plotOverall used originally,
    generalized to a variable bar count (and an optional legend band pinned
    below the axes, out of the bars' way, for the stacked charts)."""
    bottomMargin = MARGIN_BOTTOM_INCH + (LEGEND_ROW_INCH if withLegendRow else 0)
    height = numBars * BAR_HEIGHT_INCH + MARGIN_TOP_INCH + bottomMargin
    fig, ax = plt.subplots(figsize=(FIG_WIDTH_INCH, height), dpi=150)
    fig.subplots_adjust(
        top=1 - MARGIN_TOP_INCH / height,
        bottom=bottomMargin / height,
        left=LEFT_MARGIN_FRACTION,
        right=RIGHT_MARGIN_FRACTION,
    )
    return fig, ax


def addStateLegend(fig, ax):
    """Place the expression/after_expression legend in the reserved band below
    the x-label, in figure coordinates, so it never overlaps a bar or title."""
    height = fig.get_size_inches()[1]
    legendY = (LEGEND_ROW_INCH * 0.5) / height
    legend = fig.legend(
        *ax.get_legend_handles_labels(),
        loc="center",
        bbox_to_anchor=(0.5, legendY),
        ncol=2,
        frameon=False,
    )
    for text in legend.get_texts():
        text.set_color(TEXT_COLOR)


def styleAxes(ax, fig):
    fig.patch.set_alpha(0)
    ax.patch.set_alpha(0)
    for spine in ax.spines.values():
        spine.set_color(GRID_COLOR)
    ax.tick_params(colors=TEXT_COLOR)
    ax.xaxis.label.set_color(TEXT_COLOR)
    ax.yaxis.label.set_color(TEXT_COLOR)
    ax.title.set_color(TEXT_COLOR)
    ax.grid(axis="x", color=GRID_COLOR, linewidth=0.6)
    ax.set_axisbelow(True)


# Matches matplotlib's default 5% autoscale margin, applied explicitly so it
# lands on the same value for both occurrence-count charts (see xMax below).
AXIS_PADDING_FRACTION = 0.05


def plotOverall(counts, names, outPath, topN, xMax):
    values = [counts[name] for name in names]

    fig, ax = makeFigure(len(names))
    ax.barh(names, values, color=COLOR_OVERALL)
    ax.set_xlabel("occurrences")
    ax.set_xlim(0, xMax)
    ax.set_title(f"Token frequency (top {topN})")
    styleAxes(ax, fig)
    fig.savefig(outPath, transparent=True)
    plt.close(fig)


def plotByState(byState, names, outPath, topN, xMax):
    exprValues = [byState[EXPRESSION][name] for name in names]
    afterValues = [byState[AFTER_EXPRESSION][name] for name in names]

    fig, ax = makeFigure(len(names), withLegendRow=True)
    ax.barh(names, exprValues, color=COLOR_EXPRESSION, label="expression state")
    ax.barh(names, afterValues, left=exprValues, color=COLOR_AFTER_EXPRESSION, label="after_expression state")
    ax.set_xlabel("occurrences")
    ax.set_xlim(0, xMax)
    ax.set_title(f"Token frequency by lexer state (top {topN})")
    addStateLegend(fig, ax)
    styleAxes(ax, fig)
    fig.savefig(outPath, transparent=True)
    plt.close(fig)


def plotSingleState(counts, names, outPath, topN, xMax, color, title):
    """Same token set/order, figure size and x-axis as plotOverall/plotByState,
    but showing only one state's counts - directly comparable at a glance."""
    values = [counts[name] for name in names]

    fig, ax = makeFigure(len(names))
    ax.barh(names, values, color=color)
    ax.set_xlabel("occurrences")
    ax.set_xlim(0, xMax)
    ax.set_title(title)
    styleAxes(ax, fig)
    fig.savefig(outPath, transparent=True)
    plt.close(fig)


def plotStateShare(byState, outPath, topN):
    """Per-token share of expression vs. after_expression state, for tokens that appear in both."""
    overallCounts = byState[EXPRESSION] + byState[AFTER_EXPRESSION]
    both = [name for name in overallCounts if byState[EXPRESSION][name] and byState[AFTER_EXPRESSION][name]]
    both.sort(key=lambda name: overallCounts[name], reverse=True)
    both = both[:topN][::-1]

    shares = [byState[EXPRESSION][name] / overallCounts[name] * 100 for name in both]

    fig, ax = makeFigure(len(both), withLegendRow=True)
    ax.barh(both, shares, color=COLOR_EXPRESSION, label="expression state")
    ax.barh(both, [100 - s for s in shares], left=shares, color=COLOR_AFTER_EXPRESSION, label="after_expression state")
    ax.set_xlabel("share of occurrences (%)")
    ax.set_xlim(0, 100)
    ax.set_title("Tokens produced from both lexer states")
    addStateLegend(fig, ax)
    styleAxes(ax, fig)
    fig.savefig(outPath, transparent=True)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", nargs="?", default=DEFAULT_SOURCE, help="charge source file to lex")
    parser.add_argument("-o", "--out-dir", default=DEFAULT_OUT_DIR, help="directory to write the histograms into")
    parser.add_argument("--charge-bin", default=DEFAULT_CHARGE_BIN, help="path to the built charge executable")
    parser.add_argument("--top", type=int, default=15, help="how many token kinds to show per chart")
    args = parser.parse_args()

    if not os.path.isfile(args.charge_bin):
        sys.exit(f"token-state-histograms: {args.charge_bin} not found - build it first (see CLAUDE.md)")

    tokenKinds = dumpTokenKinds(args.charge_bin, args.source)

    overall = Counter()
    byState = {EXPRESSION: Counter(), AFTER_EXPRESSION: Counter()}
    for kind, state in assignStates(tokenKinds):
        overall[kind] += 1
        byState[state][kind] += 1

    os.makedirs(args.out_dir, exist_ok=True)
    overallPath = os.path.join(args.out_dir, "token-frequency-overall.svg")
    byStatePath = os.path.join(args.out_dir, "token-frequency-by-state.svg")
    exprPath = os.path.join(args.out_dir, "token-frequency-expression.svg")
    afterPath = os.path.join(args.out_dir, "token-frequency-after-expression.svg")
    sharePath = os.path.join(args.out_dir, "token-frequency-state-share.svg")

    # One token set/order (by overall frequency) shared by every occurrence-count
    # chart, so the same row always means the same token across all of them.
    names = [name for name, _ in overall.most_common(args.top)][::-1]

    # Likewise one x-axis so bar lengths stay visually comparable across
    # slides: the largest single bar any of these charts could draw is the
    # overall top token count (no state, or stacked total, can exceed it).
    xMax = overall.most_common(1)[0][1] * (1 + AXIS_PADDING_FRACTION)

    plotOverall(overall, names, overallPath, args.top, xMax)
    plotByState(byState, names, byStatePath, args.top, xMax)
    plotSingleState(byState[EXPRESSION], names, exprPath, args.top, xMax,
        COLOR_EXPRESSION, f"Token frequency - expression state (top {args.top})")
    plotSingleState(byState[AFTER_EXPRESSION], names, afterPath, args.top, xMax,
        COLOR_AFTER_EXPRESSION, f"Token frequency - after_expression state (top {args.top})")
    plotStateShare(byState, sharePath, args.top)

    totalTokens = sum(overall.values())
    print(f"{totalTokens} tokens ({sum(byState[EXPRESSION].values())} expression, "
          f"{sum(byState[AFTER_EXPRESSION].values())} after_expression)")
    for path in (overallPath, byStatePath, exprPath, afterPath, sharePath):
        print(f"wrote {path}")


if __name__ == "__main__":
    main()
