$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$projectRoot = Split-Path $scriptDir -Parent
$imageName = "poc-hbdt-seal"
$containerSourceDir = "/workspace/src"
$containerBuildDir = "/workspace/build-podman-seal"
$containerTreePath = "/workspace/data/tree.csv"
$containerDtClearDir = "/workspace/DT_clear"
$localDataDir = Join-Path $scriptDir "data"
$rootDataDir = Join-Path $projectRoot "data"
$dtClearDir = Join-Path $projectRoot "DT_clear"
$sealBuildJobs = 2

function Ensure-Image {
  podman image exists $imageName 2>$null
  if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "Image $imageName absente. Construction en cours..."
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
  Write-Host "Lancement de la demo HE integree..."
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
Write-Host "Options disponibles :"
Write-Host "  [1] Demo HE integree"
if ($datasets.Count -gt 0) {
  Write-Host "  Dossier data detecte : $selectedDataDir"
  for ($i = 0; $i -lt $datasets.Count; $i++) {
    Write-Host ("  [{0}] Dataset : {1}" -f ($i + 2), $datasets[$i].Name)
  }
} else {
  Write-Host "  Aucun dataset detecte dans src\data ni data"
}

$choiceText = Read-Host "Choisissez une option (numero)"
$parsedChoice = 0
if (-not [int]::TryParse($choiceText, [ref]$parsedChoice)) {
  throw "Choix invalide : entrez un numero."
}

$choice = $parsedChoice
$sampleText = Read-Host "Entrez un sample pour l'inference (CSV ou liste), ou laissez vide pour un sample aleatoire"

if ($choice -eq 1) {
  Run-PocHeDemo -SampleText $sampleText
  exit 0
}

if ($choice -lt 2 -or $choice -gt ($datasets.Count + 1)) {
  throw "Choix hors intervalle."
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
Write-Host "Dataset selectionne : $($selected.Name)"
Write-Host "  Features : $nbFeatures"
Write-Host "  Classes  : $nbClasses"
Write-Host "  Dossier  : $selectedDataDir"

$treeArg = $containerTreePath
if (Test-Path $plainTreeJson) {
  Write-Host "  Arbre clair : $plainTreeJson"
  $treeArg = "$containerDtClearDir/plain_tree_$($selected.Name).json"
} elseif (Test-Path $plainTreeTxt) {
  Write-Host "  Arbre clair : $plainTreeTxt"
  $treeArg = "$containerDtClearDir/plain_tree_$($selected.Name).txt"
} else {
  Write-Host "  Arbre clair : aucun plain_tree trouve, generation via train_and_export.py"
  python (Join-Path $scriptDir "train_and_export.py") --data-prefix $hostDatasetPrefix --depth 4 --output (Join-Path $projectRoot "data/tree.csv") --json (Join-Path $projectRoot "data/tree.json")
}

podman run --rm `
  -v "${projectRoot}:/workspace" `
  -w /workspace `
  -e "HE_SAMPLE=$($sampleText.Replace('"', '\"'))" `
  $imageName `
  bash -lc "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he $treeArg $nbFeatures $nbClasses"
