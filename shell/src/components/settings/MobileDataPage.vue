<script setup lang="ts">
// Dados móveis: o chip, a conexão de dados (liga/desliga, APN, roaming), o tipo de rede e o
// uso no mês (o LUMNA conta os bytes da conexão de dados a cada minuto).
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { mobileDataInfo, mobileDataSet, quickMobileData, toggleEmma, type MobileData } from "../../system";

const info = ref<MobileData | null>(null);
const error = ref("");
const busy = ref(false);
const apn = ref("");
const editingApn = ref(false);
const LIMIT = "lumna.data.limit";
const limitGb = ref(5);
try {
  limitGb.value = Number(localStorage.getItem(LIMIT)) || 5;
} catch {}

const STATES: Record<string, string> = {
  connected: "Conectado",
  registered: "Registrado na rede",
  enabled: "Ligado, sem rede",
  searching: "Procurando rede",
  disabled: "Desligado",
  locked: "Chip bloqueado (PIN)",
  failed: "Falhou",
  connecting: "Conectando",
};
const state = computed(() => {
  const i = info.value;
  if (!i?.modem) return "Modem não encontrado";
  const s = STATES[i.state] ?? i.state;
  return i.tech ? `${s} · ${i.tech}` : s;
});
const online = computed(() => info.value?.state === "connected" || info.value?.connection?.active);

function gb(bytes: number) {
  const g = bytes / 1024 ** 3;
  return g >= 1 ? `${g.toFixed(1).replace(".", ",")} GB` : `${Math.round(bytes / 1024 ** 2)} MB`;
}
const usedPercent = computed(() => Math.min(100, ((info.value?.usage ?? 0) / (limitGb.value * 1024 ** 3)) * 100));

async function load() {
  info.value = await mobileDataInfo().catch(() => null);
  if (!editingApn.value) apn.value = info.value?.connection?.apn ?? "";
}
async function run(fn: () => Promise<void>) {
  busy.value = true;
  error.value = "";
  try {
    await fn();
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
  await load();
}
const toggleData = () => run(() => quickMobileData(!info.value?.connection?.active));
const toggleRoaming = () => run(() => mobileDataSet("roaming", info.value?.connection?.roaming ? "off" : "on"));
const setMode = (id: string) => run(() => mobileDataSet("modes", id));
function saveApn() {
  editingApn.value = false;
  run(() => mobileDataSet("apn", apn.value));
}
function setLimit(event: Event) {
  limitGb.value = Number((event.target as HTMLSelectElement).value);
  try {
    localStorage.setItem(LIMIT, String(limitGb.value));
  } catch {}
}

let timer = 0;
onMounted(() => {
  load();
  timer = window.setInterval(load, 5000);
});
onUnmounted(() => clearInterval(timer));
</script>

<template>
  <section class="page">
    <h1>Dados móveis</h1>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card sim-card">
      <div class="row">
        <span class="badge big"><Icon name="sim" :size="26" /></span>
        <div class="grow">
          <div class="title">SIM 1</div>
          <div class="sub">{{ info?.operator || "Operadora" }}<template v-if="info?.number"> · {{ info.number }}</template></div>
          <div class="sub status"><span class="dot" :class="{ on: online }" /> {{ state }}</div>
        </div>
        <span v-if="info?.signal != null" class="chip">{{ info.signal }}%</span>
      </div>
    </div>

    <div class="card">
      <div class="row">
        <div class="grow">
          <div class="title">Dados móveis</div>
          <div class="sub">{{ info?.connection ? "Usar a rede da operadora" : "Nenhuma conexão de dados configurada" }}</div>
        </div>
        <button
          class="switch"
          :class="{ on: info?.connection?.active }"
          role="switch"
          :aria-checked="!!info?.connection?.active"
          :disabled="busy || !info?.connection"
          @click="toggleData()"
        />
      </div>
      <div class="row">
        <div class="grow">
          <div class="title">SIM para internet</div>
          <div class="sub">SIM 1</div>
        </div>
      </div>
      <div class="row">
        <div class="grow">
          <div class="title">Tipo de rede preferido</div>
          <div class="sub">O modem escolhe entre as gerações permitidas</div>
        </div>
        <select :value="info?.mode" :disabled="busy || !info?.modes.length" @change="setMode(($event.target as HTMLSelectElement).value)">
          <option v-if="!info?.modes.length" value="">Automático</option>
          <option v-for="m in info?.modes ?? []" :key="m.id" :value="m.id">{{ m.label }}</option>
        </select>
      </div>
      <div class="row">
        <div class="grow">
          <div class="title">Seleção de operadora</div>
          <div class="sub">Automática</div>
        </div>
      </div>
      <div class="row apn">
        <div class="grow">
          <div class="title">Ponto de acesso (APN)</div>
          <input
            v-model="apn"
            :disabled="!info?.connection"
            placeholder="ex.: zap.vivo.com.br"
            @focus="editingApn = true"
            @keydown.enter="saveApn()"
          />
        </div>
        <button v-if="editingApn" class="btn primary" :disabled="busy" @click="saveApn()">Salvar</button>
      </div>
      <div class="row">
        <div class="grow">
          <div class="title">Roaming de dados</div>
          <div class="sub">Usar dados móveis em roaming</div>
        </div>
        <button
          class="switch"
          :class="{ on: info?.connection?.roaming }"
          role="switch"
          :aria-checked="!!info?.connection?.roaming"
          :disabled="busy || !info?.connection"
          @click="toggleRoaming()"
        />
      </div>
    </div>

    <h2>Uso de dados</h2>
    <div class="card padded usage">
      <div class="used">
        <strong>{{ gb(info?.usage ?? 0) }}</strong> usados
        <span class="cycle">neste mês</span>
      </div>
      <div class="bar"><div :style="{ width: `${usedPercent}%` }" /></div>
      <div class="scale">
        <span>0 GB</span>
        <select :value="limitGb" @change="setLimit">
          <option v-for="g in [1, 2, 5, 10, 15, 20, 50]" :key="g" :value="g">{{ g }} GB</option>
        </select>
      </div>
      <p class="note">O LUMNA conta o que passa pela conexão de dados desde que ele começou a contar.</p>
    </div>

    <button class="card row emma" @click="toggleEmma()">
      <span class="badge"><Icon name="mic" :size="18" /></span>
      <div class="grow"><div class="title">Ajuda da Emma</div></div>
      <Icon name="forward" :size="18" />
    </button>
  </section>
</template>

<style scoped>
.badge.big {
  width: 48px;
  height: 48px;
}
.status {
  display: flex;
  align-items: center;
  gap: 6px;
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #94a3b8;
}
.dot.on {
  background: #22c55e;
}
select,
.apn input {
  padding: 8px 10px;
  border: 1px solid var(--lumna-line);
  border-radius: 10px;
  background: rgba(20, 16, 34, 0.9);
  color: var(--lumna-text);
  font: inherit;
  max-width: 55%;
}
.apn input {
  margin-top: 6px;
  max-width: none;
  width: 100%;
  box-sizing: border-box;
}
.usage .used {
  display: flex;
  align-items: baseline;
  gap: 8px;
}
.usage strong {
  font-size: 32px;
}
.cycle {
  margin-left: auto;
  color: var(--lumna-muted);
}
.bar {
  margin: 14px 0 8px;
  height: 8px;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.1);
  overflow: hidden;
}
.bar div {
  height: 100%;
  border-radius: 4px;
  background: var(--lumna-violet-soft);
}
.scale {
  display: flex;
  justify-content: space-between;
  align-items: center;
  color: var(--lumna-muted);
}
.emma {
  width: 100%;
  margin-top: 16px;
  text-align: left;
}
</style>
