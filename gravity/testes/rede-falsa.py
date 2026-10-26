#!/usr/bin/python3.12
"""Um NetworkManager de mentira (python-dbusmock) para testar o servico da rede sem hardware.
Corre dentro de um dbus-daemon proprio:   testes/rede-falsa.sh COMANDO...
Cria: um cabo, um Wi-Fi onboard (PCI) com 4 redes e uma antena USB com 2."""
import os, sys, time, subprocess
import dbus, dbusmock
from dbusmock import DBusTestCase

DBusTestCase.start_system_bus()
os.environ.setdefault("DBUS_SYSTEM_BUS_ADDRESS", os.environ["DBUS_SYSTEM_BUS_ADDRESS"])
mock = DBusTestCase.spawn_server_template("networkmanager", {}, stdout=subprocess.DEVNULL)[0]
bus = DBusTestCase.get_dbus(True)
nm = dbus.Interface(bus.get_object("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager"), "org.freedesktop.DBus.Mock")
def props_set(path, iface, nome, valor):
    nm.SetProperty(path, iface, nome, valor)
cabo = nm.AddEthernetDevice("eth0", "enp3s0", 20)
wifi1 = nm.AddWiFiDevice("wlan0", "wlp2s0", 30)
wifi2 = nm.AddWiFiDevice("wlan1", "wlx00e04c", 30)
for ap, ssid, forca, sec in [("a1", "CasaDoEric", 78, 0x100), ("a2", "VizinhoFast", 45, 0x100), ("a3", "CafeLivre", 60, 0), ("a4", "Escritorio_5G", 91, 0x100)]:
    nm.AddAccessPoint(wifi1, ap, ssid, "aa:bb:cc:dd:ee:0" + ap[1], 2, 5200, 400000, dbus.Byte(forca), dbus.UInt32(sec))
for ap, ssid, forca, sec in [("b1", "CasaDoEric", 55, 0x100), ("b2", "RedeDoBar", 33, 0)]:
    nm.AddAccessPoint(wifi2, ap, ssid, "aa:bb:cc:dd:ff:0" + ap[1], 2, 2412, 130000, dbus.Byte(forca), dbus.UInt32(sec))
nm.AddWiFiConnection(wifi1, "CasaDoEric", "CasaDoEric", "wpa-psk")
if len(sys.argv) > 1:
    time.sleep(0.5)
    r = subprocess.run(sys.argv[1:])
    mock.terminate()
    sys.exit(r.returncode)
print("rodando; DBUS_SYSTEM_BUS_ADDRESS=" + os.environ["DBUS_SYSTEM_BUS_ADDRESS"], flush=True)
mock.wait()
