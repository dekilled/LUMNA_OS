# Roteiro: LUMNA OS no Orbit com o Gravity

O shell em Vue fica como está. O LUMNA novo é o **Orbit** (compositor) com o **Gravity** (a
interface em C) por cima, feito por etapas que terminam sempre em algo que roda — primeiro no
Orbit dentro do PC, depois no celular. Cada peça do desktop é um processo pequeno: se uma cai, as
outras ficam de pé.

Marque `[x]` ao terminar; o que estiver a meio leva `[~]`. Última conferência com o código: 4 de novembro de 2026.

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
- [x] Topbar: LUMNA, modo, indicador de áreas, Wi-Fi, som, Bluetooth, relógio — exemplo `barra`
  - [x] Em quais telas ela aparece: só na principal, em todas, em nenhuma ou nas escolhidas (a da principal abre as outras)
  - [x] No Orbit aninhado, um botão que sai da tela cheia e volta (`orbit-msg tela_cheia`)
- [x] Dock: apps fixos, abertos, indicador do ativo — exemplo `dock`
  - [x] Menu do botão direito (ou segurar): abrir nova janela, fixar/remover, fechar, preferências
  - [x] Lista das janelas ao passar o mouse num app com várias (ícone e título; tocar vai, ✕ fecha)
  - [ ] Miniaturas na lista, quando o Orbit for para o wlroots 0.19 (que captura janelas)
  - [x] Preferências (Painel → Aparência → Dock): pílula ou barra de borda a borda; só no monitor
        principal ou em todos (uma dock por tela, também nas que ligarem depois); valem na hora
  - [x] Janela maximizada: ocultar (com a faixa que a traz de volta) ou esticar, por monitor
  - [ ] Arrastar para reordenar
- [x] Menus da topbar: Bluetooth, Áudio, Rede, o calendário e a bandeja do sistema (veja "Topbar de verdade", mais abaixo)
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
- [x] Controle pelo celular (lumna-connect, serviço da sessão): protocolo do LUMNA Connect (porta 47800, mDNS, pareamento por código), sistema (volume, modo, Home) e serviço (teclado e ponteiro virtuais, MPRIS) sem depender de qual janela está na frente; `lumna-connect parear|estado|esquecer`
  - [ ] O app do celular: touchpad (pointer/click/scroll já aceitos pelo serviço) e o teclado
  - [x] Os recursos do dia a dia do Connect (pareamento, área de transferência, avisos e arquivos) moram no lumna-connect, com as páginas do Painel e o Enviar para o celular da captura (testado só com um celular falso: `testes/connect.sh`)
  - [ ] Emma e energia pelo celular ainda moram no shell antigo
- [x] Mídia (lumna-midia): o Firefox em kiosk é o player web (um perfil, uma aba por serviço, controle por WebDriver BiDi); o serviço guarda o catálogo editável (midia.ini) e os recentes (URL, título, miniatura); Início, capa de loading com fade e seletor (Super+M) conforme os mockups; Home e abrir serviço também pelo celular; testes com um Firefox de mentira (`testes/midia.sh`)
- [x] Controle do Firefox da Mídia por uma extensão nossa (extensao-firefox/), não pelo WebDriver: a marca navigator.webdriver não liga e o Google deixa entrar; o serviço só aceita a extensão (Origin moz-extension://UUID fixo no perfil); Início > "Entrar nas contas" leva o serviço à página de entrada da conta
  - [ ] Assinar a extensão (Firefox comum) ou usar o Developer Edition: ver extensao-firefox/LEIAME.md
- [x] `LUMNA_TEMPO=1 lumna-lancador` mostra o tempo de cada etapa da abertura do launcher
  - [x] Conferido pelo Eric no Firefox Developer Edition: a extensão conecta, o login do Google funciona e, no YouTube (desktop), o play/pausa, o +10 s e o -10 s, o início, o voltar e o menu do controle do celular funcionam
  - [ ] Ainda sem teste: o YouTube em formato de TV (setas e OK, que no YouTube de desktop só rolam a página), Widevine (o CDM baixa na primeira vez) e os outros serviços
- [x] Telas em Gaming/Mídia como ilhas no Orbit (fora do Alt+Tab e da dock, sem janelas entrando ou saindo) e "Trocar telas" (Super+Shift+O, botão no Início da Mídia, item no menu de modo da topbar, `swap-screens` no celular)
  - [ ] Conferir na dock de verdade que as janelas das ilhas não aparecem; arrastar com o mouse passando pela ilha; janelas de jogos que o Gaming abre com outro app_id
  - [x] Nunca dois vídeos tocando juntos: sair de um serviço (Home, outro serviço ou o voltar na primeira página) o pausa, e abrir um serviço pausa os outros; as abas seguem vivas para retomar de onde pararam
  - [x] O voltar do controle, no Firefox da Mídia, volta uma página e, na primeira, sai do serviço
  - [ ] Uma miniatura sobrevoando a tela com o vídeo que ficou para trás (hoje ele só pausa)
  - [x] O YouTube em formato de TV: a extensão o faz se anunciar como TV (conferido o mecanismo num site local; falta ver o YouTube de verdade, inclusive o login da TV)
  - [ ] Ícones das marcas (~/.local/share/lumna/midia/icones/ID.svg), Música e Vídeos locais no Início, editar/remover serviços pela tela, limite de abas vivas (a memória, já que a pausa impede dois vídeos juntos)
- [x] Tela de login (lumna-entrada, greetd): Centralizado ou Painel lateral; `./testar-login.sh [lateral]` testa com um greetd de mentira (senha "lumna")
- [ ] Escolher o layout do login pelo Painel (precisa de root: /etc/lumna/entrada.ini)
- [x] Orbit: o que o desktop pedir pelo caminho — ativar, fechar, maximizar e ecrã inteiro (dos apps e da
      dock), ecrã inteiro por cima da barra e da dock, ponteiro virtual e as áreas de trabalho

## 3. Serviços do sistema

- [x] Som (libpulse / pipewire-pulse)
- [x] Rede (NetworkManager por D-Bus, `servicos/rede.c`; sem hardware: `testes/rede-falsa.sh`)
- [x] Bluetooth (BlueZ por D-Bus, `servicos/bluetooth.c`; sem hardware: `testes/bluez-falso.sh`)
- [ ] Energia e bateria (UPower)
- [ ] Brilho e tela

## 4. Apps

- [~] Painel de controle — a estrutura do PAINEL.md: abas Geral, Inicialização e Sistema, busca em tudo,
      usados recentemente, a barra lateral por categoria e Restaurar | Aplicar (a Dock já aplica).
      Em 4 de novembro de 2026: 43 de 74 páginas prontas e 31 esqueleto (o PAINEL.md tem a lista)
- [x] Rede — as páginas do Painel (Wi-Fi, Cabo, Adaptadores, Proxy, Várias conexões) falam com o NetworkManager; faltam VPN e Ponto de acesso
- [x] LUMNA Connect — Pareamento, Área de transferência, Avisos e Arquivos no Painel, ligados ao lumna-connect (com o celular de verdade ainda não foi testado)
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
- [ ] A tela de biblioteca de jogos do Gaming (a Mídia já existe: veja "Mídia", em 2)

## 6. O que veio depois

- [x] A Emma: o serviço em segundo plano (`lumna-emma`), o chat lateral, o app e o painel dela, arquivos e comandos com permissão, código na conversa, as histórias, os perfis SFW e NSFW (com PIN), imagens e visão, "Oi Emma", servidores MCP e a sessão contínua do assistente. O servidor fica em `emma-servidor/` (veja `docs/EMMA.md`)
  - [ ] As páginas Voz, Privacidade e Automações do Painel
  - [ ] Testar na máquina: imagens, servidores MCP de verdade e o Oi Emma com microfone
- [x] Captura de tela (`lumna-captura`): foto e gravação (grim e wf-recorder) da tela, de uma área ou de uma janela, com áudio, o editor de anotações e enviar para a Emma, copiar, o celular ou outro programa (veja `docs/CAPTURA.md`)
  - [ ] Testar na máquina: a gravação com áudio
- [x] Vários monitores no Orbit aninhado (dentro de outro desktop)
  - [x] Uma janela por monitor do desktop de fora, cada uma em tela cheia no seu; `tela_principal` e `telas_de_fora` no `orbit.ini`
  - [x] A disposição das telas: estender, por modo, espelhar, só a da esquerda e só a da direita (`disposicao.ini`; Painel → Telas e modos → Monitores)
  - [x] O mouse no lugar certo em cada janela
  - [ ] Testar no KDE de verdade (foi testado num desktop de fora falso, com dois monitores)
- [ ] O Orbit como a sessão do sistema, e não numa janela
  - [ ] Testar a entrada "LUMNA · Orbit" do login (`session/lumna-orbit.desktop`), que nunca rodou na máquina
  - [ ] A tela principal no Orbit real (a chave `tela_principal` só vale no aninhado)
  - [ ] Desligar um monitor de verdade (no aninhado a tela fica preta)

## 7. O que ainda não temos (lista de 5 de novembro de 2026)

A página de status (docs/status/) tem o detalhe e o estado de cada linha; aqui fica o resumo. Marque `[x]` ao terminar.

- [ ] **Modo Gaming**: a biblioteca de jogos em carrossel (o LUMNA antigo tem a referência: `GameLibrary.vue`), o Início do Gaming no desktop novo, ler Steam, Heroic e Lutris e abrir o jogo, gamepad como controle da interface e o desempenho enquanto joga
- [ ] **A voz da Emma**: a página Voz no Painel (motor, voz, velocidade, microfone e saída, testar), gerenciar os modelos de voz com progresso, o ícone da Emma falando e a voz na tela de bloqueio; testar com o microfone de verdade
- [ ] **As páginas do Painel**: 31 esqueletos (Energia, Som, Inicialização, Idioma, Usuários, Privacidade, as da Emma e as pontas; veja o PAINEL.md)
- [ ] **O app LUMNA Connect (Android) com todo o Connect**: arquivos, área de transferência, avisos, energia do PC, agendamento, senhas e confirmações e a conversa com a Emma
  - [ ] No PC (lumna-connect): atender `info`, `power`, `schedule-*`, `auth-*`, `emma` e `emma-decide`, como o PC antigo atendia
- [ ] **Apps do sistema** (no Gravity): Explorador, player de música, player de vídeo, Galeria, Bloco de notas, LUMNA Code, Reminders, StoryTeller, e outros a decidir (Loja, Telefone, Navegador, visualizador de imagens e PDF, terminal, calculadora, monitor do sistema)

