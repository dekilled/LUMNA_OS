#!/bin/sh
# Cria em $1 os programas de mentira (pacman, checkupdates, curl, paru) e os registros do que eles receberam. Uso: sh falsos.sh PASTA
d=${1:?}; mkdir -p "$d/bin"
cat > "$d/bin/pacman" <<'P'
#!/bin/sh
echo "pacman $*" >> "$FALSO_LOG"
M="$FALSO_LOG.mais"; F="$FALSO_LOG.fora"
atual() {
	{ printf 'mesa 24.2.1-1\nlinux 6.10.8.arch1-1\nvlc 3.0.20-1\nhtop 3.3.0-1\nmeu-app-aur 1.0-1\n'; for e in $FALSO_EXTRA; do echo "$e 1-1"; done; cat "$M" 2>/dev/null; } | while read n v; do grep -qx "$n" "$F" 2>/dev/null || echo "$n $v"; done
}
case "$1" in
-Si) cat <<'S'
Repository      : extra
Name            : firefox
Version         : 130.0-1
Description     : Fast, Private & Safe Web Browser
Architecture    : x86_64
URL             : https://www.mozilla.org/firefox/
Licenses        : MPL-2.0
Groups          : None
Download Size   : 70.50 MiB
Installed Size  : 260.25 MiB
Packager        : Fulano

Repository      : extra
Name            : mesa
Version         : 24.2.1-1
Description     : Open-source OpenGL drivers
URL             : https://mesa3d.org
Licenses        : MIT
Groups          : None
Download Size   : 30.00 MiB
Installed Size  : 160.00 MiB

Repository      : extra
Name            : mesa-utils
Version         : 9.0.0-4
Description     : Essential Mesa utilities
URL             : https://mesa3d.org
Licenses        : MIT
Groups          : None
Download Size   : 1.00 MiB
Installed Size  : 4.00 MiB

Repository      : multilib
Name            : lib32-mesa
Version         : 24.2.1-1
Description     : Open-source OpenGL drivers (32-bit)
URL             : https://mesa3d.org
Licenses        : MIT
Groups          : None
Download Size   : 30.00 MiB
Installed Size  : 160.00 MiB

Repository      : extra
Name            : libva-mesa-driver
Version         : 24.2.1-1
Description     : Open-source VA-API drivers
URL             : https://mesa3d.org
Licenses        : MIT
Groups          : None
Download Size   : 8.00 MiB
Installed Size  : 30.00 MiB

Repository      : core
Name            : linux
Version         : 6.10.8.arch1-1
Description     : The Linux kernel and modules
URL             : https://github.com/archlinux/linux
Licenses        : GPL-2.0-only
Groups          : None
Download Size   : 140.00 MiB
Installed Size  : 140.00 MiB

Repository      : core
Name            : linux-lts
Version         : 6.6.50-1
Description     : The LTS Linux kernel and modules
URL             : https://www.kernel.org
Licenses        : GPL-2.0-only
Groups          : None
Download Size   : 130.00 MiB
Installed Size  : 130.00 MiB

Repository      : extra
Name            : vlc
Version         : 3.0.21-1
Description     : Multi-platform MPEG, VCD/DVD, and DivX player
URL             : https://www.videolan.org/vlc/
Licenses        : LGPL-2.1-or-later
Groups          : None
Download Size   : 20.00 MiB
Installed Size  : 80.00 MiB

Repository      : extra
Name            : blender
Version         : 4.2.1-1
Description     : A fully integrated 3D graphics creation suite
URL             : https://www.blender.org
Licenses        : GPL-2.0-or-later
Groups          : None
Download Size   : 200.00 MiB
Installed Size  : 900.00 MiB

Repository      : extra
Name            : htop
Version         : 3.3.0-1
Description     : Interactive process viewer
URL             : https://htop.dev
Licenses        : GPL-2.0-or-later
Groups          : None
Download Size   : 200.00 KiB
Installed Size  : 400.00 KiB
S
for n in $FALSO_CATALOGO_EXTRA; do printf '\nRepository      : extra\nName            : %s\nVersion         : 1-1\nDescription     : %s falso\nURL             : None\nLicenses        : MIT\nGroups          : None\nDownload Size   : 10.00 MiB\nInstalled Size  : 40.00 MiB\n' "$n" "$n"; done
;;
-Q) atual;;
-Qe) atual | while read n v; do case "$n" in linux|vlc|meu-app-aur) echo "$n $v";; *) grep -q "^$n " "$M" 2>/dev/null && echo "$n $v";; esac; done;;
-Qm) { printf 'meu-app-aur 1.0-1\n'; cat "$FALSO_LOG.aur" 2>/dev/null; };;
-S) echo "resolvendo dependências..."; echo "instalando $*"; for a in "$@"; do ult=$a; done; { [ -n "$FALSO_FALHA" ] || [ "$ult" = linux-lts ]; } && { echo "error: target not found: $ult" >&2; exit 1; }; for a in "$@"; do case "$a" in -*) ;; *) echo "$a 1-1" >> "$FALSO_LOG.mais"; case "$a" in linux|linux-lts|linux-zen|linux-hardened) [ -n "$LUMNA_BOOT" ] && { echo k > "$LUMNA_BOOT/vmlinuz-$a"; echo i > "$LUMNA_BOOT/initramfs-$a.img"; };; esac;; esac; done; echo "(1/1) instalando";;
-Rns) echo "removendo $*"; for a in "$@"; do case "$a" in -*) ;; *) if grep -q "^$a " "$M" 2>/dev/null; then sed -i "/^$a /d" "$M"; else echo "$a" >> "$F"; fi;; esac; done;;
-Syu) echo "sincronizando"; echo "atualizando tudo";;
-Qu) printf 'vlc 3.0.20-1 -> 3.0.21-1\n';;
esac
P
cat > "$d/bin/checkupdates" <<'P'
#!/bin/sh
printf 'vlc 3.0.20-1 -> 3.0.21-1\nhtop 3.2.0-1 -> 3.3.0-1\n'
P
cat > "$d/bin/lspci" <<'P'
#!/bin/sh
[ -n "$FALSO_LSPCI" ] && cat "$FALSO_LSPCI"
P
cat > "$d/bin/curl" <<'P'
#!/bin/sh
echo "curl $*" >> "$FALSO_LOG"
case "$*" in
*sem-rede*) exit 6;;
*) cat <<'J'
{"resultcount":3,"results":[
{"Name":"visual-studio-code-bin","Version":"1.93-1","Description":"Editor redefined for modern web development","NumVotes":2000,"Popularity":15.5,"Maintainer":"alguem","OutOfDate":null,"URL":"https://code.visualstudio.com"},
{"Name":"mesa","Version":"99-1","Description":"duplicado do repositório oficial","NumVotes":1,"Popularity":0.1,"Maintainer":"x","OutOfDate":null,"URL":""},
{"Name":"meu-app-aur","Version":"1.0-1","Description":"Um app do AUR","NumVotes":5,"Popularity":0.5,"Maintainer":"eu","OutOfDate":1700000000,"URL":"https://exemplo.org"},
{"Name":"outro-app-aur","Version":"2.0-1","Description":"Outro app do AUR","NumVotes":3,"Popularity":0.3,"Maintainer":"ela","OutOfDate":null,"URL":""}],"type":"search","version":5}
J
;;
esac
P
cat > "$d/bin/paru" <<'P'
#!/bin/sh
echo "paru $*" >> "$FALSO_LOG"
case "$1" in
-Qua) printf 'meu-app-aur 1.0-1 -> 1.1-1\n';;
-S) for a in "$@"; do ult=$a; done; echo "baixando PKGBUILD"; echo "compilando"; echo "instalando"; echo "$ult 1-1" >> "$FALSO_LOG.mais"; echo "$ult 1-1" >> "$FALSO_LOG.aur";;
-Sua) echo "atualizando o AUR";;
esac
P
chmod +x "$d/bin/"*
