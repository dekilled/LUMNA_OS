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

## Escolher pacotes (etapa 2)

Na mesma página, o botão **Escolher** abre uma lista com caixinhas. Decisão do Eric: **"simular e travar"**.

**A análise (sem senha).** `session/lumna-pkg-analisar` usa o mesmo banco temporário do `checkupdates` (uma cópia sincronizada, sem tocar no banco do sistema) e **simula** cada pacote **sozinho** (`pacman -S --needed --print`).
Cada pacote cai em uma situação:

| Situação | O que significa | Pode marcar? |
|---|---|---|
| **ok** | atualiza sozinho (se pedir dependências *novas*, avisa: "Instala também (novos): ...") | sim |
| **arrasta** | puxaria a atualização de **outros pacotes já instalados** ("Puxaria também: glibc") | não |
| **quebra** | o pacman recusa (quebraria uma dependência: "installing X breaks dependency ... required by Y") | não |
| **base** | sistema base: kernel, firmware, glibc, systemd, mesa, drivers, wayland, wlroots, pacman... (a lista em `session/pacotes-base.txt`, com padrões `*`/`?`); **nem é simulado** | não, só com "Atualizar tudo" |

A lista de base é de propósito mais conservadora que a simulação: ela cobre o que **segue funcionando na simulação e mesmo assim** deixa o PC sem tela ou sem boot quando fica fora de harmonia.

**A instalação (com senha).** `pkexec lumna-pkg upgrade-pacotes PACOTE...`: valida os nomes (roda como root), **recusa o base de novo** (não confia na interface), sincroniza um **banco privado** em `/var/lib/lumna/pacman-db`
(o banco do sistema, com o retrato do Arch da última atualização completa, **não é tocado**; o banco de instalados é o do sistema, por um link), confere quais dos escolhidos têm atualização, **simula de novo** (a análise pode ser de minutos
atrás) e só instala se a conta fecha: se puxar um pacote **já instalado** que não foi escolhido, ou se o pacman recusar, **não instala nada** (código 3). Tira o snapshot antes. Depois o Painel procura de novo.

## Testes

- `sh gravity/testes/pkg/lumna.sh`: "só o LUMNA" e o agendamento (com controle negativo).
- `sh gravity/testes/pkg/pacotes.sh`: o analisador (ok com dependência nova, arrasta, quebra, base, nada a atualizar, checkupdates falhando) e o `upgrade-pacotes` (só os escolhidos, pelo banco privado, snapshot, recusa de arrasto, de recusa do pacman, do base, nomes inválidos, quem já está em dia, `db.lck`, sync falhando). Com controles negativos (sem a recusa de arrasto e sem a de base, o teste falha).
- `sh gravity/testes/painel.sh` (parte `sistema`): a marcação "é do LUMNA", a leitura do agendamento e a leitura da saída do analisador.
- `sh gravity/testes/painel_agenda.sh <orbit>` e `sh gravity/testes/painel_escolher.sh <orbit>`: o Painel de verdade num Orbit sem tela: o que os cliques mandam (dia, Tudo, interruptor; marcar, desmarcar, "Marcar os liberados", **bloqueado não marca**, "Atualizar" manda só os marcados). Controle negativo da interface feito.

**Não testado:** um `pacman` real com o repositório `[lumna]` publicado e atualizações de verdade; o `checkupdates` real (o fake imita a saída); o timer no systemd; o tempo da análise num PC com muitas atualizações (cada pacote é simulado, centenas podem levar minutos: a tela avisa "Testando os pacotes um por um"); o aviso na tela quando o agendado termina.
