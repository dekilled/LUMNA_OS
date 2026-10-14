<script setup lang="ts">
// Fundo do Desktop: o papel de parede (desenhado pelo LUMNA, por cima do swaybg de reserva) e o
// menu do botão direito na área vazia. Novas funções do Desktop entram como itens deste menu.
import { convertFileSrc } from "@tauri-apps/api/core";
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { desktopWallpaper, filesTerminal, filesWindow, settingsOpen, toggleEmma, toggleLauncher } from "../system";

interface Item {
  label: string;
  icon: string;
  run?: () => unknown;
  /** Aparece, mas ainda não faz nada (o que vem depois). */
  soon?: boolean;
}

const MENU: (Item | "line")[] = [
  { label: "Abrir terminal", icon: "code", run: () => filesTerminal("~") },
  { label: "Arquivos", icon: "folder", run: () => filesWindow() },
  { label: "Todos os apps", icon: "grid", run: () => toggleLauncher() },
  { label: "Perguntar à Emma", icon: "sparkle", run: () => toggleEmma() },
  "line",
  { label: "Papel de parede…", icon: "image", soon: true },
  { label: "Configurações", icon: "settings", run: () => settingsOpen() },
];

const wallpaper = ref<string | null>(null);
const menu = ref<{ x: number; y: number } | null>(null);

function open(event: MouseEvent) {
  event.preventDefault();
  // O menu fica inteiro dentro da tela.
  const x = Math.min(event.clientX, window.innerWidth - 230);
  const y = Math.min(event.clientY, window.innerHeight - 300);
  menu.value = { x: Math.max(8, x), y: Math.max(8, y) };
}

function close() {
  menu.value = null;
}

function choose(item: Item) {
  if (item.soon || !item.run) return;
  close();
  Promise.resolve(item.run()).catch((e) => console.error(e));
}

function onKey(event: KeyboardEvent) {
  if (event.key === "Escape") close();
}

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  window.addEventListener("blur", close);
  // A camada de fundo não recebe o teclado nem cliques em cima das janelas: o mouse sair dela
  // (indo para uma janela) também fecha o menu.
  document.documentElement.addEventListener("pointerleave", close);
  const path = await desktopWallpaper().catch(() => null);
  wallpaper.value = path ? convertFileSrc(path) : null;
});

onUnmounted(() => {
  window.removeEventListener("keydown", onKey);
  window.removeEventListener("blur", close);
  document.documentElement.removeEventListener("pointerleave", close);
});
</script>

<template>
  <div class="desktop" @contextmenu="open" @pointerdown.self="close">
    <img v-if="wallpaper" class="wallpaper" :src="wallpaper" alt="" draggable="false" @pointerdown="close" />
    <div v-if="menu" class="menu" :style="{ left: `${menu.x}px`, top: `${menu.y}px` }" @contextmenu.prevent>
      <template v-for="(item, i) in MENU" :key="i">
        <span v-if="item === 'line'" class="line" />
        <button v-else :class="{ soon: item.soon }" @click="choose(item)">
          <Icon :name="item.icon" :size="16" />
          <span class="label">{{ item.label }}</span>
          <small v-if="item.soon">em breve</small>
        </button>
      </template>
    </div>
  </div>
</template>

<style scoped>
.desktop {
  position: fixed;
  inset: 0;
  overflow: hidden;
  background: #120c24;
}

.wallpaper {
  position: absolute;
  inset: 0;
  width: 100%;
  height: 100%;
  object-fit: cover;
  user-select: none;
}

/* Mesmo visual do menu de texto e do painel da Emma (grafite, acento violeta). */
.menu {
  position: absolute;
  min-width: 220px;
  display: flex;
  flex-direction: column;
  padding: 6px;
  background: #1e1f23;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.5);
  animation: pop 140ms cubic-bezier(0.22, 1, 0.36, 1);
}
@keyframes pop {
  from {
    opacity: 0;
    transform: translateY(-4px) scale(0.98);
  }
}

.menu button {
  display: flex;
  align-items: center;
  gap: 10px;
  height: 34px;
  padding: 0 10px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: #ececf1;
  font: inherit;
  font-size: 13px;
  text-align: left;
}
.menu button:hover:not(.soon) {
  background: #7d46d7;
}
.menu .label {
  flex: 1;
}
.menu .soon {
  color: #6b6b75;
  cursor: default;
}
.menu small {
  font-size: 11px;
  color: #6b6b75;
}
.line {
  height: 1px;
  margin: 5px 8px;
  background: rgba(255, 255, 255, 0.08);
}
</style>
