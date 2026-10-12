<script setup lang="ts">
// Tela do modo Mídia, para ver de longe: destaque, "Continuar assistindo", serviços de streaming e
// os vídeos do computador. Navega por controle, teclado, mouse ou pelo celular (LUMNA Controle).
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "./Icon.vue";
import {
  artUrl,
  keyToPad,
  mediaServices,
  on,
  onPad,
  openService,
  playVideo,
  remoteForget,
  remotePairing,
  remoteStopPairing,
  videos,
  type PadAction,
  type Pairing,
  type Service,
  type Video,
} from "../system";

const props = defineProps<{ monitor: string | null }>();

// Cores de cada serviço nos cartões (só o nome escrito; nada de logotipos).
const SERVICE_STYLE: Record<string, string> = {
  netflix: "linear-gradient(135deg, #3a0507, #0b0102)",
  youtube: "linear-gradient(135deg, #3a0a0a, #120303)",
  prime: "linear-gradient(135deg, #0b2a4a, #04101e)",
  disney: "linear-gradient(135deg, #0c1a4d, #050a20)",
  music: "linear-gradient(135deg, #3d1466, #12061f)",
};
const SERVICE_COLOR: Record<string, string> = {
  netflix: "#e50914",
  youtube: "#ff3d3d",
  prime: "#38a8ff",
  disney: "#6fa3ff",
  music: "#c084fc",
};

const services = ref<Service[]>([]);
const library = ref<Video[]>([]);
const pairing = ref<Pairing | null>(null);
const pairedWith = ref<string | null>(null);
const secondsLeft = ref(0);
let countdown: number | undefined;

const continuing = computed(() => library.value.filter((v) => v.progress > 0.02 && v.progress < 0.95));
const hero = computed(() => continuing.value[0] ?? null);

type Item =
  | { kind: "hero" }
  | { kind: "service"; service: Service }
  | { kind: "local" }
  | { kind: "video"; video: Video };

/** As linhas da tela, de cima para baixo; o cursor anda por elas como numa TV. */
const rows = computed(() => {
  const list: { id: string; items: Item[] }[] = [];
  if (hero.value) list.push({ id: "hero", items: [{ kind: "hero" }] });
  if (continuing.value.length) list.push({ id: "continue", items: continuing.value.map((video) => ({ kind: "video", video })) });
  list.push({ id: "services", items: [...services.value.map((service) => ({ kind: "service" as const, service })), { kind: "local" }] });
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

function activate(item: Item | undefined) {
  if (!item) return;
  if (item.kind === "hero" && hero.value) playVideo(hero.value.id);
  else if (item.kind === "video") playVideo(item.video.id);
  else if (item.kind === "service") openService(item.service.id);
  else if (item.kind === "local") {
    const r = rows.value.findIndex((x) => x.id === "videos");
    if (r >= 0) {
      row.value = r;
      col.value = 0;
    }
  }
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

async function forget() {
  await remoteForget();
  if (pairing.value) pairing.value = { ...pairing.value, devices: [] };
}

async function load() {
  library.value = await videos().catch(() => []);
}

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
  stops.push(
    await on<string>("lumna://remote-paired", (name) => {
      pairedWith.value = name;
      window.setTimeout(closePairing, 2500);
    }),
  );
  services.value = await mediaServices();
  await load();
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  window.clearInterval(countdown);
});
</script>

<template>
  <main class="media">
    <div class="backdrop" :style="hero?.thumb ? { backgroundImage: `url('${artUrl(hero.thumb)}')` } : {}" />
    <div class="shade" />

    <header>
      <h1>Início</h1>
      <button class="pair" @click="openPairing">
        <Icon name="phone" :size="18" />
        Controle pelo celular
      </button>
    </header>

    <div class="rows">
      <template v-for="(line, r) in rows" :key="line.id">
        <section v-if="line.id === 'hero' && hero" class="hero" :class="{ cursor: isCursor(r, 0) }" @click="activate(line.items[0])">
          <p class="eyebrow">Continuar de onde parou</p>
          <h2>{{ hero.title }}</h2>
          <button class="primary"><Icon name="play" :size="18" /> Continuar</button>
        </section>

        <section v-else-if="line.id === 'continue'" class="row">
          <h3>Continuar assistindo</h3>
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
                <span class="bar"><span :style="{ width: `${Math.round(item.video.progress * 100)}%` }" /></span>
              </template>
            </article>
          </div>
        </section>

        <section v-else-if="line.id === 'services'" class="row">
          <h3>Assistir</h3>
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
              </template>
              <template v-else>
                <Icon name="folder" :size="26" />
                <strong>Vídeos locais</strong>
              </template>
            </article>
          </div>
        </section>

        <section v-else-if="line.id === 'videos'" class="row">
          <h3>Seus vídeos</h3>
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
              </template>
            </article>
          </div>
        </section>
      </template>
      <p v-if="!library.length" class="hint">
        Coloque vídeos na pasta Vídeos para eles aparecerem aqui, com "continuar assistindo".
      </p>
    </div>

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
            <button v-if="pairing.devices.length" class="ghost" @click="forget">Esquecer celulares</button>
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
.backdrop {
  position: absolute;
  inset: -40px;
  background-size: cover;
  background-position: center;
  filter: blur(28px);
  opacity: 0.35;
}
.shade {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(circle at 15% 0%, rgba(167, 139, 250, 0.22), transparent 50%),
    linear-gradient(180deg, rgba(10, 7, 22, 0.4), rgba(10, 7, 22, 0.95));
}
.media > :not(.backdrop):not(.shade):not(.overlay) {
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

.hero {
  padding: 34px 36px;
  border-radius: 24px;
  background: linear-gradient(90deg, rgba(10, 7, 22, 0.85), rgba(10, 7, 22, 0.2));
  border: 1px solid var(--lumna-line);
  transition: transform 220ms ease;
}
.eyebrow {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 13px;
  letter-spacing: 0.08em;
  text-transform: uppercase;
}
.hero h2 {
  margin: 8px 0 18px;
  font-size: 40px;
  letter-spacing: 0.06em;
}
.primary {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 44px;
  padding: 0 22px;
  border: 0;
  border-radius: 22px;
  background: #fff;
  color: #120c26;
  font: inherit;
  font-weight: 600;
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
