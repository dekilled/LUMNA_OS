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
| **Hyprland** | Animações muito fluidas, blur real, IPC por socket (cliente próprio em `crates/lumna-hypr`), workspaces por monitor, plugins, `allow_tearing` para jogos | Pensado para tiling (dá para deixar tudo flutuante por regra); sem barra de título por padrão (plugin `hyprbars`); API muda entre versões (a 0.56 trocou a config para **Lua**, `hl.*`) | **MVP** |
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
| Modo Desktop | **Alta** | Apps comuns. Gerenciador de arquivos próprio (o "Arquivos" do LUMNA); para notas, apps prontos no começo |
| Modo Gaming | **Alta** como launcher | Lê a biblioteca da Steam (`appmanifest_*.acf`), Heroic e Lutris; capas pela Steam ou SteamGridDB; abre com `steam://rungameid/<id>` |
| Modo Mídia, vídeos locais e YouTube | **Alta** | mpv/Jellyfin para locais; YouTube funciona sem DRM |
| Modo Mídia, Netflix, Prime, Disney+ | **Média, com limites** | Ver seção 3 |
| "Continuar assistindo" da Netflix | **Baixa** | Não existe API pública; só dá para ter isso de verdade nos vídeos locais/Jellyfin |
| Emma | **Alta** | Já existe no LUMNA (backend FastAPI em `127.0.0.1:8001`, voz pela Fish Audio). O OS só abre o painel e fala com esse backend |
| Distro própria (pendrive → formatação → instalação) | **Alta**, com trabalho | Arch + archiso + Calamares com tema LUMNA (seção 6) |

---

## 3. Riscos reais (e o que fazer com cada um)

### 3.1 Netflix e DRM (o maior "não" do projeto)
O WebKitGTK (motor do Tauri no Linux) **não tem Widevine**, então Netflix, Prime e Disney+ **não
tocam dentro da interface Vue**. Saída: os cards da tela Mídia abrem o Chromium/Firefox em modo app
(`chromium --app=https://www.netflix.com` ou kiosk), que tem Widevine. Mesmo assim, no Linux os
streamings costumam limitar a qualidade (Netflix historicamente em 720p no navegador). É limite das
empresas, não do LUMNA.

#### Continuidade visual: o navegador não pode parecer "outro app"
A ideia é o card da tela Mídia **virar** o player, sem piscar janela de navegador. O Hyprland
sozinho não sabe animar uma janela "saindo" de um card (as animações dele, como `popin` e `slide`,
partem do centro ou da borda). Por isso a transição é dividida em duas partes: o Tauri cuida do
movimento e o Hyprland esconde as costuras.

**Abrir (card → player):**
1. A tela Mídia fica numa superfície layer-shell na camada `overlay`, **acima** das janelas comuns.
2. Clicou no card: o Vue anima o card crescendo até ocupar a tela (técnica FLIP, só `transform`)
   e termina numa tela de abertura (fundo e logo do serviço).
3. Ao mesmo tempo o Rust abre o Chromium em modo app. Uma regra do Hyprland manda essa janela para
   `special:media`, já em tela cheia e **sem animação própria** (`noanim`), por baixo do overlay.
4. O Rust escuta o evento `openwindow` no socket de eventos do Hyprland (`.socket2.sock`), espera
   a página pintar e avisa o Vue.
5. O Vue apaga o overlay com fade. O player aparece por baixo exatamente onde o card terminou.
   Para quem olha, foi uma animação só.

**Fechar (player → card):** com o evento `closewindow`, o overlay volta com a última imagem e o
card encolhe de volta para o lugar dele na fileira "Continuar assistindo".

**Bônus:** antes de fechar, dá para capturar o último quadro da janela (`grim` com a geometria vinda
do `hyprctl clients`) e usar como capa do card. Assim o "Continuar assistindo" mostra onde você
parou, mesmo sem API da Netflix. Falta testar se o DRM deixa a captura preta.

**Riscos a medir no spike:**
- **Tempo de abertura do Chromium** (~1 s a frio). Se a tela de abertura ficar longa demais, deixar
  um Chromium "morno" em segundo plano enquanto o modo Mídia estiver aberto (gasta RAM).
- **Achar a janela certa:** no Wayland o Chromium em modo app recebe um `app_id` próprio por site
  (algo como `chrome-www.netflix.com__-Default`). As regras do Hyprland usam esse nome; confirmar o
  formato exato na versão instalada.
- **Plugin próprio** (C++, API de plugins do Hyprland) para animar a janela real a partir do
  retângulo do card fica como evolução, se o truque do overlay não bastar.

O mesmo truque vale para abrir jogos no modo Gaming (a capa vira o jogo) e para trocar de modo
(`animation = specialWorkspace, ...` com uma curva `bezier` própria do LUMNA, igual em todo o sistema).

### 3.2 Desempenho do WebKitGTK
O WebKitGTK é mais lento que o Chromium. O visual "vidro fosco" da imagem com `backdrop-filter`
em CSS pesa muito nele. Regras:
- **Blur é do compositor**, não do CSS (`layerrule = blur, lumna-topbar` no Hyprland) com janela
  transparente no Tauri.
- Animações só com `transform` e `opacity`.
- Interface de Gaming/Mídia **escondida e congelada** enquanto um jogo ou vídeo roda.

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
├── packages/          # PKGBUILDs dos pacotes lumna-*
├── iso/               # perfil archiso (sessão live)
├── installer/         # configuração e tema do Calamares
└── docs/
```

### 4.1 Controle pelo celular (modo Mídia)

O celular vira controle remoto da TV: setas, OK, voltar, volume, play/pause e um teclado para
buscas. Ele manda as **mesmas ações** que o controle de videogame (`up`, `down`, `accept`,
`back`...), então qualquer tela navegável pelo controle também funciona pelo celular.

- **No PC:** o `lumna-shell` abre um servidor WebSocket só na rede local. Para parear, a tela de
  Mídia mostra um QR code com um código de uso único.
- **No celular:** app nativo em Kotlin, o mesmo jeito de fazer do app LUMNA que já existe. Pode
  ser uma tela "Controle" dentro dele ou um app separado.
- **Segurança:** sem pareamento nada é aceito; o código troca a cada pareamento e dá para revogar
  um celular pela tela de Mídia.

---

## 5. Plano por fases

| Fase | Entrega | Critério de pronto |
|---|---|---|
| **0. Spikes** (1-2 semanas) | Topbar Tauri em layer-shell no Hyprland; troca de workspace por IPC; medir RAM e FPS; testar Netflix no Chromium `--app` com a transição card → player | Topbar presa no topo com blur do compositor, RAM medida, decisão final de compositor |
| **0.5 ISO mínimo** | Perfil archiso que dá boot na VM (UEFI) direto no Hyprland com a topbar do spike | Desde o começo, tudo é testado pelo mesmo caminho do pendrive |
| 1. Desktop MVP | Sessão LUMNA no login, topbar (relógio, bateria, volume, rede), dock, launcher, notificações | Dá para usar no dia a dia |
| 2. Troca de modo | `lumna-ctl`, `Super + Tab`, seletor visual "Alternar modo" | Trocar entre os 3 modos sem perder janelas |
| 3. Gaming | Biblioteca Steam/Heroic/Lutris com capas, filtros por loja e recentes, navegação por controle (gilrs) e teclado | Abrir um jogo da Steam pelo controle |
| 4. Mídia | Cards de streaming (navegador em modo app), vídeos locais com mpv/Jellyfin, "continuar assistindo" local e **controle pelo celular** (seção 4.1) | Netflix abre em tela cheia; vídeo local retoma de onde parou; navegar pelo celular |
| **4.5 Apps do sistema** | **Configurações** (Wi-Fi e cabo, Bluetooth, som, impressoras, sistema), **Loja** (pacman + Flatpak/Flathub + AUR, com repositórios e atualizações; polkit para pedir a senha) e **Explorador de arquivos** próprio no lugar do Thunar | Usar o PC sem abrir o terminal para nada do dia a dia |
| 5. Multi-monitor | "Um modo por tela" e "Estender modo", tela de configuração "Telas" | 3 monitores com 3 modos diferentes |
| 6. Emma | Painel lateral, voz, ligação com o backend LUMNA | "O que vamos fazer hoje?" funcionando |
| 7. Visual | Wallpaper próprio por modo, videopaper (mpvpaper), animações e efeitos mais modernos em todas as telas | Cada modo com cara própria, sem perder fluidez |
| 8. Distro completa | Calamares com tema LUMNA, formatação (Btrfs, LUKS, dual boot), Mesa (AMD/Intel), repo próprio, snapshots (seção 6) | Pendrive → formatar → instalar → primeiro boot no LUMNA, no PC real |

---

## 6. A distro LUMNA OS: do pendrive à instalação

O objetivo final é uma **distro própria**: o ISO é gravado no pendrive, o PC dá boot direto no
LUMNA, um instalador com a cara do LUMNA formata o disco e instala, e o primeiro boot já entra no
sistema pronto. A VM serve para testar, mas testa **o mesmo ISO** que vai para o pendrive. Não é
um caminho separado.

### 6.1 Base: Arch Linux + archiso + Calamares

| Opção | Controle do ISO e do instalador | Veredito |
|---|---|---|
| **Arch + archiso + Calamares** | Total: sessão live, pacotes, instalador (com tema em QML) e marca. É o caminho do EndeavourOS e do CachyOS | **Escolhida** |
| Fedora Atomic / Bazzite + BlueBuild | Imagem imutável e drivers de jogo prontos, mas o instalador é o Anaconda, com pouca liberdade de visual e de fluxo | Descartada para a distro final |
| Debian/Ubuntu + live-build | Estável, mas Hyprland, Mesa e drivers ficam velhos para jogos | Descartada |

Arch tem pacotes recentes (Hyprland, Mesa, kernel), e isso importa para Gaming. Para ajudar no
desempenho dá para usar os repositórios e kernels otimizados do **CachyOS** como opção.

### 6.2 Hardware: só AMD e Intel

O LUMNA OS **suporta apenas GPUs AMD e Intel**. NVIDIA fica fora: nenhum driver proprietário,
nenhum ajuste, nenhum teste. Isso deixa a distro bem mais simples:

- **Um único driver aberto (Mesa)**, que já vem no kernel e no ISO. Não existe passo de
  "detectar a placa e instalar driver" no instalador: o mesmo sistema roda em qualquer AMD ou Intel.
- **Pacotes de vídeo fixos:** `mesa`, `vulkan-radeon`, `vulkan-intel`, as versões `lib32-*` (para a
  Steam e o Proton) e `libva-mesa-driver` + `intel-media-driver` (vídeo por hardware no modo Mídia).
- **Wayland, Hyprland, gamescope e WebKitGTK** funcionam bem nesse hardware sem gambiarra.
- **Hardware de referência nº 1: AMD Radeon RX 6600 XT** (RDNA 2, Navi 23, 8 GB). É a placa em que
  tudo é testado primeiro e onde o "pronto" de cada fase é validado. Intel (Iris Xe ou Arc) vem depois.

#### O que a RX 6600 XT define no projeto
| Área | Na prática |
|---|---|
| Vulkan | Driver **RADV** (Mesa). É o que o Proton usa para os jogos do Windows |
| Alvo do modo Gaming | Jogar em **1080p**. Para 1440p pesado, **FSR** pelo gamescope |
| Vídeo no modo Mídia | Decodifica por hardware H.264, HEVC, VP9 e **AV1** (VA-API). O mpv usa isso direto; no Chromium precisa ligar a aceleração por VA-API e conferir |
| Gravação/stream de jogo | Codifica H.264 e HEVC por hardware. **AV1 não** (só a partir da RX 7000) |
| Desempenho | Ativar **Resizable BAR** (SAM) na BIOS. Controle de ventoinha e limite de energia pelo **LACT**, que pode virar uma tela de ajustes do modo Gaming |
| Emma local (opcional) | ROCm não suporta essa placa oficialmente. Para voz e IA locais, usar **Vulkan** (whisper.cpp, llama.cpp) |
| VM | Uma placa só: a VM usa virgl (OpenGL virtual). O desempenho real só se mede com o pendrive live no PC |

#### Próxima placa: RX 7600 XT
RDNA 3 (Navi 33), **16 GB** em barramento de **128 bits** (a de 192 bits da geração é a RX 7700 XT,
com 12 GB). Muda pouco para o LUMNA: mesmo driver RADV, e ganha **codificação AV1** por hardware
(gravar e transmitir jogos). Os 16 GB ajudam em texturas altas e em IA local (Emma); ROCm continua
sem suporte oficial, então a IA local segue por Vulkan.
- Quem tentar com NVIDIA pode até conseguir dar boot pelo driver aberto `nouveau`, mas sem garantia
  e sem suporte. O site e o instalador avisam isso.

### 6.3 O caminho completo

| Etapa | O que acontece | Peças |
|---|---|---|
| **1. Gerar o ISO** | O GitHub Actions monta o ISO a cada versão e publica na Release | `archiso` (perfil próprio em `iso/`), container `archlinux` no CI |
| **2. Gravar o pendrive** | O usuário grava o ISO | Ventoy, balenaEtcher, Rufus (modo DD) ou `dd`. Um "LUMNA Media Creator" próprio fica para depois |
| **3. Boot do pendrive** | UEFI → menu de boot com tema LUMNA → sessão live | `systemd-boot` (UEFI) e `syslinux` (BIOS antiga), Plymouth com o cristal animado |
| **4. Sessão live** | Entra direto no LUMNA (Desktop) com login automático. No dock, o ícone **"Instalar LUMNA OS"** | `greetd` com autologin, Hyprland + `lumna-shell` |
| **5. Instalador** | Idioma (pt-BR), teclado, fuso, **disco**, usuário, resumo, instalar | **Calamares** com tema LUMNA |
| **6. Formatação** | Apagar o disco todo, instalar ao lado do Windows, ou manual. Criptografia opcional | Calamares: GPT, partição EFI, **Btrfs** com subvolumes, LUKS opcional |
| **7. Instalação** | Copia o sistema e instala o bootloader. Sem detecção de placa: AMD e Intel usam o mesmo Mesa | `unpackfs`, GRUB com `os-prober` (dual boot) ou systemd-boot |
| **8. Primeiro boot** | Tela de login LUMNA → boas-vindas (Wi-Fi, conta da Emma, Steam) | `greetd` com tela própria, assistente de boas-vindas em Tauri |
| **9. Atualizações** | `pacman` normal + **repositório próprio** com os pacotes `lumna-*` | PKGBUILDs em `packages/`, repo servido pelas Releases do GitHub |
| **10. Volta segura** | Atualização quebrou? Escolhe um snapshot no menu de boot | `snapper` + `grub-btrfs` |

### 6.4 Pacotes da distro
- `lumna-shell`: o app Tauri + Vue.
- `lumna-session`: configuração do Hyprland, regras, `lumna-ctl`, entrada da sessão.
- `lumna-branding`: wallpapers, tema GTK/Qt, ícones, Plymouth, tema do Calamares e do boot.
- `lumna-gaming`: Steam, gamescope, gamemode, MangoHud, Vulkan AMD/Intel 32 bits (multilib).
- `lumna-media`: Chromium (com Widevine), mpv, codecs.
- `lumna-installer`: Calamares e a configuração dele (só no ISO).

### 6.5 Testando na VM
- **QEMU/KVM com virt-manager**, firmware **UEFI (OVMF)** para testar o mesmo boot do PC real.
- Vídeo **virtio-gpu com OpenGL (virgl)** e SPICE com OpenGL ligado. Sem aceleração o Hyprland
  fica lento ou nem abre. VirtualBox e VMware costumam ir mal com compositores Wayland.
- **Vários monitores na VM:** o virtio-gpu aceita várias saídas (`heads`/`max_outputs`). Dá para
  testar "um modo por tela" com 3 telas virtuais.
- Um **disco virtual vazio** para rodar o instalador de verdade, formatação incluída, quantas vezes
  precisar. Um segundo cenário com Windows instalado testa o dual boot.
- O que a VM **não** mostra direito: desempenho de jogos, decodificação de vídeo por hardware e HDR. Isso é testado no PC
  real, primeiro pelo pendrive em modo live, sem instalar nada.

### 6.6 Riscos da distro
- **Secure Boot:** o ISO do Arch não dá boot com Secure Boot ligado. No começo, desligar na BIOS.
  Depois dá para assinar com `sbctl`/shim, mas é trabalho à parte.
- **Formatação errada apaga dados:** o instalador tem que mostrar com clareza o que vai ser apagado
  e testar muito o modo "ao lado do Windows" na VM antes do PC de verdade.
- **Manutenção:** com Arch, a distro acompanha atualizações toda semana. O CI que gera o ISO também
  roda um teste de boot na VM (QEMU sem tela) para pegar quebra cedo.

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
