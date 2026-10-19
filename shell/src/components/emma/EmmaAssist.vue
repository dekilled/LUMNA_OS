<script setup lang="ts">
// Celular: o assistente da Emma, um cartão no rodapé por cima de qualquer tela (até do bloqueio).
// Abre segurando a barrinha, pelo "Pergunte à Emma" do Início e da central, ou pelo botão da Emma
// no bloqueio. Escreve ou fala (o microfone grava aqui e o backend transcreve); quando a pergunta
// veio falada, a Emma responde falando, frase por frase, enquanto a resposta chega.
// Com a tela bloqueada: conversa nova a cada vez, sem histórico, e só as ações seguras (emma.rs).
// Pedido rápido x conversa: uma ação que leva pra outro lugar ("abre o Spotify") fecha o popup e a
// próxima vez começa do zero; uma ação no lugar ("volume 30") também zera na próxima. Pergunta e
// resposta é conversa: continua ao abrir de novo, vai pro histórico do app e dá pra seguir lá.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { actionIcon, parseReply, plainText, sentenceEnd, speakable } from "./assist";
import {
  emmaAppOpen,
  emmaContinueInApp,
  emmaDecide,
  emmaListen,
  emmaListenStop,
  emmaNewConversation,
  emmaPermissions,
  emmaSend,
  emmaSpeak,
  emmaSpeakStop,
  emmaStatus,
  emmaStop,
  haptic,
  layerHide,
  on,
  type EmmaChannel,
  type EmmaEvent,
  type EmmaOpened,
  type EmmaTool,
  type EmmaVoice,
} from "../../system";

interface ToolView {
  id: string;
  name: string;
  label: string;
  state: "running" | "confirm" | "ok" | "error" | "denied";
  summary: string;
}

interface Turn {
  user: string;
  text: string;
  tools: ToolView[];
  /** A pergunta veio falada: a resposta também. */
  voice: boolean;
  done: boolean;
  /** Até onde a resposta já foi falada. */
  spoken: number;
}

const visible = ref(false);
const locked = ref(false);
const context = ref("");
const turns = ref<Turn[]>([]);
const draft = ref("");
const busy = ref(false);
const error = ref("");
const mic = ref<"idle" | "listening" | "transcribing">("idle");
const level = ref(0);
const speaking = ref(false);
const inset = ref(0);
const drag = ref(0);
const scroller = ref<HTMLElement>();
const input = ref<HTMLInputElement>();
const labels = ref(new Map<string, string>());
let lastUse = 0;
/** O último pedido foi uma ação rápida: a próxima abertura começa do zero. */
let fresh = false;

/** Ações que levam a pessoa pra outro lugar: feitas, o popup sai da frente. */
const OPENERS = ["apps.open", "apps.focus", "panel.open", "media.open_service", "media.play_video", "games.launch", "files.open"].map(
  (n) => `lumna_${n.replace(/\./g, "_")}`,
);
/** O microfone foi aberto aqui (o app da Emma também ouve pelo mesmo evento). */
let mine = false;

const channel = computed<EmmaChannel>(() => (locked.value ? "lock" : "assist"));
const contextLine = computed(() => (locked.value ? "Tela bloqueada • só ações seguras" : context.value));
const current = computed(() => turns.value.at(-1));
const reply = computed(() => parseReply(current.value?.text ?? ""));
/** Um pedido de permissão em aberto vira o cartão principal. */
const pending = computed(() => current.value?.tools.find((t) => t.state === "confirm"));
const greeting = computed(() =>
  locked.value
    ? ["O que está tocando?", "Aumenta o volume"]
    : context.value && context.value !== "Início"
      ? ["O que dá pra fazer aqui?", "Abaixa o volume"]
      : ["O que está tocando?", "Abre os Ajustes"],
);
const placeholder = computed(() =>
  mic.value === "listening" ? "Ouvindo…" : mic.value === "transcribing" ? "Entendendo…" : "Pergunte ou peça uma ação",
);

function toolView(tool: EmmaTool): ToolView {
  return { id: tool.id, name: tool.name, label: labels.value.get(tool.name) ?? tool.name.replace(/^lumna_/, "").replace(/_/g, " "), state: "running", summary: "" };
}

function findTool(id: string): ToolView | undefined {
  return current.value?.tools.find((t) => t.id === id);
}

async function scrollDown() {
  await nextTick();
  scroller.value?.scrollTo({ top: scroller.value.scrollHeight, behavior: "smooth" });
}

/** Fala as frases novas da resposta (todas, se ela terminou). */
function speakNew(turn: Turn) {
  if (!turn.voice) return;
  const body = parseReply(turn.text).body;
  const end = turn.done ? body.length : sentenceEnd(body, turn.spoken);
  if (end <= turn.spoken) return;
  const text = speakable(body.slice(turn.spoken, end));
  turn.spoken = end;
  if (text) emmaSpeak(text).catch((e) => (error.value = String(e)));
}

function onEvent(event: EmmaEvent) {
  if (event.channel !== "assist" && event.channel !== "lock") return;
  const turn = current.value;
  if (!turn) return;
  switch (event.type) {
    case "delta":
      turn.text += event.content;
      speakNew(turn);
      break;
    case "rewrite":
      turn.text = event.content;
      turn.spoken = Math.min(turn.spoken, parseReply(turn.text).body.length);
      break;
    case "tool_start":
      turn.tools.push(toolView(event.tool));
      break;
    case "tool_confirm": {
      const tool = findTool(event.tool.id) ?? (turn.tools.push(toolView(event.tool)), turn.tools.at(-1)!);
      tool.label = event.label || tool.label;
      // bloqueada, o shell já negou: não adianta mostrar o botão
      tool.state = locked.value ? "denied" : "confirm";
      if (!locked.value) haptic(20).catch(() => {});
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
    case "done":
      if (event.message?.content) turn.text = event.message.content;
      turn.done = true;
      speakNew(turn);
      break;
    case "error":
      error.value = event.message;
      break;
  }
  scrollDown();
}

async function send(text: string, voice = false) {
  text = text.trim();
  if (!text || busy.value) return;
  emmaSpeakStop().catch(() => {});
  draft.value = "";
  error.value = "";
  turns.value.push({ user: text, text: "", tools: [], voice, done: false, spoken: 0 });
  // só as últimas trocas ficam no cartão
  if (turns.value.length > 6) turns.value.splice(0, turns.value.length - 6);
  lastUse = Date.now();
  busy.value = true;
  scrollDown();
  try {
    await emmaSend(text, voice, channel.value, context.value);
  } catch (e) {
    error.value = String(e);
  } finally {
    busy.value = false;
    const turn = current.value;
    if (turn && !turn.done) {
      turn.done = true;
      speakNew(turn);
    }
    if (turn) settle(turn);
  }
}

/** Depois da resposta: pedido rápido ou conversa? */
function settle(turn: Turn) {
  // só as ações no LUMNA contam (uma busca na internet ainda é conversa)
  const ran = turn.tools.filter((t) => t.state === "ok" && t.name.startsWith("lumna_"));
  if (!ran.length || locked.value) return;
  if (ran.some((t) => OPENERS.includes(t.name))) {
    fresh = true;
    closeLater(turn);
  } else if (turns.value.length === 1) {
    fresh = true;
  }
}

/** Abriu algo: o popup sai da frente depois de dar tempo de ler a resposta (e de ela terminar de
 * falar). Tocar no cartão segura ele aberto. */
let autoClose = 0;
function closeLater(turn: Turn) {
  const read = Math.min(7000, Math.max(2500, 1800 + parseReply(turn.text).body.length * 55));
  const start = Date.now();
  window.clearInterval(autoClose);
  autoClose = window.setInterval(() => {
    if (!visible.value || current.value !== turn) return holdOpen();
    if (Date.now() - start < read || speaking.value) return;
    holdOpen();
    close(false);
  }, 250);
}
function holdOpen() {
  window.clearInterval(autoClose);
  autoClose = 0;
}

async function reset() {
  turns.value = [];
  fresh = false;
  await emmaNewConversation("assist").catch(() => {});
}

/** É conversa (tem pergunta e resposta sem ação): dá pra continuar no app. */
const conversation = computed(() => !locked.value && turns.value.some((t) => t.done && t.text && !t.tools.some((x) => x.name.startsWith("lumna_"))));

async function continueInApp() {
  try {
    await emmaContinueInApp();
  } catch (e) {
    error.value = String(e);
    return;
  }
  turns.value = [];
  fresh = false;
  close();
  emmaAppOpen("conversa").catch(() => {});
}

function submit() {
  send(draft.value);
}

/** Parar: ela cala, o microfone fecha e a resposta que estava chegando para onde está. */
function stop() {
  emmaStop().catch(() => {});
  speaking.value = false;
  if (mic.value !== "idle") {
    mic.value = "idle";
    mine = false;
  }
}

/** Erro que se resolve no app da Emma (sem provedor, sem login, servidor fora): vira um botão. */
const fix = computed(() => {
  const e = error.value;
  if (/provider|provedor|modelo/i.test(e)) return { label: "Escolher o provedor", tab: "provedores" };
  if (/login|conta|entrar|não respondeu|fora do ar/i.test(e)) return { label: "Abrir o app da Emma", tab: "conversa" };
  return null;
});

/** O botão da direita vira "parar" enquanto ela responde ou fala (e o campo está vazio). */
const canStop = computed(() => !draft.value.trim() && (busy.value || speaking.value));

async function decide(tool: ToolView, allow: boolean) {
  tool.state = allow ? "running" : "denied";
  try {
    await emmaDecide(tool.id, allow);
  } catch (e) {
    error.value = String(e);
  }
}

async function toggleMic() {
  if (mic.value !== "idle") {
    await emmaListenStop(false);
    return;
  }
  error.value = "";
  input.value?.blur();
  haptic(15).catch(() => {});
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
  if (!visible.value) return;
  if (!mine && ["listening", "transcribing", "heard", "idle"].includes(v.state)) return;
  if (["heard", "idle"].includes(v.state) || (v.state === "error" && mic.value !== "idle")) mine = false;
  switch (v.state) {
    case "listening":
      mic.value = "listening";
      level.value = v.level;
      break;
    case "transcribing":
      mic.value = "transcribing";
      level.value = 0;
      break;
    case "heard":
      mic.value = "idle";
      send(v.text, true);
      break;
    case "idle":
      mic.value = "idle";
      level.value = 0;
      break;
    case "speaking":
      speaking.value = true;
      break;
    case "spoken":
      speaking.value = false;
      break;
    case "error":
      mic.value = "idle";
      speaking.value = false;
      error.value = v.message;
      break;
  }
}

async function opened(info: EmmaOpened) {
  const wasLocked = locked.value;
  locked.value = !!info?.locked;
  context.value = info?.context ?? "";
  error.value = "";
  drag.value = 0;
  // bloqueado: começa do zero (nada do que foi dito antes aparece); desbloqueado, a conversa do
  // popup vale por 10 min parada, como no backend
  if (locked.value || wasLocked || fresh || Date.now() - lastUse > 10 * 60 * 1000) {
    if (locked.value) {
      turns.value = [];
      await emmaNewConversation("lock").catch(() => {});
    } else {
      await reset();
    }
  }
  visible.value = true;
  // o servidor fora do ar aparece já ao abrir (não só depois de perguntar)
  emmaStatus()
    .then((st) => {
      if (!st.online) error.value = `A Emma não respondeu em ${st.url}: o serviço dela (lumna-emma) está rodando?`;
      else if (!st.configured) error.value = "Falta entrar na conta da Emma.";
    })
    .catch(() => {});
  if (!labels.value.size) {
    emmaPermissions()
      .then((list) => (labels.value = new Map(list.map((a) => [`lumna_${a.name.replace(/\./g, "_")}`, a.label]))))
      .catch(() => {});
  }
  scrollDown();
}

function close(quiet = true) {
  if (!visible.value) return;
  visible.value = false;
  if (mic.value !== "idle") emmaListenStop(true).catch(() => {});
  if (quiet) emmaSpeakStop().catch(() => {});
  input.value?.blur();
  window.setTimeout(() => !visible.value && layerHide(), 200);
}

// arrastar a alça pra baixo fecha
let dragStart: { y: number; id: number } | null = null;
function dragDown(e: PointerEvent) {
  dragStart = { y: e.clientY, id: e.pointerId };
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}
function dragMove(e: PointerEvent) {
  if (dragStart?.id === e.pointerId) drag.value = Math.max(0, e.clientY - dragStart.y);
}
function dragUp() {
  if (!dragStart) return;
  dragStart = null;
  if (drag.value > 90) close();
  else drag.value = 0;
}

function onKey(event: KeyboardEvent) {
  if (visible.value && event.key === "Escape") close();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on<EmmaOpened>("lumna://emma-opened", opened));
  stops.push(await on<EmmaEvent>("lumna://emma", onEvent));
  stops.push(await on<EmmaVoice>("lumna://emma-voice", onVoice));
  // o teclado do LUMNA fica por cima da camada: o cartão sobe junto (a camada já termina na faixa
  // da barrinha, 26 px acima da borda)
  stops.push(
    await on<{ shown: boolean; lumna: boolean; height: number }>("lumna://keyboard", (k) => {
      inset.value = k?.shown && k.lumna ? Math.max(0, (k.height ?? 404) - 26) : 0;
      if (inset.value) scrollDown();
    }),
  );
  // desbloqueou (ou bloqueou) com o assistente aberto: a conversa do bloqueio não continua
  stops.push(
    await on<boolean>("lumna://lock", (isLocked) => {
      if (visible.value && isLocked !== locked.value) close();
    }),
  );
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="assist" :class="{ open: visible }" @pointerdown.self="close()">
    <section
      class="card"
      @pointerdown="holdOpen()"
      :style="{ transform: visible ? `translateY(${drag}px)` : undefined, marginBottom: `${inset}px` }"
      role="dialog"
      aria-label="Emma"
    >
      <div class="grab" @pointerdown="dragDown" @pointermove="dragMove" @pointerup="dragUp" @pointercancel="dragUp">
        <span />
      </div>

      <header>
        <svg class="gem" viewBox="0 0 24 24" aria-hidden="true">
          <defs>
            <linearGradient id="emma-gem" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0" stop-color="#c4b5fd" />
              <stop offset="1" stop-color="#7c3aed" />
            </linearGradient>
          </defs>
          <path d="M12 1.5 21.5 12 12 22.5 2.5 12z" fill="url(#emma-gem)" />
          <path d="M12 1.5 15.5 12 12 22.5 8.5 12zM2.5 12h19" fill="none" stroke="rgba(255,255,255,0.35)" stroke-width="0.8" />
        </svg>
        <span class="name">Emma</span>
        <button v-if="speaking" class="state" @click="stop()"><Icon name="stop" :size="14" /> Parar de falar</button>
        <button v-if="conversation" class="state expand" title="Continuar no app da Emma" @click="continueInApp()">
          <Icon name="chat" :size="14" /> Continuar no app
        </button>
        <button class="x" aria-label="Fechar" @click="close()"><Icon name="close" :size="22" /></button>
      </header>
      <p v-if="contextLine" class="context">{{ contextLine }}</p>

      <div ref="scroller" class="talk">
        <template v-if="turns.length === 0">
          <p class="answer">Oi! Como posso ajudar?</p>
          <div class="divider" />
          <button v-for="s in greeting" :key="s" class="chip" @click="send(s)">
            <Icon name="chat" :size="18" /> {{ s }}
          </button>
        </template>

        <template v-for="(turn, i) in turns" :key="i">
          <div class="you">
            <p>{{ turn.user }}</p>
          </div>
          <template v-if="turn !== current">
            <p class="answer old">{{ plainText(parseReply(turn.text).body) }}</p>
          </template>
        </template>

        <template v-if="current">
          <p v-if="reply.body" class="answer">{{ plainText(reply.body) }}</p>
          <p v-else-if="!current.done" class="answer thinking"><span /><span /><span /></p>

          <div v-if="current.tools.some((t) => t.state !== 'confirm')" class="tools">
            <span v-for="t in current.tools.filter((t) => t.state !== 'confirm')" :key="t.id" class="tool" :class="t.state">
              <Icon :name="t.state === 'ok' ? 'check' : t.state === 'running' ? 'refresh' : 'close'" :size="13" />
              {{ t.summary || t.label }}
            </span>
          </div>

          <!-- permissão em aberto: o cartão principal pede, o link nega -->
          <template v-if="pending">
            <button class="primary" @click="decide(pending, true)">
              <Icon name="check" :size="24" />
              <span>Permitir: {{ pending.label }}</span>
              <Icon name="arrow-right" :size="22" />
            </button>
            <button class="link" @click="decide(pending, false)"><Icon name="close" :size="20" /> Agora não</button>
          </template>

          <template v-else-if="current.done">
            <button v-if="reply.primary" class="primary" :disabled="busy" @click="send(reply.primary)">
              <Icon :name="actionIcon(reply.primary, true)" :size="24" />
              <span>{{ reply.primary }}</span>
              <Icon name="arrow-right" :size="22" />
            </button>
            <button v-for="l in reply.links" :key="l" class="link" :disabled="busy" @click="send(l)">
              <Icon :name="actionIcon(l)" :size="20" /> {{ l }}
            </button>
            <template v-if="reply.suggestions.length">
              <div class="divider" />
              <button v-for="s in reply.suggestions" :key="s" class="chip" :disabled="busy" @click="send(s)">
                <Icon name="chat" :size="18" /> {{ s }}
              </button>
            </template>
          </template>
        </template>

        <p v-if="error" class="error">{{ error }}</p>
        <button v-if="fix && !locked" class="primary" @click="emmaAppOpen(fix.tab)">
          <Icon name="settings" :size="22" />
          <span>{{ fix.label }}</span>
          <Icon name="arrow-right" :size="22" />
        </button>
      </div>

      <form class="pill" :class="{ listening: mic !== 'idle' }" @submit.prevent="submit()">
        <button
          type="button"
          class="mic"
          :class="{ on: mic === 'listening' }"
          :style="{ '--level': level }"
          :aria-label="mic === 'idle' ? 'Falar' : 'Parar de ouvir'"
          @click="toggleMic()"
        >
          <Icon name="mic" :size="22" />
        </button>
        <input
          ref="input"
          v-model="draft"
          :placeholder="placeholder"
          :disabled="mic !== 'idle'"
          enterkeyhint="send"
          autocomplete="off"
        />
        <button v-if="canStop" type="button" class="go stop" aria-label="Parar" @click="stop()">
          <Icon name="stop" :size="18" />
        </button>
        <button v-else type="submit" class="go" :disabled="!draft.trim() || busy" aria-label="Enviar">
          <Icon name="arrow-right" :size="22" />
        </button>
      </form>
    </section>
  </div>
</template>

<style scoped>
.assist {
  position: fixed;
  inset: 0;
  display: flex;
  flex-direction: column;
  justify-content: flex-end;
  padding: 0 8px 8px;
  background: rgba(4, 3, 10, 0);
  transition: background 200ms ease;
  pointer-events: none;
}
.assist.open {
  background: rgba(4, 3, 10, 0.45);
  pointer-events: auto;
}
.card {
  display: flex;
  flex-direction: column;
  max-height: calc(100vh - 24px);
  padding: 0 18px 14px;
  border: 1px solid rgba(167, 139, 250, 0.16);
  border-radius: 28px;
  background: #13101d;
  box-shadow: 0 -12px 40px rgba(0, 0, 0, 0.45);
  transform: translateY(110%);
  transition:
    transform 220ms cubic-bezier(0.2, 0.9, 0.3, 1),
    margin-bottom 160ms ease;
}
.assist.open .card {
  transform: translateY(0);
}
.grab {
  flex: none;
  display: flex;
  justify-content: center;
  padding: 10px 0 8px;
  touch-action: none;
}
.grab span {
  width: 40px;
  height: 4px;
  border-radius: 2px;
  background: rgba(236, 232, 255, 0.28);
}
header {
  flex: none;
  display: flex;
  align-items: center;
  gap: 10px;
}
.gem {
  width: 22px;
  height: 22px;
  filter: drop-shadow(0 0 6px rgba(139, 92, 246, 0.55));
}
.name {
  font-size: 15px;
  font-weight: 700;
  letter-spacing: 0.2em;
}
.state {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 5px 11px;
  border: 0;
  border-radius: 999px;
  background: rgba(139, 92, 246, 0.18);
  color: var(--lumna-violet-soft);
  font-size: 13px;
}
.expand {
  margin-left: auto;
}
.expand + .x {
  margin-left: 0;
}
.x {
  margin-left: auto;
  display: grid;
  place-items: center;
  width: 36px;
  height: 36px;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.context {
  flex: none;
  margin: 6px 0 4px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.talk {
  flex: 1 1 auto;
  min-height: 0;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 10px 0 14px;
}
.you {
  display: flex;
  justify-content: flex-end;
}
.you p {
  max-width: 82%;
  margin: 0;
  padding: 10px 14px;
  border-radius: 18px 18px 6px 18px;
  background: #241f33;
  font-size: 15px;
  line-height: 1.4;
}
.answer {
  margin: 0;
  font-size: 16px;
  line-height: 1.5;
  white-space: pre-wrap;
}
.answer.old {
  color: var(--lumna-muted);
  font-size: 14px;
}
.thinking {
  display: flex;
  gap: 5px;
  padding: 6px 0;
}
.thinking span {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  animation: dot 1s infinite ease-in-out;
}
.thinking span:nth-child(2) {
  animation-delay: 0.15s;
}
.thinking span:nth-child(3) {
  animation-delay: 0.3s;
}
@keyframes dot {
  0%,
  100% {
    opacity: 0.25;
    transform: translateY(0);
  }
  50% {
    opacity: 1;
    transform: translateY(-3px);
  }
}
.tools {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.tool {
  display: inline-flex;
  align-items: center;
  gap: 5px;
  padding: 4px 9px;
  border-radius: 999px;
  background: rgba(167, 139, 250, 0.1);
  color: var(--lumna-muted);
  font-size: 12px;
}
.tool.ok {
  color: #a7f3d0;
}
.tool.error,
.tool.denied {
  color: #fca5a5;
}
.primary {
  display: flex;
  align-items: center;
  gap: 12px;
  width: 100%;
  min-height: 58px;
  padding: 0 18px;
  border: 0;
  border-radius: 18px;
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
  color: #fff;
  font-size: 16px;
  font-weight: 600;
  text-align: left;
}
.primary span {
  flex: 1;
}
.link {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 4px;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font-size: 15px;
  text-align: left;
}
.divider {
  height: 1px;
  margin: 2px 0;
  background: rgba(167, 139, 250, 0.14);
}
.chip {
  align-self: flex-start;
  display: inline-flex;
  align-items: center;
  gap: 9px;
  padding: 9px 14px;
  border: 1px solid rgba(167, 139, 250, 0.22);
  border-radius: 999px;
  background: rgba(167, 139, 250, 0.06);
  color: var(--lumna-text);
  font-size: 14px;
}
button:disabled {
  opacity: 0.55;
}
.error {
  margin: 0;
  color: #fca5a5;
  font-size: 14px;
}
.pill {
  flex: none;
  display: flex;
  align-items: center;
  gap: 6px;
  height: 54px;
  padding: 0 6px;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 999px;
  background: #1c1828;
}
.pill.listening {
  border-color: rgba(167, 139, 250, 0.55);
}
.pill input {
  flex: 1;
  min-width: 0;
  height: 100%;
  border: 0;
  outline: 0;
  background: none;
  color: var(--lumna-text);
  font-size: 15px;
}
.pill input::placeholder {
  color: var(--lumna-muted);
}
.mic,
.go {
  flex: none;
  display: grid;
  place-items: center;
  width: 42px;
  height: 42px;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-violet-soft);
}
.mic.on {
  background: var(--lumna-violet);
  color: #fff;
  box-shadow: 0 0 0 calc(var(--level, 0) * 14px + 3px) rgba(139, 92, 246, 0.28);
  transition: box-shadow 90ms linear;
}
.go.stop {
  background: rgba(139, 92, 246, 0.25);
  color: var(--lumna-text);
}
.go:not(:disabled) {
  background: var(--lumna-violet);
  color: #fff;
}
</style>
