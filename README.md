# Charge Language

Charge is intended to be a memory-safe systems programming language most similar to C++ and Rust. It is a personal research project in early-ish development. The primary goals of the language are:
* Proven memory safety: This is mostly self explanatory. However most code should not have to interact with the proof system directly. Writing manual proofs provides the escape hatch from inherently safe (borrow-checked) semanticts.
* Extensive proof system: Advanced memory technichques and hacks possible in C++ should be possible in Charge as well if accompanied with a proof.
* Compact and powerful foundation: The language should be build on few but general concepts, unifying aspects of both C++ and Rust.