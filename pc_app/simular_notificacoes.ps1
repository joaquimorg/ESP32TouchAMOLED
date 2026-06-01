param(
    [string]$AppName = "App Simulada",
    [string]$Titulo = "Nova notificação",
    [string]$Mensagem = "Esta é uma notificação de teste enviada para o Windows.",
    [string]$IconPath = "$PSScriptRoot\icon.png"
)

if (-not (Get-Module -ListAvailable -Name BurntToast)) {
    Write-Host "A instalar módulo BurntToast..."
    Install-Module BurntToast -Scope CurrentUser -Force
}

Import-Module BurntToast

if (-not (Test-Path $IconPath)) {
    Write-Warning "Ícone não encontrado: $IconPath"
    Write-Warning "A notificação será enviada sem ícone."

    New-BurntToastNotification `
        -Text $Titulo, $Mensagem `
        -Header (New-BTHeader -Id "simulacao" -Title $AppName)
}
else {
    New-BurntToastNotification `
        -AppLogo $IconPath `
        -Text $Titulo, $Mensagem `
        -Header (New-BTHeader -Id "simulacao" -Title $AppName)
}


#powershell -ExecutionPolicy Bypass -File .\simular_notificacoes.ps1

#powershell -ExecutionPolicy Bypass -File .\simular_notificacoes.ps1 `
#  -AppName "Sistema de Faturação" `
#  -Titulo "Documento processado" `
#  -Mensagem "A fatura FT 2026/123 foi enviada com sucesso."