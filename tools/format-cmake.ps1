param(
  [string]$Config = ".cmake-format.yaml",
  [string]$Root,
  [string[]]$Include = @('CMakeLists.txt','*.cmake'),
  [string[]]$SkipDirs = @('third_party','out','.git','.vs','.vscode','build'),
  [string[]]$SkipFiles = @('cmake_install.cmake','CMakeSystem.cmake','CMakeCXXCompiler.cmake','CMakeRCCompiler.cmake')
)

try {
  if ($Root) {
    $BasePath = (Resolve-Path -LiteralPath $Root).Path
  } else {
    $BasePath = (Get-Location).Path
  }
} catch {
  Write-Error "Could not resolve base path. Root='$Root'. $_"
  exit 1
}

if (-not (Test-Path -LiteralPath $Config)) {
  $cfg = Join-Path -Path $PSScriptRoot -ChildPath $Config
  if (Test-Path -LiteralPath $cfg) { $Config = $cfg }
}

$cmakeFmt = Get-Command cmake-format -ErrorAction SilentlyContinue
if (-not $cmakeFmt) {
  Write-Error "cmake-format not found. Install it or add it to PATH."
  exit 1
}

$sep = '[\\/]'
$dirRegex = if ($SkipDirs.Count) {
  '(' + (($SkipDirs | ForEach-Object { "$sep$([regex]::Escape($_))$sep" }) -join '|') + ')'
} else { $null }

$fileRegex = if ($SkipFiles.Count) {
  '^(?:' + (($SkipFiles | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')$'
} else { $null }

Write-Host "Scanning $BasePath for: $($Include -join ', ')"

$files = Get-ChildItem -Path $BasePath -Recurse -File -Include $Include | Where-Object {
  $full = $_.FullName
  $name = $_.Name
  if ($dirRegex  -and $full -imatch $dirRegex) { return $false }
  if ($fileRegex -and $name -imatch $fileRegex) { return $false }
  return $true
}

if (-not $files -or $files.Count -eq 0) {
  Write-Warning "No CMake files matched under $BasePath. Pass -Root if your repo layout is different."
  exit 0
}

$ok = 0
$fail = 0
foreach ($f in $files) {
  Write-Host "Formatting $($f.FullName)"
  try {
    & $cmakeFmt.Path -c $Config -i $f.FullName
    $ok++
  } catch {
    Write-Error "cmake-format failed on $($f.FullName): $_"
    $fail++
  }
}

Write-Host "Done. Formatted $ok file(s). Failures: $fail."
exit $fail
