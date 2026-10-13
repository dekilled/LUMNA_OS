<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { onCompositorEvent, windows, type Window } from "../compositor";
import { apps, fitLayerTo, launchApp, toggleLauncher, type App } from "../system";
import Icon from "./Icon.vue";

// Apps fixos do dock, pelo id do `.desktop`. Os que não estiverem instalados não aparecem.
// "Instalar LUMNA OS" só existe no pendrive; no sistema instalado ele some sozinho do dock.
const PINNED = ["lumna-installer", "lumna-files", "firefox", "org.xfce.mousepad", "foot"];

const installed = ref<App[]>([]);
const open = ref<Window[]>([]);
const root = ref<HTMLElement>();

/** Classe de janela de cada app: a declarada no `.desktop` ou o próprio id. */
const classOf = (app: App) => (app.wm_class ?? app.id).toLowerCase();

const items = computed(() => {
  const running = new Set(open.value.map((w) => w.class.toLowerCase()));
  const pinned = PINNED.map((id) => installed.value.find((a) => a.id === id)).filter((a): a is App => !!a);
  // Apps abertos que não estão fixos entram depois dos fixos.
  const extra = installed.value.filter((a) => !PINNED.includes(a.id) && running.has(classOf(a)));
  return [...pinned, ...extra].map((app) => ({ app, running: running.has(classOf(app)) }));
});

async function refreshWindows() {
  open.value = await windows().catch(() => []);
}

const stops: (() => void)[] = [];

onMounted(async () => {
  installed.value = await apps();
  await refreshWindows();
  stops.push(
    await onCompositorEvent((event) => {
      if (event.kind === "window_opened" || event.kind === "window_closed") refreshWindows();
    }),
  );
  if (root.value) stops.push(fitLayerTo(root.value));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div ref="root" class="dock-frame">
    <nav class="dock">
      <button v-for="{ app, running } in items" :key="app.id" class="app" :title="app.name" @click="launchApp(app.id)">
        <img v-if="app.icon" :src="app.icon" alt="" />
        <span v-else class="fallback">{{ app.name.charAt(0) }}</span>
        <span class="dot" :class="{ on: running }" />
      </button>
      <span class="divider" />
      <button class="app" title="Todos os apps" @click="toggleLauncher()">
        <span class="all"><Icon name="grid" :size="22" /></span>
        <span class="dot" />
      </button>
    </nav>
  </div>
</template>

<style scoped>
/* A moldura tem folga para a sombra e o zoom do hover não serem cortados pela camada. */
.dock-frame {
  width: max-content;
  padding: 8px 10px 0;
}

.dock {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 9px 4px;
  background: var(--lumna-glass);
  border: 1px solid var(--lumna-line);
  border-radius: 20px;
  box-shadow: 0 10px 30px rgba(5, 2, 15, 0.45);
}

.app {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  border: 0;
  background: transparent;
  padding: 0;
  transition: transform 180ms cubic-bezier(0.22, 1, 0.36, 1);
}
.app:hover {
  transform: translateY(-3px) scale(1.08);
}
.app:active {
  transform: scale(0.94);
}

.app img,
.fallback,
.all {
  width: 44px;
  height: 44px;
  border-radius: 12px;
}
.fallback,
.all {
  display: grid;
  place-items: center;
  background: rgba(139, 92, 246, 0.22);
  color: var(--lumna-text);
  font-weight: 700;
  font-size: 18px;
}

.dot {
  width: 4px;
  height: 4px;
  border-radius: 2px;
  background: transparent;
}
.dot.on {
  background: var(--lumna-violet-soft);
  box-shadow: 0 0 6px var(--lumna-violet);
}

.divider {
  width: 1px;
  height: 36px;
  margin: 0 4px 7px;
  background: var(--lumna-line);
}
</style>
