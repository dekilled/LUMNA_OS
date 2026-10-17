<script setup lang="ts">
// Celular: apps recentes (o "RAM"). Abre deslizando a navegação pra cima.
// Cada cartão é uma janela aberta, com a última foto dela e quanta memória o app está usando.
// Toque abre; arrastar o cartão pra cima fecha o app; segurar abre o menu do app.
// Toque fora dos cartões (ou na barrinha) volta pra onde estava.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  apps as listApps,
  convertFileSrc,
  haptic,
  mobilePanel,
  on,
  recents as loadRecents,
  recentsKill,
  type App,
  type Recent,
} from "../../system";
import { isMinimized, onCompositorEvent, windowControl, windows } from "../../compositor";

const list = ref<Recent[]>([]);
const memory = ref({ total: 0, available: 0 });
const installed = ref<App[]>([]);
const strip = ref<HTMLElement>();
const current = ref(0);
const menu = ref<Recent | null>(null);
const error = ref("");
// arrasto de um cartão: quanto subiu, e os que estão saindo
const drag = ref<{ address: string; dy: number } | null>(null);
const leaving = ref(new Set<string>());

// Janelas do próprio LUMNA (sem memória própria): nome e ícone pelo título.
const LUMNA_WINDOWS: [string, string][] = [
  ["Arquivos", "folder"],
  ["Configurações", "settings"],
  ["Loja", "download"],
];

function appFor(r: Recent): App | undefined {
  const cls = r.class.toLowerCase();
  return installed.value.find((a) => {
    const id = a.id.toLowerCase().replace(/\.desktop$/, "");
    return a.wm_class?.toLowerCase() === cls || id === cls || id.endsWith(`.${cls}`);
  });
}
function nameOf(r: Recent): string {
  if (r.memory_kb == null) {
    const own = LUMNA_WINDOWS.find(([t]) => r.title === t || r.title.endsWith(` · ${t}`));
    if (own) return own[0];
  }
  return appFor(r)?.name ?? (r.title || r.class);
}
function glyphOf(r: Recent): string {
  return LUMNA_WINDOWS.find(([t]) => r.title === t || r.title.endsWith(` · ${t}`))?.[1] ?? "app";
}
const shotUrl = (r: Recent) => (r.shot ? `${convertFileSrc(r.shot)}?t=${r.shot_at}` : null);

function size(kb: number): string {
  if (kb >= 1024 * 1024) return `${(kb / 1024 / 1024).toFixed(1).replace(".", ",")} GB`;
  return `${Math.round(kb / 1024)} MB`;
}
const subtitle = computed(() => {
  const n = list.value.length;
  const apps = n === 0 ? "Nenhum app aberto" : n === 1 ? "1 aplicativo aberto" : `${n} aplicativos abertos`;
  return memory.value.total ? `${apps} · ${size(memory.value.available)} livres de ${size(memory.value.total)}` : apps;
});

async function load() {
  try {
    const r = await loadRecents();
    list.value = r.apps.filter((a) => !leaving.value.has(a.address));
    memory.value = { total: r.total_kb, available: r.available_kb };
  } catch (e) {
    error.value = String(e);
  }
}

const close = () => mobilePanel("mobile-recents", false);

// Abre o app do cartão: o que estava na frente sai, ele vem.
async function open(r: Recent) {
  for (const w of await windows().catch(() => [])) {
    if (w.address !== r.address && !isMinimized(w)) await windowControl("minimize", w.address).catch(() => {});
  }
  await windowControl("focus", r.address).catch(() => {});
  await close();
}

async function closeApp(r: Recent) {
  leaving.value.add(r.address);
  haptic(20).catch(() => {});
  await windowControl("close", r.address).catch(() => {});
  window.setTimeout(() => {
    list.value = list.value.filter((a) => a.address !== r.address);
    leaving.value.delete(r.address);
    load();
  }, 260);
}

async function closeAll() {
  for (const r of list.value) leaving.value.add(r.address);
  for (const r of list.value) await windowControl("close", r.address).catch(() => {});
  window.setTimeout(() => {
    list.value = [];
    leaving.value.clear();
    load();
    close();
  }, 300);
}

async function forceStop(r: Recent) {
  menu.value = null;
  try {
    await recentsKill(r.address);
    leaving.value.add(r.address);
    window.setTimeout(() => {
      leaving.value.delete(r.address);
      load();
    }, 300);
  } catch (e) {
    error.value = String(e);
  }
}

// ---- gesto do cartão: toque abre, pra cima fecha, segurar abre o menu ----
let start: { x: number; y: number; id: number; r: Recent; vertical: boolean | null } | null = null;
let hold = 0;
let moved = false;
// O WebKitGTK repete o toque como "mouse" logo depois de soltar: esse eco é ignorado.
let lastTouch = -1e9;

function cardDown(e: PointerEvent, r: Recent) {
  if (start) return;
  if (e.pointerType !== "touch" && performance.now() - lastTouch < 800) return;
  if (e.pointerType === "touch") lastTouch = performance.now();
  start = { x: e.clientX, y: e.clientY, id: e.pointerId, r, vertical: null };
  moved = false;
  clearTimeout(hold);
  hold = window.setTimeout(() => {
    if (start && !moved) {
      haptic(30).catch(() => {});
      menu.value = r;
      start = null;
    }
  }, 550);
}
function cardMove(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const dx = e.clientX - start.x;
  const dy = e.clientY - start.y;
  if (Math.hypot(dx, dy) > 8) {
    moved = true;
    clearTimeout(hold);
  }
  // decide uma vez: vertical = fechar; horizontal = a lista rola sozinha
  if (start.vertical === null && Math.hypot(dx, dy) > 10) {
    start.vertical = Math.abs(dy) > Math.abs(dx);
    if (start.vertical) (e.currentTarget as HTMLElement).setPointerCapture?.(e.pointerId);
  }
  if (start.vertical) drag.value = { address: start.r.address, dy: Math.min(0, dy) };
}
function cardUp(e: PointerEvent) {
  clearTimeout(hold);
  if (!start || e.pointerId !== start.id) return;
  if (e.pointerType === "touch") lastTouch = performance.now();
  const r = start.r;
  const wasVertical = start.vertical;
  start = null;
  const dy = drag.value?.dy ?? 0;
  drag.value = null;
  if (wasVertical && dy < -110) closeApp(r);
  else if (!moved) open(r);
}
function cardCancel() {
  clearTimeout(hold);
  start = null;
  drag.value = null;
}

function cardStyle(r: Recent) {
  if (leaving.value.has(r.address)) return { transform: "translateY(-120%)", opacity: "0" };
  if (drag.value?.address === r.address) {
    const dy = drag.value.dy;
    return { transform: `translateY(${dy}px)`, opacity: String(Math.max(0.3, 1 + dy / 400)), transition: "none" };
  }
  return {};
}

function onScroll() {
  const el = strip.value;
  if (!el) return;
  const card = el.querySelector<HTMLElement>(".slot");
  if (!card) return;
  current.value = Math.round(el.scrollLeft / (card.offsetWidth + 14));
}

// O app que estava na frente fica no meio quando os recentes abrem.
async function centerFirst() {
  await nextTick();
  strip.value?.scrollTo({ left: 0 });
  current.value = 0;
}

let timer = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  installed.value = await listApps().catch(() => []);
  await load();
  stops.push(
    await on<boolean>("lumna://mobile-panel", async (shown) => {
      clearInterval(timer);
      menu.value = null;
      error.value = "";
      if (!shown) return;
      await load();
      centerFirst();
      // a memória muda enquanto olha: atualiza de tempos em tempos
      timer = window.setInterval(load, 2500);
    }),
  );
  stops.push(
    await onCompositorEvent((ev) => {
      if (ev.kind === "window_opened" || ev.kind === "window_closed") load();
    }),
  );
});
onUnmounted(() => {
  clearInterval(timer);
  stops.forEach((s) => s());
});
</script>

<template>
  <main class="recents" @click.self="close()">
    <header>
      <div class="brand"><span class="gem" /> LUMNA</div>
      <h1>Apps recentes</h1>
      <p>{{ subtitle }}</p>
    </header>

    <div v-if="list.length === 0" class="empty" @click="close()">
      <Icon name="stack" :size="40" />
      <span>Nada aberto. Os apps que você abrir aparecem aqui.</span>
    </div>

    <div v-else ref="strip" class="strip" @scroll.passive="onScroll" @click.self="close()">
      <div v-for="(r, i) in list" :key="r.address" class="slot" :class="{ on: i === current }">
        <div class="title">
          <img v-if="appFor(r)?.icon" :src="appFor(r)!.icon!" alt="" />
          <span v-else class="glyph"><Icon :name="glyphOf(r)" :size="20" /></span>
          <span class="name">{{ nameOf(r) }}</span>
          <button v-if="i === current" class="x" aria-label="Fechar app" @click.stop="closeApp(r)">
            <Icon name="close" :size="18" />
          </button>
        </div>
        <div
          class="card"
          :style="cardStyle(r)"
          @pointerdown="cardDown($event, r)"
          @pointermove="cardMove"
          @pointerup="cardUp"
          @pointercancel="cardCancel"
          @contextmenu.prevent
        >
          <img v-if="shotUrl(r)" class="shot" :src="shotUrl(r)!" alt="" draggable="false" />
          <div v-else class="placeholder">
            <img v-if="appFor(r)?.icon" :src="appFor(r)!.icon!" alt="" />
            <Icon v-else :name="glyphOf(r)" :size="56" />
            <span>{{ r.title }}</span>
          </div>
        </div>
        <div class="mem">
          <Icon name="memory" :size="14" />
          {{ r.memory_kb == null ? "parte do LUMNA" : size(r.memory_kb) }}
        </div>
      </div>
    </div>

    <div v-if="list.length > 1" class="dots">
      <span v-for="(r, i) in list" :key="r.address" :class="{ on: i === current }" />
    </div>
    <p v-if="list.length" class="hint">Toque para abrir · Deslize para cima para fechar</p>
    <p v-if="error" class="error" @click="error = ''">{{ error }}</p>

    <button v-if="list.length" class="all" @click="closeAll()"><Icon name="stack" :size="20" /> Fechar todos</button>
    <button class="handle-area" aria-label="Voltar" @click="close()"><div class="handle" /></button>

    <!-- menu do app (segurar o cartão) -->
    <div v-if="menu" class="sheet-veil" @click.self="menu = null">
      <div class="sheet">
        <div class="sheet-head">
          <img v-if="appFor(menu)?.icon" :src="appFor(menu)!.icon!" alt="" />
          <span v-else class="glyph"><Icon :name="glyphOf(menu)" :size="22" /></span>
          <div>
            <strong>{{ nameOf(menu) }}</strong>
            <small>{{ menu.memory_kb == null ? "janela do LUMNA" : `${size(menu.memory_kb)} · processo ${menu.pid}` }}</small>
          </div>
        </div>
        <button @click="open(menu); menu = null"><Icon name="forward" :size="20" /> Abrir</button>
        <button @click="closeApp(menu); menu = null"><Icon name="close" :size="20" /> Fechar</button>
        <button v-if="menu.memory_kb != null" class="danger" @click="forceStop(menu)">
          <Icon name="zap" :size="20" /> Forçar parada
        </button>
      </div>
    </div>
  </main>
</template>

<style scoped>
.recents {
  height: 100vh;
  box-sizing: border-box;
  padding: 44px 0 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background:
    radial-gradient(circle at 115% -5%, transparent 44%, rgba(167, 139, 250, 0.45) 45%, rgba(91, 33, 182, 0.2) 47%, transparent 56%),
    radial-gradient(ellipse at 30% 60%, rgba(29, 18, 64, 0.97), rgba(7, 4, 15, 0.98) 70%);
  user-select: none;
}
header {
  padding: 0 22px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  letter-spacing: 0.5em;
  font-size: 13px;
  color: var(--lumna-violet-soft);
}
.gem {
  width: 14px;
  height: 14px;
  transform: rotate(45deg);
  border: 2px solid var(--lumna-violet-soft);
  border-radius: 3px;
}
h1 {
  margin: 18px 0 2px;
  font-size: 24px;
  font-weight: 500;
}
header p {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 14px;
}
.strip {
  flex: 1;
  min-height: 0;
  margin-top: 18px;
  display: flex;
  gap: 14px;
  /* o cartão do meio fica centralizado; os vizinhos aparecem nas bordas */
  padding: 0 18vw;
  overflow-x: auto;
  scroll-snap-type: x mandatory;
  scrollbar-width: none;
  touch-action: pan-x;
}
.strip::-webkit-scrollbar {
  display: none;
}
.slot {
  flex: 0 0 64vw;
  display: flex;
  flex-direction: column;
  scroll-snap-align: center;
  opacity: 0.6;
  transform: scale(0.94);
  transition: opacity 200ms ease, transform 200ms ease;
}
.slot.on {
  opacity: 1;
  transform: none;
}
.title {
  height: 44px;
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 8px;
}
.title img,
.glyph {
  width: 30px;
  height: 30px;
  border-radius: 9px;
  flex: none;
}
.glyph {
  display: grid;
  place-items: center;
  background: #3c3355;
  color: var(--lumna-violet-soft);
}
.name {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
  font-size: 16px;
}
.x {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.1);
  color: inherit;
}
.card {
  flex: 1;
  min-height: 0;
  overflow: hidden;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 22px;
  background: #121019;
  touch-action: pan-x;
  transition: transform 260ms ease, opacity 260ms ease;
}
.slot.on .card {
  border: 2px solid var(--lumna-violet-soft);
}
.shot {
  width: 100%;
  height: 100%;
  object-fit: cover;
  object-position: top;
  pointer-events: none;
}
.placeholder {
  height: 100%;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 14px;
  padding: 0 16px;
  color: var(--lumna-violet-soft);
  text-align: center;
}
.placeholder img {
  width: 64px;
  height: 64px;
}
.placeholder span {
  color: var(--lumna-muted);
  font-size: 13px;
}
.mem {
  margin-top: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  color: var(--lumna-muted);
  font-size: 12px;
}
.empty {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 14px;
  padding: 0 48px;
  color: var(--lumna-muted);
  text-align: center;
}
.dots {
  margin-top: 14px;
  display: flex;
  justify-content: center;
  gap: 10px;
}
.dots span {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.25);
}
.dots span.on {
  background: var(--lumna-violet-soft);
}
.hint {
  margin: 12px 0 0;
  color: var(--lumna-muted);
  font-size: 12px;
  text-align: center;
}
.error {
  margin: 8px 20px 0;
  color: #fca5a5;
  font-size: 12px;
  text-align: center;
}
.all {
  align-self: center;
  margin-top: 14px;
  padding: 12px 26px;
  display: flex;
  align-items: center;
  gap: 10px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 16px;
  background: rgba(20, 17, 30, 0.9);
  color: inherit;
  font: inherit;
  font-size: 16px;
}
.handle-area {
  width: 100%;
  height: 34px;
  margin-top: 8px;
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
.sheet-veil {
  position: fixed;
  inset: 0;
  display: flex;
  align-items: flex-end;
  background: rgba(5, 3, 14, 0.55);
}
.sheet {
  width: 100%;
  padding: 18px 14px 30px;
  display: flex;
  flex-direction: column;
  gap: 6px;
  border-radius: 22px 22px 0 0;
  background: #17141f;
}
.sheet-head {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0 6px 10px;
}
.sheet-head img,
.sheet-head .glyph {
  width: 40px;
  height: 40px;
  border-radius: 11px;
}
.sheet-head div {
  display: flex;
  flex-direction: column;
}
.sheet-head small {
  color: var(--lumna-muted);
}
.sheet button {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 14px 12px;
  border: 0;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  font-size: 15px;
  text-align: left;
}
.sheet button.danger {
  color: #fca5a5;
}
</style>
