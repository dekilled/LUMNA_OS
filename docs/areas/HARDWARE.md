# Área: Hardware, drivers e kernel

Leia antes o [LEIAME.md](LEIAME.md).

## Escopo

O que conversa com o hardware e com o sistema por baixo: **discos** (montagem, `lumna-discos`), **rede** (NetworkManager, Wi-Fi), **áudio**
(PipeWire, fones USB), **Bluetooth** (BlueZ, o serviço), **GPU** e variáveis de ambiente, **drivers e kernels** (a Loja de drivers), **boot**
(GRUB, `lumna-boot`, splash, initramfs), a **ISO** e o **instalador**, e serviços do systemd que a sessão liga.

## Onde fica

- Discos: `gravity/exemplos/discos.c` (`lumna-discos`), `gravity/servicos/discos_cfg.[ch]`, `armazenamento.[ch]`, `arquivos_discos.[ch]`.
- Drivers e kernels: `gravity/servicos/loja_hw.[ch]` (Loja, fase 2). GPU: `session/gpu-env`.
- Rede: `session/systemd-dropins/` (drop-ins com `ConditionPathExists=/etc/lumna/permitir-networkd-iwd`), `session/lumna-rede-mesclar`.
- Boot: `packages/lumna-boot/` (gancho `99-lumna-boot.hook`, registro em `/var/log/lumna-boot.log`). Doc: `docs/BOOT.md`.
- ISO e instalador: `iso/` (`build.sh`, `packages.x86_64`, `overlay/`), `gravity/servicos/instalador.[ch]`, `session/lumna-install`.
  Docs: `docs/PLANO-ISO.md`, `docs/SETUP.md`, `docs/PROXIMOS.md` (Instalador).
- Pacotes: `packages/lumna-session/PKGBUILD` (o que a sessão instala e liga), `lumna-session.install`.

## Estado

- Rede: achado em 2026-11-16 que `systemd-networkd` e `iwd` subiam junto com o NetworkManager (dois IPs, duas rotas). Corrigido por drop-ins
  (`7fde32c`, `4ab3c2b`); no PC do Eric o networkd ficou inativo, **mas a rede continua lenta para vídeo** comparada com o BigLinux na mesma
  máquina (speedtest rápido, curl rápido). **Deixado para depois pelo Eric.**
- Boot: `lumna-boot` e GRUB aplicados no PC e funcionando; o gancho foi movido para depois do mkinitcpio (`9de8fff`) e o menu é protegido (`c794ffe`).
- Bluetooth: o fone conecta sozinho ao ligar (confirmado).

## Pendências

1. **Discos não montam no boot** (os que têm jogos). Decisão do Eric: **permissão no Painel que se mantém depois do boot**. Proposta: um ajudante
   pkexec que grava a entrada no `/etc/fstab` (UUID, ponto de montagem, `nofail`, opções certas para ext4/btrfs/ntfs, dono = o usuário), e a
   página Armazenamento do Painel com "Montar ao ligar" por disco. Testar com fstab de mentira. Interface combinada com a área **Painel**.
2. **Vídeos e páginas lentos** (YouTube carrega ~3x menos que no BigLinux): estacionado. Testes já planejados: (A) o servidor do vídeo e o
   "stats for nerds" nos dois sistemas; (B) DNS (`getent`, `ping` no host do googlevideo); (C) UDP/QUIC (`RcvbufErrors` em `/proc/net/snmp`,
   `net.core.rmem_max`).
3. **Travadinhas** (CodeOSS, Atlas): sem prova; anotado, não investigar sem um caso que se repita.
4. **Print e ícones do lançador lentos na primeira vez** (antes era instantâneo): sem diagnóstico ainda (pode ser cache de ícones / gdk-pixbuf).
5. Wi-Fi interno do notebook conecta e não transmite (driver/economia de energia); fone USB (dongle) que às vezes não toca.
6. Splash do boot: o Eric vai diminuir (ele mesmo edita). Tema do GRUB na ISO live: decisão dele.
7. Instalador: particionamento manual, swap para hibernar, perfis, os-prober (ver `docs/PROXIMOS.md`).

## Para o Eric conferir

- [ ] No próximo update de **kernel**: o menu do GRUB continua com a entrada do LUMNA (`cat /var/log/lumna-boot.log`).
- [ ] `coredumpctl list orbit` depois de sair da sessão: não deve aparecer queda nova (o abort ao sair foi corrigido em `31fb1d4`).

## Confirmado no PC

- `lumna-boot` + GRUB; Bluetooth conectando sozinho; networkd inativo depois dos drop-ins (2026-11-16).

## Decisões já tomadas

- NetworkManager é o dono da rede; networkd/iwd só sobem se existir `/etc/lumna/permitir-networkd-iwd`.

## Recebido de outras áreas

- Gaming: os discos com jogos precisam montar no boot (pendência 1).
- Mídia: a lentidão dos vídeos (pendência 2).
