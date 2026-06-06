# CKKS Decision Tree Inference POC

This repository contains a proof of concept for privacy-preserving decision tree inference with CKKS and Microsoft SEAL.

The project evaluates a decision tree in several modes:

- clear `hard` inference
- clear `soft_global` inference
- clear `soft_adaptive` inference
- encrypted `HE Soft global` inference
- encrypted `HE Soft adaptive` inference

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
- [Podman](https://podman.io/) installed and available in `PATH`
- Git
- Python 3

The container build installs Microsoft SEAL automatically. You do not need a separate local SEAL installation when using `run.ps1`.

## Quick Start

1. Clone the repository.
2. Open PowerShell in the repository root.
3. Run one of the supported datasets.

Example:

```powershell
.\run.ps1 -Dataset breast -SampleCount 32
```

On the first run, the script may:

- build the Podman image
- compile `poc_clear` and `poc_he`
- create result folders if they do not exist

If you want to force a rebuild:

```powershell
.\run.ps1 -Dataset breast -SampleCount 32 -Rebuild
```

## Supported Datasets

The main script currently exposes these dataset names:

- `iris`
- `cancer`
- `wine`
- `heart`
- `steel`
- `breast`

If you run `.\run.ps1` without `-Dataset`, the script shows an interactive dataset menu.

## Recommended Commands

Validated examples from the current workflow:

```powershell
.\run.ps1 -Dataset iris -SampleCount 20
.\run.ps1 -Dataset cancer -SampleCount 32
.\run.ps1 -Dataset wine -SampleCount 26
.\run.ps1 -Dataset heart -SampleCount 26
.\run.ps1 -Dataset breast -SampleCount 32
.\run.ps1 -Dataset steel -SampleCount 32
```

## What the Script Does

For a standard run, `run.ps1`:

1. selects the dataset
2. prepares the result directories
3. builds the Podman image if needed
4. compiles the clear and HE binaries in the container
5. runs `poc_clear`
6. runs `poc_he`
7. parses the output and stores structured results

## Expected Console Output

You should see sections similar to:

```text
===== poc_clear results =====
Hard (clear)             : correct/total - accuracy%
Soft global (clear)      : correct/total - accuracy%
Soft adaptive (clear)    : correct/total - accuracy%

===== poc_he results =====
HE Soft global           : correct/total - accuracy%   ms/inf
HE Soft adaptive         : correct/total - accuracy%   ms/inf
```

The script also prints a final summary with the dataset name and parsed metrics.

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

- clear `hard`, `soft_global`, and `soft_adaptive` metrics
- HE `soft_global` and `soft_adaptive` metrics
- average HE timing per inference
- paths to the detailed log and prediction files

## Troubleshooting

If `podman` is not recognized:

- install Podman
- restart PowerShell
- verify with:

```powershell
podman --version
```

If the script fails during container compilation:

- rerun with `-Rebuild`
- check the latest file in `results/logs/`

If a dataset is reported as unknown:

- use one of the dataset names listed above
- or run the script without `-Dataset` to use the menu

## Developer Notes

- The main implementation lives in `src/`
- The CKKS container setup is described in [src/README.md](/abs/c:/Users/madicke-diadji.mbodj/POC_DT/My_Poc_all/src/README.md)
- The repository also contains benchmark and paper material; they are not required for a basic run

## Related External Projects

This repository includes local comparison material related to external projects such as Akavia, SortingHat, and Concrete-ML.

If you want to run those projects directly, please use their official repositories and setup instructions from their respective links. This repository does not aim to replace their original installation or execution workflows.

## Minimal Reproduction Path

If someone lands on your GitHub page and wants the fastest path to a working run, this is the command to use first:

```powershell
.\run.ps1 -Dataset iris -SampleCount 20
```
