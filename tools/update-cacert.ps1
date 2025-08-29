<#
.SYNOPSIS
  Fetch and install the latest cacert.pem bundle.

.DESCRIPTION
  - Downloads cacert.pem and its .sha256 from curl.se.
  - Verifies the SHA256 digest.
  - Backs up the existing file with a timestamp suffix.
  - Installs to third_party/cacert/cacert.pem by default.

.PARAMETER OutDir
  Destination directory. Defaults to project_root/third_party/cacert.

.PARAMETER Url
  Source URL for cacert.pem.

.PARAMETER Sha256Url
  Source URL for cacert.pem.sha256.

.PARAMETER Force
  Overwrite even if the target looks up to date.

.PARAMETER Quiet
  Suppress informational output.

#>

[CmdletBinding()]
param(
  [string]$OutDir = (Join-Path (Resolve-Path "$PSScriptRoot\..").Path "third_party\cacert"),
  [string]$Url = "https://curl.se/ca/cacert.pem",
  [string]$Sha256Url = "https://curl.se/ca/cacert.pem.sha256",
  [switch]$Force,
  [switch]$Quiet
)

$ErrorActionPreference = "Stop"

function Write-Info($msg) {
  if (-not $Quiet) { Write-Host $msg }
}

function Get-TempFile([string]$ext) {
  $name = [System.IO.Path]::GetRandomFileName()
  if ($ext) {
    $name = [System.IO.Path]::ChangeExtension($name, $ext.TrimStart("."))
  }
  return (Join-Path $env:TEMP $name)
}

function Extract-FirstSha256([string]$text) {
  # Pull the first 64 hex chars which represent a SHA256 digest
  $hex = ($text -replace "[^0-9a-fA-F]", "")
  if ($hex.Length -lt 64) {
    throw "Could not find a 64-hex SHA256 string in the .sha256 file"
  }
  return $hex.Substring(0, 64).ToLowerInvariant()
}

# Ensure TLS 1.2
try {
  [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
} catch {
  # Ignore if not supported
}

# Prepare paths
$null = New-Item -ItemType Directory -Force -Path $OutDir
$destPem = Join-Path $OutDir "cacert.pem"
$tmpPem = Get-TempFile -ext "pem"
$tmpSha = Get-TempFile -ext "sha256"

try {
  Write-Info "Downloading: $Url"
  Invoke-WebRequest -Uri $Url -OutFile $tmpPem -UseBasicParsing

    Write-Info "Downloading: $Sha256Url"
    Invoke-WebRequest -Uri $Sha256Url -OutFile $tmpSha -UseBasicParsing

    $actual = (Get-FileHash -Algorithm SHA256 -Path $tmpPem).Hash.ToLowerInvariant()
    $expected = Extract-FirstSha256 -text (Get-Content -Raw -Path $tmpSha)

    if ($actual -ne $expected) {
      throw "SHA256 mismatch. expected=$expected actual=$actual"
    }
    Write-Info "SHA256 verified: $actual"

  if ((-not $Force) -and (Test-Path $destPem)) {
    $current = (Get-FileHash -Algorithm SHA256 -Path $destPem).Hash.ToLowerInvariant()
    if ($current -eq $actual) {
      Write-Info "Existing cacert.pem already up to date. Nothing to do."
      return
    }
  }

  if (Test-Path $destPem) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $bak = Join-Path $OutDir ("cacert.pem.bak." + $stamp)
    Copy-Item -Path $destPem -Destination $bak -Force
    Write-Info "Backed up existing cacert.pem to $bak"
  }

  Move-Item -Path $tmpPem -Destination $destPem -Force
  Write-Info "Installed new cacert.pem to $destPem"

  try {
    $head = Get-Content -Path $destPem -TotalCount 10 -ErrorAction Stop
    $line = $head | Where-Object { $_ -match "Certificate data from Mozilla as of" } | Select-Object -First 1
    if ($line) {
      Write-Info ("Bundle info: " + $line.Trim())
    }
  } catch {
  }

} finally {
  foreach ($f in @($tmpPem, $tmpSha)) {
    if ($f -and (Test-Path $f)) { Remove-Item -Force $f -ErrorAction SilentlyContinue }
  }
}
