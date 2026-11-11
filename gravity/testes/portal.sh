#!/bin/sh
# lumna-portal (org.freedesktop.FileManager1): "Mostrar na pasta" abre o Atlas. Num barramento D-Bus de sessão só do teste e com um Atlas de mentira.
# Uso: sh testes/portal.sh   (precisa do `make portal`)
aqui=$(cd "$(dirname "$0")/.." && pwd)
[ -x "$aqui/portal" ] || { echo "portal: faltou o make portal"; exit 1; }
command -v dbus-run-session >/dev/null && command -v gdbus >/dev/null || { echo "portal: dispensado (falta dbus-run-session ou gdbus)"; exit 0; }
run=$(mktemp -d)
mkdir -p "$run/bin" "$run/casa/Downloads/pasta com espaco"
echo x > "$run/casa/Downloads/arquivo ç.pdf"
cat > "$run/bin/lumna-atlas" <<EOS
#!/bin/sh
echo "\$1" >> "$run/atlas.log"
EOS
chmod +x "$run/bin/lumna-atlas"
export PATH="$run/bin:$PATH" RUN="$run" PORTAL="$aqui/portal"
dbus-run-session -- sh -c '
	"$PORTAL" &
	sleep 1
	d=$RUN/casa/Downloads
	uri_arq="file://$d/arquivo%20%C3%A7.pdf"
	uri_pasta="file://$d/pasta%20com%20espaco"
	gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1.ShowItems "[\"$uri_arq\"]" "" >/dev/null
	gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1.ShowFolders "[\"$uri_pasta\"]" "" >/dev/null
	gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1.ShowItemProperties "[\"$uri_arq\"]" "" >/dev/null
	gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1.ShowFolders "[\"https://exemplo.com/x\", \"relativo\"]" "" >/dev/null
	sleep 1
	kill %1 2>/dev/null
'
ok=1
d="$run/casa/Downloads"
[ "$(sed -n 1p "$run/atlas.log")" = "$d/arquivo ç.pdf" ] || { echo "FALHOU: ShowItems devia abrir o Atlas no arquivo ($(sed -n 1p "$run/atlas.log"))" >&2; ok=0; }
[ "$(sed -n 2p "$run/atlas.log")" = "$d/pasta com espaco" ] || { echo "FALHOU: ShowFolders devia abrir o Atlas na pasta ($(sed -n 2p "$run/atlas.log"))" >&2; ok=0; }
[ "$(sed -n 3p "$run/atlas.log")" = "$d/arquivo ç.pdf" ] || { echo "FALHOU: ShowItemProperties devia abrir o Atlas no arquivo ($(sed -n 3p "$run/atlas.log"))" >&2; ok=0; }
[ "$(wc -l < "$run/atlas.log")" = "3" ] || { echo "FALHOU: uma URL que não é arquivo local não devia abrir nada ($(wc -l < "$run/atlas.log") aberturas)" >&2; ok=0; }
rm -rf "$run"
[ $ok = 1 ] && echo "portal (FileManager1): ok" || exit 1
