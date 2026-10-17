<script setup lang="ts">
// Um PC pareado: como ele está, conversar com a Emma dele, comandos de energia e esquecer.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { connectComputers, connectForget, connectSend, on, type Computer } from "../../../system";

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
async function forget() {
  await connectForget(props.arg!).catch(() => {});
  emit("back");
}

let timer = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  computers.value = await connectComputers().catch(() => []);
  loadInfo();
  timer = window.setInterval(loadInfo, 15000);
  stops.push(
    await on<Computer[]>("lumna://connect", (list) => {
      const was = pc.value?.online;
      computers.value = list;
      if (!was && pc.value?.online) loadInfo();
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

      <div class="ms-label">Emma do PC</div>
      <div class="ms-card pad">
        <div v-for="(m, i) in chat" :key="i" class="bubble" :class="m.from">{{ m.text }}</div>
        <p v-if="thinking" class="ms-note">A Emma está pensando…</p>
        <form class="ask" @submit.prevent="sendEmma()">
          <input v-model="ask" placeholder="Peça algo pro PC (ex.: o build terminou?)" :disabled="!pc.online || thinking" />
          <button class="send" type="submit" :disabled="!ask.trim() || !pc.online || thinking" aria-label="Enviar"><Icon name="send" :size="20" /></button>
        </form>
      </div>

      <div class="ms-label">Energia</div>
      <div class="grid">
        <button v-for="a in ACTIONS" :key="a.id" class="act" :class="{ danger: a.id === 'poweroff' }" :disabled="!pc.online" @click="confirm = a.id">
          <Icon :name="a.icon" :size="24" />
          <span>{{ a.label }}</span>
        </button>
      </div>
      <p v-if="error" class="ms-error">{{ error }}</p>

      <div class="ms-actions">
        <button class="ms-btn danger" @click="forget()">Esquecer este computador</button>
      </div>
    </template>
    <p v-else class="ms-note">Esse computador não está mais pareado.</p>

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
.act.danger {
  color: #fca5a5;
}
.act:disabled {
  opacity: 0.4;
}
</style>
