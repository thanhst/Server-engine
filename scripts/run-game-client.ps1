[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Tcp', 'Udp')]
    [string]$Protocol,

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [switch]$CheckOnly,

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Command
)

# This launcher only runs an existing build. It never builds the project,
# creates certificates, changes the trust store or starts a hidden server.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$outputDirectory = Join-Path $repoRoot "out\build\vs2022-x64-dll\$Configuration"
$client = Join-Path $outputDirectory 'ServerEngineGameClient.exe'
$certificate = Join-Path $repoRoot 'certs\server-cert.pem'

$requiredFiles = @($client)
if ($Protocol -eq 'Tcp') {
    $requiredFiles += $certificate
}

$missingFiles = @($requiredFiles | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) })
if ($missingFiles.Count -gt 0) {
    Write-Host 'CHUA CHAY CLIENT: con thieu cac file sau:' -ForegroundColor Yellow
    foreach ($missingFile in $missingFiles) { Write-Host "  $missingFile" }
    Write-Host ''
    Write-Host 'Neu thieu EXE: build preset vs2022-x64-dll truoc.'
    Write-Host 'Neu thieu PEM cho TCP: tao cap certificate/key theo huong dan de client verify server.'
    Write-Host ('Huong dan tung buoc: ' + (Join-Path $repoRoot 'docs\run-examples.md'))
    exit 2
}

if ($CheckOnly) {
    Write-Host 'Du file de thu chay client. Chua kiem tra server, certificate, port hoac runtime.'
    exit 0
}

$commands = @()
if ($Command -and $Command.Count -gt 0) {
    $commands = @($Command -join ' ')
}

if ($Protocol -eq 'Tcp') {
    $clientArgs = @('--tcp', '--host', 'localhost', '--port', '9443', '--trust', $certificate, '--') + $commands
} else {
    $clientArgs = @('--udp', '--host', '127.0.0.1', '--port', '9001', '--') + $commands
}

Push-Location -LiteralPath $repoRoot
try {
    & $client @clientArgs
    $clientExitCode = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $clientExitCode
