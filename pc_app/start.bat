@echo off
REM Arranque rapido do servico Desk Companion (duplo-clique).
REM Encaminha quaisquer argumentos para o start.ps1 (que por sua vez os passa
REM ao desk_companion.py). Sem argumentos -> corre o servico ('run').
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start.ps1" %*
endlocal
