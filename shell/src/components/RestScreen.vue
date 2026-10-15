<script setup lang="ts">
// Tela de descanso: o fundo animado trocando as imagens da grade, sem ícones, e um relógio grande
// com a data no canto inferior direito. Qualquer movimento, clique ou tecla sai dela.
import { computed, onMounted, onUnmounted, ref } from "vue";
import MotionBackground from "./MotionBackground.vue";
import { desktopWallpaper, on, restOff, wallpaperList } from "../system";

const shown = ref(false);
const now = ref(new Date());
const current = ref<string | null>(null);
const images = ref<string[]>([]);
/** Logo depois de aparecer, um tremor do mouse não conta (a camada nasce embaixo do cursor). */
let since = 0;

const time = computed(() => new Intl.DateTimeFormat("pt-BR", { hour: "2-digit", minute: "2-digit" }).format(now.value));
const date = computed(() => {
  const text = new Intl.DateTimeFormat("pt-BR", { weekday: "long", day: "numeric", month: "long" }).format(now.value);
  return text.charAt(0).toUpperCase() + text.slice(1);
});

async function show(on: boolean) {
  shown.value = on;
  if (!on) return;
  since = Date.now();
  now.value = new Date();
  current.value = await desktopWallpaper().catch(() => null);
  images.value = (await wallpaperList().catch(() => null))?.items.map((i) => i.path) ?? [];
}

function wake() {
  if (shown.value && Date.now() - since > 800) restOff().catch(() => {});
}

let clock: number | undefined;
const stops: (() => void)[] = [];

onMounted(async () => {
  clock = window.setInterval(() => (now.value = new Date()), 1000);
  stops.push(await on<boolean>("lumna://rest", show));
  for (const kind of ["pointermove", "pointerdown", "keydown", "wheel"]) {
    window.addEventListener(kind, wake);
    stops.push(() => window.removeEventListener(kind, wake));
  }
});

onUnmounted(() => {
  window.clearInterval(clock);
  stops.forEach((stop) => stop());
});
</script>

<template>
  <div class="rest" :class="{ shown }">
    <MotionBackground v-if="shown" :current="current" :images="images" mode="slideshow" :interval="30" />
    <div class="shade" />
    <div class="clock">
      <span class="time">{{ time }}</span>
      <span class="date">{{ date }}</span>
    </div>
  </div>
</template>

<style scoped>
.rest {
  position: fixed;
  inset: 0;
  background: #120c24;
  cursor: none;
  opacity: 0;
  transition: opacity 1.2s ease;
}
.rest.shown {
  opacity: 1;
}

/* Escurece o canto do relógio para ler em qualquer imagem. */
.shade {
  position: absolute;
  inset: 0;
  background: radial-gradient(ellipse at 100% 100%, rgba(8, 5, 18, 0.72) 0%, rgba(8, 5, 18, 0.25) 45%, transparent 70%);
}

.clock {
  position: absolute;
  right: 72px;
  bottom: 64px;
  display: flex;
  flex-direction: column;
  align-items: flex-end;
  color: #ece8ff;
  text-shadow: 0 2px 24px rgba(0, 0, 0, 0.45);
  user-select: none;
}
.time {
  font-size: 128px;
  font-weight: 600;
  line-height: 1;
  letter-spacing: -0.02em;
  font-variant-numeric: tabular-nums;
}
.date {
  margin-top: 10px;
  font-size: 24px;
  font-weight: 400;
  color: rgba(236, 232, 255, 0.82);
}
</style>
