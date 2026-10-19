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
- **LUMNA Music**: player de música offline (pasta Músicas, capas, fila, cartão na central e no
  bloqueio, botões do fone, Emma) e online (a definir).
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
- Volume por app, viva-voz nas ligações, app de gestos, barra de status
  da cor do app, menu nos apps recentes, refazer o modo Mídia.

## Interface

- Brainstorm da **interface nova**: funcional, lisa e perfeita — antes de mexer, conversar e
  desenhar juntos.
