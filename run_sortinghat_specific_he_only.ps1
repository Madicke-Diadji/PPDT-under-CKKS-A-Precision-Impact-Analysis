param(
  [Parameter(Mandatory = $true)][string]$Dataset,
  [string]$ImageName = "localhost/poc-hbdt-seal:latest",
  [string]$BuildDir = "build-podman-soft-adaptatif-he",
  [switch]$Rebuild
)

$ErrorActionPreference = "Stop"

function Ensure-Directory {
  param([Parameter(Mandatory = $true)][string]$Path)
  if (-not (Test-Path $Path)) {
    $null = New-Item -ItemType Directory -Path $Path -Force
  }
}

function Assert-RequiredFile {
  param([Parameter(Mandatory = $true)][string]$Path)
  if (-not (Test-Path $Path)) {
    throw "Required file not found: $Path"
  }
}

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

function Get-RelativeDatasetKey {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetDir,
    [Parameter(Mandatory = $true)][string]$DataRoot
  )

  $normalizedRoot = ((Resolve-Path $DataRoot).Path.TrimEnd('\')) + '\'
  $normalizedDir = (Resolve-Path $DatasetDir).Path

  $rootUri = New-Object System.Uri($normalizedRoot)
  $dirUri = New-Object System.Uri($normalizedDir)
  $relative = $rootUri.MakeRelativeUri($dirUri).ToString()
  return ($relative -replace "\\", "/")
}

function Get-SafeDatasetName {
  param([Parameter(Mandatory = $true)][string]$DatasetKey)
  return (($DatasetKey -replace "[\\/]+", "_") -replace "[^A-Za-z0-9_.-]", "_")
}

function Get-CanonicalSoftAdaptatifDatasetName {
  param([Parameter(Mandatory = $true)][string]$DatasetName)

  $normalized = ($DatasetName -replace "\\", "/").Trim("/")
  switch -Regex ($normalized) {
    "^breast(_11bits)?$" { return "breast" }
    "^Sortinghat_rust_pdte/breast$" { return "breast" }
    "^Sortinghat_transciphering/breast_11bits$" { return "breast" }
    "^heart$" { return "heart" }
    "^Sortinghat_rust_pdte/heart$" { return "heart" }
    "^steel$" { return "steel" }
    "^Sortinghat_rust_pdte/steel$" { return "steel" }
    default { return $null }
  }
}

function Get-DisplayDatasetKey {
  param(
    [Parameter(Mandatory = $true)][string]$DatasetDir,
    [Parameter(Mandatory = $true)][string]$DataRoot
  )

  $relativeKey = Get-RelativeDatasetKey -DatasetDir $DatasetDir -DataRoot $DataRoot
  $canonicalName = Get-CanonicalSoftAdaptatifDatasetName -DatasetName $relativeKey
  if ($canonicalName) {
    return $canonicalName
  }

  return $relativeKey
}

function Get-DatasetMetadata {
  param([Parameter(Mandatory = $true)][string]$DatasetDir)

  $xTestPath = Join-Path $DatasetDir "x_test.csv"
  $yTestPath = Join-Path $DatasetDir "y_test.csv"
  Assert-RequiredFile -Path $xTestPath
  Assert-RequiredFile -Path $yTestPath

  $firstRow = (Get-Content $xTestPath -TotalCount 1).Trim()
  if ([string]::IsNullOrWhiteSpace($firstRow)) {
    throw "x_test.csv est vide pour : $DatasetDir"
  }

  $featureCount = ($firstRow -split ",").Count
  $labels = Get-Content $yTestPath | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne "" }
  $classCount = @($labels | Sort-Object -Unique).Count
  $sampleCount = @($labels).Count

  return [PSCustomObject]@{
    Features = $featureCount
    Classes = $classCount
    Samples = $sampleCount
  }
}

function Resolve-DatasetTargets {
  param(
    [Parameter(Mandatory = $true)][string]$InputName,
    [Parameter(Mandatory = $true)][string]$DataRoot
  )

  $candidatePaths = [System.Collections.Generic.List[string]]::new()
  $canonicalName = Get-CanonicalSoftAdaptatifDatasetName -DatasetName $InputName

  if ($canonicalName -eq "breast") {
    $candidatePaths.Add((Join-Path $DataRoot "breast_11bits"))
    $candidatePaths.Add((Join-Path $DataRoot "Sortinghat_rust_pdte/breast"))
    $candidatePaths.Add((Join-Path $DataRoot "Sortinghat_transciphering/breast_11bits"))
  } elseif ($canonicalName -eq "heart") {
    $candidatePaths.Add((Join-Path $DataRoot "Sortinghat_rust_pdte/heart"))
  } elseif ($canonicalName -eq "steel") {
    $candidatePaths.Add((Join-Path $DataRoot "Sortinghat_rust_pdte/steel"))
  }

  $candidatePaths.Add((Join-Path $DataRoot $InputName))
  $candidatePaths.Add((Join-Path $DataRoot "${InputName}_11bits"))
  $candidatePaths.Add((Join-Path $DataRoot "${InputName}_10bits"))
  $candidatePaths.Add((Join-Path $DataRoot "${InputName}_16bits"))

  $resolvedPath = $null
  foreach ($candidate in $candidatePaths) {
    if (Test-Path $candidate) {
      $resolvedPath = (Resolve-Path $candidate).Path
      break
    }
  }

  if (-not $resolvedPath) {
    $allCandidates = Get-ChildItem $DataRoot -Recurse -Directory | Where-Object {
      (Test-Path (Join-Path $_.FullName "model.json")) -and
      (Test-Path (Join-Path $_.FullName "x_test.csv")) -and
      (Test-Path (Join-Path $_.FullName "y_test.csv"))
    } | Select-Object -ExpandProperty FullName

    $available = $allCandidates | ForEach-Object { Get-DisplayDatasetKey -DatasetDir $_ -DataRoot $DataRoot } | Sort-Object -Unique
    throw "Dataset not found for '$InputName'. Available datasets: $($available -join ', ')"
  }

  $directModel = Join-Path $resolvedPath "model.json"
  $directX = Join-Path $resolvedPath "x_test.csv"
  $directY = Join-Path $resolvedPath "y_test.csv"
  if ((Test-Path $directModel) -and (Test-Path $directX) -and (Test-Path $directY)) {
    return @($resolvedPath)
  }

  $children = Get-ChildItem $resolvedPath -Directory | Where-Object {
    (Test-Path (Join-Path $_.FullName "model.json")) -and
    (Test-Path (Join-Path $_.FullName "x_test.csv")) -and
    (Test-Path (Join-Path $_.FullName "y_test.csv"))
  } | Sort-Object FullName | Select-Object -ExpandProperty FullName

  if (-not $children) {
    throw "No usable dataset found in: $resolvedPath"
  }

  return @($children)
}

function Parse-HeMetrics {
  param(
    [Parameter(Mandatory = $true)][hashtable]$RunData,
    [Parameter(Mandatory = $true)][string]$HeText
  )

  $RunData.he_batch_precision_pct = Get-RegexValue -Text $HeText -Pattern "HE Soft adaptive\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  if ($HeText -match "([0-9]+)\s+errors out of\s+([0-9]+)") {
    $RunData.he_batch_errors = $matches[1]
    $RunData.he_batch_tests = $matches[2]
  }

  $RunData.he_client_encrypt_ms = Get-RegexValue -Text $HeText -Pattern "Client encryption time\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $RunData.he_server_inference_ms = Get-RegexValue -Text $HeText -Pattern "Server inference time\s*:?\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $RunData.he_client_decrypt_ms = Get-RegexValue -Text $HeText -Pattern "Client decryption time\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $RunData.he_setup_time_ms = Get-RegexValue -Text $HeText -Pattern "setup_time_ms=([0-9]+(?:\.[0-9]+)?)"
  $RunData.he_inference_time_ms = Get-RegexValue -Text $HeText -Pattern "inference_time_ms=([0-9]+(?:\.[0-9]+)?)"
  $RunData.he_inference_time_per_sample_ms = Get-RegexValue -Text $HeText -Pattern "inference_time_per_sample_ms=([0-9]+(?:\.[0-9]+)?)"

  if ($RunData.he_batch_precision_pct) {
    $RunData.he_status = "ok"
    $RunData.he_batch_status = "ok"
  } else {
    $RunData.he_status = "failed"
    $RunData.he_batch_status = "failed"
  }
}

function Save-RunResults {
  param(
    [Parameter(Mandatory = $true)][hashtable]$RunData,
    [Parameter(Mandatory = $true)][string]$CsvPath
  )

  $row = [PSCustomObject]@{
    timestamp = $RunData.timestamp
    dataset = $RunData.dataset
    features = $RunData.features
    classes = $RunData.classes
    samples = $RunData.samples
    build_status = $RunData.build_status
    he_status = $RunData.he_status
    he_batch_status = $RunData.he_batch_status
    he_batch_precision_pct = $RunData.he_batch_precision_pct
    he_batch_errors = $RunData.he_batch_errors
    he_batch_tests = $RunData.he_batch_tests
    he_client_encrypt_ms = $RunData.he_client_encrypt_ms
    he_server_inference_ms = $RunData.he_server_inference_ms
    he_client_decrypt_ms = $RunData.he_client_decrypt_ms
    he_setup_time_ms = $RunData.he_setup_time_ms
    he_inference_time_ms = $RunData.he_inference_time_ms
    he_inference_time_per_sample_ms = $RunData.he_inference_time_per_sample_ms
    error_message = $RunData.error_message
    log_file = $RunData.log_file
  }

  if (Test-Path $CsvPath) {
    $row | Export-Csv -Path $CsvPath -NoTypeInformation -Append -Encoding UTF8
  } else {
    $row | Export-Csv -Path $CsvPath -NoTypeInformation -Encoding UTF8
  }
}

$repoRoot = (Resolve-Path $PSScriptRoot).Path
$dataRoot = Join-Path $repoRoot "data"
$resultsDir = Join-Path $repoRoot "results"
$logsDir = Join-Path $resultsDir "logs"
$csvResultsPath = Join-Path $resultsDir "run_results_soft_adaptatif.csv"
$containerBuildDir = "/workspace/$BuildDir"

Ensure-Directory -Path $resultsDir
Ensure-Directory -Path $logsDir

$datasetDirs = Resolve-DatasetTargets -InputName $Dataset -DataRoot $dataRoot

Write-Host ""
Write-Host "Datasets to run:"
foreach ($datasetDir in $datasetDirs) {
  Write-Host ("  - {0}" -f (Get-DisplayDatasetKey -DatasetDir $datasetDir -DataRoot $dataRoot))
}
Write-Host "Image Podman        : $ImageName"
Write-Host "Build dir           : $BuildDir"

if ($Rebuild -or -not (Test-Path (Join-Path $repoRoot $BuildDir))) {
  Write-Host ""
  Write-Host "Building soft-adaptive poc_he inside the container..."
  $buildArgs = @("run", "--rm") + @(
    "-v", "${repoRoot}:/workspace",
    "-w", "/workspace/src",
    $ImageName,
    "bash", "-lc", "rm -rf $containerBuildDir && cmake -S . -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --target poc_he --parallel 1"
  )
  $buildResult = Invoke-LoggedCommand -FilePath "podman" -Arguments $buildArgs

  if ($buildResult.ExitCode -ne 0) {
    throw "poc_he build failed (code $($buildResult.ExitCode))."
  }
}

$runSummaries = New-Object System.Collections.Generic.List[object]

foreach ($datasetDir in $datasetDirs) {
  $datasetKey = Get-DisplayDatasetKey -DatasetDir $datasetDir -DataRoot $dataRoot
  $safeDatasetName = Get-SafeDatasetName -DatasetKey $datasetKey
  $modelPath = Join-Path $datasetDir "model.json"
  $xTestPath = Join-Path $datasetDir "x_test.csv"
  $yTestPath = Join-Path $datasetDir "y_test.csv"

  Assert-RequiredFile -Path $modelPath
  Assert-RequiredFile -Path $xTestPath
  Assert-RequiredFile -Path $yTestPath

  $metadata = Get-DatasetMetadata -DatasetDir $datasetDir
  $timestamp = Get-Date
  $timestampIso = $timestamp.ToString("s")
  $timestampFile = $timestamp.ToString("yyyyMMdd_HHmmss")
  $logFilePath = Join-Path $logsDir ("run_{0}_{1}.log" -f $safeDatasetName, $timestampFile)
  $containerModelPath = "/workspace/data/$((Get-RelativeDatasetKey -DatasetDir $datasetDir -DataRoot $dataRoot))/model.json"

  $runData = @{
    timestamp = $timestampIso
    dataset = $datasetKey
    features = $metadata.Features
    classes = $metadata.Classes
    samples = $metadata.Samples
    build_status = "ok"
    he_status = "pending"
    he_batch_status = "pending"
    he_batch_precision_pct = $null
    he_batch_errors = $null
    he_batch_tests = $null
    he_client_encrypt_ms = $null
    he_server_inference_ms = $null
    he_client_decrypt_ms = $null
    he_setup_time_ms = $null
    he_inference_time_ms = $null
    he_inference_time_per_sample_ms = $null
    error_message = $null
    log_file = $logFilePath
  }

  $logLines = New-Object System.Collections.Generic.List[string]
  $logLines.Add("Run timestamp: $timestampIso")
  $logLines.Add("Dataset: $datasetKey")
  $logLines.Add("Model: $modelPath")
  $logLines.Add("Features: $($metadata.Features)")
  $logLines.Add("Classes: $($metadata.Classes)")
  $logLines.Add("Samples: $($metadata.Samples)")

  Write-Host ""
  Write-Host "Running HE inference for: $datasetKey"

  try {
    $heArgs = @("run", "--rm") + @(
      "-v", "${repoRoot}:/workspace",
      "-w", "/workspace/src",
      $ImageName,
      "bash", "-lc", "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he $containerModelPath"
    )
    $heResult = Invoke-LoggedCommand -FilePath "podman" -Arguments $heArgs

    $logLines.Add("===== poc_he results =====")
    $heResult.Lines | ForEach-Object { $logLines.Add($_) }

    if ($heResult.ExitCode -ne 0) {
      throw "poc_he failed (code $($heResult.ExitCode))."
    }

    Parse-HeMetrics -RunData $runData -HeText $heResult.Text
    if ($runData.he_status -ne "ok") {
      $runData.error_message = "Expected metrics were not detected in the poc_he output."
    }
  } catch {
    $runData.he_status = "failed"
    $runData.he_batch_status = "failed"
    $runData.error_message = $_.Exception.Message
    Write-Host "Run failed: $($runData.error_message)"
  } finally {
    Set-Content -Path $logFilePath -Value $logLines -Encoding UTF8
    Save-RunResults -RunData $runData -CsvPath $csvResultsPath
    [void]$runSummaries.Add([PSCustomObject]$runData)
    Write-Host "Log saved: $logFilePath"
  }
}

Write-Host ""
Write-Host "Run summary:"
foreach ($summary in $runSummaries) {
  Write-Host ("  - {0} | status={1} | accuracy={2}" -f `
    $summary.dataset, `
    $summary.he_status, `
    $(if ($summary.he_batch_precision_pct) { "$($summary.he_batch_precision_pct)%" } else { "n/a" }))
}
Write-Host "Results CSV: $csvResultsPath"
