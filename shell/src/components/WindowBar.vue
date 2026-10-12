<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import Icon from "./Icon.vue";

// Barra das janelas do LUMNA (Configurações, Loja): arrastar, minimizar, maximizar e fechar.
defineProps<{ title: string }>();
const win = getCurrentWindow();
</script>

<template>
  <header class="titlebar" data-tauri-drag-region @dblclick="win.toggleMaximize()">
    <span class="title" data-tauri-drag-region>{{ title }}</span>
    <slot />
    <div class="controls">
      <button title="Minimizar" @click="win.minimize()"><Icon name="minimize" :size="15" /></button>
      <button title="Maximizar" @click="win.toggleMaximize()"><Icon name="maximize" :size="13" /></button>
      <button class="close" title="Fechar" @click="win.close()"><Icon name="close" :size="15" /></button>
    </div>
  </header>
</template>

<style scoped>
.titlebar {
  height: 44px;
  flex: none;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  padding: 0 8px 0 18px;
  border-bottom: 1px solid var(--lumna-line);
}
.title {
  font-size: 13px;
  font-weight: 600;
  color: var(--lumna-muted);
  user-select: none;
}
.controls {
  display: flex;
  gap: 2px;
}
.controls button {
  width: 34px;
  height: 30px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--lumna-muted);
}
.controls button:hover {
  background: rgba(167, 139, 250, 0.14);
  color: var(--lumna-text);
}
.controls .close:hover {
  background: rgba(239, 68, 68, 0.8);
  color: #fff;
}
</style>
