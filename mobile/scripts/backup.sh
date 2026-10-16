#!/bin/sh
# Backup de todas as partições do aparelho (menos userdata) para mobile/backup/<serial>/
# Uso: celular ligado no Android com root (Magisk), depuração USB ativa: ./backup.sh
set -eu
command -v adb >/dev/null || { echo "instale o adb (android-tools)"; exit 1; }
serial=$(adb get-serialno)
dest="$(cd "$(dirname "$0")/.." && pwd)/backup/$serial"
mkdir -p "$dest"
parts=$(adb shell su -c 'ls /dev/block/by-name' | tr -d '\r')
[ -n "$parts" ] || { echo "não consegui listar as partições (tem root?)"; exit 1; }
for p in $parts; do
    case $p in userdata) continue ;; esac
    echo "-> $p"
    adb exec-out su -c "dd if=/dev/block/by-name/$p bs=1M 2>/dev/null" > "$dest/$p.img"
done
(cd "$dest" && sha256sum *.img > SHA256SUMS)
echo "ok: backup em $dest. Guarde em mais de um lugar (modemst*, persist e fsg são únicos do aparelho!)"
