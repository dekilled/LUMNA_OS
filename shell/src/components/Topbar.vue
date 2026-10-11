<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { focusWorkspace, monitors, onCompositorEvent, workspaces, type Workspace } from "../compositor";

// Sempre mostra os workspaces 1 a 5, como no Desktop do mockup; outros aparecem quando existem.
const FIXED = [1, 2, 3, 4, 5];

const list = ref<Workspace[]>([]);
const active = ref(1);
const now = ref(new Date());

const shown = computed(() => {
  const ids = new Set([...FIXED, ...list.value.filter((w) => w.id > 0).map((w) => w.id)]);
  return [...ids].sort((a, b) => a - b).map((id) => ({
    id,
    busy: (list.value.find((w) => w.id === id)?.windows ?? 0) > 0,
  }));
});

const date = computed(() => {
  const text = new Intl.DateTimeFormat("pt-BR", { weekday: "short", day: "numeric", month: "short" })
    .format(now.value)
    .replace(/\./g, "");
  return text.charAt(0).toUpperCase() + text.slice(1);
});
const time = computed(() =>
  new Intl.DateTimeFormat("pt-BR", { hour: "2-digit", minute: "2-digit" }).format(now.value),
);

async function refresh() {
  const [ws, mons] = await Promise.all([workspaces(), monitors()]);
  list.value = ws;
  active.value = mons.find((m) => m.focused)?.active_workspace ?? active.value;
}

let stopEvents: (() => void) | undefined;
let clock: number | undefined;

onMounted(async () => {
  await refresh();
  stopEvents = await onCompositorEvent((event) => {
    if (event.kind === "workspace_focused") active.value = event.id;
    else if (event.kind === "workspaces_changed" || event.kind === "monitors_changed") refresh();
  });
  clock = window.setInterval(() => (now.value = new Date()), 1000);
});

onUnmounted(() => {
  stopEvents?.();
  window.clearInterval(clock);
});
</script>

<template>
  <header class="topbar">
    <div class="left">
      <div class="brand">
        <svg class="crystal" viewBox="0 0 24 24" aria-hidden="true">
          <defs>
            <linearGradient id="g" x1="0" y1="0" x2="1" y2="1">
              <stop offset="0" stop-color="#c4b5fd" />
              <stop offset="1" stop-color="#7c3aed" />
            </linearGradient>
          </defs>
          <path d="M12 1.5 19 9l-7 13.5L5 9z" fill="url(#g)" />
          <path d="M12 1.5 15 9l-3 13.5L9 9z" fill="#ffffff" opacity=".25" />
        </svg>
        <span>LUMNA</span>
      </div>
      <span class="mode">Desktop</span>
    </div>

    <nav class="workspaces">
      <button
        v-for="w in shown"
        :key="w.id"
        :class="{ active: w.id === active, busy: w.busy }"
        @click="focusWorkspace(w.id)"
      >
        {{ w.id }}
      </button>
    </nav>

    <div class="right">
      <span class="date">{{ date }}</span>
      <span class="time">{{ time }}</span>
    </div>
  </header>
</template>

<style scoped>
.topbar {
  height: 36px;
  box-sizing: border-box;
  display: grid;
  grid-template-columns: 1fr auto 1fr;
  align-items: center;
  padding: 0 14px;
  background: var(--lumna-glass);
  border-bottom: 1px solid var(--lumna-line);
  font-size: 13px;
}

.left,
.right {
  display: flex;
  align-items: center;
  gap: 14px;
}
.right {
  justify-content: flex-end;
}

.brand {
  display: flex;
  align-items: center;
  gap: 7px;
  font-weight: 700;
  letter-spacing: 0.08em;
}
.crystal {
  width: 16px;
  height: 16px;
  filter: drop-shadow(0 0 4px rgba(139, 92, 246, 0.8));
}

.mode {
  color: var(--lumna-muted);
}

.workspaces {
  display: flex;
  gap: 4px;
}
.workspaces button {
  min-width: 26px;
  height: 22px;
  border: 0;
  border-radius: 11px;
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 12px;
  transition: background-color 160ms ease, color 160ms ease, transform 160ms ease;
}
.workspaces button.busy {
  color: var(--lumna-text);
}
.workspaces button:hover {
  background: rgba(167, 139, 250, 0.14);
}
.workspaces button.active {
  background: var(--lumna-violet);
  color: #fff;
  min-width: 36px;
}
.workspaces button:active {
  transform: scale(0.94);
}

.time {
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}
.date {
  color: var(--lumna-muted);
}
</style>
