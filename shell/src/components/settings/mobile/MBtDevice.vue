<script setup lang="ts">
// Um aparelho pareado: conectar/desconectar, bateria, endereço e esquecer.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import {
  bluetoothConnect,
  bluetoothDisconnect,
  bluetoothForget,
  bluetoothState,
  on,
  type BluetoothState,
} from "../../../system";

const props = defineProps<{ arg?: string }>();
const emit = defineEmits<{ back: [] }>();
const state = ref<BluetoothState | null>(null);
const busy = ref(false);
const error = ref("");
const device = computed(() => state.value?.devices.find((d) => d.path === props.arg) ?? null);

async function run(fn: () => Promise<void>, leave = false) {
  busy.value = true;
  error.value = "";
  try {
    await fn();
    if (leave) emit("back");
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
  state.value = await bluetoothState().catch(() => state.value);
}

const stops: (() => void)[] = [];
onMounted(async () => {
  state.value = await bluetoothState().catch(() => null);
  stops.push(await on<BluetoothState>("lumna://bluetooth", (s) => (state.value = s)));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="ms-scroll">
    <template v-if="device">
      <div class="hero">
        <span class="big" :class="{ on: device.connected }"><Icon name="bluetooth" :size="40" /></span>
        <h2>{{ device.name }}</h2>
        <p :class="{ on: device.connected }">{{ device.connected ? "Conectado" : "Pareado, desconectado" }}</p>
      </div>
      <div class="ms-card">
        <div v-if="device.battery != null" class="ms-row"><span class="txt"><strong>Bateria</strong></span><span class="value">{{ device.battery }}%</span></div>
        <div class="ms-row"><span class="txt"><strong>Endereço</strong></span><span class="value">{{ device.address }}</span></div>
      </div>
      <div class="ms-actions">
        <button v-if="device.connected" class="ms-btn" :disabled="busy" @click="run(() => bluetoothDisconnect(device!.path))">Desconectar</button>
        <button v-else class="ms-btn primary" :disabled="busy" @click="run(() => bluetoothConnect(device!.path))">
          {{ busy ? "Conectando…" : "Conectar" }}
        </button>
        <button class="ms-btn danger" :disabled="busy" @click="run(() => bluetoothForget(device!.path), true)">Esquecer</button>
      </div>
    </template>
    <p v-else class="ms-note">Esse aparelho não está mais na lista.</p>
    <p v-if="error" class="ms-error">{{ error }}</p>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 8px 0 20px;
}
.big {
  width: 84px;
  height: 84px;
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
  margin: 14px 0 2px;
  font-size: 24px;
  font-weight: 500;
}
.hero p {
  margin: 0;
  color: var(--lumna-muted);
}
.hero p.on {
  color: var(--lumna-violet-soft);
}
</style>
