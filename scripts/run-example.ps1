[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Web', 'Game')]
    [string]$Example,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$CheckOnly
)

# This launcher only runs an existing build. It never builds the project,
# creates certificates, changes the trust store or starts a hidden server.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out\build\vs2022-x64-dll\$Configuration"
$executable = Join-Path $outputDirectory "ServerEngine${Example}Server.exe"
$certificate = Join-Path $repoRoot 'certs\server-cert.pem'
$privateKey = Join-Path $repoRoot 'certs\server-key.pem'
$clientFile = Join-Path $repoRoot 'examples\WebClient\index.html'

$requiredFiles = @($executable, (Join-Path $outputDirectory 'ServerEngine.dll'), $certificate, $privateKey)
if ($Example -eq 'Web') {
    $requiredFiles += Join-Path $repoRoot 'examples\WebMediaClient\index.html'
    $requiredFiles += Join-Path $repoRoot 'examples\WebMediaClient\app.js'
} else {
    $requiredFiles += $clientFile
}

$missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missingFiles.Count -gt 0) {
    Write-Host 'CHUA CHAY SERVER: con thieu cac file sau:' -ForegroundColor Yellow
    foreach ($missingFile in $missingFiles) { Write-Host "  $missingFile" }
    Write-Host ''
    Write-Host 'Neu thieu EXE/DLL: build preset vs2022-x64-dll truoc.'
    Write-Host 'Neu thieu PEM: tao cap certificate/key theo huong dan de server khoi dong.'
    Write-Host 'Browser tin cay chung chi la buoc rieng khi ket noi HTTPS/WSS.'
    Write-Host ('Huong dan tung buoc: ' + (Join-Path $repoRoot 'docs\run-examples.md'))
    exit 2
}

if ($CheckOnly) {
    Write-Host 'Du file de thu chay. Chua kiem tra ABI, certificate, port hoac runtime.'
    exit 0
}

Write-Host "Dang mo $Example server ($Configuration)..."
Write-Host 'Giu cua so nay mo. Nhan Ctrl+C de dung server.'
if ($Example -eq 'Web') {
    Write-Host 'Khi server bao san sang, mo browser: https://localhost:9553/' -ForegroundColor Cyan
    Write-Host 'Bam nut doc profile de thu HTTPS -> SQLite -> JSON.'
} else {
    Write-Host 'Khi server bao san sang, mo file nay bang browser:' -ForegroundColor Cyan
    Write-Host "  $clientFile"
    Write-Host 'Endpoint: wss://localhost:9444/game. Ket noi, gui PING, cho PONG.'
}
Write-Host 'Browser phai tin cay chung chi dev. Xem docs/security.md.'
Write-Host ''

# Keep the foreground process and its logs visible. Resolve database/assets
# from the repository root even when this script is called from another folder.
$exampleExitCode = 1
Push-Location -LiteralPath $repoRoot
try {
    & $executable $certificate $privateKey
    $exampleExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $exampleExitCode
