# POC HBDT SumPath with Microsoft SEAL

This `src/` directory now contains a CKKS version based on `Microsoft SEAL` instead of `OpenFHE`.

## Key Points

- `HEInference.h/.cpp` use `seal::Ciphertext`, `seal::CKKSEncoder`, `seal::Evaluator`, `seal::Encryptor`, and `seal::Decryptor`
- `CMakeLists.txt` looks for `SEAL` via `find_package(SEAL REQUIRED CONFIG)`
- `Containerfile` builds and installs `Microsoft SEAL`
- the `podman-*.ps1` scripts rely on `SEAL_ROOT` and `SEAL_DIR`

## Native Linux Build

If SEAL is already installed:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=/opt/seal-install
cmake --build build --parallel
```

Or directly with the CMake config directory:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSEAL_DIR=/opt/seal-install/lib/cmake/SEAL
cmake --build build --parallel
```

Recognized variables:

- `SEAL_ROOT`: installation prefix for Microsoft SEAL
- `SEAL_HINT_DIR`: hint path to `SEALConfig.cmake`
- `SEAL_DIR`: exact directory containing `SEALConfig.cmake`

## Build with Podman

Build the image:

```powershell
podman build --build-arg SEAL_BUILD_JOBS=2 -t poc-hbdt-seal -f Containerfile .
```

Compile inside the container:

```powershell
podman run --rm -v "${PWD}:/workspace" -w /workspace poc-hbdt-seal bash -lc "cmake -S . -B /workspace/build-podman -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=\$SEAL_ROOT -DSEAL_DIR=\$SEAL_DIR && cmake --build /workspace/build-podman --parallel"
```

Or via the provided scripts:

```powershell
.\podman-build.ps1
.\podman-build-and-run.ps1
.\podman-run-he.ps1
.\podman-run-dataset.ps1
.\podman-select-dataset-and-run.ps1
```

`.\podman-run-he.ps1` now shows a menu:

- option `1`: run the built-in HE demo
- next options: choose a detected dataset in `data/`
- if no dataset is available, only the built-in demo is offered

## Executables

After compilation:

- `poc_clear`
- `poc_he`
- `run_tests`
- `run_tests_akavia`

## Note

The tree-side inference logic has not changed; the migration only affects the CKKS HE backend and the surrounding SEAL build toolchain.
