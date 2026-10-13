This project contains the experimental memory-safe systems programming language 'charge'.

The main part is the compiler in the `compiler` directory which is further subdivided into the components:
* `compiler/parse` containing the parser generator
* `compiler/sema` containing the semantic analysis
* `compiler/server` containing an LSP-based language server
* `compiler/verify` containing an intermediate representation called chiral and an SMT solver to prove memory safety. It has further subcomponents:
    * `verify/ir` contains definitions and tools for chiral
    * `verify/backend` contains the SMT solver
    * `verify/language` contains a text-based representation of chiral

Test files are contained in the `tests` directory.

The `build` directory contains the build files. If it's not already configured, initialize it using one of the presets, for example `cmake --preset clang`. Clang 20+ or GCC 15+ is required to build, with Clang being preferred for better performance. When compatible use the system default Clang version. Using an MSVC-based build requires configuring in a Developer Command Prompt, but the GNU-like interface of plain `clang` is still preferred over `clang-cl` here.

To build the project run `cmake --build build`. There is a single executable `charge` directly in the build directory. Executing it with `./build/charge` will run the entire gtest-based test suite.

Use `dbgln()` and `dbgprint()` for debug logging. They accept C++23 style string formatting.

When complex code formatting is required use `clang-format` instead of doing it by hand.