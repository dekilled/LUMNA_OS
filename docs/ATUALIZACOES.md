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
- o resultado fica em `/var/log/lumna-atualizar.log` (até 512 KB) e em `/var/lib/lumna/atualizar.estado` (`ok`, `nada`, `precisa-completa` ou `falhou`, mais os pacotes do LUMNA atualizados e, no escopo "tudo", quantos pacotes), que a página mostra ("Última vez: ...") e o aviso na tela usa;
- roda com `Nice=19` e IO ocioso, para não atrapalhar quem está usando.

## A página

No topo: "N atualizações (X do LUMNA e Y do Arch e de outros programas)", **Procurar**, **Só o LUMNA** (só aparece se há atualização do LUMNA) e **Atualizar tudo**. Embaixo, **Atualização agendada**: interruptor, o que atualizar (Tudo | Só o LUMNA), os dias (sete botões) e o horário (de 30 em 30 minutos). A lista marca as atualizações do LUMNA.

## Testes

- `sh gravity/testes/pkg/lumna.sh`: o `lumna-pkg` com pacman/systemctl de mentira (com controle negativo): só instala o do LUMNA, não sincroniza o Arch, para quando exige o Arch, recusa entradas inválidas, estado e log do agendado, escopo "tudo".
- `sh gravity/testes/pkg/aviso.sh`: o vigia, com `gdbus` de mentira (primeira vez, avisa uma vez, não repete, "tudo" com a contagem, "nada" não avisa, adiada, falhou crítica, `gdbus` falhando não marca como visto, aspas no texto). **Também testado com o `lumna-avisos` de verdade e o `gdbus` de verdade** num Orbit sem tela (`dbus-run-session`): o aviso aparece no canto da tela com o texto certo.
- `sh gravity/testes/painel.sh` (parte `sistema`): a marcação "é do LUMNA" e a leitura do agendamento.
- `sh gravity/testes/painel_agenda.sh <orbit>`: o Painel de verdade num Orbit sem tela; confere a configuração mostrada e o que os cliques (dia, Tudo, interruptor) mandam.

**Não testado:** um `pacman` real com o repositório `[lumna]` publicado (só com pacman de mentira); o timer rodando de verdade no systemd (só o conteúdo dos arquivos); o vigia do aviso rodando de verdade na sessão do PC (só o script, de mentira e contra o lumna-avisos num Orbit sem tela).

## O aviso na tela quando o agendado termina

O serviço roda como root, mas o servidor de avisos do LUMNA (`lumna-avisos`, o `org.freedesktop.Notifications`) mora no **barramento privado da sessão** (o Orbit sobe com `dbus-run-session`), que o root **não alcança**. Por isso quem avisa é o vigia
**`lumna-atualizar-aviso`** (`/usr/bin`, lançado na sessão em `lumna-orbit-session`), que roda como a pessoa e olha o `atualizar.estado` a cada minuto (a primeira checagem, 10 s depois do login):

- se o agendado rodou **depois da última vez em que a pessoa foi avisada** (`~/.local/state/lumna/atualizar-visto`), mostra o resultado pelo `gdbus` (sem dependência nova: vem com o glib): **concluída** (o que atualizou; lembra de sair da sessão ou reiniciar), **adiada** (o LUMNA pedia pacotes do Arch mais novos; use "Atualizar tudo") ou **falhou** (urgência crítica, aponta o log e o "Desfazer a última atualização");
- **"nada a atualizar" não avisa** (não incomoda);
- cobre o **PC desligado na hora marcada**: o `Persistent=true` roda o agendado ao ligar, e o aviso sai assim que a pessoa entra e o vigia sobe (o resultado ficou no `atualizar.estado`);
- na primeira vez numa conta só guarda o que já existe (um resultado antigo não vira aviso); se o `gdbus` falha (o `lumna-avisos` ainda não subiu), **não marca como visto** e tenta no ciclo seguinte; nunca avisa duas vezes o mesmo resultado.

**Só na sessão nova (Orbit).** A sessão clássica (Hyprland) não lança o vigia.

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

**Não testado:** um `pacman` real com o repositório `[lumna]` publicado e atualizações de verdade; o `checkupdates` real (o fake imita a saída); o timer no systemd; o tempo da análise num PC com muitas atualizações (cada pacote é simulado, centenas podem levar minutos: a tela avisa "Testando os pacotes um por um"); o ícone `lumna` do aviso (este container não tem o tema de ícones instalado: aparece um quadrado no lugar; no PC deve aparecer o do LUMNA, o mesmo do aviso de configuração inicial).
