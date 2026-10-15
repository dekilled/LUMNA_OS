# Ambiente de build do LUMNA OS: Arch Linux com archiso e as ferramentas do shell.
FROM archlinux:latest

# Certificados extras (proxy corporativo etc.): coloque arquivos .crt em iso/builder/ca/.
COPY ca/ /etc/ca-certificates/trust-source/anchors/
RUN update-ca-trust \
 && pacman -Syu --noconfirm --needed \
      base-devel git archiso rust nodejs pnpm \
      webkit2gtk-4.1 gtk3 gtk-layer-shell librsvg \
 && pacman -Scc --noconfirm \
 && useradd -m builder \
 && echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder
