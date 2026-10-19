<script setup lang="ts">
// Celular: a tela de bloqueio, como nos esboços. Capa (relógio, lanterna, emergência) e, deslizando
// pra cima, a senha (com o LUMNA Keyboard). A senha é a do Linux; quem confere é o shell. Com PIN
// (Ajustes → Tela), deslizar mostra o teclado numérico; a senha continua valendo ("Usar a senha").
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import { invoke } from "@tauri-apps/api/core";
import Icon from "../Icon.vue";
import MusicCard from "../music/MusicCard.vue";
import {
  toggleEmma,
  artUrl,
  lockState,
  lockUnlock,
  on,
  phoneCall,
  quickFlashlight,
  quickState,
  status,
  wallpaperList,
  type Status,
} from "../../system";
import { swipe } from "../../swipe";

type View = "capa" | "senha" | "emergencia";
const view = ref<View>("capa");
const now = ref(new Date());
const sys = ref<Status | null>(null);
const wallpaper = ref<string | null>(null);
const torch = ref<boolean | null>(null);
const password = ref("");
const show = ref(false);
const busy = ref(false);
const error = ref("");
const shake = ref(false);
const screenOn = ref(true);
const input = ref<HTMLInputElement>();
/** Tamanho do PIN (null: não tem PIN, só a senha). */
const pinLen = ref<number | null>(null);
/** Na tela da senha, o teclado numérico do PIN (e não o campo da senha). */
const pinMode = ref(true);
const pin = ref("");
/** Errou demais: segundos até poder tentar de novo. */
const wait = ref(0);
let waitTimer = 0;
const root = ref<HTMLElement>();

const time = computed(() => now.value.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" }));
const date = computed(() => {
  const d = now.value.toLocaleDateString("pt-BR", { weekday: "long", day: "numeric", month: "long" });
  return d.charAt(0).toUpperCase() + d.slice(1);
});
const EMERGENCY = [
  { number: "190", label: "Polícia" },
  { number: "192", label: "SAMU" },
  { number: "193", label: "Bombeiros" },
  { number: "112", label: "Emergência" },
];

function reset() {
  view.value = "capa";
  password.value = "";
  pin.value = "";
  pinMode.value = true;
  error.value = "";
  show.value = false;
  input.value?.blur();
}
const usingPin = computed(() => pinLen.value != null && pinMode.value);

async function askPassword() {
  view.value = "senha";
  error.value = "";
  if (usingPin.value) return;
  await nextTick();
  input.value?.focus();
  // o foco numa camada que ainda não tem o teclado pode não avisar: diz direto que as teclas são daqui
  invoke("keyboard_target", { label: "mobile-lock", focused: true, secret: true }).catch(() => {});
}
async function usePassword(on: boolean) {
  pinMode.value = !on;
  pin.value = "";
  error.value = "";
  if (on) {
    await nextTick();
    input.value?.focus();
    invoke("keyboard_target", { label: "mobile-lock", focused: true, secret: true }).catch(() => {});
  } else {
    input.value?.blur();
  }
}
function countdown(seconds: number) {
  window.clearInterval(waitTimer);
  wait.value = seconds;
  if (seconds <= 0) return;
  waitTimer = window.setInterval(() => {
    wait.value = Math.max(0, wait.value - 1);
    if (!wait.value) {
      window.clearInterval(waitTimer);
      error.value = "";
    }
  }, 1000);
}
async function submit(secret = password.value) {
  if (!secret || busy.value || wait.value) return;
  busy.value = true;
  error.value = "";
  try {
    await lockUnlock(secret);
    reset();
  } catch (e) {
    error.value = String(e).replace(/^Error: /, "");
    password.value = "";
    pin.value = "";
    shake.value = true;
    window.setTimeout(() => (shake.value = false), 400);
    const st = await lockState().catch(() => null);
    if (st?.wait) countdown(st.wait);
  }
  busy.value = false;
}
function digit(d: string) {
  if (busy.value || wait.value || !pinLen.value || pin.value.length >= pinLen.value) return;
  error.value = "";
  pin.value += d;
  // o último número confere sozinho, como no Android
  if (pin.value.length === pinLen.value) submit(pin.value);
}
function backspace() {
  pin.value = pin.value.slice(0, -1);
}
async function toggleTorch() {
  if (torch.value == null) return;
  await quickFlashlight(!torch.value).catch(() => {});
  torch.value = (await quickState().catch(() => null))?.flashlight ?? torch.value;
}
async function emergency(number: string) {
  await phoneCall(number).catch((e) => (error.value = String(e)));
}
async function load() {
  now.value = new Date();
  sys.value = await status().catch(() => sys.value);
  const w = await wallpaperList().catch(() => null);
  wallpaper.value = w ? w.lock ?? w.current : null;
  torch.value = (await quickState().catch(() => null))?.flashlight ?? null;
  const st = await lockState().catch(() => null);
  pinLen.value = st?.pin ?? null;
  if (st?.wait) countdown(st.wait);
}

let clock = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  await load();
  clock = window.setInterval(() => (now.value = new Date()), 5000);
  stops.push(await on<Status>("lumna://status", (s) => (sys.value = s)));
  stops.push(
    await on<boolean>("lumna://lock", (locked) => {
      if (locked) {
        reset();
        load();
      }
    }),
  );
  // fechou o teclado na tela da senha: volta pro descanso (relógio grande), como no Android
  stops.push(
    await on<{ shown: boolean }>("lumna://keyboard", (k) => {
      if (!k.shown && view.value === "senha" && !usingPin.value && !busy.value) reset();
    }),
  );
  stops.push(
    await on<boolean>("lumna://screen", (on) => {
      screenOn.value = on;
      if (!on) reset();
      else load();
    }),
  );
  if (root.value) {
    stops.push(
      swipe(root.value, {
        on: {
          up: () => view.value === "capa" && askPassword(),
          down: () => view.value !== "capa" && reset(),
        },
        threshold: 70,
        ignore: "input, button",
      }),
    );
  }
});
onUnmounted(() => {
  clearInterval(clock);
  clearInterval(waitTimer);
  stops.forEach((s) => s());
});
</script>

<template>
  <main
    ref="root"
    class="lock"
    :class="{ off: !screenOn }"
    :style="wallpaper ? { backgroundImage: `linear-gradient(rgba(7,4,15,.35), rgba(7,4,15,.75)), url(${artUrl(wallpaper)})` } : {}"
  >
    <div class="status">
      <span>{{ time }}</span>
      <span class="icons">
        <Icon :name="sys?.network === 'wifi' ? 'wifi' : sys?.network === 'ethernet' ? 'ethernet' : 'offline'" :size="15" />
        <span v-if="sys?.battery != null" class="battery"><Icon :name="sys.charging ? 'bolt' : 'battery'" :size="15" />{{ sys.battery }}%</span>
      </span>
    </div>

    <!-- capa -->
    <template v-if="view === 'capa'">
      <div class="brand"><span class="gem" /> LUMNA</div>
      <div class="clock">
        <Icon class="padlock" name="lock" :size="30" />
        <div class="time">{{ time }}</div>
        <div class="date">{{ date }}</div>
      </div>
      <!-- a música que está tocando: dá pra pausar e pular sem desbloquear -->
      <MusicCard class="now-playing" />
      <!-- a Emma atende bloqueada, só com o que é seguro (volume, mídia...) -->
      <button class="ask" @click="toggleEmma()"><span class="gem small" /> Pergunte à Emma</button>
      <div class="bottom">
        <button class="round" :class="{ on: torch }" :disabled="torch == null" aria-label="Lanterna" @click="toggleTorch()">
          <Icon name="flashlight" :size="26" />
        </button>
        <button class="unlock" @click="askPassword()">
          <span class="ring"><Icon name="lock" :size="34" /></span>
          <span>Deslize para desbloquear</span>
        </button>
        <button class="round" aria-label="Emergência" @click="view = 'emergencia'"><Icon name="call" :size="24" /></button>
      </div>
    </template>

    <!-- senha -->
    <template v-else-if="view === 'senha'">
      <div class="auth">
        <Icon class="padlock" name="lock" :size="26" />
        <div class="time small">{{ time }}</div>
        <div class="date">{{ date }}</div>
        <div class="brand center"><span class="gem" /> LUMNA</div>
        <p class="title">{{ usingPin ? "Digite o PIN" : "Desbloquear LUMNA" }}</p>
        <template v-if="usingPin">
          <div class="dots" :class="{ shake }" :aria-label="`${pin.length} de ${pinLen} números`">
            <span v-for="i in pinLen ?? 0" :key="i" :class="{ on: i <= pin.length }" />
          </div>
          <p v-if="error" class="error">{{ wait ? `Muitas tentativas. Espere ${wait} s` : error }}</p>
          <div class="keypad" :class="{ off: wait > 0 || busy }">
            <button v-for="d in ['1', '2', '3', '4', '5', '6', '7', '8', '9']" :key="d" @click="digit(d)">{{ d }}</button>
            <button class="small" @click="usePassword(true)">Senha</button>
            <button @click="digit('0')">0</button>
            <button class="small" aria-label="Apagar" :disabled="!pin" @click="backspace()"><Icon name="backspace" :size="26" /></button>
          </div>
          <button class="link" @click="view = 'emergencia'">Emergência</button>
        </template>
        <template v-else>
        <form class="pill" :class="{ shake }" @submit.prevent="submit()">
          <Icon name="lock" :size="22" />
          <input
            ref="input"
            v-model="password"
            :type="show ? 'text' : 'password'"
            placeholder="Senha"
            autocomplete="current-password"
            :disabled="busy"
          />
          <button type="button" class="eye" aria-label="Mostrar senha" @click="show = !show"><Icon name="eye" :size="22" /></button>
          <button type="submit" class="go" :disabled="!password || busy" aria-label="Desbloquear"><Icon name="arrow-right" :size="24" /></button>
        </form>
        <p v-if="error" class="error">{{ wait ? `Muitas tentativas. Espere ${wait} s` : error }}</p>
        <button v-if="pinLen" class="link" @click="usePassword(false)">Usar o PIN</button>
        <button class="link" @click="view = 'emergencia'">Emergência</button>
        </template>
      </div>
    </template>

    <!-- emergência: liga sem desbloquear -->
    <template v-else>
      <div class="auth">
        <Icon class="padlock" name="call" :size="30" />
        <p class="title">Ligação de emergência</p>
        <div class="sos">
          <button v-for="e in EMERGENCY" :key="e.number" @click="emergency(e.number)">
            <strong>{{ e.number }}</strong><span>{{ e.label }}</span>
          </button>
        </div>
        <p v-if="error" class="error">{{ error }}</p>
        <button class="link" @click="reset()">Voltar</button>
      </div>
    </template>
  </main>
</template>

<style scoped>
.lock {
  height: 100vh;
  box-sizing: border-box;
  padding: 10px 22px 40px;
  display: flex;
  flex-direction: column;
  background:
    radial-gradient(circle at 105% -12%, transparent 30%, rgba(124, 92, 230, 0.75) 31%, rgba(91, 33, 182, 0.3) 33%, transparent 42%),
    #080612;
  background-size: cover;
  background-position: center;
  color: var(--lumna-text);
  user-select: none;
  touch-action: none;
}
/* tela apagada: nada responde ao toque */
.lock.off {
  pointer-events: none;
}
.status {
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 13px;
  font-weight: 500;
}
.icons,
.battery {
  display: flex;
  align-items: center;
  gap: 6px;
}
.battery {
  gap: 2px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-top: 24px;
  letter-spacing: 0.5em;
  font-size: 15px;
}
.brand.center {
  justify-content: center;
  margin: 28px 0 0;
}
.gem {
  width: 18px;
  height: 18px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.clock {
  margin-top: 13vh;
  display: flex;
  flex-direction: column;
  align-items: center;
}
.padlock {
  color: #b9a2fb;
}
.time {
  margin-top: 10px;
  font-size: 96px;
  font-weight: 200;
  line-height: 1;
  letter-spacing: -0.02em;
}
.time.small {
  font-size: 72px;
}
.date {
  margin-top: 8px;
  color: #cfc6ee;
  font-size: 19px;
}
.now-playing {
  width: 100%;
  box-sizing: border-box;
  margin-bottom: 14px;
  background: rgba(16, 13, 28, 0.78);
}
.ask {
  align-self: center;
  display: flex;
  align-items: center;
  gap: 12px;
  margin: auto 0 22px;
  padding: 11px 20px;
  border: 1px solid rgba(167, 139, 250, 0.25);
  border-radius: 999px;
  background: rgba(20, 16, 34, 0.7);
  color: var(--lumna-text);
  font-size: 15px;
}
.gem.small {
  width: 11px;
  height: 11px;
  border-radius: 2px;
}
.ask + .bottom {
  margin-top: 0;
}
.bottom {
  margin-top: auto;
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
}
.round {
  width: 58px;
  height: 58px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 50%;
  background: rgba(20, 16, 34, 0.75);
  color: var(--lumna-text);
}
.round.on {
  background: #b9a2fb;
  color: #1a1030;
}
.round:disabled {
  opacity: 0.35;
}
.unlock {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  border: 0;
  background: none;
  color: #cfc6ee;
  font: inherit;
  font-size: 15px;
}
.ring {
  width: 74px;
  height: 74px;
  display: grid;
  place-items: center;
  border: 1.5px solid rgba(185, 162, 251, 0.6);
  border-radius: 50%;
  background: rgba(20, 16, 34, 0.6);
  color: #b9a2fb;
}
.auth {
  margin-top: 6vh;
  display: flex;
  flex-direction: column;
  align-items: center;
}
.title {
  margin: 26px 0 14px;
  font-size: 18px;
}
.pill {
  width: 100%;
  height: 64px;
  box-sizing: border-box;
  padding: 0 8px 0 20px;
  display: flex;
  align-items: center;
  gap: 12px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 999px;
  background: rgba(16, 13, 28, 0.85);
  color: var(--lumna-muted);
}
.pill input {
  flex: 1;
  min-width: 0;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 18px;
  letter-spacing: 0.1em;
  outline: none;
}
.pill input::placeholder {
  letter-spacing: 0;
  color: var(--lumna-muted);
}
.eye {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-left: 1px solid rgba(255, 255, 255, 0.12);
  background: none;
  color: var(--lumna-muted);
}
.go {
  width: 50px;
  height: 50px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: #b9a2fb;
  color: #1a1030;
}
.go:disabled {
  opacity: 0.5;
}
.shake {
  animation: shake 0.35s;
}
@keyframes shake {
  20%,
  60% {
    transform: translateX(-10px);
  }
  40%,
  80% {
    transform: translateX(10px);
  }
}
.dots {
  display: flex;
  gap: 16px;
  height: 20px;
  margin: 4px 0 6px;
}
.dots span {
  width: 14px;
  height: 14px;
  border: 1.5px solid rgba(185, 162, 251, 0.75);
  border-radius: 50%;
  transition: background 0.12s;
}
.dots span.on {
  background: #b9a2fb;
}
.keypad {
  display: grid;
  grid-template-columns: repeat(3, 76px);
  gap: 14px 28px;
  margin-top: 22px;
}
.keypad.off {
  opacity: 0.4;
  pointer-events: none;
}
.keypad button {
  width: 76px;
  height: 76px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.07);
  color: var(--lumna-text);
  font: inherit;
  font-size: 30px;
  font-weight: 300;
}
.keypad button:active {
  background: rgba(185, 162, 251, 0.3);
}
.keypad button.small {
  background: none;
  color: var(--lumna-violet-soft);
  font-size: 15px;
}
.keypad button:disabled {
  opacity: 0.3;
}
.error {
  margin: 10px 0 0;
  color: #fca5a5;
  font-size: 14px;
}
.link {
  margin-top: 18px;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 15px;
}
.sos {
  width: 100%;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 10px;
}
.sos button {
  padding: 18px 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  border: 1px solid rgba(248, 113, 113, 0.35);
  border-radius: 18px;
  background: rgba(40, 12, 20, 0.6);
  color: var(--lumna-text);
  font: inherit;
}
.sos strong {
  font-size: 28px;
  font-weight: 500;
}
.sos span {
  color: #fca5a5;
  font-size: 13px;
}
</style>
