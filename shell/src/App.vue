<script setup lang="ts">
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import Dock from "./components/Dock.vue";
import Launcher from "./components/Launcher.vue";
import Notifications from "./components/Notifications.vue";
import Topbar from "./components/Topbar.vue";

// Cada janela do shell carrega o mesmo app; o rótulo diz qual peça desenhar.
const label = getCurrentWebviewWindow().label;
</script>

<template>
  <Topbar v-if="label.startsWith('topbar-')" />
  <Dock v-else-if="label === 'dock'" />
  <Launcher v-else-if="label === 'launcher'" />
  <Notifications v-else-if="label === 'notifications'" />
</template>
