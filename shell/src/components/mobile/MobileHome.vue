<script setup lang="ts">
// Celular: a tela Início (relógio grande e "Pergunte à Emma") e a gaveta de apps. A gaveta abre por
// cima do papel de parede, em tela cheia e escurecida (#111 a 65%), pelo botão Apps da barra ou
// deslizando pra cima no papel de parede (fora de uma faixa morta nas bordas, que é dos outros
// gestos). Tem a Loja, a busca, as categorias (do que cada app declara), a grade e os recentes.
// A barra fica em outra camada (MobileNav.vue); as duas falam pelo evento lumna://mobile-tab.
// Segurar um atalho da barra abre aqui a lista pra trocar o app dele.
import { computed, onMounted, onUnmounted, ref } from "vue";
import type { UnlistenFn } from "@tauri-apps/api/event";
import Icon from "../Icon.vue";
import { showHome } from "./apps";
import { BUILTIN, RECENT_KEY, edgeDeadZone, openApp, readRecent, readSlots, writeSlots } from "./dock";
import { apps, artUrl, desktopWallpaper, haptic, on, storeOpen, toggleEmma, type App } from "../../system";

const tab = ref<"inicio" | "apps">("inicio");
const wallpaper = ref<string | null>(null);
const list = ref<App[]>([]);
const query = ref("");
const time = ref("");
const date = ref("");
const recent = ref<string[]>(readRecent());
let timer = 0;
const stops: UnlistenFn[] = [];

type Category = "todos" | "trabalho" | "midia" | "jogos";
const CATEGORIES: { id: Category; label: string }[] = [
  { id: "todos", label: "Todos" },
  { id: "trabalho", label: "Trabalho" },
  { id: "midia", label: "Mídia" },
  { id: "jogos", label: "Jogos" },
];
const category = ref<Category>("todos");

function tick() {
  const now = new Date();
  time.value = now.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
  const text = now.toLocaleDateString("pt-BR", { weekday: "long", day: "numeric", month: "long" });
  date.value = text.charAt(0).toUpperCase() + text.slice(1);
}

// Busca sem acento, como no launcher do PC.
const plain = (s: string) => s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
const shown = computed(() => {
  const q = plain(query.value.trim());
  if (q) return list.value.filter((a) => plain(a.name).includes(q));
  return category.value === "todos" ? list.value : list.value.filter((a) => a.category === category.value);
});
const recentApps = computed(() => recent.value.map((id) => list.value.find((a) => a.id === id)).filter((a): a is App => !!a).slice(0, 4));

async function open(app: App) {
  query.value = "";
  await openApp(app);
  recent.value = readRecent();
}

// ---- deslizar pra cima no papel de parede abre a gaveta ----

let start: { x: number; y: number; id: number } | null = null;
function startOn(x: number, y: number, id: number, target: EventTarget | null) {
  if (tab.value !== "inicio" || (target as Element | null)?.closest?.("button, input")) return;
  const dead = edgeDeadZone();
  // as bordas (a faixa morta) são dos outros gestos: voltar, a central, os recentes
  if (x < dead || x > window.innerWidth - dead || y < dead) return;
  start = { x, y, id };
}
function endOn(x: number, y: number, id: number) {
  const s = start;
  start = null;
  if (!s || s.id !== id) return;
  const dy = y - s.y;
  if (dy < -70 && Math.abs(dy) > Math.abs(x - s.x) * 1.4) {
    haptic(10).catch(() => {});
    showHome("apps").catch(() => {});
  }
}
// o toque pelos eventos de toque (o WebKitGTK manda o mesmo dedo também como "mouse")
let lastTouch = 0;
function tstart(e: TouchEvent) {
  lastTouch = Date.now();
  const t = e.changedTouches[0];
  if (t && e.touches.length === 1) startOn(t.clientX, t.clientY, t.identifier, e.target);
}
function tend(e: TouchEvent) {
  lastTouch = Date.now();
  const t = e.changedTouches[0];
  if (t) endOn(t.clientX, t.clientY, t.identifier);
}
function pdown(e: PointerEvent) {
  if (e.pointerType === "mouse" && Date.now() - lastTouch > 1000) startOn(e.clientX, e.clientY, -1, e.target);
}
function pup(e: PointerEvent) {
  if (e.pointerType === "mouse" && Date.now() - lastTouch > 1000) endOn(e.clientX, e.clientY, -1);
}

// ---- trocar um atalho da barra ----

const picking = ref<number | null>(null);
function pick(slot: string) {
  if (picking.value == null) return;
  const slots = readSlots();
  slots[picking.value] = slot;
  writeSlots(slots);
  picking.value = null;
  haptic(15).catch(() => {});
}
const BUILTIN_LIST = Object.entries(BUILTIN).map(([id, v]) => ({ id, ...v }));

function onStorage(e: StorageEvent) {
  if (e.key === RECENT_KEY) recent.value = readRecent();
}

onMounted(async () => {
  tick();
  timer = window.setInterval(tick, 5000);
  window.addEventListener("storage", onStorage);
  wallpaper.value = await desktopWallpaper().catch(() => null);
  list.value = await apps().catch(() => []);
  stops.push(
    await on<"inicio" | "apps">("lumna://mobile-tab", async (t) => {
      tab.value = t;
      if (t === "apps") {
        recent.value = readRecent();
        list.value = await apps().catch(() => list.value);
      } else {
        query.value = "";
        (document.activeElement as HTMLElement | null)?.blur?.();
      }
    }),
  );
  stops.push(
    await on<number>("lumna://nav-pick", async (i) => {
      picking.value = i;
      if (!list.value.length) list.value = await apps().catch(() => []);
    }),
  );
  stops.push(await on<string>("lumna://wallpaper", (w) => (wallpaper.value = w)));
});
onUnmounted(() => {
  clearInterval(timer);
  window.removeEventListener("storage", onStorage);
  stops.forEach((s) => s());
});
</script>

<template>
  <main
    class="home"
    :style="wallpaper ? { backgroundImage: `url(${artUrl(wallpaper)})` } : {}"
    @touchstart.passive="tstart"
    @touchend="tend"
    @touchcancel="start = null"
    @pointerdown="pdown"
    @pointerup="pup"
  >
    <header v-show="tab === 'inicio'" class="brand"><span class="gem" /> LUMNA</header>

    <!-- Início -->
    <section class="inicio" :class="{ away: tab === 'apps' }">
      <div class="clock">
        <div class="time">{{ time }}</div>
        <div class="date">{{ date }}</div>
      </div>
      <button class="ask" :tabindex="tab === 'apps' ? -1 : 0" @click="toggleEmma()">
        <Icon name="mic" :size="22" />
        <span>Pergunte à Emma</span>
        <Icon name="forward" :size="18" />
      </button>
    </section>

    <!-- a gaveta de apps -->
    <Transition name="drawer">
      <section v-if="tab === 'apps'" class="drawer">
        <header class="brand"><span class="gem" /> LUMNA</header>
        <div class="title-row">
          <h1>Aplicativos</h1>
          <button class="store" @click="storeOpen().catch(() => {})"><Icon name="download" :size="22" /> Loja</button>
        </div>
        <label class="search">
          <Icon name="search" :size="20" />
          <input v-model="query" placeholder="Buscar aplicativos" />
        </label>
        <nav v-if="!query" class="cats">
          <button v-for="c in CATEGORIES" :key="c.id" :class="{ on: category === c.id }" @click="category = c.id">{{ c.label }}</button>
        </nav>
        <div class="scroll">
          <div class="grid">
            <button v-for="app in shown" :key="app.id" class="app" @click="open(app)">
              <img v-if="app.icon" :src="app.icon" alt="" />
              <Icon v-else name="app" :size="40" />
              <span>{{ app.name }}</span>
            </button>
          </div>
          <p v-if="!shown.length" class="empty">{{ query ? `Nenhum app com “${query}”.` : "Nenhum app nesta categoria ainda." }}</p>
          <template v-if="!query && category === 'todos' && recentApps.length">
            <div class="section"><span>Recentes</span><i /></div>
            <div class="recent">
              <button v-for="app in recentApps" :key="app.id" @click="open(app)">
                <img v-if="app.icon" :src="app.icon" alt="" />
                <Icon v-else name="app" :size="28" />
                <span>{{ app.name }}</span>
              </button>
            </div>
          </template>
        </div>
      </section>
    </Transition>

    <!-- trocar um atalho da barra (segurar nele) -->
    <div v-if="picking != null" class="veil" @click.self="picking = null">
      <div class="sheet">
        <h2>Atalho da barra</h2>
        <p>Escolha o que este lugar abre.</p>
        <div class="pick-list">
          <button v-for="b in BUILTIN_LIST" :key="b.id" @click="pick(b.id)">
            <span class="ico"><Icon :name="b.icon" :size="22" /></span>
            {{ b.label }}
          </button>
          <button v-for="app in list" :key="app.id" @click="pick(`app:${app.id}`)">
            <span class="ico"><img v-if="app.icon" :src="app.icon" alt="" /><Icon v-else name="app" :size="22" /></span>
            {{ app.name }}
          </button>
        </div>
      </div>
    </div>
  </main>
</template>

<style scoped>
.home {
  /* o #app não estica: a camada inteira é a viewport */
  position: relative;
  height: 100vh;
  box-sizing: border-box;
  padding: 44px 20px 104px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  /* sem papel de parede: o eclipse roxo do sketch */
  background:
    radial-gradient(circle at 115% -5%, transparent 44%, rgba(167, 139, 250, 0.55) 45%, rgba(91, 33, 182, 0.25) 47%, transparent 56%),
    radial-gradient(ellipse at 30% 60%, #1d1240, #07040f 70%);
  background-size: cover;
  background-position: center;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  letter-spacing: 0.5em;
  font-size: 15px;
}
.gem {
  width: 18px;
  height: 18px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.inicio {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
  transition: opacity 0.25s;
}
.inicio .ask {
  transition:
    opacity 0.3s ease,
    transform 0.35s cubic-bezier(0.2, 0.9, 0.3, 1);
}
/* a gaveta aberta: a Início some; voltando, o "Pergunte à Emma" aparece com fade e sobe um pouco */
.inicio.away {
  opacity: 0;
  pointer-events: none;
}
.inicio.away .ask {
  opacity: 0;
  transform: translateY(24px);
}
.clock {
  margin-top: 18vh;
  text-align: center;
}
.time {
  font-size: 84px;
  font-weight: 200;
  line-height: 1;
}
.date {
  margin-top: 10px;
  font-size: 16px;
}
.ask,
.search {
  display: flex;
  align-items: center;
  gap: 14px;
  height: 50px;
  padding: 0 18px;
  border: 1px solid var(--lumna-line);
  border-radius: 25px;
  background: rgba(16, 12, 30, 0.85);
  color: var(--lumna-muted);
  font: inherit;
  font-size: 15px;
}
.ask :first-child {
  color: var(--lumna-violet-soft);
}
.ask span {
  flex: 1;
  text-align: left;
}

/* ---- a gaveta ---- */
.drawer {
  position: absolute;
  inset: 0;
  z-index: 2;
  box-sizing: border-box;
  padding: 44px 20px 104px;
  display: flex;
  flex-direction: column;
  background: rgba(17, 17, 17, 0.65);
  backdrop-filter: blur(2px);
}
.drawer-enter-active,
.drawer-leave-active {
  transition:
    opacity 0.25s,
    transform 0.3s cubic-bezier(0.2, 0.9, 0.3, 1);
}
.drawer-enter-from,
.drawer-leave-to {
  opacity: 0;
  transform: translateY(24px);
}
.title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin: 26px 0 16px;
}
h1 {
  margin: 0;
  font-size: 36px;
  font-weight: 400;
}
.store {
  display: flex;
  align-items: center;
  gap: 10px;
  height: 44px;
  padding: 0 6px 0 12px;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
}
.search input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
}
.cats {
  display: flex;
  margin-top: 18px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}
.cats button {
  flex: 1;
  height: 44px;
  border: 0;
  border-bottom: 2px solid transparent;
  background: none;
  color: #d9d3f0;
  font: inherit;
  font-size: 15px;
}
.cats button.on {
  border-bottom-color: var(--lumna-violet-soft);
  color: var(--lumna-text);
}
.scroll {
  flex: 1;
  min-height: 0;
  margin-top: 18px;
  overflow-y: auto;
  scrollbar-width: none;
}
.grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  grid-auto-rows: 118px;
  gap: 12px;
}
.app {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 12px;
  min-width: 0;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 18px;
  background: rgba(20, 18, 28, 0.85);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.app img {
  width: 46px;
  height: 46px;
  object-fit: contain;
}
.app span {
  max-width: 90%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.empty {
  margin: 24px 0;
  color: var(--lumna-muted);
  text-align: center;
}
.section {
  display: flex;
  align-items: center;
  gap: 14px;
  margin: 22px 0 12px;
  color: #e4defa;
  font-size: 15px;
}
.section i {
  flex: 1;
  height: 1px;
  background: rgba(255, 255, 255, 0.1);
}
.recent {
  display: grid;
  grid-template-columns: repeat(2, 1fr);
  gap: 12px;
}
.recent button {
  display: flex;
  align-items: center;
  gap: 14px;
  min-width: 0;
  height: 58px;
  padding: 0 14px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 16px;
  background: rgba(20, 18, 28, 0.85);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.recent img {
  width: 28px;
  height: 28px;
  object-fit: contain;
}
.recent span {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

/* ---- trocar atalho ---- */
.veil {
  position: absolute;
  inset: 0;
  z-index: 5;
  display: flex;
  align-items: flex-end;
  padding-bottom: 104px;
  background: rgba(4, 3, 10, 0.55);
}
.sheet {
  width: 100%;
  max-height: 70vh;
  display: flex;
  flex-direction: column;
  padding: 18px 16px 10px;
  border: 1px solid var(--lumna-line);
  border-radius: 22px;
  background: #17141f;
}
.sheet h2 {
  margin: 0;
  font-size: 18px;
}
.sheet p {
  margin: 4px 0 12px;
  color: var(--lumna-muted);
  font-size: 14px;
}
.pick-list {
  overflow-y: auto;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.pick-list button {
  display: flex;
  align-items: center;
  gap: 14px;
  min-height: 52px;
  padding: 0 8px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  text-align: left;
}
.pick-list button:active {
  background: rgba(167, 139, 250, 0.1);
}
.ico {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border-radius: 10px;
  background: rgba(167, 139, 250, 0.12);
  color: var(--lumna-violet-soft);
}
.ico img {
  width: 28px;
  height: 28px;
  object-fit: contain;
}
</style>
