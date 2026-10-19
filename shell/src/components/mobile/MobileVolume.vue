<script setup lang="ts">
// Celular: o volume, como nos esboços. Apertando as teclas aparece a barra do lado (o volume da
// saída, o mudo, a saída e o "Mixer"); ela some sozinha. O Mixer de áudio tem a saída, o volume
// global e o de cada app tocando (com o ícone dele), "Redefinir volumes" e os ajustes de áudio.
// Tocar fora fecha.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  apps as listApps,
  audioSetDefault,
  audioSetVolume,
  audioState,
  audioToggleMute,
  haptic,
  layerHide,
  lockState,
  on,
  settingsOpen,
  type App,
  type AudioNode,
  type AudioState,
} from "../../system";

const mode = ref<"osd" | "mixer" | null>(null);
const audio = ref<AudioState | null>(null);
const installed = ref<App[]>([]);
const locked = ref(false);
const outputsOpen = ref(false);
const output = computed(() => audio.value?.outputs.find((o) => o.default) ?? audio.value?.outputs[0] ?? null);

// ---- os apps do mixer: nome e ícone pelo app instalado ----

const OWN: [RegExp, string, string][] = [
  [/^emma$|pw-play|paplay/i, "Emma", "lumna-emma"],
  [/lumna music/i, "Música", "lumna-music"],
];
function appOf(n: AudioNode): { name: string; icon: string | null } {
  const own = OWN.find(([re]) => re.test(n.name));
  const key = (own?.[2] ?? n.name).toLowerCase();
  const app = installed.value.find(
    (a) => a.id.toLowerCase().replace(/\.desktop$/, "") === key || a.name.toLowerCase() === key || a.wm_class?.toLowerCase() === key,
  );
  return { name: own?.[1] ?? app?.name ?? n.name, icon: app?.icon ?? null };
}
/** "Sons do sistema" no fim (os avisos), o resto na ordem do PipeWire. */
const rows = computed(() => audio.value?.apps ?? []);

const outputIcon = (name = "") => (/bluetooth|fone|headphone|headset|jbl|buds/i.test(name) ? "headphones" : "speaker");
const outputName = (name = "") => (/speaker|alto.?falante|built.?in|interno|analog/i.test(name) ? "Alto-falante" : name);

// ---- abrir, atualizar, fechar ----

let hideTimer = 0;
let poll = 0;
async function refresh() {
  if (dragging.value) return;
  const a = await audioState().catch(() => null);
  if (a && !dragging.value) audio.value = a;
}
function armHide() {
  clearTimeout(hideTimer);
  if (mode.value === "osd") hideTimer = window.setTimeout(close, 3000);
}
async function show(m: "osd" | "mixer") {
  // tecla apertada com o mixer aberto: ele fica (só atualiza)
  if (!(m === "osd" && mode.value === "mixer")) mode.value = m;
  outputsOpen.value = false;
  locked.value = (await lockState().catch(() => null))?.locked ?? false;
  await refresh();
  armHide();
  clearInterval(poll);
  poll = window.setInterval(refresh, mode.value === "mixer" ? 1500 : 600);
  if (!installed.value.length) installed.value = await listApps().catch(() => []);
}
function close() {
  clearTimeout(hideTimer);
  clearInterval(poll);
  mode.value = null;
  window.setTimeout(() => !mode.value && layerHide().catch(() => {}), 180);
}
function openMixer() {
  if (locked.value) return;
  haptic(10).catch(() => {});
  show("mixer");
}

// ---- mudar ----

const dragging = ref(false);
const timers = new Map<number, number>();
function setVolume(n: AudioNode, value: number) {
  n.volume = Math.round(Math.max(0, Math.min(100, value)));
  if (n.muted && n.volume > 0) {
    n.muted = false;
    audioToggleMute(n.id).catch(() => {});
  }
  clearTimeout(timers.get(n.id));
  timers.set(
    n.id,
    window.setTimeout(() => audioSetVolume(n.id, n.volume).catch(() => {}), 40),
  );
  armHide();
}
async function mute(n: AudioNode) {
  haptic(10).catch(() => {});
  n.muted = !n.muted;
  await audioToggleMute(n.id).catch(() => {});
  armHide();
}
async function pickOutput(o: AudioNode) {
  outputsOpen.value = false;
  await audioSetDefault(o.id).catch(() => {});
  await refresh();
}
async function resetVolumes() {
  haptic(15).catch(() => {});
  for (const n of rows.value) {
    n.volume = 100;
    await audioSetVolume(n.id, 100).catch(() => {});
    if (n.muted) {
      n.muted = false;
      await audioToggleMute(n.id).catch(() => {});
    }
  }
}
function soundSettings() {
  close();
  settingsOpen("som").catch(() => {});
}

// a barra vertical da tecla: arrastar (ou tocar) muda o volume
const track = ref<HTMLElement>();
let dragId: number | null = null;
function verticalAt(e: PointerEvent) {
  const r = track.value?.getBoundingClientRect();
  if (!r || !output.value) return;
  setVolume(output.value, ((r.bottom - e.clientY) / r.height) * 100);
}
function vdown(e: PointerEvent) {
  // o WebKitGTK manda o mesmo dedo também como "mouse": vale o primeiro
  if (dragId != null) return;
  dragId = e.pointerId;
  dragging.value = true;
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
  verticalAt(e);
}
function vmove(e: PointerEvent) {
  if (e.pointerId === dragId) verticalAt(e);
}
function vup(e: PointerEvent) {
  if (e.pointerId !== dragId) return;
  dragId = null;
  dragging.value = false;
}
function slide(n: AudioNode, e: Event) {
  dragging.value = true;
  setVolume(n, +(e.target as HTMLInputElement).value);
}

const stops: (() => void)[] = [];
onMounted(async () => {
  stops.push(await on<"osd" | "mixer">("lumna://volume-panel", (m) => show(m)));
});
onUnmounted(() => {
  clearInterval(poll);
  stops.forEach((s) => s());
});
</script>

<template>
  <div class="volume" :class="`is-${mode ?? 'off'}`" @pointerdown.self="close()">
    <!-- a barra do lado (teclas de volume) -->
    <template v-if="mode === 'osd' && output">
      <button class="chip" @click="openMixer()">
        <Icon :name="outputIcon(output.name)" :size="20" />
        <span>{{ outputName(output.name) }}</span>
        <Icon v-if="!locked" name="forward" :size="16" />
      </button>
      <section class="osd">
        <Icon class="top-icon" :name="output.muted || !output.volume ? 'mute' : 'volume'" :size="30" />
        <span class="label">Volume</span>
        <div ref="track" class="vtrack" @pointerdown="vdown" @pointermove="vmove" @pointerup="vup" @pointercancel="vup">
          <div class="vfill" :class="{ muted: output.muted }" :style="{ height: `${Math.min(100, output.volume)}%` }">
            <span class="knob" />
          </div>
        </div>
        <span class="pct">{{ output.muted ? "Mudo" : `${Math.min(100, output.volume)}%` }}</span>
        <button class="mute" :class="{ on: output.muted }" :aria-label="output.muted ? 'Ligar o som' : 'Silenciar'" @click="mute(output)">
          <Icon name="volume-off" :size="24" />
        </button>
        <template v-if="!locked">
          <div class="sep" />
          <button class="mixer-btn" @click="openMixer()"><Icon name="more" :size="22" /><span>Mixer</span></button>
        </template>
      </section>
    </template>

    <!-- o mixer de áudio -->
    <section v-else-if="mode === 'mixer'" class="mixer">
      <header>
        <Icon name="volume" :size="32" />
        <h2>Mixer de áudio</h2>
        <button class="x" aria-label="Fechar" @click="close()"><Icon name="close" :size="24" /></button>
      </header>

      <div class="line out">
        <span class="muted-label">Saída</span>
        <div class="out-pick">
          <button class="chip inline" @click="outputsOpen = !outputsOpen">
            <Icon :name="outputIcon(output?.name)" :size="20" />
            <span>{{ outputName(output?.name) || "Nenhuma" }}</span>
            <Icon :name="outputsOpen ? 'chevron-down' : 'forward'" :size="16" />
          </button>
          <ul v-if="outputsOpen" class="outs">
            <li v-for="o in audio?.outputs ?? []" :key="o.id">
              <button :class="{ on: o.default }" @click="pickOutput(o)">
                <Icon :name="outputIcon(o.name)" :size="18" /> <span>{{ outputName(o.name) }}</span>
                <Icon v-if="o.default" name="check" :size="16" />
              </button>
            </li>
          </ul>
        </div>
      </div>

      <div v-if="output" class="global">
        <span class="muted-label">Volume global</span>
        <div class="slider-row">
          <input
            type="range"
            min="0"
            max="100"
            :value="Math.min(100, output.volume)"
            :style="{ '--p': `${Math.min(100, output.volume)}%` }"
            aria-label="Volume global"
            @input="slide(output, $event)"
            @change="dragging = false"
          />
          <span class="pct">{{ Math.min(100, output.volume) }}%</span>
        </div>
      </div>

      <div class="apps">
        <div v-for="n in rows" :key="n.id" class="app">
          <span class="app-icon">
            <img v-if="appOf(n).icon" :src="appOf(n).icon!" alt="" />
            <Icon v-else name="app" :size="22" />
          </span>
          <span class="app-name">{{ appOf(n).name }}</span>
          <button class="round" :class="{ on: n.muted }" :aria-label="n.muted ? 'Ligar o som' : 'Silenciar'" @click="mute(n)">
            <Icon :name="n.muted ? 'mute' : 'volume'" :size="20" />
          </button>
          <input
            type="range"
            min="0"
            max="100"
            :value="Math.min(100, n.volume)"
            :style="{ '--p': `${n.muted ? 0 : Math.min(100, n.volume)}%` }"
            :aria-label="`Volume de ${appOf(n).name}`"
            @input="slide(n, $event)"
            @change="dragging = false"
          />
          <span class="pct">{{ n.muted ? "0" : Math.min(100, n.volume) }}%</span>
        </div>
        <p v-if="!rows.length" class="none">Nenhum app tocando som agora. Quando tocar, ele aparece aqui com o volume dele.</p>
      </div>

      <footer>
        <button class="foot" :disabled="!rows.length" @click="resetVolumes()"><Icon name="refresh" :size="22" /> Redefinir volumes</button>
        <span class="vsep" />
        <button class="foot" @click="soundSettings()"><Icon name="settings" :size="22" /> Ajustes de áudio</button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.volume {
  position: fixed;
  inset: 0;
  color: var(--lumna-text);
  user-select: none;
}
.volume.is-mixer {
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0 20px;
  background: rgba(4, 3, 10, 0.4);
}

/* ---- a barra do lado ---- */
.osd {
  position: absolute;
  right: 10px;
  top: 50%;
  transform: translateY(-46%);
  width: 100px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 18px 0 14px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 28px;
  background: rgba(24, 21, 34, 0.94);
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.5);
  animation: slide-in 0.16s ease-out;
}
@keyframes slide-in {
  from {
    opacity: 0;
    transform: translate(20px, -46%);
  }
}
.top-icon {
  color: #b9a2fb;
}
.label {
  margin-top: -6px;
  font-size: 13px;
  font-weight: 600;
}
.vtrack {
  position: relative;
  width: 40px;
  height: 200px;
  margin-top: 6px;
  overflow: hidden;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.1);
  touch-action: none;
}
.vfill {
  position: absolute;
  inset: auto 0 0;
  border-radius: 0 0 20px 20px;
  background: #a78bfa;
  transition: height 0.08s;
}
.vfill.muted {
  background: rgba(167, 139, 250, 0.35);
}
.knob {
  position: absolute;
  left: 50%;
  top: 0;
  width: 26px;
  height: 26px;
  transform: translate(-50%, -50%);
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.35);
}
.pct {
  font-size: 14px;
  font-variant-numeric: tabular-nums;
}
.mute {
  width: 50px;
  height: 50px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.07);
  color: #cfc6ee;
}
.mute.on {
  background: #a78bfa;
  color: #1a1030;
}
.sep {
  width: 70%;
  height: 1px;
  background: rgba(255, 255, 255, 0.1);
}
.mixer-btn {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 0;
  border: 0;
  background: none;
  color: #cfc6ee;
  font: inherit;
  font-size: 12px;
}
.chip {
  position: absolute;
  right: 120px;
  top: calc(50% + 108px);
  display: flex;
  align-items: center;
  gap: 10px;
  height: 46px;
  padding: 0 14px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 999px;
  background: rgba(24, 21, 34, 0.94);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
  animation: slide-in-chip 0.16s ease-out;
}
@keyframes slide-in-chip {
  from {
    opacity: 0;
  }
}
.chip :deep(svg) {
  color: #b9a2fb;
}
.chip.inline {
  position: static;
  height: 50px;
  padding: 0 16px;
  animation: none;
  background: rgba(255, 255, 255, 0.04);
}

/* ---- o mixer ---- */
.mixer {
  box-sizing: border-box;
  width: 100%;
  max-width: 420px;
  display: flex;
  flex-direction: column;
  padding: 22px 20px 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 26px;
  background: rgba(22, 19, 31, 0.97);
  box-shadow: 0 24px 60px rgba(0, 0, 0, 0.55);
  animation: pop 0.16s ease-out;
}
@keyframes pop {
  from {
    opacity: 0;
    transform: scale(0.97);
  }
}
.mixer header {
  display: flex;
  align-items: center;
  gap: 16px;
  color: #b9a2fb;
}
.mixer h2 {
  flex: 1;
  margin: 0;
  color: var(--lumna-text);
  font-size: 22px;
  font-weight: 600;
}
.x {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: #b9a2fb;
}
.line {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-top: 22px;
}
.muted-label {
  color: #cfc6ee;
  font-size: 15px;
}
.out-pick {
  position: relative;
}
.outs {
  position: absolute;
  right: 0;
  top: calc(100% + 6px);
  z-index: 2;
  min-width: 220px;
  margin: 0;
  padding: 6px;
  list-style: none;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 14px;
  background: #1c1828;
  box-shadow: 0 14px 30px rgba(0, 0, 0, 0.5);
}
.outs button {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 10px;
  height: 44px;
  padding: 0 10px;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.outs button span {
  flex: 1;
  text-align: left;
}
.outs button.on {
  background: rgba(167, 139, 250, 0.14);
}
.global {
  margin-top: 18px;
  padding-bottom: 16px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
}
.slider-row {
  display: flex;
  align-items: center;
  gap: 14px;
  margin-top: 8px;
}
.slider-row .pct,
.app .pct {
  width: 44px;
  text-align: right;
  font-size: 15px;
}
input[type="range"] {
  -webkit-appearance: none;
  appearance: none;
  flex: 1;
  min-width: 0;
  height: 30px;
  background: transparent;
  touch-action: pan-y;
}
input[type="range"]::-webkit-slider-runnable-track {
  height: 12px;
  border-radius: 6px;
  background: linear-gradient(90deg, #a78bfa var(--p, 0%), rgba(255, 255, 255, 0.1) var(--p, 0%));
}
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 28px;
  height: 28px;
  margin-top: -8px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 2px 8px rgba(0, 0, 0, 0.35);
}
.apps {
  display: flex;
  flex-direction: column;
}
.app {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 62px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.06);
}
.app input[type="range"]::-webkit-slider-runnable-track {
  height: 10px;
}
.app input[type="range"]::-webkit-slider-thumb {
  width: 24px;
  height: 24px;
  margin-top: -7px;
}
.app-icon {
  flex: none;
  width: 34px;
  height: 34px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 9px;
  color: #b9a2fb;
}
.app-icon img {
  width: 100%;
  height: 100%;
  object-fit: contain;
}
.app-name {
  width: 92px;
  overflow: hidden;
  font-size: 15px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.round {
  flex: none;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.04);
  color: #cfc6ee;
}
.round.on {
  border-color: rgba(252, 165, 165, 0.4);
  color: #fca5a5;
}
.none {
  margin: 16px 0 6px;
  color: var(--lumna-muted);
  font-size: 14px;
  line-height: 1.5;
}
footer {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 16px;
}
.foot {
  flex: 1;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  height: 52px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  background: none;
  color: #c4b5fd;
  font: inherit;
  font-size: 14px;
}
.foot + .vsep + .foot {
  border-color: transparent;
}
.foot:disabled {
  opacity: 0.45;
}
.vsep {
  width: 1px;
  height: 40px;
  background: rgba(255, 255, 255, 0.1);
}
</style>
