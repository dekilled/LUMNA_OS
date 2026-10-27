#!/bin/sh
# O desktop novo do LUMNA (Orbit + Gravity) numa janela, dentro do desktop que já está aberto
# (Hyprland, GNOME, KDE...), para testar no PC sem trocar de sessão:
#
#   ./testar-desktop.sh            compila o que faltar e abre
#   ./testar-desktop.sh --refazer  recompila o Gravity e atualiza o Orbit antes de abrir (use depois de dar git pull aqui:
#                                  sem isso ele abre os binários de antes e você não vê as mudanças)
#
# O Orbit vem do repositório dele (branch claude/orbit-compila) para ~/.cache/lumna/orbit.
# Lá dentro sobem o papel de parede, a topbar, a dock e os avisos, num D-Bus só deles (os avisos
# não tiram as notificações do seu desktop). Alt+Enter abre um terminal; fechar a janela sai.
# Para testar os avisos lá dentro: abra o terminal (Alt+Enter) e corra notify-send "Olá" "LUMNA".
set -e

AQUI=$(cd "$(dirname "$0")" && pwd)
CACHE=${XDG_CACHE_HOME:-$HOME/.cache}/lumna
ORBIT_DIR=${ORBIT_DIR:-$CACHE/orbit}
ORBIT_REPO=https://github.com/dekilled/LUMNA_COMPOSITOR
ORBIT_BRANCH=claude/orbit-compila
BIN=$CACHE/bin

falta() {
	echo "Falta: $1"
	echo "No Arch: sudo pacman -S --needed base-devel git wayland wayland-protocols libxkbcommon libinput cairo pango \\"
	echo "         glib2 gdk-pixbuf2 libpulse wlroots0.20 dbus foot libnotify \\"
	echo "         networkmanager bluez bluez-utils polkit"
	exit 1
}

if [ -z "$WAYLAND_DISPLAY" ]; then
	echo "Abra isto de dentro de um desktop Wayland (o Orbit abre como uma janela dele)."
	exit 1
fi
for p in wayland-client wayland-protocols wayland-scanner xkbcommon cairo pangocairo glib-2.0 gio-2.0 gdk-pixbuf-2.0 libpulse libinput; do
	pkg-config --exists "$p" || falta "$p"
done
command -v dbus-run-session >/dev/null || falta "dbus-run-session (pacote dbus)"

# o Gravity: a biblioteca e as peças do desktop
if [ "$1" = "--refazer" ] || [ ! -x "$AQUI/avisos" ]; then
	make -C "$AQUI" -j"$(nproc)"
fi

# o Orbit: compila no wlroots 0.18, 0.19 e 0.20 (o Arch de hoje só tem o 0.20; o 0.18 do AUR já
# não compila com a libinput nova). O módulo muda de nome conforme a distro e a versão.
WLR=""
for m in wlroots-0.20 wlroots-0.19 wlroots-0.18 wlroots; do
	if pkg-config --exists "$m"; then
		WLR=$m
		break
	fi
done
[ -n "$WLR" ] || falta "wlroots (no Arch: wlroots0.20)"
if [ ! -d "$ORBIT_DIR/.git" ]; then
	git clone --depth 1 -b "$ORBIT_BRANCH" "$ORBIT_REPO" "$ORBIT_DIR"
fi
O="$ORBIT_DIR/src/compositor/orbit"
if [ "$1" = "--refazer" ] || [ ! -x "$O/orbit" ]; then
	# sempre a versão mais nova antes de compilar (um clone de uma tentativa que falhou também)
	git -C "$ORBIT_DIR" remote set-url origin "$ORBIT_REPO"
	git -C "$ORBIT_DIR" pull --ff-only
	rm -f "$O/orbit"
	make -C "$O" clean >/dev/null 2>&1 || true
	cp "$O/makefile.txt" "$O/Makefile"
	make -C "$O" PKGS="$WLR wayland-server xkbcommon libinput"
fi

# os nomes que as peças chamam umas às outras (a dock abre o lumna-lancador, o sino o lumna-avisos...)
mkdir -p "$BIN"
for p in papel barra dock lancador avisos bloqueio entrada ocioso modos controle midia emma emma-chat painel apps rede transferencia mixer wifi galeria; do
	[ -x "$AQUI/$p" ] && ln -sf "$AQUI/$p" "$BIN/lumna-$p"
done
ln -sf "$O/orbit-msg" "$BIN/orbit-msg"
ln -sf "$AQUI/../session/lumna-modo" "$BIN/lumna-modo"

echo "Abrindo o desktop novo numa janela (feche-a para sair)..."
export PATH="$BIN:$PATH"
export ORBIT_BORDA=0 # as janelas desenham a própria moldura, de cantos redondos
exec dbus-run-session -- "$O/orbit" -s 'lumna-papel & lumna-barra & lumna-dock & lumna-avisos & lumna-ocioso & lumna-controle & lumna-emma & lumna-midia servico &
	sleep 1; notify-send -a LUMNA "Bem-vindo ao desktop novo" "Os avisos chegam aqui. O sino na topbar abre a central." 2>/dev/null'
