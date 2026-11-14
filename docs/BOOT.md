# Visual de boot do LUMNA (GRUB e animação)

O pacote **`lumna-boot`** traz o tema do GRUB e a animação de boot (Plymouth), e o ajudante **`lumna-boot-tema`** que os aplica. Está na ISO (o instalador
já aplica no sistema novo), mas num sistema já instalado ele **não liga sozinho**: mexer no carregador de boot é decisão da pessoa.

```
sudo pacman -S lumna-boot            # (o repositório do LUMNA; se já está instalado, pacman -Syu)
sudo lumna-boot-tema aplicar         # tema do GRUB + animação de boot; regera o menu e o initramfs
sudo lumna-boot-tema desfazer        # tira tudo e volta ao que era
sudo lumna-boot-tema grub|plymouth   # só uma das partes
lumna-boot-tema diagnostico          # se o tema ou a animação não aparecem: mostra tudo o que decide isso (sem root; cole a saída)
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

## Initramfs no formato udev (por que a animação não aparecia no boot)

Sintoma, igual no desktop (amdgpu, UEFI) e no notebook do Eric (duas radeon, BIOS): a animação só aparecia no desligamento; no boot ficava uma tela cinza ou azul com três pontinhos. O log mostrava o `plymouthd` abrindo no `simpledrm` (framebuffer genérico) e o driver real entrando um segundo depois e tirando a tela dele. No initramfs com o gancho `systemd` o Plymouth inicia por ordem de serviço, antes do driver. No formato `udev`, com o gancho `plymouth` **depois do `kms`** (o que o BigLinux faz), o driver já está carregado quando o Plymouth abre.

O `lumna-boot-tema plymouth` agora converte a lista de ganchos da pessoa: `systemd`->`udev`, `sd-vconsole`->`keymap consolefont`, `sd-encrypt`->`encrypt`, `sd-lvm2`->`lvm2`, garante o `kms` e põe o `plymouth` depois dele e antes de `encrypt`/`lvm2`/`filesystems`. A lista original fica numa linha `#LUMNA_HOOKS_ORIGINAL=` no `mkinitcpio.conf` e o `desfazer` a devolve. Teste: `iso/testes/boot_tema.sh`. Confirmado à mão nos dois PCs com os ganchos `base udev autodetect microcode kms modconf block keyboard keymap consolefont plymouth filesystems`; a conversão automática ainda não foi rodada neles.

**Lições (2026-11-13/14):** o `MODULES=(radeon)` no initramfs apagou a tela do notebook e não foi a solução (o ajudante não mexe mais no `MODULES`). Recuperação se a tela ficar azul/preta com o splash: `scripts/notebook-restaurar-video.sh` pelo live, ou no GRUB (`e`) tirar o `splash`.

## O novo visual do GRUB (2026-11-14, o esboço do Eric)

O Eric refez o desenho (iso/branding/GRUB_MOCKUP.png): painel escuro à esquerda com **MENU DE INICIALIZAÇÃO**, a lista com o destaque em gradiente azul-marinho para violeta, ícones (o losango do LUMNA, a engrenagem das opções avançadas, a bandeira do Windows),
a barra de início automático fina embaixo com a contagem, e a nebulosa com o logo à direita (a arte vazia, GRUB_BACKGROUND.png, leva também a faixa "Selecione o seu sistema" e as dicas de teclas). Fonte: Open Sans.
- **Gerar o tema:** `python3 iso/branding/grub/gera-tema.py` (precisa de Pillow, grub-mkfont e fonts-open-sans). **Ver como fica de verdade:** `sh iso/branding/grub/renderiza.sh foto.png` (sobe um GRUB EFI no QEMU com OVMF e tira uma foto da tela);
  contra o esboço, a diferença média ficou em 3,6 de 765 (1,4 na arte).
- **Títulos:** o helper reescreve os títulos do `grub.cfg` como no desenho: em maiúsculas e curtos (LUMNA OS, OPÇÕES AVANÇADAS, WINDOWS, CONFIGURAÇÕES DA UEFI; o " Linux" do fim e o "(em /dev/...)" saem). Só com o tema aplicado.
- **Limites do GRUB** (medidos): o destaque passa da área do item pelas bordas do 9-slice (20 dos lados, 12 em cima e embaixo) e o conteúdo do item selecionado desce 12 px, então o passo entre a 1ª e a 2ª linha é 12 px menor que entre as outras (rows 1 e 3 batem com o esboço, a 2ª fica ~5 px acima);
  a barra de início tem altura mínima de 28 px (o trilho de 12 px está no fundo e o preenchimento é um pixmap com margem transparente); o `item_*` e as porcentagens são fixos em 1920x1080 (em telas menores o fundo estica, e o item não).
- Quem já aplicou o tema precisa rodar **`sudo lumna-boot-tema aplicar`** de novo (o pacote não mexe no carregador sozinho).
