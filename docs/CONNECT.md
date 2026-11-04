# LUMNA Connect (dia a dia)

O PC conversa com o celular por WebSocket (porta 47800, só rede local e Tailscale),
no mesmo protocolo do app **LUMNA Connect** do Android e do **LUMNA OS Mobile**.
Quem fala com o celular é o `lumna-controle` (serviço que não morre junto com a sessão).

## O que dá para fazer
- **Parear por código** (Painel > Celular Connect > Parear): código de uso único com validade; depois o celular guarda um token.
- **Área de transferência** compartilhada nos dois sentidos (`wl-paste --watch` / `wl-copy`), com proteção contra eco.
- **Avisos**: os do celular aparecem no PC (`notify-send`) e os do PC vão para o celular (`servicos/avisos.c` repassa ao controle).
- **Arquivos**: o celular manda (em pedaços de 192 KiB, gravando `.part` e renomeando; nome sanitizado, nunca sobrescreve: `a (2).bin`); o PC oferece e o celular puxa.
- **Enviar para o celular** na captura de tela (menu Enviar > "Enviar para NOME").
- Cada recurso liga/desliga por celular (`notifications`, `phone_notifications`, `clipboard`, `files`, `control`) e o Connect inteiro tem chave geral. Tudo em `~/.config/lumna/connect.json` e `remotes.json`.

## Comandos do socket (`lumna-controle`)
`connect`, `parear`, `connect_ativo sim|nao`, `connect_pasta CAMINHO`, `recurso ID NOME sim|nao`, `enviar ID CAMINHO`, `copiar_para ID`, `aviso JSON`.

## Pacotes
`wl-clipboard`, `libnotify` (notify-send).

## Testes
`sh testes/connect.sh` (celular falso, sem tocar no sistema).
Verificado só com fakes + Orbit headless: celular real, app Android, mDNS e D-Bus real ainda precisam ser testados na máquina do Eric.
