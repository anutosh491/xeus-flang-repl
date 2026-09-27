# xeus-flang-repl

An experimental Jupyter kernel for the incremental Flang interpreter.

The kernel keeps one `flangInterpreter` session alive and sends each Jupyter
cell to it as one incremental compilation unit. The current prototype is
native-only and implements the basic Jupyter `execute_request` path, including
Fortran stdout/stderr forwarding.

## Build the current prototype

The prototype currently expects a Flang build that exports the experimental
`flangInterpreter` target.

```sh
cmake -S . -B build -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/clang \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
  -DCMAKE_PREFIX_PATH=/Users/anutosh491/micromamba/envs/xeus-cpp-llvm23 \
  -DMLIR_DIR=/private/tmp/flang-wasm-lean-build/lib/cmake/mlir \
  -DFlang_DIR=/private/tmp/flang-wasm-lean-build/lib/cmake/flang \
  -DXEUS_FLANG_SOURCE_DIR=/Users/anutosh491/work/llvm-project-flang-repl \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run `build/xflang --version` as a quick smoke test. Installing the project also
installs its `xflang` kernelspec.

## Initial scope

- one persistent Flang interpreter per kernel process
- one MLIR module per Jupyter cell
- incremental declarations and executable statements
- native MLIR ExecutionEngine execution
- Flang runtime loading and output forwarding

OpenMP/OpenACC flags, library loading magics, completion, inspection, and a
WebAssembly kernel are intentionally follow-up work.

## Logo

The kernelspec uses the public-domain Fortran language logo from the
[Fortran-lang website](https://github.com/fortran-lang/fortran-lang.org/blob/master/assets/img/fortran-logo.svg).
It is a Fortran language mark, not a Flang-specific logo.
