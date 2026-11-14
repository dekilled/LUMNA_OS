#!/bin/sh
# session/lumna-navegadores-barra numa casa de mentira: liga a barra de título do sistema nos Chromium e no Firefox só onde a pessoa nunca escolheu e o navegador está fechado,
# respeita a escolha dela, cria o perfil de um navegador instalado que nunca abriu, e desfaz. Uso: sh iso/testes/navegadores_barra.sh
aqui=$(cd "$(dirname "$0")/../.." && pwd); ajudante="$aqui/session/lumna-navegadores-barra"
command -v perl >/dev/null && perl -MJSON::PP -e1 2>/dev/null || { echo "navegadores_barra: dispensado (falta perl com JSON::PP)"; exit 0; }
run=$(mktemp -d); H="$run/casa"; C="$H/.config"; mkdir -p "$C" "$run/bin"
export HOME="$H" XDG_CONFIG_HOME="$C" PATH="$run/bin:$PATH"
ok=1; falha() { echo "FALHOU: $1" >&2; ok=0; }
chave() { perl -MJSON::PP -e 'local $/; open F, "<", $ARGV[0] or exit 3; my $j = decode_json(<F>); my $v = $j->{browser}{custom_chrome_frame}; print defined $v ? ($v ? "true" : "false") : "ausente"' "$1"; }
# Chrome: um perfil sem a chave, um onde a pessoa escolheu a barra própria
mkdir -p "$C/google-chrome/Default" "$C/google-chrome/Profile 1"
echo '{"browser":{"show_home_button":true},"zzz":1}' > "$C/google-chrome/Default/Preferences"
echo '{"browser":{"custom_chrome_frame":true}}' > "$C/google-chrome/Profile 1/Preferences"
# Edge aberto (SingletonLock aponta para um processo vivo): não se mexe
mkdir -p "$C/microsoft-edge/Default"; echo '{}' > "$C/microsoft-edge/Default/Preferences"
sleep 30 & vivo=$!; ln -s "casa-$vivo" "$C/microsoft-edge/SingletonLock"
# Brave instalado e nunca aberto: o perfil nasce
printf '#!/bin/sh\n' > "$run/bin/brave"; chmod +x "$run/bin/brave"
# Firefox: quatro perfis
FF="$H/.mozilla/firefox"; mkdir -p "$FF/a.default" "$FF/b.escolhido" "$FF/c.userjs" "$FF/d.aberto"
cat > "$FF/profiles.ini" <<FIM
[Profile0]
Name=a
IsRelative=1
Path=a.default

[Profile1]
Name=b
IsRelative=1
Path=b.escolhido

[Profile2]
Name=c
IsRelative=1
Path=c.userjs

[Profile3]
Name=d
IsRelative=1
Path=d.aberto
FIM
echo 'user_pref("browser.startup.homepage", "about:blank");' > "$FF/a.default/prefs.js"
printf 'user_pref("browser.tabs.inTitlebar", 1);\n' > "$FF/b.escolhido/prefs.js"
echo 'user_pref("x", 1);' > "$FF/c.userjs/prefs.js"; echo 'user_pref("browser.tabs.inTitlebar", 1);' > "$FF/c.userjs/user.js"
echo 'user_pref("x", 2);' > "$FF/d.aberto/prefs.js"; ln -s "127.0.0.1:+$vivo" "$FF/d.aberto/lock"
sh -c "$ajudante" > "$run/saida1.txt"
[ "$(chave "$C/google-chrome/Default/Preferences")" = false ] || falha "o perfil do Chrome sem escolha devia ganhar custom_chrome_frame = false"
grep -q '"show_home_button"' "$C/google-chrome/Default/Preferences" && grep -q '"zzz"' "$C/google-chrome/Default/Preferences" || falha "o resto do Preferences do Chrome não podia mudar"
[ "$(chave "$C/google-chrome/Profile 1/Preferences")" = true ] || falha "a escolha da pessoa (barra própria) não podia ser desfeita"
[ "$(chave "$C/microsoft-edge/Default/Preferences")" = ausente ] || falha "com o Edge aberto não se mexe no perfil"
[ "$(chave "$C/BraveSoftware/Brave-Browser/Default/Preferences")" = false ] || falha "o Brave instalado que nunca abriu devia ganhar o perfil com a barra do sistema"
grep -q 'inTitlebar", 0' "$FF/a.default/prefs.js" || falha "o perfil do Firefox sem escolha devia ganhar browser.tabs.inTitlebar = 0"
grep -q 'homepage' "$FF/a.default/prefs.js" || falha "o resto do prefs.js não podia mudar"
grep -q 'inTitlebar", 1' "$FF/b.escolhido/prefs.js" && ! grep -q 'inTitlebar", 0' "$FF/b.escolhido/prefs.js" || falha "a escolha da pessoa no Firefox (barra própria) não podia mudar"
! grep -q inTitlebar "$FF/c.userjs/prefs.js" || falha "com o user.js definindo a chave, o prefs.js não se mexe"
! grep -q inTitlebar "$FF/d.aberto/prefs.js" || falha "com o Firefox aberto não se mexe no perfil"
# idempotente: a segunda vez não muda nem avisa de nada
sh -c "$ajudante" > "$run/saida2.txt"
[ ! -s "$run/saida2.txt" ] || falha "a segunda execução não devia fazer nada ($(cat "$run/saida2.txt"))"
# desligado pela pessoa
mkdir -p "$C/lumna"; printf 'barra_do_sistema = nao\n' > "$C/lumna/navegadores.ini"
rm -rf "$C/google-chrome" "$C/BraveSoftware"; mkdir -p "$C/google-chrome/Default"; echo '{}' > "$C/google-chrome/Default/Preferences"
sh -c "$ajudante" > "$run/saida3.txt"
[ "$(chave "$C/google-chrome/Default/Preferences")" = ausente ] || falha "com barra_do_sistema = nao no navegadores.ini não se mexe em nada"
rm -f "$C/lumna/navegadores.ini"
# desfazer
sh -c "$ajudante" > /dev/null
[ "$(chave "$C/google-chrome/Default/Preferences")" = false ] || falha "(preparo) o perfil devia ter ficado com a barra do sistema"
sh -c "$ajudante --desfazer" > /dev/null
[ "$(chave "$C/google-chrome/Default/Preferences")" = true ] || falha "--desfazer devia devolver a barra própria do Chrome"
grep -q 'inTitlebar", 1' "$FF/a.default/prefs.js" || falha "--desfazer devia devolver a barra própria do Firefox"
kill $vivo 2>/dev/null
[ $ok = 1 ] && { echo "navegadores_barra: ok"; rm -rf "$run"; } || { echo "(em $run)"; exit 1; }
