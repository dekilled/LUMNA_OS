<script setup lang="ts">
// O app da Emma: a conversa longa (com histórico), as ferramentas que ela pode usar no LUMNA, o
// provedor de IA e o perfil (personalidade, diretrizes e voz). O popup do assistente é rápido e
// passageiro; aqui é onde a conversa continua e onde tudo se ajusta.
// Celular: abas embaixo; PC: barra lateral.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { parseReply, plainText, sentenceEnd, speakable } from "./assist";
import {
  emmaConversations,
  emmaDecide,
  emmaDeleteConversation,
  emmaHistory,
  emmaInstallVoice,
  emmaListen,
  emmaListenStop,
  emmaLogin,
  emmaLogout,
  emmaNewConversation,
  emmaOpenConversation,
  emmaPermissions,
  emmaProviders,
  emmaSaveSettings,
  emmaSend,
  emmaSetPermission,
  emmaSetProvider,
  emmaSettings,
  emmaSpeak,
  emmaSpeakStop,
  emmaStop,
  emmaStatus,
  on,
  type ActionInfo,
  type ActionPermission,
  type EmmaConversation,
  type EmmaEvent,
  type EmmaSettings,
  type EmmaStatus,
  type EmmaTool,
  type EmmaVoice,
  type ProviderPreset,
} from "../../system";

type Tab = "conversa" | "ferramentas" | "provedores" | "perfil";
const TABS: { tab: Tab; label: string; icon: string }[] = [
  { tab: "conversa", label: "Conversa", icon: "chat" },
  { tab: "ferramentas", label: "Ferramentas", icon: "zap" },
  { tab: "provedores", label: "Provedores", icon: "sparkle" },
  { tab: "perfil", label: "Perfil", icon: "user" },
];

const tab = ref<Tab>(TABS.some((t) => t.tab === location.hash.slice(1)) ? (location.hash.slice(1) as Tab) : "conversa");
const narrow = ref(window.innerWidth < 640);
const status = ref<EmmaStatus | null>(null);

// ---- conversa ----

interface ToolView {
  id: string;
  name: string;
  label: string;
  state: "running" | "confirm" | "ok" | "error" | "denied";
  summary: string;
  reason?: string;
}

interface Bubble {
  role: "user" | "assistant";
  text: string;
  tools: ToolView[];
  voice?: boolean;
  spoken?: number;
  done?: boolean;
}

const bubbles = ref<Bubble[]>([]);
const draft = ref("");
const busy = ref(false);
const error = ref("");
const scroller = ref<HTMLElement>();
const input = ref<HTMLTextAreaElement>();
const mic = ref<"idle" | "listening" | "transcribing">("idle");
const level = ref(0);
/** Ler as respostas em voz alta (também as escritas). */
const readAloud = ref(false);
const speaking = ref(false);
let mine = false;

/** Parar: ela cala, o microfone fecha e a resposta que estava chegando para onde está. */
function stop() {
  emmaStop().catch(() => {});
  speaking.value = false;
  mic.value = "idle";
  mine = false;
}
const canStop = computed(() => !draft.value.trim() && (busy.value || speaking.value));

const actions = ref<ActionInfo[]>([]);
const labels = computed(() => new Map(actions.value.map((a) => [`lumna_${a.name.replace(/\./g, "_")}`, a.label])));

function toolView(tool: EmmaTool): ToolView {
  return { id: tool.id, name: tool.name, label: labels.value.get(tool.name) ?? tool.name, state: "running", summary: "" };
}

const last = () => bubbles.value[bubbles.value.length - 1];

function assistant(): Bubble {
  if (last()?.role !== "assistant") bubbles.value.push({ role: "assistant", text: "", tools: [], spoken: 0 });
  return last();
}

function findTool(id: string) {
  for (let i = bubbles.value.length - 1; i >= 0; i--) {
    const tool = bubbles.value[i].tools.find((t) => t.id === id);
    if (tool) return tool;
  }
  return undefined;
}

async function scrollDown() {
  await nextTick();
  scroller.value?.scrollTo({ top: scroller.value.scrollHeight, behavior: "smooth" });
}

function speakNew(bubble: Bubble) {
  if (!bubble.voice) return;
  const body = parseReply(bubble.text).body;
  const from = bubble.spoken ?? 0;
  const end = bubble.done ? body.length : sentenceEnd(body, from);
  if (end <= from) return;
  bubble.spoken = end;
  const text = speakable(body.slice(from, end));
  if (text) emmaSpeak(text).catch((e) => (error.value = String(e)));
}

function onEvent(event: EmmaEvent) {
  if (event.channel && event.channel !== "app") return;
  const voice = last()?.role === "user" ? !!last().voice : !!assistant().voice;
  switch (event.type) {
    case "delta": {
      const b = assistant();
      b.voice = voice;
      b.text += event.content;
      speakNew(b);
      break;
    }
    case "rewrite":
      assistant().text = event.content;
      break;
    case "tool_start":
      assistant().tools.push(toolView(event.tool));
      break;
    case "tool_confirm": {
      const tool = findTool(event.tool.id) ?? (assistant().tools.push(toolView(event.tool)), last().tools.at(-1)!);
      tool.state = "confirm";
      tool.reason = event.reason;
      break;
    }
    case "tool_end": {
      const tool = findTool(event.tool.id);
      if (tool) {
        tool.state = event.tool.status === "ok" ? "ok" : event.tool.status === "denied" ? "denied" : "error";
        tool.summary = event.tool.summary;
      }
      break;
    }
    case "done": {
      const b = assistant();
      b.voice = voice;
      if (event.message?.content) b.text = event.message.content;
      b.done = true;
      speakNew(b);
      break;
    }
    case "error":
      error.value = event.message;
      break;
  }
  scrollDown();
}

async function send(text = draft.value, voice = false) {
  text = text.trim();
  if (!text || busy.value) return;
  emmaSpeakStop().catch(() => {});
  draft.value = "";
  error.value = "";
  bubbles.value.push({ role: "user", text, tools: [], voice: voice || readAloud.value });
  busy.value = true;
  scrollDown();
  try {
    await emmaSend(text, voice, "app");
  } catch (e) {
    error.value = String(e);
    // sem resposta do servidor: o aviso de fora do ar aparece no topo
    if (/não respondeu|login/.test(error.value)) refreshStatus();
    if (error.value.includes("Nenhum provider")) openTab("provedores");
  } finally {
    busy.value = false;
    const b = last();
    if (b?.role === "assistant" && !b.done) {
      b.done = true;
      speakNew(b);
    }
  }
}

function onKeydown(event: KeyboardEvent) {
  if (event.key === "Enter" && !event.shiftKey && !narrow.value) {
    event.preventDefault();
    send();
  }
}

async function decide(tool: ToolView, allow: boolean) {
  tool.state = allow ? "running" : "denied";
  try {
    await emmaDecide(tool.id, allow);
  } catch (e) {
    error.value = String(e);
  }
}

async function newConversation() {
  emmaSpeakStop().catch(() => {});
  await emmaNewConversation("app");
  bubbles.value = [];
  error.value = "";
  listOpen.value = false;
}

// ---- conversas anteriores ----

const listOpen = ref(false);
const conversations = ref<EmmaConversation[] | null>(null);
const confirmDelete = ref("");

async function toggleList() {
  listOpen.value = !listOpen.value;
  confirmDelete.value = "";
  if (!listOpen.value) return;
  conversations.value = null;
  try {
    conversations.value = await emmaConversations();
  } catch (e) {
    conversations.value = [];
    error.value = String(e);
  }
}

async function openConversation(c: EmmaConversation) {
  if (busy.value) return;
  emmaSpeakStop().catch(() => {});
  try {
    await emmaOpenConversation(c.id);
  } catch (e) {
    error.value = String(e);
    return;
  }
  error.value = "";
  listOpen.value = false;
  await loadHistory();
}

/** Apagar pede um segundo toque (o botão vira "Apagar?"). */
async function removeConversation(c: EmmaConversation) {
  if (confirmDelete.value !== c.id) {
    confirmDelete.value = c.id;
    return;
  }
  confirmDelete.value = "";
  try {
    await emmaDeleteConversation(c.id);
  } catch (e) {
    error.value = String(e);
    return;
  }
  conversations.value = conversations.value?.filter((x) => x.id !== c.id) ?? null;
  if (c.current) bubbles.value = [];
}

function when(iso: string) {
  const d = new Date(iso.endsWith("Z") || /[+-]\d\d:?\d\d$/.test(iso) ? iso : iso + "Z");
  if (Number.isNaN(d.getTime())) return "";
  const today = new Date();
  const yesterday = new Date(Date.now() - 86_400_000);
  const hour = d.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
  if (d.toDateString() === today.toDateString()) return `Hoje, ${hour}`;
  if (d.toDateString() === yesterday.toDateString()) return `Ontem, ${hour}`;
  return d.toLocaleDateString("pt-BR", { day: "numeric", month: "short", year: d.getFullYear() === today.getFullYear() ? undefined : "numeric" });
}

async function toggleMic() {
  if (mic.value !== "idle") return emmaListenStop(false);
  error.value = "";
  mic.value = "listening";
  mine = true;
  try {
    await emmaListen();
  } catch (e) {
    mic.value = "idle";
    mine = false;
    error.value = String(e);
  }
}

function onVoice(v: EmmaVoice) {
  if (v.state === "speaking" || v.state === "spoken") {
    speaking.value = v.state === "speaking";
    return;
  }
  if (v.state === "error") {
    speaking.value = false;
    if (mine || mic.value !== "idle" || tab.value === "perfil") error.value = v.message;
    if (mine) profile.value.voiceError = v.message;
    mine = false;
    mic.value = "idle";
    return;
  }
  if (!mine) return;
  if (v.state === "listening") {
    mic.value = "listening";
    level.value = v.level;
  } else if (v.state === "transcribing") {
    mic.value = "transcribing";
  } else if (v.state === "heard") {
    mine = false;
    mic.value = "idle";
    send(v.text, true);
  } else if (v.state === "idle") {
    mine = false;
    mic.value = "idle";
  }
}

async function loadHistory() {
  const history = await emmaHistory("app").catch(() => []);
  bubbles.value = history.map((m) => ({
    role: m.role,
    text: m.content,
    done: true,
    tools: (m.tools ?? []).map((t) => ({ ...toolView(t), state: t.status === "ok" ? "ok" : "error", summary: t.summary }) as ToolView),
  }));
  scrollDown();
}

// ---- entrar ----

const login = ref({ url: "http://127.0.0.1:8001", email: "", password: "", busy: false, error: "" });

async function refreshStatus() {
  status.value = await emmaStatus().catch(() => status.value);
  if (!status.value) return;
  if (status.value.url) login.value.url = status.value.url;
  if (status.value.email) login.value.email = status.value.email;
}

async function submitLogin() {
  login.value.busy = true;
  login.value.error = "";
  try {
    await emmaLogin(login.value.url, login.value.email, login.value.password);
    login.value.password = "";
    await refreshStatus();
    await loadHistory();
  } catch (e) {
    login.value.error = String(e);
  } finally {
    login.value.busy = false;
  }
}

async function logout() {
  await emmaLogout();
  bubbles.value = [];
  await refreshStatus();
}

// ---- ferramentas ----

const PERMISSION_LABEL: Record<ActionPermission, string> = { auto: "Sozinha", ask: "Pergunta", off: "Desligada" };
const toolQuery = ref("");
const plain = (s: string) => s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
const shownActions = computed(() => {
  const q = plain(toolQuery.value.trim());
  return q ? actions.value.filter((a) => plain(`${a.label} ${a.description}`).includes(q)) : actions.value;
});

async function setPermission(action: ActionInfo, permission: ActionPermission) {
  await emmaSetPermission(action.name, permission);
  actions.value = await emmaPermissions();
}

// ---- provedores ----

const presets = ref<ProviderPreset[]>([]);
const provider = ref({ current: "", preset: "ollama", baseUrl: "", apiKey: "", model: "", busy: false, error: "", saved: false });
const preset = computed(() => presets.value.find((p) => p.preset === provider.value.preset));

function pickPreset(id: string) {
  provider.value.preset = id;
  const p = presets.value.find((x) => x.preset === id);
  provider.value.baseUrl = p?.base_url ?? "";
  provider.value.model = p?.models[0] ?? "";
}

async function loadProviders() {
  provider.value.error = "";
  try {
    const info = await emmaProviders();
    presets.value = info.presets;
    provider.value.current = info.current ? `${info.current.name} · ${info.current.model}` : "";
    pickPreset(provider.value.preset);
  } catch (e) {
    provider.value.error = String(e);
  }
}

async function saveProvider() {
  const p = provider.value;
  p.busy = true;
  p.error = "";
  p.saved = false;
  try {
    await emmaSetProvider(preset.value?.name ?? "Provedor", p.baseUrl, p.apiKey, p.model);
    p.apiKey = "";
    p.saved = true;
    await loadProviders();
  } catch (e) {
    p.error = String(e);
  } finally {
    p.busy = false;
  }
}

// ---- perfil ----

const settings = ref<EmmaSettings | null>(null);
const profile = ref({
  personality: "",
  systemPrompt: "",
  tts: "fish",
  stt: "fish",
  voiceId: "",
  ttsModel: "",
  asrModel: "",
  language: "",
  apiKey: "",
  autoRead: false,
  busy: false,
  error: "",
  saved: false,
  voiceError: "",
});

async function loadProfile() {
  profile.value.error = "";
  try {
    const s = await emmaSettings();
    settings.value = s;
    Object.assign(profile.value, {
      personality: s.personality ?? "",
      systemPrompt: s.system_prompt ?? "",
      tts: s.voice?.tts_engine || "fish",
      stt: s.voice?.stt_engine || "fish",
      voiceId: s.voice?.voice_id ?? "",
      ttsModel: s.voice?.tts_model ?? "",
      asrModel: s.voice?.asr_model ?? "",
      language: s.voice?.language ?? "",
      autoRead: !!s.voice?.auto_read,
      apiKey: "",
    });
    readAloud.value = !!s.voice?.auto_read;
  } catch (e) {
    profile.value.error = String(e);
  }
}

// O modelo de voz do Fish Audio: vazio é o gratuito (o padrão do Fish sem modelo é o pago)
const FREE_TTS = "s2.1-pro-free";
const PAID_TTS = "s2.1-pro";
const ttsOther = ref(false);
const ttsChoice = computed(() => {
  const m = profile.value.ttsModel.trim();
  if (ttsOther.value) return "other";
  return !m || m === FREE_TTS ? "free" : m === PAID_TTS ? "paid" : "other";
});
/** `null`: "Outro" (o campo pro nome aparece). */
function pickTts(model: string | null) {
  ttsOther.value = model === null;
  if (model !== null) profile.value.ttsModel = model;
  else if ([FREE_TTS, PAID_TTS, ""].includes(profile.value.ttsModel.trim())) profile.value.ttsModel = "";
}

async function saveProfile() {
  const p = profile.value;
  p.busy = true;
  p.error = "";
  p.saved = false;
  try {
    const voice: Record<string, unknown> = {
      tts_engine: p.tts,
      stt_engine: p.stt,
      voice_id: p.voiceId.trim(),
      tts_model: p.ttsModel.trim() || FREE_TTS,
      asr_model: p.asrModel.trim(),
      language: p.language.trim(),
      auto_read: p.autoRead,
    };
    if (p.apiKey.trim()) voice.api_key = p.apiKey.trim();
    await emmaSaveSettings({ personality: p.personality, systemPrompt: p.systemPrompt, voice });
    p.saved = true;
    await loadProfile();
  } catch (e) {
    p.error = String(e);
  } finally {
    p.busy = false;
  }
}

async function testVoice() {
  profile.value.voiceError = "";
  await emmaSpeakStop().catch(() => {});
  emmaSpeak("Oi! Eu sou a Emma. É assim que eu falo.").catch((e) => (profile.value.voiceError = String(e)));
}

async function installVoice(what: "stt" | "tts") {
  profile.value.error = "";
  try {
    await emmaInstallVoice(what);
    window.setTimeout(loadProfile, 3000);
  } catch (e) {
    profile.value.error = String(e);
  }
}

/** A Emma voltou? Confere o servidor de novo e recarrega a aba aberta. */
const retrying = ref(false);
async function retry() {
  retrying.value = true;
  error.value = "";
  try {
    await refreshStatus();
    if (status.value?.online && status.value.configured) {
      if (tab.value === "conversa") await loadHistory();
      else await openTab(tab.value);
    }
  } finally {
    retrying.value = false;
  }
}

// ---- abas ----

async function openTab(t: Tab) {
  tab.value = t;
  if (t === "provedores") await loadProviders();
  if (t === "perfil") await loadProfile();
  if (t === "ferramentas") actions.value = await emmaPermissions();
  if (t === "conversa") scrollDown();
}

const stops: (() => void)[] = [];
const onResize = () => (narrow.value = window.innerWidth < 640);

onMounted(async () => {
  window.addEventListener("resize", onResize);
  stops.push(() => window.removeEventListener("resize", onResize));
  stops.push(await on<EmmaEvent>("lumna://emma", onEvent));
  stops.push(await on<EmmaVoice>("lumna://emma-voice", onVoice));
  stops.push(
    await on<string>("lumna://emma-app-tab", (t) => {
      if (!TABS.some((x) => x.tab === t)) return;
      openTab(t as Tab);
      // pode ter chegado uma conversa do popup ("Continuar no app")
      if (t === "conversa" && !busy.value) {
        listOpen.value = false;
        loadHistory();
      }
    }),
  );
  actions.value = await emmaPermissions().catch(() => []);
  await refreshStatus().catch(() => {});
  if (status.value?.configured) {
    await loadHistory();
    emmaSettings()
      .then((s) => (readAloud.value = !!s.voice?.auto_read))
      .catch(() => {});
  }
  if (tab.value !== "conversa") openTab(tab.value);
});

onUnmounted(() => {
  emmaSpeakStop().catch(() => {});
  stops.forEach((stop) => stop());
});
</script>

<template>
  <div class="emma-app" :class="{ narrow }">
    <nav class="tabs">
      <div v-if="!narrow" class="brand">
        <svg class="gem" viewBox="0 0 24 24" aria-hidden="true">
          <path d="M12 1.5 21.5 12 12 22.5 2.5 12z" fill="#a78bfa" />
        </svg>
        <span>Emma</span>
      </div>
      <button v-for="t in TABS" :key="t.tab" :class="{ on: tab === t.tab }" @click="openTab(t.tab)">
        <Icon :name="t.icon" :size="narrow ? 22 : 18" />
        <span>{{ t.label }}</span>
      </button>
      <small v-if="!narrow" class="online" :class="{ yes: status?.online }">
        {{ status?.online ? "Conectada" : "Fora do ar" }}
      </small>
    </nav>

    <main>
      <!-- o servidor da Emma não responde: diz onde e o que fazer (senão as telas só ficam vazias) -->
      <div v-if="status && !status.online" class="offline">
        <Icon name="offline" :size="22" />
        <div>
          <strong>A Emma não está respondendo</strong>
          <small>
            Nada em {{ status.url }}. O serviço dela pode ter parado; no terminal do celular:
            <code>systemctl --user restart lumna-emma</code>
          </small>
        </div>
        <button class="ghost" :disabled="retrying" @click="retry()">{{ retrying ? "…" : "Tentar de novo" }}</button>
      </div>

      <!-- Entrar (sem conta ainda) -->
      <form v-if="status && !status.configured" class="page form" @submit.prevent="submitLogin()">
        <h1>Conectar à Emma</h1>
        <p class="hint">Entre com a conta do servidor da Emma (NewHarnessV2). Com o pacote <code>lumna-emma</code> isso é automático.</p>
        <label>Endereço<input v-model="login.url" spellcheck="false" /></label>
        <label>E-mail<input v-model="login.email" type="email" autocomplete="username" /></label>
        <label>Senha<input v-model="login.password" type="password" autocomplete="current-password" /></label>
        <p v-if="login.error" class="error">{{ login.error }}</p>
        <button class="primary" :disabled="login.busy || !login.email || !login.password">
          {{ login.busy ? "Entrando…" : "Entrar" }}
        </button>
      </form>

      <!-- Conversa -->
      <section v-else-if="tab === 'conversa'" class="chat">
        <header class="bar">
          <h1>Conversa</h1>
          <button class="icon" :class="{ on: readAloud }" title="Ler as respostas em voz alta" @click="readAloud = !readAloud">
            <Icon :name="readAloud ? 'volume' : 'mute'" :size="20" />
          </button>
          <button class="icon" :class="{ on: listOpen }" title="Conversas anteriores" @click="toggleList()"><Icon name="list" :size="20" /></button>
          <button class="icon" title="Nova conversa" @click="newConversation()"><Icon name="plus" :size="20" /></button>
        </header>
        <div v-if="listOpen" class="conversations">
          <button class="new" @click="newConversation()"><Icon name="plus" :size="18" /> Nova conversa</button>
          <p v-if="conversations === null" class="empty">Carregando…</p>
          <p v-else-if="!conversations.length" class="empty">Nenhuma conversa ainda.</p>
          <ul v-else>
            <li v-for="c in conversations" :key="c.id" :class="{ current: c.current }">
              <button class="open" @click="openConversation(c)">
                <span class="title">{{ c.title || "Conversa" }}</span>
                <small>{{ c.current ? "Aberta agora" : when(c.updated_at) }}</small>
              </button>
              <button class="del" :class="{ sure: confirmDelete === c.id }" :title="confirmDelete === c.id ? 'Tocar de novo apaga' : 'Apagar'" @click="removeConversation(c)">
                <span v-if="confirmDelete === c.id">Apagar?</span>
                <Icon v-else name="trash" :size="18" />
              </button>
            </li>
          </ul>
        </div>
        <div ref="scroller" class="messages">
          <p v-if="bubbles.length === 0" class="empty">
            Peça qualquer coisa: <em>“abre o Spotify”</em>, <em>“volume 30”</em>, <em>“acha o PDF do contrato”</em>. Segure a
            barrinha (no celular) pra falar com ela de qualquer tela.
          </p>
          <div v-for="(b, i) in bubbles" :key="i" class="bubble" :class="b.role">
            <ul v-if="b.tools.length" class="tools">
              <li v-for="tool in b.tools" :key="tool.id" :class="tool.state">
                <div class="tool-line">
                  <span class="dot" />
                  <span class="label">{{ tool.label }}</span>
                  <span v-if="tool.summary" class="summary">{{ tool.summary }}</span>
                </div>
                <div v-if="tool.state === 'confirm'" class="confirm">
                  <p>{{ tool.reason }}</p>
                  <div class="row">
                    <button class="ghost" @click="decide(tool, false)">Não</button>
                    <button class="primary small" @click="decide(tool, true)">Permitir</button>
                  </div>
                </div>
              </li>
            </ul>
            <p v-if="b.role === 'user'" class="text">{{ b.text }}</p>
            <p v-else-if="b.text" class="text">{{ plainText(parseReply(b.text).body) }}</p>
          </div>
          <div v-if="busy && last()?.role === 'user'" class="bubble assistant typing"><span /><span /><span /></div>
          <p v-if="error" class="error">{{ error }}</p>
        </div>
        <form class="composer" @submit.prevent="send()">
          <button type="button" class="mic" :class="{ on: mic !== 'idle' }" :style="{ '--level': level }" title="Falar" @click="toggleMic()">
            <Icon name="mic" :size="20" />
          </button>
          <textarea
            ref="input"
            v-model="draft"
            rows="1"
            :placeholder="mic === 'listening' ? 'Ouvindo…' : mic === 'transcribing' ? 'Entendendo…' : 'Fale com a Emma…'"
            :disabled="mic !== 'idle'"
            @keydown="onKeydown"
          />
          <button v-if="canStop" type="button" class="send stop" title="Parar" @click="stop()"><Icon name="stop" :size="16" /></button>
          <button v-else class="send" :disabled="busy || !draft.trim()" title="Enviar"><Icon name="send" :size="18" /></button>
        </form>
      </section>

      <!-- Ferramentas -->
      <section v-else-if="tab === 'ferramentas'" class="page">
        <h1>Ferramentas</h1>
        <p class="hint">
          O que a Emma pode fazer no LUMNA. <strong>Sozinha</strong>: faz direto. <strong>Pergunta</strong>: pede antes.
          Desligar, instalar e comandos sempre perguntam. Com a tela bloqueada ela só mexe no volume, na mídia e vê o Wi-Fi.
        </p>
        <label class="search"><Icon name="search" :size="18" /><input v-model="toolQuery" placeholder="Buscar ferramenta" /></label>
        <ul class="list">
          <li v-for="action in shownActions" :key="action.name">
            <div class="info">
              <span class="label">{{ action.label }}</span>
              <span class="desc">{{ action.description }}</span>
            </div>
            <div class="segmented">
              <button
                v-for="p in ['auto', 'ask', 'off'] as ActionPermission[]"
                :key="p"
                :class="{ on: action.permission === p }"
                :disabled="p === 'auto' && action.risk === 'danger'"
                @click="setPermission(action, p)"
              >
                {{ PERMISSION_LABEL[p] }}
              </button>
            </div>
          </li>
        </ul>
      </section>

      <!-- Provedores -->
      <form v-else-if="tab === 'provedores'" class="page form" @submit.prevent="saveProvider()">
        <h1>Provedores</h1>
        <p v-if="provider.current" class="hint">Em uso: <strong>{{ provider.current }}</strong>. Pra trocar, escolha outro:</p>
        <p v-else class="hint">Escolha o serviço de IA que a Emma usa pra pensar. A chave fica no servidor da Emma.</p>
        <div class="chips">
          <button v-for="p in presets" :key="p.preset" type="button" :class="{ on: provider.preset === p.preset }" @click="pickPreset(p.preset)">
            {{ p.name }}
          </button>
        </div>
        <label v-if="provider.preset === 'custom'">Endereço<input v-model="provider.baseUrl" spellcheck="false" /></label>
        <label>Modelo<input v-model="provider.model" spellcheck="false" /></label>
        <div v-if="preset?.models.length" class="chips small">
          <button v-for="m in preset.models" :key="m" type="button" :class="{ on: provider.model === m }" @click="provider.model = m">
            {{ m }}
          </button>
        </div>
        <label>Chave da API<input v-model="provider.apiKey" type="password" autocomplete="off" placeholder="(a que já está salva continua)" /></label>
        <div v-if="provider.error" class="problem">
          <p class="error">{{ provider.error }}</p>
          <button type="button" class="ghost" @click="loadProviders()">Tentar de novo</button>
        </div>
        <p v-if="provider.saved" class="ok">Pronto: a Emma já está usando esse.</p>
        <button class="primary" :disabled="provider.busy || !provider.model">{{ provider.busy ? "Salvando…" : "Usar este" }}</button>
      </form>

      <!-- Perfil: persona e voz -->
      <form v-else class="page form" @submit.prevent="saveProfile()">
        <h1>Perfil</h1>
        <p v-if="settings?.profile" class="hint">
          Editando a Emma do perfil <strong>{{ settings.profile === "sfw" ? "SFW (modo visita)" : "NSFW" }}</strong>, o que está
          em uso no servidor agora. Cada perfil guarda a sua personalidade e as suas diretrizes.
        </p>
        <h2>Personalidade</h2>
        <p class="hint">Como a Emma fala com você (o jeito, o humor, o tratamento).</p>
        <textarea v-model="profile.personality" rows="4" placeholder="Ex.: descontraída, carinhosa, me chama de parceiro…" />
        <h2>Diretrizes</h2>
        <p class="hint">O que ela deve saber e seguir sempre (sobre você, seu trabalho, como responder).</p>
        <textarea v-model="profile.systemPrompt" rows="5" placeholder="Ex.: responda em português; eu programo em Rust e Vue…" />

        <h2>Voz</h2>
        <div class="field">
          <span>Falar (TTS)</span>
          <div class="segmented">
            <button type="button" :class="{ on: profile.tts === 'fish' }" @click="profile.tts = 'fish'">Fish Audio</button>
            <button type="button" :class="{ on: profile.tts === 'local' }" @click="profile.tts = 'local'">Local</button>
          </div>
        </div>
        <div class="field">
          <span>Ouvir (STT)</span>
          <div class="segmented">
            <button type="button" :class="{ on: profile.stt === 'fish' }" @click="profile.stt = 'fish'">Fish Audio</button>
            <button type="button" :class="{ on: profile.stt === 'local' }" @click="profile.stt = 'local'">Local</button>
          </div>
        </div>
        <template v-if="profile.tts === 'fish' || profile.stt === 'fish'">
          <label>
            Chave do Fish Audio
            <input
              v-model="profile.apiKey"
              type="password"
              autocomplete="off"
              :placeholder="settings?.voice?.has_key ? `salva (${settings.voice.api_key_masked})` : 'cole a chave aqui'"
            />
          </label>
          <label>Voz (reference id)<input v-model="profile.voiceId" spellcheck="false" placeholder="o id da voz no Fish Audio" /></label>
          <div class="field">
            <span>Modelo de voz (falar)</span>
            <div class="segmented">
              <button type="button" :class="{ on: ttsChoice === 'free' }" @click="pickTts(FREE_TTS)">Gratuito</button>
              <button type="button" :class="{ on: ttsChoice === 'paid' }" @click="pickTts(PAID_TTS)">Pago</button>
              <button type="button" :class="{ on: ttsChoice === 'other' }" @click="pickTts(null)">Outro</button>
            </div>
          </div>
          <label v-if="ttsChoice === 'other'">
            Nome do modelo
            <input v-model="profile.ttsModel" spellcheck="false" placeholder="o id do modelo no Fish Audio" />
          </label>
          <p v-else class="hint">{{ ttsChoice === "free" ? `${FREE_TTS}: o de testes, sem custo.` : `${PAID_TTS}: cobra por uso na conta do Fish Audio.` }}</p>
          <label>
            Modelo de transcrição (ouvir)
            <input v-model="profile.asrModel" spellcheck="false" placeholder="vazio = o padrão do Fish Audio" />
          </label>
          <label>Idioma<input v-model="profile.language" spellcheck="false" placeholder="pt" /></label>
        </template>
        <div v-if="profile.tts === 'local' || profile.stt === 'local'" class="local">
          <p v-if="settings?.local_voice?.available === false" class="hint">
            As vozes locais (Whisper e Pocket TTS) só rodam no PC; no celular use o Fish Audio, ou aponte a Emma pro servidor do PC.
          </p>
          <template v-else>
            <button v-if="profile.stt === 'local' && !settings?.local_voice?.stt?.installed" type="button" class="ghost" @click="installVoice('stt')">
              Instalar o Whisper (ouvir)
            </button>
            <button v-if="profile.tts === 'local' && !settings?.local_voice?.tts?.installed" type="button" class="ghost" @click="installVoice('tts')">
              Instalar o Pocket TTS (falar)
            </button>
          </template>
        </div>
        <label class="check"><input v-model="profile.autoRead" type="checkbox" /> Ler as respostas do app em voz alta</label>
        <div class="row">
          <button type="button" class="ghost" @click="testVoice()"><Icon name="volume" :size="18" /> Testar a voz</button>
          <span class="status-line">
            Servidor: {{ settings?.voice?.server_tts ? "fala" : "sem voz" }} · {{ settings?.voice?.server_stt ? "ouve" : "não ouve" }}
          </span>
        </div>
        <p v-if="profile.voiceError" class="error">{{ profile.voiceError }}</p>
        <p v-if="profile.error" class="error">{{ profile.error }}</p>
        <p v-if="profile.saved" class="ok">Salvo.</p>
        <button class="primary" :disabled="profile.busy">{{ profile.busy ? "Salvando…" : "Salvar perfil" }}</button>

        <h2>Conta</h2>
        <p class="hint">{{ status?.email || "conta local" }} em {{ status?.url }}</p>
        <button type="button" class="ghost danger" @click="logout()">Sair da conta da Emma</button>
      </form>
    </main>
  </div>
</template>

<style scoped>
.emma-app {
  --bg: #0d0b14;
  --card: #16131f;
  --input: #1c1828;
  --line: rgba(167, 139, 250, 0.14);
  display: flex;
  height: var(--app-h, 100vh);
  background: var(--bg);
  color: var(--lumna-text);
  overflow: hidden;
}
.emma-app.narrow {
  flex-direction: column-reverse;
}
.tabs {
  flex: none;
  display: flex;
  flex-direction: column;
  gap: 4px;
  width: 210px;
  padding: 18px 12px;
  border-right: 1px solid var(--line);
}
.narrow .tabs {
  flex-direction: row;
  width: auto;
  padding: 6px 6px 8px;
  border-right: 0;
  border-top: 1px solid var(--line);
}
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 4px 10px 18px;
  font-weight: 700;
  letter-spacing: 0.2em;
}
.gem {
  width: 20px;
  height: 20px;
}
.tabs button {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 12px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-muted);
  font-size: 14px;
  text-align: left;
}
.narrow .tabs button {
  flex: 1;
  flex-direction: column;
  gap: 4px;
  padding: 6px 2px;
  font-size: 11px;
  text-align: center;
}
.tabs button.on {
  background: rgba(139, 92, 246, 0.16);
  color: var(--lumna-text);
}
.online {
  margin-top: auto;
  padding: 0 12px;
  color: #fca5a5;
}
.online.yes {
  color: #a7f3d0;
}
main {
  flex: 1;
  min-height: 0;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.page {
  flex: 1;
  overflow-y: auto;
  padding: 20px 22px 28px;
}
.form {
  display: flex;
  flex-direction: column;
  gap: 12px;
  max-width: 640px;
}
.form > * {
  flex-shrink: 0;
}
h1 {
  margin: 0 0 4px;
  font-size: 22px;
}
h2 {
  margin: 14px 0 0;
  font-size: 15px;
  color: var(--lumna-violet-soft);
}
.hint {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 14px;
  line-height: 1.45;
}
label {
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 13px;
  color: var(--lumna-muted);
}
input,
textarea {
  padding: 11px 13px;
  border: 1px solid var(--line);
  border-radius: 12px;
  background: var(--input);
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  outline: 0;
  resize: vertical;
}
input:focus,
textarea:focus {
  border-color: rgba(167, 139, 250, 0.5);
}
label.check {
  flex-direction: row;
  align-items: center;
  gap: 10px;
  font-size: 14px;
  color: var(--lumna-text);
}
.field {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  font-size: 14px;
}
.row {
  display: flex;
  align-items: center;
  gap: 10px;
  flex-wrap: wrap;
}
.status-line {
  color: var(--lumna-muted);
  font-size: 13px;
}
.primary {
  align-self: flex-start;
  padding: 12px 22px;
  border: 0;
  border-radius: 14px;
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
  color: #fff;
  font-size: 15px;
  font-weight: 600;
}
.primary.small {
  padding: 8px 16px;
  font-size: 14px;
}
.ghost {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  align-self: flex-start;
  padding: 9px 16px;
  border: 1px solid var(--line);
  border-radius: 12px;
  background: none;
  color: var(--lumna-text);
  font-size: 14px;
}
.ghost.danger {
  color: #fca5a5;
}
button:disabled {
  opacity: 0.5;
}
.error {
  margin: 0;
  color: #fca5a5;
  font-size: 14px;
}
.ok {
  margin: 0;
  color: #a7f3d0;
  font-size: 14px;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.chips button {
  padding: 8px 14px;
  border: 1px solid var(--line);
  border-radius: 999px;
  background: none;
  color: var(--lumna-text);
  font-size: 14px;
}
.chips.small button {
  padding: 5px 11px;
  font-size: 12px;
}
.chips button.on {
  border-color: transparent;
  background: var(--lumna-violet);
  color: #fff;
}
.segmented {
  flex: none;
  display: flex;
  padding: 3px;
  border-radius: 12px;
  background: var(--input);
}
.segmented button {
  padding: 6px 11px;
  border: 0;
  border-radius: 9px;
  background: none;
  color: var(--lumna-muted);
  font-size: 13px;
}
.segmented button.on {
  background: var(--lumna-violet);
  color: #fff;
}
.search {
  flex-direction: row;
  align-items: center;
  gap: 8px;
  margin: 14px 0 6px;
  padding: 0 12px;
  border: 1px solid var(--line);
  border-radius: 12px;
  background: var(--input);
}
.search input {
  flex: 1;
  border: 0;
  background: none;
  padding: 10px 0;
}
.list {
  margin: 0;
  padding: 0;
  list-style: none;
}
.list li {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 0;
  border-bottom: 1px solid var(--line);
}
.narrow .list li {
  flex-direction: column;
  align-items: stretch;
}
.info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.label {
  font-size: 15px;
}
.desc {
  color: var(--lumna-muted);
  font-size: 13px;
}
.offline {
  flex: none;
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 12px 14px 0;
  padding: 12px 14px;
  border: 1px solid rgba(252, 165, 165, 0.3);
  border-radius: 16px;
  background: rgba(127, 29, 29, 0.25);
  color: #fecaca;
}
.offline div {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.offline small {
  color: var(--lumna-muted);
  line-height: 1.4;
}
.offline .ghost {
  flex: none;
  align-self: center;
}
.problem {
  display: flex;
  flex-direction: column;
  gap: 8px;
  padding: 12px;
  border: 1px solid rgba(252, 165, 165, 0.25);
  border-radius: 14px;
}
/* conversa */
.chat {
  position: relative;
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
/* as conversas anteriores: por cima da conversa, abaixo da barra */
.conversations {
  position: absolute;
  inset: 62px 0 0;
  z-index: 2;
  overflow-y: auto;
  padding: 6px 14px 20px;
  background: var(--bg);
}
.conversations .new {
  display: flex;
  align-items: center;
  gap: 8px;
  width: 100%;
  height: 48px;
  margin-bottom: 8px;
  padding: 0 14px;
  border: 1px dashed rgba(167, 139, 250, 0.35);
  border-radius: 14px;
  background: none;
  color: var(--lumna-violet-soft);
  font-size: 15px;
}
.conversations ul {
  margin: 0;
  padding: 0;
  list-style: none;
}
.conversations li {
  display: flex;
  align-items: center;
  gap: 4px;
  border-radius: 14px;
}
.conversations li.current {
  background: rgba(167, 139, 250, 0.1);
}
.conversations .open {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  padding: 11px 12px;
  border: 0;
  background: none;
  color: inherit;
  text-align: left;
}
.conversations .title {
  max-width: 100%;
  overflow: hidden;
  font-size: 15px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.conversations small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.conversations .del {
  flex: none;
  display: grid;
  place-items: center;
  min-width: 44px;
  height: 44px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-muted);
  font-size: 13px;
}
.conversations .del.sure {
  padding: 0 10px;
  background: rgba(239, 68, 68, 0.16);
  color: #fca5a5;
}
.bar {
  flex: none;
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 14px 16px 8px 22px;
}
.bar h1 {
  flex: 1;
  margin: 0;
}
.icon {
  display: grid;
  place-items: center;
  width: 40px;
  height: 40px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-muted);
}
.icon.on {
  color: var(--lumna-violet-soft);
}
.messages {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 8px 18px 16px;
}
.empty {
  margin: 30px auto;
  max-width: 420px;
  color: var(--lumna-muted);
  text-align: center;
  line-height: 1.6;
}
.bubble {
  max-width: min(680px, 86%);
}
.bubble.user {
  align-self: flex-end;
  padding: 10px 14px;
  border-radius: 18px 18px 6px 18px;
  background: #241f33;
}
.bubble .text {
  margin: 0;
  white-space: pre-wrap;
  line-height: 1.5;
}
.tools {
  margin: 0 0 6px;
  padding: 0;
  list-style: none;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.tool-line {
  display: flex;
  align-items: center;
  gap: 8px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.dot {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
}
.tools .ok .dot {
  background: #6ee7b7;
}
.tools .error .dot,
.tools .denied .dot {
  background: #fca5a5;
}
.confirm {
  margin: 6px 0 4px;
  padding: 10px 12px;
  border: 1px solid var(--line);
  border-radius: 14px;
  background: var(--card);
}
.confirm p {
  margin: 0 0 8px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.typing {
  display: flex;
  gap: 5px;
  padding: 8px 0;
}
.typing span {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  animation: dot 1s infinite ease-in-out;
}
.typing span:nth-child(2) {
  animation-delay: 0.15s;
}
.typing span:nth-child(3) {
  animation-delay: 0.3s;
}
@keyframes dot {
  50% {
    opacity: 0.3;
  }
}
.composer {
  flex: none;
  display: flex;
  align-items: flex-end;
  gap: 8px;
  margin: 0 14px 12px;
  padding: 6px;
  border: 1px solid var(--line);
  border-radius: 24px;
  background: var(--input);
}
.composer textarea {
  flex: 1;
  height: 40px;
  min-height: 40px;
  max-height: 160px;
  padding: 9px 4px;
  border: 0;
  background: none;
  resize: none;
}
.mic,
.send {
  flex: none;
  display: grid;
  place-items: center;
  width: 40px;
  height: 40px;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-violet-soft);
}
.mic.on {
  background: var(--lumna-violet);
  color: #fff;
  box-shadow: 0 0 0 calc(var(--level, 0) * 12px + 2px) rgba(139, 92, 246, 0.28);
}
.send.stop {
  background: rgba(139, 92, 246, 0.25);
  color: var(--lumna-text);
}
.send:not(:disabled) {
  background: var(--lumna-violet);
  color: #fff;
}
</style>
