# Charge Language

Charge is an experimental memory-safe systems programming language, most similar to C++ and Rust. It is a personal research project in early development. The primary goals of the language are:

* **Proven memory safety ... :** rigorous and semi-automatic proofs, enabled by formalizing reference guarantees, object invariants and more in the project's own intermediate representation Chiral.
* **... that doesn't get in the way:** most code should not interact with the proof system directly. Writing manual proofs is the escape hatch from inherently safe borrow-checked semantics.
* **Extensive proof system:** advanced memory techniques and hacks that are possible in C++ should be possible in Charge too, when accompanied by a proof.
* **Compact and powerful foundation:** the language is built on few but general concepts, unifying aspects of both C++ and Rust.

## Status

It is currently not possible to compile or verify any real programs.
The current work is focused on developing the safety-related compiler components individually and exercising them with tests. Integration between components is mostly missing and is only planned once they have reached a good level of maturity. Some important components, such as a compile-time interpreter and a code generation backend, are completely missing.

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

The project requires Python 3 and a C++23-compatible compiler and standard library. Supported compilers are Clang 20+ or GCC 15+, anything older and MSVC's `cl.exe` are _known to fail_. Clang is preferred as it gives better performance:
```bash
cmake --preset clang
cmake --build build && ./build/charge
```
This builds with the default Clang found in your PATH. Linux, macOS and Windows are supported, but how Clang gets on PATH differs: Installations on Linux via a package manager are usually automatically in PATH, same for Xcode on macOS. Homebrew and MinGW installs are typically added by hand. With Visual Studio, either configure from a Developer Command Prompt, or open the folder in Visual Studio and select the Clang preset there.

If Clang is not found or the version is too old, you can point CMake directly at your preferred compiler, for example:
```bash
cmake --preset default \
  -DCMAKE_C_COMPILER=clang-20 \
  -DCMAKE_CXX_COMPILER=clang++-20
```

> [!WARNING]
> Configuring without a preset will make an unoptimized build with only the baseline ISA for x86-64.

To test the Visual Studio Code extension, Node.js is required. First run `npm install` once in the `vscode` directory to set up the extension. Then open the project in Visual Studio Code and press F5 or manually run the `Extension` configuration. This will open a development instance with the extension installed.
