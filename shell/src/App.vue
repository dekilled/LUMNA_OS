<script setup lang="ts">
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import Dock from "./components/Dock.vue";
import Launcher from "./components/Launcher.vue";
import ModeScreen from "./components/ModeScreen.vue";
import Notifications from "./components/Notifications.vue";
import SettingsApp from "./components/settings/SettingsApp.vue";
import Switcher from "./components/Switcher.vue";
import Topbar from "./components/Topbar.vue";
import WifiMenu from "./components/WifiMenu.vue";

// Cada janela do shell carrega o mesmo app; o rótulo diz qual peça desenhar.
const label = getCurrentWebviewWindow().label;
</script>

<template>
  <Topbar v-if="label.startsWith('topbar-')" />
  <Dock v-else-if="label === 'dock'" />
  <Launcher v-else-if="label === 'launcher'" />
  <Notifications v-else-if="label === 'notifications'" />
  <ModeScreen v-else-if="label.startsWith('mode-')" />
  <Switcher v-else-if="label === 'switcher'" />
  <WifiMenu v-else-if="label === 'wifi'" />
  <SettingsApp v-else-if="label === 'settings'" />
</template>
