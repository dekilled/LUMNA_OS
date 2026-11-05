# LUMNA Connect (dia a dia)

O PC conversa com o celular por WebSocket (porta 47800, só rede local e Tailscale),
no mesmo protocolo do app **LUMNA Connect** do Android e do **LUMNA OS Mobile**.
Quem fala com o celular é o `lumna-connect` (serviço que não morre junto com a sessão).

## O que dá para fazer
- **Parear por código** (Painel > LUMNA Connect > Parear): código de uso único com validade; depois o celular guarda um token.
- **Área de transferência** compartilhada nos dois sentidos (`wl-paste --watch` / `wl-copy`), com proteção contra eco.
- **Avisos**: os do celular aparecem no PC (`notify-send`) e os do PC vão para o celular (`servicos/avisos.c` repassa ao controle).
- **Arquivos**: o celular manda (em pedaços de 192 KiB, gravando `.part` e renomeando; nome sanitizado, nunca sobrescreve: `a (2).bin`); o PC oferece e o celular puxa.
- **Enviar para o celular** na captura de tela (menu Enviar > "Enviar para NOME").
- Cada recurso liga/desliga por celular (`notifications`, `phone_notifications`, `clipboard`, `files`, `control`) e o Connect inteiro tem chave geral. Tudo em `~/.config/lumna/connect.json` e `remotes.json`.

## Comandos do socket (`lumna-connect`)
`connect`, `parear`, `connect_ativo sim|nao`, `connect_pasta CAMINHO`, `recurso ID NOME sim|nao`, `enviar ID CAMINHO`, `copiar_para ID`, `aviso JSON`.

## Pacotes
- `wtype`: **o controle do celular** (direcional, OK, voltar, texto e o menu da Mídia) aperta teclas por ele. Sem o `wtype` o celular recebe um erro, e o resto do controle (volume, modo, Home, mídia) continua funcionando.
- `wl-clipboard` (`wl-copy` e `wl-paste`): a área de transferência.
- `libnotify` (`notify-send`): os avisos do celular no PC.
- `firefox-developer-edition`: a Mídia. O Firefox comum só liga extensões assinadas, e a nossa ainda não é; o serviço da Mídia já escolhe o Developer Edition sozinho quando ele está instalado (veja `gravity/extensao-firefox/LEIAME.md`). Se o Firefox não conectar em 25 s, a Mídia mostra um aviso e escreve o motivo no terminal.

Se algo não funcionar, rode dentro do desktop: `sh scripts/diag-connect.sh`. Ele diz o que falta e o que o serviço da Mídia e o controle estão vendo.

## Os apps do celular
- **LUMNA Connect** (`remote/android`): só o controle remoto (direcional, texto, teclas, mídia, volume, modo, Home e voz). Não tem arquivos nem área de transferência.
- **LUMNA Connect** (o app antigo do celular): é o que fala arquivos, área de transferência e avisos. A parte do PC foi reescrita aqui seguindo o protocolo antigo; ainda não foi testada com o app de verdade.

## Testes
`sh testes/connect.sh` (celular falso, sem tocar no sistema).
Verificado só com fakes + Orbit headless: celular real, app Android, mDNS e D-Bus real ainda precisam ser testados na máquina do Eric.
