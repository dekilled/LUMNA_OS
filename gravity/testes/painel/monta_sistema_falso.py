# Uma árvore falsa de /proc, /sys, /etc e /var/log, e um checkupdates falso, para testar servicos/sistema.c.
import os, sys
r = sys.argv[1]
for d in ["proc", "sys/class/dmi/id", "sys/class/drm/card0/device", "etc", "var/log", "bin"]: os.makedirs(r + "/" + d, exist_ok=True)
open(r + "/proc/cpuinfo", "w").write("processor\t: 0\nmodel name\t: Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz\n\nprocessor\t: 1\nmodel name\t: Intel(R) Core(TM) i7-9750H CPU @ 2.60GHz\n")
open(r + "/proc/meminfo", "w").write("MemTotal:       16384000 kB\nMemFree:  1000 kB\nMemAvailable:    8000000 kB\n")
open(r + "/proc/uptime", "w").write("3600.55 7000.00\n")
open(r + "/sys/class/dmi/id/sys_vendor", "w").write("LENOVO\n"); open(r + "/sys/class/dmi/id/product_name", "w").write("ThinkPad T14\n")
open(r + "/sys/class/drm/card0/device/vendor", "w").write("0x1002\n"); open(r + "/sys/class/drm/card0/device/uevent", "w").write("DRIVER=amdgpu\nPCI_ID=1002:1638\n")
open(r + "/etc/os-release", "w").write('NAME="Arch Linux"\nPRETTY_NAME="Arch Linux"\n'); open(r + "/etc/lumna-release", "w").write("VERSION=1.2.3\n")
open(r + "/var/log/pacman.log", "w").write("""[2026-10-20T09:00:00-0300] [PACMAN] Running 'pacman -Syu'
[2026-10-20T09:00:05-0300] [ALPM] installed foo (2.0-1)
[2026-10-25T10:11:12-0300] [ALPM] upgraded bar (1.0-1 -> 1.1-1)
[2026-10-26T08:00:00-0300] [ALPM] removed velho (0.9-1)
[2026-10-26T08:00:01-0300] [ALPM-SCRIPTLET] ignorado
""")
open(r + "/bin/checkupdates", "w").write("#!/bin/sh\nprintf 'firefox 130.0-1 -> 131.0-1\\nlinux 6.10.1-1 -> 6.10.2-1\\n'\n")
os.chmod(r + "/bin/checkupdates", 0o755)
