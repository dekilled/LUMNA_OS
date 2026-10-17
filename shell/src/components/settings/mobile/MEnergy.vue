<script setup lang="ts">
// Bateria e energia: carga, saúde da bateria, economia e desligar/reiniciar. O botão power
// (tocar = bloquear, segurar = este menu) e a tela de bloqueio vêm na próxima etapa.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { batteryInfo, powerAction, powerSetProfile, powerState, type Battery, type PowerState } from "../../../system";

defineProps<{ arg?: string }>();
const bat = ref<Battery | null>(null);
const power = ref<PowerState | null>(null);
const error = ref("");
const confirm = ref<"reboot" | "poweroff" | null>(null);

const STATUS: Record<string, string> = { Charging: "Carregando", Discharging: "Na bateria", Full: "Carregada", "Not charging": "Ligado na tomada" };
const saver = computed(() => (power.value?.profiles.includes("power-saver") ? power.value.profile === "power-saver" : null));

async function load() {
  bat.value = await batteryInfo().catch(() => null);
  power.value = await powerState().catch(() => null);
}
async function toggleSaver() {
  try {
    await powerSetProfile(saver.value ? "balanced" : "power-saver");
  } catch (e) {
    error.value = String(e);
  }
  load();
}
async function act(a: "reboot" | "poweroff") {
  confirm.value = null;
  await powerAction(a).catch((e) => (error.value = String(e)));
}
let timer = 0;
onMounted(() => {
  load();
  timer = window.setInterval(load, 5000);
});
onUnmounted(() => clearInterval(timer));
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-card pad battery">
      <div class="big">
        <strong>{{ bat?.percent ?? "—" }}<small>%</small></strong>
        <span :class="{ charging: bat?.status === 'Charging' }">
          <Icon :name="bat?.status === 'Charging' ? 'bolt' : 'battery'" :size="18" /> {{ STATUS[bat?.status ?? ""] ?? bat?.status }}
        </span>
      </div>
      <div class="bar"><div :style="{ width: `${bat?.percent ?? 0}%` }" /></div>
    </div>

    <div class="ms-card">
      <div v-if="bat?.health != null" class="ms-row"><span class="txt"><strong>Saúde da bateria</strong><small>Capacidade em relação à de fábrica</small></span><span class="value">{{ bat.health }}%</span></div>
      <div v-if="bat?.temperature != null" class="ms-row"><span class="txt"><strong>Temperatura</strong></span><span class="value">{{ bat.temperature.toFixed(1).replace(".", ",") }} °C</span></div>
      <div v-if="bat?.power != null" class="ms-row"><span class="txt"><strong>{{ bat.power >= 0 ? "Entrando" : "Gastando" }}</strong></span><span class="value">{{ Math.abs(bat.power).toFixed(1).replace(".", ",") }} W</span></div>
      <div v-if="bat?.voltage != null" class="ms-row"><span class="txt"><strong>Tensão</strong></span><span class="value">{{ bat.voltage.toFixed(2).replace(".", ",") }} V</span></div>
      <div v-if="bat?.cycles != null" class="ms-row"><span class="txt"><strong>Ciclos de carga</strong></span><span class="value">{{ bat.cycles }}</span></div>
    </div>

    <div class="ms-label">Economia</div>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt">
          <strong>Economia de energia</strong>
          <small>{{ saver == null ? "Instale o power-profiles-daemon pra usar" : "Segura o processador e dura mais" }}</small>
        </span>
        <button class="ms-switch" :class="{ on: saver }" :disabled="saver == null" aria-label="Economia" @click="toggleSaver()" />
      </div>
    </div>

    <div class="ms-label">Desligar</div>
    <div class="ms-card">
      <button class="ms-row" @click="confirm = 'reboot'">
        <span class="ico"><Icon name="refresh" :size="24" /></span>
        <span class="txt"><strong>Reiniciar</strong></span>
      </button>
      <button class="ms-row" @click="confirm = 'poweroff'">
        <span class="ico"><Icon name="power" :size="24" /></span>
        <span class="txt"><strong class="danger">Desligar</strong></span>
      </button>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>

    <div v-if="confirm" class="ms-sheet-veil" @click.self="confirm = null">
      <div class="ms-sheet">
        <h2>{{ confirm === "reboot" ? "Reiniciar o celular?" : "Desligar o celular?" }}</h2>
        <div class="ms-actions">
          <button class="ms-btn" @click="confirm = null">Cancelar</button>
          <button class="ms-btn primary" @click="act(confirm!)">{{ confirm === "reboot" ? "Reiniciar" : "Desligar" }}</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.battery .big {
  display: flex;
  align-items: baseline;
  justify-content: space-between;
}
.battery strong {
  font-size: 48px;
  font-weight: 400;
}
.battery strong small {
  font-size: 22px;
  color: var(--lumna-muted);
}
.battery span {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--lumna-muted);
}
.battery span.charging {
  color: #4ade80;
}
.bar {
  margin-top: 12px;
  height: 10px;
  border-radius: 5px;
  background: rgba(255, 255, 255, 0.1);
  overflow: hidden;
}
.bar div {
  height: 100%;
  border-radius: 5px;
  background: var(--lumna-violet-soft);
}
.danger {
  color: #fca5a5;
}
</style>
