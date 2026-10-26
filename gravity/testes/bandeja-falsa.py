#!/usr/bin/python3.12
"""Dois apps de mentira na bandeja (StatusNotifierItem + dbusmenu), no D-Bus da sessao.
Imprime o que a barra manda (Activate, Event). Para o teste da barra: testes/tudo-falso.sh ..."""
import sys, time
import dbus, dbus.service, dbus.types
import dbus.mainloop.glib
from gi.repository import GLib

dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
bus = dbus.SessionBus()

def pixmap(w, h, argb):
    dados = bytes(argb) * (w * h)
    return dbus.Array([dbus.Struct((dbus.Int32(w), dbus.Int32(h), dbus.Array([dbus.Byte(b) for b in dados], signature="y")), signature="iiay")], signature="(iiay)")

class Menu(dbus.service.Object):
    def __init__(self, nome, caminho):
        dbus.service.Object.__init__(self, nome, caminho)
        self.silenciado = False
    def itens(self):
        return [
            (1, {"label": "_Abrir o app"}),
            (2, {"type": "separator"}),
            (3, {"label": "Silenciar", "toggle-type": "checkmark", "toggle-state": dbus.Int32(1 if self.silenciado else 0)}),
            (4, {"label": "Preferencias", "children-display": "submenu"}),
            (5, {"label": "Iniciar com o sistema", "enabled": False}),
            (6, {"label": "Sair"}),
        ]
    @dbus.service.method("com.canonical.dbusmenu", in_signature="i", out_signature="b")
    def AboutToShow(self, id):
        return False
    @dbus.service.method("com.canonical.dbusmenu", in_signature="iias", out_signature="u(ia{sv}av)")
    def GetLayout(self, parent, depth, props):
        filhos = []
        for (i, p) in self.itens():
            sub = []
            if i == 4:
                sub = [dbus.Struct((dbus.Int32(41), dbus.Dictionary({"label": "Notificacoes"}, signature="sv"), dbus.Array([], signature="v")), signature="(ia{sv}av)")]
            filhos.append(dbus.Struct((dbus.Int32(i), dbus.Dictionary(p, signature="sv"), dbus.Array(sub, signature="v")), signature="(ia{sv}av)"))
        return dbus.UInt32(1), dbus.Struct((dbus.Int32(0), dbus.Dictionary({"children-display": "submenu"}, signature="sv"), dbus.Array(filhos, signature="v")), signature="(ia{sv}av)")
    @dbus.service.method("com.canonical.dbusmenu", in_signature="isvu", out_signature="")
    def Event(self, id, evento, dado, ts):
        print("MENU EVENT", id, evento, flush=True)
        if id == 3:
            self.silenciado = not self.silenciado

class Item(dbus.service.Object):
    def __init__(self, nome, id, titulo, icone, pix=None):
        self.nome = dbus.service.BusName(nome, dbus.SessionBus(private=True))
        dbus.service.Object.__init__(self, self.nome, "/StatusNotifierItem")
        self.menu = Menu(self.nome, "/MenuBar")
        self.p = {"Id": id, "Title": titulo, "Status": "Active", "IconName": icone, "Menu": dbus.ObjectPath("/MenuBar"),
                  "IconThemePath": "", "ItemIsMenu": False, "Category": "ApplicationStatus"}
        if pix:
            self.p["IconPixmap"] = pix
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="s", out_signature="a{sv}")
    def GetAll(self, iface):
        return self.p
    @dbus.service.method("org.freedesktop.DBus.Properties", in_signature="ss", out_signature="v")
    def Get(self, iface, nome):
        return self.p[nome]
    @dbus.service.method("org.kde.StatusNotifierItem", in_signature="ii", out_signature="")
    def Activate(self, x, y):
        print("ACTIVATE", self.p["Id"], x, y, flush=True)
    @dbus.service.method("org.kde.StatusNotifierItem", in_signature="ii", out_signature="")
    def SecondaryActivate(self, x, y):
        print("SECONDARY", self.p["Id"], flush=True)
    @dbus.service.method("org.kde.StatusNotifierItem", in_signature="ii", out_signature="")
    def ContextMenu(self, x, y):
        print("CONTEXTMENU", self.p["Id"], flush=True)

def registrar():
    try:
        w = bus.get_object("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher")
        i = dbus.Interface(w, "org.kde.StatusNotifierWatcher")
        # assincronas: um app de verdade atende o D-Bus enquanto espera (a barra le o item logo depois)
        for n in ("org.kde.StatusNotifierItem-1001-1", "org.kde.StatusNotifierItem-1002-1"):
            i.RegisterStatusNotifierItem(n, reply_handler=lambda: None, error_handler=lambda e: print("erro", e, flush=True))
        print("registrados", flush=True)
        return False
    except dbus.DBusException:
        return True

a = Item("org.kde.StatusNotifierItem-1001-1", "discord", "Discord", "discord")
b = Item("org.kde.StatusNotifierItem-1002-1", "downloads", "Downloads", "", pixmap(24, 24, [255, 230, 60, 90]))
GLib.timeout_add(500, registrar)
GLib.timeout_add_seconds(int(sys.argv[1]) if len(sys.argv) > 1 else 60, lambda: sys.exit(0))
GLib.MainLoop().run()
