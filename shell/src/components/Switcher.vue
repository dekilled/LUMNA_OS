<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import {
  changeLayout,
  fitLayerTo,
  layerHide,
  MODE_LABEL,
  modes,
  on,
  switchTo,
  type Layout,
  type Mode,
  type ModesSnapshot,
} from "../system";

const ORDER: Mode[] = ["desktop", "gaming", "media"];
const ICON: Record<Mode, string> = { desktop: "desktop", gaming: "gamepad", media: "play" };
/** Tempo na tela depois da última troca ou interação. */
const VISIBLE_MS = 1600;

const snapshot = ref<ModesSnapshot | null>(null);
const shown = ref(false);
const hovering = ref(false);
const root = ref<HTMLElement>();
let timer: number | undefined;

const current = computed<Mode>(() => {
  const s = snapshot.value;
  if (!s) return "desktop";
  const name = s.focused ?? Object.keys(s.modes)[0];
  return (name && s.modes[name]) || "desktop";
});
const index = computed(() => ORDER.indexOf(current.value));

function scheduleHide() {
  window.clearTimeout(timer);
  timer = window.setTimeout(() => {
    if (hovering.value) return scheduleHide();
    shown.value = false;
    // Espera a animação de saída antes de esconder a camada.
    window.setTimeout(() => !shown.value && layerHide(), 200);
  }, VISIBLE_MS);
}

function pick(mode: Mode) {
  scheduleHide();
  if (mode !== current.value) switchTo(mode);
}

function pickLayout(layout: Layout) {
  scheduleHide();
  if (layout !== snapshot.value?.layout) changeLayout(layout);
}

const stops: (() => void)[] = [];

onMounted(async () => {
  snapshot.value = await modes();
  stops.push(await on<ModesSnapshot>("lumna://modes", (s) => (snapshot.value = s)));
  stops.push(
    await on("lumna://switcher-shown", () => {
      shown.value = true;
      scheduleHide();
    }),
  );
  if (root.value) stops.push(fitLayerTo(root.value));
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  window.clearTimeout(timer);
});
</script>

<template>
  <div ref="root" class="frame" @mouseenter="hovering = true" @mouseleave="hovering = false">
    <section class="switcher" :class="{ shown }">
      <div class="pill" :style="{ '--index': index }">
        <span class="slider" />
        <button v-for="mode in ORDER" :key="mode" :class="{ active: mode === current }" @click="pick(mode)">
          <Icon :name="ICON[mode]" :size="22" />
          {{ MODE_LABEL[mode] }}
        </button>
      </div>
      <footer>
        <span class="hint"><kbd>Super</kbd> + <kbd>Tab</kbd></span>
        <div class="layout">
          <button :class="{ active: snapshot?.layout === 'per-screen' }" @click="pickLayout('per-screen')">
            <Icon name="screens" :size="15" /> Um modo por tela
          </button>
          <button :class="{ active: snapshot?.layout === 'extend' }" @click="pickLayout('extend')">
            <Icon name="extend" :size="15" /> Estender modo
          </button>
        </div>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.frame {
  width: max-content;
  padding: 24px;
}

.switcher {
  display: flex;
  flex-direction: column;
  gap: 14px;
  padding: 16px;
  background: rgba(18, 13, 36, 0.86);
  border: 1px solid var(--lumna-line);
  border-radius: 28px;
  box-shadow: 0 24px 60px rgba(5, 2, 15, 0.55);
  opacity: 0;
  transform: scale(0.94);
  transition: opacity 180ms ease, transform 260ms cubic-bezier(0.22, 1, 0.36, 1);
}
.switcher.shown {
  opacity: 1;
  transform: none;
}

.pill {
  --width: 170px;
  position: relative;
  display: flex;
  padding: 5px;
  border-radius: 22px;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--lumna-line);
}
/* O destaque desliza até o modo atual, como no mockup. */
.slider {
  position: absolute;
  top: 5px;
  left: 5px;
  width: var(--width);
  height: calc(100% - 10px);
  border-radius: 18px;
  background: linear-gradient(135deg, #a78bfa, #7c3aed);
  box-shadow: 0 0 24px rgba(139, 92, 246, 0.55);
  transform: translateX(calc(var(--index) * var(--width)));
  transition: transform 320ms cubic-bezier(0.22, 1, 0.36, 1);
}
.pill button {
  position: relative;
  width: var(--width);
  height: 56px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  border: 0;
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 17px;
  transition: color 200ms ease;
}
.pill button.active {
  color: #fff;
}

footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
  font-size: 12px;
  color: var(--lumna-muted);
}
kbd {
  padding: 2px 7px;
  border-radius: 6px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.05);
  font: inherit;
  color: var(--lumna-text);
}
.layout {
  display: flex;
  gap: 6px;
}
.layout button {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 30px;
  padding: 0 11px;
  border-radius: 10px;
  border: 1px solid transparent;
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 12px;
}
.layout button.active {
  border-color: var(--lumna-violet);
  color: var(--lumna-text);
  background: rgba(139, 92, 246, 0.16);
}
</style>
