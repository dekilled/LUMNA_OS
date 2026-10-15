<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import { onMounted, onUnmounted, ref, type Component } from "vue";
import Icon from "../Icon.vue";
import WindowBar from "../WindowBar.vue";
import { on, type SettingsPage } from "../../system";
import AudioPage from "./AudioPage.vue";
import BluetoothPage from "./BluetoothPage.vue";
import NetworkPage from "./NetworkPage.vue";
import PrintersPage from "./PrintersPage.vue";
import SystemPage from "./SystemPage.vue";
import WallpaperPage from "./WallpaperPage.vue";
import "./settings.css";

const PAGES: { id: SettingsPage; label: string; icon: string; component: Component }[] = [
  { id: "rede", label: "Rede", icon: "wifi", component: NetworkPage },
  { id: "bluetooth", label: "Bluetooth", icon: "bluetooth", component: BluetoothPage },
  { id: "som", label: "Som", icon: "volume", component: AudioPage },
  { id: "impressoras", label: "Impressoras", icon: "printer", component: PrintersPage },
  { id: "papel-de-parede", label: "Papel de parede", icon: "image", component: WallpaperPage },
  { id: "sistema", label: "Sistema", icon: "info", component: SystemPage },
];

const pick = (id: string | null | undefined) => PAGES.find((p) => p.id === id)?.id ?? "rede";
const page = ref<SettingsPage>(pick(location.hash.slice(1)));
const current = () => PAGES.find((p) => p.id === page.value)!;

const win = getCurrentWindow();
const stops: (() => void)[] = [];

function onKey(event: KeyboardEvent) {
  if (event.key === "w" && event.ctrlKey) win.close();
}

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on<string>("lumna://settings-page", (id) => (page.value = pick(id))));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="settings">
    <WindowBar title="Configurações" />

    <div class="body">
      <nav class="sidebar">
        <button v-for="p in PAGES" :key="p.id" :class="{ active: p.id === page }" @click="page = p.id">
          <Icon :name="p.icon" :size="18" />
          {{ p.label }}
        </button>
      </nav>
      <main class="content">
        <!-- Cada página é recriada ao entrar: a do Bluetooth procura aparelhos só enquanto está aberta. -->
        <component :is="current().component" :key="page" />
      </main>
    </div>
  </div>
</template>

<style scoped>
.settings {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: rgba(16, 12, 30, 0.94);
  user-select: text;
}

.body {
  flex: 1;
  min-height: 0;
  display: flex;
}

.sidebar {
  width: 210px;
  flex: none;
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 14px 10px;
  border-right: 1px solid var(--lumna-line);
  user-select: none;
}
.sidebar button {
  height: 40px;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0 12px;
  border: 0;
  border-radius: 10px;
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 14px;
  text-align: left;
}
.sidebar button:hover {
  background: rgba(167, 139, 250, 0.1);
  color: var(--lumna-text);
}
.sidebar button.active {
  background: rgba(139, 92, 246, 0.22);
  color: #fff;
}

.content {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  padding: 26px 32px 40px;
}
</style>
