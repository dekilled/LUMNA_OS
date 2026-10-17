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
// cartões saindo (fechados)
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
  // o cartão do meio saiu: o vizinho da direita (ou o da esquerda, se era o último) toma o lugar
  const index = list.value.indexOf(r);
  haptic(20).catch(() => {});
  await windowControl("close", r.address).catch(() => {});
  window.setTimeout(async () => {
    list.value = list.value.filter((a) => a.address !== r.address);
    leaving.value.delete(r.address);
    if (index >= 0 && current.value >= list.value.length) current.value = Math.max(0, list.value.length - 1);
    await nextTick();
    paintTrack(0, true);
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

// ---- gestos: o carrossel é nosso (sem a rolagem nativa) ----
// Arrastar pro lado move a fila inteira com o dedo e, ao soltar, encaixa no cartão mais perto
// (ou no vizinho, se o gesto foi rápido). Arrastar um cartão pra cima fecha o app. Toque abre,
// segurar abre o menu. Durante o arrasto o estilo é escrito direto no elemento, a cada quadro,
// sem passar pelo Vue: é o que deixa a animação lisa no 6T.
const GAP = 14;
let start: {
  x: number;
  y: number;
  t: number;
  id: number;
  index: number;
  card: HTMLElement | null;
  axis: "x" | "y" | null;
} | null = null;
let hold = 0;
let moved = false;
let frame = 0;
let lastX = 0;
let lastT = 0;
let velocity = 0;
// O WebKitGTK repete o toque como "mouse" logo depois de soltar: esse eco é ignorado.
let lastTouch = -1e9;

function step(): number {
  const slot = strip.value?.querySelector<HTMLElement>(".slot");
  return (slot?.offsetWidth ?? 0) + GAP;
}
function baseOffset(index: number): number {
  const el = strip.value;
  const slot = el?.querySelector<HTMLElement>(".slot");
  if (!el || !slot) return 0;
  return (el.clientWidth - slot.offsetWidth) / 2 - index * step();
}
function paintTrack(dx: number, animate: boolean) {
  const track = strip.value?.querySelector<HTMLElement>(".track");
  if (!track) return;
  // nas pontas a fila resiste (elástico)
  const atEdge = (current.value === 0 && dx > 0) || (current.value === list.value.length - 1 && dx < 0);
  const x = baseOffset(current.value) + (atEdge ? dx / 3 : dx);
  track.style.transition = animate ? "transform 280ms cubic-bezier(0.22, 1, 0.36, 1)" : "none";
  track.style.transform = `translate3d(${x}px, 0, 0)`;
}
function paintCard(card: HTMLElement, dy: number, animate: boolean) {
  card.style.transition = animate ? "transform 260ms ease, opacity 260ms ease" : "none";
  card.style.transform = dy ? `translate3d(0, ${dy}px, 0)` : "";
  card.style.opacity = dy ? String(Math.max(0.3, 1 + dy / 400)) : "";
}

function down(e: PointerEvent) {
  if (start) return;
  if (e.pointerType !== "touch" && performance.now() - lastTouch < 800) return;
  if (e.pointerType === "touch") lastTouch = performance.now();
  // o X do cartão é um botão comum
  if ((e.target as Element).closest("button")) return;
  const slot = (e.target as Element).closest<HTMLElement>(".slot");
  const index = slot ? Number(slot.dataset.i) : -1;
  start = { x: e.clientX, y: e.clientY, t: e.timeStamp, id: e.pointerId, index, card: slot?.querySelector(".card") ?? null, axis: null };
  moved = false;
  velocity = 0;
  lastX = e.clientX;
  lastT = e.timeStamp;
  try {
    strip.value?.setPointerCapture(e.pointerId);
  } catch {}
  clearTimeout(hold);
  if (index >= 0 && (e.target as Element).closest(".card")) {
    hold = window.setTimeout(() => {
      if (start && !moved) {
        haptic(30).catch(() => {});
        menu.value = list.value[index] ?? null;
        start = null;
      }
    }, 550);
  }
}
function move(e: PointerEvent) {
  if (!start || e.pointerId !== start.id) return;
  const dx = e.clientX - start.x;
  const dy = e.clientY - start.y;
  if (Math.hypot(dx, dy) > 8) {
    moved = true;
    clearTimeout(hold);
  }
  if (start.axis === null && Math.hypot(dx, dy) > 10) {
    // vertical só vale num cartão (é o "fechar"); fora dele tudo vira arrasto lateral
    start.axis = Math.abs(dy) > Math.abs(dx) && start.card ? "y" : "x";
  }
  if (e.timeStamp > lastT) {
    velocity = (e.clientX - lastX) / (e.timeStamp - lastT);
    lastX = e.clientX;
    lastT = e.timeStamp;
  }
  cancelAnimationFrame(frame);
  const s = start;
  frame = requestAnimationFrame(() => {
    if (s.axis === "x") paintTrack(dx, false);
    else if (s.axis === "y" && s.card) paintCard(s.card, Math.min(0, dy), false);
  });
}
function up(e: PointerEvent) {
  clearTimeout(hold);
  if (!start || e.pointerId !== start.id) return;
  if (e.pointerType === "touch") lastTouch = performance.now();
  cancelAnimationFrame(frame);
  const s = start;
  start = null;
  const dx = e.clientX - s.x;
  const dy = e.clientY - s.y;
  if (s.axis === "x") {
    const w = step() || 1;
    let target = Math.round(current.value - dx / w);
    // gesto rápido: vai pro vizinho mesmo sem arrastar meio cartão
    if (target === current.value && Math.abs(velocity) > 0.35) target -= Math.sign(velocity);
    current.value = Math.max(0, Math.min(list.value.length - 1, target));
    paintTrack(0, true);
  } else if (s.axis === "y" && s.card) {
    if (dy < -110) {
      paintCard(s.card, -window.innerHeight * 0.6, true);
      const r = list.value[s.index];
      if (r) closeApp(r);
    } else paintCard(s.card, 0, true);
  } else if (!moved) {
    // toque: no cartão do meio abre; num vizinho, traz ele pro meio; fora dos cartões, volta
    if (s.index < 0) close();
    else if (s.index === current.value) open(list.value[s.index]);
    else {
      current.value = s.index;
      paintTrack(0, true);
    }
  }
}
function cancel() {
  clearTimeout(hold);
  cancelAnimationFrame(frame);
  if (start?.card) paintCard(start.card, 0, true);
  start = null;
  paintTrack(0, true);
}

// O app que estava na frente fica no meio quando os recentes abrem.
async function centerFirst() {
  current.value = 0;
  await nextTick();
  paintTrack(0, false);
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
      // a memória muda enquanto olha: atualiza de tempos em tempos (nunca no meio de um arrasto)
      timer = window.setInterval(() => !start && load(), 2500);
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

    <div
      v-else
      ref="strip"
      class="strip"
      @pointerdown="down"
      @pointermove="move"
      @pointerup="up"
      @pointercancel="cancel"
      @contextmenu.prevent
    >
      <div class="track">
      <div v-for="(r, i) in list" :key="r.address" class="slot" :class="{ on: i === current, leaving: leaving.has(r.address) }" :data-i="i">
        <div class="title">
          <img v-if="appFor(r)?.icon" :src="appFor(r)!.icon!" alt="" />
          <span v-else class="glyph"><Icon :name="glyphOf(r)" :size="20" /></span>
          <span class="name">{{ nameOf(r) }}</span>
          <button v-if="i === current" class="x" aria-label="Fechar app" @click.stop="closeApp(r)">
            <Icon name="close" :size="18" />
          </button>
        </div>
        <div class="card">
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
  overflow: hidden;
  touch-action: none;
}
/* o cartão do meio fica centralizado; os vizinhos aparecem nas bordas */
.track {
  height: 100%;
  display: flex;
  gap: 14px;
  transition: transform 280ms cubic-bezier(0.22, 1, 0.36, 1);
  will-change: transform;
}
.slot {
  flex: 0 0 64vw;
  display: flex;
  flex-direction: column;
  opacity: 0.6;
  transform: scale(0.94);
  transition: opacity 200ms ease, transform 200ms ease;
}
.slot.on {
  opacity: 1;
  transform: none;
}
.slot.leaving .card {
  transform: translate3d(0, -120%, 0) !important;
  opacity: 0 !important;
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
  will-change: transform;
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
