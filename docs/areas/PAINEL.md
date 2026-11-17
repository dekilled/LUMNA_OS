# Área: Painel de controle

Leia antes o [LEIAME.md](LEIAME.md).

## Escopo

Todas as páginas do Painel (`lumna-painel`): Aparência, Animações, Papel de parede, Barra, Dock, Monitores, Som, Rede, Bluetooth, Energia,
Teclado e teclado na tela, Idioma, Data e hora, Usuários, Privacidade, Inicialização, Modos, Emma, Connect, Armazenamento, Sobre e as
**Atualizações** (com o ajudante `lumna-pkg`). O pré-setup do primeiro login e a animação pós-login (o interruptor fica no Painel).
Quando uma página só mostra o que outra área faz (ex.: a montagem de discos), a **interface** é daqui e o **mecanismo** é da outra área.

## Onde fica

- Páginas: `gravity/exemplos/painel/*.c` (uma por página; `painel.h`, `comum.c`, `categorias.c`). Plano e lista do que existe: `gravity/PAINEL.md`.
- Serviços por trás: `gravity/servicos/` (`sistema.c` = Sobre/Atualizações, `som.c`, `rede.c`, `bluetooth.c`, `energia.c`, `luz.c`, `autostart.c`...).
- Atualizações: `session/lumna-pkg` (ajudante root via pkexec), `session/lumna-pkg-analisar`, `session/pacotes-base.txt`,
  `session/lumna-atualizar.{service,timer}`, `session/lumna-atualizar-aviso`. Doc: `docs/ATUALIZACOES.md`.
- Testes: `gravity/testes/painel.sh`, `painel_agenda.sh`, pasta `gravity/testes/painel/`.

## Estado

- Atualizações (Painel > Sistema > Sobre): **"só o LUMNA"**, **escolher pacotes** e **horário marcado**: o Eric confirmou que funcionam (2026-11-17).
- Animações (página própria, nível + ajuste fino), Inicialização (modo inicial, apps ao iniciar, login automático, serviços do usuário): feitos.

## Pendências

1. **Aviso na tela quando a atualização agendada termina** (`lumna-atualizar-aviso`, vigia dentro da sessão): o Eric está esperando o horário
   (21 h) para ver. Se não aparecer: `journalctl --user -b | grep -i atualizar` e `systemctl status lumna-atualizar.timer`.
2. **Bluetooth:** existe a página (ligar, procurar, parear, conectar, esquecer, nome e visibilidade). O Eric sente falta de **gerenciar as conexões**
   no dia a dia e de **enviar e receber arquivos** (e bateria do aparelho). Conferir com ele o que falta na página antes de fazer app novo.
3. **Permissão "montar este disco ao ligar"** (pedido do Eric para os discos de jogos): a página é daqui (Armazenamento), o mecanismo é da área
   **Hardware**. Combinar entre as duas.
4. Animação pós-login (`lumna-abertura`): falta o Eric ver se roda bem no notebook fraco (TESTES-ISO 2.477).
5. Teclado na tela (`lumna-teclado`): só testado headless; falta tela de toque de verdade, Firefox/GTK.

## Para o Eric conferir

No registro de testes: `python3 docs/testes/teste.py lista Painel` (pendentes) e `lista Painel falhou`. Os itens recentes:

- [ ] O aviso do agendado às 21 h apareceu?
- [ ] O que falta na página Bluetooth para o uso do dia a dia (lista do que ele quer).

## Confirmado no PC

- Atualizações: só o LUMNA, escolher pacotes, agendamento (2026-11-17).

## Decisões já tomadas

- O Arch não suporta atualização parcial: o "só o LUMNA" baixa só o `lumna.db`, simula antes e **não instala nada** se a conta puxar pacote do Arch.
- O banco do `[lumna]` baixado fica 644 (com 600 o pacman de usuário não via as atualizações).
- Animações: página própria, com a versão simples para máquina fraca.

## Recebido de outras áreas

(vazio)
