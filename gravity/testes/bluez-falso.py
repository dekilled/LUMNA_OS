#!/usr/bin/python3.12
"""Um BlueZ de mentira (python-dbusmock): um adaptador e tres aparelhos. testes/bluez-falso.sh COMANDO..."""
import os, sys, time, subprocess
import dbus, dbusmock
from dbusmock import DBusTestCase

DBusTestCase.start_system_bus()
mock = DBusTestCase.spawn_server_template("bluez5", {}, stdout=subprocess.DEVNULL)[0]
bus = DBusTestCase.get_dbus(True)
m = dbus.Interface(bus.get_object("org.bluez", "/org/bluez"), "org.bluez.Mock")
m.AddAdapter("hci0", "Lumna-PC")
m.AddDevice("hci0", "11:22:33:44:55:66", "Fone do Eric")
m.AddDevice("hci0", "AA:BB:CC:DD:EE:FF", "Teclado K380")
m.AddDevice("hci0", "01:02:03:04:05:06", "Galaxy S23")
if len(sys.argv) > 1:
    time.sleep(0.5)
    r = subprocess.run(sys.argv[1:])
    mock.terminate()
    sys.exit(r.returncode)
mock.wait()
