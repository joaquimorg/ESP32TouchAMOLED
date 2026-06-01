# Ícones por app (mapeamento por nome)

Coloca aqui PNGs para forçar o ícone de notificações de uma app específica.
Isto **tem prioridade** sobre o logótipo que o Windows fornece (útil quando o
Windows atribui o mesmo logótipo a várias notificações — ex.: tudo via "Ligação
ao telemóvel"/Phone Link, ou o mesmo logótipo para apps diferentes).

## Como funciona

- O nome do ficheiro (sem extensão) é reduzido a um *slug* (minúsculas, só
  letras/dígitos). Ex.: `Microsoft Teams.png` → `microsoftteams`.
- Uma notificação usa este ícone se o *slug* aparecer **no nome da app emissora
  ou no título**. Ex.: `Instagram.png` cobre títulos/apps que contenham
  "instagram" (mesmo "Instagram: nova mensagem").
- Se nenhum ficheiro corresponder, usa-se o logótipo do Windows; e se esse
  faltar, o `../notification.png`.
- Em caso de vários nomes compatíveis, ganha o *slug* mais longo (mais específico).

## Formato

- PNG (com transparência, se quiseres — é composto sobre fundo preto).
- Qualquer tamanho; é redimensionado para `--icon-size` (px, máx. 96) e
  convertido para RGB565 ao enviar.

## Exemplos de nomes

```
Instagram.png
WhatsApp.png
Microsoft Outlook.png
Slack.png
Discord.png
```
