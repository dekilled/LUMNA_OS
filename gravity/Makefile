# Gravity — a interface do LUMNA OS em C.
#   make            a biblioteca (libgravity.a) e os exemplos
#   make mixer      só o mixer
PKGS = wayland-client wayland-cursor xkbcommon cairo pangocairo glib-2.0 gdk-pixbuf-2.0
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
	-Isrc -Ivendor/clay -I. $(shell pkg-config --cflags $(PKGS))
LDLIBS = $(shell pkg-config --libs $(PKGS)) -lm
WAYLAND_PROTOCOLS = $(shell pkg-config --variable=pkgdatadir wayland-protocols)
WAYLAND_SCANNER = $(shell pkg-config --variable=wayland_scanner wayland-scanner)

FONTES = src/gv_app.c src/gv_texto.c src/gv_selecao.c src/gv_tema.c src/gv_anim.c src/gv_svg.c src/gv_icones.c \
	src/gv_desenho.c src/gv_componentes.c src/gv_campo.c src/gv_escrita.c src/gv_rolagem.c src/gv_imagem.c src/gv_foco.c src/gv_janelas.c src/gv_clay.c src/gv_crash.c
PROTOS = xdg-shell-client-protocol.h xdg-shell-protocol.c \
	text-input-unstable-v3-client-protocol.h text-input-unstable-v3-protocol.c \
	wlr-layer-shell-unstable-v1-client-protocol.h wlr-layer-shell-unstable-v1-protocol.c \
	wlr-foreign-toplevel-management-unstable-v1-client-protocol.h wlr-foreign-toplevel-management-unstable-v1-protocol.c \
	ext-session-lock-v1-client-protocol.h ext-session-lock-v1-protocol.c
OBJS = $(FONTES:.c=.o) xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o text-input-unstable-v3-protocol.o \
	wlr-foreign-toplevel-management-unstable-v1-protocol.o ext-session-lock-v1-protocol.o

all: libgravity.a mixer vitrine galeria musica wifi painel apps papel barra rede transferencia dock lancador avisos bloqueio entrada ocioso inicio energia luz modos connect midia emma emma-chat emma-app perfil captura polkit polkit-janela instalador pulse teclado atlas discos musica loja widgets setup jogos gamepad

wlr-virtual-pointer-unstable-v1-client-protocol.h: protocolos/wlr-virtual-pointer-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
wlr-virtual-pointer-unstable-v1-protocol.c: protocolos/wlr-virtual-pointer-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@

xdg-shell-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml $@
text-input-unstable-v3-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/unstable/text-input/text-input-unstable-v3.xml $@
text-input-unstable-v3-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/unstable/text-input/text-input-unstable-v3.xml $@
ext-session-lock-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@
ext-session-lock-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/ext-session-lock/ext-session-lock-v1.xml $@
wlr-layer-shell-unstable-v1-client-protocol.h: protocolos/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
wlr-layer-shell-unstable-v1-protocol.c: protocolos/wlr-layer-shell-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
wlr-foreign-toplevel-management-unstable-v1-client-protocol.h: protocolos/wlr-foreign-toplevel-management-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
wlr-foreign-toplevel-management-unstable-v1-protocol.c: protocolos/wlr-foreign-toplevel-management-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@

src/gv_app.o: xdg-shell-client-protocol.h wlr-layer-shell-unstable-v1-client-protocol.h ext-session-lock-v1-client-protocol.h
src/gv_escrita.o: text-input-unstable-v3-client-protocol.h
src/gv_janelas.o: wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
$(OBJS): src/gravity.h src/gv_interno.h vendor/clay/clay.h

# o Clay é de fora: os avisos dele não são nossos
src/gv_clay.o: CFLAGS += -w

libgravity.a: $(OBJS)
	$(AR) rcs $@ $^

# o mixer usa o serviço de som (libpulse: o PipeWire responde pelo pipewire-pulse)
mixer: exemplos/mixer.c servicos/som.c servicos/som.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags libpulse) -o $@ exemplos/mixer.c servicos/som.c \
		libgravity.a $(LDLIBS) $(shell pkg-config --libs libpulse) -lpthread

# a vitrine dos componentes (não é o app de fotos, que é o galeria)
vitrine: exemplos/vitrine.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

wifi: exemplos/wifi.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

# o Painel: a moldura e uma categoria por arquivo em exemplos/painel/
PAINEL = exemplos/painel.c exemplos/painel/comum.c exemplos/painel/aparencia.c exemplos/painel/categorias.c \
	exemplos/painel/papel.c exemplos/painel/estilo.c exemplos/painel/barra.c exemplos/painel/bloqueio.c \
	exemplos/painel/desenvolvedor.c exemplos/painel/atalhos.c exemplos/painel/rede.c exemplos/painel/mesclar.c exemplos/painel/bluetooth.c exemplos/painel/modos.c exemplos/painel/areas.c exemplos/painel/sobre.c exemplos/painel/datahora.c exemplos/painel/apps.c exemplos/painel/monitores.c exemplos/painel/teclado.c exemplos/painel/teclado_tela.c servicos/teclado_cfg.c servicos/teclado_layout.c exemplos/painel/emma.c exemplos/painel/persona.c exemplos/painel/importar.c exemplos/painel/ativacao.c exemplos/painel/mcp.c exemplos/painel/recursos.c exemplos/painel/voz.c exemplos/painel/som.c servicos/som.c exemplos/painel/inicializacao.c servicos/autostart.c servicos/segundoplano.c servicos/greetd.c exemplos/painel/privacidade_pg.c servicos/appspadrao.c servicos/permissoes.c servicos/privacidade.c exemplos/painel/idioma.c servicos/idioma.c exemplos/painel/energia.c servicos/bateria.c servicos/perfil.c exemplos/painel/usuarios.c servicos/contas.c exemplos/painel/sons.c servicos/sons.c exemplos/painel/luz.c servicos/luz.c exemplos/painel/emma_dados.c exemplos/painel/connect.c servicos/connectc.c servicos/emmac.c servicos/emmar.c servicos/telas.c servicos/entrada.c exemplos/painel/armazenamento.c servicos/armazenamento.c servicos/discos_cfg.c servicos/pacotes.c servicos/sistema.c servicos/hora.c \
	servicos/papelini.c servicos/entrar.c servicos/desenvolvedor.c servicos/rede.c servicos/bluetooth.c servicos/modos.c servicos/json.c
painel: $(PAINEL) exemplos/painel/painel.h servicos/papelini.h servicos/entrar.h servicos/desenvolvedor.h servicos/rede.h servicos/bluetooth.h libgravity.a
	$(CC) $(CFLAGS) -Iexemplos/painel -Iservicos $(shell pkg-config --cflags gio-unix-2.0 libpulse) -o $@ $(PAINEL) libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-unix-2.0 libpulse) -lpthread

apps: exemplos/apps.c servicos/desktopenv.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -o $@ $< libgravity.a $(LDLIBS)

papel: exemplos/papel.c servicos/papelini.c servicos/papelini.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -o $@ exemplos/papel.c servicos/papelini.c libgravity.a $(LDLIBS)

barra: exemplos/barra.c servicos/mpris.c servicos/mpris.h servicos/bateria.c servicos/bateria.h servicos/emmac.c servicos/emmac.h servicos/emma_ativa.c servicos/emma_ativa.h servicos/json.c servicos/json.h servicos/hora.c servicos/hora.h servicos/rede.c servicos/bluetooth.c servicos/som.c servicos/bandeja.c servicos/modos.c servicos/bandeja.h servicos/modos.h servicos/rede.h servicos/bluetooth.h servicos/som.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0 libpulse) -o $@ exemplos/barra.c servicos/mpris.c servicos/bateria.c servicos/emmac.c servicos/emma_ativa.c servicos/json.c servicos/hora.c servicos/rede.c servicos/bluetooth.c servicos/som.c servicos/bandeja.c servicos/modos.c \
		libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0 libpulse) -lpthread

rede: exemplos/rede.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

transferencia: exemplos/transferencia.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

dock: exemplos/dock.c servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/dock.c servicos/modos.c libgravity.a $(LDLIBS)

captura: exemplos/captura.c servicos/telas.c servicos/telas.h servicos/modos.c servicos/modos.h servicos/json.c servicos/emmac.c servicos/emmac.h servicos/connectc.c servicos/connectc.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gdk-pixbuf-2.0 gio-2.0) -o $@ exemplos/captura.c servicos/telas.c servicos/modos.c servicos/json.c servicos/emmac.c servicos/connectc.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gdk-pixbuf-2.0 gio-2.0) -lm

perfil: exemplos/perfil.c servicos/emmac.c servicos/emmac.h servicos/json.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/perfil.c servicos/emmac.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

lancador: exemplos/lancador.c servicos/desktopenv.h servicos/privacidade.c servicos/privacidade.h servicos/modos.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/lancador.c servicos/privacidade.c servicos/modos.c libgravity.a $(LDLIBS)

# os avisos: o servidor org.freedesktop.Notifications (GDBus) e o canto e a central
avisos: exemplos/avisos.c servicos/avisos.c servicos/avisos.h servicos/sons.c servicos/sons.h servicos/modos.c servicos/modos.h servicos/json.c servicos/json.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/avisos.c servicos/avisos.c servicos/sons.c servicos/modos.c servicos/json.c \
		libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

# o seletor de modo (Desktop, Gaming, Mídia) e o serviço dos modos (servicos/modos.c)
modos: exemplos/modos.c servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/modos.c servicos/modos.c libgravity.a $(LDLIBS)

# trancar sozinho: sem mexer (ext-idle-notify) e antes de dormir (logind); não usa o Gravity
ext-idle-notify-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@
ext-idle-notify-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@
wlr-output-power-management-unstable-v1-client-protocol.h: protocolos/wlr-output-power-management-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
wlr-output-power-management-unstable-v1-protocol.c: protocolos/wlr-output-power-management-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
ocioso: exemplos/ocioso.c ext-idle-notify-v1-client-protocol.h ext-idle-notify-v1-protocol.c wlr-output-power-management-unstable-v1-client-protocol.h wlr-output-power-management-unstable-v1-protocol.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags gio-unix-2.0) -o $@ exemplos/ocioso.c ext-idle-notify-v1-protocol.c wlr-output-power-management-unstable-v1-protocol.c \
		$(shell pkg-config --libs wayland-client gio-unix-2.0)

wlr-gamma-control-unstable-v1-client-protocol.h: protocolos/wlr-gamma-control-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
wlr-gamma-control-unstable-v1-protocol.c: protocolos/wlr-gamma-control-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
luz: exemplos/luz.c servicos/luz.c servicos/luz.h wlr-gamma-control-unstable-v1-client-protocol.h wlr-gamma-control-unstable-v1-protocol.c
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/luz.c servicos/luz.c wlr-gamma-control-unstable-v1-protocol.c \
		$(shell pkg-config --libs wayland-client glib-2.0) -lm

energia: exemplos/energia.c servicos/energia.c servicos/energia.h servicos/json.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/energia.c servicos/energia.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

inicio: exemplos/inicio.c servicos/autostart.c servicos/sessao.c servicos/json.c servicos/modos.c
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-unix-2.0) -o $@ exemplos/inicio.c servicos/autostart.c servicos/sessao.c \
		servicos/json.c servicos/modos.c $(shell pkg-config --libs gio-unix-2.0)

# a tela de bloqueio: o PIN e a senha (servicos/entrar.c), o que toca (servicos/mpris.c) e o papel
bloqueio: exemplos/bloqueio.c servicos/hora.c servicos/hora.h servicos/entrar.c servicos/mpris.c servicos/modos.c servicos/json.c servicos/papelini.c servicos/desenvolvedor.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/bloqueio.c servicos/hora.c servicos/entrar.c \
		servicos/mpris.c servicos/modos.c servicos/json.c servicos/papelini.c servicos/desenvolvedor.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

# a tela de login (o greetd corre-a dentro de um Orbit só dela: orbit -S lumna-entrada)
entrada: exemplos/entrada.c servicos/greetd.c servicos/papelini.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos -o $@ exemplos/entrada.c servicos/greetd.c servicos/papelini.c libgravity.a $(LDLIBS)

# o agente do polkit (a senha de administrador): o serviço sem a interface, e a janela de senha em Gravity
polkit: exemplos/polkit.c servicos/polkitag.c servicos/polkitag.h
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/polkit.c servicos/polkitag.c $(shell pkg-config --libs gio-2.0)

polkit-janela: exemplos/polkit-janela.c libgravity.a
	$(CC) $(CFLAGS) -o $@ exemplos/polkit-janela.c libgravity.a $(LDLIBS)

# o teclado na tela (lumna-teclado): uma camada, o input-method-v2 e um teclado virtual
input-method-unstable-v2-client-protocol.h: protocolos/input-method-unstable-v2.xml
	$(WAYLAND_SCANNER) client-header $< $@
input-method-unstable-v2-protocol.c: protocolos/input-method-unstable-v2.xml
	$(WAYLAND_SCANNER) private-code $< $@
virtual-keyboard-unstable-v1-client-protocol.h: protocolos/virtual-keyboard-unstable-v1.xml
	$(WAYLAND_SCANNER) client-header $< $@
virtual-keyboard-unstable-v1-protocol.c: protocolos/virtual-keyboard-unstable-v1.xml
	$(WAYLAND_SCANNER) private-code $< $@
servicos/teclado_im.o: input-method-unstable-v2-client-protocol.h virtual-keyboard-unstable-v1-client-protocol.h
teclado_im_obj = input-method-unstable-v2-protocol.c virtual-keyboard-unstable-v1-protocol.c

teclado: exemplos/teclado.c servicos/teclado_im.c servicos/teclado_im.h servicos/teclado_layout.c servicos/teclado_layout.h servicos/teclado_cfg.c servicos/teclado_cfg.h servicos/modos.c input-method-unstable-v2-client-protocol.h virtual-keyboard-unstable-v1-client-protocol.h $(teclado_im_obj) libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags pangocairo gio-2.0 wayland-client xkbcommon) -o $@ exemplos/teclado.c servicos/teclado_im.c servicos/teclado_layout.c servicos/teclado_cfg.c servicos/modos.c $(teclado_im_obj) libgravity.a $(LDLIBS) $(shell pkg-config --libs pangocairo gio-2.0) -lm

# a Galeria de fotos e vídeos (lumna-galeria): o serviço (galeria.c) mais as operações de arquivo do Atlas (lixeira, copiar) e o cliente do Connect
GALERIA = exemplos/galeria/principal.c exemplos/galeria/grade.c exemplos/galeria/visualizador.c exemplos/galeria/ajustes.c exemplos/galeria/editor.c exemplos/galeria/editor_video.c exemplos/galeria/player.c
galeria: $(GALERIA) exemplos/galeria/app.h servicos/galeria.c servicos/galeria.h servicos/edicao.c servicos/edicao.h servicos/edicao_video.c servicos/edicao_video.h servicos/video_player.c servicos/video_player.h servicos/musica_mpris.c servicos/musica_mpris.h servicos/arquivos.c servicos/arquivos.h servicos/arquivos_op.c servicos/arquivos_op.h servicos/connectc.c servicos/connectc.h servicos/json.c servicos/json.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -Iexemplos/galeria $(shell pkg-config --cflags pangocairo gio-2.0 gdk-pixbuf-2.0 mpv) -o $@ $(GALERIA) servicos/galeria.c servicos/edicao.c servicos/edicao_video.c servicos/video_player.c servicos/musica_mpris.c servicos/arquivos.c servicos/arquivos_op.c servicos/connectc.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs pangocairo gio-2.0 gdk-pixbuf-2.0 mpv) -lm -lpthread

# o player de música (lumna-music)
MUSICA = exemplos/musica/principal.c exemplos/musica/barra.c exemplos/musica/biblioteca.c exemplos/musica/ouvir.c exemplos/musica/ajustes.c
musica: $(MUSICA) exemplos/musica/app.h servicos/musica.c servicos/musica.h servicos/musica_player.c servicos/musica_player.h servicos/musica_mpris.c servicos/musica_mpris.h servicos/json.c servicos/json.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -Iexemplos/musica $(shell pkg-config --cflags pangocairo gio-2.0 gdk-pixbuf-2.0) -o $@ $(MUSICA) servicos/musica.c servicos/musica_player.c servicos/musica_mpris.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs pangocairo gio-2.0 gdk-pixbuf-2.0) -lm -lpthread

# os widgets da área de trabalho (lumna-widgets)
widgets: exemplos/widgets.c servicos/mpris.c servicos/mpris.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/widgets.c servicos/mpris.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0) -lm -lpthread

# a loja de aplicativos (lumna-loja)
LOJA = exemplos/loja/principal.c exemplos/loja/barra.c exemplos/loja/telas.c exemplos/loja/drivers.c
loja: $(LOJA) exemplos/loja/app.h servicos/loja.c servicos/loja.h servicos/loja_hw.c servicos/loja_hw.h servicos/json.c servicos/json.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -Iexemplos/loja $(shell pkg-config --cflags pangocairo gio-2.0 gdk-pixbuf-2.0) -o $@ $(LOJA) servicos/loja.c servicos/loja_hw.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs pangocairo gio-2.0 gdk-pixbuf-2.0) -lm -lpthread

# o pré-setup (lumna-setup): a configuração inicial depois da instalação
SETUP_SERV = servicos/setup.c servicos/emma_ativa.c servicos/emmar.c servicos/emmac.c servicos/autostart.c servicos/entrada.c servicos/modos.c servicos/idioma.c servicos/instalador.c servicos/connectc.c servicos/rede.c servicos/som.c servicos/sons.c servicos/json.c
setup: exemplos/setup.c $(SETUP_SERV) servicos/setup.h servicos/emma_ativa.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0 gio-unix-2.0 libpulse) -o $@ exemplos/setup.c $(SETUP_SERV) libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0 gio-unix-2.0 libpulse) -lpthread

# os controles do modo Gaming (lumna-gamepad)
gamepad: exemplos/gamepad.c servicos/gamepad.c servicos/gamepad.h servicos/gamepadc.c servicos/gamepadc.h servicos/emma_ativa.c servicos/json.c servicos/modos.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0 gio-2.0 gio-unix-2.0) -o $@ exemplos/gamepad.c servicos/gamepad.c servicos/gamepadc.c servicos/emma_ativa.c servicos/json.c servicos/modos.c $(shell pkg-config --libs glib-2.0 gio-2.0 gio-unix-2.0) -lm

# o modo Gaming: a biblioteca de jogos (lumna-jogos)
jogos: exemplos/jogos.c servicos/jogos.c servicos/jogos.h servicos/json.c servicos/json.h servicos/modos.c servicos/modos.h servicos/gamepad.c servicos/gamepad.h servicos/gamepadc.c servicos/gamepadc.h servicos/bluetooth.c servicos/bluetooth.h servicos/loja_steam.c servicos/loja_steam.h servicos/loja_gog.c servicos/loja_gog.h servicos/loja_epic.c servicos/loja_epic.h exemplos/jogos_loja.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0 gio-2.0 sqlite3) -o $@ exemplos/jogos.c servicos/jogos.c servicos/json.c servicos/modos.c servicos/gamepad.c servicos/gamepadc.c servicos/bluetooth.c servicos/loja_steam.c servicos/loja_gog.c servicos/loja_epic.c libgravity.a $(LDLIBS) $(shell pkg-config --libs glib-2.0 gio-2.0 sqlite3) -lpthread

# o explorador de arquivos (lumna-atlas)
atlas: exemplos/atlas.c servicos/arquivos.c servicos/arquivos.h servicos/arquivos_cfg.c servicos/arquivos_cfg.h servicos/arquivos_prop.c servicos/arquivos_prop.h servicos/arquivos_rede.c servicos/arquivos_rede.h servicos/arquivos_op.c servicos/arquivos_op.h servicos/arquivos_discos.c servicos/arquivos_discos.h servicos/armazenamento.c servicos/armazenamento.h servicos/connectc.c servicos/json.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags pangocairo gio-2.0) -o $@ exemplos/atlas.c servicos/arquivos.c servicos/arquivos_cfg.c servicos/arquivos_prop.c servicos/arquivos_rede.c servicos/arquivos_op.c servicos/arquivos_discos.c servicos/armazenamento.c servicos/connectc.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs pangocairo gio-2.0) -lm

# a montagem automática de pendrives e discos externos (lumna-discos): só GIO, sem tela
discos: exemplos/discos.c servicos/discos_cfg.c servicos/discos_cfg.h
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-unix-2.0) -o $@ exemplos/discos.c servicos/discos_cfg.c $(shell pkg-config --libs gio-unix-2.0)

# o Pulse Terminal (lumna-pulse): a libvterm e um pty, desenhados à mão
pulse: exemplos/pulse.c servicos/terminal.c servicos/terminal.h servicos/pulsedb.c servicos/pulsedb.h servicos/emma_terminal.c servicos/emma_terminal.h servicos/emmac.c servicos/json.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags vterm pangocairo sqlite3 gio-2.0) -o $@ exemplos/pulse.c servicos/terminal.c servicos/pulsedb.c servicos/emma_terminal.c servicos/emmac.c servicos/json.c libgravity.a $(LDLIBS) $(shell pkg-config --libs vterm sqlite3 gio-2.0) -lutil

# o instalador do LUMNA OS (Instalar LUMNA OS, da sessão live)
instalador: exemplos/instalador.c servicos/instalador.c servicos/instalador.h servicos/json.c servicos/json.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/instalador.c servicos/instalador.c servicos/json.c libgravity.a $(LDLIBS) -lpthread

clean:
	rm -f $(OBJS) libgravity.a gamepad jogos setup mixer vitrine galeria musica wifi painel apps papel barra rede transferencia dock lancador avisos bloqueio entrada ocioso inicio energia luz modos connect midia emma emma-chat emma-app perfil captura polkit polkit-janela instalador pulse teclado atlas discos loja widgets ext-idle-notify-v1-client-protocol.h ext-idle-notify-v1-protocol.c $(PROTOS)

.PHONY: all clean

# o controle do celular (servicos/json.c, mdns.c, ponteiro.c): um serviço da sessão, sem a biblioteca de interface
connect: exemplos/connect.c servicos/agenda.c servicos/agenda.h servicos/energia.c servicos/energia.h servicos/emmac.c servicos/emmac.h servicos/midiac.c servicos/midiac.h servicos/midia.h servicos/json.c servicos/json.h servicos/mdns.c servicos/mdns.h servicos/ponteiro.c servicos/ponteiro.h \
		servicos/modos.c servicos/modos.h servicos/mpris.c servicos/mpris.h wlr-virtual-pointer-unstable-v1-client-protocol.h \
		wlr-virtual-pointer-unstable-v1-protocol.c
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags libsoup-3.0 gio-2.0) -o $@ exemplos/connect.c servicos/agenda.c servicos/energia.c servicos/emmac.c servicos/midiac.c servicos/json.c servicos/mdns.c \
		servicos/ponteiro.c servicos/modos.c servicos/mpris.c wlr-virtual-pointer-unstable-v1-protocol.c \
		$(shell pkg-config --libs libsoup-3.0 gio-2.0 wayland-client) -lm

# a Mídia: o serviço (Firefox em kiosk, catálogo, recentes) e as telas (Início, capa, seletor)
midia: exemplos/midia.c servicos/midiad.c servicos/midiad.h servicos/midiac.c servicos/midiac.h servicos/midia.c servicos/midia.h \
		servicos/firefox.c servicos/firefox.h servicos/json.c servicos/json.h servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags libsoup-3.0 gio-2.0) -o $@ exemplos/midia.c servicos/midiad.c servicos/midiac.c \
		servicos/midia.c servicos/firefox.c servicos/json.c servicos/modos.c libgravity.a $(LDLIBS) $(shell pkg-config --libs libsoup-3.0 gio-2.0) -lpthread

# a extensão do Firefox da Mídia, empacotada (sem assinatura: ver extensao-firefox/LEIAME.md para assinar)
extensao:
	cd extensao-firefox && rm -f lumna-midia.xpi && zip -q lumna-midia.xpi manifest.json fundo.js

# a Emma em segundo plano (servicos/emma*.c): o serviço da sessão, sem a biblioteca de interface
EMMA = exemplos/emma.c servicos/emma_ativa.c servicos/entrar.c servicos/emmaa.c servicos/emmaq.c servicos/emmah.c servicos/emmab.c servicos/emmal.c servicos/emmas.c servicos/emmac.c servicos/emmav.c servicos/json.c servicos/midia.c servicos/midiac.c servicos/modos.c servicos/mpris.c
emma: $(EMMA) servicos/emmaa.h servicos/emmaq.h servicos/emmah.h servicos/emmab.h servicos/emmal.h servicos/emmas.h servicos/emmac.h servicos/emmav.h
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags libsoup-3.0 gio-2.0 gio-unix-2.0) -o $@ $(EMMA) $(shell pkg-config --libs libsoup-3.0 gio-2.0 gio-unix-2.0) -lm -lpthread

# o painel flutuante da Emma (à direita)
emma-app: exemplos/emma-app.c exemplos/emma-ui.c exemplos/emma-ui.h servicos/emma_ativa.c servicos/emmac.c servicos/emmac.h servicos/emmar.c servicos/emmar.h servicos/json.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos -Iexemplos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/emma-app.c exemplos/emma-ui.c servicos/emma_ativa.c servicos/emmac.c servicos/emmar.c servicos/json.c servicos/destaque.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

emma-chat: exemplos/emma-chat.c exemplos/emma-ui.c exemplos/emma-ui.h servicos/emma_ativa.c servicos/emmac.c servicos/emmac.h servicos/json.c servicos/modos.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -Iexemplos -o $@ exemplos/emma-chat.c exemplos/emma-ui.c servicos/emma_ativa.c servicos/emmac.c servicos/json.c servicos/modos.c servicos/destaque.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)
