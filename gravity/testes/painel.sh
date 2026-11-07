#!/bin/sh
# Testa os serviços do Painel: sistema.c (Sobre), pacotes.c (Aplicativos e Armazenamento) e hora.c (formato 12/24 h),
# com árvores falsas (testes/painel/): nada toca no sistema de verdade.
set -e
AQUI=$(cd "$(dirname "$0")/.." && pwd)
T=$(mktemp -d)
cd "$AQUI"
CF="-std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Iservicos $(pkg-config --cflags glib-2.0)"
LG="$(pkg-config --libs glib-2.0)"

# pacotes
cc $CF -o "$T/testa_pacotes" testes/painel/testa_pacotes.c servicos/pacotes.c $LG
ENVV=$(python3 testes/painel/monta_pacman_falso.py "$T/pac")
env -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_CONFIG_HOME $ENVV "$T/testa_pacotes"

# sistema (Sobre)
cc $CF -o "$T/testa_sistema" testes/painel/testa_sistema.c servicos/sistema.c $LG
python3 testes/painel/monta_sistema_falso.py "$T/raiz"
LUMNA_RAIZ_SISTEMA="$T/raiz" PATH="$T/raiz/bin:$PATH" "$T/testa_sistema"

# hora (formato)
cc $CF -o "$T/testa_hora" testes/painel/testa_hora.c servicos/hora.c $LG
mkdir -p "$T/cfg/lumna"
env -u XDG_DATA_HOME XDG_CONFIG_HOME="$T/cfg" "$T/testa_hora" "$T/cfg/lumna/hora.ini"
# entrada e telas (Teclado e mouse, Monitores)
cc $CF -o "$T/testa_entrada" testes/painel/testa_entrada.c servicos/entrada.c servicos/telas.c servicos/json.c servicos/modos.c $LG -lm
mkdir -p "$T/cfg2"
env -u XDG_DATA_HOME XDG_CONFIG_HOME="$T/cfg2" LUMNA_XKB_LST="$AQUI/testes/painel/evdev-falso.lst" env -u XKB_DEFAULT_LAYOUT "$T/testa_entrada"
# apps que abrem ao entrar (autostart)
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/testa_autostart" testes/painel/testa_autostart.c servicos/autostart.c $(pkg-config --libs gio-unix-2.0)
mkdir -p "$T/auto"
env -u XDG_DATA_HOME "$T/testa_autostart" "$T/auto"
# a sessão (reabrir os apps do último login)
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/testa_sessao" testes/painel/testa_sessao.c servicos/sessao.c servicos/json.c servicos/modos.c $(pkg-config --libs gio-unix-2.0)
mkdir -p "$T/sess"
env "$T/testa_sessao" "$T/sess"
# o que o Painel muda na tela de login (session/lumna-entrada-ajustar)
sh testes/painel/testa_entrada_ajustar.sh "$T/ent"
# os serviços do usuário (Em segundo plano), com um systemctl falso
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/testa_segundoplano" testes/painel/testa_segundoplano.c servicos/segundoplano.c $(pkg-config --libs gio-unix-2.0)
printf 'syncthing.service sim sim Syncthing - Open Source Continuous File Synchronization\nbackup.service nao nao Copia de seguranca\npipewire.service sim sim PipeWire\n' > "$T/fundo.txt"
FUNDO_ESTADO="$T/fundo.txt" PATH="$AQUI/testes/painel/bin:$PATH" "$T/testa_segundoplano"
# os apps padrão (mimeapps.list)
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/testa_appspadrao" testes/painel/testa_appspadrao.c servicos/appspadrao.c $(pkg-config --libs gio-unix-2.0)
mkdir -p "$T/padrao"
"$T/testa_appspadrao" "$T/padrao"
# as permissões dos apps (PermissionStore dos portais), contra um de mentira
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/testa_permissoes" testes/painel/testa_permissoes.c servicos/permissoes.c $(pkg-config --libs gio-unix-2.0)
cc $CF $(pkg-config --cflags gio-unix-2.0) -o "$T/permissoes_falsas" testes/painel/permissoes_falsas.c $(pkg-config --libs gio-unix-2.0)
dbus-run-session -- sh -c "'$T/permissoes_falsas' > '$T/pf.log' 2>&1 & pf=\$!; sleep 1; '$T/testa_permissoes'; r=\$?; kill \$pf; exit \$r" > "$T/perm.log" 2>&1 || true
grep -v "fd limit" "$T/perm.log"
grep -q "permissoes ok" "$T/perm.log"
# privacidade (recentes e logs)
cc $CF -o "$T/testa_privacidade" testes/painel/testa_privacidade.c servicos/privacidade.c servicos/modos.c $LG
mkdir -p "$T/priv"
"$T/testa_privacidade" "$T/priv"
# idioma e região (localectl e locale falsos, e o session/lumna-idioma-gerar de verdade)
cc $CF -o "$T/testa_idioma" testes/painel/testa_idioma.c servicos/idioma.c $LG
mkdir -p "$T/idi"
PATH="$AQUI/testes/painel/bin:$PATH" "$T/testa_idioma" "$T/idi" "$AQUI/../session/lumna-idioma-gerar"
# a bateria (/sys falso)
cc $CF -o "$T/testa_bateria" testes/painel/testa_bateria.c servicos/bateria.c $LG
mkdir -p "$T/bat"
"$T/testa_bateria" "$T/bat"
# o perfil de energia (powerprofilesctl falso)
cc $CF -o "$T/testa_perfil" testes/painel/testa_perfil.c servicos/perfil.c $LG
echo balanced > "$T/perfil.txt"
PERFIL_ESTADO="$T/perfil.txt" PATH="$AQUI/testes/painel/bin:$PATH" "$T/testa_perfil"
# as contas (Usuários): /etc falso e o session/lumna-usuarios em modo seco, com getent e id falsos
mkdir -p "$T/usr/bin"
printf '#!/bin/sh\n[ "$1" = passwd ] && grep "^$2:" "$LUMNA_ETC/passwd"\n[ "$1" = group ] && grep "^$2:" "$LUMNA_ETC/group"\nexit 0\n' > "$T/usr/bin/getent"; chmod +x "$T/usr/bin/getent"
cc $CF $(pkg-config --cflags gdk-pixbuf-2.0) -o "$T/testa_contas" testes/painel/testa_contas.c servicos/contas.c $LG $(pkg-config --libs gdk-pixbuf-2.0)
mkdir -p "$T/con"
PATH="$T/usr/bin:$PATH" "$T/testa_contas" "$T/con" "$AQUI/../session/lumna-usuarios"
# os sons do sistema (pasta de sons e pw-play falsos)
cc $CF -o "$T/testa_sons" testes/painel/testa_sons.c servicos/sons.c $LG
mkdir -p "$T/som"
PATH="$AQUI/testes/painel/bin:$PATH" "$T/testa_sons" "$T/som"
# a luz noturna (horário, temperatura e rampas)
cc $CF -o "$T/testa_luz" testes/painel/testa_luz.c servicos/luz.c $LG -lm
mkdir -p "$T/luz"
"$T/testa_luz" "$T/luz"
cc $CF -o "$T/testa_desktopenv" testes/painel/testa_desktopenv.c -Iservicos
"$T/testa_desktopenv"
# o instalador: discos, fusos, validações e a execução com o lumna-install de mentira
chmod +x testes/instalador/instalar-falso.sh
cc $CF -o "$T/testa_instalador" testes/painel/testa_instalador.c servicos/instalador.c servicos/json.c $LG -lpthread
"$T/testa_instalador" "$AQUI/testes/instalador/instalar-falso.sh"
# o agente do polkit: as partes puras e o agente inteiro contra um polkit de mentira (precisa do python3.12 com dbus)
cc $CF -o "$T/testa_polkitag" testes/painel/testa_polkitag.c servicos/polkitag.c $LG
"$T/testa_polkitag"
if /usr/bin/python3.12 -c "import dbus" 2>/dev/null && command -v gdbus >/dev/null; then
	make polkit >/dev/null && sh testes/polkit/polkit.sh "$AQUI"
else
	echo "(sem python3.12 com dbus: o teste do agente do polkit ficou de fora)"
fi
# as atualizações: o snapshot antes de atualizar (e o voltar) e a publicação dos pacotes (poda e conferência)
bash testes/pkg/snapshot.sh
if command -v bsdtar >/dev/null && command -v xmllint >/dev/null; then
	bash testes/publica/limpar.sh
else
	echo "(sem bsdtar/xmllint: o teste da publicação dos pacotes ficou de fora)"
fi
# o lumna-install em modo seco: o modo de boot (UEFI ou BIOS) e o que cada um exige
bash testes/instalador/dryrun.sh
echo "(arquivos de teste em $T)"
