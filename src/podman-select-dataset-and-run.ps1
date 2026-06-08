$ErrorActionPreference = "Stop"

$scriptDir = $PSScriptRoot
$projectRoot = Split-Path $scriptDir -Parent
$imageName = "poc-hbdt-seal"
$containerSourceDir = "/workspace/src"
$containerBuildDir = "/workspace/src/build-podman"
$containerDtClearDir = "/workspace/DT_clear"
$sealBuildJobs = 2
$dataDir = Join-Path $projectRoot "data"
$dtClearDir = Join-Path $projectRoot "DT_clear"
$resultsDir = Join-Path $projectRoot "results"
$logsDir = Join-Path $resultsDir "logs"
$csvResultsPath = Join-Path $resultsDir "run_results.csv"
$excelXmlResultsPath = Join-Path $resultsDir "run_results.xml"

function Ensure-Directory {
  param([Parameter(Mandatory = $true)][string]$Path)
  if (-not (Test-Path $Path)) {
    $null = New-Item -ItemType Directory -Path $Path -Force
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

function Get-RegexValues {
  param(
    [string]$Text,
    [string]$Pattern
  )

  $values = @()
  foreach ($m in [regex]::Matches($Text, $Pattern)) {
    $values += $m.Groups[1].Value
  }
  return $values
}

function Get-BlockErrorMessage {
  param(
    [string]$Text,
    [string]$Title
  )

  $escapedTitle = [regex]::Escape($Title)
  $pattern = "(?s)Failure for $escapedTitle\s*:\s*(.+?)(?:\r?\n\r?\n|$)"
  return Get-RegexValue -Text $Text -Pattern $pattern
}

function ConvertTo-XmlSafeString {
  param([AllowNull()][object]$Value)

  if ($null -eq $Value) {
    return ""
  }

  return [System.Security.SecurityElement]::Escape([string]$Value)
}

function Export-ResultsToExcelXml {
  param(
    [Parameter(Mandatory = $true)][object[]]$Rows,
    [Parameter(Mandatory = $true)][string]$Path
  )

  $columns = @(
    "timestamp",
    "dataset",
    "features",
    "classes",
    "train_status",
    "build_status",
    "clear_status",
    "clear_samples",
    "clear_hard_baseline_pct",
    "clear_soft_adaptive_pct",
    "he_status",
    "he_demo_status",
    "he_batch_status",
    "he_ground_truth",
    "he_prediction_hard_clear",
    "he_prediction_final",
    "he_client_encrypt_ms",
    "he_server_inference_ms",
    "he_client_decrypt_ms",
    "he_demo_total_ms",
    "he_setup_time_ms",
    "he_inference_time_ms",
    "he_inference_time_per_sample_ms",
    "he_batch_precision_pct",
    "he_batch_errors",
    "he_batch_tests",
    "error_message",
    "log_file"
  )

  $sb = New-Object System.Text.StringBuilder
  [void]$sb.AppendLine('<?xml version="1.0"?>')
  [void]$sb.AppendLine('<?mso-application progid="Excel.Sheet"?>')
  [void]$sb.AppendLine('<Workbook xmlns="urn:schemas-microsoft-com:office:spreadsheet"')
  [void]$sb.AppendLine(' xmlns:o="urn:schemas-microsoft-com:office:office"')
  [void]$sb.AppendLine(' xmlns:x="urn:schemas-microsoft-com:office:excel"')
  [void]$sb.AppendLine(' xmlns:ss="urn:schemas-microsoft-com:office:spreadsheet">')
  [void]$sb.AppendLine(' <Worksheet ss:Name="Runs">')
  [void]$sb.AppendLine('  <Table>')
  [void]$sb.AppendLine('   <Row>')
  foreach ($column in $columns) {
    [void]$sb.AppendLine("    <Cell><Data ss:Type=`"String`">$column</Data></Cell>")
  }
  [void]$sb.AppendLine('   </Row>')

  foreach ($row in $Rows) {
    [void]$sb.AppendLine('   <Row>')
    foreach ($column in $columns) {
      $value = ConvertTo-XmlSafeString $row.$column
      [void]$sb.AppendLine("    <Cell><Data ss:Type=`"String`">$value</Data></Cell>")
    }
    [void]$sb.AppendLine('   </Row>')
  }

  [void]$sb.AppendLine('  </Table>')
  [void]$sb.AppendLine(' </Worksheet>')
  [void]$sb.AppendLine('</Workbook>')

  $content = $sb.ToString()
  try {
    Set-Content -Path $Path -Value $content -Encoding UTF8
    return $Path
  } catch [System.IO.IOException] {
    $directory = Split-Path $Path -Parent
    $baseName = [System.IO.Path]::GetFileNameWithoutExtension($Path)
    $extension = [System.IO.Path]::GetExtension($Path)
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $fallbackPath = Join-Path $directory ("{0}_{1}{2}" -f $baseName, $timestamp, $extension)
    Set-Content -Path $fallbackPath -Value $content -Encoding UTF8
    Write-Host "Excel file is locked, export written to: $fallbackPath"
    return $fallbackPath
  }
}

function Save-RunResults {
  param(
    [Parameter(Mandatory = $true)][hashtable]$RunData
  )

  Ensure-Directory -Path $resultsDir
  Ensure-Directory -Path $logsDir
  $row = [PSCustomObject]@{
    timestamp = $RunData.timestamp
    dataset = $RunData.dataset
    features = $RunData.features
    classes = $RunData.classes
    train_status = $RunData.train_status
    build_status = $RunData.build_status
    clear_status = $RunData.clear_status
    clear_samples = $RunData.clear_samples
    clear_hard_baseline_pct = $RunData.clear_hard_baseline_pct
    clear_soft_adaptive_pct = $RunData.clear_soft_adaptive_pct
    he_status = $RunData.he_status
    he_demo_status = $RunData.he_demo_status
    he_batch_status = $RunData.he_batch_status
    he_ground_truth = $RunData.he_ground_truth
    he_prediction_hard_clear = $RunData.he_prediction_hard_clear
    he_prediction_final = $RunData.he_prediction_final
    he_client_encrypt_ms = $RunData.he_client_encrypt_ms
    he_server_inference_ms = $RunData.he_server_inference_ms
    he_client_decrypt_ms = $RunData.he_client_decrypt_ms
    he_demo_total_ms = $RunData.he_demo_total_ms
    he_setup_time_ms = $RunData.he_setup_time_ms
    he_inference_time_ms = $RunData.he_inference_time_ms
    he_inference_time_per_sample_ms = $RunData.he_inference_time_per_sample_ms
    he_batch_precision_pct = $RunData.he_batch_precision_pct
    he_batch_errors = $RunData.he_batch_errors
    he_batch_tests = $RunData.he_batch_tests
    error_message = $RunData.error_message
    log_file = $RunData.log_file
  }

  $csvOutputPath = $csvResultsPath
  try {
    if (Test-Path $csvResultsPath) {
      $row | Export-Csv -Path $csvResultsPath -NoTypeInformation -Append -Encoding UTF8
    } else {
      $row | Export-Csv -Path $csvResultsPath -NoTypeInformation -Encoding UTF8
    }
  } catch [System.IO.IOException] {
    $timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $csvOutputPath = Join-Path $resultsDir ("run_results_{0}.csv" -f $timestamp)
    $row | Export-Csv -Path $csvOutputPath -NoTypeInformation -Encoding UTF8
    Write-Host "CSV file is locked, export written to: $csvOutputPath"
  }

  $allRows = Import-Csv -Path $csvOutputPath
  $excelOutputPath = Export-ResultsToExcelXml -Rows $allRows -Path $excelXmlResultsPath

  return [PSCustomObject]@{
    CsvPath = $csvOutputPath
    ExcelPath = $excelOutputPath
  }
}

function Parse-RunMetrics {
  param(
    [Parameter(Mandatory = $true)][hashtable]$RunData,
    [string]$ClearText,
    [string]$HeText
  )

  $RunData.clear_samples = Get-RegexValue -Text $ClearText -Pattern "Samples\s*:\s*([0-9]+)"
  $RunData.clear_hard_baseline_pct = Get-RegexValue -Text $ClearText -Pattern "Hard \(clear\)\s*:\s*([0-9]+(?:\.[0-9]+)?)%"
  $RunData.clear_soft_adaptive_pct = Get-RegexValue -Text $ClearText -Pattern "Soft adaptive \(clear\)\s*:\s*([0-9]+(?:\.[0-9]+)?)%"

  $RunData.he_ground_truth = Get-RegexValue -Text $HeText -Pattern "Label ground-truth\s*:\s*([0-9]+)"
  $RunData.he_prediction_hard_clear = Get-RegexValue -Text $HeText -Pattern "Hard prediction \(clear\)\s*:\s*([0-9]+)"
  $RunData.he_prediction_final = Get-RegexValue -Text $HeText -Pattern "FINAL CLIENT RESULT \(after decryption\)\s*:\s*([0-9]+)"

  $encryptValues = Get-RegexValues -Text $HeText -Pattern "Client encryption time\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $serverValues = Get-RegexValues -Text $HeText -Pattern "Server inference time\s*:?\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $decryptValues = Get-RegexValues -Text $HeText -Pattern "Client decryption time\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*ms"

  if ($encryptValues.Count -gt 0) { $RunData.he_client_encrypt_ms = $encryptValues[0] }
  if ($serverValues.Count -gt 0) { $RunData.he_server_inference_ms = $serverValues[0] }
  if ($decryptValues.Count -gt 0) { $RunData.he_client_decrypt_ms = $decryptValues[0] }

  $RunData.he_demo_total_ms = Get-RegexValue -Text $HeText -Pattern "Total demo time\s*:\s*([0-9]+(?:\.[0-9]+)?)\s*ms"
  $RunData.he_setup_time_ms = Get-RegexValue -Text $HeText -Pattern "setup_time_ms=([0-9]+(?:\.[0-9]+)?)"
  $RunData.he_inference_time_ms = Get-RegexValue -Text $HeText -Pattern "inference_time_ms=([0-9]+(?:\.[0-9]+)?)"
  $RunData.he_inference_time_per_sample_ms = Get-RegexValue -Text $HeText -Pattern "inference_time_per_sample_ms=([0-9]+(?:\.[0-9]+)?)"
  $RunData.he_batch_precision_pct = Get-RegexValue -Text $HeText -Pattern "HE Soft adaptive\s*:\s*[0-9]+/[0-9]+\s*-\s*([0-9]+(?:\.[0-9]+)?)%"
  $RunData.he_batch_errors = Get-RegexValue -Text $HeText -Pattern "([0-9]+)\s+errors out of\s+([0-9]+)"
  if ($HeText -match "([0-9]+)\s+errors out of\s+([0-9]+)") {
    $RunData.he_batch_errors = $matches[1]
    $RunData.he_batch_tests = $matches[2]
  }

  $demoError = Get-BlockErrorMessage -Text $HeText -Title "DEMO HE ADAPTATIF"
  $batchError = Get-RegexValue -Text $HeText -Pattern "\[main_he\] Vertical packed batch failed:\s*(.+)"

  if ($demoError) {
    $RunData.he_demo_status = "failed"
  } elseif ($RunData.he_prediction_final) {
    $RunData.he_demo_status = "ok"
  }

  if ($batchError) {
    $RunData.he_batch_status = "failed"
  } elseif ($RunData.he_batch_precision_pct) {
    $RunData.he_batch_status = "ok"
  }

  if ($RunData.he_demo_status -eq "failed" -or $RunData.he_batch_status -eq "failed") {
    $messages = @()
    if ($demoError) { $messages += $demoError.Trim() }
    if ($batchError) { $messages += $batchError.Trim() }
    $RunData.error_message = ($messages -join " | ")
    $RunData.he_status = "failed"
  } elseif ($RunData.he_demo_status -eq "ok") {
    $RunData.he_status = "ok"
  }
}

Ensure-Directory -Path $resultsDir
Ensure-Directory -Path $logsDir
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

if (Test-Path $plainTreeJson) {
  $treePath = "$containerDtClearDir/plain_tree_$($selected.Name).json"
} elseif (Test-Path $plainTreeTxt) {
  $treePath = "$containerDtClearDir/plain_tree_$($selected.Name).txt"
} else {
  throw "Plain hard tree not found for dataset '$($selected.Name)'. Expected $plainTreeJson or $plainTreeTxt"
}

$timestamp = Get-Date
$timestampIso = $timestamp.ToString("s")
$timestampFile = $timestamp.ToString("yyyyMMdd_HHmmss")
$logFilePath = Join-Path $logsDir ("run_{0}_{1}.log" -f $selected.Name, $timestampFile)

$runData = @{
  timestamp = $timestampIso
  dataset = $selected.Name
  features = $nbFeatures
  classes = $nbClasses
  train_status = "pending"
  build_status = "pending"
  clear_status = "pending"
  clear_samples = $null
  clear_hard_baseline_pct = $null
  clear_soft_adaptive_pct = $null
  he_status = "pending"
  he_demo_status = "pending"
  he_batch_status = "pending"
  he_ground_truth = $null
  he_prediction_hard_clear = $null
  he_prediction_final = $null
  he_client_encrypt_ms = $null
  he_server_inference_ms = $null
  he_client_decrypt_ms = $null
  he_demo_total_ms = $null
  he_setup_time_ms = $null
  he_inference_time_ms = $null
  he_inference_time_per_sample_ms = $null
  he_batch_precision_pct = $null
  he_batch_errors = $null
  he_batch_tests = $null
  error_message = $null
  log_file = $logFilePath
}

$logLines = New-Object System.Collections.Generic.List[string]
$logLines.Add("Run timestamp: $timestampIso")
$logLines.Add("Dataset: $($selected.Name)")
$logLines.Add("Features: $nbFeatures")
$logLines.Add("Classes: $nbClasses")

try {
  $logLines.Add("===== plain_tree =====")
  $logLines.Add($treePath)
  $runData.train_status = "ok"

  $buildResult = Invoke-LoggedCommand -FilePath "podman" -Arguments @(
    "build",
    "--build-arg", "SEAL_BUILD_JOBS=$sealBuildJobs",
    "-t", $imageName,
    "-f", (Join-Path $scriptDir "Containerfile"),
    $scriptDir
  )
  $logLines.Add("===== podman build =====")
  $buildResult.Lines | ForEach-Object { $logLines.Add($_) }
  if ($buildResult.ExitCode -ne 0) {
    throw "Podman build failed (code $($buildResult.ExitCode))."
  }

  $compileResult = Invoke-LoggedCommand -FilePath "podman" -Arguments @(
    "run", "--rm",
    "-v", "${projectRoot}:/workspace",
    "-w", "/workspace",
    $imageName,
    "bash", "-lc", "rm -rf $containerBuildDir && cmake -S $containerSourceDir -B $containerBuildDir -DCMAKE_BUILD_TYPE=Release -DSEAL_ROOT=`$SEAL_ROOT -DSEAL_DIR=`$SEAL_DIR && cmake --build $containerBuildDir --target poc_clear --parallel && cmake --build $containerBuildDir --target poc_he --parallel 1"
  )
  $logLines.Add("===== cmake build =====")
  $compileResult.Lines | ForEach-Object { $logLines.Add($_) }
  if ($compileResult.ExitCode -ne 0) {
    throw "CMake/Podman compilation failed (code $($compileResult.ExitCode))."
  }
  $runData.build_status = "ok"

  Write-Host ""
  Write-Host "===== poc_clear results ====="
  $clearResult = Invoke-LoggedCommand -FilePath "podman" -Arguments @(
    "run", "--rm",
    "-v", "${projectRoot}:/workspace",
    "-w", "/workspace",
    $imageName,
    "bash", "-lc", "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_clear $treePath $nbFeatures $nbClasses"
  )
  $logLines.Add("===== poc_clear results =====")
  $clearResult.Lines | ForEach-Object { $logLines.Add($_) }
  if ($clearResult.ExitCode -ne 0) {
    $runData.clear_status = "failed"
    throw "poc_clear failed (code $($clearResult.ExitCode))."
  }
  $runData.clear_status = "ok"

  Write-Host ""
  Write-Host "===== poc_he results ====="
  $heResult = Invoke-LoggedCommand -FilePath "podman" -Arguments @(
    "run", "--rm",
    "-v", "${projectRoot}:/workspace",
    "-w", "/workspace",
    $imageName,
    "bash", "-lc", "export LD_LIBRARY_PATH=/opt/seal-install/lib:/opt/seal-install/lib64:`$LD_LIBRARY_PATH && $containerBuildDir/poc_he $treePath $nbFeatures $nbClasses"
  )
  $logLines.Add("===== poc_he results =====")
  $heResult.Lines | ForEach-Object { $logLines.Add($_) }
  if ($heResult.ExitCode -ne 0) {
    $runData.he_status = "failed"
    throw "poc_he failed (code $($heResult.ExitCode))."
  }

  Parse-RunMetrics -RunData $runData -ClearText $clearResult.Text -HeText $heResult.Text
} catch {
  if (-not $runData.error_message) {
    $runData.error_message = $_.Exception.Message
  } else {
    $runData.error_message = "$($runData.error_message) | $($_.Exception.Message)"
  }

  if ($runData.train_status -eq "pending") { $runData.train_status = "failed" }
  elseif ($runData.build_status -eq "pending") { $runData.build_status = "failed" }
  elseif ($runData.clear_status -eq "pending") { $runData.clear_status = "failed" }
  elseif ($runData.he_status -eq "pending") { $runData.he_status = "failed" }

  Write-Host ""
  Write-Host "Run failed: $($_.Exception.Message)"
} finally {
  Set-Content -Path $logFilePath -Value $logLines -Encoding UTF8
  $resultPaths = Save-RunResults -RunData $runData
  Write-Host ""
  Write-Host "Saved results:"
  Write-Host "  CSV   : $($resultPaths.CsvPath)"
  Write-Host "  Excel : $($resultPaths.ExcelPath)"
  Write-Host "  Log   : $logFilePath"
}
