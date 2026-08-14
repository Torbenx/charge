# Charge Language

Charge is an experimental memory-safe systems programming language, most similar to C++ and Rust. It is a personal research project in early development. The primary goals of the language are:

* **Proven memory safety ... :** rigorous and semi-automatic proofs, enabled by formalizing reference guarantees, object invariants, the memory model and more in the project's own intermediate representation Chiral.
* **... that doesn't get in the way:** most code should not interact with the proof system directly. Writing manual proofs is the escape hatch from inherently safe borrow-checked semantics.
* **Extensive proof system:** advanced memory techniques and hacks that are possible in C++ should be possible in Charge too, when accompanied by a proof.
* **Compact and powerful foundation:** the language is built on few but general concepts, unifying aspects of both C++ and Rust.

## Status

It is currently not possible to compile or verify any real programs.
The current work is focused on developing the safety-related compiler components individually and exercising them with tests. Integration between components is mostly missing and is only planned once they have reached a good level of maturity individually. Some important components, such as a compile-time interpreter and a code generation backend, are completely missing.

Next milestones:

* Parse and semantically check all safety-related Charge features, in particular `context` parameters and borrow checking.
* End-to-end test of Chiral: `input.chiral` &rarr; parsing to IR &rarr; backend ingest &rarr; SMT solving &rarr; proof replacement in IR &rarr; proof validation &rarr; formatting to file &rarr; `output.chiral`

## Layout

The project hosts two languages: Charge, a high-level systems programming language, and Chiral (<ins>Ch</ins>arge <ins>I</ins>ntermediate <ins>R</ins>epresentation <ins>A</ins>nd <ins>L</ins>anguage), a verification-focused intermediate representation.

The Charge compiler found in the `compiler` directory has the following subcomponents:

* `compiler/parse`: Parser for Charge, built using a custom parser generator.
* `compiler/sema`: Semantic analysis of the Charge language.
* `compiler/server`: LSP-based language server.

The Chiral tools are found in `compiler/verify` and are subdivided into:

* `compiler/verify/language`: Tools for the text representation of Chiral, such as a parser and formatter.
* `compiler/verify/ir`: Definition and tools for the binary representation of Chiral.
* `compiler/verify/backend`: From-scratch SMT solver focused on memory and invariant reasoning.

There is also a Visual Studio Code extension in `vscode` with syntax highlighting for both languages and support for the Charge language server.

## Building

The project requires Python 3 and a C++23-compatible compiler and standard library. Building is tested with Clang 22 and GCC 15 on Ubuntu 26.04. The build produces a single binary `charge` that runs the test suite.
```
cmake -S . -B build
cmake --build build && ./build/charge
```

To test the Visual Studio Code extension, Node.js is required. First run `npm install` once in the `vscode` directory to set up the extension. Then open the project in Visual Studio Code and press F5 or manually run the `Extension` configuration. This will open a development instance with the extension installed.
