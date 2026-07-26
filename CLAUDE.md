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

The `build` directory contains the build files. Do *NOT* attempt do configure the build yourselves it should already be initialized. To build the project run `ninja` in the build directory. There a single exectuable `charge` directly in build directory. Executing it with `./charge` will run the entire gtest based test suite.

When complex code formatting is required use `clang-format` instead of doing it by hand.