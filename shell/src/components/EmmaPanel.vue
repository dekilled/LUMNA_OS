<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import {
  emmaDecide,
  emmaHistory,
  emmaLogin,
  emmaLogout,
  emmaNewConversation,
  emmaPermissions,
  emmaProviders,
  emmaSetProvider,
  emmaSend,
  emmaSetPermission,
  emmaStatus,
  automationOverview,
  automationRunMacro,
  automationSetEnabled,
  layerHide,
  on,
  type ActionInfo,
  type Automation,
  type AutomationRun,
  type Macro,
  type MacroStep,
  type ProviderPreset,
  type ActionPermission,
  type EmmaEvent,
  type EmmaStatus,
  type EmmaTool,
} from "../system";

interface ToolView {
  id: string;
  name: string;
  label: string;
  /** running → (confirm) → ok | error | denied */
  state: "running" | "confirm" | "ok" | "error" | "denied";
  summary: string;
  reason?: string;
  /** Os argumentos, mostrados na confirmação ("o que exatamente vai rodar"). */
  args?: Record<string, unknown>;
}

interface Bubble {
  role: "user" | "assistant";
  text: string;
  tools: ToolView[];
}

const visible = ref(false);
const view = ref<"chat" | "login" | "permissions" | "automations" | "provider">("chat");
const status = ref<EmmaStatus | null>(null);
const bubbles = ref<Bubble[]>([]);
const draft = ref("");
const busy = ref(false);
const error = ref("");
const actions = ref<ActionInfo[]>([]);
const scroller = ref<HTMLElement>();
const input = ref<HTMLTextAreaElement>();

const login = ref({ url: "http://127.0.0.1:8001", email: "", password: "", busy: false, error: "" });

/** `lumna_apps_open` → "Abrir programa". */
const labels = computed(() => {
  const map = new Map<string, string>();
  for (const a of actions.value) map.set(`lumna_${a.name.replace(/\./g, "_")}`, a.label);
  return map;
});

/** Ações que levam a pessoa para outra janela: com elas feitas, o painel sai da frente. */
const MOVES_ON = new Set([
  "lumna_apps_open",
  "lumna_apps_focus",
  "lumna_games_launch",
  "lumna_media_open_service",
  "lumna_media_play_video",
  "lumna_files_open",
  "lumna_panel_open",
  "lumna_mode_switch",
]);

const presets = ref<ProviderPreset[]>([]);
const provider = ref({ current: "", preset: "ollama", baseUrl: "", apiKey: "", model: "", busy: false, error: "" });
const preset = computed(() => presets.value.find((p) => p.preset === provider.value.preset));

function pickPreset(id: string) {
  provider.value.preset = id;
  const p = presets.value.find((x) => x.preset === id);
  provider.value.baseUrl = p?.base_url ?? "";
  provider.value.model = p?.models[0] ?? "";
}

async function openProvider() {
  view.value = "provider";
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
  try {
    await emmaSetProvider(preset.value?.name ?? "Provedor", p.baseUrl, p.apiKey, p.model);
    p.apiKey = "";
    error.value = "";
    view.value = "chat";
  } catch (e) {
    p.error = String(e);
  } finally {
    p.busy = false;
  }
}

const macros = ref<Macro[]>([]);
const automations = ref<Automation[]>([]);
const runs = ref<AutomationRun[]>([]);
const running = ref("");

async function refreshAutomations() {
  const overview = await automationOverview();
  macros.value = overview.macros;
  automations.value = overview.automations;
  runs.value = overview.history;
}

async function openAutomations() {
  view.value = view.value === "automations" ? "chat" : "automations";
  if (view.value === "automations") await refreshAutomations();
}

async function runMacro(macro: Macro) {
  running.value = macro.name;
  try {
    const outcome = await automationRunMacro(macro.name);
    if (!outcome.ok) error.value = outcome.summary;
  } finally {
    running.value = "";
    await refreshAutomations();
  }
}

async function toggleAutomation(automation: Automation) {
  await automationSetEnabled(automation.id, !automation.enabled);
  await refreshAutomations();
}

/** "Volume", "Pedir à Emma: luzes em 20%", "Esperar 2 s". */
function stepText(step: MacroStep): string {
  if ("emma" in step) return `Pedir à Emma: ${step.emma}`;
  if ("wait" in step) return `Esperar ${step.wait} s`;
  const label = actions.value.find((a) => a.name === step.action)?.label ?? step.action;
  const args = Object.values(step.args ?? {}).join(", ");
  return args ? `${label}: ${args}` : label;
}

/** Argumentos legíveis para a confirmação. */
function argsText(args?: Record<string, unknown>): string {
  if (!args || Object.keys(args).length === 0) return "";
  return JSON.stringify(args, null, 1).slice(0, 600);
}

/** Para instalar: de onde vem o programa e o que isso quer dizer. */
function sourceNote(tool: { name: string; args?: Record<string, unknown> }): { text: string; level: string } | null {
  if (tool.name !== "store.install") return null;
  switch (tool.args?.source) {
    case "flathub":
      return { text: "Flathub: roda isolado do sistema. Alguns apps pedem acesso às suas pastas.", level: "safe" };
    case "arch":
      return { text: "Repositório do Arch: revisado pela equipe do Arch e instalado no sistema.", level: "system" };
    case "aur":
      return { text: "AUR: código da comunidade, sem revisão. A Emma não instala daqui.", level: "risk" };
    default:
      return null;
  }
}

const PERMISSION_LABEL: Record<ActionPermission, string> = { auto: "Sozinha", ask: "Pergunta", off: "Desligada" };

function toolView(tool: EmmaTool): ToolView {
  return {
    id: tool.id,
    name: tool.name,
    label: labels.value.get(tool.name) ?? tool.name,
    state: "running",
    summary: "",
    args: tool.args,
  };
}

const last = () => bubbles.value[bubbles.value.length - 1];

function assistant(): Bubble {
  const bubble = last();
  if (bubble?.role === "assistant") return bubble;
  bubbles.value.push({ role: "assistant", text: "", tools: [] });
  return last();
}

function findTool(id: string): ToolView | undefined {
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

let movedOn = false;

function onEvent(event: EmmaEvent) {
  switch (event.type) {
    case "delta":
      assistant().text += event.content;
      break;
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
    case "tool_client": {
      const tool = findTool(event.tool.id);
      if (tool) tool.state = "running";
      break;
    }
    case "tool_ran": {
      const tool = findTool(event.id);
      if (tool && event.ok && MOVES_ON.has(tool.name)) movedOn = true;
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
      if (event.message?.content) assistant().text = event.message.content;
      break;
    case "error":
      error.value = event.message;
      break;
  }
  scrollDown();
}

async function send() {
  const text = draft.value.trim();
  if (!text || busy.value) return;
  draft.value = "";
  error.value = "";
  bubbles.value.push({ role: "user", text, tools: [] });
  scrollDown();
  busy.value = true;
  movedOn = false;
  try {
    await emmaSend(text);
  } catch (e) {
    error.value = String(e);
    if (String(e).includes("login")) view.value = "login";
  } finally {
    // Sem provedor de IA ainda (primeiro uso): a tela de escolher aparece sozinha.
    if (error.value.includes("Nenhum provider")) await openProvider();
    busy.value = false;
    input.value?.focus();
  }
  if (movedOn) window.setTimeout(() => !busy.value && close(), 900);
}

async function decide(tool: ToolView, allow: boolean) {
  tool.state = allow ? "running" : "denied";
  try {
    await emmaDecide(tool.id, allow);
  } catch (e) {
    error.value = String(e);
  }
}

function onKeydown(event: KeyboardEvent) {
  if (event.key === "Enter" && !event.shiftKey) {
    event.preventDefault();
    send();
  }
}

async function newConversation() {
  await emmaNewConversation();
  bubbles.value = [];
  error.value = "";
  input.value?.focus();
}

async function refreshStatus() {
  status.value = await emmaStatus();
  if (status.value.url) login.value.url = status.value.url;
  if (status.value.email) login.value.email = status.value.email;
  if (!status.value.configured) view.value = "login";
}

async function submitLogin() {
  login.value.busy = true;
  login.value.error = "";
  try {
    await emmaLogin(login.value.url, login.value.email, login.value.password);
    login.value.password = "";
    view.value = "chat";
    await refreshStatus();
    await nextTick();
    input.value?.focus();
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
  view.value = "login";
}

async function setPermission(action: ActionInfo, permission: ActionPermission) {
  await emmaSetPermission(action.name, permission);
  actions.value = await emmaPermissions();
}

async function opened() {
  visible.value = true;
  view.value = "chat";
  actions.value = await emmaPermissions();
  await refreshStatus();
  if (status.value?.configured && bubbles.value.length === 0) {
    const history = await emmaHistory();
    bubbles.value = history.map((m) => ({
      role: m.role,
      text: m.content,
      tools: (m.tools ?? []).map((t) => ({ ...toolView(t), state: t.status === "ok" ? "ok" : "error", summary: t.summary }) as ToolView),
    }));
  }
  await nextTick();
  input.value?.focus();
  scrollDown();
}

function close() {
  visible.value = false;
  window.setTimeout(() => !visible.value && layerHide(), 180);
}

function onKey(event: KeyboardEvent) {
  if (visible.value && event.key === "Escape") close();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://emma-opened", opened));
  stops.push(await on<EmmaEvent>("lumna://emma", onEvent));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="veil" @click.self="close()">
    <section class="panel" :class="{ shown: visible }">
      <header>
        <span class="mark"><Icon name="sparkle" :size="18" /></span>
        <div class="title">
          <strong>Emma</strong>
          <small :class="{ online: status?.online }">{{ status?.online ? "Pronta" : "Fora do ar" }}</small>
        </div>
        <button v-if="view === 'chat'" class="icon" title="Nova conversa" @click="newConversation()">
          <Icon name="plus" :size="18" />
        </button>
        <button
          class="icon"
          :class="{ active: view === 'automations' }"
          title="Macros e automações"
          @click="openAutomations()"
        >
          <Icon name="zap" :size="18" />
        </button>
        <button
          class="icon"
          :class="{ active: view === 'permissions' }"
          title="O que a Emma pode fazer"
          @click="view = view === 'permissions' ? 'chat' : 'permissions'"
        >
          <Icon name="settings" :size="18" />
        </button>
        <button class="icon" title="Fechar (Esc)" @click="close()"><Icon name="close" :size="18" /></button>
      </header>

      <!-- Conversa -->
      <template v-if="view === 'chat'">
        <div ref="scroller" class="messages">
          <p v-if="bubbles.length === 0" class="empty">
            Peça qualquer coisa: <em>“abre o Spotify”</em>, <em>“modo filme”</em>, <em>“volume 30”</em>,
            <em>“acha o PDF do contrato”</em>.
          </p>
          <div v-for="(bubble, i) in bubbles" :key="i" class="bubble" :class="bubble.role">
            <ul v-if="bubble.tools.length" class="tools">
              <li v-for="tool in bubble.tools" :key="tool.id" :class="tool.state">
                <div class="tool-line">
                  <span class="state" />
                  <span class="label">{{ tool.label }}</span>
                  <span v-if="tool.summary" class="summary">{{ tool.summary }}</span>
                </div>
                <div v-if="tool.state === 'confirm'" class="confirm">
                  <p>{{ tool.reason }}</p>
                  <pre v-if="argsText(tool.args)">{{ argsText(tool.args) }}</pre>
                  <p v-if="sourceNote(tool)" class="source" :class="sourceNote(tool)!.level">{{ sourceNote(tool)!.text }}</p>
                  <p v-if="tool.name === 'store.install'" class="source">Depois de permitir, a senha de administrador é pedida.</p>
                  <div class="confirm-actions">
                    <button class="deny" @click="decide(tool, false)">Não</button>
                    <button class="allow" @click="decide(tool, true)">Permitir</button>
                  </div>
                </div>
              </li>
            </ul>
            <p v-if="bubble.text" class="text">{{ bubble.text }}</p>
          </div>
          <div v-if="busy && last()?.role === 'user'" class="bubble assistant typing"><span /><span /><span /></div>
          <p v-if="error" class="error">{{ error }}</p>
        </div>
        <footer>
          <textarea
            ref="input"
            v-model="draft"
            rows="1"
            placeholder="Fale com a Emma…"
            :disabled="!status?.configured"
            @keydown="onKeydown"
          />
          <button class="send" :disabled="busy || !draft.trim()" title="Enviar (Enter)" @click="send()">
            <Icon name="send" :size="18" />
          </button>
        </footer>
      </template>

      <!-- Entrar -->
      <form v-else-if="view === 'login'" class="login" @submit.prevent="submitLogin()">
        <h2>Conectar à Emma</h2>
        <p>Entre com a conta do servidor da Emma. Com o pacote <code>lumna-emma</code> isso é automático.</p>
        <label>Endereço<input v-model="login.url" spellcheck="false" /></label>
        <label>E-mail<input v-model="login.email" type="email" autocomplete="username" /></label>
        <label>Senha<input v-model="login.password" type="password" autocomplete="current-password" /></label>
        <p v-if="login.error" class="error">{{ login.error }}</p>
        <button class="primary" :disabled="login.busy || !login.email || !login.password">
          {{ login.busy ? "Entrando…" : "Entrar" }}
        </button>
      </form>

      <!-- Provedor de IA (primeiro uso) -->
      <form v-else-if="view === 'provider'" class="login" @submit.prevent="saveProvider()">
        <h2>Cérebro da Emma</h2>
        <p v-if="provider.current">Em uso: <strong>{{ provider.current }}</strong>. Trocar:</p>
        <p v-else>Escolha o serviço de IA que a Emma usa para pensar. A chave fica guardada só neste computador.</p>
        <div class="chips">
          <button
            v-for="p in presets"
            :key="p.preset"
            type="button"
            :class="{ on: provider.preset === p.preset }"
            @click="pickPreset(p.preset)"
          >
            {{ p.name }}
          </button>
        </div>
        <label v-if="provider.preset === 'custom'">Endereço<input v-model="provider.baseUrl" spellcheck="false" /></label>
        <label>
          Modelo
          <input v-model="provider.model" spellcheck="false" />
        </label>
        <!-- Sugestões em chips: um <datalist> abriria um popup nativo, que derruba as camadas do layer-shell. -->
        <div v-if="preset?.models.length" class="chips small">
          <button
            v-for="m in preset.models"
            :key="m"
            type="button"
            :class="{ on: provider.model === m }"
            @click="provider.model = m"
          >
            {{ m }}
          </button>
        </div>
        <label>Chave da API<input v-model="provider.apiKey" type="password" autocomplete="off" /></label>
        <p v-if="provider.error" class="error">{{ provider.error }}</p>
        <button class="primary" :disabled="provider.busy || !provider.model">
          {{ provider.busy ? "Salvando…" : "Usar este" }}
        </button>
      </form>

      <!-- Macros e automações -->
      <div v-else-if="view === 'automations'" class="automations">
        <p class="hint">
          Peça à Emma: <em>“cria uma macro hora do filme: modo mídia, volume 40 e abre a Netflix”</em> ou
          <em>“todo dia útil às 8h roda a macro trabalho”</em>.
        </p>
        <h3>Macros</h3>
        <p v-if="macros.length === 0" class="none">Nenhuma macro ainda.</p>
        <ul>
          <li v-for="macro in macros" :key="macro.name" class="macro">
            <div class="info">
              <span class="label">{{ macro.name }}</span>
              <span class="desc">{{ macro.steps.map(stepText).join(" → ") }}</span>
            </div>
            <button class="run" :disabled="!!running" :title="`Rodar ${macro.name}`" @click="runMacro(macro)">
              <Icon :name="running === macro.name ? 'refresh' : 'play'" :size="18" />
            </button>
          </li>
        </ul>
        <h3>Automações</h3>
        <p v-if="automations.length === 0" class="none">Nenhuma automação ainda.</p>
        <ul>
          <li v-for="automation in automations" :key="automation.id">
            <div class="info">
              <span class="label">{{ automation.name }}</span>
              <span class="desc">{{ automation.description }} → {{ automation.macro }}</span>
            </div>
            <button
              class="toggle"
              :class="{ on: automation.enabled }"
              role="switch"
              :aria-checked="automation.enabled"
              @click="toggleAutomation(automation)"
            >
              <span />
            </button>
          </li>
        </ul>
        <h3 v-if="runs.length">Últimas execuções</h3>
        <ul class="runs">
          <li v-for="(run, i) in runs" :key="i" :class="{ failed: !run.ok }">
            <span class="when">{{ run.at.slice(5, 16) }}</span>
            <span class="what">{{ run.summary }}</span>
            <span class="by">{{ run.by }}</span>
          </li>
        </ul>
      </div>

      <!-- O que a Emma pode fazer -->
      <div v-else class="permissions">
        <p class="hint">
          <strong>Sozinha</strong>: faz direto. <strong>Pergunta</strong>: mostra aqui antes. Desligar, instalar e
          comandos sempre perguntam.
        </p>
        <ul>
          <li v-for="action in actions" :key="action.name">
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
        <button v-if="status?.configured" class="link" @click="openProvider()">Provedor de IA…</button>
        <button v-if="status?.configured" class="logout" @click="logout()">Sair da conta da Emma</button>
      </div>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
}

/* Grafite com o acento violeta: a linguagem do visual novo (docs/VISUAL.md). */
.panel {
  --bg: #1e1f23;
  --card: #222327;
  --input: #25262a;
  --line: rgba(255, 255, 255, 0.07);
  --muted: #9a9aa5;
  position: absolute;
  top: 48px;
  right: 12px;
  bottom: 12px;
  width: 420px;
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
  background: var(--bg);
  border: 1px solid var(--line);
  border-radius: 16px;
  box-shadow: 0 24px 60px rgba(0, 0, 0, 0.5);
  color: #ececf1;
  overflow: hidden;
  opacity: 0;
  transform: translateX(14px);
  transition: opacity 160ms ease, transform 240ms cubic-bezier(0.22, 1, 0.36, 1);
}
.panel.shown {
  opacity: 1;
  transform: none;
}

header {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 14px 14px 12px 16px;
  border-bottom: 1px solid var(--line);
}
.mark {
  display: grid;
  place-items: center;
  width: 34px;
  height: 34px;
  border-radius: 11px;
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
  color: white;
}
.title {
  display: flex;
  flex-direction: column;
  flex: 1;
  line-height: 1.2;
}
.title strong {
  font-size: 15px;
}
.title small {
  font-size: 12px;
  color: var(--muted);
}
.title small::before {
  content: "";
  display: inline-block;
  width: 6px;
  height: 6px;
  margin-right: 6px;
  border-radius: 3px;
  background: #f87171;
  vertical-align: 1px;
}
.title small.online::before {
  background: #22c55e;
}
.icon {
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  border: 0;
  border-radius: 9px;
  background: transparent;
  color: var(--muted);
}
.icon:hover,
.icon.active {
  background: var(--input);
  color: #ececf1;
}

.messages {
  flex: 1;
  overflow-y: auto;
  padding: 16px;
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.empty {
  margin: auto 8px;
  color: var(--muted);
  font-size: 14px;
  line-height: 1.6;
  text-align: center;
}
.empty em {
  color: #a78bfa;
  font-style: normal;
}

.bubble {
  max-width: 88%;
  font-size: 14px;
  line-height: 1.5;
}
.bubble.user {
  align-self: flex-end;
  padding: 9px 13px;
  border-radius: 14px 14px 4px 14px;
  background: #7d46d7;
}
.bubble.assistant {
  align-self: flex-start;
}
.bubble .text {
  margin: 0;
  white-space: pre-wrap;
  word-break: break-word;
}
.bubble.assistant .text {
  padding: 9px 13px;
  border-radius: 14px 14px 14px 4px;
  background: var(--card);
}

.tools {
  list-style: none;
  margin: 0 0 6px;
  padding: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.tools li {
  padding: 7px 10px;
  border: 1px solid var(--line);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.02);
  font-size: 13px;
}
.tool-line {
  display: flex;
  align-items: baseline;
  gap: 8px;
}
.state {
  flex: none;
  width: 7px;
  height: 7px;
  border-radius: 4px;
  background: var(--muted);
  align-self: center;
}
.running .state {
  background: #a78bfa;
  animation: pulse 1s ease-in-out infinite;
}
.confirm .state,
li.confirm .state {
  background: #fbbf24;
}
.ok .state {
  background: #22c55e;
}
.error .state,
.denied .state {
  background: #f87171;
}
.tools .label {
  font-weight: 600;
}
.tools .summary {
  color: var(--muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.confirm p {
  margin: 6px 0 8px;
  color: var(--muted);
}
.confirm .source {
  margin: 6px 0 0;
  font-size: 12px;
  color: #9a9aa5;
}
.confirm .source.safe {
  color: #86efac;
}
.confirm .source.system {
  color: #c4b5fd;
}
.confirm .source.risk {
  color: #fca5a5;
}
.confirm-actions {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
.confirm-actions button,
.primary {
  height: 32px;
  padding: 0 14px;
  border: 0;
  border-radius: 9px;
  font: inherit;
  font-size: 13px;
  font-weight: 600;
}
.deny {
  background: var(--input);
  color: #ececf1;
}
.allow,
.primary {
  background: #8b5cf6;
  color: white;
}

.typing {
  display: flex;
  gap: 4px;
  padding: 12px 14px;
  border-radius: 14px;
  background: var(--card);
}
.typing span {
  width: 6px;
  height: 6px;
  border-radius: 3px;
  background: var(--muted);
  animation: pulse 1s ease-in-out infinite;
}
.typing span:nth-child(2) {
  animation-delay: 150ms;
}
.typing span:nth-child(3) {
  animation-delay: 300ms;
}
@keyframes pulse {
  50% {
    opacity: 0.3;
  }
}

.error {
  margin: 0;
  padding: 8px 12px;
  border-radius: 10px;
  background: rgba(248, 113, 113, 0.12);
  color: #fca5a5;
  font-size: 13px;
}

footer {
  display: flex;
  align-items: flex-end;
  gap: 8px;
  padding: 12px;
  border-top: 1px solid var(--line);
}
textarea {
  flex: 1;
  min-height: 40px;
  max-height: 140px;
  box-sizing: border-box;
  padding: 10px 12px;
  border: 1px solid var(--line);
  border-radius: 12px;
  background: var(--input);
  color: #ececf1;
  font: inherit;
  font-size: 14px;
  resize: none;
  field-sizing: content;
}
textarea:focus,
.login input:focus {
  outline: none;
  border-color: #8b5cf6;
}
.send {
  display: grid;
  place-items: center;
  width: 40px;
  height: 40px;
  border: 0;
  border-radius: 12px;
  background: #8b5cf6;
  color: white;
}
.send:disabled {
  opacity: 0.4;
}

.login {
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 24px 20px;
}
.login h2 {
  margin: 0;
  font-size: 20px;
}
.login p {
  margin: 0;
  color: var(--muted);
  font-size: 13px;
  line-height: 1.5;
}
.login label {
  display: flex;
  flex-direction: column;
  gap: 6px;
  font-size: 13px;
  color: var(--muted);
}
.login input {
  height: 38px;
  padding: 0 12px;
  border: 1px solid var(--line);
  border-radius: 10px;
  background: var(--input);
  color: #ececf1;
  font: inherit;
}
.primary {
  height: 40px;
  margin-top: 6px;
}
.primary:disabled {
  opacity: 0.5;
}

.permissions {
  flex: 1;
  overflow-y: auto;
  padding: 14px 16px;
}
.hint {
  margin: 0 0 12px;
  color: var(--muted);
  font-size: 13px;
  line-height: 1.5;
}
.permissions ul {
  list-style: none;
  margin: 0;
  padding: 0;
}
.permissions li {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 0;
  border-bottom: 1px solid var(--line);
}
.info {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 2px;
  min-width: 0;
}
.info .label {
  font-size: 14px;
  font-weight: 600;
}
.info .desc {
  font-size: 12px;
  color: var(--muted);
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.segmented {
  display: flex;
  padding: 2px;
  border-radius: 9px;
  background: var(--input);
}
.segmented button {
  height: 26px;
  padding: 0 8px;
  border: 0;
  border-radius: 7px;
  background: transparent;
  color: var(--muted);
  font: inherit;
  font-size: 12px;
}
.segmented button.on {
  background: #7d46d7;
  color: white;
}
.segmented button:disabled {
  opacity: 0.35;
}
.confirm pre {
  margin: 0 0 8px;
  max-height: 140px;
  overflow: auto;
  padding: 8px 10px;
  border-radius: 8px;
  background: var(--input);
  color: #ececf1;
  font-family: ui-monospace, "JetBrains Mono", monospace;
  font-size: 12px;
  white-space: pre-wrap;
  word-break: break-word;
}

.automations {
  flex: 1;
  overflow-y: auto;
  padding: 14px 16px;
}
.automations .hint em {
  color: #a78bfa;
  font-style: normal;
}
.automations h3 {
  margin: 18px 0 4px;
  font-size: 12px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--muted);
}
.automations ul {
  list-style: none;
  margin: 0;
  padding: 0;
}
.automations li {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 10px 0;
  border-bottom: 1px solid var(--line);
}
.automations .desc {
  white-space: normal;
}
.none {
  margin: 6px 0;
  color: var(--muted);
  font-size: 13px;
}
.run {
  display: grid;
  place-items: center;
  flex: none;
  width: 34px;
  height: 34px;
  border: 0;
  border-radius: 10px;
  background: var(--input);
  color: #a78bfa;
}
.run:hover:not(:disabled) {
  background: #7d46d7;
  color: white;
}
.run:disabled {
  opacity: 0.5;
}
.toggle {
  flex: none;
  position: relative;
  width: 40px;
  height: 22px;
  border: 0;
  border-radius: 11px;
  background: #3a3b40;
  padding: 0;
}
.toggle span {
  position: absolute;
  top: 3px;
  left: 3px;
  width: 16px;
  height: 16px;
  border-radius: 8px;
  background: white;
  transition: transform 160ms ease;
}
.toggle.on {
  background: #7d46d7;
}
.toggle.on span {
  transform: translateX(18px);
}
.runs li {
  gap: 8px;
  font-size: 12px;
}
.runs .when {
  flex: none;
  color: var(--muted);
  font-variant-numeric: tabular-nums;
}
.runs .what {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.runs .by {
  flex: none;
  max-width: 120px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  color: var(--muted);
}
.runs .failed .what {
  color: #fca5a5;
}

.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.chips button {
  height: 30px;
  padding: 0 12px;
  border: 1px solid var(--line);
  border-radius: 15px;
  background: var(--input);
  color: #ececf1;
  font: inherit;
  font-size: 13px;
}
.chips.small {
  margin-top: -4px;
}
.chips.small button {
  height: 26px;
  padding: 0 10px;
  font-size: 12px;
}
.chips button.on {
  border-color: #8b5cf6;
  background: #332f3d;
}
.link {
  display: block;
  margin-top: 16px;
  border: 0;
  background: transparent;
  color: #a78bfa;
  font: inherit;
  font-size: 13px;
}

.logout {
  margin-top: 16px;
  border: 0;
  background: transparent;
  color: #f87171;
  font: inherit;
  font-size: 13px;
}
</style>
