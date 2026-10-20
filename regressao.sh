#!/bin/sh
# Arranque curto e invariantes. Sem wlroots, só confirma o código.
set -eu
cd "$(dirname "$0")"
for f in orbit.h server.c arrange.c anim.c input.c touch.c im.c ipc.c orbit-msg.c config.c lock.c foreign.c manage.c; do
	test -f "$f"
done
grep -q ORBIT_LAYOUT_SPIRAL orbit.h
grep -q ORBIT_LAYOUT_DWINDLE arrange.c
grep -q ORBIT_CURSOR_GESTURE input.c
grep -q anim_ms config.c
grep -q wlr_session_lock_manager_v1_create lock.c
grep -q wlr_seat_touch_notify_down touch.c
grep -q wlr_input_method_manager_v2_create im.c
grep -q wlr_virtual_keyboard_manager_v1_create im.c
PKGS=
for m in wlroots-0.18 wlroots-0.19 wlroots; do
	if pkg-config --exists "$m" wayland-server xkbcommon 2>/dev/null; then
		PKGS="$m wayland-server xkbcommon"
		break
	fi
done
if [ -n "$PKGS" ]; then
	cp makefile.txt Makefile
	make PKGS="$PKGS"
	# arranca sem ecrã e pergunta pelo orbit-msg: saídas e um toque de teste
	run=$(mktemp -d)
	chmod 700 "$run"
	XDG_RUNTIME_DIR=$run WLR_BACKENDS=headless WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 &
	pid=$!
	sock=
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		sock=$(ls "$run"/orbit.*.sock 2>/dev/null | head -n1)
		[ -n "$sock" ] && break
		sleep 0.3
	done
	ok=1
	if [ -z "$sock" ]; then
		echo "o orbit não abriu o socket do orbit-msg" >&2
		ok=0
	else
		./orbit-msg -s "$sock" saidas | grep -q '"ok":true' || { echo "orbit-msg saidas falhou" >&2; ok=0; }
		./orbit-msg -s "$sock" toque baixo 0 10 10 | grep -q '"ok":true' || { echo "orbit-msg toque falhou" >&2; ok=0; }
		./orbit-msg -s "$sock" toque cima 0 | grep -q '"ok":true' || { echo "orbit-msg toque falhou" >&2; ok=0; }
	fi
	kill "$pid" 2>/dev/null || true
	wait "$pid" 2>/dev/null || true
	if [ "$ok" -ne 1 ]; then
		cat "$run/log" >&2
		rm -rf "$run"
		exit 1
	fi
	rm -rf "$run"
	echo "arranque headless e orbit-msg ok"
else
	echo "sem wlroots neste ambiente; invariantes do código ok"
fi
