param(
    [string]$Port = "COM5",
    [switch]$CompileOnly
)

$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$cliPath = "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
$srcMain = Join-Path $repoRoot "controller_uno\src\main.cpp"
$sketchDir = Join-Path $repoRoot "controller_uno_cli\eweed_uno"
$sketchFile = Join-Path $sketchDir "eweed_uno.ino"
$buildDir = Join-Path $repoRoot "controller_uno_cli\build"

if (-not (Test-Path -LiteralPath $cliPath)) {
    throw "No se encontro arduino-cli en: $cliPath"
}

if (-not (Test-Path -LiteralPath $srcMain)) {
    throw "No se encontro firmware fuente UNO en: $srcMain"
}

New-Item -ItemType Directory -Force -Path $sketchDir | Out-Null
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Copy-Item -LiteralPath $srcMain -Destination $sketchFile -Force
Write-Host "[UNO] Fuente sincronizada: $srcMain -> $sketchFile"

& $cliPath compile --fqbn arduino:renesas_uno:minima --build-path $buildDir $sketchDir
if ($LASTEXITCODE -ne 0) {
    throw "[UNO] Compilacion fallida (codigo $LASTEXITCODE)."
}
Write-Host "[UNO] Compilacion OK"

if (-not $CompileOnly) {
    & $cliPath upload -p $Port --fqbn arduino:renesas_uno:minima --input-dir $buildDir $sketchDir
    if ($LASTEXITCODE -ne 0) {
        throw "[UNO] Upload fallido en $Port (codigo $LASTEXITCODE)."
    }
    Write-Host "[UNO] Upload OK en $Port"
}
