<script setup lang="ts">
// Galeria: as fotos (Imagens, Capturas, a câmera) agrupadas por dia, com os álbuns em cima.
// Tocar abre a foto na tela toda: deslizar troca, pinça (ou a roda do mouse) dá zoom, toque duplo
// aproxima onde tocou; manda pro PC pelo Connect, mostra os detalhes e manda pra lixeira.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import { getCurrentWindow } from "@tauri-apps/api/window";
import Icon from "../Icon.vue";
import { showApp } from "../mobile/apps";
import {
  artUrl,
  connectShare,
  connectTargets,
  filesThumb,
  filesTrash,
  galleryList,
  galleryPlay,
  haptic,
  on,
  type ConnectTarget,
  type Photo,
} from "../../system";

const photos = ref<Photo[]>([]);
const loading = ref(true);
const album = ref("Tudo");
const thumbs = ref(new Map<string, string>());
const narrow = ref(window.innerWidth < 640);
const current = ref<number | null>(null);
const chrome = ref(true);
const info = ref(false);
const message = ref("");
const targets = ref<ConnectTarget[] | null>(null);
const confirmDelete = ref(false);
/** O tamanho de cada foto, de quando ela carregou (a vizinha carrega antes de virar a atual). */
const sizes = ref(new Map<string, { w: number; h: number }>());
const natural = computed(() => (photo.value && sizes.value.get(photo.value.path)) || { w: 0, h: 0 });

// ---- lista ----

const albums = computed(() => {
  const count = new Map<string, number>();
  for (const p of photos.value) count.set(p.album, (count.get(p.album) ?? 0) + 1);
  const order = ["Câmera", "Capturas", "Gravações", "Imagens"];
  const names = [...count.keys()].sort((a, b) => {
    const ia = order.indexOf(a);
    const ib = order.indexOf(b);
    if (ia >= 0 || ib >= 0) return (ia < 0 ? 99 : ia) - (ib < 0 ? 99 : ib);
    return (count.get(b) ?? 0) - (count.get(a) ?? 0);
  });
  return [{ name: "Tudo", count: photos.value.length }, ...names.map((name) => ({ name, count: count.get(name) ?? 0 }))];
});

const shown = computed(() => (album.value === "Tudo" ? photos.value : photos.value.filter((p) => p.album === album.value)));

function dayLabel(time: number): string {
  const d = new Date(time * 1000);
  const today = new Date();
  const yesterday = new Date(Date.now() - 86_400_000);
  if (d.toDateString() === today.toDateString()) return "Hoje";
  if (d.toDateString() === yesterday.toDateString()) return "Ontem";
  return d.toLocaleDateString("pt-BR", { day: "numeric", month: "long", year: d.getFullYear() === today.getFullYear() ? undefined : "numeric" });
}

const days = computed(() => {
  const out: { label: string; items: { photo: Photo; index: number }[] }[] = [];
  shown.value.forEach((photo, index) => {
    const label = dayLabel(photo.time);
    const last = out[out.length - 1];
    if (last?.label === label) last.items.push({ photo, index });
    else out.push({ label, items: [{ photo, index }] });
  });
  return out;
});

// miniaturas só do que aparece na tela (as fotos grandes viram um JPEG pequeno no cache)
let observer: IntersectionObserver | null = null;
function watchThumb(el: unknown, path: string) {
  if (!(el instanceof HTMLElement) || el.dataset.watched) return;
  el.dataset.watched = "1";
  el.dataset.path = path;
  observer?.observe(el);
}
async function loadThumb(path: string) {
  if (thumbs.value.has(path)) return;
  thumbs.value.set(path, "");
  const thumb = await filesThumb(path).catch(() => null);
  // vídeo sem miniatura (sem ffmpeg): fica o quadrado com o ▶
  const video = photos.value.find((p) => p.path === path)?.video;
  thumbs.value.set(path, thumb ? (artUrl(thumb) ?? "") : video ? "" : (artUrl(path) ?? ""));
  thumbs.value = new Map(thumbs.value);
}

async function load() {
  loading.value = true;
  photos.value = await galleryList().catch(() => []);
  loading.value = false;
}

// ---- visualizador ----

const photo = computed(() => (current.value == null ? null : shown.value[current.value] ?? null));

function open(index: number) {
  current.value = index;
  chrome.value = true;
  info.value = false;
  targets.value = null;
  confirmDelete.value = false;
  resetZoom();
}
function close() {
  current.value = null;
}
function go(step: number) {
  if (current.value == null) return;
  const next = current.value + step;
  if (next < 0 || next >= shown.value.length) {
    slide.value = 0;
    return;
  }
  current.value = next;
  resetZoom();
  info.value = false;
  targets.value = null;
  confirmDelete.value = false;
}

const scale = ref(1);
const tx = ref(0);
const ty = ref(0);
const slide = ref(0);
/** Puxando a foto pra baixo (pra fechar). */
const drop = ref(0);
const animating = ref(false);
const stage = ref<HTMLElement>();

function resetZoom() {
  scale.value = 1;
  tx.value = 0;
  ty.value = 0;
  slide.value = 0;
  drop.value = 0;
}

/** Ponto da tela relativo ao centro do palco. */
function local(x: number, y: number) {
  const r = stage.value?.getBoundingClientRect();
  return { x: x - (r ? r.left + r.width / 2 : 0), y: y - (r ? r.top + r.height / 2 : 0) };
}

/** Zoom mantendo o ponto `at` (relativo ao centro) parado na tela. */
function zoomAt(next: number, at: { x: number; y: number }, from = { s: scale.value, x: tx.value, y: ty.value }) {
  const s = Math.min(6, Math.max(1, next));
  const px = (at.x - from.x) / from.s;
  const py = (at.y - from.y) / from.s;
  scale.value = s;
  tx.value = s === 1 ? 0 : at.x - s * px;
  ty.value = s === 1 ? 0 : at.y - s * py;
}

// Gestos. No celular vêm pelos eventos de toque: a lista de dedos do WebKit é a certa. Os eventos
// de ponteiro do WebKitGTK não servem lá: cada toque chega também como "mouse" de
// compatibilidade, e às vezes o mesmo dedo vira dois ponteiros (pinça fantasma: deslizar pra
// cima encolhia, pra baixo dava zoom). Os ponteiros ficam só pro mouse, no PC.
type Pt = { x: number; y: number };
const MOUSE = -1;
const fingers = new Map<number, Pt>();
let pinch: { dist: number; mid: Pt; s: number; x: number; y: number } | null = null;
let pan: { x: number; y: number; tx: number; ty: number; moved: boolean; axis: "" | "x" | "y" } | null = null;
let lastTap = 0;
let lastTouch = 0;
let track: { t: number; x: number }[] = [];
let switching = false;
const GAP = 16;
const width = () => stage.value?.clientWidth ?? window.innerWidth;

function dist() {
  const [a, b] = [...fingers.values()];
  return Math.hypot(a.x - b.x, a.y - b.y);
}
function mid() {
  const [a, b] = [...fingers.values()];
  return local((a.x + b.x) / 2, (a.y + b.y) / 2);
}

function begin(id: number, x: number, y: number, t: number) {
  if (switching) return;
  fingers.set(id, { x, y });
  animating.value = false;
  if (fingers.size === 2) {
    pan = null;
    pinch = { dist: Math.max(1, dist()), mid: mid(), s: scale.value, x: tx.value, y: ty.value };
  } else if (fingers.size === 1) {
    pan = { x, y, tx: tx.value, ty: ty.value, moved: false, axis: "" };
    track = [{ t, x }];
  }
}

function moveTo(id: number, x: number, y: number, t: number) {
  if (!fingers.has(id)) return;
  fingers.set(id, { x, y });
  if (pinch && fingers.size >= 2) {
    const m = mid();
    zoomAt((pinch.s * dist()) / pinch.dist, pinch.mid, { s: pinch.s, x: pinch.x, y: pinch.y });
    // e anda junto com os dedos
    tx.value += m.x - pinch.mid.x;
    ty.value += m.y - pinch.mid.y;
    return;
  }
  if (!pan) return;
  const dx = x - pan.x;
  const dy = y - pan.y;
  if (!pan.moved && Math.abs(dx) + Math.abs(dy) > 8) {
    pan.moved = true;
    // a direção decide o gesto: de lado troca a foto, de cima pra baixo fecha
    pan.axis = Math.abs(dx) >= Math.abs(dy) ? "x" : "y";
  }
  track.push({ t, x });
  if (track.length > 6) track.shift();
  if (scale.value > 1) {
    tx.value = pan.tx + dx;
    ty.value = pan.ty + dy;
  } else if (pan.axis === "x") {
    // na primeira e na última foto o deslize "resiste" (não tem pra onde ir)
    const edge = (dx > 0 && !prevPhoto.value) || (dx < 0 && !nextPhoto.value);
    slide.value = edge ? dx * 0.25 : dx;
  } else if (pan.axis === "y") {
    drop.value = Math.max(0, dy);
  }
}

function finish(id: number, x: number, y: number) {
  if (!fingers.has(id)) return;
  fingers.delete(id);
  if (pinch) {
    if (fingers.size < 2) {
      pinch = null;
      if (scale.value <= 1.02) resetZoom();
      // o dedo que ficou continua arrastando dali
      const rest = [...fingers.values()][0];
      pan = rest ? { x: rest.x, y: rest.y, tx: tx.value, ty: ty.value, moved: true, axis: "" } : null;
    }
    return;
  }
  const p = pan;
  pan = null;
  if (!p) return;
  animating.value = true;
  if (p.moved && scale.value === 1) {
    if (p.axis === "y") {
      // puxou pra baixo: fecha a foto (como no Android)
      if (drop.value > 140) close();
      drop.value = 0;
      return;
    }
    // passou de um quarto da tela, ou foi um peteleco rápido (mesmo curto): troca
    const first = track[0];
    const last = track[track.length - 1];
    const speed = first && last && last.t > first.t ? (last.x - first.x) / (last.t - first.t) : 0;
    const w = width();
    const step = slide.value < -w / 4 || (speed < -0.35 && slide.value < -20) ? 1 : slide.value > w / 4 || (speed > 0.35 && slide.value > 20) ? -1 : 0;
    if ((step === 1 && nextPhoto.value) || (step === -1 && prevPhoto.value)) slideTo(step);
    else slide.value = 0;
    return;
  }
  if (p.moved) return;
  // toque: duplo aproxima/afasta, simples mostra/esconde os botões
  const now = Date.now();
  if (now - lastTap < 300) {
    lastTap = 0;
    if (scale.value > 1) resetZoom();
    else zoomAt(2.5, local(x, y));
    haptic(10).catch(() => {});
  } else {
    lastTap = now;
    window.setTimeout(() => {
      if (lastTap === now) chrome.value = !chrome.value;
    }, 300);
  }
}

function tstart(e: TouchEvent) {
  lastTouch = Date.now();
  // um "mouse" que tenha começado antes do toque é o fantasma: sai
  if (fingers.has(MOUSE)) {
    fingers.delete(MOUSE);
    pan = null;
  }
  for (const t of Array.from(e.changedTouches)) begin(t.identifier, t.clientX, t.clientY, e.timeStamp);
}
function tmove(e: TouchEvent) {
  lastTouch = Date.now();
  if (e.cancelable) e.preventDefault();
  for (const t of Array.from(e.changedTouches)) moveTo(t.identifier, t.clientX, t.clientY, e.timeStamp);
}
function tend(e: TouchEvent) {
  lastTouch = Date.now();
  for (const t of Array.from(e.changedTouches)) finish(t.identifier, t.clientX, t.clientY);
}

// só o mouse de verdade (nada logo depois de um toque)
const isMouse = (e: PointerEvent) => e.pointerType === "mouse" && Date.now() - lastTouch > 1000;
function pdown(e: PointerEvent) {
  if (!isMouse(e) || e.button !== 0) return;
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
  begin(MOUSE, e.clientX, e.clientY, e.timeStamp);
}
function pmove(e: PointerEvent) {
  if (isMouse(e)) moveTo(MOUSE, e.clientX, e.clientY, e.timeStamp);
}
function pup(e: PointerEvent) {
  if (isMouse(e)) finish(MOUSE, e.clientX, e.clientY);
}

/** A foto sai pro lado junto com a vizinha entrando; no fim, a vizinha vira a atual. */
function slideTo(step: number) {
  switching = true;
  animating.value = true;
  slide.value = -step * (width() + GAP);
  window.setTimeout(() => {
    animating.value = false;
    go(step);
    switching = false;
  }, 220);
}

const neighbor = (step: number) => {
  if (current.value == null) return null;
  const p = shown.value[current.value + step];
  return p && !p.video ? p : null;
};
const prevPhoto = computed(() => neighbor(-1));
const nextPhoto = computed(() => neighbor(1));

/** As três fotos (anterior, atual, próxima), sempre os mesmos <img> por foto: ao trocar, a
 * vizinha que entrou vira a atual sem recarregar (era o que piscava). */
const slots = computed(() => {
  if (!photo.value) return [];
  const list: { photo: Photo; off: number }[] = [{ photo: photo.value, off: 0 }];
  if (scale.value === 1 || switching) {
    if (prevPhoto.value) list.unshift({ photo: prevPhoto.value, off: -1 });
    if (nextPhoto.value) list.push({ photo: nextPhoto.value, off: 1 });
  }
  return list;
});
function slotStyle(off: number) {
  if (off === 0) return { transform: `translate(${tx.value + slide.value}px, ${ty.value + drop.value}px) scale(${scale.value * (1 - Math.min(drop.value, 400) / 1600)})` };
  return { transform: `translateX(${off * (width() + GAP) + slide.value}px)` };
}

function wheel(e: WheelEvent) {
  e.preventDefault();
  animating.value = false;
  zoomAt(scale.value * Math.exp(-e.deltaY * 0.0025), local(e.clientX, e.clientY));
}


// ---- ações ----

async function play(path: string) {
  try {
    // celular: um app por vez (senão o Hyprland divide a tela entre a Galeria e o mpv)
    if (narrow.value) await showApp().catch(() => {});
    await galleryPlay(path);
  } catch (e) {
    flash(String(e));
  }
}

function flash(text: string) {
  message.value = text;
  window.setTimeout(() => message.value === text && (message.value = ""), 2600);
}

async function share() {
  if (targets.value) {
    targets.value = null;
    return;
  }
  const list = await connectTargets().catch(() => []);
  if (!list.length) {
    flash("Nenhum aparelho conectado no Connect");
    return;
  }
  targets.value = list;
}

async function sendTo(t: ConnectTarget) {
  if (!photo.value) return;
  targets.value = null;
  try {
    await connectShare(t, [photo.value.path]);
    flash(`Enviando pra ${t.name}`);
  } catch (e) {
    flash(String(e));
  }
}

async function remove() {
  const p = photo.value;
  if (!p || current.value == null) return;
  if (!confirmDelete.value) {
    confirmDelete.value = true;
    return;
  }
  confirmDelete.value = false;
  try {
    await filesTrash([p.path]);
  } catch (e) {
    flash(String(e));
    return;
  }
  const index = current.value;
  photos.value = photos.value.filter((x) => x.path !== p.path);
  if (!shown.value.length) close();
  else current.value = Math.min(index, shown.value.length - 1);
  resetZoom();
  flash("Foto na lixeira");
}

function sizeText(bytes: number) {
  if (bytes < 1024 * 1024) return `${Math.max(1, Math.round(bytes / 1024))} KB`;
  return `${(bytes / 1024 / 1024).toFixed(1).replace(".", ",")} MB`;
}

const when = computed(() =>
  photo.value
    ? new Date(photo.value.time * 1000).toLocaleString("pt-BR", { day: "numeric", month: "long", year: "numeric", hour: "2-digit", minute: "2-digit" })
    : "",
);

/** Abre direto numa foto (o aviso da captura, o lumna-ctl gallery ARQUIVO). */
function openPath(path: string) {
  album.value = "Tudo";
  const index = photos.value.findIndex((p) => p.path === path);
  if (index >= 0) open(index);
}

function onKey(e: KeyboardEvent) {
  if (current.value == null) return;
  if (e.key === "Escape") close();
  else if (e.key === "ArrowRight") go(1);
  else if (e.key === "ArrowLeft") go(-1);
  else if (e.key === "Delete") remove();
}

watch(album, () => close());

const stops: (() => void)[] = [];
const onResize = () => (narrow.value = window.innerWidth < 640);

onMounted(async () => {
  observer = new IntersectionObserver(
    (entries) => {
      for (const entry of entries) {
        const path = (entry.target as HTMLElement).dataset.path;
        if (entry.isIntersecting && path) loadThumb(path);
      }
    },
    { rootMargin: "400px" },
  );
  window.addEventListener("keydown", onKey);
  window.addEventListener("resize", onResize);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(() => window.removeEventListener("resize", onResize));
  await load();
  const start = decodeURIComponent(location.hash.slice(1));
  if (start) openPath(start);
  stops.push(
    await on<string>("lumna://gallery-open", async (path) => {
      await load();
      await nextTick();
      openPath(path);
    }),
  );
  // voltou pra janela: pode ter foto nova (uma captura, um download)
  stops.push(await getCurrentWindow().onFocusChanged(({ payload }) => payload && current.value == null && load()));
});

onUnmounted(() => {
  observer?.disconnect();
  stops.forEach((s) => s());
});
</script>

<template>
  <div class="gallery" :class="{ narrow }">
    <header class="top">
      <h1>Galeria</h1>
      <span class="count">{{ shown.length }} {{ shown.length === 1 ? "foto" : "fotos" }}</span>
    </header>
    <nav class="albums">
      <button v-for="a in albums" :key="a.name" :class="{ on: album === a.name }" @click="album = a.name">
        {{ a.name }} <small>{{ a.count }}</small>
      </button>
    </nav>

    <main class="grid-scroll">
      <p v-if="loading" class="empty">Procurando fotos…</p>
      <div v-else-if="!shown.length" class="empty">
        <Icon name="image" :size="40" />
        <p>Nenhuma foto ainda. As capturas de tela vão para Imagens/Capturas e as gravações para Vídeos/Gravações.</p>
      </div>
      <section v-for="day in days" :key="day.label" class="day">
        <h2>{{ day.label }}</h2>
        <div class="grid">
          <button
            v-for="item in day.items"
            :key="item.photo.path"
            :ref="(el) => watchThumb(el, item.photo.path)"
            class="cell"
            :title="item.photo.name"
            @click="open(item.index)"
          >
            <img v-if="thumbs.get(item.photo.path)" :src="thumbs.get(item.photo.path)" alt="" loading="lazy" draggable="false" />
            <span v-if="item.photo.video" class="play"><Icon name="play" :size="22" /></span>
          </button>
        </div>
      </section>
    </main>

    <!-- a foto na tela toda -->
    <div v-if="photo" class="viewer">
      <!-- vídeo: o player do WebKit, sem zoom nem deslizar (os controles dele usam o toque) -->
      <!-- vídeo: a capa com o ▶; tocar abre no mpv (decodifica por hardware, toca qualquer formato) -->
      <div v-if="photo.video" class="stage video" @click="chrome = !chrome">
        <img v-if="thumbs.get(photo.path)" class="full" :src="thumbs.get(photo.path)" alt="" draggable="false" />
        <button class="big-play" aria-label="Tocar" @click.stop="play(photo.path)"><Icon name="play" :size="56" /></button>
      </div>
      <div
        v-else
        ref="stage"
        class="stage"
        @touchstart="tstart"
        @touchmove="tmove"
        @touchend="tend"
        @touchcancel="tend"
        @pointerdown="pdown"
        @pointermove="pmove"
        @pointerup="pup"
        @pointercancel="pup"
        @wheel="wheel"
      >
        <img
          v-for="slot in slots"
          :key="slot.photo.path"
          class="full"
          :class="{ animating, side: slot.off !== 0 }"
          :src="artUrl(slot.photo.path) ?? ''"
          :style="slotStyle(slot.off)"
          alt=""
          draggable="false"
          @load="(e) => sizes.set(slot.photo.path, { w: (e.target as HTMLImageElement).naturalWidth, h: (e.target as HTMLImageElement).naturalHeight })"
        />
      </div>

      <header v-show="chrome" class="vbar">
        <button class="icon" aria-label="Voltar" @click="close()"><Icon name="back" :size="24" /></button>
        <div class="title">
          <strong>{{ photo.album }}</strong>
          <small>{{ when }}</small>
        </div>
        <span class="pos">{{ (current ?? 0) + 1 }}/{{ shown.length }}</span>
      </header>

      <div v-if="info && chrome" class="info">
        <p><strong>{{ photo.name }}</strong></p>
        <p>{{ when }}</p>
        <p>{{ natural.w }} × {{ natural.h }} · {{ sizeText(photo.size) }}</p>
        <p class="path">{{ photo.path }}</p>
      </div>

      <div v-if="targets && chrome" class="targets">
        <button v-for="t in targets" :key="t.id" @click="sendTo(t)">
          <Icon :name="t.kind === 'pc' ? 'computer' : 'phone'" :size="20" /> {{ t.name }}
        </button>
      </div>

      <footer v-show="chrome" class="vtools">
        <button @click="share()"><Icon name="share" :size="22" /><span>Enviar</span></button>
        <button :class="{ on: info }" @click="info = !info"><Icon name="info" :size="22" /><span>Detalhes</span></button>
        <button class="danger" :class="{ on: confirmDelete }" @click="remove()">
          <Icon name="trash" :size="22" /><span>{{ confirmDelete ? "Tocar de novo" : "Apagar" }}</span>
        </button>
      </footer>

      <button v-if="(!narrow || photo.video) && chrome && (current ?? 0) > 0" class="nav prev" aria-label="Anterior" @click="go(-1)"><Icon name="back" :size="28" /></button>
      <button v-if="(!narrow || photo.video) && chrome && (current ?? 0) < shown.length - 1" class="nav next" aria-label="Próxima" @click="go(1)">
        <Icon name="forward" :size="28" />
      </button>
    </div>

    <p v-if="message" class="toast">{{ message }}</p>
  </div>
</template>

<style scoped>
.gallery {
  display: flex;
  flex-direction: column;
  height: var(--app-h, 100vh);
  background: #0d0b14;
  color: var(--lumna-text);
  overflow: hidden;
}
.top {
  flex: none;
  display: flex;
  align-items: baseline;
  gap: 12px;
  padding: 18px 20px 6px;
}
.top h1 {
  margin: 0;
  font-size: 24px;
}
.count {
  color: var(--lumna-muted);
  font-size: 14px;
}
.albums {
  flex: none;
  display: flex;
  gap: 8px;
  padding: 8px 20px 12px;
  overflow-x: auto;
  scrollbar-width: none;
}
.albums button {
  flex: none;
  padding: 8px 14px;
  border: 1px solid rgba(167, 139, 250, 0.18);
  border-radius: 999px;
  background: none;
  color: var(--lumna-text);
  font-size: 14px;
}
.albums button small {
  color: var(--lumna-muted);
  margin-left: 4px;
}
.albums button.on {
  border-color: transparent;
  background: var(--lumna-violet);
}
.albums button.on small {
  color: rgba(255, 255, 255, 0.75);
}
.grid-scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 0 20px 24px;
}
.narrow .grid-scroll {
  padding: 0 2px 16px;
}
.day h2 {
  margin: 14px 0 8px;
  font-size: 15px;
  font-weight: 600;
  color: var(--lumna-muted);
}
.narrow .day h2 {
  margin-left: 14px;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 6px;
}
.narrow .grid {
  grid-template-columns: repeat(4, 1fr);
  gap: 2px;
}
.cell {
  aspect-ratio: 1;
  padding: 0;
  border: 0;
  border-radius: 6px;
  overflow: hidden;
  background: #1c1828;
}
.narrow .cell {
  border-radius: 0;
}
.cell {
  position: relative;
}
.play {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  color: #fff;
  filter: drop-shadow(0 1px 3px rgba(0, 0, 0, 0.6));
}
.cell img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}
.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  margin: 18vh auto 0;
  max-width: 320px;
  color: var(--lumna-muted);
  text-align: center;
}
.viewer {
  position: fixed;
  inset: 0;
  z-index: 10;
  background: #000;
}
.stage {
  position: absolute;
  inset: 0;
  display: flex;
  align-items: center;
  justify-content: center;
  overflow: hidden;
  touch-action: none;
}
.full {
  width: 100%;
  height: 100%;
  object-fit: contain;
  user-select: none;
  will-change: transform;
}
.full {
  position: absolute;
  inset: 0;
}
.stage.video .full {
  opacity: 0.55;
}
.big-play {
  position: absolute;
  display: grid;
  place-items: center;
  width: 104px;
  height: 104px;
  border: 0;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.85);
  color: #fff;
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
}
.full.animating {
  transition: transform 200ms cubic-bezier(0.2, 0.9, 0.3, 1);
}
.vbar {
  position: absolute;
  inset: 0 0 auto;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 12px 26px;
  background: linear-gradient(rgba(0, 0, 0, 0.7), transparent);
}
.title {
  flex: 1;
  display: flex;
  flex-direction: column;
  min-width: 0;
}
.title small,
.pos {
  color: rgba(236, 232, 255, 0.7);
  font-size: 13px;
}
.icon {
  display: grid;
  place-items: center;
  width: 44px;
  height: 44px;
  border: 0;
  border-radius: 50%;
  background: none;
  color: #fff;
}
.vtools {
  position: absolute;
  inset: auto 0 0;
  display: flex;
  justify-content: space-around;
  padding: 26px 12px 18px;
  background: linear-gradient(transparent, rgba(0, 0, 0, 0.75));
}
.vtools button {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
  min-width: 84px;
  padding: 6px;
  border: 0;
  border-radius: 14px;
  background: none;
  color: #fff;
  font-size: 12px;
}
.vtools button.on {
  background: rgba(167, 139, 250, 0.2);
}
.vtools .danger.on {
  background: rgba(239, 68, 68, 0.3);
}
.info,
.targets {
  position: absolute;
  left: 12px;
  right: 12px;
  bottom: 96px;
  max-width: 460px;
  margin: 0 auto;
  padding: 12px 14px;
  border-radius: 16px;
  background: rgba(19, 16, 29, 0.94);
  font-size: 14px;
}
.info p {
  margin: 2px 0;
  overflow-wrap: anywhere;
}
.info .path {
  color: var(--lumna-muted);
  font-size: 12px;
}
.targets {
  display: flex;
  flex-direction: column;
  gap: 4px;
  padding: 6px;
}
.targets button {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-text);
  font-size: 15px;
  text-align: left;
}
.nav {
  position: absolute;
  top: 50%;
  transform: translateY(-50%);
  display: grid;
  place-items: center;
  width: 52px;
  height: 52px;
  border: 0;
  border-radius: 50%;
  background: rgba(19, 16, 29, 0.6);
  color: #fff;
}
.nav.prev {
  left: 16px;
}
.nav.next {
  right: 16px;
}
.toast {
  position: fixed;
  left: 50%;
  bottom: 110px;
  z-index: 20;
  transform: translateX(-50%);
  margin: 0;
  padding: 10px 16px;
  border-radius: 999px;
  background: rgba(19, 16, 29, 0.95);
  font-size: 14px;
  white-space: nowrap;
}
</style>
