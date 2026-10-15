<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import Icon from "./Icon.vue";
import { windowControl } from "../compositor";

// Barra das janelas do LUMNA (Configurações, Loja): arrastar, minimizar, maximizar e fechar.
// Minimizar e maximizar vão pelo compositor (o Hyprland ignora os pedidos do GTK); a janela que
// recebe o clique é a em foco.
defineProps<{ title: string }>();
const win = getCurrentWindow();
const control = (action: "minimize" | "maximize") => windowControl(action).catch((e) => console.error(e));
</script>

<template>
  <header class="titlebar" data-tauri-drag-region @dblclick="control('maximize')">
    <span class="title" data-tauri-drag-region>{{ title }}</span>
    <slot />
    <div class="controls">
      <button title="Minimizar" @click="control('minimize')"><Icon name="minimize" :size="15" /></button>
      <button title="Maximizar" @click="control('maximize')"><Icon name="maximize" :size="13" /></button>
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
