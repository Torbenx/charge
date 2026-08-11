# Charge Language

Charge is an experimental memory-safe systems programming language, most similar to C++ and Rust. It is a personal research project in early development. The primary goals of the language are:

* **Proven memory safety ... :** Ironclad and semi-automatic proofs, enabled by formalizing reference guarantees, object invariants, the memory model and more in the project's own intermediate representation Chiral.
* **... that doesn't get in the way:** most code should not interact with the proof system directly. Writing manual proofs is the escape hatch from inherently safe borrow-checked semantics.
* **Extensive proof system:** advanced memory techniques and hacks that are possible in C++ should be possible in Charge too, when accompanied by a proof.
* **Compact and powerful foundation:** the language is built on few but general concepts, unifying aspects of both C++ and Rust.

## Status

Next milestones:

* Parse and semantically check all safety-related Charge features, in particular `context` fields, borrow checking and `ghost` code.
* End-to-end test of Chiral: `input.chiral` &rarr; parsing to IR &rarr; backend ingest &rarr; SMT solving &rarr; proof replacement in IR &rarr; proof validation &rarr; formatting to file &rarr; `output.chiral`

## Building

The project requires a C++23 compatible compiler and standard library. Building is tested with Clang 22 and GCC 15 on Ubuntu 26.04. The build produces a single binary `charge` that runs the test suite.
