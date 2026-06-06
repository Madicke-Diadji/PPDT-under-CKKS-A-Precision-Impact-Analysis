$ErrorActionPreference = "Stop"

$imageName = "poc-hbdt-seal"
$containerBuildDir = "/workspace/build-podman"
$sealBuildJobs = 2
$treePath = "/workspace/data/tree.csv"
$dataDir = Join-Path $PWD "data"

$trainFiles = Get-ChildItem $dataDir -Filter "*_train.csv" | Sort-Object Name
if (-not $trainFiles) {
  throw "No *_train.csv dataset was found in the data folder."
}

$datasets = @()
foreach ($trainFile in $trainFiles) {
  $prefix = $trainFile.BaseName -replace "_train$", ""
  $testPath = Join-Path $dataDir "${prefix}_test.csv"
  if (Test-Path $testPath) {
    $datasets += [PSCustomObject]@{
      Name = $prefix
      TrainPath = $trainFile.FullName
      TestPath = $testPath
    }
  }
}

if (-not $datasets) {
  throw "No train/test dataset pair was found in the data folder."
}

Write-Host ""
Write-Host "Available datasets:"
for ($i = 0; $i -lt $datasets.Count; $i++) {
  Write-Host ("  [{0}] {1}" -f ($i + 1), $datasets[$i].Name)
}

$choiceText = Read-Host "Choose a dataset (number)"
$parsedChoice = 0
if (-not [int]::TryParse($choiceText, [ref]$parsedChoice)) {
  throw "Invalid choice: enter a number."
}

$choice = $parsedChoice
if ($choice -lt 1 -or $choice -gt $datasets.Count) {
  throw "Choice out of range."
}

$selected = $datasets[$choice - 1]
$datasetPrefix = "data/$($selected.Name)"

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

python .\train_and_export.py --data-prefix $datasetPrefix --depth 4 --output data/tree.csv --json data/tree.json

podman build --build-arg SEAL_BUILD_JOBS=$sealBuildJobs -t $imageName -f Containerfile .

podman run --rm `
  -v "${PWD}:/workspace" `
  -w /workspace `
  $imageName `
  bash -lc "cmake -S . -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --target poc_clear --parallel && cmake --build $containerBuildDir --target poc_he --parallel 1"

$localPocClear = Join-Path $PWD "build-podman\poc_clear"
$localPocHe = Join-Path $PWD "build-podman\poc_he"

if (-not (Test-Path $localPocClear)) {
  throw "The poc_clear binary was not generated in build-podman."
}

if (-not (Test-Path $localPocHe)) {
  throw "The poc_he binary was not generated in build-podman. Re-run the poc_he build in verbose mode to inspect the real error: podman run --rm -v `"${PWD}:/workspace`" -w /workspace $imageName bash -lc `"cmake --build $containerBuildDir --target poc_he --verbose --parallel 1`""
}

Write-Host ""
Write-Host "===== poc_clear results ====="
podman run --rm `
  -v "${PWD}:/workspace" `
  -w /workspace `
  $imageName `
  bash -lc "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_clear $treePath $nbFeatures $nbClasses"

Write-Host ""
Write-Host "===== poc_he results ====="
podman run --rm `
  -v "${PWD}:/workspace" `
  -w /workspace `
  $imageName `
  bash -lc "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he $treePath $nbFeatures $nbClasses"
