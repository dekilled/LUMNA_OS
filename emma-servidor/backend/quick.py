"""Direct commands: short, unambiguous requests ("abre o Spotify", "liga a lanterna", "timer de 5
minutos") run their tool right away, without waiting for the model to decide to call it. The model
then only confirms what happened. Anything longer or less clear goes to the model as usual."""
import re
import unicodedata
from typing import Optional

MAX_WORDS = 9  # longer messages are probably more than a plain command


def _norm(text: str) -> str:
    t = unicodedata.normalize("NFKD", text).encode("ascii", "ignore").decode().lower()
    return re.sub(r"\s+", " ", re.sub(r"[!?.,;:]+$", "", t.strip())).strip()


POLITE = r"(?:(?:por favor|pfv|pf|lumna|emma|ei|oi|ok|amor|querida|entao|agora|bora|vai|so)[, ]+)*"
OPEN_VERB = r"(?:abr[ae]|abrir|abre ai|abra ai|inicia|iniciar|executa|executar|roda|rodar|bota|liga|ligar)"
SETTINGS = {
    "wifi": "wifi", "wi-fi": "wifi", "wi fi": "wifi", "bluetooth": "bluetooth", "bateria": "bateria", "som": "som",
    "tela": "tela", "localizacao": "localizacao", "gps": "localizacao", "modo aviao": "modo aviao", "dados moveis": "dados",
    "armazenamento": "armazenamento", "aplicativos": "apps", "apps": "apps",
}
PC = r"(?:o )?(?:pc|computador|notebook)"
COLOR_WORDS = ["vermelho", "vermelha", "verde", "azul", "amarelo", "amarela", "laranja", "roxo", "roxa", "lilas", "violeta", "rosa", "pink",
               "magenta", "ciano", "turquesa", "azul claro", "azul escuro", "verde claro", "verde escuro", "dourado", "salmao", "coral",
               "quente", "neutra", "fria", "branca", "branco", "aconchegante"]
COLOR_WORDS += ["azuis", "lilases", "quentes", "frias", "neutras"] + [w + "s" for w in COLOR_WORDS if w[-1] in "aeo" and " " not in w]
UNITS = {"s": 1, "seg": 1, "segundo": 1, "segundos": 1, "m": 60, "min": 60, "minuto": 60, "minutos": 60, "h": 3600, "hora": 3600, "horas": 3600}


FOLLOW_OFF = r"(?:pode )?(?:desliga|desligar|apaga|apagar|desativa|desativar)(?: (?:ela|ele|isso|essa|de novo|agora|ja))*"
FOLLOW_ON = r"(?:pode )?(?:liga|ligar|acende|acender|ativa|ativar)(?: (?:ela|ele|isso|essa|de novo|outra vez|agora|ja))*"


def match(text: str, available: set[str], last_tools: list[dict] = ()) -> Optional[tuple[str, dict]]:
    """(tool name, arguments) when the message is a plain command for an available tool. last_tools
    (the previous answer's) gives meaning to follow-ups like a bare "desliga" after the flashlight."""
    t = _norm(text)
    if not t or len(t.split()) > MAX_WORDS or "http" in t:
        return None
    t = re.sub(rf"^{POLITE}", "", t)
    t = re.sub(r"^(?:voce )?(?:pode|consegue|poderia) ", "", t)
    # what comes after the command is talk too: "abre as configuracoes pra mim, emma"
    t = re.sub(r"(?:[, ]+(?:por favor|pfv|pf|pra mim|para mim|emma|lumna|ai|rapidinho|agora|ja))+$", "", t)

    last_light = next((x for x in last_tools if x.get("name") in ("light_power", "light_schedule")), None)
    if "light_power" in available and last_light:
        device = str((last_light.get("args") or {}).get("device") or "")
        if re.fullmatch(FOLLOW_OFF, t):
            return "light_power", {"on": False, "device": device}
        if re.fullmatch(FOLLOW_ON, t):
            return "light_power", {"on": True, "device": device}

    if "flashlight" in available and any(x.get("name") == "flashlight" for x in last_tools):
        if re.fullmatch(FOLLOW_OFF, t):
            return "flashlight", {"on": False}
        if re.fullmatch(FOLLOW_ON, t):
            return "flashlight", {"on": True}

    if "flashlight" in available:
        if re.fullmatch(r"(?:liga|ligar|acende|acender|ativa|ativar)(?: (?:a|o))? (?:lanterna|luz do celular|flash)", t):
            return "flashlight", {"on": True}
        if re.fullmatch(r"(?:desliga|desligar|apaga|apagar|desativa|desativar)(?: (?:a|o))? (?:lanterna|luz do celular|flash)", t):
            return "flashlight", {"on": False}

    if "set_timer" in available and (m := re.fullmatch(
            r"(?:(?:coloca|poe|bota|cria|inicia|liga|faz)(?: um)? )?(?:timer|temporizador|cronometro|contagem)(?: de| por| pra)? (\d{1,3}) ?(s|seg|segundos?|m|min|minutos?|h|horas?)", t)):
        return "set_timer", {"seconds": int(m.group(1)) * UNITS[m.group(2)]}

    if "set_alarm" in available and (m := re.fullmatch(
            r"(?:(?:me )?(?:acorda|desperta)|(?:coloca|poe|bota|cria|marca|liga)(?: um)? (?:despertador|alarme)|(?:despertador|alarme))"
            r"(?: (?:amanha|hoje))?(?: (?:as|a|para as|pras|pra))? (\d{1,2})(?:[:h ](\d{2}))?(?: ?h| horas?)?"
            r"(?: (?:da|de) (manha|tarde|noite))?(?: (?:amanha|hoje))?", t)):
        hour, minute = int(m.group(1)), int(m.group(2) or 0)
        if m.group(3) in ("tarde", "noite") and hour < 12:
            hour += 12
        if hour < 24 and minute < 60:
            return "set_alarm", {"hour": hour, "minute": minute}

    # Yeelight lamps: "acende a luz", "apaga a luz do quarto", "apaga as luzes daqui a 30 minutos".
    if "light_power" in available and (m := re.fullmatch(
            r"(acende|acender|liga|ligar|apaga|apagar|desliga|desligar) (?:a |as |o |os )?(luz|luzes|lampada|lampadas|luminaria)"
            r"(?: (?:do|da|de|dos|das) ([a-z ]+?))?(?: (?:em|daqui a|daqui|dentro de) (\d{1,4}) ?(m|min|minutos?|h|horas?))?", t)):
        on = m.group(1).startswith(("acend", "lig"))
        device = (m.group(3) or "").strip() or ("todas" if m.group(2).endswith("s") else "")
        if m.group(4):
            minutes = int(m.group(4)) * (60 if m.group(5).startswith("h") else 1)
            if "light_schedule" in available and 1 <= minutes <= 10080:
                return "light_schedule", {"on": on, "minutes": minutes, "device": device}
        else:
            return "light_power", {"on": on, "device": device}

    # Lamp brightness / color: "luz do quarto em 30%", "deixa a luz azul", "luz quente".
    if "light_set" in available:
        lamp = r"(?:(?:deixa|coloca|poe|muda|bota|ajusta) )?(?:a |as )?(luz|luzes|lampada|lampadas)(?: (?:do|da|de|dos|das) ([a-z ]+?))?"
        which = lambda m: (m.group(2) or "").strip() or ("todas" if m.group(1).endswith("s") else "")  # noqa: E731
        if m := re.fullmatch(lamp + r"(?: (?:em|no|pra|para|a|com))? (\d{1,3}) ?(?:%|por ?cento)?(?: de brilho)?", t):
            if 1 <= int(m.group(3)) <= 100:
                return "light_set", {"brightness": int(m.group(3)), "device": which(m)}
        colors = "|".join(sorted(COLOR_WORDS, key=len, reverse=True))
        if m := re.fullmatch(lamp + rf"(?: (?:em|no|pra|para|de))? ((?:branco |branca )?(?:{colors}))", t):
            return "light_set", {"color": m.group(3), "device": which(m)}

    if "pc_power" in available:
        if re.fullmatch(r"cancela(?:r)? (?:o )?(?:desligamento|reinicio|desligar)(?: do pc| do computador)?", t):
            return "pc_power", {"action": "cancel"}
        if m := re.fullmatch(rf"(desliga|desligar|reinicia|reiniciar) {PC}(?: (?:em|daqui a|daqui|dentro de) (\d{{1,4}}) ?(m|min|minutos?|h|horas?))?", t):
            minutes = int(m.group(2)) * (60 if m.group(3) and m.group(3).startswith("h") else 1) if m.group(2) else 0
            if minutes <= 1440:
                return "pc_power", {"action": "shutdown" if m.group(1).startswith("deslig") else "restart", "minutes": minutes}
    if "lumna_power_lock" in available and re.fullmatch(rf"(?:bloqueia|bloquear|trava|travar) (?:a tela(?: do pc| do computador)?|{PC})", t):
        return "lumna_power_lock", {}
    if "pc_lock" in available and re.fullmatch(rf"(?:bloqueia|bloquear|trava|travar) (?:a tela(?: do pc| do computador)?|{PC})", t):
        return "pc_lock", {}
    if "lumna_audio_volume" in available and "set_volume" not in available:
        if re.fullmatch(rf"(?:muta|mutar|silencia|deixa (?:o )?(?:pc|computador|som) (?:no|em) mudo)(?: {PC})?", t):
            return "lumna_audio_volume", {"mute": True}
        if (m := re.fullmatch(r"(?:(?:coloca|poe|bota|deixa|muda|ajusta)(?: o)? )?volume(?: do (?:pc|computador))?(?: (?:em|pra|para|no|a))? (\d{1,3}) ?(?:%|por ?cento)?", t)) and int(m.group(1)) <= 100:
            return "lumna_audio_volume", {"percent": int(m.group(1))}
    if "pc_volume" in available and "set_volume" not in available:
        if re.fullmatch(rf"(?:muta|mutar|silencia|deixa (?:o )?(?:pc|computador|som) (?:no|em) mudo)(?: {PC})?", t):
            return "pc_volume", {"mute": True}
        if (m := re.fullmatch(r"(?:(?:coloca|poe|bota|deixa|muda|ajusta)(?: o)? )?volume(?: do (?:pc|computador))?(?: (?:em|pra|para|no|a))? (\d{1,3}) ?(?:%|por ?cento)?", t)) and int(m.group(1)) <= 100:
            return "pc_volume", {"percent": int(m.group(1))}

    if "set_volume" in available and (m := re.fullmatch(
            r"(?:(?:coloca|poe|bota|deixa|muda|ajusta)(?: o)? )?volume(?: (?:em|pra|para|no|a))? (\d{1,3}) ?(?:%|por ?cento)?", t)):
        if int(m.group(1)) <= 100:
            return "set_volume", {"percent": int(m.group(1))}

    if "set_brightness" in available and (m := re.fullmatch(
            r"(?:(?:coloca|poe|bota|deixa|muda|ajusta)(?: o)? )?brilho(?: (?:em|pra|para|no|a))? (\d{1,3}) ?(?:%|por ?cento)?", t)):
        if int(m.group(1)) <= 100:
            return "set_brightness", {"percent": int(m.group(1))}

    if "open_settings" in available and (m := re.fullmatch(
            rf"{OPEN_VERB}(?: as| a| o)? (?:configuracoes|configuracao|config|ajustes)(?: (?:do|da|de))? ?(.*)", t)):
        screen = SETTINGS.get(m.group(1).strip(), "")
        return "open_settings", {"screen": screen}

    # LUMNA OS: its Settings screen ("abre os ajustes do wi-fi" → the Wi-Fi page).
    if "lumna_panel_open" in available and (m := re.fullmatch(
            rf"{OPEN_VERB}(?: as| a| o| os)? (?:configuracoes|configuracao|config|ajustes)(?: (?:do|da|de|dos|das))? ?(.*)", t)):
        return "lumna_panel_open", {"panel": "configuracoes", "page": m.group(1).strip()}

    # On the phone an app; on the computer a program.
    opener = next((o for o in ("open_app", "lumna_apps_open", "pc_open") if o in available), None)
    if opener and (m := re.fullmatch(
            rf"{OPEN_VERB}(?: (?:o|a|os|as))?(?: (?:app|aplicativo|programa))?(?: (?:do|da|de))? (.+)", t)):
        name = m.group(1).strip()
        # "liga a lanterna"/"abre o link…" were handled above or aren't apps; long names are sentences.
        if name and len(name.split()) <= 3 and not re.search(r"\b(lanterna|flash|volume|brilho|link|site|pagina|e|pra|para|que|novo|ela|ele|isso|essa|pc|computador|notebook|tela)\b", name):
            return opener, {"name": name}
    return None
