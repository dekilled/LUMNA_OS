<script setup lang="ts">
import { nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import {
  layerHide,
  on,
  wifiConnect,
  wifiDisconnect,
  wifiForget,
  wifiPower,
  wifiScan,
  wifiState,
  type WifiNetwork,
  type WifiState,
} from "../system";

const state = ref<WifiState | null>(null);
const visible = ref(false);
/** Rede aberta na lista (senha, desconectar, aviso). */
const open = ref<string | null>(null);
const password = ref("");
const reveal = ref(false);
const busy = ref<string | null>(null);
const error = ref<{ path: string; text: string } | null>(null);
const passwordInput = ref<HTMLInputElement[]>();

const needsPassword = (n: WifiNetwork) => n.security === "psk" && !n.known;

async function refresh() {
  state.value = await wifiState();
}

async function opened() {
  visible.value = true;
  open.value = null;
  error.value = null;
  await refresh();
  if (state.value?.powered) wifiScan().catch(() => {});
}

function close() {
  visible.value = false;
  password.value = "";
  // Espera a animação de saída antes de esconder a camada.
  window.setTimeout(() => !visible.value && layerHide(), 160);
}

async function expand(path: string) {
  open.value = open.value === path ? null : path;
  password.value = "";
  reveal.value = false;
  error.value = null;
  await nextTick();
  passwordInput.value?.[0]?.focus();
}

async function connect(network: WifiNetwork, passphrase?: string) {
  busy.value = network.path;
  error.value = null;
  try {
    await wifiConnect(network.path, passphrase);
    open.value = null;
    password.value = "";
  } catch (e) {
    error.value = { path: network.path, text: String(e) };
    open.value = network.path;
  } finally {
    busy.value = null;
    await refresh();
  }
}

function choose(network: WifiNetwork) {
  if (busy.value) return;
  if (network.connected || network.security === "8021x" || needsPassword(network)) expand(network.path);
  else connect(network);
}

function submit(network: WifiNetwork) {
  if (password.value.length < 8) {
    error.value = { path: network.path, text: "A senha do Wi-Fi tem pelo menos 8 caracteres." };
    return;
  }
  connect(network, password.value);
}

async function act(path: string, action: () => Promise<void>) {
  busy.value = path;
  error.value = null;
  try {
    await action();
    open.value = null;
  } catch (e) {
    error.value = { path, text: String(e) };
  } finally {
    busy.value = null;
    await refresh();
  }
}

async function power() {
  if (!state.value) return;
  const on = !state.value.powered;
  await wifiPower(on).catch(() => {});
  await refresh();
  if (on) wifiScan().catch(() => {});
}

function onKey(event: KeyboardEvent) {
  if (visible.value && event.key === "Escape") close();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://wifi-opened", opened));
  stops.push(await on<WifiState>("lumna://wifi", (s) => (state.value = s)));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <!-- O véu é transparente; clicar nele fecha o menu. -->
  <div class="veil" @click.self="close()">
    <section class="panel" :class="{ shown: visible }">
      <header>
        <div>
          <h2>Wi-Fi</h2>
          <p v-if="!state?.available">Nenhuma placa Wi-Fi encontrada</p>
          <p v-else-if="!state.powered">Desligado</p>
          <p v-else-if="state.connected">Conectado a {{ state.connected }}</p>
          <p v-else-if="state.state === 'connecting'">Conectando…</p>
          <p v-else>Escolha uma rede</p>
        </div>
        <button
          v-if="state?.available && state.powered"
          class="icon"
          :class="{ spinning: state.scanning }"
          title="Procurar redes"
          @click="wifiScan().catch(() => {})"
        >
          <Icon name="refresh" :size="17" />
        </button>
        <button
          v-if="state?.available"
          class="switch"
          :class="{ on: state.powered }"
          :title="state.powered ? 'Desligar o Wi-Fi' : 'Ligar o Wi-Fi'"
          @click="power()"
        >
          <span />
        </button>
      </header>

      <ul v-if="state?.available && state.powered" class="networks">
        <li v-if="!state.networks.length" class="empty">
          {{ state.scanning ? "Procurando redes…" : "Nenhuma rede por perto" }}
        </li>
        <li v-for="n in state.networks" :key="n.path" :class="{ open: open === n.path, connected: n.connected }">
          <button class="row" :disabled="busy !== null && busy !== n.path" @click="choose(n)">
            <svg class="signal" viewBox="0 0 24 24" aria-hidden="true">
              <path
                v-for="(d, i) in ['M12 19.5h.01', 'M8.5 16a5 5 0 0 1 7 0', 'M5 12.5a10 10 0 0 1 14 0', 'M2 9a15 15 0 0 1 20 0']"
                :key="i"
                :d="d"
                :class="{ lit: i === 0 ? n.signal > 0 : n.signal > i }"
              />
            </svg>
            <span class="name">{{ n.name }}</span>
            <span v-if="busy === n.path" class="spinner" />
            <span v-else-if="n.connected" class="tag"><Icon name="check" :size="13" /> Conectada</span>
            <span v-else-if="n.known" class="tag">Salva</span>
            <Icon v-if="n.security !== 'open'" name="lock" :size="13" class="lock" />
          </button>

          <div v-if="open === n.path" class="details">
            <template v-if="n.connected">
              <div class="actions">
                <button @click="act(n.path, wifiDisconnect)">Desconectar</button>
                <button @click="act(n.path, () => wifiForget(n.path))">Esquecer rede</button>
              </div>
            </template>
            <p v-else-if="n.security === '8021x'" class="note">
              Redes com usuário e senha (empresa, faculdade) ainda não são suportadas pelo menu.
            </p>
            <form v-else-if="needsPassword(n)" @submit.prevent="submit(n)">
              <div class="field">
                <input
                  ref="passwordInput"
                  v-model="password"
                  :type="reveal ? 'text' : 'password'"
                  placeholder="Senha"
                  autocomplete="off"
                  spellcheck="false"
                  :disabled="busy === n.path"
                  @keydown.enter.prevent="submit(n)"
                />
                <button type="button" class="icon" :title="reveal ? 'Esconder senha' : 'Mostrar senha'" @click="reveal = !reveal">
                  <Icon name="eye" :size="16" />
                </button>
              </div>
              <button class="primary" type="submit" :disabled="busy === n.path">
                {{ busy === n.path ? "Conectando…" : "Conectar" }}
              </button>
            </form>
            <p v-if="error?.path === n.path" class="error">{{ error.text }}</p>
          </div>
          <p v-else-if="error?.path === n.path" class="error inline">{{ error.text }}</p>
        </li>
      </ul>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
}

.panel {
  position: absolute;
  top: 8px;
  right: 10px;
  width: 340px;
  max-height: calc(100vh - 24px);
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
  padding: 14px;
  background: rgba(18, 13, 36, 0.9);
  border: 1px solid var(--lumna-line);
  border-radius: 20px;
  box-shadow: 0 20px 50px rgba(5, 2, 15, 0.55);
  opacity: 0;
  transform: translateY(-8px) scale(0.98);
  transform-origin: top right;
  transition: opacity 150ms ease, transform 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.panel.shown {
  opacity: 1;
  transform: none;
}

button {
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  padding: 0;
}

header {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 2px 4px 12px;
}
header > div {
  flex: 1;
  min-width: 0;
}
h2 {
  margin: 0;
  font-size: 16px;
  font-weight: 600;
}
header p {
  margin: 2px 0 0;
  font-size: 12px;
  color: var(--lumna-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}

.icon {
  display: grid;
  place-items: center;
  width: 30px;
  height: 30px;
  border-radius: 10px;
  color: var(--lumna-muted);
}
.icon:hover {
  background: rgba(167, 139, 250, 0.14);
  color: var(--lumna-text);
}
.spinning svg {
  animation: spin 900ms linear infinite;
}

.switch {
  width: 40px;
  height: 22px;
  border-radius: 11px;
  background: rgba(255, 255, 255, 0.12);
  position: relative;
  transition: background-color 180ms ease;
}
.switch span {
  position: absolute;
  top: 3px;
  left: 3px;
  width: 16px;
  height: 16px;
  border-radius: 50%;
  background: #fff;
  transition: transform 200ms cubic-bezier(0.22, 1, 0.36, 1);
}
.switch.on {
  background: var(--lumna-violet);
}
.switch.on span {
  transform: translateX(18px);
}

.networks {
  list-style: none;
  margin: 0;
  padding: 0;
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.empty {
  padding: 18px 8px;
  text-align: center;
  font-size: 13px;
  color: var(--lumna-muted);
}

li {
  border-radius: 12px;
  transition: background-color 160ms ease;
}
li.open {
  background: rgba(255, 255, 255, 0.05);
}

.row {
  width: 100%;
  height: 42px;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 10px;
  border-radius: 12px;
  font-size: 14px;
  text-align: left;
}
.row:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.12);
}
.row:disabled {
  opacity: 0.5;
}
.connected .row {
  color: #fff;
}

.signal {
  width: 18px;
  height: 18px;
  flex: none;
  fill: none;
  stroke-width: 2;
  stroke-linecap: round;
}
.signal path {
  stroke: rgba(255, 255, 255, 0.18);
}
.signal path.lit {
  stroke: var(--lumna-violet-soft);
}

.name {
  flex: 1;
  min-width: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.tag {
  display: flex;
  align-items: center;
  gap: 4px;
  font-size: 11px;
  color: var(--lumna-violet-soft);
}
.lock {
  color: var(--lumna-muted);
  flex: none;
}

.details {
  padding: 2px 10px 12px;
}
form {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.field {
  display: flex;
  align-items: center;
  gap: 4px;
  height: 38px;
  padding: 0 4px 0 12px;
  border-radius: 10px;
  border: 1px solid var(--lumna-line);
  background: rgba(0, 0, 0, 0.25);
}
.field:focus-within {
  border-color: var(--lumna-violet);
}
input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.primary,
.actions button {
  height: 34px;
  border-radius: 10px;
  font-size: 13px;
}
.primary {
  background: var(--lumna-violet);
  color: #fff;
}
.primary:disabled {
  opacity: 0.6;
}
.actions {
  display: flex;
  gap: 8px;
}
.actions button {
  flex: 1;
  background: rgba(255, 255, 255, 0.07);
}
.actions button:hover {
  background: rgba(167, 139, 250, 0.2);
}

.note,
.error {
  margin: 4px 0 0;
  font-size: 12px;
  line-height: 1.4;
  color: var(--lumna-muted);
}
.error {
  color: #fca5a5;
}
.error.inline {
  padding: 0 10px 8px;
}

.spinner {
  width: 14px;
  height: 14px;
  border-radius: 50%;
  border: 2px solid rgba(255, 255, 255, 0.2);
  border-top-color: var(--lumna-violet-soft);
  animation: spin 800ms linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
