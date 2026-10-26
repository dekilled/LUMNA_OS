#!/usr/bin/python3.12
"""NetworkManager e BlueZ de mentira juntos no mesmo D-Bus do sistema (para a topbar e o Painel).
   testes/tudo-falso.sh COMANDO..."""
import os, sys, time, subprocess
import dbus, dbusmock
from dbusmock import DBusTestCase

DBusTestCase.start_system_bus()
nmp = DBusTestCase.spawn_server_template("networkmanager", {}, stdout=subprocess.DEVNULL)[0]
btp = DBusTestCase.spawn_server_template("bluez5", {}, stdout=subprocess.DEVNULL)[0]
bus = DBusTestCase.get_dbus(True)
nm = dbus.Interface(bus.get_object("org.freedesktop.NetworkManager", "/org/freedesktop/NetworkManager"), "org.freedesktop.DBus.Mock")
cabo = nm.AddEthernetDevice("eth0", "enp3s0", 20)
wifi1 = nm.AddWiFiDevice("wlan0", "wlp2s0", 30)
for ap, ssid, forca, sec in [("a1", "CasaDoEric", 78, 0x100), ("a2", "VizinhoFast", 45, 0x100), ("a3", "CafeLivre", 60, 0), ("a4", "Escritorio_5G", 91, 0x100)]:
    nm.AddAccessPoint(wifi1, ap, ssid, "aa:bb:cc:dd:ee:0" + ap[1], 2, 5200, 400000, dbus.Byte(forca), dbus.UInt32(sec))
nm.AddWiFiConnection(wifi1, "CasaDoEric", "CasaDoEric", "wpa-psk")
bt = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"), "org.bluez.Mock")
bt.AddAdapter("hci0", "Lumna-PC")
bt.AddDevice("hci0", "11:22:33:44:55:66", "Fone do Eric")
bt.AddDevice("hci0", "AA:BB:CC:DD:EE:FF", "Teclado K380")
bt.PairDevice("hci0", "11:22:33:44:55:66") if hasattr(bt, "PairDevice") else None
bt.ConnectDevice("hci0", "11:22:33:44:55:66") if hasattr(bt, "ConnectDevice") else None
if len(sys.argv) > 1:
    time.sleep(0.5)
    r = subprocess.run(sys.argv[1:])
    nmp.terminate(); btp.terminate()
    sys.exit(r.returncode)
nmp.wait()
