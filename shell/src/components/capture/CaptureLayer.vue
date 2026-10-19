<script setup lang="ts">
// Captura de tela: a foto congelada da tela inteira, com o retângulo da área escolhida. Arrastar
// fora dele desenha outro; arrastar dentro move; as alças (cantos e lados) esticam. "Tela toda"
// volta pra tela inteira. Salvar guarda em Imagens/Capturas; Copiar põe na área de transferência.
// A área vai pro Rust em frações da foto: vale igual no celular (escala 2,5) e no PC.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { artUrl, captureCancel, captureSave, haptic, on, type Frozen } from "../../system";

interface Area {
  x: number;
  y: number;
  w: number;
  h: number;
}

const shot = ref<Frozen | null>(null);
const stamp = ref(0);
const area = ref<Area>({ x: 0, y: 0, w: 1, h: 1 });
const busy = ref(false);
const error = ref("");
const root = ref<HTMLElement>();

/** Menor área (em px da tela) que dá pra escolher. */
const MIN = 24;

const full = computed(() => area.value.x === 0 && area.value.y === 0 && area.value.w === 1 && area.value.h === 1);
const sizeText = computed(() => {
  if (!shot.value) return "";
  const w = Math.max(1, Math.round(area.value.w * shot.value.width));
  const h = Math.max(1, Math.round(area.value.h * shot.value.height));
  return `${w} × ${h}`;
});
const box = computed(() => ({
  left: `${area.value.x * 100}%`,
  top: `${area.value.y * 100}%`,
  width: `${area.value.w * 100}%`,
  height: `${area.value.h * 100}%`,
}));

type Mode = "new" | "move" | "n" | "s" | "e" | "w" | "ne" | "nw" | "se" | "sw";
let drag: { mode: Mode; id: number; x: number; y: number; start: Area } | null = null;

function size() {
  const r = root.value?.getBoundingClientRect();
  return { w: r?.width || window.innerWidth, h: r?.height || window.innerHeight };
}

function down(e: PointerEvent, mode: Mode) {
  if (busy.value || drag) return;
  // com a tela toda escolhida não há o que mover: arrastar desenha a área
  if (mode === "move" && full.value) mode = "new";
  e.stopPropagation();
  const { w, h } = size();
  const x = e.clientX / w;
  const y = e.clientY / h;
  drag = { mode, id: e.pointerId, x, y, start: mode === "new" ? { x, y, w: 0, h: 0 } : { ...area.value } };
  if (mode === "new") area.value = { x, y, w: 0, h: 0 };
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}

function move(e: PointerEvent) {
  if (!drag || e.pointerId !== drag.id) return;
  const { w: sw, h: sh } = size();
  const px = Math.min(1, Math.max(0, e.clientX / sw));
  const py = Math.min(1, Math.max(0, e.clientY / sh));
  const dx = px - drag.x;
  const dy = py - drag.y;
  const s = drag.start;
  const minW = MIN / sw;
  const minH = MIN / sh;
  let { x, y, w, h } = s;
  switch (drag.mode) {
    case "new":
      x = Math.min(drag.x, px);
      y = Math.min(drag.y, py);
      w = Math.abs(px - drag.x);
      h = Math.abs(py - drag.y);
      break;
    case "move":
      x = Math.min(1 - s.w, Math.max(0, s.x + dx));
      y = Math.min(1 - s.h, Math.max(0, s.y + dy));
      break;
    default: {
      const m = drag.mode;
      if (m.includes("w")) {
        x = Math.min(s.x + s.w - minW, Math.max(0, s.x + dx));
        w = s.x + s.w - x;
      }
      if (m.includes("e")) w = Math.min(1 - s.x, Math.max(minW, s.w + dx));
      if (m.includes("n")) {
        y = Math.min(s.y + s.h - minH, Math.max(0, s.y + dy));
        h = s.y + s.h - y;
      }
      if (m.includes("s")) h = Math.min(1 - s.y, Math.max(minH, s.h + dy));
    }
  }
  area.value = { x, y, w, h };
}

function up(e: PointerEvent) {
  if (!drag || e.pointerId !== drag.id) return;
  const mode = drag.mode;
  drag = null;
  const { w, h } = size();
  // um toque sem arrastar (ou área minúscula) não vale como área: volta a anterior/a tela toda
  if (mode === "new" && (area.value.w * w < MIN || area.value.h * h < MIN)) area.value = { x: 0, y: 0, w: 1, h: 1 };
}

function wholeScreen() {
  area.value = { x: 0, y: 0, w: 1, h: 1 };
}

async function finish(save: boolean, copy: boolean) {
  if (busy.value) return;
  busy.value = true;
  error.value = "";
  haptic(25).catch(() => {});
  try {
    await captureSave(area.value, save, copy);
    shot.value = null;
  } catch (e) {
    error.value = String(e);
  } finally {
    busy.value = false;
  }
}

function cancel() {
  shot.value = null;
  captureCancel().catch(() => {});
}

function onKey(e: KeyboardEvent) {
  if (!shot.value) return;
  if (e.key === "Escape") cancel();
  else if (e.key === "Enter") finish(true, false);
  else if (e.key === "c" && (e.ctrlKey || e.metaKey)) finish(false, true);
}

const stops: (() => void)[] = [];
onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(
    await on<Frozen>("lumna://capture", (f) => {
      // a mesma foto de antes no cache do WebKit: o carimbo força ler a nova
      stamp.value = Date.now();
      area.value = { x: 0, y: 0, w: 1, h: 1 };
      error.value = "";
      busy.value = false;
      shot.value = f;
    }),
  );
});
onUnmounted(() => stops.forEach((s) => s()));

const HANDLES: Mode[] = ["nw", "n", "ne", "e", "se", "s", "sw", "w"];
</script>

<template>
  <div v-if="shot" ref="root" class="capture" @pointerdown="down($event, 'new')" @pointermove="move" @pointerup="up" @pointercancel="up">
    <img class="frozen" :src="`${artUrl(shot.path)}?t=${stamp}`" alt="" draggable="false" />
    <div class="area" :class="{ full }" :style="box" @pointerdown="down($event, 'move')">
      <span class="size">{{ sizeText }}</span>
      <span
        v-for="h in HANDLES"
        :key="h"
        class="handle"
        :class="h"
        @pointerdown="down($event, h)"
      />
    </div>

    <div class="bar" @pointerdown.stop>
      <button class="round" aria-label="Cancelar" @click="cancel()"><Icon name="close" :size="22" /></button>
      <button class="pill" :disabled="full" @click="wholeScreen()"><Icon name="screens" :size="18" /> Tela toda</button>
      <button class="pill" :disabled="busy" @click="finish(false, true)"><Icon name="copy" :size="18" /> Copiar</button>
      <button class="pill primary" :disabled="busy" @click="finish(true, false)"><Icon name="check" :size="18" /> Salvar</button>
    </div>
    <p v-if="error" class="error" @pointerdown.stop>{{ error }}</p>
    <p v-else class="hint">Arraste pra escolher a área</p>
  </div>
</template>

<style scoped>
.capture {
  position: fixed;
  inset: 0;
  overflow: hidden;
  background: #000;
  cursor: crosshair;
  touch-action: none;
  user-select: none;
}
.frozen {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: fill;
  pointer-events: none;
}
.area {
  position: absolute;
  box-sizing: border-box;
  border: 2px solid #a78bfa;
  /* tudo fora da área escurece */
  box-shadow: 0 0 0 200vmax rgba(4, 3, 10, 0.55);
  cursor: move;
}
.area.full {
  border-color: rgba(167, 139, 250, 0.6);
  box-shadow: none;
}
.size {
  position: absolute;
  left: 8px;
  top: 8px;
  padding: 3px 8px;
  border-radius: 8px;
  background: rgba(13, 11, 20, 0.8);
  color: #ece8ff;
  font-size: 12px;
  font-variant-numeric: tabular-nums;
  pointer-events: none;
}
.handle {
  position: absolute;
  width: 26px;
  height: 26px;
  margin: -13px 0 0 -13px;
  touch-action: none;
}
.handle::after {
  content: "";
  position: absolute;
  inset: 8px;
  border-radius: 50%;
  background: #fff;
  box-shadow: 0 0 0 2px #7c3aed;
}
.nw { left: 0; top: 0; cursor: nwse-resize; }
.n { left: 50%; top: 0; cursor: ns-resize; }
.ne { left: 100%; top: 0; cursor: nesw-resize; }
.e { left: 100%; top: 50%; cursor: ew-resize; }
.se { left: 100%; top: 100%; cursor: nwse-resize; }
.s { left: 50%; top: 100%; cursor: ns-resize; }
.sw { left: 0; top: 100%; cursor: nesw-resize; }
.w { left: 0; top: 50%; cursor: ew-resize; }
.bar {
  position: absolute;
  left: 50%;
  bottom: max(28px, 4vh);
  transform: translateX(-50%);
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px;
  border: 1px solid rgba(167, 139, 250, 0.25);
  border-radius: 999px;
  background: rgba(19, 16, 29, 0.92);
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
  cursor: default;
}
.round,
.pill {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 7px;
  height: 44px;
  border: 0;
  border-radius: 999px;
  background: rgba(167, 139, 250, 0.12);
  color: #ece8ff;
  font-size: 14px;
  white-space: nowrap;
}
.round {
  width: 44px;
}
.pill {
  padding: 0 14px;
}
.pill.primary {
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
  font-weight: 600;
}
button:disabled {
  opacity: 0.45;
}
.hint,
.error {
  position: absolute;
  left: 50%;
  top: max(48px, 6vh);
  transform: translateX(-50%);
  margin: 0;
  padding: 6px 12px;
  border-radius: 999px;
  background: rgba(13, 11, 20, 0.8);
  color: #ece8ff;
  font-size: 13px;
  pointer-events: none;
  white-space: nowrap;
}
.error {
  color: #fca5a5;
  white-space: normal;
  max-width: 90vw;
  text-align: center;
}
</style>
