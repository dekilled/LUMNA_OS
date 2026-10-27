# Roteiro: LUMNA OS no Orbit com o Gravity

O shell em Vue fica como está. O LUMNA novo é o **Orbit** (compositor) com o **Gravity** (a
interface em C) por cima, feito por etapas que terminam sempre em algo que roda — primeiro no
Orbit dentro do PC, depois no celular. Cada peça do desktop é um processo pequeno: se uma cai, as
outras ficam de pé.

Marque `[x]` ao terminar; o que estiver a meio leva `[~]`.

## 1. A base do Gravity

- [x] Paleta nova do LUMNA (tokens) e componentes nos estados certos
- [x] Botões, deslizante, interruptor, campo de texto, linha de ajuste, abas, barra da janela
- [x] Rolagem: arrastar com o dedo, inércia, elástico nas pontas, roda do rato, barra que aparece
- [x] Imagens: PNG/JPEG/WebP/SVG descodificadas em threads, no tamanho da tela, em cache
- [x] Popups e menus presos a um botão (xdg-popup), que fecham ao tocar fora
- [x] Lista suspensa (dropdown), como na tela de Rede
- [x] Foco pelo teclado entre controles (Tab, setas, Enter) com o anel de foco
- [x] Texto que quebra em várias linhas e campo de várias linhas
- [x] Ícones dos apps pelo tema de ícones (freedesktop) para o lançador

## 2. O desktop no Orbit

- [x] Papel de parede (camada de fundo) — exemplo `papel`: um por tela e por modo, troca na hora e
      sozinho pela coleção (papel.ini), o menu do botão direito
- [~] Topbar: LUMNA, modo, indicador de áreas, Wi-Fi, som, Bluetooth, relógio — exemplo `barra`
- [x] Dock: apps fixos, abertos, indicador do ativo — exemplo `dock`
  - [x] Menu do botão direito (ou segurar): abrir nova janela, fixar/remover, fechar, preferências
  - [x] Lista das janelas ao passar o mouse num app com várias (ícone e título; tocar vai, ✕ fecha)
  - [ ] Miniaturas na lista, quando o Orbit for para o wlroots 0.19 (que captura janelas)
  - [x] Preferências (Painel → Aparência → Dock): pílula ou barra de borda a borda; só no monitor
        principal ou em todos (uma dock por tela, também nas que ligarem depois); valem na hora
  - [x] Janela maximizada: ocultar (com a faixa que a traz de volta) ou esticar, por monitor
  - [ ] Arrastar para reordenar
- [~] Menus da topbar: Bluetooth, Áudio, Rede (as telas novas) — o do Bluetooth já abre (dados de mentira)
- [x] Lançador ("Início") — exemplo `lancador`: favoritos, abas por categoria com os detalhes do
      app (o rato mostra, segurar fixa, ✕ solta; abre sempre solto), recentes, busca,
      bloquear/desligar/Emma e o Fechar ao centro
  - [ ] Atualizações e desinstalar de verdade (o gestor de pacotes: lumna-pkg)
- [x] Notificações — exemplo `avisos`: o servidor org.freedesktop.Notifications (ações, imagens,
      urgência, residentes e transitórios), os avisos no canto (o rato segura, ✕ dispensa) e a
      central pelo sino da topbar (por app, limpar, "Não perturbe")
  - [ ] Um ponto no sino quando há avisos por ver, e os avisos no ecrã de bloqueio
- [x] Tela de bloqueio — exemplo `bloqueio` (lumna-bloqueio): tranca a sessão (ext-session-lock), layouts
      Centralizado e Lateral, PIN ou senha (o mesmo PIN do celular), o que está tocando (MPRIS) e a Emma
      só para o simples
  - [x] Todas as telas trancam juntas (o papel nas outras), e sozinho: parado (lumna-ocioso, ext-idle-notify) e antes de suspender (logind)
  - [x] O papel do bloqueio pode ser uma imagem sua; o layout vale também para a tela de login (Painel, com pkexec)
  - [ ] A voz da Emma no bloqueio
- [x] Controle pelo celular (lumna-controle, serviço da sessão): protocolo do LUMNA Controle (porta 47800, mDNS, pareamento por código), sistema (volume, modo, Home) e serviço (teclado e ponteiro virtuais, MPRIS) sem depender de qual janela está na frente; `lumna-controle parear|estado|esquecer`
  - [ ] O app do celular: touchpad (pointer/click/scroll já aceitos pelo serviço) e o teclado
  - [ ] Os recursos do Connect (arquivos, área de transferência, avisos, Emma, energia) ainda moram no shell antigo
- [x] Mídia (lumna-midia): o Firefox em kiosk é o player web (um perfil, uma aba por serviço, controle por WebDriver BiDi); o serviço guarda o catálogo editável (midia.ini) e os recentes (URL, título, miniatura); Início, capa de loading com fade e seletor (Super+M) conforme os mockups; Home e abrir serviço também pelo celular; testes com um Firefox de mentira (`testes/midia.sh`)
- [x] Controle do Firefox da Mídia por uma extensão nossa (extensao-firefox/), não pelo WebDriver: a marca navigator.webdriver não liga e o Google deixa entrar; o serviço só aceita a extensão (Origin moz-extension://UUID fixo no perfil); Início > "Entrar nas contas" leva o serviço à página de entrada da conta
  - [ ] Assinar a extensão (Firefox comum) ou usar o Developer Edition: ver extensao-firefox/LEIAME.md
- [x] `LUMNA_TEMPO=1 lumna-lancador` mostra o tempo de cada etapa da abertura do launcher
  - [ ] Testar no Firefox de verdade: a extensão carrega e conecta (about:addons), app_id da janela (--name/--class), Widevine (o CDM baixa na primeira vez), login do Google
- [x] Telas em Gaming/Mídia como ilhas no Orbit (fora do Alt+Tab e da dock, sem janelas entrando ou saindo) e "Trocar telas" (Super+Shift+O, botão no Início da Mídia, item no menu de modo da topbar, `swap-screens` no celular)
  - [ ] Conferir na dock de verdade que as janelas das ilhas não aparecem; arrastar com o mouse passando pela ilha; janelas de jogos que o Gaming abre com outro app_id
  - [ ] Ícones das marcas (~/.local/share/lumna/midia/icones/ID.svg), Música e Vídeos locais no Início, editar/remover serviços pela tela, LRU de abas vivas
- [x] Tela de login (lumna-entrada, greetd): Centralizado ou Painel lateral; `./testar-login.sh [lateral]` testa com um greetd de mentira (senha "lumna")
- [ ] Escolher o layout do login pelo Painel (precisa de root: /etc/lumna/entrada.ini)
- [~] Orbit: o que o desktop pedir pelo caminho — ativar, fechar, maximizar e ecrã inteiro (dos apps e da
      dock), ecrã inteiro por cima da barra e da dock, ponteiro virtual; falta as áreas

## 3. Serviços do sistema

- [x] Som (libpulse / pipewire-pulse)
- [ ] Rede (NetworkManager por D-Bus)
- [ ] Bluetooth (BlueZ por D-Bus)
- [ ] Energia e bateria (UPower)
- [ ] Brilho e tela

## 4. Apps

- [~] Painel de controle — a estrutura do PAINEL.md: abas Geral, Inicialização e Sistema, busca em tudo,
      usados recentemente, a barra lateral por categoria e Restaurar | Aplicar (a Dock já aplica)
- [~] Rede — a janela da tela nova no exemplo `rede` (falta o NetworkManager)
- [~] Celular Connect — a área de transferência no exemplo `transferencia` (falta a ligação ao celular)
- [ ] Mixer como app e como camada (já existe no exemplo `mixer`)

## 5. O celular

- [ ] Descobrir o crashdump: depois de cair, ler `/sys/fs/pstore/*` e `journalctl -b -1`; o
      Samsung ir para o RAM dump é pânico do kernel (não é só falta de memória: essa mata o
      processo, não o aparelho). Ver se acontece só com o shell atual ou também parado no TTY.
- [ ] Orbit e Gravity em aarch64 no CI (o workflow Celular)
- [ ] Orbit no backend DRM do aparelho, com libseat
- [ ] Gestos de borda, teclado na tela (já há o relay do input-method), rotação
- [ ] Medir memória e CPU lá (cada app Gravity usa uns 16 MB aqui no PC)
- [ ] Trocar a sessão do celular pelo Orbit + desktop novo
- [x] Atalhos no Painel (Sistema → Teclado e mouse → Atalhos): Alt+Tab, Super+H, Super+Tab (lumna-modo), Super sozinho abre o lançador
- [x] Modo desenvolvedor (Painel → Sistema → Desenvolvedor): tela de toque no desktop (teclado de números no PIN) e aplicar o Painel em tempo real
- [x] Topbar de verdade: Wi-Fi (rede, senha ali mesmo), som (volume, saída, apps), Bluetooth, calendário do mês e a bandeja do sistema (StatusNotifierItem + dbusmenu: os apps em segundo plano, menu no clique direito)
- [x] Modos do desktop: um espaço por modo e por saída no Orbit, Super+Tab / Super+Shift+Tab, seletor na topbar e no meio da tela, dock escondida e não perturbe por modo, Painel → Telas e modos → Modos
- [ ] As telas de Gaming (biblioteca de jogos) e de Mídia

