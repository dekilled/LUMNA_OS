<script setup lang="ts">
// Celular: a barra flutuante da Início. O botão do meio é sempre Apps; os outros 4 são atalhos que
// a pessoa escolhe (segurar um abre a lista na Início, dock.ts). Abrindo os Apps (a gaveta em tela
// cheia, MobileHome.vue) a barra desce e some, e ficam só o botão Apps, no mesmo lugar e tamanho,
// e o botão de energia à direita. Apps de novo: volta pra Início e a barra sobe.
// A barrinha de baixo: toque ou deslize curto pra cima = teclado; deslize longo = apps recentes.
// Com um app aberto o dock some: a camada encolhe pra só a faixa da barrinha (o app desce até
// ela) e as bordas laterais ganham o gesto de voltar (MobileEdge.vue).
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { emit } from "@tauri-apps/api/event";
import HandleStrip from "./HandleStrip.vue";
import Icon from "../Icon.vue";
import { apps as listApps, haptic, layerResize, mobileEdges, on, powerMenuOpen, type App } from "../../system";
import { appUp, showHome, watchApps } from "./apps";
import { SLOTS_KEY, readSlots, runSlot, slotView } from "./dock";

const tab = ref<"inicio" | "apps">("inicio");
const drawer = computed(() => tab.value === "apps");
const slots = ref(readSlots());
const installed = ref<App[]>([]);
const views = computed(() => slots.value.map((s) => slotView(s, installed.value)));

const FULL = 92;
const HANDLE = 26;
watch(appUp, (up) => {
  layerResize(-1, up ? HANDLE : FULL).catch(() => {});
  mobileEdges(up).catch(() => {});
});
// com o teclado aberto quem desenha a barrinha (no mesmo lugar) é ele: a nossa some por baixo
const keyboardOver = ref(false);

function onStorage(e: StorageEvent) {
  if (e.key === SLOTS_KEY) slots.value = readSlots();
}
const stops: (() => void)[] = [];
onMounted(async () => {
  watchApps();
  window.addEventListener("storage", onStorage);
  stops.push(() => window.removeEventListener("storage", onStorage));
  installed.value = await listApps().catch(() => []);
  stops.push(await on<{ shown: boolean; lumna: boolean }>("lumna://keyboard", (k) => (keyboardOver.value = k.shown && k.lumna)));
  stops.push(
    await on<string>("lumna://mobile-tab", (t) => {
      tab.value = t === "apps" ? "apps" : "inicio";
      slots.value = readSlots();
    }),
  );
});
onUnmounted(() => stops.forEach((s) => s()));

function toggleDrawer() {
  haptic(10).catch(() => {});
  showHome(drawer.value ? "inicio" : "apps").catch(() => {});
}

// segurar um atalho: a Início abre a lista pra trocar o app dele
let hold = 0;
let held = false;
function holdStart(i: number) {
  held = false;
  clearTimeout(hold);
  hold = window.setTimeout(() => {
    held = true;
    haptic(30).catch(() => {});
    emit("lumna://nav-pick", i).catch(() => {});
  }, 550);
}
function holdEnd() {
  clearTimeout(hold);
}
async function tap(i: number) {
  if (held) {
    held = false;
    return;
  }
  if (!installed.value.length) installed.value = await listApps().catch(() => []);
  runSlot(slots.value[i], installed.value);
}
const isActive = (i: number) => slots.value[i] === "inicio" && tab.value === "inicio" && !appUp.value;
</script>

<template>
  <div class="root" :class="{ compact: appUp, drawer }">
    <nav v-show="!appUp" class="nav">
      <div class="bg" />
      <template v-for="(v, i) in views" :key="`${i}-${slots[i]}`">
        <button
          class="slot"
          :class="{ on: isActive(i) }"
          :style="{ order: i < 2 ? i : i + 1 }"
          @pointerdown="holdStart(i)"
          @pointerup="holdEnd"
          @pointercancel="holdEnd"
          @pointerleave="holdEnd"
          @contextmenu.prevent
          @click="tap(i)"
        >
          <img v-if="v.img" :src="v.img" alt="" />
          <Icon v-else :name="v.icon" :size="24" />
          <span>{{ v.label }}</span>
        </button>
      </template>
      <!-- o do meio: sempre Apps, no mesmo lugar e tamanho na Início e na gaveta -->
      <button class="slot apps" :class="{ on: drawer }" style="order: 2" @click="toggleDrawer()">
        <Icon name="grid" :size="24" />
        <span>Apps</span>
      </button>
      <button class="power" aria-label="Energia" :tabindex="drawer ? 0 : -1" @click="powerMenuOpen().catch(() => {})">
        <Icon name="power" :size="26" />
      </button>
    </nav>
    <HandleStrip :hidden="keyboardOver" />
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
  position: relative;
  margin: 0 12px;
  height: 66px;
  display: flex;
}
/* o fundo da barra: desce e some com a gaveta aberta (o botão Apps fica) */
.bg {
  position: absolute;
  inset: 0;
  border: 1px solid var(--lumna-line);
  border-radius: 22px;
  background: rgba(16, 12, 30, 0.92);
  transition:
    transform 0.28s cubic-bezier(0.2, 0.9, 0.3, 1),
    opacity 0.22s;
}
.slot {
  position: relative;
  flex: 1;
  min-width: 0;
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
  transition:
    transform 0.28s cubic-bezier(0.2, 0.9, 0.3, 1),
    opacity 0.22s;
}
.slot img {
  width: 26px;
  height: 26px;
  object-fit: contain;
}
.slot span {
  max-width: 92%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.slot.on {
  color: var(--lumna-violet-soft);
}
.power {
  position: absolute;
  right: 0;
  top: 0;
  width: 20%;
  height: 100%;
  display: grid;
  place-items: center;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  opacity: 0;
  pointer-events: none;
  transform: translateY(16px);
  transition:
    transform 0.28s cubic-bezier(0.2, 0.9, 0.3, 1),
    opacity 0.22s;
}
/* a gaveta aberta: a barra desce, só ficam Apps e energia */
.drawer .bg,
.drawer .slot:not(.apps) {
  opacity: 0;
  transform: translateY(90px);
  pointer-events: none;
}
.drawer .power {
  opacity: 1;
  pointer-events: auto;
  transform: none;
  transition-delay: 0.08s;
}
</style>
