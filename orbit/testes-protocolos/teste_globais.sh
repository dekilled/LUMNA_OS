#!/bin/sh
# Os protocolos novos do Orbit aparecem para os apps, e copiar/colar funciona sem janela com foco (wlr-data-control): é o que o wl-clipboard usa.
# sh teste_globais.sh PASTA_DO_ORBIT
orbit_dir=${1:?}
run=$(mktemp -d); chmod 700 "$run"
export XDG_RUNTIME_DIR=$run WLR_RENDERER=pixman WLR_LIBINPUT_NO_DEVICES=1 WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=1
(cd "$orbit_dir" && ./orbit > "$run/orbit.log" 2>&1 &)
sleep 3
export WAYLAND_DISPLAY=wayland-0
falhas=0; fail() { echo "FALHOU: $*" >&2; falhas=$((falhas + 1)); }
globais=$(wayland-info 2>/dev/null | grep -o "interface: '[a-z_0-9]*'")
for g in zwp_primary_selection_device_manager_v1 zwlr_data_control_manager_v1 wp_cursor_shape_manager_v1 xdg_activation_v1 wp_single_pixel_buffer_manager_v1 \
	wp_alpha_modifier_v1 zwp_relative_pointer_manager_v1 zwp_pointer_constraints_v1; do
	echo "$globais" | grep -q "'$g'" || fail "o Orbit não anuncia $g"
done
printf 'ola-clip' | wl-copy; sleep 0.4
[ "$(timeout 3 wl-paste --no-newline)" = "ola-clip" ] || fail "copiar e colar (área de transferência) não voltou o texto"
printf 'ola-primaria' | wl-copy -p; sleep 0.4
[ "$(timeout 3 wl-paste -p --no-newline)" = "ola-primaria" ] || fail "copiar e colar (seleção primária) não voltou o texto"
pkill -x orbit
[ "$falhas" -eq 0 ] && echo "protocolos novos e área de transferência: ok" || { echo "$falhas falha(s) (log em $run)"; exit 1; }
