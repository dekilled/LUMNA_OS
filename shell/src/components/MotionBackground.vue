<script setup lang="ts">
// Fundo animado do LUMNA (desktop e tela de descanso):
// - none: a imagem parada;
// - zoom: a mesma imagem num zoom lento, indo e voltando;
// - slideshow: troca entre as imagens com fade, cada uma com um zoom lento numa direção diferente.
import { convertFileSrc } from "@tauri-apps/api/core";
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import type { WallpaperMotion } from "../system";

const props = withDefaults(
  defineProps<{
    /** Imagem principal (a escolhida). */
    current: string | null;
    /** Todas as imagens, para a troca. */
    images?: string[];
    mode: WallpaperMotion;
    /** Segundos de cada imagem na troca. */
    interval?: number;
  }>(),
  { images: () => [], interval: 60 },
);

interface Slide {
  id: number;
  src: string;
  /** Direção do zoom (varia a cada imagem). */
  origin: string;
}

const ORIGINS = ["30% 40%", "70% 35%", "50% 60%", "35% 65%", "65% 55%"];
const slides = ref<Slide[]>([]);
let counter = 0;
let timer: number | undefined;
let order: string[] = [];

function push(path: string) {
  const src = convertFileSrc(path);
  // Só entra depois de carregada, para o fade não mostrar um buraco.
  const img = new Image();
  img.onload = () => {
    counter += 1;
    slides.value = [...slides.value.slice(-1), { id: counter, src, origin: ORIGINS[counter % ORIGINS.length] }];
  };
  img.src = src;
}

function shuffled(list: string[]) {
  const copy = [...list];
  for (let i = copy.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [copy[i], copy[j]] = [copy[j], copy[i]];
  }
  return copy;
}

function next() {
  if (!order.length) order = shuffled(props.images.length ? props.images : props.current ? [props.current] : []);
  const path = order.shift();
  if (path) push(path);
}

function restart() {
  window.clearInterval(timer);
  timer = undefined;
  slides.value = [];
  order = [];
  if (props.mode === "slideshow" && props.images.length > 1) {
    // Começa pela escolhida e segue embaralhado.
    if (props.current) push(props.current);
    else next();
    order = shuffled(props.images.filter((p) => p !== props.current));
    timer = window.setInterval(next, props.interval * 1000);
  } else if (props.current) {
    push(props.current);
  }
}

const zoomSeconds = computed(() => (props.mode === "slideshow" ? props.interval + 4 : 40));

watch(() => [props.current, props.mode, props.images.join("|"), props.interval], restart);
onMounted(restart);
onUnmounted(() => window.clearInterval(timer));
</script>

<template>
  <div class="motion" :class="`mode-${mode}`" :style="{ '--zoom': `${zoomSeconds}s` }">
    <TransitionGroup name="fade">
      <img
        v-for="s in slides"
        :key="s.id"
        class="slide"
        :src="s.src"
        :style="{ transformOrigin: s.origin }"
        alt=""
        draggable="false"
      />
    </TransitionGroup>
  </div>
</template>

<style scoped>
.motion {
  position: absolute;
  inset: 0;
  overflow: hidden;
  background: #120c24;
}

.slide {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  user-select: none;
  will-change: transform, opacity;
}

/* Zoom lento indo e voltando (a mesma imagem). */
.mode-zoom .slide {
  animation: breathe var(--zoom) ease-in-out infinite alternate;
}
@keyframes breathe {
  from {
    transform: scale(1);
  }
  to {
    transform: scale(1.12);
  }
}

/* Cada imagem da troca entra num zoom que não volta (a próxima chega antes de acabar). */
.mode-slideshow .slide {
  animation: drift var(--zoom) linear forwards;
}
@keyframes drift {
  from {
    transform: scale(1.02);
  }
  to {
    transform: scale(1.14);
  }
}

.fade-enter-active,
.fade-leave-active {
  transition: opacity 1.8s ease;
}
.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}
</style>
