<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  bluetoothConfirm,
  bluetoothConnect,
  bluetoothDisconnect,
  bluetoothForget,
  bluetoothPair,
  bluetoothPower,
  bluetoothScan,
  bluetoothState,
  on,
  type BluetoothState,
  type BtConfirm,
  type BtDevice,
} from "../../system";

const state = ref<BluetoothState | null>(null);
const open = ref<string | null>(null);
const busy = ref<string | null>(null);
const error = ref<{ path: string; text: string } | null>(null);
const confirm = ref<BtConfirm | null>(null);

const paired = computed(() => state.value?.devices.filter((d) => d.paired) ?? []);
const nearby = computed(() => state.value?.devices.filter((d) => !d.paired) ?? []);

function icon(d: BtDevice) {
  if (d.icon.startsWith("audio")) return "headphones";
  if (d.icon === "input-gaming") return "gamepad";
  if (d.icon === "input-keyboard") return "keyboard";
  if (d.icon === "input-mouse" || d.icon === "input-tablet") return "mouse";
  if (d.icon === "phone") return "phone";
  if (d.icon === "computer") return "desktop";
  return "bluetooth";
}

async function run(path: string, action: () => Promise<void>) {
  busy.value = path;
  error.value = null;
  try {
    await action();
    open.value = null;
  } catch (e) {
    error.value = { path, text: String(e) };
  } finally {
    busy.value = null;
    state.value = await bluetoothState();
  }
}

async function power() {
  if (!state.value) return;
  const on = !state.value.powered;
  await bluetoothPower(on).catch(() => {});
  state.value = await bluetoothState();
  if (on) bluetoothScan(true).catch(() => {});
}

function answer(accept: boolean) {
  if (confirm.value) bluetoothConfirm(confirm.value.device, accept);
  confirm.value = null;
}

const stops: (() => void)[] = [];

onMounted(async () => {
  stops.push(await on<BluetoothState>("lumna://bluetooth", (s) => (state.value = s)));
  stops.push(await on<BtConfirm>("lumna://bt-confirm", (c) => (confirm.value = c)));
  stops.push(await on("lumna://bt-confirm-done", () => (confirm.value = null)));
  state.value = await bluetoothState();
  // Procura aparelhos só enquanto a página está aberta (a busca gasta bateria e atrapalha o Wi-Fi).
  if (state.value.powered) bluetoothScan(true).catch(() => {});
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  if (state.value?.powered) bluetoothScan(false).catch(() => {});
});
</script>

<template>
  <div class="top">
    <div>
      <h1>Bluetooth</h1>
      <p class="lead">Fones, caixas de som, controles, teclados e mouses.</p>
    </div>
    <button
      v-if="state?.available"
      class="switch"
      :class="{ on: state.powered }"
      :title="state.powered ? 'Desligar o Bluetooth' : 'Ligar o Bluetooth'"
      @click="power()"
    />
  </div>

  <div v-if="!state" />
  <div v-else-if="!state.available" class="card"><p class="empty">Nenhum adaptador Bluetooth encontrado.</p></div>
  <div v-else-if="!state.powered" class="card"><p class="empty">O Bluetooth está desligado.</p></div>
  <template v-else>
    <h2>Meus aparelhos</h2>
    <div class="card">
      <p v-if="!paired.length" class="empty">Nenhum aparelho pareado ainda.</p>
      <template v-for="d in paired" :key="d.path">
        <button class="row" :disabled="busy !== null" @click="open = open === d.path ? null : d.path">
          <span class="badge"><Icon :name="icon(d)" :size="18" /></span>
          <div class="grow">
            <div class="title">{{ d.name }}</div>
            <div class="sub">
              {{ d.connected ? "Conectado" : "Não conectado" }}
              <template v-if="d.battery !== null"> · bateria {{ d.battery }}%</template>
            </div>
          </div>
          <span v-if="busy === d.path" class="spinner" />
        </button>
        <div v-if="open === d.path" class="actions">
          <button v-if="d.connected" class="btn" @click="run(d.path, () => bluetoothDisconnect(d.path))">Desconectar</button>
          <button v-else class="btn primary" @click="run(d.path, () => bluetoothConnect(d.path))">Conectar</button>
          <button class="btn danger" @click="run(d.path, () => bluetoothForget(d.path))">Esquecer</button>
        </div>
        <p v-if="error?.path === d.path" class="error">{{ error.text }}</p>
      </template>
    </div>

    <h2>
      Aparelhos por perto
      <span v-if="state.discovering" class="spinner" title="Procurando" />
    </h2>
    <div class="card">
      <p v-if="!nearby.length" class="empty">
        Procurando… Coloque o aparelho em modo de pareamento (no controle de PS5: segure PS + Create; no de Xbox: o
        botão de parear em cima).
      </p>
      <template v-for="d in nearby" :key="d.path">
        <button class="row" :disabled="busy !== null" @click="run(d.path, () => bluetoothPair(d.path))">
          <span class="badge"><Icon :name="icon(d)" :size="18" /></span>
          <div class="grow">
            <div class="title">{{ d.name }}</div>
            <div class="sub">{{ busy === d.path ? "Pareando…" : "Clique para parear" }}</div>
          </div>
          <span v-if="busy === d.path" class="spinner" />
        </button>
        <p v-if="error?.path === d.path" class="error">{{ error.text }}</p>
      </template>
    </div>
  </template>

  <div v-if="confirm" class="dialog-veil">
    <div class="dialog">
      <h3>Parear com {{ confirm.name || "o aparelho" }}</h3>
      <template v-if="confirm.kind === 'confirm'">
        <p>Este código aparece no aparelho?</p>
        <div class="code">{{ confirm.code }}</div>
        <div class="buttons">
          <button class="btn" @click="answer(false)">Não</button>
          <button class="btn primary" @click="answer(true)">Sim, parear</button>
        </div>
      </template>
      <template v-else>
        <p>Digite este código no aparelho e aperte Enter.</p>
        <div class="code">{{ confirm.code }}</div>
        <div class="buttons">
          <button class="btn" @click="confirm = null">Fechar</button>
        </div>
      </template>
    </div>
  </div>
</template>

<style scoped>
.top {
  display: flex;
  align-items: flex-start;
  justify-content: space-between;
  gap: 16px;
}
.top .switch {
  margin-top: 8px;
}
.dialog-veil {
  position: fixed;
  inset: 0 0 var(--kb-inset, 0px) 0;
  display: grid;
  place-items: center;
  background: rgba(5, 2, 15, 0.55);
}
.dialog {
  width: 360px;
  padding: 22px;
  border-radius: 18px;
  background: #1a1433;
  border: 1px solid var(--lumna-line);
  box-shadow: 0 24px 60px rgba(5, 2, 15, 0.6);
}
.dialog h3 {
  margin: 0 0 6px;
  font-size: 17px;
}
.dialog p {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 13px;
}
.code {
  margin: 18px 0;
  text-align: center;
  font-size: 34px;
  font-weight: 700;
  letter-spacing: 0.18em;
  font-variant-numeric: tabular-nums;
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
