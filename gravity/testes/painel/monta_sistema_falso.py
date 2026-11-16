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
open(r + "/bin/checkupdates", "w").write("#!/bin/sh\nprintf 'firefox 130.0-1 -> 131.0-1\\nlinux 6.10.1-1 -> 6.10.2-1\\nlumna-orbit 1.0-1 -> 1.1-1\\n'\n")
os.chmod(r + "/bin/checkupdates", 0o755)
# o repositório [lumna] (pacman -Slq lumna): marca de quem é cada atualização
open(r + "/bin/pacman", "w").write("#!/bin/sh\ncase \"$*\" in \"-Slq lumna\") printf 'lumna-orbit\\nlumna-shell\\n' ;; esac\n")
os.chmod(r + "/bin/pacman", 0o755)
# a atualização agendada: o que o Painel gravou e o que o último agendado fez
os.makedirs(r + "/etc/lumna", exist_ok=True); os.makedirs(r + "/var/lib/lumna", exist_ok=True)
open(r + "/etc/lumna/atualizar.conf", "w").write("modo=ligado\nescopo=lumna\nhora=04:30\ndias=seg,qua,sex\n")
open(r + "/var/lib/lumna/atualizar.estado", "w").write("quando=1790000000\nescopo=lumna\nresultado=ok\npacotes=lumna-orbit lumna-shell\n")
