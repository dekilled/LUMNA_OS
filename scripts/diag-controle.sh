#!/bin/sh
# Diagnóstico do controle do celular, do Connect e da Mídia: o que está instalado e o que cada peça diz.
# Rode DENTRO da sessão do LUMNA (o terminal do Alt+Enter) e mande a saída inteira:
#   sh scripts/diag-controle.sh > ~/controle-diag.txt 2>&1
# Não muda nada no sistema.
sec() { printf '\n===== %s =====\n' "$1"; }
tem() { # programa, o que quebra sem ele
	if command -v "$1" >/dev/null 2>&1; then printf '  ok     %s\n' "$1"; else printf '  FALTA  %-22s %s\n' "$1" "$2"; fi
}

sec "programas de que cada função depende"
tem wtype "o direcional, o OK, o voltar, o texto e o menu (Super+M) do controle do celular"
tem wl-copy "a área de transferência do Connect"
tem wl-paste "a área de transferência do Connect"
tem notify-send "os avisos do celular no PC"
tem grim "a captura de tela"
tem wf-recorder "a gravação de tela"
tem pactl "o volume e o áudio da gravação"
tem firefox "a Mídia (os serviços abrem num Firefox em tela cheia)"
tem firefox-developer-edition "a Mídia com a extensão sem assinatura (o Firefox comum só liga extensões assinadas)"
tem orbit-msg "tudo: o Orbit"

sec "o Firefox da Mídia"
for f in firefox firefox-developer-edition firefox-nightly firefox-esr; do
	command -v "$f" >/dev/null 2>&1 && printf '  %s: %s\n' "$f" "$($f --version 2>/dev/null | head -1)"
done
if [ -n "${LUMNA_FIREFOX:-}" ]; then
	echo "  o serviço vai usar: $LUMNA_FIREFOX (LUMNA_FIREFOX)"
else
	USA=firefox
	for f in firefox-developer-edition firefox-nightly firefox-esr firefox; do
		command -v "$f" >/dev/null 2>&1 && { USA=$f; break; }
	done
	echo "  o serviço vai usar: $USA (o primeiro que existir entre developer-edition, nightly, esr e o comum)"
	[ "$USA" = firefox ] && echo "  >> só o firefox comum: ele não liga a extensão sem assinatura"
fi
for x in "${LUMNA_EXTENSAO:-}" /usr/share/lumna/midia/lumna-midia.xpi; do
	[ -n "$x" ] && [ -e "$x" ] && echo "  extensão: $x"
done
ls "$(dirname "$0")/../gravity/extensao-firefox/" 2>/dev/null | sed 's/^/  extensao-firefox\/: /'

sec "o serviço da Mídia (lumna-midia servico)"
SOCK="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/lumna-midia.sock"
if [ -S "$SOCK" ]; then
	python3 - "$SOCK" <<'PY'
import json, socket, sys
s = socket.socket(socket.AF_UNIX); s.settimeout(3); s.connect(sys.argv[1])
s.sendall(b"estado\n")
t = b""
while not t.endswith(b"\n"):
    d = s.recv(65536)
    if not d: break
    t += d
try:
    j = json.loads(t)
    print("  firefox conectado à extensão:", j.get("firefox"), "| extensão encontrada:", j.get("extensao"), "| aba da frente:", repr(j.get("ativo")))
    print("  serviços:", [(x.get("id"), "ativo" if x.get("ativo") else "desligado") for x in j.get("servicos", [])])
    if not j.get("firefox"):
        print("  >> o Firefox NÃO conectou à extensão: os serviços não abrem. Firefox comum não liga extensão sem assinatura.")
except Exception as e:
    print("  resposta ilegível:", t[:200], e)
PY
else
	echo "  o serviço não está rodando ($SOCK não existe): rode  lumna-midia servico"
fi

sec "players de mídia no D-Bus (MPRIS): é por eles que o +10 s e o -10 s do controle andam o vídeo"
echo "  (deixe um vídeo tocando na Mídia antes de rodar; se o Firefox não aparecer aqui, o controle cai nas setas do teclado)"
if command -v busctl >/dev/null 2>&1; then
	busctl --user list --no-pager 2>/dev/null | grep -i mpris | sed 's/^/  /' || true
elif command -v gdbus >/dev/null 2>&1; then
	gdbus call --session --dest org.freedesktop.DBus --object-path /org/freedesktop/DBus --method org.freedesktop.DBus.ListNames 2>/dev/null | tr ',' '\n' | grep -i mpris | sed 's/^/  /' || true
fi
[ -z "$(busctl --user list --no-pager 2>/dev/null | grep -i mpris)" ] && echo "  nenhum player MPRIS agora"

sec "o controle e o Connect (lumna-controle)"
if command -v lumna-controle >/dev/null 2>&1; then
	lumna-controle estado 2>&1 | python3 -c '
import json, sys
t = sys.stdin.read()
try:
    j = json.loads(t.strip().splitlines()[-1])
except Exception:
    print(t); sys.exit()
print("  porta:", j.get("porta"), "| celulares pareados:", len(j.get("dispositivos", j.get("remotes", []))))
for d in j.get("dispositivos", j.get("remotes", [])):
    print("   -", d.get("nome") or d.get("name"), "| conectado:", d.get("conectado"), "| recursos:", d.get("recursos") or d.get("features"))
' 2>&1
else
	echo "  lumna-controle não está no PATH"
fi

sec "o Orbit"
command -v orbit-msg >/dev/null 2>&1 && { orbit-msg tela_cheia estado 2>&1; orbit-msg modo 2>&1 | head -c 400; echo; }
