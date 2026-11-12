# Próximos passos

Ideias combinadas para depois, para não se perderem. A ordem não é prioridade.

## Instalador

- **Particionamento manual**: escolher discos, criar/apagar/redimensionar partições, escolher o
  sistema de arquivos e o ponto de montagem de cada uma (hoje o instalador usa o disco inteiro).
- **Pronto para hibernar**: opção de criar swap (partição ou swapfile no Btrfs) do tamanho da
  memória, com `resume` configurado no initramfs e no boot. Hoje o menu de energia só mostra
  "Hibernar" quando o computador já consegue.
- **Tipos de instalação**: simples (o padrão de hoje), completa (com mais apps) e com serviços
  extras (ex.: servidor de mídia, compartilhamento de arquivos, SSH).
- **Perfis de instalação**: conjuntos prontos de escolhas (ex.: "Jogos", "Trabalho", "Mídia na TV")
  que já marcam apps, serviços e o modo inicial.
- **Instalação automática**: um arquivo de respostas (disco, usuário, idioma, teclado, perfil) que
  instala sempre do mesmo jeito, sem perguntas — para repetir a instalação em vários computadores.
- **GRUB com a cara do LUMNA**: tema próprio no lugar do menu simples (fundo, fonte e cores do
  LUMNA, no estilo da Emma).
- **Entrar no GRUB que já existe**: detectar outros sistemas (os-prober) e aparecer no menu de boot
  deles, sem precisar escolher pela BIOS.

## Sistema

- **Animação de boot** (Plymouth) no lugar das mensagens do kernel.
- **Tela de login sem erros no journal** ao entrar (coredumps do Hyprland/shell da tela de login).

## Emma

- **"Oi, Emma"**: ela ouve e abre o assistente em qualquer tela (base: a palavra de ativação do
  NewHarnessV2, `backend/wake.py` com Vosk e o `WakeWord.kt` do Android). Liga/desliga no app.
- Gatilho de automação "computador parado há N minutos".

## Apps

- **LUMNA Browser** (esboços: `esbocos/browser-pc.png` e `esbocos/browser-celular.png`): em cima
  do WebKitGTK que o shell já usa. Abas (no PC em cima; no celular o contador embaixo), barra de
  endereço (no celular embaixo, com microfone), página inicial "Seu espaço na web" com favoritos e
  "Continuar de onde parou" com miniaturas, e a **Emma** do lado (PC) ou por cima (celular):
  resumir a página, encontrar nela, comparar abas, perguntar sobre a página. Enviar a aba pro PC
  pelo Connect. Enquanto não sai: testar o Epiphany.
- **LUMNA Music online** (a definir). O offline já existe: pasta Músicas, álbuns/artistas, fila,
  cartão na central e no bloqueio, MPRIS (fone, Bluetooth, Emma).
- **Player de vídeo**: o mpv por baixo (hardware, formatos, legendas) com os controles do LUMNA.
- **LUMNA Connect gerenciando o aparelho**: ao conectar o celular, mostra modelo/versão/slot;
  backup e restauração; atualizar com progresso (o `atualizar.sh`); reinstalar/recuperar pelo
  fastboot, só com o modelo confirmado e sempre no slot B.

## Celular

- **Tela dividida (dois apps)**: um em cima e outro embaixo (ex.: o vídeo no mpv e a Galeria),
  com a divisória arrastável pra mudar a proporção, trocar de lugar e voltar pra um só. Entrar
  pelos recentes ("dividir com…") ou por gesto. Planejar o desenho antes (apareceu sem querer,
  pelo Hyprland, abrindo o mpv por cima da Galeria).
- **Teclado que abre sozinho nos apps de fora** (input-method-v2 do Wayland): hoje só abre sozinho
  nos campos do próprio LUMNA.
- Viva-voz nas ligações, app de gestos, barra de status da cor do app, refazer o modo Mídia.

## Interface

- Brainstorm da **interface nova**: funcional, lisa e perfeita — antes de mexer, conversar e
  desenhar juntos.

## Gerenciador de processos (lumna-processos) — em andamento

Esboços do Eric: Processos (lista com Nome/PID/CPU/Memória/GPU/Disco, busca, filtro, "Encerrar processo", árvore), Desempenho (CPU, Memória, GPU, discos, rede), Aplicativos de inicialização e Serviços.

Decidido: atalho **Ctrl+Shift+Esc** (o Orbit nunca o entrega a um jogo; no Gaming, o botão Guia do controle também); serviço **residente** com prioridade de tempo real (SCHED_FIFO 50) e protegido do OOM killer, que só mostra a janela quando chamado; senha do **polkit uma vez por sessão** para processos do sistema.

Prioridades: Baixa (nice 10, E/S ociosa), Média (0), Alta (-5), Máxima (-15), Tempo real (SCHED_RR 20); encerrar = SIGTERM ou SIGKILL por pidfd (sem corrida de PID), do processo ou da árvore dele.

Feito: `servicos/processos.c` (ler /proc, CPU, encerrar, prioridade, proteger o próprio processo) + `lumna-processos-ajudante` (pkexec) + política `os.lumna.processos`, com testes em processos reais.

Feito (tela Processos): `lumna-processos` com o serviço residente (`lumna-processos servico`, chamado pelo Orbit no Ctrl+Shift+Esc e iniciado na sessão), a janela overlay (lista agrupada por aplicativo com filhos, busca que abre os grupos, filtro, ordenação, Encerrar/Forçar/prioridades, confirmação para processos da sessão, ajudante por pkexec numa linha à parte) e o teste `testes/processos_app.sh` num Orbit headless. Desempenho: cinco cartões com gráfico dos últimos 2 min (CPU, Memória, GPU via sysfs amdgpu/i915, Disco, Rede recebendo/enviando). Gaming: botão Guia do controle com a opção "gerenciador de processos" (não é o padrão: o padrão continua "biblioteca") e o cartão "Fechar jogo" (acha o jogo pelo SteamGameId no ambiente, SIGKILL só na árvore do jogo; Lutris/Heroic ainda não). A janela é uma camada que cobre a tela, mas só o cartão recebe o mouse (o resto passa para o jogo ou o app de baixo): arrastar pela barra, redimensionar por bordas e cantos (mínimo 960x540), maximizar (botão ou duplo clique na barra), minimizar (vira uma pastilha no canto, que devolve o teclado ao jogo) e lembrar tamanho e posição (`~/.config/lumna/processos.ini`). O atalho (Ctrl+Shift+Esc) com a janela minimizada a traz de volta; aberta, fecha. A lista é virtualizada (só desenha as linhas à vista): o Clay tem teto de 8192 elementos por quadro e centenas de processos estouravam o layout. Inicialização é só um atalho na lateral: abre o Painel em Inicialização > Apps ao iniciar (`lumna-painel --secao inicializacao/apps`) e fecha o gerenciador, que cobriria a janela. Serviços (`servicos/sdservicos.c`, só leitura): lê o `systemctl` do sistema e da sessão (`--user`) e separa em Em execução, Parados (inclui os que falharam, em vermelho), Programados (timers ativos, com a próxima execução) e Em pausa (`FreezerState=frozen`, o `systemctl freeze`), com busca, contagem por estado e atualização a cada 5 s. Ações nos serviços (marque a linha e use os botões do rodapé): Iniciar, Parar, Reiniciar e Pausar/Retomar (`freeze`/`thaw`). Os da **sessão** vão direto pelo `systemctl --user` (são do próprio usuário); os do **sistema** passam pelo `lumna-processos-ajudante servico AÇÃO UNIDADE` (pkexec, senha uma vez por sessão). Parar, reiniciar e pausar serviço do sistema pedem confirmação. O ajudante só aceita as cinco ações, unidades `.service`/`.timer` de nome simples (nada que o systemctl leia como opção ou caminho), roda o `systemctl` sem shell e recusa parar, reiniciar ou pausar o que derrubaria a sessão (dbus, logind, journald, udevd, polkit, greetd e gerenciadores de login, `user@`, `getty@`). Não faz: habilitar/desabilitar na inicialização, nem mascarar. **Falta:**  botão Guia do controle e "fechar jogo" no Gaming, e confirmar no PC real (senha do pkexec, jogo travado de verdade, oom_score_adj -900, que o ambiente de teste não deixa gravar).
