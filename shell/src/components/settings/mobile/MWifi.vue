<script setup lang="ts">
// Wi-Fi: liga/desliga, a rede atual (toque = propriedades), as disponíveis e "adicionar rede".
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { on, wifiConnect, wifiPower, wifiScan, wifiState, type WifiNetwork, type WifiState } from "../../../system";

defineProps<{ arg?: string }>();
const emit = defineEmits<{ open: [page: "wifi-detalhes" | "wifi-adicionar"] }>();

const state = ref<WifiState | null>(null);
const error = ref("");
const busy = ref(false);
const asking = ref<WifiNetwork | null>(null);
const password = ref("");
const show = ref(false);

const current = computed(() => state.value?.networks.find((n) => n.connected) ?? null);
const others = computed(() => state.value?.networks.filter((n) => !n.connected) ?? []);
const SEC: Record<string, string> = { open: "Aberta", psk: "Protegida", "8021x": "Empresarial" };

async function load() {
  state.value = await wifiState().catch(() => null);
}
async function toggle() {
  busy.value = true;
  error.value = "";
  try {
    await wifiPower(!state.value?.powered);
    window.setTimeout(() => wifiScan().catch(() => {}), 1500);
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
  window.setTimeout(load, 800);
}
async function pick(n: WifiNetwork) {
  if (!n.known && n.security !== "open") {
    asking.value = n;
    password.value = "";
    return;
  }
  await connect(n);
}
async function connect(n: WifiNetwork, pass?: string) {
  busy.value = true;
  error.value = "";
  try {
    await wifiConnect(n.path, pass);
    asking.value = null;
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
  load();
}

let timer = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  await load();
  wifiScan().catch(() => {});
  stops.push(await on<WifiState>("lumna://wifi", (s) => (state.value = s)));
  timer = window.setInterval(() => wifiScan().catch(() => {}), 12000);
});
onUnmounted(() => {
  clearInterval(timer);
  stops.forEach((s) => s());
});
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-card">
      <div class="ms-row">
        <span class="ico violet"><Icon name="wifi" :size="26" /></span>
        <span class="txt">
          <strong>Wi-Fi</strong>
          <small>{{ !state?.available ? "Sem placa Wi-Fi" : state.powered ? "Ativado" : "Desativado" }}</small>
        </span>
        <button class="ms-switch" :class="{ on: state?.powered }" :disabled="busy || !state?.available" aria-label="Wi-Fi" @click="toggle()" />
      </div>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>

    <template v-if="state?.powered">
      <template v-if="current">
        <div class="ms-label">Rede atual</div>
        <div class="ms-card">
          <button class="ms-row" @click="emit('open', 'wifi-detalhes')">
            <span class="ico violet"><Icon name="wifi" :size="26" /></span>
            <span class="txt"><strong>{{ current.name }}</strong><small class="on">Conectado</small></span>
            <Icon class="chev" name="forward" :size="20" />
          </button>
        </div>
      </template>

      <div class="ms-label">Redes disponíveis</div>
      <div class="ms-card">
        <button v-for="n in others" :key="n.path" class="ms-row" :disabled="busy" @click="pick(n)">
          <span class="ms-bars"><i v-for="b in 4" :key="b" :class="{ on: b <= n.signal }" :style="{ height: `${b * 4}px` }" /></span>
          <span class="txt">
            <strong>{{ n.name }}</strong>
            <small>{{ n.known ? "Salva" : SEC[n.security] ?? "Protegida" }}</small>
          </span>
          <Icon v-if="n.security !== 'open'" class="chev" name="lock" :size="18" />
        </button>
        <p v-if="!others.length" class="ms-note pad">Procurando redes…</p>
      </div>

      <div class="ms-card add">
        <button class="ms-row" @click="emit('open', 'wifi-adicionar')">
          <span class="ico violet"><Icon name="plus" :size="24" /></span>
          <span class="txt"><strong>Adicionar rede</strong><small>Rede oculta ou digitada à mão</small></span>
          <Icon class="chev" name="forward" :size="20" />
        </button>
      </div>
    </template>

    <div v-if="asking" class="ms-sheet-veil" @click.self="asking = null">
      <div class="ms-sheet">
        <h2>{{ asking.name }}</h2>
        <label class="ms-field">
          <small>Senha</small>
          <div class="pw">
            <input v-model="password" :type="show ? 'text' : 'password'" autocomplete="off" @keydown.enter="connect(asking!, password)" />
            <button class="ms-icon-btn" aria-label="Mostrar senha" @click="show = !show"><Icon name="eye" :size="20" /></button>
          </div>
        </label>
        <p v-if="error" class="ms-error">{{ error }}</p>
        <div class="ms-actions">
          <button class="ms-btn" @click="asking = null">Cancelar</button>
          <button class="ms-btn primary" :disabled="busy || password.length < 8" @click="connect(asking!, password)">
            {{ busy ? "Conectando…" : "Conectar" }}
          </button>
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
.add {
  margin-top: 14px;
}
.ms-bars {
  width: 32px;
  justify-content: center;
}
.pw {
  display: flex;
  align-items: center;
}
.pw input {
  flex: 1;
  min-width: 0;
}
</style>
