<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import {
  activeWindow,
  focusWorkspace,
  monitors,
  onCompositorEvent,
  windowControl,
  workspaces,
  type Window,
  type Workspace,
} from "../compositor";
import {
  changeVolume,
  MODE_LABEL,
  modes,
  monitorName,
  on,
  showSwitcher,
  status,
  toggleLauncher,
  toggleMute,
  toggleWifi,
  type Mode,
  type ModesSnapshot,
  type Status,
  type WifiState,
} from "../system";
import Icon from "./Icon.vue";

// Sempre mostra os workspaces 1 a 5, como no Desktop do mockup; outros aparecem quando existem.
const FIXED = [1, 2, 3, 4, 5];

const list = ref<Workspace[]>([]);
const active = ref(1);
const now = ref(new Date());
const sys = ref<Status | null>(null);
const monitor = ref<string | null>(null);
const mode = ref<Mode>("desktop");

// Barra da janela em foco (vale para qualquer app, até os que não desenham a própria barra, como o
// Firefox): título + minimizar, maximizar e fechar. Só na topbar do monitor onde a janela está.
const focused = ref<Window | null>(null);
const monitorId = ref<number | null>(null);
const showWindow = computed(() => focused.value && monitorId.value !== null && focused.value.monitor === monitorId.value);

async function refreshWindow() {
  focused.value = await activeWindow().catch(() => null);
}

function control(action: "minimize" | "maximize" | "close") {
  const address = focused.value?.address;
  if (address) windowControl(action, address).catch((e) => console.error(e));
}

const MODE_ICON: Record<Mode, string> = { desktop: "desktop", gaming: "gamepad", media: "play" };

function applyModes(snapshot: ModesSnapshot) {
  if (monitor.value) mode.value = snapshot.modes[monitor.value] ?? "desktop";
}

const shown = computed(() => {
  // Só os workspaces numerados; os com nome são dos modos Gaming e Mídia.
  const numbered = list.value.filter((w) => /^\d+$/.test(w.name)).map((w) => w.id);
  const ids = new Set([...FIXED, ...numbered]);
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

const networkIcon = computed(() => ({ wifi: "wifi", ethernet: "ethernet", none: "offline" })[sys.value?.network ?? "none"]);
const wifi = ref<WifiState | null>(null);
const networkLabel = computed(() => {
  const net = sys.value?.network ?? "none";
  if (net === "ethernet") return "Cabo conectado";
  if (net === "wifi") return wifi.value?.connected ? `Wi-Fi: ${wifi.value.connected}` : "Wi-Fi conectado";
  return "Sem rede · clique para escolher o Wi-Fi";
});

async function refresh() {
  const [ws, mons] = await Promise.all([workspaces(), monitors()]);
  list.value = ws;
  active.value = mons.find((m) => m.focused)?.active_workspace ?? active.value;
  if (monitor.value) monitorId.value = mons.find((m) => m.name === monitor.value)?.id ?? null;
}

function onVolumeWheel(event: WheelEvent) {
  changeVolume(event.deltaY < 0 ? 5 : -5);
}

const stops: (() => void)[] = [];
let clock: number | undefined;

onMounted(async () => {
  clock = window.setInterval(() => (now.value = new Date()), 1000);
  stops.push(await on<Status>("lumna://status", (s) => (sys.value = s)));
  stops.push(
    await onCompositorEvent((event) => {
      if (event.kind === "workspace_focused") active.value = event.id;
      else if (event.kind === "workspaces_changed" || event.kind === "monitors_changed") refresh().catch(() => {});
      if (event.kind !== "monitor_focused") refreshWindow();
    }),
  );
  stops.push(await on<ModesSnapshot>("lumna://modes", applyModes));
  stops.push(await on<WifiState>("lumna://wifi", (w) => (wifi.value = w)));
  sys.value = await status();
  await refresh().catch(() => {});
  monitor.value = await monitorName().catch(() => null);
  await refresh().catch(() => {});
  await refreshWindow();
  applyModes(await modes());
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  window.clearInterval(clock);
});
</script>

<template>
  <header class="topbar">
    <div class="left">
      <button class="brand" title="Apps" @click="toggleLauncher()">
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
      </button>
      <button class="mode" title="Alternar modo (Super + Tab)" @click="showSwitcher()">
        <Icon :name="MODE_ICON[mode]" :size="14" />
        {{ MODE_LABEL[mode] }}
      </button>
    </div>

    <!-- Fora do Desktop os workspaces numerados não valem; o centro fica livre. -->
    <span v-if="mode !== 'desktop'" />
    <nav v-else class="workspaces">
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
      <div v-if="showWindow && focused" class="window" :title="focused.title">
        <span class="window-title">{{ focused.title || focused.class }}</span>
        <button title="Minimizar" @click="control('minimize')"><Icon name="minimize" :size="14" /></button>
        <button title="Maximizar" @click="control('maximize')"><Icon name="maximize" :size="12" /></button>
        <button class="close" title="Fechar (Super + Q)" @click="control('close')"><Icon name="close" :size="14" /></button>
      </div>
      <div v-if="sys" class="status">
        <button class="item" :title="networkLabel" @click="toggleWifi()"><Icon :name="networkIcon" /></button>
        <button
          v-if="sys.volume !== null"
          class="item"
          :title="sys.muted ? 'Som desligado' : `Volume ${sys.volume}%`"
          @click="toggleMute()"
          @wheel.prevent="onVolumeWheel"
        >
          <Icon :name="sys.muted ? 'mute' : 'volume'" />
        </button>
        <span v-if="sys.battery !== null" class="item battery" :title="`Bateria ${sys.battery}%`">
          <Icon :name="sys.charging ? 'bolt' : 'battery'" />
          {{ sys.battery }}%
        </span>
      </div>
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

button {
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  padding: 0;
}

.brand {
  display: flex;
  align-items: center;
  gap: 7px;
  font-weight: 700;
  letter-spacing: 0.08em;
  border-radius: 8px;
  padding: 3px 6px;
  margin-left: -6px;
  transition: background-color 160ms ease;
}
.brand:hover {
  background: rgba(167, 139, 250, 0.14);
}
.crystal {
  width: 16px;
  height: 16px;
  filter: drop-shadow(0 0 4px rgba(139, 92, 246, 0.8));
}

.mode {
  display: flex;
  align-items: center;
  gap: 6px;
  height: 24px;
  padding: 0 9px;
  border-radius: 12px;
  color: var(--lumna-muted);
  transition: background-color 160ms ease, color 160ms ease;
}
.mode:hover {
  background: rgba(167, 139, 250, 0.14);
  color: var(--lumna-text);
}


.workspaces {
  display: flex;
  gap: 4px;
}
.workspaces button {
  min-width: 26px;
  height: 22px;
  border-radius: 11px;
  color: var(--lumna-muted);
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

.status {
  display: flex;
  align-items: center;
  gap: 4px;
  color: var(--lumna-text);
}
.item {
  display: flex;
  align-items: center;
  gap: 4px;
  height: 24px;
  padding: 0 5px;
  border-radius: 6px;
}
button.item:hover {
  background: rgba(167, 139, 250, 0.14);
}
.battery {
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}

.time {
  font-weight: 600;
  font-variant-numeric: tabular-nums;
}
.date {
  color: var(--lumna-muted);
}

.window {
  display: flex;
  align-items: center;
  gap: 2px;
  margin-right: 10px;
  padding: 0 4px 0 10px;
  height: 26px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.05);
}
.window-title {
  max-width: 280px;
  margin-right: 6px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
  font-size: 12px;
  color: var(--lumna-muted);
}
.window button {
  display: grid;
  place-items: center;
  width: 26px;
  height: 22px;
  border: 0;
  border-radius: 6px;
  background: transparent;
  color: var(--lumna-muted);
}
.window button:hover {
  background: rgba(167, 139, 250, 0.18);
  color: var(--lumna-text);
}
.window .close:hover {
  background: rgba(239, 68, 68, 0.85);
  color: #fff;
}
</style>
