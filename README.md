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
  -DXEUS_FLANG_SOURCE_DIR=/private/tmp/llvm-flang-repl-sparse \
  -DXEUS_FLANG_OPENMP_RUNTIME=/path/to/libomp.dylib \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Run `build/xflang --version` as a quick smoke test. Installing the project
always installs the `xflang` kernelspec. When `XEUS_FLANG_OPENMP_RUNTIME` is
set, it also installs `xflang-openmp`, which enables `-fopenmp`, uses `-O2`,
and preloads the selected runtime before the first cell.

The kernel uses one persistent `CompilerInstance` by default. Pass
`--cell-compiler=isolated` to construct a fresh input-specific compiler for
each cell while retaining the same interpreter, module history, and JIT. This
is primarily a correctness oracle for testing the persistent reset boundary.

## Initial scope

- one persistent Flang interpreter and compiler instance per kernel process
- one MLIR module per Jupyter cell
- incremental declarations and executable statements
- native MLIR ExecutionEngine execution
- Flang runtime loading and output forwarding
- frontend option forwarding and native library preloading
- native CPU OpenMP when launched with `-fopenmp` and `libomp`
- Jupyter rich display through the `xflang_display` Fortran module
- `%%mlir` inspection of the LLVM-dialect module sent to the JIT
- `%load /absolute/path/to/library` for loading native dynamic libraries

OpenACC execution, completion, source-level inspection, debugger integration,
and a WebAssembly kernel are follow-up work. Current Flang can expose OpenACC
operations in HLFIR, but its OpenACC-to-LLVM lowering is not yet complete.

## Examples

- `01-OpenMP-Tour.ipynb` exercises parallel regions, reductions, sections,
  and tasks.
- `02-Mandelbrot-OpenMP.ipynb` computes pixels with OpenMP and publishes a BMP
  directly through Jupyter rich display.
- `03-MLIR-Inspection.ipynb` uses `%%mlir` to inspect JIT input.
- `04-OpenACC-MLIR.ipynb` documents the current source-to-HLFIR boundary
  without claiming executable OpenACC support.
- `05-Stress-Test-and-OpenBLAS.ipynb` exercises persistent state, procedures,
  recursion, `ISO_C_BINDING`, native `%load`, CBLAS, and LAPACKE in one session.

## Logo

The kernelspec uses the public-domain Fortran language logo from the
[Fortran-lang website](https://github.com/fortran-lang/fortran-lang.org/blob/master/assets/img/fortran-logo.svg).
It is a Fortran language mark, not a Flang-specific logo.
