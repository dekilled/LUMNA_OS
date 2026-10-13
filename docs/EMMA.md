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

Arquivo `~/.config/lumna/automations.json`; tela no Painel → Emma → Automações (liga/desliga,
última execução, histórico).

## 5. Segurança

- Ações `ler` rodam sozinhas; `agir` seguem a permissão da pessoa; `perigo` (desligar, apagar,
  instalar, comando) **sempre** perguntam, e as de administrador ainda pedem a senha (polkit).
- Conteúdo de fora (páginas, e-mails, arquivos) que entrar na conversa trava as ações `agir`
  até a pessoa confirmar (o backend já faz isso).
- Tudo que a Emma executa vai para um histórico (Painel → Emma → Atividade), com quem pediu.
- O token do backend fica no chaveiro do usuário (`~/.config/lumna/emma.json`, modo 600).

## 6. Fases

1. ✅ **Ponte**: login no backend, painel da Emma (chat + voz) no shell, `where="lumna"` no backend,
   `tool_client` → executar → `/tool-result`, confirmações na tela do LUMNA.
2. ✅ **Catálogo** com as ações da tabela (começando por apps, modos, mídia, som, janelas, arquivos).
3. **Macros** (arquivo, `lumna-ctl macro run`, ferramenta `macro.*` para a Emma, tela simples).
4. **Gatilhos** (horário, login, modo, aparelho, rede, app, ocioso) + histórico.
5. **"Oi Emma"** no LUMNA (long poll do `/wake/wait`) e voz no painel.
6. **Backend da Emma no ISO** como serviço do usuário (`systemctl --user`), com o primeiro boot
   ("conta da Emma", provedor de LLM).
