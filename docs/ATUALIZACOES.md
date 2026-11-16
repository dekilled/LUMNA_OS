# Atualizações do LUMNA: "só o LUMNA" e o horário marcado

Pedido do Eric (2026-11-16): o Arch atualiza toda hora ("atualizo e 2 min depois já tem outra"). O LUMNA ganhou: (1) **atualizar só o LUMNA**, (2) **atualizar em horário marcado** e,
numa segunda etapa (ainda não feita), (3) **escolher quais pacotes** atualizar. Onde: **Painel > Sistema > Sobre > Atualizações do sistema** e o ajudante `session/lumna-pkg`.

## Por que não é "atualizar só alguns pacotes" do jeito simples

O Arch **não suporta atualização parcial**: um pacote novo com as bibliotecas velhas (ou o contrário) pode quebrar o sistema. Por isso o "só o LUMNA" **não sincroniza o Arch**:

- baixa **apenas o `lumna.db`** (o banco do repositório `[lumna]`) para `/var/lib/pacman/sync`; os bancos do Arch ficam como estavam na última atualização completa;
- calcula as atualizações = o que o `pacman -Qu` acha ∩ `pacman -Slq lumna`;
- **simula** (`pacman -S --needed --print`) antes de instalar: se o pacman recusar a conta, ou se a conta incluir **qualquer pacote que não seja do `[lumna]`**, **não instala nada**, diz quais pacotes do Arch seriam necessários e manda "atualizar tudo" (código de saída 3);
- só então tira o snapshot Btrfs (os 5 últimos ficam; "Desfazer a última atualização" na mesma página) e instala só os pacotes do LUMNA.

## O horário marcado

Um timer do systemd (`lumna-atualizar.timer`, `Persistent=true`: **se o PC estava desligado na hora, roda quando ligar**) chama `lumna-pkg agendado` como root:

- a configuração vem de `/etc/lumna/atualizar.conf` (`modo`, `escopo` = `tudo`|`lumna`, `hora` = HH:MM, `dias` = `todos`|`seg,qua,...`), gravada por `pkexec lumna-pkg agendar LIGADO ESCOPO HH:MM DIAS` (a entrada é **validada** à risca: roda como root); o horário vai para `/etc/systemd/system/lumna-atualizar.timer.d/horario.conf`;
- escopo "tudo": snapshot + `pacman -Syu`; escopo "lumna": o de cima. **Não atualiza o AUR** (precisa de senha e de uma pessoa);
- o resultado fica em `/var/log/lumna-atualizar.log` (até 512 KB) e em `/var/lib/lumna/atualizar.estado` (`ok`, `nada`, `precisa-completa` ou `falhou`), que a página mostra ("Última vez: ...");
- roda com `Nice=19` e IO ocioso, para não atrapalhar quem está usando.

## A página

No topo: "N atualizações (X do LUMNA e Y do Arch e de outros programas)", **Procurar**, **Só o LUMNA** (só aparece se há atualização do LUMNA) e **Atualizar tudo**. Embaixo, **Atualização agendada**: interruptor, o que atualizar (Tudo | Só o LUMNA), os dias (sete botões) e o horário (de 30 em 30 minutos). A lista marca as atualizações do LUMNA.

## Testes

- `sh gravity/testes/pkg/lumna.sh`: o `lumna-pkg` com pacman/systemctl de mentira (com controle negativo): só instala o do LUMNA, não sincroniza o Arch, para quando exige o Arch, recusa entradas inválidas, estado e log do agendado, escopo "tudo".
- `sh gravity/testes/painel.sh` (parte `sistema`): a marcação "é do LUMNA" e a leitura do agendamento.
- `sh gravity/testes/painel_agenda.sh <orbit>`: o Painel de verdade num Orbit sem tela; confere a configuração mostrada e o que os cliques (dia, Tudo, interruptor) mandam.

**Não testado:** um `pacman` real com o repositório `[lumna]` publicado (só com pacman de mentira); o timer rodando de verdade no systemd (só o conteúdo dos arquivos); o aviso na tela quando o agendado termina (hoje o resultado só aparece na página e no log).

## Falta (etapa 2)

**Escolher pacotes**, com as caixinhas: cada pacote **simulado** antes (se arrastar outros pacotes ou quebrar dependência: ⚠ e bloqueado; kernel, glibc, systemd, mesa e outros de base sempre bloqueados), snapshot antes. Decisão do Eric: "simular e travar".
