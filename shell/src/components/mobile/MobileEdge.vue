<script setup lang="ts">
// Celular: a faixa fina numa borda lateral (só com um app aberto). Deslizar dela na diagonal,
// pra dentro e pra baixo, volta pra tela Início. O toque fica preso nesta superfície até soltar,
// então dá pra medir o gesto todo mesmo saindo da faixa.
import { haptic } from "../../system";
import { showHome } from "./apps";

const props = defineProps<{ side: "left" | "right" }>();
const MIN = 45;
let start: { x: number; y: number; id: number } | null = null;
let armed = false;

function down(e: PointerEvent) {
  start = { x: e.screenX, y: e.screenY, id: e.pointerId };
  armed = false;
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}
// passou do ponto: uma vibraçãozinha avisa que soltar volta pra Início
function move(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const dx = (e.screenX - start.x) * (props.side === "left" ? 1 : -1);
  const dy = e.screenY - start.y;
  const ok = dx >= MIN && dy >= MIN;
  if (ok && !armed) haptic(18).catch(() => {});
  armed = ok;
}
function up(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const dx = (e.screenX - start.x) * (props.side === "left" ? 1 : -1);
  const dy = e.screenY - start.y;
  start = null;
  // pra dentro e pra baixo, nenhum dos dois quase zero (diagonal de verdade)
  if (dx >= MIN && dy >= MIN && dy < dx * 3 && dx < dy * 3) {
    haptic(30).catch(() => {});
    showHome();
  }
}
</script>

<template>
  <div class="edge" @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="start = null" />
</template>

<style scoped>
.edge {
  height: 100vh;
  touch-action: none;
}
</style>
