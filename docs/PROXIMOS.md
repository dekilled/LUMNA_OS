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

- Voz ("Oi, Emma").
- Gatilho de automação "computador parado há N minutos".

## Interface

- Brainstorm da **interface nova**: funcional, lisa e perfeita — antes de mexer, conversar e
  desenhar juntos.
