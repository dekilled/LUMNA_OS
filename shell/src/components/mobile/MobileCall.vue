<script setup lang="ts">
// Celular: a tela da ligação, por cima de tudo. Aparece sozinha quando uma chamada chega ou sai
// (o shell acompanha o ModemManager) e some um instante depois que ela acaba.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  contactFor,
  contactName,
  contactsList,
  formatNumber,
  haptic,
  on,
  phoneAccept,
  phoneCalls,
  phoneDtmf,
  phoneHangup,
  phoneMute,
  type Call,
  type Contact,
} from "../../system";
import { avatarColor, initial } from "../phone/avatar";

const calls = ref<Call[]>([]);
const contacts = ref<Contact[]>([]);
const now = ref(Date.now());
const muted = ref(false);
const pad = ref(false);
const tones = ref("");
const error = ref("");

// a chamada que importa: a que está tocando, senão a em andamento, senão a última
const call = computed(
  () =>
    calls.value.find((c) => c.state === "incoming" || c.state === "waiting") ??
    calls.value.find((c) => c.state !== "ended") ??
    calls.value[calls.value.length - 1] ??
    null,
);
const who = computed(() => {
  const c = call.value;
  if (!c) return { name: "", number: "" };
  const contact = contactFor(contacts.value, c.number);
  return contact
    ? { name: contactName(contact), number: formatNumber(c.number) }
    : { name: c.number ? formatNumber(c.number) : "Número privado", number: "" };
});
const ringing = computed(() => call.value?.state === "incoming" || call.value?.state === "waiting");
const status = computed(() => {
  const c = call.value;
  if (!c) return "";
  switch (c.state) {
    case "incoming":
    case "waiting":
      return "Chamada recebida";
    case "dialing":
    case "ringing-out":
      return "Chamando…";
    case "held":
      return "Em espera";
    case "ended":
      return "Chamada encerrada";
    case "active": {
      const s = c.active_since ? Math.max(0, Math.floor(now.value / 1000 - c.active_since)) : 0;
      return `${String(Math.floor(s / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;
    }
    default:
      return "";
  }
});

async function act(fn: () => Promise<void>) {
  error.value = "";
  try {
    await fn();
  } catch (e) {
    error.value = String(e);
  }
}
const accept = () => call.value && act(() => phoneAccept(call.value!.path));
const hangup = () => call.value && act(() => phoneHangup(call.value!.path));
async function toggleMute() {
  muted.value = !muted.value;
  await phoneMute(muted.value).catch(() => {});
}
function tone(k: string) {
  haptic(18).catch(() => {});
  tones.value += k;
  if (call.value) phoneDtmf(call.value.path, k).catch(() => {});
}

let timer = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  calls.value = await phoneCalls().catch(() => []);
  contacts.value = await contactsList().catch(() => []);
  timer = window.setInterval(() => (now.value = Date.now()), 1000);
  stops.push(
    await on<Call[]>("lumna://calls", (list) => {
      const wasLive = calls.value.some((c) => c.state !== "ended");
      calls.value = list;
      if (!list.some((c) => c.state !== "ended") && wasLive) {
        pad.value = false;
        tones.value = "";
        muted.value = false;
      }
    }),
  );
  stops.push(
    await on<boolean>("lumna://mobile-panel", async (shown) => {
      if (shown) contacts.value = await contactsList().catch(() => contacts.value);
    }),
  );
});
onUnmounted(() => {
  clearInterval(timer);
  stops.forEach((s) => s());
});
</script>

<template>
  <main class="call">
    <div class="who">
      <div class="avatar" :class="{ pulse: ringing }" :style="{ background: avatarColor(who.name) }">{{ initial(who.name) }}</div>
      <h1>{{ who.name }}</h1>
      <p v-if="who.number" class="number">{{ who.number }}</p>
      <p class="status" :class="{ ended: call?.state === 'ended' }">{{ status }}</p>
      <p v-if="call && call.state === 'active'" class="note">O áudio da ligação ainda não funciona no LUMNA</p>
    </div>

    <!-- tocando: atender ou recusar -->
    <div v-if="ringing" class="answer">
      <button class="big decline" aria-label="Recusar" @click="hangup()"><Icon name="call-end" :size="34" /></button>
      <button class="big accept" aria-label="Atender" @click="accept()"><Icon name="call" :size="32" /></button>
    </div>

    <!-- em andamento -->
    <template v-else-if="call && call.state !== 'ended'">
      <div v-if="pad" class="pad">
        <div class="tones">{{ tones || " " }}</div>
        <div class="keys">
          <button v-for="k in ['1', '2', '3', '4', '5', '6', '7', '8', '9', '*', '0', '#']" :key="k" @click="tone(k)">{{ k }}</button>
        </div>
      </div>
      <div class="controls">
        <button :class="{ on: muted }" @click="toggleMute()">
          <span class="ctl"><Icon :name="muted ? 'mic-off' : 'mic'" :size="26" /></span>Mudo
        </button>
        <button :class="{ on: pad }" @click="pad = !pad">
          <span class="ctl"><Icon name="dialpad" :size="26" /></span>Teclado
        </button>
        <button disabled>
          <span class="ctl"><Icon name="speaker" :size="26" /></span>Viva-voz
        </button>
      </div>
      <button class="big decline end" aria-label="Encerrar" @click="hangup()"><Icon name="call-end" :size="34" /></button>
    </template>

    <p v-if="error" class="error" @click="error = ''">{{ error }}</p>
  </main>
</template>

<style scoped>
.call {
  height: 100vh;
  box-sizing: border-box;
  padding: 90px 24px 60px;
  display: flex;
  flex-direction: column;
  align-items: center;
  background:
    radial-gradient(circle at 50% 0%, rgba(124, 92, 230, 0.35), transparent 60%),
    #07040f;
  color: var(--lumna-text);
  user-select: none;
}
.who {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  text-align: center;
}
.avatar {
  width: 120px;
  height: 120px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  font-size: 48px;
  font-weight: 600;
}
.avatar.pulse {
  box-shadow: 0 0 0 10px rgba(167, 139, 250, 0.15), 0 0 0 22px rgba(167, 139, 250, 0.07);
}
h1 {
  margin: 26px 0 4px;
  font-size: 30px;
  font-weight: 500;
}
.number {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 16px;
}
.status {
  margin: 12px 0 0;
  color: var(--lumna-violet-soft);
  font-size: 18px;
  font-variant-numeric: tabular-nums;
}
.status.ended {
  color: #f87171;
}
.note {
  margin: 10px 0 0;
  color: var(--lumna-muted);
  font-size: 12px;
}
.answer {
  width: 100%;
  display: flex;
  justify-content: space-around;
}
.big {
  width: 78px;
  height: 78px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  color: #fff;
}
.accept {
  background: #22c55e;
}
.decline {
  background: #ef4444;
}
.end {
  margin-top: 28px;
}
.controls {
  width: 100%;
  display: flex;
  justify-content: space-around;
}
.controls button {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}
.controls button:disabled {
  opacity: 0.35;
}
.ctl {
  width: 64px;
  height: 64px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.08);
}
.controls button.on .ctl {
  background: var(--lumna-violet-soft);
  color: #1a1030;
}
.pad {
  width: 100%;
  margin-bottom: 20px;
}
.tones {
  min-height: 30px;
  text-align: center;
  font-size: 26px;
  letter-spacing: 0.1em;
}
.keys {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 10px;
  margin-top: 8px;
}
.keys button {
  height: 54px;
  border: 0;
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.07);
  color: inherit;
  font: inherit;
  font-size: 24px;
}
.error {
  margin-top: 14px;
  color: #fca5a5;
  font-size: 13px;
  text-align: center;
}
</style>
