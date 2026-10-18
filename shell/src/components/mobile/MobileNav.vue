<script setup lang="ts">
// Celular: barra de navegação de baixo. Início e Apps são abas da tela Início (e escondem os
// apps abertos); Arquivos, Ajustes e Emma abrem as janelas e o painel que o PC já tem.
// A barrinha de baixo: toque ou deslize curto pra cima = teclado; deslize longo = apps recentes.
// Com um app aberto o dock some: a camada encolhe pra só a faixa da barrinha (o app desce até
// ela) e as bordas laterais ganham o gesto de voltar (MobileEdge.vue).
import { computed, onMounted, ref, watch } from "vue";
import Icon from "../Icon.vue";
import { filesWindow, haptic, keyboardToggle, layerResize, mobileEdges, mobilePanel, on, recentsSnapshot, settingsOpen, toggleEmma } from "../../system";
import { windows } from "../../compositor";
import { appUp, showApp, showHome, watchApps } from "./apps";

type Tab = "inicio" | "apps" | "arquivos" | "ajustes" | "emma";
const active = ref<Tab>("inicio");

const items: { tab: Tab; label: string; icon: string }[] = [
  { tab: "inicio", label: "Início", icon: "home" },
  { tab: "apps", label: "Apps", icon: "grid" },
  { tab: "arquivos", label: "Arquivos", icon: "folder" },
  { tab: "ajustes", label: "Ajustes", icon: "settings" },
  { tab: "emma", label: "Emma", icon: "waves" },
];

const FULL = 92;
const HANDLE = 26;
watch(appUp, (up) => {
  layerResize(-1, up ? HANDLE : FULL).catch(() => {});
  mobileEdges(up).catch(() => {});
  if (!up && active.value !== "inicio" && active.value !== "apps") active.value = "inicio";
});
onMounted(async () => {
  watchApps();
  // um app do LUMNA está abrindo: some o dock antes dele aparecer (o compositor confirma depois)
  await on("lumna://app-opening", () => (appUp.value = true));
  await on<string>("lumna://mobile-tab", (t) => (active.value = t as Tab));
});

// Como no celular: um app por vez. Se a janela já existe (mesmo minimizada), volta para ela em vez
// de abrir outra; e o que estava na frente sai de cena.
// (o Arquivos põe a pasta no título: "Downloads · Arquivos")
async function bringUp(title: string, open: () => Promise<void>) {
  const list = await windows().catch(() => []);
  const found = list.find((w) => w.title === title || w.title.endsWith(` · ${title}`));
  // (sem foto pros recentes aqui: o grim trava o compositor um instante a cada troca de app; a
  // miniatura é tirada ao abrir os recentes)
  appUp.value = true;
  // já aberto: vem com a troca animada; senão abre numa área vazia (a Início sai animada antes)
  if (found) await showApp(found.address).catch(() => {});
  else {
    await showApp().catch(() => {});
    await open();
  }
}

async function go(tab: Tab) {
  active.value = tab;
  if (tab === "inicio" || tab === "apps") await showHome(tab); else if (tab === "arquivos") await bringUp("Arquivos", () => filesWindow());
  else if (tab === "ajustes") await bringUp("Configurações", () => settingsOpen());
  else await toggleEmma();
}

async function openRecents() {
  await recentsSnapshot();
  await mobilePanel("mobile-recents", true);
}

// ---- gesto da barrinha ----
// Curto (até LONG px pra cima) abre/fecha o teclado; longo abre os recentes. A barrinha estica
// enquanto o dedo sobe e dá um toque de vibração quando passa pro "longo".
const SHORT = 24;
const LONG = 140;
const lift = ref(0);
let start: { y: number; id: number } | null = null;
let swallow = false;
const long = computed(() => lift.value >= LONG);

function handleDown(e: PointerEvent) {
  if (start) return;
  start = { y: e.clientY, id: e.pointerId };
  lift.value = 0;
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}
function handleMove(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const was = long.value;
  lift.value = Math.max(0, start.y - e.clientY);
  if (long.value && !was) haptic(25).catch(() => {});
}
function handleUp(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  start = null;
  const up = lift.value;
  lift.value = 0;
  if (up < SHORT) return; // toque: o clique cuida
  swallow = true;
  window.setTimeout(() => (swallow = false), 400);
  if (up >= LONG) openRecents();
  else keyboardToggle();
}
function handleCancel() {
  start = null;
  lift.value = 0;
}
function handleClick() {
  if (swallow) {
    swallow = false;
    return;
  }
  keyboardToggle();
}
</script>

<template>
  <div class="root" :class="{ compact: appUp }">
  <nav v-show="!appUp" class="nav">
    <button v-for="item in items" :key="item.tab" :class="{ on: active === item.tab }" @click="go(item.tab)">
      <Icon :name="item.icon" :size="24" />
      <span>{{ item.label }}</span>
    </button>
  </nav>
  <!-- a barrinha abre o teclado nos apps que não são do LUMNA (terminal, navegador...) -->
  <button
    class="handle-area"
    aria-label="Teclado (deslize longo: apps recentes)"
    @pointerdown="handleDown"
    @pointermove="handleMove"
    @pointerup="handleUp"
    @pointercancel="handleCancel"
    @click="handleClick"
  >
    <div class="handle" :class="{ long }" :style="{ width: `${120 + Math.min(lift, LONG) * 0.5}px` }" />
  </button>
  </div>
</template>

<style scoped>
.root {
  height: 100vh;
  touch-action: none;
}
/* com um app aberto: só a faixa da barrinha, fechada (nada do fundo aparece embaixo do app) */
.root.compact {
  background: #111015;
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
  transition: background 120ms ease;
}
.handle.long {
  background: var(--lumna-violet-soft);
}
</style>
