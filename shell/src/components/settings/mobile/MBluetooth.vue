<script setup lang="ts">
// Bluetooth: liga/desliga, os aparelhos pareados (toque = página do aparelho) e os disponíveis,
// procurados enquanto a página está aberta. Parear pede confirmação do código na tela.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import {
  bluetoothConfirm,
  bluetoothPair,
  bluetoothPower,
  bluetoothScan,
  bluetoothState,
  on,
  type BluetoothState,
  type BtConfirm,
  type BtDevice,
} from "../../../system";

defineProps<{ arg?: string }>();
const emit = defineEmits<{ open: [page: "bt-aparelho", arg: string] }>();

const state = ref<BluetoothState | null>(null);
const confirm = ref<BtConfirm | null>(null);
const pairing = ref("");
const error = ref("");
const busy = ref(false);

const paired = computed(() => state.value?.devices.filter((d) => d.paired) ?? []);
const found = computed(() => state.value?.devices.filter((d) => !d.paired) ?? []);

const ICONS: Record<string, string> = { "audio-headphones": "headphones", "audio-headset": "headphones", "input-keyboard": "keyboard", "input-mouse": "mouse", phone: "phone", computer: "computer" };
const iconOf = (d: BtDevice) => ICONS[d.icon] ?? "bluetooth";

async function load() {
  state.value = await bluetoothState().catch(() => null);
}
async function toggle() {
  busy.value = true;
  error.value = "";
  try {
    await bluetoothPower(!state.value?.powered);
    if (!state.value?.powered) window.setTimeout(() => bluetoothScan(true).catch(() => {}), 800);
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
  load();
}
async function pair(d: BtDevice) {
  pairing.value = d.path;
  error.value = "";
  try {
    await bluetoothPair(d.path);
  } catch (e) {
    error.value = String(e);
  }
  pairing.value = "";
  load();
}
function answer(accept: boolean) {
  if (confirm.value) bluetoothConfirm(confirm.value.device, accept).catch(() => {});
  confirm.value = null;
}

const stops: (() => void)[] = [];
onMounted(async () => {
  await load();
  if (state.value?.powered) bluetoothScan(true).catch(() => {});
  stops.push(await on<BluetoothState>("lumna://bluetooth", (s) => (state.value = s)));
  stops.push(await on<BtConfirm>("lumna://bt-confirm", (c) => (confirm.value = c)));
  stops.push(await on("lumna://bt-confirm-done", () => (confirm.value = null)));
});
onUnmounted(() => {
  bluetoothScan(false).catch(() => {});
  stops.forEach((s) => s());
});
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-card">
      <div class="ms-row">
        <span class="ico violet"><Icon name="bluetooth" :size="26" /></span>
        <span class="txt">
          <strong>Bluetooth</strong>
          <small>{{ !state?.available ? "Sem Bluetooth (o bluetoothd está rodando?)" : state.powered ? "Ativado · visível como LUMNA" : "Desativado" }}</small>
        </span>
        <button class="ms-switch" :class="{ on: state?.powered }" :disabled="busy || !state?.available" aria-label="Bluetooth" @click="toggle()" />
      </div>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>

    <template v-if="state?.powered">
      <div class="ms-label">Meus aparelhos</div>
      <div class="ms-card">
        <button v-for="d in paired" :key="d.path" class="ms-row" @click="emit('open', 'bt-aparelho', d.path)">
          <span class="ico" :class="{ violet: d.connected }"><Icon :name="iconOf(d)" :size="24" /></span>
          <span class="txt">
            <strong>{{ d.name }}</strong>
            <small :class="{ on: d.connected }">
              {{ d.connected ? "Conectado" : "Pareado" }}<template v-if="d.battery != null"> · {{ d.battery }}%</template>
            </small>
          </span>
          <Icon class="chev" name="forward" :size="20" />
        </button>
        <p v-if="!paired.length" class="ms-note pad">Nenhum aparelho pareado ainda.</p>
      </div>

      <div class="ms-label">Disponíveis</div>
      <div class="ms-card">
        <button v-for="d in found" :key="d.path" class="ms-row" :disabled="!!pairing" @click="pair(d)">
          <span class="ico"><Icon :name="iconOf(d)" :size="24" /></span>
          <span class="txt"><strong>{{ d.name }}</strong><small>{{ pairing === d.path ? "Pareando…" : "Toque pra parear" }}</small></span>
        </button>
        <p v-if="!found.length" class="ms-note pad">Procurando aparelhos…</p>
      </div>
    </template>

    <div v-if="confirm" class="ms-sheet-veil">
      <div class="ms-sheet">
        <h2>Parear com {{ confirm.name || "o aparelho" }}?</h2>
        <p class="ms-note">
          {{ confirm.kind === "type" ? "Digite este código no outro aparelho:" : "Confira se o mesmo código aparece no outro aparelho:" }}
        </p>
        <div class="code">{{ confirm.code }}</div>
        <div class="ms-actions">
          <button class="ms-btn" @click="answer(false)">Cancelar</button>
          <button v-if="confirm.kind === 'confirm'" class="ms-btn primary" @click="answer(true)">Parear</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.pad {
  padding: 16px 18px;
  margin: 0;
}
.code {
  margin: 14px 0 4px;
  text-align: center;
  font-size: 34px;
  letter-spacing: 0.2em;
  font-variant-numeric: tabular-nums;
}
</style>
