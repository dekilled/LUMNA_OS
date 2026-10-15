<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import type { Monitor } from "../../compositor";
import { displays, displaysArrange, type DisplayAlign } from "../../system";

// Telas lado a lado, na ordem da mesa: o mouse passa de uma para a outra pela borda em que elas
// se encostam. Arraste as telas (ou use as setas) e aplique.
const monitors = ref<Monitor[]>([]);
/** Ordem na tela (da esquerda para a direita), ainda não aplicada. */
const order = ref<string[]>([]);
const align = ref<DisplayAlign>("center");
const saved = ref<{ order: string[]; align: DisplayAlign }>({ order: [], align: "center" });
const selected = ref<string | null>(null);
const busy = ref(false);
const error = ref("");
const done = ref(false);

const ALIGNS: { id: DisplayAlign; label: string }[] = [
  { id: "top", label: "Pelo topo" },
  { id: "center", label: "Pelo centro" },
  { id: "bottom", label: "Pela base" },
];

const changed = computed(
  () => align.value !== saved.value.align || order.value.join() !== saved.value.order.join(),
);

const byName = (name: string) => monitors.value.find((m) => m.name === name)!;

// ---- desenho ----

const stage = ref<HTMLElement>();
const stageWidth = ref(640);
const STAGE_HEIGHT = 240;
let observer: ResizeObserver | undefined;

/** Posição de cada tela como vai ficar (a mesma conta do lado Rust). */
const placed = computed(() => {
  const list = order.value.map(byName).filter(Boolean);
  const tallest = Math.max(0, ...list.map((m) => m.height));
  let x = 0;
  return list.map((m) => {
    const y = align.value === "top" ? 0 : align.value === "bottom" ? tallest - m.height : Math.floor((tallest - m.height) / 2);
    const item = { m, x, y };
    x += m.width;
    return item;
  });
});

const zoom = computed(() => {
  const total = placed.value.reduce((sum, p) => sum + p.m.width, 0) || 1;
  const tallest = Math.max(1, ...placed.value.map((p) => p.m.height));
  const width = stageWidth.value - 48;
  return Math.min(width / total, (STAGE_HEIGHT - 48) / tallest);
});

const offset = computed(() => {
  const total = placed.value.reduce((sum, p) => sum + p.m.width, 0);
  const tallest = Math.max(0, ...placed.value.map((p) => p.m.height));
  const width = stageWidth.value;
  return { x: (width - total * zoom.value) / 2, y: (STAGE_HEIGHT - tallest * zoom.value) / 2 };
});

// ---- arrastar ----

const drag = ref<{ name: string; startX: number; dx: number } | null>(null);

function grab(event: PointerEvent, name: string) {
  selected.value = name;
  drag.value = { name, startX: event.clientX, dx: 0 };
  (event.currentTarget as HTMLElement).setPointerCapture(event.pointerId);
}

function moveDrag(event: PointerEvent) {
  if (drag.value) drag.value.dx = event.clientX - drag.value.startX;
}

/** Solta: a tela vai para a posição em que o centro dela caiu. */
function drop() {
  const current = drag.value;
  drag.value = null;
  if (!current || Math.abs(current.dx) < 4) return;
  const centers = placed.value.map((p) => ({
    name: p.m.name,
    center: p.x + p.m.width / 2 + (p.m.name === current.name ? current.dx / zoom.value : 0),
  }));
  centers.sort((a, b) => a.center - b.center);
  order.value = centers.map((c) => c.name);
  done.value = false;
}

function shift(by: number) {
  if (!selected.value) return;
  const list = [...order.value];
  const from = list.indexOf(selected.value);
  const to = from + by;
  if (from < 0 || to < 0 || to >= list.length) return;
  [list[from], list[to]] = [list[to], list[from]];
  order.value = list;
  done.value = false;
}

// ---- aplicar ----

function take(data: { monitors: Monitor[]; align: DisplayAlign }) {
  monitors.value = data.monitors;
  order.value = data.monitors.map((m) => m.name);
  align.value = data.align;
  saved.value = { order: [...order.value], align: data.align };
}

async function apply() {
  busy.value = true;
  error.value = "";
  try {
    take(await displaysArrange(order.value, align.value));
    done.value = true;
  } catch (e) {
    error.value = String(e);
  } finally {
    busy.value = false;
  }
}

function undo() {
  order.value = [...saved.value.order];
  align.value = saved.value.align;
}

onMounted(async () => {
  if (stage.value) {
    observer = new ResizeObserver(() => (stageWidth.value = stage.value?.clientWidth ?? 640));
    observer.observe(stage.value);
  }
  try {
    take(await displays());
    selected.value = order.value[0] ?? null;
  } catch (e) {
    error.value = String(e);
  }
});
onUnmounted(() => observer?.disconnect());
</script>

<template>
  <section class="page">
    <h1>Telas</h1>
    <p class="lead">
      Arraste as telas para ficarem na mesma ordem da sua mesa. O mouse passa de uma para a outra pela borda em que
      elas se encostam.
    </p>
    <p v-if="error" class="error">{{ error }}</p>

    <div ref="stage" class="stage" :style="{ height: `${STAGE_HEIGHT}px` }">
      <button
        v-for="(p, i) in placed"
        :key="p.m.name"
        class="screen"
        :class="{ chosen: selected === p.m.name, dragging: drag?.name === p.m.name }"
        :style="{
          left: `${offset.x + p.x * zoom + (drag?.name === p.m.name ? drag.dx : 0)}px`,
          top: `${offset.y + p.y * zoom}px`,
          width: `${p.m.width * zoom - 6}px`,
          height: `${p.m.height * zoom - 6}px`,
        }"
        @pointerdown="grab($event, p.m.name)"
        @pointermove="moveDrag"
        @pointerup="drop"
        @pointercancel="drop"
      >
        <span class="number">{{ i + 1 }}</span>
        <span class="label">{{ p.m.label }}</span>
        <small>{{ p.m.name }} · {{ Math.round(p.m.width * p.m.scale) }} × {{ Math.round(p.m.height * p.m.scale) }}</small>
      </button>
      <p v-if="monitors.length === 1" class="single">Só uma tela ligada agora.</p>
    </div>

    <div v-if="monitors.length > 1" class="tools">
      <button class="btn" :disabled="!selected || order.indexOf(selected) <= 0" @click="shift(-1)">
        <Icon name="back" :size="15" /> Para a esquerda
      </button>
      <button class="btn" :disabled="!selected || order.indexOf(selected) >= order.length - 1" @click="shift(1)">
        Para a direita <Icon name="forward" :size="15" />
      </button>
    </div>

    <h2>Alinhar as telas</h2>
    <p class="note">Telas de tamanhos diferentes: escolha por onde elas ficam alinhadas na altura.</p>
    <div class="segments">
      <button v-for="a in ALIGNS" :key="a.id" :class="{ chosen: align === a.id }" @click="(align = a.id), (done = false)">
        {{ a.label }}
      </button>
    </div>

    <div class="toolbar">
      <button class="btn primary" :disabled="!changed || busy" @click="apply">
        <Icon :name="busy ? 'refresh' : 'check'" :size="15" /> {{ busy ? "Aplicando…" : "Aplicar" }}
      </button>
      <button class="btn" :disabled="!changed || busy" @click="undo">Desfazer</button>
      <span v-if="done && !changed" class="ok"><Icon name="check" :size="15" /> Pronto. Vale também nos próximos logins.</span>
    </div>
  </section>
</template>

<style scoped>
.stage {
  position: relative;
  margin-top: 6px;
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--lumna-line);
  overflow: hidden;
  touch-action: none;
}

.screen {
  position: absolute;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  padding: 6px;
  border: 2px solid rgba(167, 139, 250, 0.35);
  border-radius: 10px;
  background: linear-gradient(135deg, #2a1857, #1a1033);
  color: var(--lumna-text);
  font: inherit;
  cursor: grab;
  overflow: hidden;
  transition: left 200ms cubic-bezier(0.22, 1, 0.36, 1), top 200ms cubic-bezier(0.22, 1, 0.36, 1);
  user-select: none;
}
.screen.chosen {
  border-color: #8b5cf6;
  box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.25);
}
.screen.dragging {
  cursor: grabbing;
  transition: none;
  z-index: 2;
  opacity: 0.9;
}
.screen .number {
  font-size: 26px;
  font-weight: 700;
  color: #c4b5fd;
}
.screen .label {
  font-size: 13px;
  font-weight: 600;
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.screen small {
  font-size: 11px;
  color: var(--lumna-muted);
}
.single {
  position: absolute;
  bottom: 8px;
  width: 100%;
  margin: 0;
  text-align: center;
  font-size: 12px;
  color: var(--lumna-muted);
}

.tools {
  display: flex;
  justify-content: center;
  gap: 8px;
  margin-top: 12px;
}

.segments {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.segments button {
  height: 34px;
  padding: 0 16px;
  border: 1px solid var(--lumna-line);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  font-size: 13px;
  cursor: pointer;
}
.segments button:hover {
  background: rgba(167, 139, 250, 0.1);
}
.segments button.chosen {
  border-color: #8b5cf6;
  background: rgba(139, 92, 246, 0.25);
}

.toolbar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-top: 24px;
}
.ok {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: #86efac;
}
</style>
