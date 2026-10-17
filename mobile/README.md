# LUMNA OS no celular

O mesmo LUMNA do PC (Arch + Hyprland + lumna-shell) num celular, começando pelo OnePlus 6T.
Esta pasta cuida do aparelho: kernel, initramfs, rootfs Arch Linux ARM e a gravação.

A ideia é um Linux nativo (sem chroot dentro do Android) que roda pacman, AUR, Flatpak e APK.
O que for fechado a gente reaproveita do Android ou contorna com driver mínimo.

> Status: **o Arch roda no OnePlus 6T de verdade.** Kernel 7.1-rc1 sdm845, tela, GPU (Adreno 630,
> `renderD128`), UFS, touch, vibração, botões e rede USB funcionando; systemd sobe e o SSH entra em
> `alarm@lumna`. Falta: interface gráfica, bateria/carregador no rootfs (testar), Wi-Fi, modem, áudio.
> O Z5 Pro GT está pausado (o kernel compila e o boot.img sai com header v1).

## A interface

1. ✅ Hyprland no 6T: tela, toque e GPU Adreno 630 pelo freedreno (`session/primeira-tela.sh`).
2. ✅ `lumna-shell --mobile` rodando no 6T (a bateria já aparece na barra de status): barra de status, Início (relógio e "Pergunte à Emma"), aba Apps e
   barra de navegação (Início, Apps, Arquivos, Ajustes, Emma). Arquivos, Ajustes e Emma abrem os
   mesmos apps do PC. Compila no celular com `sh mobile/session/lumna-mobile.sh`.
3. LUMNA Keyboard (`shell/src/components/mobile/MobileKeyboard.vue` + `touchkeys.rs`): letras ABNT
   com ç, duas páginas de símbolos e acentos. A barra de cima tem três camadas: ferramentas (Ajustes,
   Emojis, Copiados; a padrão), sugestões (aparece ao digitar, aprende as palavras) e atalhos (Esc,
   Tab, Ctrl, Alt, Home, End; aparece no terminal e nos editores). A grade alterna entre elas. Abre sozinho nos campos do LUMNA; nos
   outros apps, toque na barrinha de baixo. As teclas chegam ao app pelo teclado virtual do Wayland (wtype).

   | Ferramentas | Terminal | Emojis | Copiados |
   |---|---|---|---|
   | ![](docs/teclado-letras.png) | ![](docs/teclado-terminal.png) | ![](docs/teclado-emojis.png) | ![](docs/teclado-clipboard.png) |
4. Falta: gestos, Arquivos e Ajustes no formato do celular, tela de bloqueio.

![LUMNA no OnePlus 6T](docs/6t-primeiro-boot.jpg)

| Início | Apps |
|---|---|
| ![Início](docs/celular-inicio.png) | ![Apps](docs/celular-apps.png) |

(Renderizado no navegador em 432×936, o tamanho lógico da tela do 6T com escala 2.5.)

## Internet e chamadas

✅ **Wi-Fi funcionando no 6T** (WCN3990 pelo ath10k, conectado pelo NetworkManager) e o modem
ligado. ✅ **Chip registrado na rede** (Vivo, LTE, 75% de sinal) depois do `lumna-sim.service`.
✅ **Primeira ligação recebida**: um Android ligou pro Arch Linux no 6T, com o nome do contato na
tela de chamada do LUMNA.

![Primeira ligação recebida no LUMNA](docs/primeira-ligacao.jpg)
Falta: dados móveis (APN) e o áudio das chamadas.

`session/modem-wifi.sh` (no celular) compila do AUR a pilha da Qualcomm que o postmarketOS usa
(qrtr, rmtfs, pd-mapper, tqftpserv), põe a calibração do Wi-Fi do OnePlus e liga o ModemManager.
O Wi-Fi do 6T só sobe com o modem rodando. Depois de reiniciar: `sh session/modem-wifi.sh status`.

Chamadas: o modem atende pelo ModemManager (`mmcli`); falta o áudio da ligação (q6voiced + UCM
do sdm845) e um discador. Em operadora que só aceita VoLTE, a voz ainda é experimental no mainline.

Gestos: deslizar a barra de status pra baixo abre a **central** (notificações e atalhos: Wi-Fi,
Bluetooth, dados móveis, som, lanterna, não perturbe, modo avião, economia, brilho e volume);
na barrinha de baixo, toque ou deslize curto abre o teclado e o deslize longo abre os **apps recentes**, com a memória que cada app usa
(arrastar o cartão pra cima fecha; segurar dá "Forçar parada").

Telefone: o app Telefone (teclado, recentes e contatos) liga pelo ModemManager; uma chamada
chegando abre a tela de ligação por cima de tudo e o celular vibra. O áudio (alto-falante, fone e
o som da ligação) instala com `sh mobile/session/audio.sh`: PipeWire, o UCM do sdm845-mainline e o
`lumna-voiced`, que abre o PCM de voz do DSP durante a chamada (o shell troca a placa pro perfil
"Voice Call" sozinho). Conferir: `sh mobile/session/audio.sh status`. Em Ajustes > Dados móveis: APN, roaming, tipo de rede e o uso do mês.

Bloqueio: a tela de bloqueio do LUMNA (senha do Linux, conferida pelo `unix_chkpwd`; o LUMNA
Keyboard digita nela). Botão power: toque apaga a tela e bloqueia, segurar abre o menu de energia,
e tocando uma ligação ele silencia. O tempo parado até apagar fica em Ajustes > Tela e aparência
(hypridle). O logind não cuida mais do botão (`/etc/systemd/logind.conf.d/50-lumna-power.conf`).

Celular Connect: o celular conversa com o LUMNA do PC (WebSocket na porta 47800, achado pelo
mDNS). Pareamento: no PC, Configurações > Celular mostra um código de 6 dígitos; no celular,
Ajustes > Celular Connect > Adicionar computador. Pareado, chegam no celular os avisos do PC, os
pedidos de senha de administrador (o polkit do PC; a senha digitada no celular volta pra ele) e as
confirmações da Emma; e do celular dá pra falar com a Emma do PC, bloquear, suspender, reiniciar e
desligar. E o resto, como um KDE Connect: o que copiar num aparece no outro, os avisos e as
ligações do celular aparecem no PC, arquivos vão e vêm (no Arquivos: Enviar para…; no celular
também pela página do PC) e agendamentos no PC (desligar, lembrete, um pedido pra Emma...).
Cada lado liga/desliga os recursos de cada aparelho e escolhe a pasta de recebidos: no PC em
Configurações > Celular, no celular em Ajustes > Celular Connect. Fora de casa: Tailscale nos dois (`sudo pacman -S tailscale`,
`sudo systemctl enable --now tailscaled`, `sudo tailscale up`) e adicionar o PC pelo IP 100.x.

Emma: `sh mobile/session/emma.sh` instala o backend (pacote `lumna-emma`) e liga o serviço do
usuário; aí o painel entra sozinho, como no desktop. O código da Emma (NewHarnessV2) é privado:
o script fala como copiar do PC pela USB. Pra conferir: `sh mobile/session/emma.sh status`.

## Aparelhos

| Aparelho | SoC | Caminho | Papel |
|---|---|---|---|
| OnePlus 6T (`oneplus-fajita`) | SDM845 | mainline | laboratório: tudo é desenvolvido aqui primeiro |
| Lenovo Z5 Pro GT (`lenovo-z5progt`) | SM8150 | downstream 4.14, depois mainline | **pausado**: flat da bateria quebrado |
| Galaxy Z Flip3 | SM8350 | a decidir | prova de que a base é portável |
| Note10+ / Note20 Ultra | Exynos | — | banco de reserva |

**mainline**: kernel oficial com patches do [sdm845-mainline](https://gitlab.com/sdm845-mainline/linux). Os drivers são livres; só firmware é blob.
**downstream**: kernel do LineageOS (Qualcomm 4.14). Tem todos os drivers, mas é velho demais pro systemd atual.
**Halium/libhybris**: carrega os drivers `.so` do Android dentro do Linux. Fica de plano B pra câmera.

## Como as peças se encaixam

```
boot.img = Image.gz + dtb + initramfs (busybox + init do LUMNA)
   │  init: carrega módulos, sobe rede USB (172.16.42.1), procura LABEL=lumna-root
   ▼
rootfs Arch Linux ARM (partição userdata)
   ├── pacman + AUR (base-devel, git)
   ├── Flatpak
   └── Waydroid (APK)      ← etapa 3
```

Sem rootfs (ou com `lumna.debug` na cmdline) o init vira um shell por telnet pela USB.
Assim dá para diagnosticar sem tela.

## Etapas

1. ✅ **Primeiro boot no 6T sem gravar nada**: kernel + initramfs via `fastboot boot`, shell pela USB.
2. ✅ **Arch instalado no 6T**: rootfs na userdata, SSH, NetworkManager, Flatpak.
3. **Celular de verdade**: Phosh ou Plasma Mobile, modem (rmtfs/pd-mapper/tqftpserv/qrtr), Wi-Fi, áudio, Waydroid, BlackArch.
4. *(pausado)* **Z5 Pro GT, primeiro boot**: kernel 4.14 do LineageOS + nosso initramfs, shell pela USB (veja abaixo).
5. *(pausado)* **Z5 Pro GT com Arch completo**: kernel mainline SM8150 com device tree próprio do `heart`.
6. **Z Flip3.**

## Usando (OnePlus 6T)

### Preparar o PC (uma vez só)

Instale tudo de uma vez, conforme a sua distro:

```sh
# Arch / Manjaro / EndeavourOS
sudo pacman -S --needed base-devel git curl bc dtc clang llvm lld bison flex perl python kmod openssl libelf \
    cpio xz zstd libarchive binutils e2fsprogs android-tools \
    qemu-system-aarch64 qemu-user-static qemu-user-static-binfmt

# Debian / Ubuntu / Mint / Pop!_OS
sudo apt install build-essential git curl bc device-tree-compiler clang llvm lld bison flex perl python3 kmod \
    libssl-dev libelf-dev cpio xz-utils zstd libarchive-tools binutils e2fsprogs adb fastboot \
    qemu-system-arm qemu-user-static binfmt-support

# Fedora
sudo dnf install @development-tools git curl bc dtc clang llvm lld bison flex perl python3 kmod \
    openssl-devel elfutils-libelf-devel cpio xz zstd bsdtar binutils e2fsprogs android-tools \
    qemu-system-aarch64 qemu-user-static
```

Pra que serve cada grupo:
- **Kernel:** `clang` + `llvm` (o `llvm-objcopy`, `llvm-ar`...) + `lld` (o `ld.lld`), `bison`, `flex`, `bc`, `perl`, `dtc`, `openssl`/`libssl-dev` (assinatura dos módulos), `kmod` (o `depmod`).
- **initramfs e rootfs:** `cpio`, `xz`, `zstd`, `bsdtar` (libarchive), `e2fsprogs` (o `mkfs.ext4`), `qemu-user-static` (roda o pacman ARM dentro do PC x86).
- **Celular:** `fastboot` e `adb` (android-tools).
- **Teste opcional:** `qemu-system-aarch64`.

Se faltar alguma coisa, o script avisa todos os comandos que faltam de uma vez e o pacote de cada um.

### Compilar e testar

```sh
cd mobile/scripts
./backup.sh                                     # ANTES de tudo, com o Android ainda ligado e root
./build-kernel.sh oneplus-fajita                # ~30-60 min na primeira vez
./build-initramfs.sh oneplus-fajita
./build-boot.sh oneplus-fajita lumna.debug    # debug: para no shell, não procura rootfs

./test-qemu.sh oneplus-fajita                   # opcional: testa o initramfs no QEMU antes
./test-qemu.sh oneplus-fajita ../out/oneplus-fajita/rootfs.img   # ou o Arch inteiro (não grava nada)

# celular em modo fastboot (desligado, Volume- + Volume+ + Power; solte só quando aparecer)
./flash.sh oneplus-fajita test                  # boot só na RAM; reiniciou, voltou ao Android
# se o bootloader recusar ("Load Error", comum no 6T): grava no slot que o Android não usa
./flash.sh oneplus-fajita slot                  # Android e dados ficam intactos no outro slot
./flash.sh oneplus-fajita voltar                # (no fastboot) liga de novo no slot do Android
# pra voltar ao fastboot sem brigar com os botões:
#   no shell de debug (telnet): lumna-reboot bootloader
#   no Arch (ssh):              sudo systemctl reboot bootloader
telnet 172.16.42.1                              # se o PC não pegar IP: 172.16.42.2/24 na interface USB
```

Se o shell aparecer, a etapa 1 está pronta. Para instalar de verdade (**apaga o Android**):

```sh
sudo ./build-rootfs.sh oneplus-fajita
./build-boot.sh oneplus-fajita
./flash.sh oneplus-fajita install
ssh alarm@172.16.42.1                           # senha alarm; troque as senhas de alarm e root
sudo ./usb-internet.sh                          # no PC: empresta a internet pro celular (pacman)
```

No LUMNA OS o pacman atende também por **`henchman`** (`sudo henchman -Syu`). É só um apelido:
o `pacman` de verdade continua no lugar, porque o makepkg, o yay e o paru chamam ele pelo nome.

Para voltar ao Android: reinstale o OxygenOS pelo MSM Download Tool ou grave as imagens do `backup/`.

## Lenovo Z5 Pro GT (`heart`) (pausado)

Parado até trocar o flat da bateria (a bateria do Z5 Pro comum não serve). Os scripts já compilam o kernel
e geram o boot.img, então dá pra retomar de onde parou.

O aparelho é oficial no LineageOS ([device](https://github.com/LineageOS/android_device_lenovo_heart),
[sm8150-common](https://github.com/LineageOS/android_device_lenovo_sm8150-common)). O kernel é o do
[Lucchetto](https://github.com/Lucchetto/android_kernel_lenovo_sm8150), Linux **4.14** downstream.

O **systemd atual exige kernel 5.10 ou mais novo**, e o Arch usa systemd. Então o kernel do Lineage serve pro
primeiro boot: nosso initramfs com shell pela USB, pra estudar o hardware (dmesg, painel, touch, slider)
sem gravar nada. O Arch completo no `heart` vai precisar de kernel mainline
([SM8150 do postmarketOS](https://gitlab.postmarketos.org/soc/qualcomm-sm8150/linux), que já tem suporte em testes pro OnePlus 7 Pro e pro Poco X3 Pro)
com um device tree escrito pro aparelho. Esse é o trabalho de hacker de verdade.

```sh
./build-kernel.sh lenovo-z5progt              # config do Lineage + devices/lenovo-z5progt/lumna.config
./build-initramfs.sh lenovo-z5progt
./build-boot.sh lenovo-z5progt lumna.debug
./flash.sh lenovo-z5progt test                # fastboot boot: nada é gravado
telnet 172.16.42.1
```

O aparelho é não-A/B. A partição recovery (TWRP/SHRP) não é tocada, então continua sendo a rede de segurança.

## Avisos

- `backup.sh` salva `modemst1`, `modemst2`, `fsg` e `persist`. Elas são **únicas de cada aparelho** (IMEI, calibração). Se perder, não tem de onde tirar outra. Guarde em mais de um lugar.
- Se o `fastboot boot` der tela preta, confira primeiro o telnet. A tela pode só estar demorando. Depois veja a [página do aparelho no wiki do postmarketOS](https://wiki.postmarketos.org/wiki/OnePlus_6_(oneplus-enchilada)), que lista os detalhes de bootloader do 6/6T.
- Os valores de cada aparelho (offsets, kernel, firmware) ficam em `devices/<aparelho>/device.conf`, tirados do [pmaports](https://gitlab.postmarketos.org/postmarketOS/pmaports) num commit fixo.
