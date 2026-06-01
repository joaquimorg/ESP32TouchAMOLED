<#
.SYNOPSIS
    Arranque rapido do servico Desk Companion (cliente BLE para o PC).

.DESCRIPTION
    - Garante que o ambiente virtual (.venv) existe (cria-o se faltar).
    - Garante que as dependencias (requirements.txt) estao instaladas.
    - Arranca o servico: liga ao dispositivo guardado, religa se cair e
      reencaminha as notificacoes do Windows.

    Por omissao corre o comando 'run'. Qualquer argumento extra e passado
    diretamente ao desk_companion.py.

.EXAMPLE
    .\start.ps1
        Arranca o servico (equivale a: python desk_companion.py run).

.EXAMPLE
    .\start.ps1 pair
        Corre o emparelhamento em vez do servico.

.EXAMPLE
    .\start.ps1 run --icon-size 96 --retry 15
        Passa opcoes ao servico.
#>
[CmdletBinding()]
param(
    # Argumentos passados ao desk_companion.py. Sem argumentos -> "run".
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$Forward
)

$ErrorActionPreference = 'Stop'

# Trabalha sempre a partir da pasta deste script (pc_app/).
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $Root

$VenvDir    = Join-Path $Root '.venv'
$VenvPython = Join-Path $VenvDir 'Scripts\python.exe'
$ReqFile    = Join-Path $Root 'requirements.txt'
$Stamp      = Join-Path $VenvDir '.deps-installed'  # marcador de deps instaladas

function Write-Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

# 1) Garante o ambiente virtual.
if (-not (Test-Path $VenvPython)) {
    Write-Step 'Ambiente virtual nao encontrado. A criar .venv ...'
    $py = (Get-Command py -ErrorAction SilentlyContinue) ?? (Get-Command python -ErrorAction SilentlyContinue)
    if (-not $py) {
        throw "Python nao encontrado no PATH. Instala o Python 3.10-3.13 e tenta de novo."
    }
    & $py.Source -m venv $VenvDir
    if ($LASTEXITCODE -ne 0) { throw "Falha ao criar o ambiente virtual." }
}

# 2) Garante as dependencias (reinstala se o requirements.txt mudou).
$needInstall = $true
if ((Test-Path $Stamp) -and (Test-Path $ReqFile)) {
    if ((Get-Item $Stamp).LastWriteTime -ge (Get-Item $ReqFile).LastWriteTime) {
        $needInstall = $false
    }
}
if ($needInstall) {
    Write-Step 'A instalar/atualizar dependencias (requirements.txt) ...'
    & $VenvPython -m pip install --upgrade pip | Out-Null
    & $VenvPython -m pip install -r $ReqFile
    if ($LASTEXITCODE -ne 0) { throw "Falha ao instalar dependencias." }
    New-Item -ItemType File -Path $Stamp -Force | Out-Null
    Set-Content -Path $Stamp -Value (Get-Date -Format o)
}

# 3) Arranca o desk_companion. Sem argumentos -> 'run'.
if (-not $Forward -or $Forward.Count -eq 0) { $Forward = @('run') }

Write-Step "A arrancar: desk_companion.py $($Forward -join ' ')"
Write-Host '   (Ctrl+C para terminar)' -ForegroundColor DarkGray
& $VenvPython (Join-Path $Root 'desk_companion.py') @Forward
exit $LASTEXITCODE
