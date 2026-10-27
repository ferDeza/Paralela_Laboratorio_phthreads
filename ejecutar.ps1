param(
    [ValidateRange(1, 2147483647)]
    [int]$Threads = 4,
    [ValidateRange(1, 2147483647)]
    [int]$PiSteps = 30000000,
    [ValidateRange(1, 2147483647)]
    [int]$MatrixSize = 1500,
    [ValidateRange(1, 2147483647)]
    [int]$ListOperations = 1000,
    [ValidateRange(1, 2147483647)]
    [int]$BarrierRounds = 1000
)

$ErrorActionPreference = 'Stop'
$compiler = 'C:\msys64\ucrt64\bin\gcc.exe'
$compilerDirectory = Split-Path -Parent $compiler
$msysDirectory = 'C:\msys64\usr\bin'

if (-not (Test-Path -LiteralPath $compiler)) {
    throw "No se encontro GCC en $compiler. Instala GCC en MSYS2 UCRT64."
}

$env:PATH = "$compilerDirectory;$msysDirectory;$env:PATH"

$activities = @(
    'actividad_1',
    'actividad_2',
    'actividad_3',
    'actividad_4'
)

foreach ($activity in $activities) {
    $source = Join-Path $PSScriptRoot "$activity.c"
    $executable = Join-Path $PSScriptRoot "$activity.exe"

    Write-Host "`nCompilando $activity.c ..."
    & $compiler -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror `
        $source -o $executable -pthread

    if ($LASTEXITCODE -ne 0) {
        throw "La compilacion de $activity.c fallo (codigo $LASTEXITCODE)."
    }
}

$runs = @(
    @{ Name = 'Actividad 1: calculo de pi'; Executable = 'actividad_1.exe'; Arguments = @("$Threads", "$PiSteps") },
    @{ Name = 'Actividad 2: matriz-vector'; Executable = 'actividad_2.exe'; Arguments = @("$Threads", "$MatrixSize") },
    @{ Name = 'Actividad 3: lista enlazada sincronizada'; Executable = 'actividad_3.exe'; Arguments = @("$Threads", "$ListOperations") },
    @{ Name = 'Actividad 4: barreras'; Executable = 'actividad_4.exe'; Arguments = @("$Threads", "$BarrierRounds") }
)

foreach ($run in $runs) {
    $executable = Join-Path $PSScriptRoot $run.Executable
    Write-Host "`n========== $($run.Name) =========="
    Write-Host "Comando: .\$($run.Executable) $($run.Arguments -join ' ')"
    & $executable @($run.Arguments)

    if ($LASTEXITCODE -ne 0) {
        throw "$($run.Name) termino con codigo $LASTEXITCODE."
    }
}

Write-Host "`nEjecucion completa. Los cuatro .exe quedaron junto a los archivos .c."
