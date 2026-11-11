#!/bin/sh
# lumna-portal, em dois serviços (num barramento D-Bus de sessão só do teste e com um Atlas de mentira que registra o que recebeu):
#  - org.freedesktop.FileManager1: "Mostrar na pasta" abre o Atlas na pasta/arquivo;
#  - org.freedesktop.impl.portal.FileChooser: abrir, abrir vários, escolher pasta, salvar, salvar vários, filtros, cancelar e Request.Close.
# Uso: sh testes/portal.sh   (precisa do `make portal`)
aqui=$(cd "$(dirname "$0")/.." && pwd)
[ -x "$aqui/portal" ] || { echo "portal: faltou o make portal"; exit 1; }
command -v dbus-run-session >/dev/null && command -v gdbus >/dev/null || { echo "portal: dispensado (falta dbus-run-session ou gdbus)"; exit 0; }
run=$(mktemp -d)
mkdir -p "$run/bin" "$run/casa/Downloads/pasta com espaco"
echo x > "$run/casa/Downloads/arquivo ç.pdf"
cat > "$run/bin/lumna-atlas" <<EOS
#!/bin/sh
case "\$1" in
--escolher=*)
	echo "\$*" >> "$run/args.log"
	case "\$*" in
	*--titulo=cancelar*) exit 1 ;;
	*--titulo=dormir*) echo "\$\$" > "$run/dormiu.pid"; exec sleep 30 ;;
	esac
	case "\$1" in
	--escolher=abrir-varios) printf '%s\n%s\n' "$run/casa/Downloads/arquivo ç.pdf" "$run/casa/Downloads/b.txt" ;;
	--escolher=pasta) echo "$run/casa/Downloads/pasta com espaco" ;;
	*) echo "$run/casa/Downloads/arquivo ç.pdf" ;;
	esac ;;
*) echo "\$1" >> "$run/atlas.log" ;;
esac
EOS
chmod +x "$run/bin/lumna-atlas"
export PATH="$run/bin:$PATH" RUN="$run" PORTAL="$aqui/portal"
dbus-run-session -- sh -c '
	"$PORTAL" &
	sleep 1
	d=$RUN/casa/Downloads
	FM="gdbus call --session --dest org.freedesktop.FileManager1 --object-path /org/freedesktop/FileManager1 --method org.freedesktop.FileManager1"
	$FM.ShowItems "[\"file://$d/arquivo%20%C3%A7.pdf\"]" "" >/dev/null
	$FM.ShowFolders "[\"file://$d/pasta%20com%20espaco\"]" "" >/dev/null
	$FM.ShowItemProperties "[\"file://$d/arquivo%20%C3%A7.pdf\"]" "" >/dev/null
	$FM.ShowFolders "[\"https://exemplo.com/x\", \"relativo\"]" "" >/dev/null
	FC="gdbus call --session --dest org.freedesktop.impl.portal.desktop.lumna --object-path /org/freedesktop/portal/desktop --method org.freedesktop.impl.portal.FileChooser"
	h=/org/freedesktop/portal/desktop/request/teste
	$FC.OpenFile $h/1 app "" "Escolha" "{}" > $RUN/r_abrir.txt 2>&1
	$FC.OpenFile $h/2 app "" "Varios" "{\"multiple\": <true>}" > $RUN/r_varios.txt 2>&1
	$FC.OpenFile $h/3 app "" "Pasta" "{\"directory\": <true>}" > $RUN/r_pasta.txt 2>&1
	$FC.SaveFile $h/4 app "" "Salvar" "{\"current_name\": <\"doc.txt\">, \"current_folder\": <b\"$d\\000\">, \"accept_label\": <\"_Guardar\">}" > $RUN/r_salvar.txt 2>&1
	$FC.OpenFile $h/5 app "" "Filtros" "{\"filters\": <[(\"Imagens\", [(uint32 0, \"*.png\"), (uint32 1, \"image/png\")]), (\"Tudo\", [(uint32 0, \"*\")])]>}" > $RUN/r_filtros.txt 2>&1
	$FC.OpenFile $h/6 app "" "cancelar" "{}" > $RUN/r_cancelar.txt 2>&1
	$FC.SaveFiles $h/7 app "" "Varios" "{\"files\": <[b\"a.txt\\000\", b\"b.txt\\000\"]>}" > $RUN/r_salvarvarios.txt 2>&1
	# Request.Close: a chamada dorme no seletor (um Atlas que não acaba), o app desiste e a chamada responde 2
	$FC.OpenFile $h/8 app "" "dormir" "{}" > $RUN/r_fechar.txt 2>&1 &
	sleep 1.5
	gdbus call --session --dest org.freedesktop.impl.portal.desktop.lumna --object-path $h/8 --method org.freedesktop.impl.portal.Request.Close >/dev/null 2>&1
	sleep 1.5
	kill %1 2>/dev/null
' 2>/dev/null
ok=1
falha() { echo "FALHOU: $1" >&2; ok=0; }
d="$run/casa/Downloads"
[ "$(sed -n 1p "$run/atlas.log")" = "$d/arquivo ç.pdf" ] || falha "ShowItems devia abrir o Atlas no arquivo ($(sed -n 1p "$run/atlas.log"))"
[ "$(sed -n 2p "$run/atlas.log")" = "$d/pasta com espaco" ] || falha "ShowFolders devia abrir o Atlas na pasta ($(sed -n 2p "$run/atlas.log"))"
[ "$(sed -n 3p "$run/atlas.log")" = "$d/arquivo ç.pdf" ] || falha "ShowItemProperties devia abrir o Atlas no arquivo ($(sed -n 3p "$run/atlas.log"))"
[ "$(wc -l < "$run/atlas.log")" = "3" ] || falha "uma URL que não é arquivo local não devia abrir nada ($(wc -l < "$run/atlas.log") aberturas)"
grep -q "uint32 0" "$run/r_abrir.txt" && grep -q "file://.*arquivo%20%C3%A7.pdf" "$run/r_abrir.txt" || falha "OpenFile devia responder 0 com a URI do arquivo ($(cat "$run/r_abrir.txt"))"
grep -q -- "--escolher=abrir --titulo=Escolha" "$run/args.log" || falha "OpenFile devia abrir o Atlas em --escolher=abrir com o título"
grep -q "uint32 0" "$run/r_varios.txt" && grep -q "b.txt" "$run/r_varios.txt" && grep -q "arquivo%20%C3%A7.pdf" "$run/r_varios.txt" || falha "OpenFile com 'multiple' devia devolver os dois arquivos ($(cat "$run/r_varios.txt"))"
grep -q -- "--escolher=abrir-varios" "$run/args.log" || falha "'multiple' devia ir como --escolher=abrir-varios"
grep -q "uint32 0" "$run/r_pasta.txt" && grep -q "pasta%20com%20espaco" "$run/r_pasta.txt" || falha "OpenFile com 'directory' devia devolver a pasta ($(cat "$run/r_pasta.txt"))"
grep -q -- "--escolher=pasta" "$run/args.log" || falha "'directory' devia ir como --escolher=pasta"
grep -q -- "--escolher=salvar --titulo=Salvar --nome=doc.txt --pasta=$d --rotulo=Guardar" "$run/args.log" || falha "SaveFile devia mandar o nome, a pasta e o botão sem o _ ($(grep Salvar "$run/args.log"))"
grep -q "uint32 0" "$run/r_salvar.txt" || falha "SaveFile devia responder 0 ($(cat "$run/r_salvar.txt"))"
grep -q -- "--filtro=Imagens::g=\*.png,m=image/png --filtro=Tudo::g=\*" "$run/args.log" || falha "os filtros do app deviam ir como --filtro ($(grep Filtros "$run/args.log"))"
grep -q "uint32 1" "$run/r_cancelar.txt" && ! grep -q "uris" "$run/r_cancelar.txt" || falha "cancelar no seletor devia responder 1 sem URIs ($(cat "$run/r_cancelar.txt"))"
grep -q "uint32 0" "$run/r_salvarvarios.txt" && grep -q "pasta%20com%20espaco/a.txt" "$run/r_salvarvarios.txt" && grep -q "pasta%20com%20espaco/b.txt" "$run/r_salvarvarios.txt" || falha "SaveFiles devia devolver a pasta escolhida + cada nome ($(cat "$run/r_salvarvarios.txt"))"
grep -q "uint32 2" "$run/r_fechar.txt" || falha "Request.Close devia fazer a chamada responder 2 ($(cat "$run/r_fechar.txt"))"
if [ -f "$run/dormiu.pid" ] && kill -0 "$(cat "$run/dormiu.pid")" 2>/dev/null; then falha "Request.Close devia encerrar o seletor"; kill "$(cat "$run/dormiu.pid")" 2>/dev/null; fi
rm -rf "$run"
[ $ok = 1 ] && echo "portal (FileManager1 e seletor de arquivos): ok" || exit 1
