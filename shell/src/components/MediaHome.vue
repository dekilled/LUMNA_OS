<script setup lang="ts">
// Tela do modo Mídia (o "Início"), para ver de longe: os serviços, as últimas páginas de cada um
// (Recentes, com o print de quando você saiu) e os vídeos do computador. Navega por controle,
// teclado, mouse ou pelo celular (LUMNA Controle). Cada monitor tem a sua.
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "./Icon.vue";
import { SERVICE_COLOR, SERVICE_STYLE, serviceMark } from "../mediaBrand";
import {
  artUrl,
  keyToPad,
  mediaServices,
  on,
  onPad,
  playVideo,
  remoteForget,
  remotePairing,
  remoteStopPairing,
  timeAgo,
  tvForget,
  tvOpen,
  tvResume,
  tvState,
  videos,
  type PadAction,
  type Pairing,
  type Recent,
  type Service,
  type TvState,
  type Video,
} from "../system";

const props = defineProps<{ monitor: string | null }>();

const services = ref<Service[]>([]);
const library = ref<Video[]>([]);
const tv = ref<TvState>({ playing: null, live: [], recents: [] });
const pairing = ref<Pairing | null>(null);
const pairedWith = ref<string | null>(null);
const secondsLeft = ref(0);
/** Muda a cada minuto, para o "há 5 min" andar. */
const tick = ref(0);
let countdown: number | undefined;
let minute: number | undefined;

const continuing = computed(() => library.value.filter((v) => v.progress > 0.02 && v.progress < 0.95));
const serviceName = (id: string) => services.value.find((s) => s.id === id)?.name ?? id;

type Item =
  | { kind: "service"; service: Service }
  | { kind: "local" }
  | { kind: "recent"; recent: Recent }
  | { kind: "video"; video: Video };

/** As linhas da tela, de cima para baixo; o cursor anda por elas como numa TV. */
const rows = computed(() => {
  const list: { id: string; items: Item[] }[] = [];
  list.push({ id: "services", items: [...services.value.map((service) => ({ kind: "service" as const, service })), { kind: "local" }] });
  if (tv.value.recents.length) list.push({ id: "recents", items: tv.value.recents.map((recent) => ({ kind: "recent", recent })) });
  if (continuing.value.length) list.push({ id: "continue", items: continuing.value.map((video) => ({ kind: "video", video })) });
  if (library.value.length) list.push({ id: "videos", items: library.value.map((video) => ({ kind: "video", video })) });
  return list;
});

const row = ref(0);
const col = ref(0);

// O cursor sempre visível: a linha e a fileira rolam até ele.
watch([row, col], async () => {
  await nextTick();
  document.querySelector(".media .cursor")?.scrollIntoView({ block: "nearest", inline: "nearest", behavior: "smooth" });
});
const isCursor = (r: number, c: number) => r === row.value && c === col.value && !pairing.value;

function goToRow(id: string) {
  const r = rows.value.findIndex((x) => x.id === id);
  if (r >= 0) {
    row.value = r;
    col.value = 0;
  }
}

function activate(item: Item | undefined) {
  if (!item) return;
  if (item.kind === "video") playVideo(item.video.id);
  else if (item.kind === "recent") tvResume(props.monitor, item.recent.id).catch((e) => console.error(e));
  else if (item.kind === "service") {
    // Serviço vivo (estacionado): volta onde estava; senão abre na página inicial dele.
    tvOpen(props.monitor, item.service.id).catch((e) => console.error(e));
  } else if (item.kind === "local") goToRow(continuing.value.length ? "continue" : "videos");
}

function forget(recent: Recent) {
  tvForget(props.monitor, recent.id).catch(() => {});
}

function act(action: PadAction) {
  if (pairing.value) {
    if (action === "back" || action === "accept") closePairing();
    return;
  }
  const lines = rows.value;
  const width = lines[row.value]?.items.length ?? 0;
  if (action === "left") col.value = Math.max(0, col.value - 1);
  else if (action === "right") col.value = Math.min(width - 1, col.value + 1);
  else if (action === "up" || action === "down") {
    row.value = Math.min(lines.length - 1, Math.max(0, row.value + (action === "up" ? -1 : 1)));
    col.value = Math.min(col.value, (lines[row.value]?.items.length ?? 1) - 1);
  } else if (action === "accept") activate(lines[row.value]?.items[col.value]);
  else if (action === "menu") openPairing();
}

function onKey(event: KeyboardEvent) {
  const action = keyToPad(event);
  if (action) {
    event.preventDefault();
    act(action);
  }
}

async function openPairing() {
  pairedWith.value = null;
  pairing.value = await remotePairing();
  secondsLeft.value = pairing.value.expires_in;
  window.clearInterval(countdown);
  countdown = window.setInterval(() => {
    secondsLeft.value -= 1;
    if (secondsLeft.value <= 0) closePairing();
  }, 1000);
}

function closePairing() {
  window.clearInterval(countdown);
  pairing.value = null;
  remoteStopPairing();
}

async function forgetPhones() {
  await remoteForget();
  if (pairing.value) pairing.value = { ...pairing.value, devices: [] };
}

async function load() {
  library.value = await videos().catch(() => []);
}

async function loadTv() {
  tv.value = await tvState(props.monitor).catch(() => tv.value);
  col.value = Math.min(col.value, Math.max(0, (rows.value[row.value]?.items.length ?? 1) - 1));
}

const ago = (seconds: number) => (tick.value, timeAgo(seconds));

const timeLeft = computed(() => {
  const m = Math.floor(secondsLeft.value / 60);
  const s = String(secondsLeft.value % 60).padStart(2, "0");
  return `${m}:${s}`;
});

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await onPad(() => props.monitor, act));
  stops.push(await on("lumna://videos-updated", load));
  stops.push(await on<string>("lumna://tv", (monitor) => (!props.monitor || monitor === props.monitor) && loadTv()));
  stops.push(
    await on<{ monitor: string; row: string }>("lumna://media-focus", (e) => {
      if (e.monitor === props.monitor) goToRow(e.row === "videos" && continuing.value.length ? "continue" : e.row);
    }),
  );
  stops.push(
    await on<string>("lumna://remote-paired", (name) => {
      pairedWith.value = name;
      window.setTimeout(closePairing, 2500);
    }),
  );
  minute = window.setInterval(() => (tick.value += 1), 60_000);
  services.value = await mediaServices();
  await Promise.all([load(), loadTv()]);
});

watch(() => props.monitor, loadTv);

onUnmounted(() => {
  stops.forEach((stop) => stop());
  window.clearInterval(countdown);
  window.clearInterval(minute);
});
</script>

<template>
  <main class="media">
    <div class="shade" />

    <header>
      <div>
        <h1>Início</h1>
        <p class="lead">Seus serviços e últimas páginas, em um só lugar.</p>
      </div>
      <button class="pair" @click="openPairing">
        <Icon name="phone" :size="18" />
        Controle pelo celular
      </button>
    </header>

    <div class="rows">
      <template v-for="(line, r) in rows" :key="line.id">
        <section v-if="line.id === 'services'" class="row">
          <h3>Serviços</h3>
          <div class="strip">
            <article
              v-for="(item, c) in line.items"
              :key="c"
              class="service"
              :class="{ cursor: isCursor(r, c) }"
              :style="item.kind === 'service' ? { background: SERVICE_STYLE[item.service.id] } : {}"
              @click="activate(item)"
            >
              <template v-if="item.kind === 'service'">
                <Icon v-if="item.service.id === 'music'" name="music" :size="26" />
                <strong :style="{ color: SERVICE_COLOR[item.service.id] }">{{ item.service.name }}</strong>
                <span v-if="tv.live.includes(item.service.id)" class="live">Aberto</span>
              </template>
              <template v-else>
                <Icon name="folder" :size="26" />
                <strong>Vídeos locais</strong>
              </template>
            </article>
          </div>
        </section>

        <section v-else-if="line.id === 'recents'" class="row">
          <h3>Recentes</h3>
          <div class="strip">
            <article
              v-for="(item, c) in line.items"
              :key="c"
              class="recent"
              :class="{ cursor: isCursor(r, c) }"
              @click="activate(item)"
            >
              <template v-if="item.kind === 'recent'">
                <div class="shot">
                  <img v-if="item.recent.thumb" :src="artUrl(item.recent.thumb)!" alt="" />
                  <span class="mark" :style="{ background: SERVICE_COLOR[item.recent.service] ?? '#7c5cff' }">
                    <Icon v-if="item.recent.service === 'music'" name="music" :size="14" />
                    <b v-else>{{ serviceMark(serviceName(item.recent.service)) }}</b>
                  </span>
                  <button class="drop" title="Tirar dos recentes" @click.stop="forget(item.recent)">
                    <Icon name="close" :size="13" />
                  </button>
                </div>
                <div class="info">
                  <span class="title">{{ item.recent.title }}</span>
                  <span class="when">{{ serviceName(item.recent.service) }} · Última página · {{ ago(item.recent.at) }}</span>
                </div>
                <span class="resume"><Icon name="play" :size="14" /> Retomar</span>
              </template>
            </article>
          </div>
        </section>

        <section v-else-if="line.id === 'continue' || line.id === 'videos'" class="row">
          <h3>{{ line.id === "continue" ? "Continuar assistindo" : "Vídeos locais" }}</h3>
          <div class="strip">
            <article
              v-for="(item, c) in line.items"
              :key="c"
              class="thumb"
              :class="{ cursor: isCursor(r, c) }"
              @click="activate(item)"
            >
              <template v-if="item.kind === 'video'">
                <img v-if="item.video.thumb" :src="artUrl(item.video.thumb)!" alt="" />
                <span class="name">{{ item.video.title }}</span>
                <span v-if="line.id === 'continue'" class="bar"><span :style="{ width: `${Math.round(item.video.progress * 100)}%` }" /></span>
              </template>
            </article>
          </div>
        </section>
      </template>
      <p v-if="!library.length" class="hint">
        Coloque vídeos na pasta Vídeos para eles aparecerem aqui, com "continuar assistindo".
      </p>
    </div>

    <footer class="keys">
      <span><kbd>←</kbd><kbd>→</kbd> Navegar</span>
      <span><kbd>Enter</kbd> Abrir</span>
      <span><kbd>Super</kbd>+<kbd>M</kbd> Trocar serviço</span>
      <span class="tip">Abra um card para voltar à página salva.</span>
    </footer>

    <div v-if="pairing" class="overlay" @click.self="closePairing">
      <section class="panel">
        <template v-if="pairedWith">
          <Icon name="phone" :size="40" />
          <h2>Pareado com {{ pairedWith }}</h2>
          <p>Já pode usar o celular como controle.</p>
        </template>
        <template v-else>
          <h2>Controle pelo celular</h2>
          <p>Abra o app <strong>LUMNA Controle</strong>, escolha esta TV e digite o código:</p>
          <div class="code">
            <span v-for="(digit, i) in pairing.code" :key="i">{{ digit }}</span>
          </div>
          <p class="small">
            Vale por {{ timeLeft }} e uma vez só.
            <template v-if="pairing.addresses.length">
              Se a TV não aparecer no app, use o endereço {{ pairing.addresses[0] }}.
            </template>
          </p>
          <footer>
            <span v-if="pairing.devices.length" class="small">Pareados: {{ pairing.devices.join(", ") }}</span>
            <button v-if="pairing.devices.length" class="ghost" @click="forgetPhones">Esquecer celulares</button>
            <button class="ghost" @click="closePairing">Fechar</button>
          </footer>
        </template>
      </section>
    </div>
  </main>
</template>

<style scoped>
.media {
  position: fixed;
  inset: 0;
  overflow: hidden;
  padding: 40px 64px 32px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  gap: 18px;
  background: linear-gradient(180deg, #0b0918, #150f2b);
  animation: enter 320ms cubic-bezier(0.22, 1, 0.36, 1);
}
.shade {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(circle at 15% 0%, rgba(167, 139, 250, 0.22), transparent 50%),
    linear-gradient(180deg, rgba(10, 7, 22, 0.4), rgba(10, 7, 22, 0.95));
}
.media > :not(.shade):not(.overlay) {
  position: relative;
}

header {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
h1 {
  margin: 0;
  font-size: 34px;
}
.lead {
  margin: 4px 0 0;
  color: var(--lumna-muted);
  font-size: 15px;
}
.pair {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 40px;
  padding: 0 16px;
  border-radius: 12px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.06);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}

.rows {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 22px;
  min-height: 0;
  overflow: hidden;
}

/* O cursor do controle: contorno claro e um leve zoom. */
.cursor {
  outline: 3px solid #fff;
  outline-offset: 3px;
  transform: scale(1.04);
  box-shadow: 0 0 36px rgba(167, 139, 250, 0.5);
}

h3 {
  margin: 0 0 10px;
  font-size: 18px;
  font-weight: 600;
}
.strip {
  display: flex;
  gap: 16px;
  padding: 6px;
  overflow: hidden;
}

.thumb {
  position: relative;
  flex: none;
  width: 260px;
  aspect-ratio: 16 / 9;
  border-radius: 14px;
  overflow: hidden;
  background: linear-gradient(135deg, #2a1d52, #120c26);
  transition: transform 220ms ease, box-shadow 220ms ease;
}
.thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.name {
  position: absolute;
  left: 10px;
  right: 10px;
  bottom: 12px;
  font-size: 13px;
  font-weight: 600;
  text-shadow: 0 1px 6px rgba(0, 0, 0, 0.9);
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.bar {
  position: absolute;
  left: 10px;
  right: 10px;
  bottom: 5px;
  height: 3px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.25);
}
.bar span {
  display: block;
  height: 100%;
  border-radius: 2px;
  background: #a78bfa;
}

.service {
  flex: none;
  width: 180px;
  height: 100px;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  border-radius: 14px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.05);
  color: var(--lumna-text);
  transition: transform 220ms ease, box-shadow 220ms ease;
}
.service strong {
  font-size: 20px;
  letter-spacing: 0.02em;
}

.live {
  font-size: 11px;
  padding: 2px 8px;
  border-radius: 8px;
  background: rgba(255, 255, 255, 0.12);
  color: #ece8ff;
}

.recent {
  position: relative;
  flex: none;
  width: 300px;
  display: flex;
  flex-direction: column;
  border-radius: 16px;
  overflow: hidden;
  background: rgba(255, 255, 255, 0.05);
  border: 1px solid var(--lumna-line);
  transition: transform 220ms ease, box-shadow 220ms ease;
}
.shot {
  position: relative;
  aspect-ratio: 16 / 9;
  background: linear-gradient(135deg, #2a1d52, #120c26);
}
.shot img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.mark {
  position: absolute;
  left: 10px;
  top: 10px;
  width: 28px;
  height: 28px;
  display: grid;
  place-items: center;
  border-radius: 8px;
  color: #fff;
  font-size: 14px;
  box-shadow: 0 2px 10px rgba(0, 0, 0, 0.5);
}
.drop {
  position: absolute;
  right: 8px;
  top: 8px;
  width: 26px;
  height: 26px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: rgba(10, 7, 22, 0.7);
  color: #ece8ff;
  opacity: 0;
  transition: opacity 160ms ease;
}
.recent:hover .drop {
  opacity: 1;
}
.info {
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 10px 12px 4px;
  min-width: 0;
}
.info .title {
  font-size: 15px;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.info .when {
  font-size: 12px;
  color: var(--lumna-muted);
}
.resume {
  align-self: flex-start;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  margin: 6px 12px 12px;
  height: 30px;
  padding: 0 14px;
  border-radius: 15px;
  background: #fff;
  color: #120c26;
  font-size: 13px;
  font-weight: 600;
}

.keys {
  margin-top: auto;
  display: flex;
  align-items: center;
  gap: 22px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.keys .tip {
  margin-left: auto;
}
kbd {
  display: inline-grid;
  place-items: center;
  min-width: 22px;
  height: 22px;
  margin: 0 3px;
  padding: 0 6px;
  border-radius: 6px;
  background: rgba(255, 255, 255, 0.1);
  font: inherit;
  font-size: 11px;
  font-weight: 600;
  color: #ece8ff;
}

.hint {
  margin: 0;
  color: var(--lumna-muted);
}

.overlay {
  position: absolute;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(5, 3, 12, 0.6);
}
.panel {
  width: 560px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  padding: 32px;
  border-radius: 26px;
  background: rgba(18, 13, 36, 0.95);
  border: 1px solid var(--lumna-line);
  text-align: center;
}
.panel h2 {
  margin: 0;
}
.panel p {
  margin: 0;
  color: #cfc8ef;
}
.code {
  display: flex;
  gap: 10px;
  margin: 14px 0;
}
.code span {
  width: 58px;
  height: 72px;
  display: grid;
  place-items: center;
  border-radius: 14px;
  background: rgba(139, 92, 246, 0.18);
  border: 1px solid var(--lumna-violet);
  font-size: 36px;
  font-weight: 700;
  font-variant-numeric: tabular-nums;
}
.small {
  font-size: 12px;
  color: var(--lumna-muted) !important;
}
.panel footer {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-top: 10px;
}
.ghost {
  height: 34px;
  padding: 0 14px;
  border-radius: 10px;
  border: 1px solid var(--lumna-line);
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}

@keyframes enter {
  from {
    opacity: 0;
    transform: scale(1.02);
  }
}
</style>
