"""PC actions (Modo Full, G7): the Lumna acts on the computer the backend runs on.

A closed list, never a free shell: power (shut down / restart, now or in N minutes, cancel), lock the
screen, volume/mute, open an installed program or a link, show a notification. Each runs fixed
commands with checked arguments (no shell). All ask for confirmation by default (tools.permission).

Only offered when the backend runs directly on a computer: not on the phone (the Android app sets
HARNESS_BOOT_ID) and not inside a container (/.dockerenv), where it would act on the server, not on
the user's PC. LUMNA_PC_ACTIONS=1/0 forces it on/off. LUMNA_PC_DRYRUN=<file> records the commands
instead of running them (tests).
"""
import asyncio
import difflib
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import unicodedata
from pathlib import Path
from typing import Optional

from tools import REGISTRY, Outcome, Tool

SYSTEM = platform.system()  # Linux | Windows | Darwin
OS_NAME = {"Linux": "Linux", "Windows": "Windows", "Darwin": "macOS"}.get(SYSTEM, SYSTEM)
MAX_MINUTES = 24 * 60


def available() -> bool:
    forced = os.environ.get("LUMNA_PC_ACTIONS", "").strip()
    if forced in ("0", "1"):
        return forced == "1"
    if os.environ.get("HARNESS_BOOT_ID"):  # the Android app (backend inside the phone)
        return False
    if Path("/.dockerenv").exists():  # a container: the "PC" would be the server
        return False
    return SYSTEM in ("Linux", "Windows", "Darwin")


# ---------- running commands ----------
class Failed(Exception):
    pass


def _record(cmd: list[str], env: Optional[dict] = None) -> bool:
    path = os.environ.get("LUMNA_PC_DRYRUN")
    if not path:
        return False
    with open(path, "a", encoding="utf-8") as f:
        f.write(json.dumps({"cmd": cmd, "env": env or {}}, ensure_ascii=False) + "\n")
    return True


async def _run(cmd: list[str], env: Optional[dict] = None, timeout: float = 20) -> str:
    """Runs a fixed command (no shell) and waits for it; Failed with its message if it fails."""
    if _record(cmd, env):
        return ""
    if not shutil.which(cmd[0]):
        raise Failed(f"o comando {cmd[0]} não existe neste computador")
    proc = await asyncio.create_subprocess_exec(*cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                                env={**os.environ, **(env or {})})
    try:
        out, err = await asyncio.wait_for(proc.communicate(), timeout)
    except asyncio.TimeoutError:
        proc.kill()
        raise Failed(f"{cmd[0]} não respondeu")
    if proc.returncode != 0:
        raise Failed((err or out).decode(errors="replace").strip()[:200] or f"{cmd[0]} falhou (código {proc.returncode})")
    return out.decode(errors="replace")


async def _first(options: list[list[str]], env: Optional[dict] = None) -> str:
    """The first command of the list that exists and works (desktops differ)."""
    errors = []
    for cmd in options:
        if not os.environ.get("LUMNA_PC_DRYRUN") and not shutil.which(cmd[0]):
            continue
        try:
            return await _run(cmd, env)
        except Failed as e:
            errors.append(str(e))
    raise Failed(errors[-1] if errors else "nenhum comando para isso neste computador")


def _launch(cmd: list[str], env: Optional[dict] = None) -> None:
    """Starts a program detached (it keeps running on its own)."""
    if _record(cmd, env):
        return
    kw = {"stdin": subprocess.DEVNULL, "stdout": subprocess.DEVNULL, "stderr": subprocess.DEVNULL, "env": {**os.environ, **(env or {})}}
    if SYSTEM == "Windows":
        kw["creationflags"] = 0x00000008 | 0x00000200  # DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP
    else:
        kw["start_new_session"] = True
    subprocess.Popen(cmd, **kw)


def _powershell(script: str) -> list[str]:
    return ["powershell", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", script]


# ---------- power ----------
_mac_timer: Optional[asyncio.Task] = None  # macOS: a delayed shutdown would need root, so the backend waits


async def pc_power(args: dict) -> Outcome:
    global _mac_timer
    action = str(args.get("action") or "shutdown").lower()
    try:
        minutes = int(args.get("minutes") or 0)
    except (TypeError, ValueError):
        minutes = -1
    if action not in ("shutdown", "restart", "cancel"):
        return Outcome("Erro: action deve ser shutdown, restart ou cancel.", "Ação inválida", status="error")
    if not 0 <= minutes <= MAX_MINUTES:
        return Outcome("Erro: minutes deve ser de 0 a 1440.", "Tempo inválido", status="error")
    verb = {"shutdown": "desligar", "restart": "reiniciar"}.get(action, "")
    when = "agora" if minutes == 0 else f"em {minutes} min"
    try:
        if SYSTEM == "Linux":
            if action == "cancel":
                await _run(["shutdown", "-c"])
            else:
                await _run(["shutdown", "-h" if action == "shutdown" else "-r", "now" if minutes == 0 else f"+{minutes}"])
        elif SYSTEM == "Windows":
            if action == "cancel":
                await _run(["shutdown", "/a"])
            else:
                await _run(["shutdown", "/s" if action == "shutdown" else "/r", "/t", str(minutes * 60)])
        else:  # macOS
            if _mac_timer:
                _mac_timer.cancel()
                _mac_timer = None
            if action != "cancel":
                apple = "shut down" if action == "shutdown" else "restart"

                async def later():
                    await asyncio.sleep(minutes * 60)
                    await _run(["osascript", "-e", f'tell application "System Events" to {apple}'])
                if minutes == 0:
                    await later()
                else:
                    _mac_timer = asyncio.create_task(later())
    except Failed as e:
        return Outcome(f"Erro: não consegui ({e}).", f"Não consegui {verb or 'cancelar'}", status="error")
    if action == "cancel":
        return Outcome("Desligamento/reinício agendado cancelado.", "Cancelou o desligamento")
    return Outcome(f"O computador vai {verb} {when}. Para desistir, use action=cancel.", f"Vai {verb} o PC {when}")


# ---------- lock ----------
async def pc_lock(args: dict) -> Outcome:
    try:
        if SYSTEM == "Linux":
            await _first([["loginctl", "lock-session"], ["xdg-screensaver", "lock"], ["gnome-screensaver-command", "-l"], ["dm-tool", "lock"]])
        elif SYSTEM == "Windows":
            await _run(["rundll32.exe", "user32.dll,LockWorkStation"])
        else:
            await _run(["pmset", "displaysleepnow"])
    except Failed as e:
        return Outcome(f"Erro: não consegui bloquear a tela ({e}).", "Não consegui bloquear", status="error")
    return Outcome("Tela bloqueada.", "Bloqueou a tela")


# ---------- volume ----------
_WIN_AUDIO = r"""
Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
[Guid("5CDF2C82-841E-4546-9722-0CF74078229A"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IAudioEndpointVolume {
  int f(); int g(); int h(); int i();
  int SetMasterVolumeLevelScalar(float fLevel, System.Guid pguidEventContext);
  int j();
  int GetMasterVolumeLevelScalar(out float pfLevel);
  int k(); int l(); int m(); int n();
  int SetMute([MarshalAs(UnmanagedType.Bool)] bool bMute, System.Guid pguidEventContext);
  int GetMute(out bool pbMute);
}
[Guid("D666063F-1587-4E43-81F1-B948E807363F"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDevice { int Activate(ref System.Guid id, int clsCtx, int activationParams, out IAudioEndpointVolume aev); }
[Guid("A95664D2-9614-4F35-A746-DE8DB63617E6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
interface IMMDeviceEnumerator { int f(); int GetDefaultAudioEndpoint(int dataFlow, int role, out IMMDevice endpoint); }
[ComImport, Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")] class MMDeviceEnumeratorComObject { }
public class LumnaAudio {
  static IAudioEndpointVolume Vol() {
    var e = new MMDeviceEnumeratorComObject() as IMMDeviceEnumerator;
    IMMDevice dev = null; Marshal.ThrowExceptionForHR(e.GetDefaultAudioEndpoint(0, 1, out dev));
    IAudioEndpointVolume v = null; var id = typeof(IAudioEndpointVolume).GUID;
    Marshal.ThrowExceptionForHR(dev.Activate(ref id, 23, 0, out v)); return v;
  }
  public static void Set(float level) { Marshal.ThrowExceptionForHR(Vol().SetMasterVolumeLevelScalar(level, System.Guid.Empty)); }
  public static void Mute(bool on) { Marshal.ThrowExceptionForHR(Vol().SetMute(on, System.Guid.Empty)); }
}
'@
"""


async def pc_volume(args: dict) -> Outcome:
    percent = args.get("percent")
    mute = args.get("mute")
    if percent is None and mute is None:
        return Outcome("Erro: informe percent (0-100) e/ou mute (true/false).", "Faltou o volume", status="error")
    if percent is not None:
        try:
            percent = int(percent)
        except (TypeError, ValueError):
            percent = -1
        if not 0 <= percent <= 100:
            return Outcome("Erro: percent deve ser de 0 a 100.", "Volume inválido", status="error")
    try:
        if SYSTEM == "Linux":
            if percent is not None:
                await _first([["pactl", "set-sink-volume", "@DEFAULT_SINK@", f"{percent}%"], ["wpctl", "set-volume", "@DEFAULT_AUDIO_SINK@", f"{percent}%"],
                              ["amixer", "-q", "sset", "Master", f"{percent}%"]])
            if mute is not None:
                await _first([["pactl", "set-sink-mute", "@DEFAULT_SINK@", "1" if mute else "0"], ["wpctl", "set-mute", "@DEFAULT_AUDIO_SINK@", "1" if mute else "0"],
                              ["amixer", "-q", "sset", "Master", "mute" if mute else "unmute"]])
        elif SYSTEM == "Windows":
            script = _WIN_AUDIO
            if percent is not None:
                script += f"[LumnaAudio]::Set({percent / 100:.2f})\n"
            if mute is not None:
                script += f"[LumnaAudio]::Mute(${'true' if mute else 'false'})\n"
            await _run(_powershell(script))
        else:
            if percent is not None:
                await _run(["osascript", "-e", f"set volume output volume {percent}"])
            if mute is not None:
                await _run(["osascript", "-e", f"set volume output muted {'true' if mute else 'false'}"])
    except Failed as e:
        return Outcome(f"Erro: não consegui mudar o volume ({e}).", "Não consegui mudar o volume", status="error")
    parts = ([f"volume em {percent}%"] if percent is not None else []) + (["mudo"] if mute else ["som ligado"] if mute is False else [])
    text = ", ".join(parts)
    return Outcome(f"PC: {text}.", f"PC: {text}")


# ---------- open a program or a link ----------
def _fold(s: str) -> str:
    return re.sub(r"[^a-z0-9 ]", "", unicodedata.normalize("NFKD", s).encode("ascii", "ignore").decode().lower()).strip()


def _linux_apps() -> dict[str, list[str]]:
    """Installed programs (desktop entries): name → command to start it."""
    home = os.environ.get("XDG_DATA_HOME") or str(Path.home() / ".local/share")
    dirs = [home] + (os.environ.get("XDG_DATA_DIRS") or "/usr/local/share:/usr/share").split(":") + ["/var/lib/flatpak/exports/share", str(Path.home() / ".local/share/flatpak/exports/share")]
    apps: dict[str, list[str]] = {}
    for d in dirs:
        for f in sorted(Path(d, "applications").glob("*.desktop")) if Path(d, "applications").is_dir() else []:
            entry: dict[str, str] = {}
            section = ""
            try:
                for line in f.read_text(errors="replace").splitlines():
                    if line.startswith("["):
                        section = line.strip()
                    elif section == "[Desktop Entry]" and "=" in line:
                        k, v = line.split("=", 1)
                        entry.setdefault(k.strip(), v.strip())
            except OSError:
                continue
            if entry.get("Type") != "Application" or entry.get("NoDisplay") == "true" or entry.get("Hidden") == "true" or not entry.get("Name"):
                continue
            if shutil.which("gtk-launch"):
                cmd = ["gtk-launch", f.stem]
            else:
                exec_ = re.sub(r"%[fFuUdDnNickvm]", "", entry.get("Exec", "")).strip()
                try:
                    cmd = shlex.split(exec_)
                except ValueError:
                    continue
            if cmd:
                apps.setdefault(entry["Name"], cmd)
    return apps


def _windows_apps() -> dict[str, list[str]]:
    roots = [Path(os.environ.get("ProgramData", r"C:\ProgramData")), Path(os.environ.get("APPDATA", ""))]
    apps: dict[str, list[str]] = {}
    for r in roots:
        start = r / "Microsoft/Windows/Start Menu/Programs"
        for f in start.rglob("*.lnk") if start.is_dir() else []:
            apps.setdefault(f.stem, ["explorer.exe", str(f)])
    return apps


def _mac_apps() -> dict[str, list[str]]:
    apps: dict[str, list[str]] = {}
    for d in ["/Applications", "/System/Applications", "/System/Applications/Utilities", str(Path.home() / "Applications")]:
        for f in Path(d).glob("*.app") if Path(d).is_dir() else []:
            apps.setdefault(f.stem, ["open", "-a", str(f)])
    return apps


def installed_apps() -> dict[str, list[str]]:
    return {"Linux": _linux_apps, "Windows": _windows_apps, "Darwin": _mac_apps}.get(SYSTEM, dict)()


# What people call programs → how they are installed (the first one found wins).
ALIASES = {
    "vscode": ["visual studio code", "code"], "vs code": ["visual studio code", "code"],
    "chrome": ["google chrome", "chromium"], "edge": ["microsoft edge"], "zap": ["whatsapp"],
    "word": ["microsoft word", "word", "libreoffice writer"], "excel": ["microsoft excel", "excel", "libreoffice calc"],
    "powerpoint": ["microsoft powerpoint", "powerpoint", "libreoffice impress"],
    "explorador": ["file explorer", "explorador de arquivos", "files", "arquivos", "nautilus", "dolphin", "finder"],
    "arquivos": ["files", "arquivos", "file explorer", "explorador de arquivos", "nautilus", "dolphin", "finder"],
    "terminal": ["terminal", "windows terminal", "gnome terminal", "konsole", "prompt de comando"],
    "calculadora": ["calculadora", "calculator", "gnome calculator", "kcalc"],
    "configuracoes": ["configuracoes", "settings", "system settings", "configuracoes do sistema", "ajustes do sistema"],
}


def match_app(name: str, apps: dict[str, list[str]]) -> tuple[Optional[str], list[str]]:
    """(the program, other candidates): exact (or a known nickname), then by word, then by similarity
    ("spotfy" → Spotify)."""
    want = _fold(name)
    folded = {_fold(k): k for k in apps}
    if want in folded:
        return folded[want], []
    for alias in ALIASES.get(want, []):
        if alias in folded:
            return folded[alias], []
    squeezed = {f.replace(" ", ""): k for f, k in folded.items()}  # "vs code"/"vscode", "libre office"
    if want.replace(" ", "") in squeezed:
        return squeezed[want.replace(" ", "")], []
    starts = [k for f, k in folded.items() if f.startswith(want) or f.split(" ")[0] == want]
    if len(starts) == 1:
        return starts[0], []
    contains = starts or [k for f, k in folded.items() if want in f]
    if len(contains) == 1:
        return contains[0], []
    if contains:
        return None, sorted(contains, key=len)[:5]
    close = difflib.get_close_matches(want, list(folded), n=3, cutoff=0.75)
    if len(close) == 1:
        return folded[close[0]], []
    return None, [folded[c] for c in close]


async def pc_open(args: dict) -> Outcome:
    url = str(args.get("url") or "").strip()
    name = str(args.get("name") or "").strip()
    if url:
        if not re.match(r"^https?://[^\s]+$", url, re.I):
            return Outcome("Erro: só abro links http(s).", "Link não permitido", status="error")
        cmd = {"Linux": ["xdg-open", url], "Windows": ["explorer.exe", url], "Darwin": ["open", url]}[SYSTEM]
        try:
            _launch(cmd)
        except OSError as e:
            return Outcome(f"Erro: não consegui abrir o link ({e}).", "Não consegui abrir o link", status="error")
        host = re.sub(r"^https?://(www\.)?", "", url, flags=re.I).split("/")[0]
        return Outcome(f"Abri {url} no navegador do computador.", f"Abriu {host} no PC")
    if not name:
        return Outcome("Erro: informe name (programa) ou url.", "Faltou o programa", status="error")
    apps = await asyncio.to_thread(installed_apps)
    found, options = match_app(name, apps)
    if not found:
        if options:
            return Outcome(f"Mais de um programa combina com \"{name}\": {', '.join(options)}. Pergunte qual.", f"Mais de um programa combina com \"{name}\"", status="error")
        return Outcome(f"Nenhum programa instalado combina com \"{name}\".", f"Nenhum programa chamado \"{name}\"", status="error")
    try:
        _launch(apps[found])
    except OSError as e:
        return Outcome(f"Erro: não consegui abrir {found} ({e}).", f"Não consegui abrir {found}", status="error")
    return Outcome(f"Abri {found} no computador.", f"Abriu {found} no PC")


# ---------- notification ----------
async def pc_notify(args: dict) -> Outcome:
    text = str(args.get("text") or "").strip()[:300]
    if not text:
        return Outcome("Erro: falta o texto.", "Notificação sem texto", status="error")
    try:
        if SYSTEM == "Linux":
            await _run(["notify-send", "-a", "LUMNA", "Lumna", text])
        elif SYSTEM == "Windows":
            # The text goes in an environment variable, never inside the script.
            script = ("Add-Type -AssemblyName System.Windows.Forms; $n = New-Object System.Windows.Forms.NotifyIcon; "
                      "$n.Icon = [System.Drawing.SystemIcons]::Information; $n.BalloonTipTitle = 'Lumna'; $n.BalloonTipText = $env:LUMNA_TEXT; "
                      "$n.Visible = $true; $n.ShowBalloonTip(8000); Start-Sleep -Seconds 9; $n.Dispose()")
            _launch(_powershell(script), {"LUMNA_TEXT": text})  # the balloon stays a few seconds: not waited for
        else:
            await _run(["osascript", "-e", "on run argv", "-e", 'display notification (item 1 of argv) with title "Lumna"', "-e", "end run", text])
    except Failed as e:
        return Outcome(f"Erro: não consegui notificar ({e}).", "Não consegui notificar", status="error")
    return Outcome("Notificação mostrada no computador.", f"Notificou no PC: {text[:60]}")


TOOLS = [
    Tool("pc_power", "PC: desligar / reiniciar",
         f"Desliga ou reinicia o computador ({OS_NAME}) onde o LUMNA roda, agora ou em N minutos, ou cancela um desligamento agendado.",
         {"type": "object", "properties": {
             "action": {"type": "string", "enum": ["shutdown", "restart", "cancel"], "description": "shutdown, restart ou cancel"},
             "minutes": {"type": "integer", "description": "Daqui a quantos minutos (0 = agora; até 1440)"},
         }, "required": ["action"]},
         pc_power, risk="act", default="ask", where="pc"),
    Tool("pc_lock", "PC: bloquear a tela", f"Bloqueia a tela do computador ({OS_NAME}).",
         {"type": "object", "properties": {}, "required": []}, pc_lock, risk="act", default="ask", where="pc"),
    Tool("pc_volume", "PC: volume", f"Ajusta o volume do computador ({OS_NAME}) e/ou deixa no mudo.",
         {"type": "object", "properties": {
             "percent": {"type": "integer", "description": "0 a 100"},
             "mute": {"type": "boolean", "description": "true = mudo, false = tira do mudo"},
         }, "required": []},
         pc_volume, risk="act", default="ask", where="pc"),
    Tool("pc_open", "PC: abrir programas e links",
         f"Abre um programa instalado no computador ({OS_NAME}) pelo nome (aceita nome aproximado) ou um link http(s) no navegador dele.",
         {"type": "object", "properties": {
             "name": {"type": "string", "description": "Nome do programa, ex.: Spotify, VS Code, Firefox"},
             "url": {"type": "string", "description": "Ou um link https://..."},
         }, "required": []},
         pc_open, risk="act", default="ask", where="pc"),
    Tool("pc_notify", "PC: notificação", f"Mostra uma notificação na tela do computador ({OS_NAME}).",
         {"type": "object", "properties": {"text": {"type": "string"}}, "required": ["text"]},
         pc_notify, risk="act", default="ask", where="pc"),
]
PC_TOOLS = {t.name for t in TOOLS}

if available():
    for _t in TOOLS:
        REGISTRY[_t.name] = _t
