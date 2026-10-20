#!/bin/sh
# Arranque curto e invariantes. Sem wlroots, só confirma o código.
set -eu
cd "$(dirname "$0")"
for f in orbit.h server.c arrange.c anim.c input.c config.c lock.c foreign.c manage.c; do
	test -f "$f"
done
grep -q ORBIT_LAYOUT_SPIRAL orbit.h
grep -q ORBIT_LAYOUT_DWINDLE arrange.c
grep -q ORBIT_CURSOR_GESTURE input.c
grep -q anim_ms config.c
grep -q wlr_session_lock_manager_v1_create lock.c
if pkg-config --exists wlroots wayland-server xkbcommon 2>/dev/null; then
	make
	set +e
	WLR_BACKENDS=headless timeout 2 ./orbit
	code=$?
	set -e
	if [ "$code" -ne 0 ] && [ "$code" -ne 124 ]; then
		echo "arranque headless falhou ($code)" >&2
		exit 1
	fi
	echo "arranque headless ok"
else
	echo "sem wlroots neste ambiente; invariantes do código ok"
fi
