$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$projectRoot = Split-Path $scriptDir -Parent
$imageName = "poc-hbdt-seal"
$containerSourceDir = "/workspace/src"
$containerBuildDir = "/workspace/build-podman-seal"
$containerDtClearDir = "/workspace/DT_clear"
$localDataDir = Join-Path $scriptDir "data"
$rootDataDir = Join-Path $projectRoot "data"
$dtClearDir = Join-Path $projectRoot "DT_clear"
$sealBuildJobs = 2

function Ensure-Image {
  podman image exists $imageName 2>$null
  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Image $imageName is missing. Building it now..."
    podman build --build-arg SEAL_BUILD_JOBS=$sealBuildJobs -t $imageName -f (Join-Path $scriptDir "Containerfile") $scriptDir
  }
}

function Build-PocHe {
  podman run --rm `
    -v "${projectRoot}:/workspace" `
    -w /workspace `
    $imageName `
    bash -lc "rm -rf $containerBuildDir && cmake -S $containerSourceDir -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --target poc_he --parallel 1"
}

function Run-PocHeDemo {
  param(
    [string]$SampleText = ""
  )
  Write-Host ""
  Write-Host "Running the built-in HE demo..."
  $escapedSample = $SampleText.Replace('"', '\"')
  podman run --rm `
    -v "${projectRoot}:/workspace" `
    -w /workspace `
    -e "HE_SAMPLE=$escapedSample" `
    $imageName `
    bash -lc "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he"
}

Ensure-Image
Build-PocHe

$datasets = @()
$selectedDataDir = $null

if (Test-Path $localDataDir) {
  $selectedDataDir = $localDataDir
} elseif (Test-Path $rootDataDir) {
  $selectedDataDir = $rootDataDir
}

if ($selectedDataDir) {
  $trainFiles = Get-ChildItem $selectedDataDir -Filter "*_train.csv" | Sort-Object Name
  foreach ($trainFile in $trainFiles) {
    $prefix = $trainFile.BaseName -replace "_train$", ""
    $testPath = Join-Path $selectedDataDir "${prefix}_test.csv"
    if (Test-Path $testPath) {
      $datasets += [PSCustomObject]@{
        Name = $prefix
        TrainPath = $trainFile.FullName
        TestPath = $testPath
      }
    }
  }
}

Write-Host ""
Write-Host "Available options:"
Write-Host "  [1] Built-in HE demo"
if ($datasets.Count -gt 0) {
  Write-Host "  Detected data directory: $selectedDataDir"
  for ($i = 0; $i -lt $datasets.Count; $i++) {
    Write-Host ("  [{0}] Dataset: {1}" -f ($i + 2), $datasets[$i].Name)
  }
} else {
  Write-Host "  No dataset detected in src\\data or data"
}

$choiceText = Read-Host "Choose an option (number)"
$parsedChoice = 0
if (-not [int]::TryParse($choiceText, [ref]$parsedChoice)) {
  throw "Invalid choice: enter a number."
}

$choice = $parsedChoice
$sampleText = Read-Host "Enter a sample for inference (CSV or list), or leave empty for a random sample"

if ($choice -eq 1) {
  Run-PocHeDemo -SampleText $sampleText
  exit 0
}

if ($choice -lt 2 -or $choice -gt ($datasets.Count + 1)) {
  throw "Choice out of range."
}

$selected = $datasets[$choice - 2]
$hostDatasetPrefix = Join-Path $selectedDataDir $selected.Name
$plainTreeJson = Join-Path $dtClearDir ("plain_tree_{0}.json" -f $selected.Name)
$plainTreeTxt = Join-Path $dtClearDir ("plain_tree_{0}.txt" -f $selected.Name)

$header = (Get-Content $selected.TrainPath -TotalCount 1).Trim()
$headerParts = $header -split ","
$nbFeatures = $headerParts.Count - 1

$labels = @{}
Import-Csv $selected.TrainPath | ForEach-Object { $labels[$_.label] = $true }
Import-Csv $selected.TestPath | ForEach-Object { $labels[$_.label] = $true }
$nbClasses = $labels.Keys.Count

Write-Host ""
Write-Host "Selected dataset: $($selected.Name)"
Write-Host "  Features : $nbFeatures"
Write-Host "  Classes  : $nbClasses"
Write-Host "  Folder   : $selectedDataDir"

if (Test-Path $plainTreeJson) {
  Write-Host "  Clear tree : $plainTreeJson"
  $treeArg = "$containerDtClearDir/plain_tree_$($selected.Name).json"
} elseif (Test-Path $plainTreeTxt) {
  Write-Host "  Clear tree : $plainTreeTxt"
  $treeArg = "$containerDtClearDir/plain_tree_$($selected.Name).txt"
} else {
  throw "Plain hard tree not found for dataset '$($selected.Name)'. Expected $plainTreeJson or $plainTreeTxt"
}

podman run --rm `
  -v "${projectRoot}:/workspace" `
  -w /workspace `
  -e "HE_SAMPLE=$($sampleText.Replace('"', '\"'))" `
  $imageName `
  bash -lc "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he $treeArg $nbFeatures $nbClasses"
