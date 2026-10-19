<script setup lang="ts">
// Gravando a tela: a pílula no topo com o tempo; tocar para (o vídeo vai pra Vídeos/Gravações e
// o aviso abre ele na Galeria).
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { captureRecordStop, haptic, on } from "../../system";

const elapsed = ref(0);
const stopping = ref(false);
let stop: (() => void) | null = null;

const clock = (s: number) => `${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")}`;

async function finish() {
  if (stopping.value) return;
  stopping.value = true;
  haptic(25).catch(() => {});
  await captureRecordStop().catch(() => {});
  stopping.value = false;
}

onMounted(async () => {
  stop = await on<{ recording: boolean; elapsed: number }>("lumna://recording", (r) => {
    elapsed.value = r.elapsed;
    if (!r.recording) stopping.value = false;
  });
});
onUnmounted(() => stop?.());
</script>

<template>
  <button class="pill" :class="{ stopping }" @click="finish()">
    <span class="dot" />
    <span class="time">{{ stopping ? "Salvando…" : clock(elapsed) }}</span>
    <span class="stop"><Icon name="stop" :size="16" /></span>
  </button>
</template>

<style scoped>
.pill {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 6px 0 16px;
  border: 1px solid rgba(239, 68, 68, 0.45);
  border-radius: 999px;
  background: rgba(19, 16, 29, 0.94);
  color: #ece8ff;
  font-size: 15px;
  font-variant-numeric: tabular-nums;
}
.dot {
  width: 11px;
  height: 11px;
  border-radius: 50%;
  background: #ef4444;
  animation: pulse 1.2s infinite ease-in-out;
}
.time {
  flex: 1;
  text-align: left;
}
.stop {
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  border-radius: 50%;
  background: rgba(239, 68, 68, 0.85);
  color: #fff;
}
.stopping .dot {
  animation: none;
  opacity: 0.4;
}
@keyframes pulse {
  50% {
    opacity: 0.3;
  }
}
</style>
