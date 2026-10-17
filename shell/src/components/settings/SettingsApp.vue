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

// Tela estreita (celular): primeiro a lista; tocar num item abre a página, com "voltar".
// Aberta já numa página (atalho da central, link do Telefone), vai direto nela.
const narrow = ref(window.innerWidth < 600);
const listing = ref(!location.hash.slice(1));
const onResize = () => (narrow.value = window.innerWidth < 600);
function choose(id: SettingsPage) {
  page.value = id;
  listing.value = false;
}
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
      listing.value = false;
    }),
  );
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="settings" :class="{ narrow }">
    <WindowBar v-if="!narrow" title="Configurações" />

    <div class="body">
      <nav v-if="!narrow || listing" class="sidebar">
        <h1 v-if="narrow" class="list-title">Ajustes</h1>
        <button v-for="p in PAGES" :key="p.id" :class="{ active: !narrow && p.id === page }" @click="choose(p.id)">
          <Icon :name="p.icon" :size="narrow ? 22 : 18" />
          {{ p.label }}
          <Icon v-if="narrow" class="chev" name="forward" :size="18" />
        </button>
      </nav>
      <main v-if="!narrow || !listing" class="content">
        <button v-if="narrow" class="back" @click="listing = true"><Icon name="arrow-left" :size="22" /> Ajustes</button>
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

/* celular */
.narrow .sidebar {
  flex: 1;
  width: auto;
  padding: 24px 14px;
  border-right: 0;
  overflow-y: auto;
  gap: 6px;
}
.narrow .sidebar button {
  height: 56px;
  padding: 0 16px;
  gap: 16px;
  border: 1px solid var(--lumna-line);
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.03);
  color: var(--lumna-text);
  font-size: 16px;
}
.narrow .sidebar .chev {
  margin-left: auto;
  color: var(--lumna-muted);
}
.list-title {
  margin: 8px 6px 14px;
  font-size: 32px;
}
.narrow .content {
  padding: 16px 16px 40px;
}
.back {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
  padding: 8px 4px;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 16px;
}
</style>
