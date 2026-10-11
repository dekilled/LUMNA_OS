# LUMNA OS: análise de viabilidade

> Seu espaço. Seus modos. Sua Emma.

Um ambiente Linux com três modos (**Desktop**, **Gaming** e **Mídia**), troca por `Super + Tab`
e suporte a vários monitores (um modo por tela, ou um modo estendido em todas). A interface é
feita em **Vue.js** e o lado nativo em **Rust via Tauri**.

**Veredito curto: dá para fazer, desde que o LUMNA OS seja um _shell_ em cima de um compositor
Wayland que já existe, e não um compositor escrito do zero.** O Tauri desenha a barra, o dock,
os launchers e as telas de Gaming e Mídia. Quem posiciona, anima e desenha as janelas dos apps é o
compositor. Escrever o compositor próprio fica para depois, se um dia valer a pena.

---

## 1. O ponto que decide tudo: Tauri não é gerenciador de janelas

Um Desktop Environment tem duas camadas:

| Camada | O que faz | Quem faz no LUMNA OS |
|---|---|---|
| **Compositor / gerenciador de janelas** | Recebe as janelas dos apps (Firefox, Steam, jogos), posiciona, anima, desenha na tela, controla teclado, mouse e monitores | Compositor Wayland existente (Hyprland no MVP) |
| **Shell** | Topbar, dock, launcher, notificações, troca de modo, wallpaper, telas cheias de Gaming/Mídia | **Tauri + Vue** |

O Tauri cria janelas comuns (GTK + WebKitGTK no Linux). Ele não recebe as janelas dos outros apps,
então não tem como ser o gerenciador de janelas. Para a topbar e o dock ficarem "presos" na borda da
tela, por cima de tudo, as janelas do Tauri viram superfícies **layer-shell** (protocolo
`wlr-layer-shell`) com a biblioteca `gtk-layer-shell`.

### Opções de compositor

| Opção | Prós | Contras | Veredito |
|---|---|---|---|
| **Hyprland** | Animações muito fluidas, blur real, IPC por socket (crate `hyprland` em Rust), workspaces por monitor, plugins, `allow_tearing` para jogos | Pensado para tiling (dá para deixar tudo flutuante por regra); sem barra de título por padrão (plugin `hyprbars`); API muda entre versões | **MVP** |
| Wayfire | Janelas flutuantes de verdade, plugins, animações, IPC | Comunidade menor, menos polido | Plano B |
| labwc | Muito leve, flutuante, estilo Openbox | Animações e blur fracos, pouca "fluidez" | Não combina com o visual |
| KWin (KDE) | Completo, efeitos, scripts | Pesado, puxa o Plasma junto | Contra o "leve" |
| Compositor próprio com **Smithay** (Rust) | Controle total, tudo em Rust junto do Tauri (é o caminho do COSMIC e do niri) | Meses a anos de trabalho só para chegar no básico | **Fase futura** |

Para não ficar preso ao Hyprland, o lado Rust conversa com o compositor por uma interface própria
(`trait Compositor`), e o Hyprland é só a primeira implementação.

---

## 2. Viabilidade por parte

| Parte | Viabilidade | Observação |
|---|---|---|
| Topbar, dock e launcher em Tauri + Vue | **Alta** (com spike) | Precisa ligar o layer-shell antes da janela aparecer: criar a janela com `visible: false`, pegar o `gtk_window()` e chamar `init_layer_shell`. É o primeiro risco a provar |
| Gerenciamento de janelas fluido | **Alta** | Vem do compositor (animações e blur do Hyprland) |
| Troca de modo com `Super + Tab` | **Alta** | Atalho no compositor chama `lumna-ctl mode next`; o shell troca workspaces e interface |
| Multi-monitor (um modo por tela / estender) | **Alta** | Workspaces presos a monitores via IPC; o shell abre uma topbar por monitor |
| Modo Desktop | **Alta** | Apps comuns. Gerenciador de arquivos e notas da imagem: usar apps prontos (Nautilus/Thunar) no começo |
| Modo Gaming | **Alta** como launcher | Lê a biblioteca da Steam (`appmanifest_*.acf`), Heroic e Lutris; capas pela Steam ou SteamGridDB; abre com `steam://rungameid/<id>` |
| Modo Mídia, vídeos locais e YouTube | **Alta** | mpv/Jellyfin para locais; YouTube funciona sem DRM |
| Modo Mídia, Netflix, Prime, Disney+ | **Média, com limites** | Ver seção 3 |
| "Continuar assistindo" da Netflix | **Baixa** | Não existe API pública; só dá para ter isso de verdade nos vídeos locais/Jellyfin |
| Emma | **Alta** | Já existe no LUMNA (backend FastAPI em `127.0.0.1:8001`, voz pela Fish Audio). O OS só abre o painel e fala com esse backend |
| Distro instalável (ISO) | **Média** | Possível, mas é o último passo (seção 6) |

---

## 3. Riscos reais (e o que fazer com cada um)

### 3.1 Netflix e DRM (o maior "não" do projeto)
O WebKitGTK (motor do Tauri no Linux) **não tem Widevine**, então Netflix, Prime e Disney+ **não
tocam dentro da interface Vue**. Saída: os cards da tela Mídia abrem o Chromium/Firefox em modo app
(`chromium --app=https://www.netflix.com` ou kiosk), que tem Widevine. Mesmo assim, no Linux os
streamings costumam limitar a qualidade (Netflix historicamente em 720p no navegador). É limite das
empresas, não do LUMNA.

### 3.2 Desempenho do WebKitGTK
O WebKitGTK é mais lento que o Chromium. O visual "vidro fosco" da imagem com `backdrop-filter`
em CSS pesa muito nele. Regras:
- **Blur é do compositor**, não do CSS (`layerrule = blur, lumna-topbar` no Hyprland) com janela
  transparente no Tauri.
- Animações só com `transform` e `opacity`.
- Interface de Gaming/Mídia **escondida e congelada** enquanto um jogo ou vídeo roda.
- Placas NVIDIA podem precisar de `WEBKIT_DISABLE_DMABUF_RENDERER=1`.

### 3.3 Memória
Cada webview do WebKitGTK custa memória. Estimativa (a medir no spike): compositor ~80 MB + shell
~250-400 MB. Fica abaixo de GNOME e KDE, mas não é "minúsculo". Para economizar: poucas janelas
Tauri, uma topbar por monitor, e o resto (launcher, troca de modo, notificações) reaproveitado.

### 3.4 Jogos em tela cheia
O jogo tem que pegar a tela inteira sem o shell no meio. No modo Gaming: esconder a topbar ao abrir
o jogo, `allow_tearing` no Hyprland, governor de CPU em `performance`, "Não perturbe" ligado.
Opcional: abrir pelo **gamescope** (o compositor do Steam Deck) para limitar FPS e escalar resolução.

### 3.5 Controle e TV
Gaming e Mídia são usados de longe e com controle. A interface Vue precisa de navegação por foco
(setas/D-pad) desde o começo, lendo o controle pela Gamepad API ou pelo Rust (`gilrs`).

---

## 4. Arquitetura proposta

```
 ┌──────────────────────────────── Sessão Wayland "LUMNA" ────────────────────────────────┐
 │                                                                                        │
 │  Hyprland (compositor)  ◄── IPC socket ──►  lumna-shell (Tauri, Rust)                  │
 │   ├─ janelas dos apps                        ├─ ModeManager (Desktop/Gaming/Mídia)     │
 │   ├─ animações, blur                         ├─ trait Compositor → HyprlandAdapter     │
 │   └─ bind Super+Tab ─► lumna-ctl ──socket──► ├─ D-Bus (zbus): notificações, MPRIS,     │
 │                                              │   bateria (UPower), rede, áudio         │
 │                                              ├─ apps (.desktop), Steam/Heroic/Lutris   │
 │                                              └─ janelas Vue (layer-shell)              │
 │                                                  ├─ topbar (1 por monitor)             │
 │                                                  ├─ dock                               │
 │                                                  ├─ tela Gaming (cheia)                │
 │                                                  ├─ tela Mídia (cheia)                 │
 │                                                  └─ painel da Emma ──► backend LUMNA   │
 └────────────────────────────────────────────────────────────────────────────────────────┘
```

### Como os modos funcionam por baixo
Cada modo é um **conjunto de workspaces + uma interface + ajustes do sistema**:

| Modo | Workspaces | Interface | Ajustes |
|---|---|---|---|
| Desktop | 1-9 | topbar + dock | normal |
| Gaming | `special:gaming` | tela "Sua biblioteca" | performance, DND, tearing, topbar oculta no jogo |
| Mídia | `special:media` | tela "Início" estilo TV | DND, tela não apaga |

- **Um modo por tela:** cada monitor guarda o seu modo; `Super + Tab` troca o modo do monitor em foco.
- **Estender modo:** o modo vale para todos os monitores ao mesmo tempo.
- Trocar de modo **não fecha nada**: as janelas ficam no workspace de cada modo, como trocar de mesa.

### Estrutura do repositório
```
LUMNA_OS/
├── crates/
│   ├── lumna-core/    # modos, estado, trait Compositor, config
│   ├── lumna-hypr/    # adaptador Hyprland
│   └── lumna-ctl/     # CLI chamada pelos atalhos
├── shell/
│   ├── src-tauri/     # app Tauri v2 (layer-shell, D-Bus, comandos)
│   └── src/           # Vue 3 + Vite + Pinia (topbar, dock, Gaming, Mídia, Emma)
├── session/           # hyprland.conf, lumna.desktop (entrada no login), regras
└── docs/
```

---

## 5. Plano por fases

| Fase | Entrega | Critério de pronto |
|---|---|---|
| **0. Spikes** (1-2 semanas) | Topbar Tauri em layer-shell no Hyprland; troca de workspace por IPC; medir RAM e FPS; testar Netflix no Chromium `--app` | Topbar presa no topo com blur do compositor, RAM medida, decisão final de compositor |
| 1. Desktop MVP | Sessão LUMNA no login, topbar (relógio, bateria, volume, rede), dock, launcher, notificações | Dá para usar no dia a dia |
| 2. Troca de modo | `lumna-ctl`, `Super + Tab`, seletor visual "Alternar modo" | Trocar entre os 3 modos sem perder janelas |
| 3. Gaming | Biblioteca Steam/Heroic/Lutris, capas, filtros por gênero, controle, ajustes de desempenho | Abrir um jogo da Steam pelo controle |
| 4. Mídia | Cards de streaming (navegador em modo app), vídeos locais com mpv/Jellyfin, "continuar assistindo" local | Netflix abre em tela cheia; vídeo local retoma de onde parou |
| 5. Multi-monitor | "Um modo por tela" e "Estender modo", tela de configuração "Telas" | 3 monitores com 3 modos diferentes |
| 6. Emma | Painel lateral, voz, ligação com o backend LUMNA | "O que vamos fazer hoje?" funcionando |
| 7. Distro | Imagem instalável (seção 6) | ISO que inicia direto no LUMNA |

---

## 6. Base de distribuição

Primeiro o LUMNA OS é **uma sessão instalável** (aparece no GDM/SDDM como "LUMNA") em cima de uma
distro comum. Isso deixa testar tudo sem montar distro. Quando estiver estável:

- **Fedora Atomic / Bazzite + BlueBuild:** imagem imutável, atualizações atômicas, drivers e
  ajustes de jogos já prontos (Bazzite). Melhor encaixe para o modo Gaming.
- **Arch (archiso) / CachyOS:** mais controle e pacotes recentes, mais manutenção.

Recomendação: começar a sessão em **Arch ou Fedora**, e a ISO final em **Bazzite + BlueBuild**.

---

## 7. Alternativas consideradas

Ferramentas feitas só para shells, como **Quickshell** (QML) ou **AGS/Astal** (GJS), seriam mais
leves que o WebKitGTK. Ficam como plano B caso o spike da fase 0 mostre que o Tauri pesa demais.
A escolha atual é **Tauri + Vue** porque deixa reaproveitar o jeito de fazer interface web do
LUMNA e dá um backend Rust forte para D-Bus, IPC e sistema.

## 8. Próximo passo

Fazer a **Fase 0**: um app Tauri v2 + Vue mínimo que abre uma topbar layer-shell no Hyprland,
lê o workspace atual por IPC e troca de workspace por um botão. Se isso ficar fluido e leve, o
resto do projeto é trabalho, não incerteza.
