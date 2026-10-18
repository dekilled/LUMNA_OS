<script setup lang="ts">
// Celular: a tela de bloqueio, como nos esboços. Capa (relógio, lanterna, emergência) e, deslizando
// pra cima, a senha (com o LUMNA Keyboard). A senha é a do Linux; quem confere é o shell.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  artUrl,
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
  error.value = "";
  show.value = false;
  input.value?.blur();
}
async function askPassword() {
  view.value = "senha";
  error.value = "";
  await nextTick();
  input.value?.focus();
}
async function submit() {
  if (!password.value || busy.value) return;
  busy.value = true;
  error.value = "";
  try {
    await lockUnlock(password.value);
    reset();
  } catch (e) {
    error.value = String(e).replace(/^Error: /, "");
    password.value = "";
    shake.value = true;
    window.setTimeout(() => (shake.value = false), 400);
  }
  busy.value = false;
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
      if (!k.shown && view.value === "senha" && !busy.value) reset();
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
        <p class="title">Desbloquear LUMNA</p>
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
        <p v-if="error" class="error">{{ error }}</p>
        <button class="link" @click="view = 'emergencia'">Emergência</button>
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
