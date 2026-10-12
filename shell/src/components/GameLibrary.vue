<script setup lang="ts">
// Biblioteca do modo Gaming: carrossel de capas navegável por controle, teclado ou mouse.
// Duas zonas de foco: o carrossel (esquerda/direita escolhe o jogo, A joga) e os filtros
// (esquerda/direita escolhe, A aplica). Para cima e para baixo trocam de zona.
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { artUrl, games, keyToPad, launchGame, onPad, type Game, type PadAction } from "../system";
import Icon from "./Icon.vue";

const props = defineProps<{ monitor: string | null }>();

type Filter = "all" | "recent" | Game["source"];
const FILTER_LABEL: Record<Filter, string> = {
  all: "Todos",
  recent: "Recentes",
  steam: "Steam",
  epic: "Epic",
  gog: "GOG",
  amazon: "Amazon",
  lutris: "Lutris",
};

const library = ref<Game[]>([]);
const loaded = ref(false);
const filter = ref<Filter>("all");
const selected = ref(0);
const zone = ref<"carousel" | "filters">("carousel");
const filterIndex = ref(0);
const launching = ref<string | null>(null);

/** Só os filtros que têm jogos (sem "Epic" vazio para quem não usa o Heroic). */
const filters = computed<Filter[]>(() => {
  const sources = new Set(library.value.map((g) => g.source));
  const list: Filter[] = ["all"];
  if (library.value.some((g) => g.last_played > 0)) list.push("recent");
  for (const s of ["steam", "epic", "gog", "amazon", "lutris"] as const) if (sources.has(s)) list.push(s);
  return list;
});

const shown = computed(() => {
  if (filter.value === "all") return library.value;
  if (filter.value === "recent") return library.value.filter((g) => g.last_played > 0).slice(0, 12);
  return library.value.filter((g) => g.source === filter.value);
});

const current = computed(() => shown.value[selected.value] ?? null);
const background = computed(() => artUrl(current.value?.hero ?? current.value?.cover ?? null));

watch(filter, () => (selected.value = 0));

async function load() {
  library.value = await games().catch(() => []);
  loaded.value = true;
  selected.value = Math.min(selected.value, Math.max(0, shown.value.length - 1));
}

function play(game: Game | null) {
  if (!game) return;
  launching.value = game.id;
  launchGame(game.id).finally(() => window.setTimeout(() => (launching.value = null), 4000));
}

function act(action: PadAction) {
  const count = shown.value.length;
  if (zone.value === "carousel") {
    if (action === "left") selected.value = Math.max(0, selected.value - 1);
    else if (action === "right") selected.value = Math.min(count - 1, selected.value + 1);
    else if (action === "accept") play(current.value);
    else if (action === "down") {
      zone.value = "filters";
      filterIndex.value = filters.value.indexOf(filter.value);
    }
  } else {
    if (action === "left") filterIndex.value = Math.max(0, filterIndex.value - 1);
    else if (action === "right") filterIndex.value = Math.min(filters.value.length - 1, filterIndex.value + 1);
    else if (action === "accept") {
      filter.value = filters.value[filterIndex.value];
      zone.value = "carousel";
    } else if (action === "up" || action === "back") zone.value = "carousel";
  }
}

function onKey(event: KeyboardEvent) {
  const action = keyToPad(event);
  if (action) {
    event.preventDefault();
    act(action);
  }
}

function clickCard(index: number) {
  zone.value = "carousel";
  if (index === selected.value) play(current.value);
  else selected.value = index;
}

function clickFilter(f: Filter) {
  filter.value = f;
  zone.value = "carousel";
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await onPad(() => props.monitor, act));
  await load();
});

onUnmounted(() => stops.forEach((stop) => stop()));

// O carrossel desliza para manter o jogo escolhido no centro.
const CARD = 248;
const GAP = 28;
const offset = computed(() => -(selected.value * (CARD + GAP)));
</script>

<template>
  <main class="library">
    <div class="backdrop" :style="background ? { backgroundImage: `url('${background}')` } : {}" />
    <div class="shade" />

    <header>
      <h1>Sua biblioteca</h1>
      <span class="dnd"><Icon name="gamepad" :size="16" /> Não perturbe</span>
    </header>

    <section v-if="!loaded" class="empty" />
    <section v-else-if="!library.length" class="empty">
      <strong>Nenhum jogo instalado ainda</strong>
      <p>Jogos da Steam, do Heroic (Epic, GOG, Amazon) e do Lutris aparecem aqui sozinhos.</p>
    </section>

    <template v-else>
      <section class="carousel" :class="{ focused: zone === 'carousel' }">
        <div class="track" :style="{ transform: `translateX(${offset}px)`, '--card': `${CARD}px`, '--gap': `${GAP}px` }">
          <article
            v-for="(game, i) in shown"
            :key="game.id"
            class="card"
            :class="{ selected: i === selected }"
            @click="clickCard(i)"
          >
            <img v-if="game.cover" :src="artUrl(game.cover)!" alt="" loading="lazy" />
            <div v-else class="placeholder">{{ game.title }}</div>
            <span class="source">{{ FILTER_LABEL[game.source] }}</span>
            <button v-if="i === selected" class="play" @click.stop="play(game)">
              <Icon name="play" :size="18" />
              {{ launching === game.id ? "Abrindo…" : "Jogar" }}
            </button>
          </article>
        </div>
      </section>

      <h2 v-if="current" class="title">{{ current.title }}</h2>

      <div class="dots">
        <span v-for="(_, i) in shown.slice(0, 12)" :key="i" :class="{ on: i === Math.min(selected, 11) }" />
      </div>

      <nav class="filters" :class="{ focused: zone === 'filters' }">
        <button
          v-for="(f, i) in filters"
          :key="f"
          class="chip"
          :class="{ active: f === filter, cursor: zone === 'filters' && i === filterIndex }"
          @click="clickFilter(f)"
        >
          {{ FILTER_LABEL[f] }}
        </button>
      </nav>
    </template>
  </main>
</template>

<style scoped>
.library {
  position: fixed;
  inset: 0;
  overflow: hidden;
  display: flex;
  flex-direction: column;
  padding: 40px 0 36px;
  box-sizing: border-box;
  background: linear-gradient(180deg, #0d0a1c, #120c26);
  animation: enter 320ms cubic-bezier(0.22, 1, 0.36, 1);
}

/* Fundo: a arte do jogo escolhido, desfocada, por baixo de um véu escuro. */
.backdrop {
  position: absolute;
  inset: -40px;
  background-size: cover;
  background-position: center;
  filter: blur(36px) saturate(1.2);
  opacity: 0.45;
  transition: background-image 400ms ease;
}
.shade {
  position: absolute;
  inset: 0;
  background:
    radial-gradient(circle at 75% 0%, rgba(124, 58, 237, 0.3), transparent 55%),
    linear-gradient(180deg, rgba(10, 7, 22, 0.55), rgba(10, 7, 22, 0.92));
}
.library > :not(.backdrop):not(.shade) {
  position: relative;
}

header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 64px;
}
h1 {
  margin: 0;
  font-size: 34px;
  font-weight: 700;
}
.dnd {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 7px 14px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid var(--lumna-line);
  color: var(--lumna-muted);
  font-size: 13px;
}

.carousel {
  flex: 1;
  display: flex;
  align-items: center;
  /* O primeiro cartão começa no centro da tela; o trilho desliza a partir dele. */
  padding-left: calc(50% - 124px);
  min-height: 0;
}
.track {
  display: flex;
  gap: var(--gap);
  transition: transform 380ms cubic-bezier(0.22, 1, 0.36, 1);
}

.card {
  position: relative;
  flex: none;
  width: var(--card);
  aspect-ratio: 2 / 3;
  border-radius: 18px;
  overflow: hidden;
  background: #1b1433;
  box-shadow: 0 18px 40px rgba(0, 0, 0, 0.5);
  opacity: 0.55;
  transform: scale(0.9);
  transition: transform 320ms cubic-bezier(0.22, 1, 0.36, 1), opacity 260ms ease, box-shadow 260ms ease;
  cursor: pointer;
}
.card.selected {
  opacity: 1;
  transform: scale(1.06);
  box-shadow:
    0 0 0 3px #a78bfa,
    0 0 40px rgba(139, 92, 246, 0.55),
    0 24px 50px rgba(0, 0, 0, 0.6);
}
.carousel:not(.focused) .card.selected {
  box-shadow: 0 0 0 2px rgba(167, 139, 250, 0.4), 0 24px 50px rgba(0, 0, 0, 0.6);
}
.card img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}
.placeholder {
  height: 100%;
  display: grid;
  place-items: center;
  padding: 18px;
  text-align: center;
  font-size: 20px;
  font-weight: 700;
  background: linear-gradient(160deg, #4c1d95, #1e1338);
}
.source {
  position: absolute;
  top: 10px;
  left: 10px;
  padding: 3px 9px;
  border-radius: 999px;
  background: rgba(10, 7, 22, 0.7);
  font-size: 11px;
  color: var(--lumna-muted);
}
.play {
  position: absolute;
  left: 16px;
  right: 16px;
  bottom: 16px;
  height: 46px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  border: 0;
  border-radius: 12px;
  background: linear-gradient(135deg, #a78bfa, #7c3aed);
  color: #fff;
  font: inherit;
  font-size: 16px;
  font-weight: 600;
  box-shadow: 0 8px 24px rgba(124, 58, 237, 0.5);
}

.title {
  margin: 8px 0 0;
  text-align: center;
  font-size: 22px;
  font-weight: 600;
}

.dots {
  display: flex;
  justify-content: center;
  gap: 8px;
  margin: 14px 0 18px;
}
.dots span {
  width: 7px;
  height: 7px;
  border-radius: 4px;
  background: rgba(255, 255, 255, 0.2);
  transition: width 200ms ease, background-color 200ms ease;
}
.dots span.on {
  width: 18px;
  background: #a78bfa;
}

.filters {
  display: flex;
  justify-content: center;
  gap: 10px;
}
.chip {
  padding: 10px 22px;
  border-radius: 999px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.05);
  color: var(--lumna-muted);
  font: inherit;
  font-size: 14px;
  transition: background-color 180ms ease, color 180ms ease, box-shadow 180ms ease;
}
.chip.active {
  background: linear-gradient(135deg, #a78bfa, #7c3aed);
  border-color: transparent;
  color: #fff;
}
.chip.cursor {
  box-shadow: 0 0 0 2px #fff;
}

.empty {
  flex: 1;
  display: grid;
  place-content: center;
  gap: 6px;
  margin: 24px 64px 0;
  text-align: center;
  border: 1px dashed var(--lumna-line);
  border-radius: 24px;
  color: var(--lumna-muted);
}
.empty strong {
  color: var(--lumna-text);
  font-size: 18px;
}
.empty p {
  margin: 0;
}

@keyframes enter {
  from {
    opacity: 0;
    transform: scale(1.02);
  }
}
</style>
