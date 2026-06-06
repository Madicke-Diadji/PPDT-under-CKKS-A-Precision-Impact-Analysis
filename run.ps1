[CmdletBinding()]
param(
  [string]$Dataset,
  [string]$ImageName = "localhost/poc-hbdt-seal:latest",
  [string]$BuildDir = "build-podman-soft-adaptatif-he",
  [ValidateSet("podman", "docker")][string]$ContainerEngine = "podman",
  [switch]$Rebuild,
  [int]$SampleCount = 10
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$srcDir = Join-Path $repoRoot "src"
$dataDir = Join-Path $repoRoot "data"
$resultsDir = Join-Path $repoRoot "results"
$logsDir = Join-Path $resultsDir "logs"
$containerBuildDir = "/workspace/$BuildDir"
$containerPocClear = "$containerBuildDir/poc_clear"
$containerPocHe = "$containerBuildDir/poc_he"

$datasetMenu = @(
  [PSCustomObject]@{ Id = 1; Name = "iris";    Type = "standard"; Prefix = "iris" }
  [PSCustomObject]@{ Id = 2; Name = "cancer";  Type = "standard"; Prefix = "cancer" }
  [PSCustomObject]@{ Id = 3; Name = "heart";   Type = "special";  Prefix = $null }
  [PSCustomObject]@{ Id = 4; Name = "wine";    Type = "standard"; Prefix = "wine" }
  [PSCustomObject]@{ Id = 5; Name = "steel";   Type = "special";  Prefix = $null }
  [PSCustomObject]@{ Id = 6; Name = "breast";  Type = "special";  Prefix = $null }
)

function Invoke-LoggedCommand {
  param(
    [Parameter(Mandatory = $true)][string]$FilePath,
    [Parameter(Mandatory = $true)][string[]]$Arguments
  )

  $captured = New-Object System.Collections.Generic.List[string]
  $previousErrorActionPreference = $ErrorActionPreference
  $global:LASTEXITCODE = 0

  try {
    $ErrorActionPreference = "Continue"
    & $FilePath @Arguments 2>&1 | ForEach-Object {
      $line = $_.ToString()
      if ($line -ne "") {
        Write-Host $line
        [void]$captured.Add($line)
      }
    }
  } finally {
    $ErrorActionPreference = $previousErrorActionPreference
  }

  return [PSCustomObject]@{
    ExitCode = $LASTEXITCODE
    Lines = @($captured | ForEach-Object { "$_" })
    Text = (@($captured | ForEach-Object { "$_" }) -join [Environment]::NewLine)
  }
}

function Get-RegexValue {
  param(
    [string]$Text,
    [string]$Pattern
  )

  if ($Text -match $Pattern) {
    return $matches[1]
  }
  return $null
}

function Ensure-ResultsDirectories {
  if (-not (Test-Path $resultsDir)) {
    New-Item -ItemType Directory -Path $resultsDir | Out-Null
  }
  if (-not (Test-Path $logsDir)) {
    New-Item -ItemType Directory -Path $logsDir | Out-Null
  }
}

function Convert-ToNullableInt {
  param([string]$Value)

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $null
  }
  return [int]$Value
}

function Convert-ToNullableDouble {
  param([string]$Value)

  if ([string]::IsNullOrWhiteSpace($Value)) {
    return $null
  }
  return [double]::Parse($Value, [System.Globalization.CultureInfo]::InvariantCulture)
}

function Get-PredictionMetricsFromCsv {
  param(
    [string]$Path,
    [string[]]$PredictionColumns
  )

  if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path $Path)) {
    return $null
  }

  $rows = @(Import-Csv $Path)
  if ($rows.Count -eq 0) {
    return $null
  }

  $availableColumns = @($rows[0].PSObject.Properties.Name)
  $predictionColumn = $PredictionColumns | Where-Object { $_ -in $availableColumns } | Select-Object -First 1
  if (-not $predictionColumn -or -not ("y_true" -in $availableColumns)) {
    return $null
  }

  $correct = 0
  foreach ($row in $rows) {
    if ($row.y_true -eq $row.$predictionColumn) {
      $correct++
    }
  }

  $sampleCount = $rows.Count
  $accuracyPct = if ($sampleCount -gt 0) {
    [math]::Round((100.0 * $correct / $sampleCount), 2)
  } else {
    $null
  }

  return [PSCustomObject]@{
    CorrectCount = "$correct/$sampleCount"
    AccuracyPct = $accuracyPct
    SampleCount = $sampleCount
  }
}

function Get-CsvHeaderColumns {
  param(
    [Parameter(Mandatory = $true)][string]$Path
  )

  if (-not (Test-Path $Path)) {
    return @()
  }

  $firstLine = Get-Content $Path -TotalCount 1
  if ([string]::IsNullOrWhiteSpace($firstLine)) {
    return @()
  }

  $columns = $firstLine.Split(",") | ForEach-Object {
    $_.Trim().Trim('"')
  }
  return @($columns)
}

function Ensure-RunResultsCsvSchema {
  param(
    [Parameter(Mandatory = $true)][string]$CsvFile,
    [Parameter(Mandatory = $true)]$Row,
    [Parameter(Mandatory = $true)][datetime]$Timestamp
  )

  if (-not (Test-Path $CsvFile)) {
    return
  }

  $existingColumns = @(Get-CsvHeaderColumns -Path $CsvFile)
  if ($existingColumns.Count -eq 0) {
    return
  }

  $rowColumns = @($Row.PSObject.Properties.Name)
  $sameCount = ($existingColumns.Count -eq $rowColumns.Count)
  $sameOrder = $sameCount

  if ($sameOrder) {
    for ($i = 0; $i -lt $existingColumns.Count; $i++) {
      if ($existingColumns[$i] -ne $rowColumns[$i]) {
        $sameOrder = $false
        break
      }
    }
  }

  if ($sameOrder) {
    return
  }

  $archiveFile = Join-Path $resultsDir ("run_results_soft_adaptatif_legacy_{0}.csv" -f $Timestamp.ToString("yyyyMMdd_HHmmss"))
  Move-Item -LiteralPath $CsvFile -Destination $archiveFile
  Write-Host ""
  Write-Host "Legacy CSV schema detected."
  Write-Host "  Archived previous file : $archiveFile"
  Write-Host "  New file created       : $CsvFile"
}

function Write-RunResultsCsvRow {
  param(
    [Parameter(Mandatory = $true)][string]$CsvFile,
    [Parameter(Mandatory = $true)]$Row,
    [Parameter(Mandatory = $true)][datetime]$Timestamp
  )

  Ensure-RunResultsCsvSchema -CsvFile $CsvFile -Row $Row -Timestamp $Timestamp

  try {
    $csvExists = Test-Path $CsvFile
    $Row | Export-Csv -Path $CsvFile -Append:$csvExists -NoTypeInformation
    return
  } catch {
    $message = $_.Exception.Message
    if ($message -like "*CannotAppendCsvWithMismatchedPropertyNames*" -or
        $message -like "*ne possède pas de propriété correspondant à la colonne suivante*" -or
        $message -like "*Impossible d’ajouter du contenu CSV*") {
      $archiveFile = Join-Path $resultsDir ("run_results_soft_adaptatif_legacy_retry_{0}.csv" -f $Timestamp.ToString("yyyyMMdd_HHmmss"))
      if (Test-Path $CsvFile) {
        Move-Item -LiteralPath $CsvFile -Destination $archiveFile -Force
      }
      Write-Host ""
      Write-Host "CSV incompatibility detected during export."
      Write-Host "  Archived previous file : $archiveFile"
      Write-Host "  CSV reset              : $CsvFile"

      $Row | Export-Csv -Path $CsvFile -NoTypeInformation
      return
    }
    throw
  }
}

function Select-DatasetName {
  param(
    [string]$RequestedDataset
  )

  if ($RequestedDataset) {
    $selected = $datasetMenu | Where-Object { $_.Name -eq $RequestedDataset }
    if (-not $selected) {
      throw "Unknown dataset: $RequestedDataset. Available values: $($datasetMenu.Name -join ', ')"
    }
    return $selected.Name
  }

  Write-Host ""
  Write-Host "Available datasets:"
  foreach ($entry in $datasetMenu) {
    Write-Host ("  [{0}] {1}" -f $entry.Id, $entry.Name)
  }

  $choiceText = Read-Host "Choose a dataset (number)"
  $choice = 0
  if (-not [int]::TryParse($choiceText, [ref]$choice)) {
    throw "Invalid choice: enter a number."
  }

  $selected = $datasetMenu | Where-Object { $_.Id -eq $choice }
  if (-not $selected) {
    throw "Choice out of range."
  }

  return $selected.Name
}

function Get-SpecialDatasetDirectory {
  param(
    [Parameter(Mandatory = $true)][string]$Name
  )

  $candidates = switch ($Name) {
    "breast" {
      @(
        (Join-Path $dataDir "breast_11bits"),
        (Join-Path $dataDir "Sortinghat_rust_pdte\breast"),
        (Join-Path $dataDir "Sortinghat_transciphering\breast_11bits")
      )
    }
    "heart" {
      @(
        (Join-Path $dataDir "Sortinghat_rust_pdte\heart")
      )
    }
    "steel" {
      @(
        (Join-Path $dataDir "Sortinghat_rust_pdte\steel")
      )
    }
    default {
      @()
    }
  }

  foreach ($candidate in $candidates) {
    if ((Test-Path (Join-Path $candidate "model.json")) -and
        (Test-Path (Join-Path $candidate "x_test.csv")) -and
        (Test-Path (Join-Path $candidate "y_test.csv"))) {
      return (Resolve-Path $candidate).Path
    }
  }

  throw "Special dataset not found: $Name"
}

function Get-StandardDatasetMetadata {
  param(
    [Parameter(Mandatory = $true)][string]$Prefix
  )

  $trainPath = Join-Path $dataDir "${Prefix}_train.csv"
  $testPath = Join-Path $dataDir "${Prefix}_test.csv"
  if (-not (Test-Path $trainPath)) { throw "Training CSV not found: $trainPath" }
  if (-not (Test-Path $testPath)) { throw "Test CSV not found: $testPath" }

  $header = (Get-Content $trainPath -TotalCount 1).Trim()
  $featureCount = ($header -split ",").Count - 1

  $labels = @{}
  Import-Csv $trainPath | ForEach-Object { $labels[$_.label] = $true }
  Import-Csv $testPath | ForEach-Object { $labels[$_.label] = $true }

  Write-Host ""
  Write-Host "Standard dataset detected:"
  Write-Host "  Training CSV        : $trainPath"
  Write-Host "  Test CSV            : $testPath"
  Write-Host "  A fresh tree export will be generated for this dataset."

  return [PSCustomObject]@{
    Features = $featureCount
    Classes = $labels.Keys.Count
    ModelPath = "/workspace/data/tree.csv"
    LocalPrefix = "data/$Prefix"
  }
}

function Get-SpecialDatasetMetadata {
  param(
    [Parameter(Mandatory = $true)][string]$Name
  )

  $datasetDir = Get-SpecialDatasetDirectory -Name $Name
  $xTestPath = Join-Path $datasetDir "x_test.csv"
  $yTestPath = Join-Path $datasetDir "y_test.csv"
  $firstRow = (Get-Content $xTestPath -TotalCount 1).Trim()
  if ([string]::IsNullOrWhiteSpace($firstRow)) {
    throw "x_test.csv est vide pour : $datasetDir"
  }

  $featureCount = ($firstRow -split ",").Count
  $labels = Get-Content $yTestPath | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
  $classCount = @($labels | Sort-Object -Unique).Count
  $relativeDir = $datasetDir.Substring($dataDir.Length).TrimStart('\').Replace('\', '/')

  return [PSCustomObject]@{
    Features = $featureCount
    Classes = $classCount
    ModelPath = "/workspace/data/$relativeDir/model.json"
    LocalPrefix = $null
  }
}

function Get-DatasetExecutionPlan {
  param(
    [Parameter(Mandatory = $true)][string]$Name
  )

  $selected = $datasetMenu | Where-Object { $_.Name -eq $Name }
  if (-not $selected) {
    throw "Unknown dataset: $Name"
  }

  if ($selected.Type -eq "standard") {
    $meta = Get-StandardDatasetMetadata -Prefix $selected.Prefix
  } else {
    $meta = Get-SpecialDatasetMetadata -Name $selected.Name
  }

  return [PSCustomObject]@{
    Name = $selected.Name
    Type = $selected.Type
    Features = $meta.Features
    Classes = $meta.Classes
    ModelPath = $meta.ModelPath
    LocalPrefix = $meta.LocalPrefix
  }
}

function Ensure-PodmanImage {
  Write-Host ""
  Write-Host "Building container image with $ContainerEngine..."
  $buildResult = Invoke-LoggedCommand -FilePath $ContainerEngine -Arguments @(
    "build",
    "--build-arg", "SEAL_BUILD_JOBS=2",
    "-t", $ImageName,
    "-f", (Join-Path $srcDir "Containerfile"),
    $srcDir
  )

  if ($buildResult.ExitCode -ne 0) {
    throw "Container image build failed with $ContainerEngine (code $($buildResult.ExitCode))."
  }
}

function Ensure-Binaries {
  $localBuildDir = Join-Path $repoRoot $BuildDir
  $localPocClear = Join-Path $localBuildDir "poc_clear"
  $localPocHe = Join-Path $localBuildDir "poc_he"

  if ((-not $Rebuild) -and (Test-Path $localPocClear) -and (Test-Path $localPocHe)) {
    return
  }

  Write-Host ""
  Write-Host "Compilation de poc_clear et poc_he..."
  $compileResult = Invoke-LoggedCommand -FilePath $ContainerEngine -Arguments @(
    "run", "--rm",
    "-v", "${repoRoot}:/workspace",
    "-w", "/workspace/src",
    $ImageName,
    "bash", "-lc", "rm -rf $containerBuildDir && cmake -S . -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --target poc_clear --parallel && cmake --build $containerBuildDir --target poc_he --parallel 1"
  )

  if ($compileResult.ExitCode -ne 0) {
    throw "CMake/container compilation failed with $ContainerEngine (code $($compileResult.ExitCode))."
  }
}

function Train-StandardDataset {
  param(
    [Parameter(Mandatory = $true)][string]$LocalPrefix
  )

  Write-Host ""
  Write-Host "Entrainement et export de l'arbre..."
  $trainResult = Invoke-LoggedCommand -FilePath "python" -Arguments @(
    (Join-Path $srcDir "train_and_export.py"),
    "--data-prefix", $LocalPrefix,
    "--depth", "4",
    "--output", (Join-Path $dataDir "tree.csv"),
    "--json", (Join-Path $dataDir "tree.json")
  )

  if ($trainResult.ExitCode -ne 0) {
    throw "train_and_export.py failed (code $($trainResult.ExitCode))."
  }
}

function Run-ClearInference {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetName,
    [Parameter(Mandatory = $true)][string]$DetailedResultsFile,
    [Parameter(Mandatory = $true)][string]$ModelPath,
    [Parameter(Mandatory = $true)][int]$Features,
    [Parameter(Mandatory = $true)][int]$Classes,
    [Parameter(Mandatory = $true)][int]$SampleCount
  )

  Write-Host ""
  Write-Host "===== poc_clear results ====="
  $result = Invoke-LoggedCommand -FilePath $ContainerEngine -Arguments @(
    "run", "--rm",
    "-e", "POC_RESULTS_DIR=/workspace/results",
    "-e", "POC_DATASET_NAME=$DatasetName",
    "-e", "POC_CLEAR_RESULTS_FILE=$DetailedResultsFile",
    "-v", "${repoRoot}:/workspace",
    "-w", "/workspace/src",
    $ImageName,
    "bash", "-lc", "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerPocClear $ModelPath $Features $Classes $SampleCount"
  )

  if ($result.ExitCode -ne 0) {
    throw "poc_clear failed (code $($result.ExitCode))."
  }

  return $result.Text
}

function Run-HeInference {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetName,
    [Parameter(Mandatory = $true)][string]$DetailedResultsFile,
    [Parameter(Mandatory = $true)][string]$ModelPath,
    [Parameter(Mandatory = $true)][int]$Features,
    [Parameter(Mandatory = $true)][int]$Classes,
    [Parameter(Mandatory = $true)][int]$SampleCount
  )

  Write-Host ""
  Write-Host "===== poc_he results ====="
  $result = Invoke-LoggedCommand -FilePath $ContainerEngine -Arguments @(
    "run", "--rm",
    "-e", "POC_RESULTS_DIR=/workspace/results",
    "-e", "POC_DATASET_NAME=$DatasetName",
    "-e", "POC_HE_RESULTS_FILE=$DetailedResultsFile",
    "-v", "${repoRoot}:/workspace",
    "-w", "/workspace/src",
    $ImageName,
    "bash", "-lc", "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerPocHe $ModelPath $Features $Classes $SampleCount"
  )

  if ($result.ExitCode -ne 0) {
    throw "poc_he failed (code $($result.ExitCode))."
  }

  return $result.Text
}

function Print-FinalSummary {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetName,
    [Parameter(Mandatory = $true)][string]$ClearText,
    [Parameter(Mandatory = $true)][string]$HeText,
    [string]$ClearDetailsFile,
    [string]$HeDetailsFile
  )

  $metrics = Get-RunMetrics -DatasetName $DatasetName -ClearText $ClearText -HeText $HeText -ClearDetailsFile $ClearDetailsFile -HeDetailsFile $HeDetailsFile

  Write-Host ""
  Write-Host "================ Resume final ================"
  Write-Host "Dataset                    : $DatasetName"
  Write-Host "Hard inference (clear)     : $(if ($metrics.clear_hard_correct_count -and $metrics.clear_hard_accuracy_pct -ne $null) { "$($metrics.clear_hard_correct_count) - $($metrics.clear_hard_accuracy_pct)%" } else { 'n/a' })"
  Write-Host "Soft global inference      : $(if ($metrics.clear_soft_global_correct_count -and $metrics.clear_soft_global_accuracy_pct -ne $null) { "$($metrics.clear_soft_global_correct_count) - $($metrics.clear_soft_global_accuracy_pct)%" } else { 'n/a' })"
  Write-Host "Soft adaptive inference    : $(if ($metrics.clear_soft_adaptive_correct_count -and $metrics.clear_soft_adaptive_accuracy_pct -ne $null) { "$($metrics.clear_soft_adaptive_correct_count) - $($metrics.clear_soft_adaptive_accuracy_pct)%" } else { 'n/a' })"
  Write-Host "HE soft global inference   : $(if ($metrics.he_soft_global_correct_count -and $metrics.he_soft_global_accuracy_pct -ne $null) { "$($metrics.he_soft_global_correct_count) - $($metrics.he_soft_global_accuracy_pct)%" } elseif ($metrics.he_soft_global_status -eq 'failed') { "failed - $($metrics.he_soft_global_error)" } elseif ($metrics.he_output_detected) { 'unparsed output' } else { 'n/a (legacy output or rebuild missing)' })"
  Write-Host "HE soft adaptive inference : $(if ($metrics.he_soft_adaptive_correct_count -and $metrics.he_soft_adaptive_accuracy_pct -ne $null) { "$($metrics.he_soft_adaptive_correct_count) - $($metrics.he_soft_adaptive_accuracy_pct)%" } elseif ($metrics.he_soft_adaptive_status -eq 'failed') { "failed - $($metrics.he_soft_adaptive_error)" } elseif ($metrics.he_output_detected) { 'unparsed output' } else { 'n/a (legacy output or rebuild missing)' })"

  return $metrics
}

function Get-RunMetrics {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetName,
    [Parameter(Mandatory = $true)][string]$ClearText,
    [Parameter(Mandatory = $true)][string]$HeText,
    [string]$ClearDetailsFile,
    [string]$HeDetailsFile
  )

  $hardClearCount = Get-RegexValue -Text $ClearText -Pattern "Hard \((?:clear|clair)\)\s*:\s*([0-9]+/[0-9]+)"
  $hardClear = Get-RegexValue -Text $ClearText -Pattern "Hard \((?:clear|clair)\)\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $softGlobalClearCount = Get-RegexValue -Text $ClearText -Pattern "Soft global \((?:clear|clair)\)\s*:\s*([0-9]+/[0-9]+)"
  $softGlobalClear = Get-RegexValue -Text $ClearText -Pattern "Soft global \((?:clear|clair)\)\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $softClearCount = Get-RegexValue -Text $ClearText -Pattern "Soft (?:adaptive|adaptatif) \((?:clear|clair)\)\s*:\s*([0-9]+/[0-9]+)"
  $softClear = Get-RegexValue -Text $ClearText -Pattern "Soft (?:adaptive|adaptatif) \((?:clear|clair)\)\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $softHeGlobalCount = Get-RegexValue -Text $HeText -Pattern "HE Soft global\s*:\s*([0-9]+/[0-9]+)"
  $softHeGlobal = Get-RegexValue -Text $HeText -Pattern "HE Soft global\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $softHeGlobalAvgMs = Get-RegexValue -Text $HeText -Pattern "HE Soft global\s*:\s*[0-9]+/[0-9]+\s*-\s*[0-9]+(?:\.[0-9]+)?%\s*([0-9]+(?:\.[0-9]+)?)\s*ms/inf"
  $softHeAdaptiveCount = Get-RegexValue -Text $HeText -Pattern "HE Soft (?:adaptive|adaptatif)\s*:\s*([0-9]+/[0-9]+)"
  $softHeAdaptive = Get-RegexValue -Text $HeText -Pattern "HE Soft (?:adaptive|adaptatif)\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $softHeAdaptiveAvgMs = Get-RegexValue -Text $HeText -Pattern "HE Soft (?:adaptive|adaptatif)\s*:\s*[0-9]+/[0-9]+\s*-\s*[0-9]+(?:\.[0-9]+)?%\s*([0-9]+(?:\.[0-9]+)?)\s*ms/inf"
  $softHeGlobalFailed = [bool]($HeText -match "HE Soft global\s*:\s*failed")
  $softHeAdaptiveFailed = [bool]($HeText -match "HE Soft (?:adaptive|adaptatif)\s*:\s*failed")
  $heOutputDetected = [bool]($HeText -match "(?:Results - encrypted inference|Resultats - inference chiffree) \(CKKS/SEAL\)")
  $globalError = $null
  $adaptiveError = $null
  if ($HeText -match "HE Soft global\s*:\s*failed\s*\r?\n\s*Reason\s*:\s*(.+)") {
    $globalError = $matches[1].Trim()
  }
  if ($HeText -match "HE Soft (?:adaptive|adaptatif)\s*:\s*failed\s*\r?\n\s*Reason\s*:\s*(.+)") {
    $adaptiveError = $matches[1].Trim()
  }
  $clearSamples = Get-RegexValue -Text $ClearText -Pattern "Samples\s*:\s*([0-9]+)"
  $heSamples = Get-RegexValue -Text $HeText -Pattern "Samples\s*:\s*([0-9]+)"
  $heMultDepth = Get-RegexValue -Text $HeText -Pattern "Mult\.\s*depth\s*:\s*([0-9]+)"

  $clearHardCsvMetrics = Get-PredictionMetricsFromCsv -Path $ClearDetailsFile -PredictionColumns @("pred_hard")
  $clearSoftGlobalCsvMetrics = Get-PredictionMetricsFromCsv -Path $ClearDetailsFile -PredictionColumns @("pred_soft_global")
  $clearSoftAdaptiveCsvMetrics = Get-PredictionMetricsFromCsv -Path $ClearDetailsFile -PredictionColumns @("pred_soft_adaptive", "pred_soft_adaptatif")
  $heSoftGlobalCsvMetrics = Get-PredictionMetricsFromCsv -Path $HeDetailsFile -PredictionColumns @("pred_soft_global_ckks")
  $heSoftAdaptiveCsvMetrics = Get-PredictionMetricsFromCsv -Path $HeDetailsFile -PredictionColumns @("pred_soft_adaptive_ckks", "pred_soft_adaptatif_ckks")

  if (-not $hardClearCount -and $clearHardCsvMetrics) {
    $hardClearCount = $clearHardCsvMetrics.CorrectCount
  }
  if ($hardClear -eq $null -and $clearHardCsvMetrics) {
    $hardClear = [string]$clearHardCsvMetrics.AccuracyPct
  }
  if (-not $softGlobalClearCount -and $clearSoftGlobalCsvMetrics) {
    $softGlobalClearCount = $clearSoftGlobalCsvMetrics.CorrectCount
  }
  if ($softGlobalClear -eq $null -and $clearSoftGlobalCsvMetrics) {
    $softGlobalClear = [string]$clearSoftGlobalCsvMetrics.AccuracyPct
  }
  if (-not $softClearCount -and $clearSoftAdaptiveCsvMetrics) {
    $softClearCount = $clearSoftAdaptiveCsvMetrics.CorrectCount
  }
  if ($softClear -eq $null -and $clearSoftAdaptiveCsvMetrics) {
    $softClear = [string]$clearSoftAdaptiveCsvMetrics.AccuracyPct
  }
  if (-not $softHeGlobalCount -and $heSoftGlobalCsvMetrics) {
    $softHeGlobalCount = $heSoftGlobalCsvMetrics.CorrectCount
  }
  if ($softHeGlobal -eq $null -and $heSoftGlobalCsvMetrics) {
    $softHeGlobal = [string]$heSoftGlobalCsvMetrics.AccuracyPct
  }
  if (-not $softHeAdaptiveCount -and $heSoftAdaptiveCsvMetrics) {
    $softHeAdaptiveCount = $heSoftAdaptiveCsvMetrics.CorrectCount
  }
  if ($softHeAdaptive -eq $null -and $heSoftAdaptiveCsvMetrics) {
    $softHeAdaptive = [string]$heSoftAdaptiveCsvMetrics.AccuracyPct
  }
  if (-not $clearSamples -and $clearHardCsvMetrics) {
    $clearSamples = [string]$clearHardCsvMetrics.SampleCount
  }
  if (-not $heSamples -and $heSoftGlobalCsvMetrics) {
    $heSamples = [string]$heSoftGlobalCsvMetrics.SampleCount
  }

  return [PSCustomObject]@{
    dataset = $DatasetName
    clear_samples = Convert-ToNullableInt $clearSamples
    clear_hard_correct_count = $hardClearCount
    clear_hard_accuracy_pct = Convert-ToNullableDouble $hardClear
    clear_soft_global_correct_count = $softGlobalClearCount
    clear_soft_global_accuracy_pct = Convert-ToNullableDouble $softGlobalClear
    clear_soft_adaptive_correct_count = $softClearCount
    clear_soft_adaptive_accuracy_pct = Convert-ToNullableDouble $softClear
    he_soft_global_correct_count = $softHeGlobalCount
    he_soft_global_accuracy_pct = Convert-ToNullableDouble $softHeGlobal
    he_soft_global_avg_ms = Convert-ToNullableDouble $softHeGlobalAvgMs
    he_soft_global_status = $(if ($softHeGlobalCount) { 'ok' } elseif ($softHeGlobalFailed) { 'failed' } else { 'missing' })
    he_soft_global_error = $globalError
    he_soft_adaptive_correct_count = $softHeAdaptiveCount
    he_soft_adaptive_accuracy_pct = Convert-ToNullableDouble $softHeAdaptive
    he_soft_adaptive_avg_ms = Convert-ToNullableDouble $softHeAdaptiveAvgMs
    he_soft_adaptive_status = $(if ($softHeAdaptiveCount) { 'ok' } elseif ($softHeAdaptiveFailed) { 'failed' } else { 'missing' })
    he_soft_adaptive_error = $adaptiveError
    he_output_detected = $heOutputDetected
    he_samples = Convert-ToNullableInt $heSamples
    he_mult_depth = Convert-ToNullableInt $heMultDepth
  }
}

function Save-RunArtifacts {
  param(
    [Parameter(Mandatory = $true)][datetime]$Timestamp,
    [Parameter(Mandatory = $true)][string]$ClearDetailsFile,
    [Parameter(Mandatory = $true)][string]$HeDetailsFile,
    [Parameter(Mandatory = $true)][string]$DatasetName,
    [Parameter(Mandatory = $true)][int]$Features,
    [Parameter(Mandatory = $true)][int]$Classes,
    [Parameter(Mandatory = $true)][int]$SampleCount,
    [Parameter(Mandatory = $true)]$Metrics,
    [Parameter(Mandatory = $true)][string]$ClearText,
    [Parameter(Mandatory = $true)][string]$HeText
  )

  Ensure-ResultsDirectories

  $timestamp = $Timestamp
  $timestampText = $timestamp.ToString("yyyy-MM-ddTHH:mm:ss")
  $timestampFile = $timestamp.ToString("yyyyMMdd_HHmmss")
  $logFile = Join-Path $logsDir ("run_{0}_{1}.log" -f $DatasetName, $timestampFile)
  $csvFile = Join-Path $resultsDir "run_results_soft_adaptatif.csv"

  $logContent = @"
timestamp: $timestampText
dataset: $DatasetName
features: $Features
classes: $Classes
samples_requested: $SampleCount

===== poc_clear results =====
$ClearText

===== poc_he results =====
$HeText

clear_predictions_file: $clearDetailsFile
he_predictions_file: $heDetailsFile
"@
  Set-Content -Path $logFile -Value $logContent

  $row = [PSCustomObject]@{
    timestamp = $timestampText
    dataset = $DatasetName
    features = $Features
    classes = $Classes
    samples_requested = $SampleCount
    clear_samples = $Metrics.clear_samples
    clear_hard_correct = $Metrics.clear_hard_correct_count
    clear_hard_accuracy_pct = $Metrics.clear_hard_accuracy_pct
    clear_soft_global_correct = $Metrics.clear_soft_global_correct_count
    clear_soft_global_accuracy_pct = $Metrics.clear_soft_global_accuracy_pct
    clear_soft_adaptive_correct = $Metrics.clear_soft_adaptive_correct_count
    clear_soft_adaptive_accuracy_pct = $Metrics.clear_soft_adaptive_accuracy_pct
    he_soft_global_correct = $Metrics.he_soft_global_correct_count
    he_soft_global_accuracy_pct = $Metrics.he_soft_global_accuracy_pct
    he_soft_global_avg_ms = $Metrics.he_soft_global_avg_ms
    he_soft_adaptive_correct = $Metrics.he_soft_adaptive_correct_count
    he_soft_adaptive_accuracy_pct = $Metrics.he_soft_adaptive_accuracy_pct
    he_soft_adaptive_avg_ms = $Metrics.he_soft_adaptive_avg_ms
    he_samples = $Metrics.he_samples
    he_mult_depth = $Metrics.he_mult_depth
    clear_predictions_file = $clearDetailsFile
    he_predictions_file = $heDetailsFile
    log_file = $logFile
  }

  Write-RunResultsCsvRow -CsvFile $csvFile -Row $row -Timestamp $timestamp

  Write-Host ""
  Write-Host "Saved results:"
  Write-Host "  CSV : $csvFile"
  Write-Host "  Log : $logFile"
}

$selectedDataset = Select-DatasetName -RequestedDataset $Dataset
$plan = Get-DatasetExecutionPlan -Name $selectedDataset

Write-Host ""
Write-Host "Selected dataset    : $($plan.Name)"
Write-Host "Type                : $($plan.Type)"
Write-Host "Features            : $($plan.Features)"
Write-Host "Classes             : $($plan.Classes)"
Write-Host "Evaluated samples   : $SampleCount"

Ensure-PodmanImage
Ensure-Binaries
Ensure-ResultsDirectories

if ($plan.Type -eq "standard" -and $plan.LocalPrefix) {
  Train-StandardDataset -LocalPrefix $plan.LocalPrefix
}

$runTimestamp = Get-Date
$timestampFile = $runTimestamp.ToString("yyyyMMdd_HHmmss")
$clearDetailedResultsFile = "/workspace/results/logs/run_{0}_{1}_clear_predictions.csv" -f $plan.Name, $timestampFile
$heDetailedResultsFile = "/workspace/results/logs/run_{0}_{1}_he_predictions.csv" -f $plan.Name, $timestampFile
$clearText = Run-ClearInference -DatasetName $plan.Name -DetailedResultsFile $clearDetailedResultsFile -ModelPath $plan.ModelPath -Features $plan.Features -Classes $plan.Classes -SampleCount $SampleCount
$heText = Run-HeInference -DatasetName $plan.Name -DetailedResultsFile $heDetailedResultsFile -ModelPath $plan.ModelPath -Features $plan.Features -Classes $plan.Classes -SampleCount $SampleCount

$metrics = Print-FinalSummary -DatasetName $plan.Name -ClearText $clearText -HeText $heText -ClearDetailsFile ("results/logs/run_{0}_{1}_clear_predictions.csv" -f $plan.Name, $timestampFile) -HeDetailsFile ("results/logs/run_{0}_{1}_he_predictions.csv" -f $plan.Name, $timestampFile)
Save-RunArtifacts -Timestamp $runTimestamp -ClearDetailsFile ("results/logs/run_{0}_{1}_clear_predictions.csv" -f $plan.Name, $timestampFile) -HeDetailsFile ("results/logs/run_{0}_{1}_he_predictions.csv" -f $plan.Name, $timestampFile) -DatasetName $plan.Name -Features $plan.Features -Classes $plan.Classes -SampleCount $SampleCount -Metrics $metrics -ClearText $clearText -HeText $heText
