<script setup lang="ts">
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import { apps, launchApp, layerHide, on, type App } from "../system";
import Icon from "./Icon.vue";

const COLUMNS = 6;

const all = ref<App[]>([]);
const query = ref("");
const selected = ref(0);
const visible = ref(false);
const input = ref<HTMLInputElement>();

/** Busca sem acento e sem diferenciar maiúsculas: "musica" acha "Música". */
const fold = (text: string) => text.normalize("NFD").replace(/\p{Diacritic}/gu, "").toLowerCase();

const results = computed(() => {
  const q = fold(query.value.trim());
  if (!q) return all.value;
  return all.value
    .map((app) => {
      const name = fold(app.name);
      const score = name.startsWith(q) ? 0 : name.includes(q) ? 1 : fold(app.comment).includes(q) ? 2 : 3;
      return { app, score };
    })
    .filter((r) => r.score < 3)
    .sort((a, b) => a.score - b.score)
    .map((r) => r.app);
});

watch(query, () => (selected.value = 0));

async function opened() {
  query.value = "";
  selected.value = 0;
  visible.value = true;
  await nextTick();
  input.value?.focus();
  all.value = await apps();
}

function close() {
  visible.value = false;
  layerHide();
}

function launch(app: App | undefined) {
  if (!app) return;
  visible.value = false;
  launchApp(app.id);
}

function onKey(event: KeyboardEvent) {
  if (!visible.value) return;
  // Uma tecla que chega antes do foco no campo de busca não se perde: vai para a busca.
  if (document.activeElement !== input.value && event.key.length === 1 && !event.ctrlKey && !event.altKey) {
    event.preventDefault();
    query.value += event.key;
    input.value?.focus();
    return;
  }
  const count = results.value.length;
  const moves: Record<string, number> = { ArrowRight: 1, ArrowLeft: -1, ArrowDown: COLUMNS, ArrowUp: -COLUMNS };
  if (event.key === "Escape") close();
  else if (event.key === "Enter") launch(results.value[selected.value]);
  else if (event.key in moves && count) {
    event.preventDefault();
    selected.value = Math.min(count - 1, Math.max(0, selected.value + moves[event.key]));
  }
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://launcher-opened", opened));
  stops.push(await on("lumna://launcher-closed", () => (visible.value = false)));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="backdrop" :class="{ visible }" @click.self="close">
    <section class="panel">
      <label class="search">
        <Icon name="search" :size="18" />
        <input ref="input" v-model="query" placeholder="Buscar apps" spellcheck="false" autocomplete="off" />
      </label>
      <div class="grid">
        <button
          v-for="(app, i) in results"
          :key="app.id"
          class="app"
          :class="{ selected: i === selected }"
          :title="app.comment || app.name"
          @click="launch(app)"
          @mouseenter="selected = i"
        >
          <img v-if="app.icon" :src="app.icon" alt="" />
          <span v-else class="fallback">{{ app.name.charAt(0) }}</span>
          <span class="name">{{ app.name }}</span>
        </button>
        <p v-if="!results.length" class="empty">Nenhum app encontrado</p>
      </div>
    </section>
  </div>
</template>

<style scoped>
.backdrop {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(8, 5, 18, 0.45);
  opacity: 0;
  transition: opacity 180ms cubic-bezier(0.22, 1, 0.36, 1);
}
.backdrop.visible {
  opacity: 1;
}

.panel {
  width: min(760px, 90vw);
  /* Altura fixa: o painel não pula enquanto a busca filtra. */
  height: min(560px, 72vh);
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  gap: 18px;
  padding: 22px;
  background: rgba(18, 13, 36, 0.82);
  border: 1px solid var(--lumna-line);
  border-radius: 24px;
  box-shadow: 0 30px 80px rgba(5, 2, 15, 0.6);
  transform: translateY(12px) scale(0.98);
  transition: transform 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.visible .panel {
  transform: none;
}

.search {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 14px;
  height: 46px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid var(--lumna-line);
  color: var(--lumna-muted);
}
.search:focus-within {
  border-color: var(--lumna-violet);
}
.search input {
  flex: 1;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(6, minmax(0, 1fr));
  grid-auto-rows: max-content;
  gap: 6px;
  overflow-y: auto;
}

.app {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 14px 6px 10px;
  border: 0;
  border-radius: 16px;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 12px;
}
.app.selected {
  background: rgba(139, 92, 246, 0.22);
}
.app img,
.fallback {
  width: 52px;
  height: 52px;
  border-radius: 14px;
}
.fallback {
  display: grid;
  place-items: center;
  background: rgba(139, 92, 246, 0.3);
  font-size: 22px;
  font-weight: 700;
}
.name {
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.empty {
  grid-column: 1 / -1;
  text-align: center;
  color: var(--lumna-muted);
}
</style>
