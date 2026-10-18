<script setup lang="ts">
// Celular: a barrinha de baixo, a mesma em toda a interface (na navegação e embaixo do teclado,
// no mesmo lugar e tamanho). A faixa inteira (26 px) é a área de toque.
// Toque ou deslize curto pra cima = teclado (abre/fecha); deslize longo = apps recentes; segurar
// parado = a Emma (o assistente, como no Android). A barrinha estica enquanto o dedo sobe e vibra
// quando passa pro "longo".
import { computed, ref } from "vue";
import { haptic, keyboardToggle, mobilePanel, recentsSnapshot, toggleEmma } from "../../system";

defineProps<{ hidden?: boolean }>();

const SHORT = 24;
const LONG = 140;
const HOLD = 450;
const lift = ref(0);
const holding = ref(false);
let start: { y: number; id: number } | null = null;
let swallow = false;
let holdTimer = 0;
const long = computed(() => lift.value >= LONG);

async function openRecents() {
  await recentsSnapshot();
  await mobilePanel("mobile-recents", true);
}
function down(e: PointerEvent) {
  if (start) return;
  start = { y: e.clientY, id: e.pointerId };
  lift.value = 0;
  window.clearTimeout(holdTimer);
  holdTimer = window.setTimeout(() => {
    if (!start || lift.value >= SHORT) return;
    // segurou sem subir: a Emma (o toque que solta depois não abre o teclado)
    start = null;
    swallow = true;
    window.setTimeout(() => (swallow = false), 600);
    holding.value = true;
    window.setTimeout(() => (holding.value = false), 300);
    haptic(30).catch(() => {});
    toggleEmma();
  }, HOLD);
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}
function move(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const was = long.value;
  lift.value = Math.max(0, start.y - e.clientY);
  if (lift.value >= SHORT) window.clearTimeout(holdTimer);
  if (long.value && !was) haptic(25).catch(() => {});
}
function up(e: PointerEvent) {
  window.clearTimeout(holdTimer);
  if (!start || e.pointerId !== start.id) return;
  start = null;
  const lifted = lift.value;
  lift.value = 0;
  if (lifted < SHORT) return; // toque: o clique cuida
  swallow = true;
  window.setTimeout(() => (swallow = false), 400);
  if (lifted >= LONG) openRecents();
  else keyboardToggle();
}
function cancel() {
  window.clearTimeout(holdTimer);
  start = null;
  lift.value = 0;
}
function click() {
  if (swallow) {
    swallow = false;
    return;
  }
  keyboardToggle();
}
</script>

<template>
  <button
    class="strip"
    aria-label="Teclado (deslize longo: apps recentes; segure: Emma)"
    @pointerdown="down"
    @pointermove="move"
    @pointerup="up"
    @pointercancel="cancel"
    @click="click"
  >
    <div class="handle" :class="{ long: long || holding, hidden }" :style="{ width: `${120 + Math.min(lift, LONG) * 0.5}px` }" />
  </button>
</template>

<style scoped>
.strip {
  flex: none;
  width: 100%;
  height: 26px;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 0;
  border: 0;
  background: none;
  touch-action: none;
}
.handle {
  width: 120px;
  height: 4px;
  border-radius: 2px;
  background: rgba(236, 232, 255, 0.7);
  transition: background 120ms ease;
}
.handle.hidden {
  opacity: 0;
}
.handle.long {
  background: var(--lumna-violet-soft);
}
</style>
