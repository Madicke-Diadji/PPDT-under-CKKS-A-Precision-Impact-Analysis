# CKKS Decision Tree Inference POC

This repository contains a proof of concept for privacy-preserving decision tree inference with CKKS and Microsoft SEAL.

The project evaluates a decision tree in several modes:

- clear `hard` inference
- clear `soft_global` inference
- clear `soft_adaptive` inference
- encrypted `HE Soft adaptive` inference

For terminal readability, `run.ps1` hides `soft_global` lines from the live console output and from the final terminal summary. Those metrics are still captured in logs and CSV exports.

The main entry point for reproducible runs is [`run.ps1`](/abs/c:/Users/madicke-diadji.mbodj/POC_DT/My_Poc_all/run.ps1).

## What This Repo Contains

- `src/`: C++ implementation of clear and CKKS inference
- `data/`: datasets, exported trees, and benchmark inputs
- `results/`: generated logs and CSV results
- `DT_clear/`: plain-tree benchmark assets
- `paper_dpm_esorics/`: paper source
- `run.ps1`: main orchestration script

## Prerequisites

To run the POC as documented here, you need:

- Windows PowerShell
- either [Podman](https://podman.io/) or Docker installed and available in `PATH`
- Git
- Python 3

The container build installs Microsoft SEAL automatically. You do not need a separate local SEAL installation when using `run.ps1`.

## Quick Start

1. Clone the repository.
2. Open PowerShell in the repository root.
3. Run the dataset you want with `run.ps1`.

The first run may build the container image, compile `poc_clear` and `poc_he`, and create the `results/` folders.

## Reviewer Commands

The recommended entry point for all reviewers is `run.ps1`.

With Podman:

```powershell
.\run.ps1 -Dataset iris -SampleCount 20 -Rebuild
.\run.ps1 -Dataset cancer -SampleCount 32 -Rebuild
.\run.ps1 -Dataset wine -SampleCount 26 -Rebuild
.\run.ps1 -Dataset heart -SampleCount 26 -Rebuild
.\run.ps1 -Dataset breast -SampleCount 32 -Rebuild
.\run.ps1 -Dataset steel -SampleCount 32 -Rebuild
```

With Docker:

```powershell
.\run.ps1 -Dataset iris -SampleCount 20 -Rebuild -ContainerEngine docker
.\run.ps1 -Dataset cancer -SampleCount 32 -Rebuild -ContainerEngine docker
.\run.ps1 -Dataset wine -SampleCount 26 -Rebuild -ContainerEngine docker
.\run.ps1 -Dataset heart -SampleCount 26 -Rebuild -ContainerEngine docker
.\run.ps1 -Dataset breast -SampleCount 32 -Rebuild -ContainerEngine docker
.\run.ps1 -Dataset steel -SampleCount 32 -Rebuild -ContainerEngine docker
```

Notes:

- `podman` is the default container engine
- Docker is enabled through `-ContainerEngine docker`
- `-Rebuild` is recommended on a new machine to avoid stale binaries
- for standard datasets (`iris`, `cancer`, `wine`), `run.ps1` now uses the plain hard trees from `DT_clear/plain_tree_<dataset>.json`

`run.ps1`:

- builds the container image when needed
- compiles `poc_clear` and `poc_he` inside the container
- runs clear and HE inference
- saves logs and CSV outputs in `results/`

## Supported Datasets

The main script currently exposes these dataset names:

- `iris`
- `cancer`
- `wine`
- `heart`
- `steel`
- `breast`

If you run `.\run.ps1` without `-Dataset`, the script shows an interactive dataset menu.

## What the Script Does

For a standard run, `run.ps1`:

1. selects the dataset
2. prepares the result directories
3. builds the Podman image if needed
4. compiles the clear and HE binaries in the container
5. loads the plain hard tree from `DT_clear/` for standard datasets or the dataset model for special datasets
6. runs `poc_clear`
7. runs `poc_he`
8. parses the output and stores structured results

## Expected Console Output

You should see sections similar to:

```text
===== poc_clear results =====
Hard (clear)             : correct/total - accuracy%
Soft adaptive (clear)    : correct/total - accuracy%

===== poc_he results =====
HE Soft adaptive         : correct/total - accuracy%   ms/inf
```

The `soft_global` results are intentionally not shown in the terminal output anymore.

The script also prints a final summary with the dataset name and the visible parsed metrics.

## Generated Files

After each run, the script saves:

- cumulative results CSV:
  `results/run_results_soft_adaptatif.csv`
- timestamped run log:
  `results/logs/run_<dataset>_<timestamp>.log`
- detailed clear predictions:
  `results/logs/run_<dataset>_<timestamp>_clear_predictions.csv`
- detailed HE predictions:
  `results/logs/run_<dataset>_<timestamp>_he_predictions.csv`

The cumulative CSV includes, among other fields:

- clear `hard`,  and `soft_adaptive` metrics
- HE `soft_adaptive` metrics
- average HE timing per inference
- paths to the detailed log and prediction files

The timestamped log and CSV files keep the `soft_global` metrics even though they are hidden from the terminal.

## Troubleshooting

If the selected container engine is not recognized:

- install Podman or Docker
- restart PowerShell
- verify with one of:

```powershell
podman --version
docker --version
```

If the script fails during container compilation:

- rerun with `-Rebuild`
- check the latest file in `results/logs/`

If a dataset is reported as unknown:

- use one of the dataset names listed above
- or run the script without `-Dataset` to use the menu

## Minimal Reproduction Path

If someone lands on your GitHub page and wants the fastest path to a working run, this is the command to use first:

```powershell
.\run.ps1 -Dataset cancer -SampleCount 32 -Rebuild
```
