<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import { onMounted, onUnmounted, ref, type Component } from "vue";
import Icon from "../Icon.vue";
import WindowBar from "../WindowBar.vue";
import { on, type SettingsPage } from "../../system";
import AudioPage from "./AudioPage.vue";
import BluetoothPage from "./BluetoothPage.vue";
import DisplaysPage from "./DisplaysPage.vue";
import DockPage from "./DockPage.vue";
import EnergyPage from "./EnergyPage.vue";
import KeyboardPage from "./KeyboardPage.vue";
import MobileDataPage from "./MobileDataPage.vue";
import MobileSettings, { type MPage } from "./mobile/MobileSettings.vue";
import NetworkPage from "./NetworkPage.vue";
import PrintersPage from "./PrintersPage.vue";
import SystemPage from "./SystemPage.vue";
import WallpaperPage from "./WallpaperPage.vue";
import "./settings.css";

const PAGES: { id: SettingsPage; label: string; icon: string; component: Component }[] = [
  { id: "rede", label: "Rede", icon: "wifi", component: NetworkPage },
  { id: "dados-moveis", label: "Dados móveis", icon: "signal", component: MobileDataPage },
  { id: "bluetooth", label: "Bluetooth", icon: "bluetooth", component: BluetoothPage },
  { id: "som", label: "Som", icon: "volume", component: AudioPage },
  { id: "impressoras", label: "Impressoras", icon: "printer", component: PrintersPage },
  { id: "telas", label: "Telas", icon: "screens", component: DisplaysPage },
  { id: "teclado", label: "Teclado e idioma", icon: "keyboard", component: KeyboardPage },
  { id: "energia", label: "Energia", icon: "power", component: EnergyPage },
  { id: "papel-de-parede", label: "Papel de parede", icon: "image", component: WallpaperPage },
  { id: "dock", label: "Dock", icon: "dock", component: DockPage },
  { id: "sistema", label: "Sistema", icon: "info", component: SystemPage },
];

const pick = (id: string | null | undefined) => PAGES.find((p) => p.id === id)?.id ?? "rede";
const page = ref<SettingsPage>(pick(location.hash.slice(1)));

// Tela estreita (celular): os Ajustes com cara de celular (mobile/MobileSettings.vue).
// Aberta já numa página (atalho da central, link do Telefone), vai direto nela.
const narrow = ref(window.innerWidth < 600);
const onResize = () => (narrow.value = window.innerWidth < 600);
const MOBILE_PAGE: Partial<Record<string, MPage>> = {
  rede: "wifi",
  "dados-moveis": "dados",
  bluetooth: "bluetooth",
  som: "som",
  telas: "tela",
  "papel-de-parede": "tela",
  dock: "tela",
  teclado: "teclado",
  energia: "energia",
  sistema: "sistema",
};
const mobileStart = ref<MPage>(MOBILE_PAGE[location.hash.slice(1)] ?? "home");
const current = () => PAGES.find((p) => p.id === page.value)!;

const win = getCurrentWindow();
const stops: (() => void)[] = [];

function onKey(event: KeyboardEvent) {
  if (event.key === "w" && event.ctrlKey) win.close();
}

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  window.addEventListener("resize", onResize);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(() => window.removeEventListener("resize", onResize));
  stops.push(
    await on<string>("lumna://settings-page", (id) => {
      page.value = pick(id);
      mobileStart.value = MOBILE_PAGE[id] ?? "home";
    }),
  );
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <MobileSettings v-if="narrow" :start="mobileStart" />
  <div v-else class="settings">
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
