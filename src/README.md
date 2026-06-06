# POC HBDT SumPath avec Microsoft SEAL

Ce dossier `src/` contient maintenant une version CKKS basée sur `Microsoft SEAL` au lieu d'`OpenFHE`.

## Points clés

- `HEInference.h/.cpp` utilisent `seal::Ciphertext`, `seal::CKKSEncoder`, `seal::Evaluator`, `seal::Encryptor` et `seal::Decryptor`
- `CMakeLists.txt` cherche `SEAL` via `find_package(SEAL REQUIRED CONFIG)`
- `Containerfile` construit et installe `Microsoft SEAL`
- les scripts `podman-*.ps1` passent par `SEAL_ROOT` et `SEAL_DIR`

## Build natif Linux

Si SEAL est déjà installé :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=/opt/seal-install
cmake --build build --parallel
```

Ou directement avec le dossier de config CMake :

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSEAL_DIR=/opt/seal-install/lib/cmake/SEAL
cmake --build build --parallel
```

Variables reconnues :

- `SEAL_ROOT` : préfixe d'installation de Microsoft SEAL
- `SEAL_HINT_DIR` : chemin indicatif vers `SEALConfig.cmake`
- `SEAL_DIR` : chemin exact du dossier contenant `SEALConfig.cmake`

## Build avec Podman

Construire l'image :

```powershell
podman build --build-arg SEAL_BUILD_JOBS=2 -t poc-hbdt-seal -f Containerfile .
```

Compiler dans le conteneur :

```powershell
podman run --rm -v "${PWD}:/workspace" -w /workspace poc-hbdt-seal bash -lc "cmake -S . -B /workspace/build-podman -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=\$SEAL_ROOT -DSEAL_DIR=\$SEAL_DIR && cmake --build /workspace/build-podman --parallel"
```

Ou via les scripts fournis :

```powershell
.\podman-build.ps1
.\podman-build-and-run.ps1
.\podman-run-he.ps1
.\podman-run-dataset.ps1
.\podman-select-dataset-and-run.ps1
```

`.\podman-run-he.ps1` affiche maintenant un menu :

- option `1` : lancer la demo HE integree
- options suivantes : choisir un dataset detecte dans `data/`
- si aucun dataset n'est disponible, seule la demo integree est proposee

## Exécutables

Après compilation :

- `poc_clear`
- `poc_he`
- `run_tests`
- `run_tests_akavia`

## Remarque

La logique d'inférence côté arbre n'a pas été changée ; la migration porte sur le backend HE CKKS et la chaîne de build autour de SEAL.
