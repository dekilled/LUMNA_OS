<script setup lang="ts">
// Barra de serviços da Mídia: sobe por cima do streaming (Super + M, Super + Esc ou o botão central
// do controle) para voltar ao Início, trocar de serviço ou ir para os vídeos locais. O serviço que
// sai fica vivo, pausado, e a página dele vira um Recente.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { SERVICE_COLOR, serviceMark } from "../mediaBrand";
import {
  keyToPad,
  mediaServices,
  on,
  toggleEmma,
  tvbarClose,
  tvHome,
  tvOpen,
  type PadAction,
  type Service,
  type TvState,
} from "../system";

type Tile =
  | { kind: "home" }
  | { kind: "service"; service: Service }
  | { kind: "local" };

const shown = ref(false);
const monitor = ref<string | null>(null);
const state = ref<TvState>({ playing: null, live: [], recents: [] });
const services = ref<Service[]>([]);
const cursor = ref(0);
const busy = ref(false);

const tiles = computed<Tile[]>(() => [
  { kind: "home" },
  ...services.value.map((service) => ({ kind: "service" as const, service })),
  { kind: "local" },
]);

const playingName = computed(() => services.value.find((s) => s.id === state.value.playing)?.name ?? null);

/** Última página guardada de um serviço (para retomar quando ele não está mais vivo). */
const lastPage = (id: string) => state.value.recents.find((r) => r.service === id) ?? null;

function sub(tile: Tile): string {
  if (tile.kind === "home") return "Seus serviços e recentes";
  if (tile.kind === "local") return "Da pasta Vídeos";
  const id = tile.service.id;
  if (state.value.playing === id) return "Em uso";
  if (state.value.live.includes(id) || lastPage(id)) return "Retomar última página";
  return "Abrir";
}

async function opened(data: { monitor: string; state: TvState }) {
  monitor.value = data.monitor;
  state.value = data.state;
  busy.value = false;
  if (!services.value.length) services.value = await mediaServices().catch(() => []);
  const at = tiles.value.findIndex((t) => t.kind === "service" && t.service.id === data.state.playing);
  cursor.value = at >= 0 ? at : 0;
  shown.value = true;
  await nextTick();
  showCursor();
}

/** O cartão escolhido fica visível e com o foco (o foco traz o teclado para a camada). */
function showCursor() {
  const tile = document.querySelector<HTMLElement>(".tile.cursor");
  tile?.focus({ preventScroll: true });
  tile?.scrollIntoView({ inline: "nearest", block: "nearest", behavior: "smooth" });
}

function close() {
  shown.value = false;
  tvbarClose();
}

async function choose(tile: Tile | undefined) {
  if (!tile || busy.value) return;
  if (tile.kind === "service" && tile.service.id === state.value.playing) {
    close();
    return;
  }
  busy.value = true;
  shown.value = false;
  try {
    if (tile.kind === "home") await tvHome(monitor.value);
    else if (tile.kind === "local") await tvHome(monitor.value, "videos");
    else {
      const id = tile.service.id;
      // Vivo: volta exatamente onde estava. Senão, abre na última página guardada.
      const url = state.value.live.includes(id) ? null : (lastPage(id)?.url ?? null);
      await tvOpen(monitor.value, id, url);
    }
  } catch (e) {
    console.error(e);
  } finally {
    busy.value = false;
  }
}

function emma() {
  close();
  toggleEmma();
}

function act(action: PadAction) {
  if (!shown.value) return;
  if (action === "left") cursor.value = Math.max(0, cursor.value - 1);
  else if (action === "right") cursor.value = Math.min(tiles.value.length - 1, cursor.value + 1);
  else if (action === "accept") choose(tiles.value[cursor.value]);
  else if (action === "back") close();
  nextTick(showCursor);
}

function onKey(event: KeyboardEvent) {
  const action = keyToPad(event);
  if (action) {
    event.preventDefault();
    act(action);
  }
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://tvbar", opened));
  stops.push(await on<{ action: PadAction }>("lumna://tvbar-pad", (e) => act(e.action)));
  services.value = await mediaServices().catch(() => []);
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="veil" :class="{ shown }" @click.self="close()">
    <button class="back" @click="close()"><kbd>Esc</kbd> Voltar ao vídeo</button>

    <section class="bar">
      <p class="now">
        <span class="dot" />
        <template v-if="playingName">Reproduzindo no <b>{{ playingName }}</b></template>
        <template v-else>Reproduzindo um vídeo local</template>
      </p>

      <div class="tiles">
        <button
          v-for="(tile, i) in tiles"
          :key="i"
          class="tile"
          :class="{ cursor: i === cursor, playing: tile.kind === 'service' && tile.service.id === state.playing }"
          @mouseenter="cursor = i"
          @click="choose(tile)"
        >
          <span
            class="badge"
            :style="tile.kind === 'service' ? { background: SERVICE_COLOR[tile.service.id] ?? '#7c5cff' } : {}"
          >
            <Icon v-if="tile.kind === 'home'" name="home" :size="22" />
            <Icon v-else-if="tile.kind === 'local'" name="folder" :size="22" />
            <Icon v-else-if="tile.service.id === 'music'" name="music" :size="22" />
            <b v-else>{{ serviceMark(tile.service.name) }}</b>
          </span>
          <span class="text">
            <span class="name">{{ tile.kind === "home" ? "Início" : tile.kind === "local" ? "Vídeos locais" : tile.service.name }}</span>
            <span class="sub" :class="{ live: sub(tile) === 'Em uso' }">{{ sub(tile) }}</span>
          </span>
        </button>
      </div>

      <footer>
        <span class="hints">
          <span><kbd>←</kbd><kbd>→</kbd> Escolher</span>
          <span><kbd>Enter</kbd> Trocar</span>
          <span><kbd>Esc</kbd> Fechar</span>
        </span>
        <button class="mic" title="Falar com a Emma" @click="emma()">
          <Icon name="mic" :size="20" />
        </button>
      </footer>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
  display: flex;
  flex-direction: column;
  justify-content: flex-end;
  background: linear-gradient(180deg, transparent 35%, rgba(8, 5, 18, 0.55) 60%, rgba(8, 5, 18, 0.94) 100%);
  opacity: 0;
  transition: opacity 180ms ease;
  color: #ece8ff;
  user-select: none;
}
.veil.shown {
  opacity: 1;
}

.back {
  position: absolute;
  top: 28px;
  right: 32px;
  display: flex;
  align-items: center;
  gap: 8px;
  height: 38px;
  padding: 0 16px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 19px;
  background: rgba(14, 10, 28, 0.7);
  backdrop-filter: blur(12px);
  color: inherit;
  font: inherit;
  font-size: 14px;
}

.bar {
  padding: 0 56px 28px;
  transform: translateY(24px);
  transition: transform 260ms cubic-bezier(0.22, 1, 0.36, 1);
}
.veil.shown .bar {
  transform: none;
}

.now {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0 0 14px;
  font-size: 15px;
  color: rgba(236, 232, 255, 0.8);
}
.now b {
  color: #fff;
}
.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #a78bfa;
  box-shadow: 0 0 10px #a78bfa;
}

.tiles {
  display: flex;
  gap: 12px;
  padding: 8px 4px;
  overflow-x: auto;
  scrollbar-width: none;
}
.tile {
  flex: 1 0 168px;
  display: flex;
  align-items: center;
  gap: 12px;
  max-width: 240px;
  height: 84px;
  padding: 0 16px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 16px;
  background: rgba(20, 15, 40, 0.92);
  color: inherit;
  font: inherit;
  text-align: left;
  outline: none;
  transition: transform 180ms ease, border-color 180ms ease, box-shadow 180ms ease;
}
.tile.cursor {
  border-color: #a78bfa;
  box-shadow: 0 0 0 2px #a78bfa, 0 12px 36px rgba(124, 92, 255, 0.35);
  transform: translateY(-4px);
}
.tile.playing {
  background: rgba(124, 92, 255, 0.22);
}
.badge {
  flex: none;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: rgba(167, 139, 250, 0.2);
  color: #fff;
  font-size: 20px;
}
.text {
  display: flex;
  flex-direction: column;
  gap: 3px;
  min-width: 0;
}
.name {
  font-size: 16px;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.sub {
  font-size: 12px;
  color: rgba(236, 232, 255, 0.6);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.sub.live {
  color: #c4b5fd;
}

footer {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 16px;
}
.hints {
  display: flex;
  gap: 22px;
  font-size: 13px;
  color: rgba(236, 232, 255, 0.7);
}
kbd {
  display: inline-grid;
  place-items: center;
  min-width: 22px;
  height: 22px;
  margin-right: 4px;
  padding: 0 6px;
  border-radius: 6px;
  background: rgba(255, 255, 255, 0.12);
  font: inherit;
  font-size: 11px;
  font-weight: 600;
}
.mic {
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
  color: #fff;
  box-shadow: 0 8px 26px rgba(124, 58, 237, 0.5);
}
</style>
