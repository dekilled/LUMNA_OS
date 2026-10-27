# Emma no LUMNA OS: camada de automação (plano)

A Emma já existe no projeto **NewHarnessV2**: backend FastAPI em `127.0.0.1:8001`, LLM por qualquer
endpoint compatível com OpenAI (Ollama Cloud por padrão, Claude, Gemini, OpenAI, Ollama local),
**chamada de ferramentas** (`tools.py`, `REGISTRY`), voz (Fish Audio ou local: faster-whisper +
voz clonada da Emma), palavra de ativação "Oi Emma" (Vosk), lembretes e lâmpadas Yeelight.

O LUMNA não refaz a Emma: ele vira **as mãos dela no computador**. O backend pensa e conversa; o
shell do LUMNA executa, com uma camada de automação própria que também funciona sem a Emma
(atalhos, `lumna-ctl`, gatilhos).

## 1. Como as peças se ligam

```
 voz / painel da Emma (Super + A, botão do dock, "Oi Emma")
            │  SSE: POST /api/sessions/{sid}/chat
            ▼
 ┌────────────────────────┐   tool_client {id, name, args}   ┌──────────────────────────────┐
 │ backend da Emma (8001) │ ───────────────────────────────▶ │ lumna-shell: ponte da Emma    │
 │ LLM + tools + memória  │ ◀─────────────────────────────── │  └ lumna-actions (catálogo,   │
 └────────────────────────┘   POST /tool-result              │     macros, gatilhos, log)    │
                                                              └──────────────┬───────────────┘
                                   lumna-ctl / atalhos / gatilhos ───────────┘
```

- O backend já tem o padrão certo: ferramentas "do cliente" (`where="android"` hoje). A ponte
  adiciona `where="lumna"`: o backend oferece as ferramentas do LUMNA ao modelo, manda
  `tool_client` quando o modelo chama uma, e o shell executa e devolve com `/tool-result`.
- Confirmações (`tool_confirm`) aparecem na tela do LUMNA, no estilo da tela de senha.
- O shell manda o catálogo de ações para o backend a cada mensagem (`lumna_tools` no
  `POST /chat`), então ações novas (e as macros da pessoa) viram ferramentas da Emma sem mexer no
  backend. Quem decide a permissão é o LUMNA; as ações não dependem do modo Full do servidor.

## 2. O catálogo de ações

Fica no shell (`shell/src-tauri/src/actions.rs`), que já sabe abrir apps, jogos, vídeos, trocar
modo e falar com o Wi-Fi e a Loja. O `lumna-ctl` usa o mesmo catálogo pelo socket do shell:

```sh
lumna-ctl actions                                   # lista
lumna-ctl action apps.open '{"name":"firefox"}'     # roda
```

Cada ação tem nome, descrição para o modelo, parâmetros (JSON Schema), **risco** (`read`, `act`,
`danger`) e permissão (`auto`, `ask`, `off`), editável no painel da Emma (⚙). O que é `danger`
nunca roda sem perguntar. Para a Emma, `apps.open` vira a ferramenta `lumna_apps_open`.

**Feito (primeira leva):** `apps.list/open/close/focus`, `windows.list/move/fullscreen`,
`mode.current/switch`, `panel.open`, `audio.volume/outputs/output`, `media.control/now_playing/
open_service/play_video`, `games.list/launch`, `files.open/search/recent`, `wifi.status/connect`,
`notify`, `clipboard.get/set`, `screenshot`, `power`, `store.search/install`,
`service.list/control`, `shell.run` (desligada por padrão).

Plano original (o que falta: `mode.layout`, `media.seek`, `bluetooth.connect`, `power.lock` com tela de bloqueio própria, `store.remove`, serviços do sistema):

| Grupo | Ações (primeira leva) |
|---|---|
| **Apps** | `apps.list`, `apps.open` (qualquer `.desktop`, inclusive Flatpak e AUR, por nome aproximado), `apps.close`, `apps.focus` |
| **Janelas e modos** | `windows.list`, `windows.move` (workspace/monitor), `windows.fullscreen`, `mode.switch` (Desktop/Gaming/Mídia), `mode.layout` |
| **Mídia e som** | `media.play_pause/next/prev/seek` (playerctl), `audio.volume`, `audio.mute`, `audio.output` (trocar fone/caixa) |
| **Jogos e Mídia** | `games.list`, `games.launch`, `media.open_service` (Netflix, YouTube…), `media.play_video` |
| **Arquivos** | `files.open` (pasta ou arquivo), `files.search` (por nome), `files.recent` |
| **Sistema** | `wifi.connect/disconnect`, `bluetooth.connect` (aparelho pareado), `notify`, `clipboard.get/set`, `screenshot`, `power.lock/suspend/shutdown` |
| **Loja** | `store.search`, `store.install`/`store.remove` (sempre pedem confirmação + senha) |
| **Serviços** | `service.status/start/stop/restart` de serviços **do usuário** (`systemctl --user`); do sistema só com confirmação e polkit |
| **Comando** | `shell.run`: só em modo avançado, sempre pergunta, mostra o comando inteiro antes |
| **Macros** | `macro.list`, `macro.run`, e cada macro salva vira uma ferramenta `macro.<nome>` |

## 3. Macros

Arquivos em `~/.config/lumna/macros/<nome>.json` (a Emma pode criar e editar; a pessoa também,
por uma tela no Painel). Uma macro é uma lista de passos com ações do catálogo:

```json
{
  "name": "Hora do filme",
  "description": "Mídia na TV, luz baixa e Netflix",
  "steps": [
    { "action": "mode.switch", "args": { "mode": "media" } },
    { "action": "audio.output", "args": { "match": "HDMI" } },
    { "action": "audio.volume", "args": { "percent": 40 } },
    { "emma": "deixa as luzes da sala em 20% e amareladas" },
    { "action": "media.open_service", "args": { "id": "netflix" } }
  ]
}
```

- Passos `action` rodam direto no LUMNA, sem LLM (rápido e previsível).
- Passos `emma` mandam um pedido em linguagem natural para a Emma (para o que só ela sabe:
  lâmpadas, lembretes, pesquisa).
- `wait` (segundos ou "até a janela X abrir") e `if` simples (modo atual, rede, aparelho
  conectado) ficam para a segunda versão.

Feito em `shell/src-tauri/src/automation.rs`. Pedida pela própria Emma (`macro.run` numa conversa),
os passos `emma` voltam para ela fazer em seguida; pedida por gatilho ou pelo painel, eles viram
uma mensagem para a Emma. Uma macro não roda outra macro, e ações desligadas no painel param a
macro com erro.

## 4. Gatilhos ("serviços" de automação)

Um executor no shell (como o agendador das lâmpadas no backend, mas local) liga **gatilho →
macro**:

| Gatilho | Exemplo |
|---|---|
| Horário (cron) | "Dias úteis 8h: abrir Firefox e o e-mail" |
| Login / desbloqueio | "Ao entrar: Wi-Fi de casa, volume 30%" |
| Modo trocado | "Entrou no Gaming: fone como saída, avisos em silêncio" |
| Aparelho conectado | "Fone Bluetooth conectou: virar a saída de som" |
| Rede conectada | "Wi-Fi do trabalho: abrir o Slack" |
| App aberto/fechado | "Steam fechou: voltar ao Desktop" |
| Ocioso | "20 min parado: bloquear" |
| Voz | "Oi Emma, hora do filme" → `macro.hora-do-filme` |

Arquivo `~/.config/lumna/automations.json`, por exemplo:

```json
[{ "id": "cinema", "name": "Cinema", "enabled": true,
   "when": { "type": "mode", "mode": "media" }, "macro": "hora-do-filme" },
 { "id": "manha", "name": "Manhã", "enabled": true,
   "when": { "type": "time", "at": "08:00", "days": [1, 2, 3, 4, 5] }, "macro": "trabalho" }]
```

`when.type`: `time`, `login`, `mode`, `app_opened`, `app_closed`, `wifi`, `bluetooth`. Tela no
painel da Emma (⚡): macros com ▶, automações liga/desliga e as últimas execuções
(`~/.local/state/lumna/automations.log`).

## 4.1 O pacote `lumna-emma`

- O backend (só os `.py` e as dependências do `requirements.txt`, sem venv) vai para
  `/usr/lib/lumna-emma`; o serviço `lumna-emma.service` do systemd do usuário vem ligado para todos (`systemctl --global enable` na instalação do pacote).
- Na primeira vez, `/usr/lib/lumna/lumna-emma` cria `~/.config/lumna/emma/backend.env` (modo 600)
  com segredos aleatórios e uma conta local (`$USER@lumna.local` + senha aleatória). O painel lê
  esse arquivo e entra sozinho; dados em `~/.local/share/lumna-emma`.
- **O servidor da Emma é o motor** (skills, memória, RAG e as conversas dele). O LUMNA entra nele sozinho, sem tela de login: com a **chave local** que o próprio
  backend cria ao ligar em `~/.config/lumna/emma/chave` (modo 600; `LUMNA_KEY_FILE` muda o lugar, `off` desliga), ou com a conta local abaixo.
- **Reserva sem servidor:** se o servidor estiver desligado, a Emma conversa **direto com o provedor de IA** (Ollama, OpenAI, Gemini, Claude ou qualquer API compatível com
  OpenAI) com a URL, a chave e o modelo de Painel > Emma > Provedores e Roteamento (`~/.config/lumna/emma/provedores.json`, modo 600). Sem skills, memória nem RAG; as conversas
  da reserva ficam em `~/.local/share/lumna/emma` (`servicos/emmal.c` e `emmas.c`). Quando o servidor volta, ele volta a ser o motor.
- O backend escuta só em `127.0.0.1:8001`. Endpoints locais (Ollama no próprio PC) são liberados
  (`ALLOW_PRIVATE_URLS=true`).
- O código vem do NewHarnessV2 (privado): `EMMA_SRC` no build local, `EMMA_REPO_TOKEN` no CI. Sem
  ele, o ISO sai sem o pacote e o painel pede o endereço de um servidor da Emma.

## 5. Segurança

- Ações `ler` rodam sozinhas; `agir` seguem a permissão da pessoa; `perigo` (desligar, apagar,
  instalar, comando) **sempre** perguntam, e as de administrador ainda pedem a senha (polkit).
- Conteúdo de fora (páginas, e-mails, arquivos) que entrar na conversa trava as ações `agir`
  até a pessoa confirmar (o backend já faz isso).
- Tudo que a Emma executa vai para um histórico (Painel → Emma → Atividade), com quem pediu.
- O token do backend fica no chaveiro do usuário (`~/.config/lumna/emma.json`, modo 600).

## 6. Fases

1. ✅ **Ponte**: login no backend, painel da Emma (conversa por texto; a voz é a fase 5) no shell, `where="lumna"` no backend,
   `tool_client` → executar → `/tool-result`, confirmações na tela do LUMNA.
2. ✅ **Catálogo** com as ações da tabela (começando por apps, modos, mídia, som, janelas, arquivos).
3. ✅ **Macros** (arquivo, `lumna-ctl action macro.run`, ações `macro.*` para a Emma, tela ⚡ no painel).
4. ✅ **Gatilhos** (horário, login, modo, Bluetooth, Wi-Fi, app aberto/fechado) + histórico.
   Falta: ocioso (precisa do `ext-idle-notify` no shell).
5. **"Oi Emma"** no LUMNA (long poll do `/wake/wait`) e voz no painel.
6. ✅ **Backend da Emma no ISO** como serviço do usuário (`systemctl --user`), com o primeiro boot
   ("conta da Emma", provedor de LLM).


## 6. A Emma no desktop novo (Orbit + Gravity): `lumna-emma`

No desktop novo (C, sem Tauri) a Emma é um **serviço da sessão**, `lumna-emma` (`gravity/exemplos/emma.c`), que fica
ligado o tempo todo. Ele faz a ponte entre as pessoas e o backend da porta 8001 (o mesmo protocolo do shell antigo:
login, sessões, SSE, `tool_client`, `tool-result`, `/api/stt`, `/api/tts`; o `~/.config/lumna/emma.json` é o mesmo,
então conta, conversas e permissões valem nos dois):

- **Quem pede** (socket `$XDG_RUNTIME_DIR/lumna-emma.sock`, ver `servicos/emmac.h`): o ícone da topbar (estado e
  microfone), o controle do celular, `lumna-emma <pedido>` na linha de comando e, nos próximos passos, o chat lateral e
  o painel completo.
- **O ícone da topbar é o processo**, não o chat: mostra que a Emma está rodando (e o que faz: ouvindo, pensando, falando) e o menu
  tem Abrir (o app completo), Configurações (Painel → Emma), Reiniciar, Fechar e, com ela parada, Iniciar. Só roda um serviço por sessão.
- **Comandos curtos sem a Emma** (`servicos/emmaa.c`, `emmaa_intencao`): "pausa", "continua", "próxima", "avança 30
  segundos", "volume 40", "aumenta o volume", "mudo", "abre a Netflix", "início", "modo mídia", "troca as telas"...
  Rápidos e sem internet. O resto vai para a Emma.
- **Ações da Emma** (mesmo esquema das permissões antigas: `auto`, `ask`, `off`): Mídia (`midia.estado/abrir/fechar/inicio/
  seletor/controle/navegar/digitar`), `audio.volume`, `mode.atual/trocar/trocar_telas`, `apps.listar/abrir`, `notificar`.
  O catálogo antigo (arquivos, Wi-Fi, loja, serviços...) entra nas próximas levas.
- **Painel flutuante** (`lumna-emma-chat`, `gravity/exemplos/emma-chat.c`): o chat rápido à direita, como no desenho (foto e estado,
  balões com hora, botões de próximo passo `[acao:]`/`[link:]`/`[sugestao:]`, cartão "Abrir configuração" quando a Emma usa
  `painel.abrir`, contexto da janela em foco, Nova conversa, microfone, fixar e "Abrir app completo"). O **botão da dock** (o microfone) e o
  **Super+A** abrem e fecham (o "Oi Emma", quando existir, chama pelo mesmo caminho); **Super+Shift+A** liga o microfone. Pedido rápido ("pausa") fecha sozinho se não estiver fixado.
  As imagens da Emma ficam em `gravity/assets/emma/` (`avatar.jpg`, `fundo.jpg`); `LUMNA_EMMA_ASSETS` muda a pasta.
- **Voz**: microfone do sistema (`parec`/`arecord`, 16 kHz) com detecção do fim da fala, `/api/stt`, e a resposta falada por
  `/api/tts` frase a frase. O atalho do microfone (`emma_voz` no `orbit.ini`) liga; apertar de novo encerra a fala e transcreve.
- **Celular**: o app LUMNA Controle tem "Falar com a Emma" (reconhecimento de voz do próprio Android) e "Microfone da TV".
  O `lumna-controle` recebe `{"type":"voice","text":"..."}`, passa para a Emma e devolve `{"type":"voice_reply",...}`.

Testes: `gravity/testes/emma.sh` (backend, Mídia e comandos de mentira).
