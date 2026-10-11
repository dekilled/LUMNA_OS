<script setup lang="ts">
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import Topbar from "./components/Topbar.vue";

// Cada janela do shell carrega o mesmo app; o rótulo diz qual peça desenhar.
const label = getCurrentWebviewWindow().label;
</script>

<template>
  <Topbar v-if="label.startsWith('topbar-')" />
</template>
