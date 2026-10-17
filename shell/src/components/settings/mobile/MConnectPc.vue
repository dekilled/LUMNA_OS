<script setup lang="ts">
// Um PC pareado: como ele está, mandar arquivos e o que foi copiado, conversar com a Emma dele,
// agendamentos, energia, os recursos ligados pra ele, o endereço e esquecer.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import Schedules from "../../connect/Schedules.vue";
import Transfers from "../../connect/Transfers.vue";
import { formatSize } from "../../files/format";
import {
  connectComputers,
  connectForget,
  connectRecentFiles,
  connectSend,
  connectSendClipboard,
  connectSendFiles,
  connectSetAddress,
  connectSetFeatures,
  convertFileSrc,
  on,
  type Computer,
  type ConnectFeatures,
  type RecentFile,
  type ScheduleTask,
} from "../../../system";

const props = defineProps<{ arg?: string }>();
const emit = defineEmits<{ back: [] }>();

interface Info {
  host: string;
  user: string;
  uptime: number;
  load: string | null;
  memory_total_kb: number;
  memory_available_kb: number;
  battery: number | null;
  charging: boolean;
}
const computers = ref<Computer[]>([]);
const pc = computed(() => computers.value.find((c) => c.id === props.arg) ?? null);
const info = ref<Info | null>(null);
const ask = ref("");
const chat = ref<{ from: "eu" | "emma"; text: string }[]>([]);
const thinking = ref(false);
const confirm = ref<"lock" | "suspend" | "reboot" | "poweroff" | null>(null);
const error = ref("");
const note = ref("");
const tasks = ref<ScheduleTask[]>([]);
const picking = ref(false);
const recent = ref<RecentFile[]>([]);
const chosen = ref<Set<string>>(new Set());
const editing = ref(false);
const address = ref("");

const FEATURES: { key: keyof ConnectFeatures; title: string; sub: string }[] = [
  { key: "notifications", title: "Avisos do PC", sub: "As notificações do PC aparecem aqui" },
  { key: "phone_notifications", title: "Avisos deste celular no PC", sub: "Notificações e ligações chegando" },
  { key: "clipboard", title: "Área de transferência", sub: "O que copiar num aparece no outro" },
  { key: "files", title: "Receber arquivos", sub: "O que o PC mandar vai pra pasta de recebidos" },
  { key: "control", title: "Senhas e confirmações", sub: "Pedidos de senha e da Emma do PC" },
];
const IMAGE = /\.(jpe?g|png|webp|gif)$/i;

const ACTIONS = [
  { id: "lock", label: "Bloquear", icon: "lock" },
  { id: "suspend", label: "Suspender", icon: "moon" },
  { id: "reboot", label: "Reiniciar", icon: "refresh" },
  { id: "poweroff", label: "Desligar", icon: "power" },
] as const;
const LABEL: Record<string, string> = { lock: "Bloquear", suspend: "Suspender", reboot: "Reiniciar", poweroff: "Desligar" };

function uptime(s: number) {
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  return d ? `${d} d ${h} h` : h ? `${h} h ${m} min` : `${m} min`;
}
const memory = computed(() => {
  if (!info.value?.memory_total_kb) return "";
  const used = (info.value.memory_total_kb - info.value.memory_available_kb) / 1024 / 1024;
  const total = info.value.memory_total_kb / 1024 / 1024;
  return `${used.toFixed(1).replace(".", ",")} de ${total.toFixed(1).replace(".", ",")} GB`;
});

async function loadInfo() {
  if (!props.arg || !pc.value?.online) return;
  info.value = await connectSend(props.arg, { type: "info" }).catch(() => info.value);
}
async function sendEmma() {
  const text = ask.value.trim();
  if (!text || !props.arg) return;
  chat.value.push({ from: "eu", text });
  ask.value = "";
  thinking.value = true;
  error.value = "";
  try {
    const reply = await connectSend(props.arg, { type: "emma", text });
    chat.value.push({ from: "emma", text: reply?.content ?? "(sem resposta)" });
  } catch (e) {
    error.value = String(e);
  }
  thinking.value = false;
}
async function power(action: string) {
  confirm.value = null;
  error.value = "";
  try {
    await connectSend(props.arg!, { type: "power", action });
  } catch (e) {
    error.value = String(e);
  }
}
function flash(text: string) {
  note.value = text;
  window.setTimeout(() => (note.value = ""), 3000);
}
async function toggle(key: keyof ConnectFeatures) {
  if (!pc.value) return;
  const features = { ...pc.value.features, [key]: !pc.value.features[key] };
  pc.value.features = features;
  await connectSetFeatures(pc.value.id, features).catch((e) => (error.value = String(e)));
}
async function loadTasks() {
  if (!props.arg || !pc.value?.online) return;
  const r = await connectSend(props.arg, { type: "schedule-list" }).catch(() => null);
  if (r?.tasks) tasks.value = r.tasks;
}
async function addTask(task: Omit<ScheduleTask, "id" | "by">) {
  error.value = "";
  try {
    const r = await connectSend(props.arg!, { type: "schedule-add", task });
    tasks.value = r.tasks;
  } catch (e) {
    error.value = String(e);
  }
}
async function removeTask(id: string) {
  try {
    const r = await connectSend(props.arg!, { type: "schedule-remove", id });
    tasks.value = r.tasks;
  } catch (e) {
    error.value = String(e);
  }
}
async function pickFiles() {
  chosen.value = new Set();
  picking.value = true;
  recent.value = await connectRecentFiles().catch(() => []);
}
function choose(path: string) {
  const s = new Set(chosen.value);
  if (s.has(path)) s.delete(path);
  else s.add(path);
  chosen.value = s;
}
async function sendFiles() {
  picking.value = false;
  error.value = "";
  await connectSendFiles(props.arg!, [...chosen.value]).catch((e) => (error.value = String(e)));
}
async function sendClipboard() {
  error.value = "";
  try {
    await connectSendClipboard(props.arg!);
    flash("Mandei o que você copiou pro PC.");
  } catch (e) {
    error.value = String(e);
  }
}
async function saveAddress() {
  error.value = "";
  const [host, port] = address.value.trim().split(/:(?=\d+$)/);
  try {
    await connectSetAddress(props.arg!, host, port ? Number(port) : undefined);
    editing.value = false;
  } catch (e) {
    error.value = String(e);
  }
}

async function forget() {
  await connectForget(props.arg!).catch(() => {});
  emit("back");
}

let timer = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  computers.value = await connectComputers().catch(() => []);
  loadInfo();
  loadTasks();
  timer = window.setInterval(loadInfo, 15000);
  stops.push(
    await on<Computer[]>("lumna://connect", (list) => {
      const was = pc.value?.online;
      computers.value = list;
      if (!was && pc.value?.online) {
        loadInfo();
        loadTasks();
      }
    }),
  );
});
onUnmounted(() => {
  clearInterval(timer);
  stops.forEach((s) => s());
});
</script>

<template>
  <div class="ms-scroll">
    <template v-if="pc">
      <div class="hero">
        <span class="big" :class="{ on: pc.online }"><Icon name="desktop" :size="38" /></span>
        <h2>{{ pc.name }}</h2>
        <p :class="{ on: pc.online }">{{ pc.online ? "Conectado" : "Fora do ar (tentando de novo)" }}</p>
      </div>

      <div v-if="info" class="ms-card">
        <div class="ms-row"><span class="txt"><strong>Usuário</strong></span><span class="value">{{ info.user }}</span></div>
        <div class="ms-row"><span class="txt"><strong>Ligado há</strong></span><span class="value">{{ uptime(info.uptime) }}</span></div>
        <div v-if="memory" class="ms-row"><span class="txt"><strong>Memória</strong></span><span class="value">{{ memory }}</span></div>
        <div v-if="info.battery != null" class="ms-row">
          <span class="txt"><strong>Bateria</strong></span><span class="value">{{ info.battery }}%{{ info.charging ? " · carregando" : "" }}</span>
        </div>
      </div>

      <div class="ms-label">Compartilhar</div>
      <div class="grid two">
        <button class="act" :disabled="!pc.online" @click="pickFiles()"><Icon name="upload" :size="24" /><span>Enviar arquivos</span></button>
        <button class="act" :disabled="!pc.online" @click="sendClipboard()"><Icon name="clipboard" :size="24" /><span>Mandar o que copiei</span></button>
      </div>
      <p v-if="note" class="ms-note">{{ note }}</p>
      <Transfers class="transfers" :peer="pc.name" />

      <div class="ms-label">Emma do PC</div>
      <div class="ms-card pad">
        <div v-for="(m, i) in chat" :key="i" class="bubble" :class="m.from">{{ m.text }}</div>
        <p v-if="thinking" class="ms-note">A Emma está pensando…</p>
        <form class="ask" @submit.prevent="sendEmma()">
          <input v-model="ask" placeholder="Peça algo pro PC (ex.: o build terminou?)" :disabled="!pc.online || thinking" />
          <button class="send" type="submit" :disabled="!ask.trim() || !pc.online || thinking" aria-label="Enviar"><Icon name="send" :size="20" /></button>
        </form>
      </div>

      <div class="ms-label">Agendamentos no PC</div>
      <Schedules :tasks="tasks" :disabled="!pc.online" @add="addTask" @remove="removeTask" />

      <div class="ms-label">Energia</div>
      <div class="grid">
        <button v-for="a in ACTIONS" :key="a.id" class="act" :class="{ danger: a.id === 'poweroff' }" :disabled="!pc.online" @click="confirm = a.id">
          <Icon :name="a.icon" :size="24" />
          <span>{{ a.label }}</span>
        </button>
      </div>
      <p v-if="error" class="ms-error">{{ error }}</p>

      <div class="ms-label">Recursos</div>
      <div class="ms-card">
        <div v-for="f in FEATURES" :key="f.key" class="ms-row">
          <span class="txt"><strong>{{ f.title }}</strong><small>{{ f.sub }}</small></span>
          <button class="ms-switch" :class="{ on: pc.features[f.key] }" :aria-label="f.title" @click="toggle(f.key)" />
        </div>
      </div>
      <p class="ms-note">O PC tem os ajustes dele pra este celular em Configurações → Celular.</p>

      <div class="ms-label">Conexão</div>
      <div class="ms-card">
        <button class="ms-row" @click="(address = pc.port === 47800 ? pc.host : `${pc.host}:${pc.port}`), (editing = true)">
          <span class="txt"><strong>Endereço</strong><small>{{ pc.host }}{{ pc.port === 47800 ? "" : `:${pc.port}` }}</small></span>
          <Icon class="chev" name="edit" :size="20" />
        </button>
      </div>

      <div class="ms-actions">
        <button class="ms-btn danger" @click="forget()">Esquecer este computador</button>
      </div>
    </template>
    <p v-else class="ms-note">Esse computador não está mais pareado.</p>

    <div v-if="picking" class="ms-sheet-veil" @click.self="picking = false">
      <div class="ms-sheet files">
        <h2>Enviar pro {{ pc?.name }}</h2>
        <div class="pick">
          <button v-for="f in recent" :key="f.path" class="file" :class="{ on: chosen.has(f.path) }" @click="choose(f.path)">
            <img v-if="IMAGE.test(f.name)" :src="convertFileSrc(f.path)" alt="" loading="lazy" />
            <span v-else class="fi"><Icon name="file" :size="22" /></span>
            <span class="fn"><strong>{{ f.name }}</strong><small>{{ formatSize(f.size) }}</small></span>
            <span class="check"><Icon v-if="chosen.has(f.path)" name="check" :size="16" /></span>
          </button>
          <p v-if="!recent.length" class="ms-note">Nenhum arquivo recente. No Arquivos, segure um arquivo → Enviar para.</p>
        </div>
        <div class="ms-actions">
          <button class="ms-btn" @click="picking = false">Cancelar</button>
          <button class="ms-btn primary" :disabled="!chosen.size" @click="sendFiles()">Enviar{{ chosen.size ? ` (${chosen.size})` : "" }}</button>
        </div>
      </div>
    </div>

    <div v-if="editing" class="ms-sheet-veil" @click.self="editing = false">
      <div class="ms-sheet">
        <h2>Endereço do PC</h2>
        <p class="ms-note">O IP da rede ou o 100.x do Tailscale (pra usar fora de casa).</p>
        <label class="ms-field">
          <small>Endereço</small>
          <input v-model="address" placeholder="192.168.0.10 ou 100.x.y.z" autocomplete="off" @keydown.enter="saveAddress()" />
        </label>
        <div class="ms-actions">
          <button class="ms-btn" @click="editing = false">Cancelar</button>
          <button class="ms-btn primary" :disabled="!address.trim()" @click="saveAddress()">Salvar</button>
        </div>
      </div>
    </div>

    <div v-if="confirm" class="ms-sheet-veil" @click.self="confirm = null">
      <div class="ms-sheet">
        <h2>{{ LABEL[confirm] }} o {{ pc?.name }}?</h2>
        <div class="ms-actions">
          <button class="ms-btn" @click="confirm = null">Cancelar</button>
          <button class="ms-btn primary" @click="power(confirm!)">{{ LABEL[confirm] }}</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 4px 0 18px;
}
.big {
  width: 80px;
  height: 80px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.08);
}
.big.on {
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
h2 {
  margin: 12px 0 2px;
  font-size: 22px;
  font-weight: 500;
}
.hero p {
  margin: 0;
  color: var(--lumna-muted);
}
.hero p.on {
  color: #4ade80;
}
.bubble {
  max-width: 85%;
  margin-bottom: 8px;
  padding: 10px 14px;
  border-radius: 16px;
  font-size: 15px;
  line-height: 1.35;
  white-space: pre-wrap;
}
.bubble.eu {
  margin-left: auto;
  background: rgba(139, 92, 246, 0.3);
}
.bubble.emma {
  background: rgba(255, 255, 255, 0.07);
}
.ask {
  display: flex;
  gap: 8px;
  margin-top: 6px;
}
.ask input {
  flex: 1;
  min-width: 0;
  padding: 12px 14px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  background: rgba(10, 8, 18, 0.6);
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  outline: none;
}
.send {
  width: 46px;
  border: 0;
  border-radius: 14px;
  background: #6d4fc4;
  color: #fff;
}
.send:disabled {
  opacity: 0.4;
}
.grid {
  display: grid;
  grid-template-columns: repeat(4, 1fr);
  gap: 8px;
}
.act {
  padding: 14px 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  border: 1px solid rgba(255, 255, 255, 0.07);
  border-radius: 16px;
  background: rgba(20, 17, 30, 0.9);
  color: var(--lumna-text);
  font: inherit;
  font-size: 12px;
}
.grid.two {
  grid-template-columns: 1fr 1fr;
}
.grid.two .act {
  font-size: 14px;
}
.transfers {
  margin-top: 10px;
}
.files {
  max-height: 80vh;
  display: flex;
  flex-direction: column;
}
.pick {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  margin: 4px -4px 0;
}
.file {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 8px 6px;
  border: 0;
  border-radius: 14px;
  background: none;
  color: inherit;
  font: inherit;
  text-align: left;
}
.file.on {
  background: rgba(139, 92, 246, 0.16);
}
.file img,
.fi {
  width: 48px;
  height: 48px;
  flex: none;
  border-radius: 10px;
  object-fit: cover;
}
.fi {
  display: grid;
  place-items: center;
  background: rgba(255, 255, 255, 0.07);
  color: var(--lumna-muted);
}
.fn {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.fn strong {
  font-weight: 400;
  font-size: 15px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.fn small {
  color: var(--lumna-muted);
  font-size: 12.5px;
}
.check {
  width: 24px;
  height: 24px;
  flex: none;
  display: grid;
  place-items: center;
  border: 1.5px solid rgba(255, 255, 255, 0.25);
  border-radius: 50%;
}
.file.on .check {
  border-color: transparent;
  background: #7c5ce6;
  color: #fff;
}
.act.danger {
  color: #fca5a5;
}
.act:disabled {
  opacity: 0.4;
}
</style>
