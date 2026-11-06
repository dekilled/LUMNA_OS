#!/bin/sh
# session/lumna-entrada-ajustar (o que o Painel muda na tela de login), com um /etc falso e um getent falso: sh testa_entrada_ajustar.sh PASTA_TEMP
T=$1; H=$(cd "$(dirname "$0")/../../.." && pwd)/session/lumna-entrada-ajustar
mkdir -p "$T/etc/greetd" "$T/bin"
printf '#!/bin/sh\n[ "$2" = maria ] && echo "maria:x:1000:1000::/home/maria:/bin/sh"\n[ "$2" = daemon ] && echo "daemon:x:1:1::/:/bin/false"\nexit 0\n' > "$T/bin/getent"; chmod +x "$T/bin/getent"
printf '[terminal]\nvt = 1\n\n[default_session]\ncommand = "lumna-entrada-greeter"\nuser = "greeter"\n\n[initial_session]\ncommand = "velho"\nuser = "x"\n\n[extra]\na = 1\n' > "$T/etc/greetd/config.toml"
export LUMNA_ETC="$T/etc" PATH="$T/bin:$PATH"
ok=1; falha() { echo "FALHOU: $*" >&2; ok=0; }
"$H" login-automatico ligar daemon 2>/dev/null && falha "um usuário de sistema não pode ser o automático"
"$H" login-automatico ligar fantasma 2>/dev/null && falha "um usuário que não existe não pode ser o automático"
"$H" login-automatico ligar maria || falha "ligar o login automático de um usuário de verdade"
[ "$(grep -c '^\[initial_session\]' "$T/etc/greetd/config.toml")" = 1 ] || falha "o bloco antigo devia ser trocado, não somado"
grep -q 'user = "maria"' "$T/etc/greetd/config.toml" || falha "o usuário devia ser a maria"
grep -q 'user = "greeter"' "$T/etc/greetd/config.toml" || falha "o default_session devia ficar"
grep -q '^\[extra\]' "$T/etc/greetd/config.toml" || falha "os outros blocos deviam ficar"
"$H" login-automatico desligar; grep -q initial_session "$T/etc/greetd/config.toml" && falha "desligar devia tirar o bloco"
"$H" layout lateral && "$H" layout centro && grep -q 'layout = centro' "$T/etc/lumna/entrada.ini" || falha "layout"
"$H" layout torto 2>/dev/null && falha "layout inválido"
echo img > "$T/foto.png"; "$H" papel "$T/foto.png" && grep -q "papel = $T/etc/lumna/entrada-papel.png" "$T/etc/lumna/entrada.ini" && [ -f "$T/etc/lumna/entrada-papel.png" ] || falha "papel"
echo x > "$T/script.sh"; "$H" papel "$T/script.sh" 2>/dev/null && falha "só imagens"
"$H" papel - ; grep -q papel "$T/etc/lumna/entrada.ini" && falha "papel - devia tirar a chave"; ls "$T"/etc/lumna/entrada-papel.* 2>/dev/null && falha "papel - devia apagar a imagem"
grep -q 'layout = centro' "$T/etc/lumna/entrada.ini" || falha "papel - não devia apagar o layout"
[ $ok = 1 ] && echo "lumna-entrada-ajustar ok"; [ $ok = 1 ]
