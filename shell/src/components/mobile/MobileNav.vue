<script setup lang="ts">
// Celular: barra de navegação de baixo. Início e Apps são abas da tela Início (e escondem os
// apps abertos); Arquivos, Ajustes e Emma abrem as janelas e o painel que o PC já tem.
// Deslizar a barra pra cima abre os apps recentes.
import { onMounted, onUnmounted, ref } from "vue";
import { emit } from "@tauri-apps/api/event";
import Icon from "../Icon.vue";
import { filesWindow, keyboardToggle, mobilePanel, recentsSnapshot, settingsOpen, toggleEmma } from "../../system";
import { swipe } from "../../swipe";
import { isMinimized, windowControl, windows } from "../../compositor";

type Tab = "inicio" | "apps" | "arquivos" | "ajustes" | "emma";
const active = ref<Tab>("inicio");

const items: { tab: Tab; label: string; icon: string }[] = [
  { tab: "inicio", label: "Início", icon: "home" },
  { tab: "apps", label: "Apps", icon: "grid" },
  { tab: "arquivos", label: "Arquivos", icon: "folder" },
  { tab: "ajustes", label: "Ajustes", icon: "settings" },
  { tab: "emma", label: "Emma", icon: "waves" },
];

// Volta para a tela Início: minimiza o que estiver aberto por cima (antes, tira a foto dele pros
// apps recentes).
async function showHome() {
  await recentsSnapshot();
  const list = await windows().catch(() => []);
  for (const w of list) if (!isMinimized(w)) await windowControl("minimize", w.address).catch(() => {});
}

// Como no celular: um app por vez. Se a janela já existe (mesmo minimizada), volta para ela em vez
// de abrir outra; e o que estava na frente sai de cena.
// (o Arquivos põe a pasta no título: "Downloads · Arquivos")
async function bringUp(title: string, open: () => Promise<void>) {
  const list = await windows().catch(() => []);
  const found = list.find((w) => w.title === title || w.title.endsWith(` · ${title}`));
  await recentsSnapshot();
  for (const w of list) if (w !== found && !isMinimized(w)) await windowControl("minimize", w.address).catch(() => {});
  if (found) await windowControl("focus", found.address);
  else await open();
}

async function go(tab: Tab) {
  active.value = tab;
  if (tab === "inicio" || tab === "apps") {
    await showHome();
    await emit("lumna://mobile-tab", tab);
  } else if (tab === "arquivos") await bringUp("Arquivos", () => filesWindow());
  else if (tab === "ajustes") await bringUp("Configurações", () => settingsOpen());
  else await toggleEmma();
}

const root = ref<HTMLElement>();
let stopSwipe: (() => void) | null = null;
async function openRecents() {
  await recentsSnapshot();
  await mobilePanel("mobile-recents", true);
}
onMounted(() => {
  if (root.value) stopSwipe = swipe(root.value, { on: { up: openRecents }, threshold: 24 });
});
onUnmounted(() => stopSwipe?.());
</script>

<template>
  <div ref="root" class="root">
  <nav class="nav">
    <button v-for="item in items" :key="item.tab" :class="{ on: active === item.tab }" @click="go(item.tab)">
      <Icon :name="item.icon" :size="24" />
      <span>{{ item.label }}</span>
    </button>
  </nav>
  <!-- a barrinha abre o teclado nos apps que não são do LUMNA (terminal, navegador...) -->
  <button class="handle-area" aria-label="Teclado" @click="keyboardToggle()"><div class="handle" /></button>
  </div>
</template>

<style scoped>
.root {
  height: 100vh;
  touch-action: none;
}
.nav {
  margin: 0 12px;
  height: 66px;
  display: flex;
  border: 1px solid var(--lumna-line);
  border-radius: 22px;
  background: rgba(16, 12, 30, 0.92);
}
button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 11px;
}
button.on {
  color: var(--lumna-violet-soft);
}
.handle-area {
  display: block;
  width: 100%;
  height: 26px;
  border: 0;
  background: none;
}
.handle {
  margin: 0 auto;
  width: 120px;
  height: 4px;
  border-radius: 2px;
  background: rgba(236, 232, 255, 0.7);
}
</style>
