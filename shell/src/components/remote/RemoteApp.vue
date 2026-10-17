<script setup lang="ts">
// LUMNA Controle no celular: o controle do modo Mídia do PC, pelo Celular Connect. Escolhe o PC
// e a tela (o projetor, a TV...), liga/desliga o Mídia nela e controla: setas, OK, voltar,
// início, mídia, volume e digitar. A área do meio também aceita deslizar (como um touchpad de TV).
import { getCurrentWindow } from "@tauri-apps/api/window";
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { connectComputers, connectMedia, connectSend, haptic, on, type Computer, type PcMonitor } from "../../system";

const computers = ref<Computer[]>([]);
const pcId = ref(decodeURIComponent(location.hash.slice(1)));
const pc = computed(() => computers.value.find((c) => c.id === pcId.value) ?? computers.value.find((c) => c.online) ?? null);
const monitors = ref<PcMonitor[]>([]);
const screen = ref("");
const volume = ref<number | null>(null);
const muted = ref(false);
const typing = ref(false);
const text = ref("");
const error = ref("");
const busy = ref(false);
const media = ref<string[]>([]);

const selected = computed(() => monitors.value.find((m) => m.name === screen.value) ?? null);
const inMedia = computed(() => !!selected.value && media.value.includes(selected.value.name));

async function send(message: Record<string, unknown>, buzz = 18) {
  if (!pc.value?.online) return null;
  haptic(buzz).catch(() => {});
  try {
    error.value = "";
    return await connectSend(pc.value.id, message);
  } catch (e) {
    error.value = String(e);
    return null;
  }
}
const pad = (action: string) => send({ type: "pad", action });
const mediaKey = (action: string) => send({ type: "media", action });
async function vol(action: "up" | "down" | "mute") {
  const r = await send({ type: "volume", action });
  if (r?.type === "volume") {
    volume.value = r.value;
    muted.value = r.muted;
  }
}
async function loadMonitors() {
  const r = await send({ type: "monitors" }, 0);
  if (r?.monitors) {
    monitors.value = r.monitors;
    media.value = r.monitors.filter((m: PcMonitor) => m.media).map((m: PcMonitor) => m.name);
    if (!monitors.value.some((m) => m.name === screen.value)) {
      // o que já está no Mídia; senão, o maior que não é o principal (o projetor)
      screen.value = (monitors.value.find((m) => m.media) ?? monitors.value.find((m) => !m.focused) ?? monitors.value[0])?.name ?? "";
    }
  }
}
async function toggleMedia() {
  if (!selected.value) return;
  busy.value = true;
  await send({ type: "mode", value: inMedia.value ? "desktop" : "media", monitor: selected.value.name }, 30);
  await loadMonitors();
  busy.value = false;
}
async function sendText() {
  const t = text.value;
  if (!t) return;
  text.value = "";
  await send({ type: "text", value: t });
}
function nextPc() {
  const online = computers.value.filter((c) => c.online);
  if (online.length < 2) return;
  const i = online.findIndex((c) => c.id === pc.value?.id);
  pcId.value = online[(i + 1) % online.length].id;
  monitors.value = [];
  loadMonitors();
}

// deslizar na área do meio = setas; toque = OK
let start: { x: number; y: number; t: number } | null = null;
function down(e: PointerEvent) {
  start = { x: e.clientX, y: e.clientY, t: Date.now() };
}
function up(e: PointerEvent) {
  if (!start) return;
  const dx = e.clientX - start.x;
  const dy = e.clientY - start.y;
  const far = Math.max(Math.abs(dx), Math.abs(dy));
  if (far < 24) {
    if (Date.now() - start.t < 400) pad("accept");
  } else if (Math.abs(dx) > Math.abs(dy)) pad(dx > 0 ? "right" : "left");
  else pad(dy > 0 ? "down" : "up");
  start = null;
}

const stops: (() => void)[] = [];
onMounted(async () => {
  computers.value = await connectComputers().catch(() => []);
  media.value = pc.value ? ((await connectMedia().catch(() => ({})))[pc.value.id] ?? []) : [];
  loadMonitors();
  stops.push(
    await on<Computer[]>("lumna://connect", (l) => {
      const was = pc.value?.online;
      computers.value = l;
      if (!was && pc.value?.online) loadMonitors();
    }),
  );
  stops.push(
    await on<{ computer: string; media: string[] }>("lumna://connect-mode", (m) => {
      if (m.computer === pc.value?.id) media.value = m.media;
    }),
  );
  stops.push(
    await on<string>("lumna://remote-pc", (id) => {
      pcId.value = id;
      loadMonitors();
    }),
  );
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <main class="remote">
    <header>
      <div class="brand"><span class="gem" /> LUMNA Controle</div>
      <button class="icon" aria-label="Fechar" @click="getCurrentWindow().close()"><Icon name="close" :size="22" /></button>
    </header>

    <template v-if="pc">
      <button class="pc" @click="nextPc()">
        <Icon name="desktop" :size="18" />
        <span>{{ pc.name }}</span>
        <i class="dot" :class="{ on: pc.online }" />
      </button>

      <section v-if="pc.online" class="screens">
        <div class="chips">
          <button v-for="m in monitors" :key="m.name" :class="{ on: screen === m.name }" @click="screen = m.name">
            <strong>{{ m.label }}</strong>
            <small>{{ m.width }}×{{ m.height }}<template v-if="media.includes(m.name)"> · Mídia</template></small>
          </button>
        </div>
        <button v-if="selected" class="mode" :class="{ live: inMedia }" :disabled="busy" @click="toggleMedia()">
          <Icon :name="inMedia ? 'desktop' : 'play'" :size="18" />
          {{ inMedia ? "Voltar essa tela pro Desktop" : "Abrir o Mídia nessa tela" }}
        </button>
      </section>
      <p v-else class="off">O PC está fora do ar. Ele precisa estar ligado e com o LUMNA aberto.</p>

      <section class="dpad" :class="{ dim: !pc.online }">
        <button class="arrow up" aria-label="Cima" @click="pad('up')"><Icon name="chevron-up" :size="30" /></button>
        <button class="arrow left" aria-label="Esquerda" @click="pad('left')"><Icon name="back" :size="30" /></button>
        <div class="ok" @pointerdown="down" @pointerup="up" @pointercancel="start = null">OK</div>
        <button class="arrow right" aria-label="Direita" @click="pad('right')"><Icon name="forward" :size="30" /></button>
        <button class="arrow down" aria-label="Baixo" @click="pad('down')"><Icon name="chevron-down" :size="30" /></button>
      </section>

      <section class="row3">
        <button aria-label="Voltar" @click="pad('back')"><Icon name="arrow-left" :size="24" /><span>Voltar</span></button>
        <button aria-label="Início" @click="send({ type: 'home' })"><Icon name="home" :size="24" /><span>Início</span></button>
        <button aria-label="Menu" @click="pad('menu')"><Icon name="menu" :size="24" /><span>Menu</span></button>
      </section>

      <section class="media">
        <button aria-label="Anterior" @click="mediaKey('previous')"><Icon name="skip-back" :size="22" /></button>
        <button aria-label="Voltar 10 s" @click="mediaKey('back')"><span class="ten">-10</span></button>
        <button class="play" aria-label="Tocar/pausar" @click="mediaKey('play-pause')"><Icon name="play" :size="28" /></button>
        <button aria-label="Avançar 10 s" @click="mediaKey('forward')"><span class="ten">+10</span></button>
        <button aria-label="Próxima" @click="mediaKey('next')"><Icon name="skip-forward" :size="22" /></button>
      </section>

      <section class="volume">
        <button aria-label="Baixar volume" @click="vol('down')"><Icon name="minus" :size="22" /></button>
        <button class="mute" :class="{ on: muted }" aria-label="Mudo" @click="vol('mute')">
          <Icon :name="muted ? 'volume-off' : 'volume'" :size="22" />
          <span v-if="volume != null">{{ muted ? "Mudo" : `${volume}%` }}</span>
        </button>
        <button aria-label="Subir volume" @click="vol('up')"><Icon name="plus" :size="22" /></button>
      </section>

      <form v-if="typing" class="type" @submit.prevent="sendText()">
        <input v-model="text" placeholder="Digitar no PC (buscar, senha do Wi-Fi…)" autofocus autocomplete="off" />
        <button type="button" aria-label="Apagar" @click="send({ type: 'key', value: 'backspace' })"><Icon name="backspace" :size="20" /></button>
        <button type="submit" aria-label="Enviar"><Icon name="send" :size="20" /></button>
      </form>
      <button v-else class="kb" @click="typing = true"><Icon name="keyboard" :size="20" /> Digitar no PC</button>

      <p v-if="error" class="err">{{ error }}</p>
    </template>
    <div v-else class="empty">
      <Icon name="desktop" :size="40" />
      <p>Nenhum PC pareado. Em Ajustes → Celular Connect, adicione o seu PC.</p>
    </div>
  </main>
</template>

<style scoped>
.remote {
  height: 100vh;
  box-sizing: border-box;
  padding: 18px 20px 28px;
  display: flex;
  flex-direction: column;
  gap: 16px;
  overflow-y: auto;
  background:
    radial-gradient(circle at 50% 40%, rgba(124, 92, 230, 0.18), transparent 60%),
    #080612;
  color: var(--lumna-text);
  user-select: none;
}
header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.brand {
  display: flex;
  align-items: center;
  gap: 12px;
  letter-spacing: 0.3em;
  font-size: 13px;
}
.gem {
  width: 14px;
  height: 14px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.icon {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.06);
  color: inherit;
}
.pc {
  align-self: center;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 999px;
  background: rgba(20, 16, 34, 0.8);
  color: inherit;
  font: inherit;
  font-size: 14px;
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #f87171;
}
.dot.on {
  background: #4ade80;
}
.screens {
  display: flex;
  flex-direction: column;
  gap: 10px;
}
.chips {
  display: flex;
  gap: 8px;
  overflow-x: auto;
}
.chips button {
  flex: none;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  gap: 2px;
  padding: 10px 14px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  background: rgba(20, 16, 34, 0.8);
  color: inherit;
  font: inherit;
}
.chips button.on {
  border-color: rgba(139, 92, 246, 0.7);
  background: rgba(139, 92, 246, 0.2);
}
.chips strong {
  font-weight: 500;
  font-size: 14px;
}
.chips small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.mode {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 12px;
  border: 0;
  border-radius: 16px;
  background: #6d4fc4;
  color: #fff;
  font: inherit;
  font-size: 15px;
  font-weight: 600;
}
.mode.live {
  background: rgba(255, 255, 255, 0.08);
  color: var(--lumna-text);
  font-weight: 500;
}
.mode:disabled {
  opacity: 0.5;
}
.off {
  margin: 0;
  text-align: center;
  color: var(--lumna-muted);
  font-size: 14px;
}
.dpad {
  align-self: center;
  width: min(78vw, 300px);
  aspect-ratio: 1;
  display: grid;
  grid-template: 1fr 1.25fr 1fr / 1fr 1.25fr 1fr;
  border-radius: 50%;
  background: rgba(22, 18, 38, 0.95);
  border: 1px solid rgba(255, 255, 255, 0.08);
  box-shadow: 0 10px 40px rgba(0, 0, 0, 0.4);
}
.dpad.dim {
  opacity: 0.4;
  pointer-events: none;
}
.arrow {
  display: grid;
  place-items: center;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
}
.arrow:active {
  color: #fff;
}
.up {
  grid-area: 1 / 2;
}
.left {
  grid-area: 2 / 1;
}
.right {
  grid-area: 2 / 3;
}
.down {
  grid-area: 3 / 2;
}
.ok {
  grid-area: 2 / 2;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: #2a2147;
  font-size: 18px;
  font-weight: 600;
  letter-spacing: 0.08em;
  touch-action: none;
}
.ok:active {
  background: #6d4fc4;
}
.row3,
.media,
.volume {
  display: flex;
  justify-content: space-between;
  gap: 10px;
}
.row3 button {
  flex: 1;
  padding: 12px 0;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 18px;
  background: rgba(20, 16, 34, 0.85);
  color: inherit;
  font: inherit;
  font-size: 12px;
}
.media button,
.volume button {
  width: 56px;
  height: 56px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 50%;
  background: rgba(20, 16, 34, 0.85);
  color: inherit;
  font: inherit;
}
.media .play {
  width: 68px;
  height: 68px;
  border: 0;
  background: #b9a2fb;
  color: #1a1030;
}
.ten {
  font-size: 14px;
  font-weight: 600;
}
.volume .mute {
  flex: 1;
  width: auto;
  border-radius: 999px;
  display: flex;
  justify-content: center;
  gap: 8px;
}
.volume .mute.on {
  color: #fca5a5;
}
.kb {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  padding: 13px;
  border: 1px dashed rgba(255, 255, 255, 0.15);
  border-radius: 16px;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 14px;
}
.type {
  display: flex;
  gap: 8px;
}
.type input {
  flex: 1;
  min-width: 0;
  padding: 13px 14px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 14px;
  background: rgba(10, 8, 18, 0.7);
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  outline: none;
}
.type button {
  width: 48px;
  border: 0;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.08);
  color: inherit;
}
.type button[type="submit"] {
  background: #6d4fc4;
  color: #fff;
}
.err {
  margin: 0;
  text-align: center;
  color: #fca5a5;
  font-size: 13px;
}
.empty {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 14px;
  color: var(--lumna-muted);
  text-align: center;
}
</style>
