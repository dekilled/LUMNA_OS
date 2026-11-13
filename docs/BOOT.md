# Visual de boot do LUMNA (GRUB e animação)

O pacote **`lumna-boot`** traz o tema do GRUB e a animação de boot (Plymouth), e o ajudante **`lumna-boot-tema`** que os aplica. Está na ISO (o instalador
já aplica no sistema novo), mas num sistema já instalado ele **não liga sozinho**: mexer no carregador de boot é decisão da pessoa.

```
sudo pacman -S lumna-boot            # (o repositório do LUMNA; se já está instalado, pacman -Syu)
sudo lumna-boot-tema aplicar         # tema do GRUB + animação de boot; regera o menu e o initramfs
sudo lumna-boot-tema desfazer        # tira tudo e volta ao que era
sudo lumna-boot-tema grub|plymouth   # só uma das partes
```

## O que o `aplicar` mexe (e o que não)

- **GRUB:** copia o tema para `/boot/grub/themes/lumna`; em `/etc/default/grub` põe `GRUB_THEME`, `GRUB_GFXMODE=1920x1080,auto`, `GRUB_GFXPAYLOAD_LINUX=keep`,
  `GRUB_TERMINAL_OUTPUT=gfxterm` e acrescenta `splash` na linha do kernel; regera o `grub.cfg` (em português se o sistema tem o idioma) e põe a classe de ícone
  nas **Opções avançadas** e no **firmware UEFI**. **Não mexe** na detecção de outros sistemas (`GRUB_DISABLE_OS_PROBER`), no tempo do menu nem nas entradas que
  a pessoa já tem (as de Windows e BigLinux ficam como estão: cada uma ganha o ícone pela classe que já tem; sem ícone próprio, o do Linux genérico).
- **Plymouth:** põe o gancho `plymouth` do `mkinitcpio` logo depois do `udev` (ou do `systemd`), `Theme=lumna` em `/etc/plymouth/plymouthd.conf` e refaz o initramfs.
  O pacote também traz um complemento do `greetd` (`greetd.service.d/plymouth.conf`): a animação fica até o login aparecer.
- Um gancho do pacman (`90-lumna-boot.hook`) refaz o menu e as classes depois de um kernel novo ou do `grub`, só em quem aplicou o tema.

## O tema do GRUB

A arte (`iso/branding/GRUB_BACKGROUND.png`, 1920x1080: fundo, logo, painel com título e dicas) é o fundo; o GRUB desenha por cima só os itens, com o destaque em
gradiente de cantos arredondados, os ícones (LUMNA, avançadas, firmware, Windows, Ubuntu, Linux genérico) e a fonte Inter. Limites: o GRUB **não anima** e não
borra o fundo (o "vidro" é da imagem); não há o visto (✓) no item selecionado; o painel tem a posição da arte, então fora de 16:9 sobram margens (por isso o
menu pede 1920x1080 e cai para `auto` se a tela não tiver). O título "GRUB BOOT MENU" está na arte. Regerar: `python3 iso/branding/grub/gera-tema.py`
(precisa de Pillow e do `grub-mkfont`); o resultado em `packages/lumna-boot/grub/lumna/` é versionado.

## A animação de boot

Do vídeo `iso/branding/BOOT_ANIMATION.mp4` (1280x720, 24 fps) o `iso/branding/plymouth/gera-tema.py` tira, em vez de 240 quadros, uns 880 KB de imagens:
a **placa** estática (mediana no tempo: fundo, logo, textos, rodapé), o **brilho da gema** (pulsa por cima da placa, ~5,2 s), o **ponto** da órbita (posição por
matemática: elipse inclinada, período ~5,2 s) e o **anel** de carregamento (57 quadros pequenos: o arco cresce e encolhe enquanto gira). O script do Plymouth
(`lumna.script`) escala a placa para caber em qualquer tela (pela altura, ou pela largura se a tela for mais estreita que 16:9) e anima a 30 quadros/s, com um fade
de entrada de 0,5 s. O **áudio** do vídeo é silêncio e o Plymouth não toca som. Regerar: `python3 iso/branding/plymouth/gera-tema.py` (ffmpeg, numpy, Pillow).

Testado: o tema rodou numa **VM de verdade** (kernel e initramfs com o Plymouth, GPU virtio, 1920x1080) e a imagem bate com o vídeo; o GRUB com o tema rodou no QEMU
com UEFI (OVMF), com as setas e a rolagem. **Não testado** (só no PC): a troca da animação para a tela de login (`plymouth deactivate`/`quit --retain-splash` no
greetd), o `mkinitcpio` e o `grub-mkconfig` de verdade num Arch instalado, e o desempenho em placas reais. Teste do ajudante: `iso/testes/boot_tema.sh` (raiz de mentira).

## O que ficou para depois

- A animação de **login** (o segundo vídeo, com a voz da Emma): a ideia é uma sequência só do boot até a área de trabalho.
- O tema na **ISO live** (o GRUB UEFI do archiso aceitaria o mesmo tema; o syslinux do BIOS só aceita um fundo simples). Hoje a live mantém os menus do Arch.
