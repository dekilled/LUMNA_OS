<script setup lang="ts">
// LUMNA Keyboard: o teclado na tela do celular, como no esboço. Teclas: letras (ABNT, com ç),
// símbolos e acentos (?123, =\<). A barra de cima tem três camadas:
//   ferramentas  a padrão ao abrir: Ajustes, Emojis, Área de transferência (e o que vier)
//   sugestões    aparece sozinha quando você começa a digitar
//   atalhos      Esc, Tab, Ctrl, Alt, Home, End; aparece sozinha no terminal e nos editores
// O botão de grade alterna entre as ferramentas e a camada do momento.
// As teclas vão para o app em foco pelo backend (touchkeys.rs).
import { computed, onMounted, onUnmounted, ref } from "vue";
import type { UnlistenFn } from "@tauri-apps/api/event";
import Icon from "../Icon.vue";
import { clipboardRead, haptic, keyboardKey, keyboardShow, keyboardType, on } from "../../system";
import { activeWindow } from "../../compositor";

type Page = "letras" | "simbolos" | "simbolos2" | "clipboard" | "ajustes" | "emojis";
const page = ref<Page>("letras");

type Bar = "ferramentas" | "sugestoes" | "atalhos";
const bar = ref<Bar>("ferramentas");
// o app em foco é um terminal ou editor: a camada de atalhos é a dele, e , . viram / -
const terminal = ref(false);
const contextBar = (): Bar => (terminal.value ? "atalhos" : settings.value.suggestions ? "sugestoes" : "ferramentas");
function toggleBar() {
  bar.value = bar.value === "ferramentas" ? contextBar() : "ferramentas";
}

// classes de janela (minúsculas) que pedem os atalhos: terminais e editores
const TERMINALS = ["foot", "kitty", "alacritty", "wezterm", "xterm", "konsole", "terminal", "terminator",
  "code", "codium", "kate", "gedit", "mousepad", "neovide", "nvim", "zed", "emacs", "helix"];
const shift = ref<"off" | "once" | "lock">("off");
const ctrl = ref(false);
const alt = ref(false);
const layout = ref<"pt" | "en">("pt");

const letters = computed(() => [
  ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
  ["a", "s", "d", "f", "g", "h", "j", "k", "l", layout.value === "pt" ? "ç" : "'"],
  ["z", "x", "c", "v", "b", "n", "m"],
]);
const symbols = [
  ["@", "#", "$", "%", "&", "*", "-", "+", "(", ")"],
  ["!", '"', "'", ":", ";", "/", "?", "_", "=", "~"],
  ["á", "é", "í", "ó", "ú", "ã", "õ"],
];
// segunda página (o "=\\<" na página de símbolos): o que o terminal pede e o resto
const symbols2 = [
  ["|", "\\", "[", "]", "{", "}", "<", ">", "^", "`"],
  ["€", "£", "°", "§", "¬", "•", "¿", "¡", "…", "¨"],
  ["â", "ê", "ô", "à", "ü", "ñ", "ç"],
];
const rows = computed(() => (page.value === "simbolos" ? symbols : page.value === "simbolos2" ? symbols2 : letters.value));
const label = (k: string) => (shift.value !== "off" ? k.toUpperCase() : k);

// ---- digitar ----

// palavra sendo digitada (para as sugestões); some quando a posição do cursor fica incerta
const word = ref("");

function mods() {
  const m: string[] = [];
  if (ctrl.value) m.push("ctrl");
  if (alt.value) m.push("alt");
  ctrl.value = alt.value = false;
  return m;
}

async function type(ch: string) {
  const m = mods();
  if (m.length && /^[a-z0-9]$/i.test(ch)) {
    word.value = "";
    await keyboardKey(ch.toLowerCase(), m);
    return;
  }
  const out = label(ch);
  if (shift.value === "once") shift.value = "off";
  // começou a digitar: as ferramentas dão lugar às sugestões (ou aos atalhos, no terminal)
  if (bar.value === "ferramentas") bar.value = contextBar();
  word.value = /[\p{L}\p{N}]/u.test(out) ? word.value + out : "";
  await keyboardType(out);
}

async function key(name: string) {
  const m = mods();
  if (name === "BackSpace") word.value = word.value.slice(0, -1);
  else if (name === "space" || name === "Return") {
    if (word.value.length > 2) learn(word.value);
    word.value = "";
  } else word.value = "";
  await keyboardKey(name, m);
}

// ---- ajustes do teclado (⚙ no rodapé), guardados neste aparelho ----

interface Settings {
  /** apagar segurando repete */
  repeat: boolean;
  /** espera antes de começar a repetir (ms) */
  repeatDelay: number;
  /** intervalo entre as repetições (ms) */
  repeatRate: number;
  vibrate: boolean;
  vibrateMs: number;
  suggestions: boolean;
}
const SETTINGS = "lumna.keyboard.settings";
const settings = ref<Settings>({ repeat: true, repeatDelay: 500, repeatRate: 120, vibrate: true, vibrateMs: 30, suggestions: true });
try {
  settings.value = { ...settings.value, ...JSON.parse(localStorage.getItem(SETTINGS) ?? "{}") };
} catch {}
// as forças antigas (10/18 ms) eram curtas demais pro motor do 6T
if (![20, 30, 45].includes(settings.value.vibrateMs)) settings.value.vibrateMs = 30;
const saveSettings = () => {
  try {
    localStorage.setItem(SETTINGS, JSON.stringify(settings.value));
  } catch {}
};
const buzzes = [
  { label: "Leve", ms: 20 },
  { label: "Média", ms: 30 },
  { label: "Forte", ms: 45 },
];
const speeds = [
  { label: "Lenta", rate: 200 },
  { label: "Média", rate: 120 },
  { label: "Rápida", rate: 60 },
];

// apagar segurando: um toque apaga um; segurando, espera e repete no ritmo dos ajustes.
// O dedo é "capturado" pela tecla, e qualquer jeito de soltar (levantar, cancelar, sair) para.
let repeat = 0;
let delay = 0;
let holding = false;
function holdStart(e: PointerEvent, name: string) {
  if (holding || ghost) return;
  holding = true;
  try {
    (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
  } catch {}
  key(name);
  if (!settings.value.repeat) return;
  delay = window.setTimeout(() => {
    repeat = window.setInterval(() => (holding ? key(name) : holdEnd()), settings.value.repeatRate);
  }, settings.value.repeatDelay);
}
function holdEnd() {
  holding = false;
  clearTimeout(delay);
  clearInterval(repeat);
}

// ---- tato: vibração e o balãozinho da tecla ----
// No WebKitGTK um toque pode chegar duas vezes: o do dedo e, logo depois de soltar, um "mouse"
// emulado a partir dele. Os cliques já vêm um só, mas o apagar escuta o pointerdown direto e
// apagava dois. Um segundo pointerdown de outro tipo logo em seguida (ou colado no anterior) é
// esse fantasma: não vibra, não mostra balão e não apaga.
let lastDown = { at: -1e9, type: "" };
let ghost = false;
const preview = ref<{ text: string; x: number; y: number; w: number } | null>(null);
let previewHide = 0;
function feel(e: PointerEvent) {
  const now = performance.now();
  ghost = (e.pointerType !== lastDown.type && now - lastDown.at < 600) || now - lastDown.at < 70;
  if (ghost) return;
  lastDown = { at: now, type: e.pointerType };
  const button = (e.target as Element).closest("button");
  if (!button) return;
  if (settings.value.vibrate) haptic(settings.value.vibrateMs).catch(() => {});
  const text = button.dataset.p;
  clearTimeout(previewHide);
  if (!text) return (preview.value = null);
  const r = button.getBoundingClientRect();
  preview.value = { text, x: r.left + r.width / 2, y: r.top, w: Math.max(r.width + 12, 48) };
}
function unfeel() {
  clearTimeout(previewHide);
  previewHide = window.setTimeout(() => (preview.value = null), 70);
}

function toggleShift() {
  // um toque: só a próxima letra; dois toques seguidos: caps lock
  shift.value = shift.value === "off" ? "once" : shift.value === "once" ? "lock" : "off";
}

// ---- sugestões ----

const BASE = `LUMNA sistema celular que não para com uma por mais como mas foi ele ela isso você
está tem muito também já quando então obrigado bom dia boa tarde noite tudo bem sim agora aqui
hoje amanhã ontem casa trabalho Emma arquivo pasta configurações internet ligação mensagem
porque onde quem fazer pode vou vamos depois antes ainda sempre nunca nada coisa gente`.split(/\s+/);

const STORE = "lumna.keyboard.words";
const learned = ref<Record<string, number>>({});
try {
  learned.value = JSON.parse(localStorage.getItem(STORE) ?? "{}");
} catch {}
function saveLearned() {
  try {
    localStorage.setItem(STORE, JSON.stringify(learned.value));
  } catch {}
}
function learn(w: string) {
  learned.value[w] = (learned.value[w] ?? 0) + 1;
  saveLearned();
}

const plain = (s: string) => s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
const suggestions = computed(() => {
  if (!settings.value.suggestions) return [];
  const all = [...Object.keys(learned.value).sort((a, b) => learned.value[b] - learned.value[a]), ...BASE];
  const unique = [...new Set(all)];
  if (!word.value) return unique.slice(0, 3);
  const q = plain(word.value);
  return unique.filter((w) => plain(w).startsWith(q) && w !== word.value).slice(0, 3);
});

async function pick(w: string) {
  // troca o pedaço digitado pela palavra inteira
  for (let i = 0; i < word.value.length; i++) await keyboardKey("BackSpace");
  learn(w);
  word.value = "";
  await keyboardType(w + " ");
}

// ---- área de transferência ----

interface Clip {
  text: string;
  pinned: boolean;
}
const CLIPS = "lumna.keyboard.clips";
const clips = ref<Clip[]>([]);
const onlyPinned = ref(false);
try {
  clips.value = JSON.parse(localStorage.getItem(CLIPS) ?? "[]");
} catch {}
const saveClips = () => {
  try {
    localStorage.setItem(CLIPS, JSON.stringify(clips.value));
  } catch {}
};

// guarda o que está copiado agora (os fixados ficam na frente)
async function captureClipboard() {
  const text = (await clipboardRead().catch(() => "")).trim();
  if (!text || clips.value.some((c) => c.text === text)) return;
  clips.value.unshift({ text, pinned: false });
  const pinned = clips.value.filter((c) => c.pinned);
  const rest = clips.value.filter((c) => !c.pinned).slice(0, 20);
  clips.value = [...pinned, ...rest];
  saveClips();
}

const shownClips = computed(() => (onlyPinned.value ? clips.value.filter((c) => c.pinned) : clips.value));

async function openClipboard() {
  page.value = "clipboard";
  await captureClipboard();
}
function togglePin(c: Clip) {
  c.pinned = !c.pinned;
  saveClips();
}
function removeClip(c: Clip) {
  clips.value = clips.value.filter((x) => x !== c);
  saveClips();
}
function clearClips() {
  clips.value = clips.value.filter((c) => c.pinned);
  saveClips();
}
async function paste(c: Clip) {
  word.value = "";
  await keyboardType(c.text);
}

// ---- emojis ----

const EMOJIS: Record<string, string> = {
  Rostos: "😀😃😄😁😆😅😂🤣🙂🙃😉😊😇🥰😍🤩😘😋😛😜🤪😎🤓🧐🤔🤨😐😑😶🙄😏😴😌😔😕😟😢😭😤😠😡🤯😳🥺😱😨😰🤗🤭🤫🥱😬🤐😷🤒🤕🤢🤮🥵🥶😵🥳",
  Gestos: "👍👎👌✌🤞🤟🤘🤙👈👉👆👇☝✋🤚🖐🖖👋🤝🙏👏🙌👐🤲💪🫶✍",
  Corações: "❤🧡💛💚💙💜🖤🤍🤎💔❣💕💞💓💗💖💘💝💟",
  Natureza: "🌙⭐🌟✨⚡🔥🌈☀🌤⛅🌧❄🌊🌸🌹🌻🌼🌷🌱🌿🍀🍁🐶🐱🐭🐰🦊🐻🐼🐨🐯🦁🐮🐷🐸🐵🐔🐧🐦🦋🐢🐍🐙",
  Coisas: "🎉🎊🎁🎂🎈🎮🕹🎧🎵🎶📱💻⌨🖥📷🎥💡🔋🔌📌📎✏📝📚💼🗂📁⏰⌛🔒🔑🛠⚙🚀✈🚗🏠☕🍕🍔🍟🍫🍿🍺",
  Símbolos: "✅❌⚠❗❓💯🔴🟠🟡🟢🔵🟣⚫⚪➕➖➗✖➡⬅⬆⬇🔁🔄💤💬👀🆗🆕",
};
const emojiTab = ref("Recentes");
const RECENT = "lumna.keyboard.emojis";
const recent = ref<string[]>([]);
try {
  recent.value = JSON.parse(localStorage.getItem(RECENT) ?? "[]");
} catch {}
const split = (str: string) => [...str].filter((c) => !/[\u200d\ufe0f]/.test(c));
const emojiList = computed(() => (emojiTab.value === "Recentes" ? recent.value : split(EMOJIS[emojiTab.value] ?? "")));
async function sendEmoji(e: string) {
  recent.value = [e, ...recent.value.filter((x) => x !== e)].slice(0, 32);
  try {
    localStorage.setItem(RECENT, JSON.stringify(recent.value));
  } catch {}
  word.value = "";
  await keyboardType(e);
}
function openEmojis() {
  emojiTab.value = recent.value.length ? "Recentes" : "Rostos";
  page.value = "emojis";
}

// ---- abrir e fechar ----

let stop: UnlistenFn | null = null;
// Os Ajustes (outra janela) mudam o mesmo armazenamento: aplica aqui na hora.
function onStorage(e: StorageEvent) {
  if (e.key === SETTINGS) {
    try {
      settings.value = { ...settings.value, ...JSON.parse(e.newValue ?? "{}") };
    } catch {}
  } else if (e.key === STORE) {
    learned.value = {};
  }
}
onMounted(async () => {
  window.addEventListener("storage", onStorage);
  stop = await on<{ shown: boolean; lumna: boolean }>("lumna://keyboard", async ({ shown, lumna }) => {
    if (!shown) {
      page.value = "letras";
      return;
    }
    word.value = "";
    captureClipboard();
    const win = lumna ? null : await activeWindow().catch(() => null);
    const cls = (win?.class ?? "").toLowerCase();
    terminal.value = !!cls && TERMINALS.some((t) => cls.includes(t));
    bar.value = terminal.value ? "atalhos" : "ferramentas";
  });
});
onUnmounted(() => {
  stop?.();
  window.removeEventListener("storage", onStorage);
});
</script>

<template>
  <div class="kb" @contextmenu.prevent @pointerdown.capture="feel" @pointerup.capture="unfeel" @pointercancel.capture="unfeel">
    <div v-if="preview" class="bubble" :style="{ left: `${preview.x}px`, top: `${preview.y}px`, width: `${preview.w}px` }">{{ preview.text }}</div>
    <!-- área de transferência -->
    <template v-if="page === 'clipboard'">
      <div class="row top">
        <button class="key icon" @click="page = 'letras'"><Icon name="back" :size="22" /></button>
        <div class="title">Área de transferência</div>
        <button class="key icon" :class="{ on: onlyPinned }" @click="onlyPinned = !onlyPinned"><Icon name="pin" :size="20" /></button>
        <button class="key icon" @click="clearClips"><Icon name="trash" :size="20" /></button>
      </div>
      <div class="row arrows">
        <button class="key" @click="key('Left')"><Icon name="arrow-left" :size="20" /></button>
        <button class="key" @click="key('Up')"><Icon name="arrow-up" :size="20" /></button>
        <button class="key" @click="key('Down')"><Icon name="arrow-down" :size="20" /></button>
        <button class="key" @click="key('Right')"><Icon name="arrow-right" :size="20" /></button>
      </div>
      <div class="hint">Toque para colar</div>
      <div class="clips">
        <div v-for="(c, i) in shownClips" :key="i" class="clip" @click="paste(c)">
          <span class="text">{{ c.text }}</span>
          <small>Texto</small>
          <button class="pin" :class="{ on: c.pinned }" @click.stop="togglePin(c)"><Icon name="pin" :size="16" /></button>
          <button class="more" @click.stop="removeClip(c)"><Icon name="dots" :size="16" /></button>
        </div>
        <div v-if="!shownClips.length" class="empty">Copie um texto e ele aparece aqui.</div>
      </div>
    </template>

    <!-- emojis -->
    <template v-else-if="page === 'emojis'">
      <div class="row top">
        <button class="key icon" @click="page = 'letras'"><Icon name="back" :size="22" /></button>
        <div class="tabs">
          <button
            v-for="t in ['Recentes', ...Object.keys(EMOJIS)]"
            :key="t"
            :class="{ on: emojiTab === t }"
            @click="emojiTab = t"
          >{{ t }}</button>
        </div>
      </div>
      <div class="emojis">
        <button v-for="e in emojiList" :key="e" @click="sendEmoji(e)">{{ e }}</button>
        <div v-if="!emojiList.length" class="empty">Os emojis que você usar aparecem aqui.</div>
      </div>
      <div class="row">
        <button class="key mode" @click="page = 'letras'">ABC</button>
        <button class="key space" @click="key('space')">espaço</button>
        <button
          class="key wide"
          @pointerdown="holdStart($event, 'BackSpace')"
          @pointerup="holdEnd"
          @pointercancel="holdEnd"
          @lostpointercapture="holdEnd"
        ><Icon name="backspace" :size="20" /></button>
      </div>
    </template>

    <!-- ajustes do teclado -->
    <template v-else-if="page === 'ajustes'">
      <div class="row top">
        <button class="key icon" @click="page = 'letras'"><Icon name="back" :size="22" /></button>
        <div class="title">Ajustes do teclado</div>
      </div>
      <div class="prefs">
        <label class="pref">
          <span>Apagar segurando repete</span>
          <input v-model="settings.repeat" type="checkbox" @change="saveSettings" />
        </label>
        <div class="pref" :class="{ off: !settings.repeat }">
          <span>Espera antes de repetir <b>{{ (settings.repeatDelay / 1000).toFixed(1) }} s</b></span>
          <input v-model.number="settings.repeatDelay" type="range" min="250" max="1200" step="50" :disabled="!settings.repeat" @change="saveSettings" />
        </div>
        <div class="pref" :class="{ off: !settings.repeat }">
          <span>Velocidade ao repetir</span>
          <div class="seg">
            <button
              v-for="s in speeds"
              :key="s.rate"
              :class="{ on: settings.repeatRate === s.rate }"
              :disabled="!settings.repeat"
              @click="settings.repeatRate = s.rate; saveSettings()"
            >{{ s.label }}</button>
          </div>
        </div>
        <label class="pref">
          <span>Vibrar ao tocar</span>
          <input v-model="settings.vibrate" type="checkbox" @change="saveSettings" />
        </label>
        <div class="pref" :class="{ off: !settings.vibrate }">
          <span>Força da vibração</span>
          <div class="seg">
            <button
              v-for="v in buzzes"
              :key="v.ms"
              :class="{ on: settings.vibrateMs === v.ms }"
              :disabled="!settings.vibrate"
              @click="settings.vibrateMs = v.ms; saveSettings(); haptic(v.ms)"
            >{{ v.label }}</button>
          </div>
        </div>
        <label class="pref">
          <span>Sugestões de palavras</span>
          <input v-model="settings.suggestions" type="checkbox" @change="saveSettings" />
        </label>
        <button class="pref link" @click="learned = {}; saveLearned()">Esquecer as palavras aprendidas</button>
      </div>
    </template>

    <!-- letras e símbolos -->
    <template v-else>
      <div class="row top">
        <button class="key icon tools" :class="{ on: bar === 'ferramentas' }" @click="toggleBar"><Icon name="grid" :size="22" /></button>
        <template v-if="bar === 'ferramentas'">
          <button class="key tool" @click="page = 'ajustes'"><Icon name="settings" :size="18" /> Ajustes</button>
          <button class="key tool" @click="openEmojis"><span class="emo">😊</span> Emojis</button>
          <button class="key tool" @click="openClipboard"><Icon name="paste" :size="18" /> Copiados</button>
        </template>
        <template v-else-if="bar === 'atalhos'">
          <button class="key small" @click="key('Escape')">Esc</button>
          <button class="key small" @click="key('Tab')">Tab</button>
          <button class="key small" :class="{ on: ctrl }" @click="ctrl = !ctrl">Ctrl</button>
          <button class="key small" :class="{ on: alt }" @click="alt = !alt">Alt</button>
          <button class="key small" @click="key('Home')">Home</button>
          <button class="key small" @click="key('End')">End</button>
        </template>
        <template v-else>
          <div class="suggest">
            <button v-for="s in suggestions" :key="s" @click="pick(s)">{{ s }}</button>
          </div>
        </template>
      </div>
      <div class="row arrows" :class="{ framed: bar !== 'atalhos' }">
        <button class="key" @click="key('Left')"><Icon name="arrow-left" :size="20" /></button>
        <button class="key" @click="key('Up')"><Icon name="arrow-up" :size="20" /></button>
        <button class="key" @click="key('Down')"><Icon name="arrow-down" :size="20" /></button>
        <button class="key" @click="key('Right')"><Icon name="arrow-right" :size="20" /></button>
      </div>
      <div class="row">
        <button v-for="n in '1234567890'" :key="n" class="key" :data-p="n" @click="type(n)">{{ n }}</button>
      </div>
      <div class="row">
        <button v-for="k in rows[0]" :key="k" class="key" :data-p="label(k)" @click="type(k)">{{ label(k) }}</button>
      </div>
      <div class="row">
        <button v-for="k in rows[1]" :key="k" class="key" :data-p="label(k)" @click="type(k)">{{ label(k) }}</button>
      </div>
      <div class="row">
        <button v-if="page === 'letras'" class="key wide" :class="{ on: shift !== 'off', lock: shift === 'lock' }" @click="toggleShift">
          <Icon name="shift" :size="20" />
        </button>
        <button v-else class="key wide small" @click="page = page === 'simbolos' ? 'simbolos2' : 'simbolos'">{{ page === "simbolos" ? "=\\<" : "?123" }}</button>
        <button v-for="k in rows[2]" :key="k" class="key" :data-p="label(k)" @click="type(k)">{{ label(k) }}</button>
        <button v-if="page === 'simbolos'" class="key" @click="type('â')">â</button>
        <button v-if="page === 'simbolos2'" class="key" @click="type('ã')">ã</button>
        <button
          class="key wide"
          @pointerdown="holdStart($event, 'BackSpace')"
          @pointerup="holdEnd"
          @pointercancel="holdEnd"
          @lostpointercapture="holdEnd"
        >
          <Icon name="backspace" :size="20" />
        </button>
      </div>
      <div class="row">
        <button class="key mode" @click="page = page === 'letras' ? 'simbolos' : 'letras'">{{ page === "letras" ? "?123" : "ABC" }}</button>
        <button class="key" @click="type(terminal ? '/' : ',')">{{ terminal ? "/" : "," }}</button>
        <button class="key space" @click="key('space')">{{ layout === "pt" ? "Português (BR)" : "English (US)" }}</button>
        <button class="key" @click="type(terminal ? '-' : '.')">{{ terminal ? "-" : "." }}</button>
        <button class="key enter" @click="key('Return')"><Icon name="enter" :size="22" /></button>
      </div>
    </template>

    <footer>
      <button @click="layout = layout === 'pt' ? 'en' : 'pt'"><Icon name="globe" :size="22" /> Trocar</button>
      <button @click="keyboardShow(false)"><Icon name="chevron-down" :size="22" /> Ocultar</button>
    </footer>
    <div class="handle" />
  </div>
</template>

<style scoped>
.kb {
  height: 100vh;
  box-sizing: border-box;
  padding: 6px 6px 0;
  display: flex;
  flex-direction: column;
  gap: 5px;
  background: #121216;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
  color: #f2f0f8;
  touch-action: none;
}
.row {
  display: flex;
  gap: 5px;
  height: 44px;
}
.key {
  flex: 1;
  min-width: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  border: 0;
  border-radius: 8px;
  background: #2a2a31;
  color: inherit;
  font: inherit;
  font-size: 23px;
  font-weight: 500;
  box-shadow: 0 1px 0 rgba(0, 0, 0, 0.5);
}
.key:active {
  background: #45454f;
}
.key.on {
  background: #3c3355;
  color: var(--lumna-violet-soft);
}
.key.lock {
  box-shadow: inset 0 -3px 0 var(--lumna-violet-soft);
}
.key.small {
  font-size: 15px;
}
.key.icon {
  flex: 0 0 44px;
}
.tools {
  color: var(--lumna-violet-soft);
}
.key.wide {
  flex: 1.5;
}
.key.mode {
  flex: 1.5;
  font-size: 16px;
}
.key.space {
  flex: 4.5;
  font-size: 15px;
}
.key.enter {
  flex: 1.5;
  background: #9a7cf4;
  color: #fff;
}
.bubble {
  position: fixed;
  z-index: 5;
  height: 52px;
  transform: translate(-50%, -100%) translateY(-4px);
  display: flex;
  align-items: center;
  justify-content: center;
  border-radius: 10px;
  background: #3a3a44;
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.5);
  font-size: 30px;
  font-weight: 500;
  pointer-events: none;
}
.suggest {
  flex: 1;
  display: flex;
  border-radius: 8px;
  background: #1d1d22;
}
.suggest button {
  flex: 1;
  border: 0;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 15px;
}
.suggest button + button {
  border-left: 1px solid rgba(255, 255, 255, 0.12);
}
.arrows.framed {
  padding: 3px;
  margin: -3px 0;
  border: 1.5px solid #9a7cf4;
  border-radius: 10px;
}
.title {
  flex: 1;
  display: flex;
  align-items: center;
  padding-left: 12px;
  border-radius: 8px;
  background: #1d1d22;
  font-size: 18px;
}
.hint {
  padding: 2px 4px;
  color: #a8a6b2;
  font-size: 13px;
}
.clips {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  display: grid;
  grid-template-columns: 1fr 1fr;
  grid-auto-rows: 74px;
  gap: 6px;
}
.clip {
  position: relative;
  padding: 8px 30px 8px 10px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 8px;
  background: #1d1d22;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}
.clip .text {
  font-size: 13px;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}
.clip small {
  color: #a8a6b2;
  font-size: 11px;
}
.clip .pin,
.clip .more {
  position: absolute;
  right: 4px;
  border: 0;
  background: none;
  color: #a8a6b2;
}
.clip .pin {
  top: 6px;
}
.clip .pin.on {
  color: var(--lumna-violet-soft);
}
.clip .more {
  bottom: 6px;
}
.empty {
  grid-column: 1 / -1;
  color: #a8a6b2;
  font-size: 13px;
  text-align: center;
  padding-top: 20px;
}
.key.tool {
  gap: 6px;
  font-size: 13px;
}
.emo {
  font-size: 16px;
}
.tabs {
  flex: 1;
  display: flex;
  gap: 4px;
  overflow-x: auto;
  scrollbar-width: none;
}
.tabs button {
  flex: none;
  padding: 0 10px;
  border: 0;
  border-radius: 8px;
  background: #1d1d22;
  color: #a8a6b2;
  font: inherit;
  font-size: 13px;
}
.tabs button.on {
  background: #3c3355;
  color: var(--lumna-violet-soft);
}
.emojis {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  display: grid;
  grid-template-columns: repeat(8, 1fr);
  grid-auto-rows: 44px;
  align-content: start;
}
.emojis button {
  border: 0;
  border-radius: 8px;
  background: none;
  font-size: 26px;
  font-family: "Noto Color Emoji", sans-serif;
}
.emojis button:active {
  background: #2a2a31;
}
.prefs {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.pref {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 10px 12px;
  border-radius: 8px;
  background: #1d1d22;
  font-size: 14px;
}
label.pref {
  flex-direction: row;
  justify-content: space-between;
  align-items: center;
}
.pref.off {
  opacity: 0.45;
}
.pref b {
  color: var(--lumna-violet-soft);
  font-weight: 500;
}
.pref input[type="checkbox"] {
  width: 20px;
  height: 20px;
  accent-color: #9a7cf4;
}
.pref input[type="range"] {
  accent-color: #9a7cf4;
}
.pref.link {
  border: 0;
  color: #e7a3a3;
  font: inherit;
  font-size: 14px;
  text-align: left;
}
.seg {
  display: flex;
  gap: 5px;
}
.seg button {
  flex: 1;
  height: 32px;
  border: 0;
  border-radius: 8px;
  background: #2a2a31;
  color: inherit;
  font: inherit;
  font-size: 13px;
}
.seg button.on {
  background: #3c3355;
  color: var(--lumna-violet-soft);
}
footer {
  margin-top: auto;
  display: flex;
  justify-content: space-between;
  border-top: 1px solid rgba(255, 255, 255, 0.06);
  height: 34px;
}
footer button {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 12px;
  border: 0;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 14px;
}
.handle {
  margin: 4px auto 6px;
  width: 120px;
  height: 4px;
  border-radius: 2px;
  background: rgba(236, 232, 255, 0.7);
}
</style>
