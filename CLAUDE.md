This project contains the experimental memory-safe systems programming language 'charge'.

The main part is the compiler in the `compiler` directory which is further subdivided into the components:
* `compiler/parse` containing the parser generator
* `compiler/sema` containing the semantic analysis
* `compiler/server` containing an LSP based language server
* `compiler/verify` containing an itermediate reprensentation and SMT solver to prove memory safety. It has further subcomponents:
    * `verify/ir` contains definitions and tools for the IR
    * `verify/backend` contains the SMT solver
    * `verify/language` contains a text based representation for the IR

Test files are contained in the `tests` directory.

The `build` directory contains the build files. If its not already configured run `cmake -G Ninja -S . -B build -DCMAKE_C_COMPILER=clang-22 -DCMAKE_CXX_COMPILER=clang++-22` in the project directory. To build the project run `cmake --build build`. There is a single exectuable `charge` directly in build directory. Executing it with `./build/charge` will run the entire gtest based test suite.

Use `dbgln()` and `dbgprint()` for debug logging. They accept C++23 style string formatting.

When complex code formatting is required use `clang-format` instead of doing it by hand.