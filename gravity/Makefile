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

FONTES = src/gv_app.c src/gv_texto.c src/gv_tema.c src/gv_anim.c src/gv_svg.c src/gv_icones.c \
	src/gv_desenho.c src/gv_componentes.c src/gv_campo.c src/gv_escrita.c src/gv_rolagem.c src/gv_imagem.c src/gv_foco.c src/gv_janelas.c src/gv_clay.c
PROTOS = xdg-shell-client-protocol.h xdg-shell-protocol.c \
	text-input-unstable-v3-client-protocol.h text-input-unstable-v3-protocol.c \
	wlr-layer-shell-unstable-v1-client-protocol.h wlr-layer-shell-unstable-v1-protocol.c \
	wlr-foreign-toplevel-management-unstable-v1-client-protocol.h wlr-foreign-toplevel-management-unstable-v1-protocol.c \
	ext-session-lock-v1-client-protocol.h ext-session-lock-v1-protocol.c
OBJS = $(FONTES:.c=.o) xdg-shell-protocol.o wlr-layer-shell-unstable-v1-protocol.o text-input-unstable-v3-protocol.o \
	wlr-foreign-toplevel-management-unstable-v1-protocol.o ext-session-lock-v1-protocol.o

all: libgravity.a mixer galeria wifi painel apps papel barra rede transferencia dock lancador avisos bloqueio entrada ocioso modos controle

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

galeria: exemplos/galeria.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

wifi: exemplos/wifi.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

# o Painel: a moldura e uma categoria por arquivo em exemplos/painel/
PAINEL = exemplos/painel.c exemplos/painel/comum.c exemplos/painel/aparencia.c exemplos/painel/categorias.c \
	exemplos/painel/papel.c exemplos/painel/estilo.c exemplos/painel/barra.c exemplos/painel/bloqueio.c \
	exemplos/painel/desenvolvedor.c exemplos/painel/atalhos.c exemplos/painel/rede.c exemplos/painel/mesclar.c exemplos/painel/bluetooth.c exemplos/painel/modos.c exemplos/painel/areas.c \
	servicos/papelini.c servicos/entrar.c servicos/desenvolvedor.c servicos/rede.c servicos/bluetooth.c servicos/modos.c
painel: $(PAINEL) exemplos/painel/painel.h servicos/papelini.h servicos/entrar.h servicos/desenvolvedor.h servicos/rede.h servicos/bluetooth.h libgravity.a
	$(CC) $(CFLAGS) -Iexemplos/painel -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ $(PAINEL) libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

apps: exemplos/apps.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

papel: exemplos/papel.c servicos/papelini.c servicos/papelini.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos -o $@ exemplos/papel.c servicos/papelini.c libgravity.a $(LDLIBS)

barra: exemplos/barra.c servicos/rede.c servicos/bluetooth.c servicos/som.c servicos/bandeja.c servicos/modos.c servicos/bandeja.h servicos/modos.h servicos/rede.h servicos/bluetooth.h servicos/som.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0 libpulse) -o $@ exemplos/barra.c servicos/rede.c servicos/bluetooth.c servicos/som.c servicos/bandeja.c servicos/modos.c \
		libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0 libpulse) -lpthread

rede: exemplos/rede.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

transferencia: exemplos/transferencia.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

dock: exemplos/dock.c servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/dock.c servicos/modos.c libgravity.a $(LDLIBS)

lancador: exemplos/lancador.c libgravity.a
	$(CC) $(CFLAGS) -o $@ $< libgravity.a $(LDLIBS)

# os avisos: o servidor org.freedesktop.Notifications (GDBus) e o canto e a central
avisos: exemplos/avisos.c servicos/avisos.c servicos/avisos.h servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/avisos.c servicos/avisos.c servicos/modos.c \
		libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

# o seletor de modo (Desktop, Gaming, Mídia) e o serviço dos modos (servicos/modos.c)
modos: exemplos/modos.c servicos/modos.c servicos/modos.h libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags glib-2.0) -o $@ exemplos/modos.c servicos/modos.c libgravity.a $(LDLIBS)

# trancar sozinho: sem mexer (ext-idle-notify) e antes de dormir (logind); não usa o Gravity
ext-idle-notify-v1-client-protocol.h:
	$(WAYLAND_SCANNER) client-header $(WAYLAND_PROTOCOLS)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@
ext-idle-notify-v1-protocol.c:
	$(WAYLAND_SCANNER) private-code $(WAYLAND_PROTOCOLS)/staging/ext-idle-notify/ext-idle-notify-v1.xml $@
ocioso: exemplos/ocioso.c ext-idle-notify-v1-client-protocol.h ext-idle-notify-v1-protocol.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags gio-unix-2.0) -o $@ exemplos/ocioso.c ext-idle-notify-v1-protocol.c \
		$(shell pkg-config --libs wayland-client gio-unix-2.0)

# a tela de bloqueio: o PIN e a senha (servicos/entrar.c), o que toca (servicos/mpris.c) e o papel
bloqueio: exemplos/bloqueio.c servicos/entrar.c servicos/mpris.c servicos/papelini.c servicos/desenvolvedor.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags gio-2.0) -o $@ exemplos/bloqueio.c servicos/entrar.c \
		servicos/mpris.c servicos/papelini.c servicos/desenvolvedor.c libgravity.a $(LDLIBS) $(shell pkg-config --libs gio-2.0)

# a tela de login (o greetd corre-a dentro de um Orbit só dela: orbit -S lumna-entrada)
entrada: exemplos/entrada.c servicos/greetd.c servicos/papelini.c libgravity.a
	$(CC) $(CFLAGS) -Iservicos -o $@ exemplos/entrada.c servicos/greetd.c servicos/papelini.c libgravity.a $(LDLIBS)

clean:
	rm -f $(OBJS) libgravity.a mixer galeria wifi painel apps papel barra rede transferencia dock lancador avisos bloqueio entrada ocioso modos controle ext-idle-notify-v1-client-protocol.h ext-idle-notify-v1-protocol.c $(PROTOS)

.PHONY: all clean

# o controle do celular (servicos/json.c, mdns.c, ponteiro.c): um serviço da sessão, sem a biblioteca de interface
controle: exemplos/controle.c servicos/json.c servicos/json.h servicos/mdns.c servicos/mdns.h servicos/ponteiro.c servicos/ponteiro.h \
		servicos/modos.c servicos/modos.h servicos/mpris.c servicos/mpris.h wlr-virtual-pointer-unstable-v1-client-protocol.h \
		wlr-virtual-pointer-unstable-v1-protocol.c
	$(CC) $(CFLAGS) -Iservicos $(shell pkg-config --cflags libsoup-3.0 gio-2.0) -o $@ exemplos/controle.c servicos/json.c servicos/mdns.c \
		servicos/ponteiro.c servicos/modos.c servicos/mpris.c wlr-virtual-pointer-unstable-v1-protocol.c \
		$(shell pkg-config --libs libsoup-3.0 gio-2.0 wayland-client) -lm
