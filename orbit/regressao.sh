#!/bin/sh
# Arranque curto e invariantes. Sem wlroots, só confirma o código.
set -eu
cd "$(dirname "$0")"
for f in protocolos.c orbit.h server.c arrange.c anim.c input.c touch.c im.c ipc.c orbit-msg.c config.c lock.c foreign.c manage.c entrada.c telas.c; do
	test -f "$f"
done
grep -q ORBIT_LAYOUT_SPIRAL orbit.h
grep -q ORBIT_LAYOUT_DWINDLE arrange.c
grep -q ORBIT_CURSOR_GESTURE input.c
grep -q anim_ms config.c
grep -q marked_primary telas.c
grep -q wlr_session_lock_manager_v1_create lock.c
grep -q wlr_pointer_constraints_v1_create protocolos.c
grep -q wlr_seat_touch_notify_down touch.c
grep -q wlr_input_method_manager_v2_create im.c
grep -q wlr_virtual_keyboard_manager_v1_create im.c
PKGS=
for m in wlroots-0.18 wlroots-0.19 wlroots; do
	if pkg-config --exists "$m" wayland-server xkbcommon libinput 2>/dev/null; then
		PKGS="$m wayland-server xkbcommon libinput"
		break
	fi
done
if [ -n "$PKGS" ]; then
	cp makefile.txt Makefile
	make PKGS="$PKGS"
	# arranca sem ecrã e pergunta pelo orbit-msg: saídas e um toque de teste
	run=$(mktemp -d)
	chmod 700 "$run"
	mkdir -p "$run/cfg/lumna"
	# um modos.ini de antes, com o "layout = estender" que juntava os modos de todas as telas: agora é ignorado
	printf '[geral]\nlayout = estender\nao_ligar = desktop\n' >"$run/cfg/lumna/modos.ini"
	XDG_CONFIG_HOME="$run/cfg" XDG_RUNTIME_DIR=$run WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./orbit >"$run/log" 2>&1 &
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
		# o que o Painel manda: entrada.ini (o mapa do teclado) e telas.ini (escala, rotação, desligar e ligar)
		printf '[teclado]\nlayout=br,us\nvariant=abnt2,\noptions=grp:alt_shift_toggle\n' >"$run/cfg/lumna/e.tmp" && mv "$run/cfg/lumna/e.tmp" "$run/cfg/lumna/entrada.ini"
		printf '[HEADLESS-1]\nescala=2\nrotacao=90\n[HEADLESS-2]\nligada=0\n' >"$run/cfg/lumna/t.tmp" && mv "$run/cfg/lumna/t.tmp" "$run/cfg/lumna/telas.ini"
		sleep 1
		./orbit-msg -s "$sock" entrada | grep -q '"layout":"br,us".*Portuguese' || { echo "entrada.ini não chegou ao teclado" >&2; ok=0; }
		./orbit-msg -s "$sock" saidas | grep -q '"escala":2.00.*"rotacao":90' || { echo "telas.ini não chegou à saída" >&2; ok=0; }
		./orbit-msg -s "$sock" saidas | grep -q '"nome":"HEADLESS-2".*"ligada":false' || { echo "a saída não desligou" >&2; ok=0; }
		printf '[HEADLESS-2]\nligada=1\n' >"$run/cfg/lumna/t.tmp" && mv "$run/cfg/lumna/t.tmp" "$run/cfg/lumna/telas.ini"
		sleep 1
		./orbit-msg -s "$sock" saidas | grep -q '"ligada":false' && { echo "a saída não voltou a ligar" >&2; ok=0; }
		# cada tela tem sempre o seu modo: a Mídia numa não mexe na outra, e sem nenhuma em modo especial é tudo Desktop de novo
		./orbit-msg -s "$sock" modo midia HEADLESS-1 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-1","modo":"midia"' || { echo "a Mídia não ligou na tela pedida" >&2; ok=0; }
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-2","modo":"desktop"' || { echo "a Mídia numa tela mexeu na outra" >&2; ok=0; }
		# a Mídia existe numa tela só: pedida noutra, passa para ela e a primeira volta ao Desktop (o resto não mexe)
		./orbit-msg -s "$sock" modo midia HEADLESS-2 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-1","modo":"desktop"' || { echo "a Mídia pedida noutra tela devia sair da primeira" >&2; ok=0; }
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-2","modo":"midia"' || { echo "a Mídia devia passar para a tela pedida" >&2; ok=0; }
		./orbit-msg -s "$sock" modo midia HEADLESS-1 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo gaming HEADLESS-2 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-1","modo":"midia"' || { echo "o Gaming numa tela tirou a outra da Mídia" >&2; ok=0; }
		# as duas telas em modo especial: pedir o Gaming na da Mídia troca os dois de monitor (não manda ninguém ao Desktop)
		./orbit-msg -s "$sock" modo gaming HEADLESS-1 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-1","modo":"gaming"' || { echo "o Gaming devia ir para a tela da Mídia" >&2; ok=0; }
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-2","modo":"midia"' || { echo "a Mídia devia ir para a tela do Gaming (trocam de monitor)" >&2; ok=0; }
		./orbit-msg -s "$sock" modo gaming HEADLESS-2 >/dev/null
		sleep 0.5
		./orbit-msg -s "$sock" modo desktop HEADLESS-1 >/dev/null
		./orbit-msg -s "$sock" modo | grep -q '"saida":"HEADLESS-2","modo":"gaming"' || { echo "voltar ao Desktop numa tela mexeu na outra" >&2; ok=0; }
		./orbit-msg -s "$sock" modo desktop HEADLESS-2 >/dev/null
		./orbit-msg -s "$sock" modo | grep -q '"modo":"midia"\|"modo":"gaming"' && { echo "sem nenhuma tela em modo especial devia ser tudo Desktop" >&2; ok=0; }
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
	# aninhado em dois monitores: o Orbit de dentro abre uma janela por monitor do de fora, a principal no monitor pedido, cada uma no seu lugar
	run=$(mktemp -d)
	chmod 700 "$run"
	mkdir -p "$run/cfg/orbit"
	printf '[geral]\ninibir_atalhos = nao\ntela_cheia = sim\ntela_principal = HEADLESS-1\n' >"$run/cfg/orbit/orbit.ini"
	XDG_RUNTIME_DIR=$run WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2 WLR_RENDERER=pixman ./orbit >"$run/log-fora" 2>&1 &
	fora=$!
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		sockfora=$(ls "$run"/orbit.*.sock 2>/dev/null | head -n1)
		[ -n "$sockfora" ] && break
		sleep 0.3
	done
	hostwl=$(basename "$sockfora" .sock | sed 's/^orbit\.//')
	XDG_RUNTIME_DIR=$run WAYLAND_DISPLAY=$hostwl XDG_CONFIG_HOME="$run/cfg" WLR_RENDERER=pixman ./orbit >"$run/log-dentro" 2>&1 &
	dentro=$!
	sockdentro=
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		sockdentro=$(ls "$run"/orbit.*.sock 2>/dev/null | grep -v "$sockfora" | head -n1)
		[ -n "$sockdentro" ] && break
		sleep 0.3
	done
	ok=1
	if [ -z "$sockdentro" ]; then
		echo "o Orbit aninhado não abriu o socket" >&2
		ok=0
	else
		sleep 1
		saidas=$(./orbit-msg -s "$sockdentro" saidas)
		[ "$(printf '%s' "$saidas" | grep -o '"nome":"WL-' | wc -l)" -eq 2 ] || { echo "aninhado: devia ter uma janela por monitor de fora (2)" >&2; ok=0; }
		printf '%s' "$saidas" | grep -q '"nome":"WL-1".*"x":1280.*"monitor_fora":"HEADLESS-1"' || { echo "aninhado: a saída principal devia estar no HEADLESS-1, em x=1280" >&2; ok=0; }
		printf '%s' "$saidas" | grep -q '"nome":"WL-2".*"x":0.*"monitor_fora":"HEADLESS-2"' || { echo "aninhado: a segunda saída devia estar no HEADLESS-2, em x=0" >&2; ok=0; }
		grep -q "janela WL-1: tela cheia em HEADLESS-1" "$run/log-dentro" || { echo "aninhado: WL-1 devia pedir tela cheia no HEADLESS-1" >&2; ok=0; }
		grep -q "janela WL-2: tela cheia em HEADLESS-2" "$run/log-dentro" || { echo "aninhado: WL-2 devia pedir tela cheia no HEADLESS-2" >&2; ok=0; }
		# a disposição: espelhado põe as duas na mesma posição, "esquerda" apaga a da direita, e estender volta tudo
		./orbit-msg -s "$sockdentro" disposicao espelhar | grep -q '"disposicao":"espelhar"' || { echo "disposicao espelhar falhou" >&2; ok=0; }
		[ "$(./orbit-msg -s "$sockdentro" saidas | grep -o '"x":1280' | wc -l)" -eq 2 ] || { echo "espelhado: as duas saídas devem ficar em x=1280 (a da principal)" >&2; ok=0; }
		./orbit-msg -s "$sockdentro" disposicao esquerda >/dev/null
		./orbit-msg -s "$sockdentro" saidas | grep -q '"nome":"WL-1".*"ligada":false' || { echo "esquerda: a WL-1 (a da direita) devia ficar apagada" >&2; ok=0; }
		./orbit-msg -s "$sockdentro" disposicao direita >/dev/null
		./orbit-msg -s "$sockdentro" saidas | grep -q '"nome":"WL-2".*"ligada":false' || { echo "direita: a WL-2 (a da esquerda) devia ficar apagada" >&2; ok=0; }
		./orbit-msg -s "$sockdentro" disposicao estender >/dev/null
		./orbit-msg -s "$sockdentro" saidas | grep -q '"ligada":false' && { echo "estender: nenhuma saída devia ficar apagada" >&2; ok=0; }
		./orbit-msg -s "$sockdentro" saidas | grep -q '"nome":"WL-2".*"x":0' || { echo "estender: a WL-2 devia voltar a x=0" >&2; ok=0; }
	fi
	kill "$dentro" "$fora" 2>/dev/null || true
	wait "$dentro" "$fora" 2>/dev/null || true
	if [ "$ok" -ne 1 ]; then
		cat "$run/log-dentro" >&2
		rm -rf "$run"
		exit 1
	fi
	rm -rf "$run"
	echo "aninhado em dois monitores ok"
else
	echo "sem wlroots neste ambiente; invariantes do código ok"
fi
