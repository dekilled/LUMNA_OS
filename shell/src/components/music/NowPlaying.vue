<script setup lang="ts">
// Celular: o player na tela toda (o esboço). Capa, título com o coração, a barra do tempo, os
// controles, a saída do som e a Emma; embaixo as abas Fila, Letras e Detalhes. Arrastar a alça
// (ou o ⌄) pra baixo fecha.
import { computed, ref } from "vue";
import Icon from "../Icon.vue";
import { haptic, musicControl, musicMode, musicSeek, toggleEmma, type MusicAction, type MusicState } from "../../system";
import DetailsView from "./DetailsView.vue";
import LyricsView from "./LyricsView.vue";
import OutputPicker from "./OutputPicker.vue";
import QueueList from "./QueueList.vue";
import { albumOf, artistOf, clock, coverUrl } from "./music";
import { favorites, openMenu, panel, toggleFavorite } from "./store";

const props = defineProps<{ music: MusicState }>();
const emit = defineEmits<{ close: [] }>();
const track = computed(() => props.music.current!);

function act(a: MusicAction) {
  haptic(10).catch(() => {});
  musicControl(a).catch(() => {});
}
function cycleRepeat() {
  const r = props.music.repeat;
  musicMode({ repeat: r === "off" ? "all" : r === "all" ? "one" : "off" }).catch(() => {});
}
let seekTimer = 0;
function seek(v: number) {
  props.music.position = v;
  clearTimeout(seekTimer);
  seekTimer = window.setTimeout(() => musicSeek(v).catch(() => {}), 120);
}

let dragY: number | null = null;
const drag = ref(0);
function down(e: PointerEvent) {
  dragY = e.clientY;
}
function move(e: PointerEvent) {
  if (dragY != null) drag.value = Math.max(0, e.clientY - dragY);
}
function up() {
  if (dragY == null) return;
  dragY = null;
  if (drag.value > 110) emit("close");
  drag.value = 0;
}
</script>

<template>
  <div class="now" :style="{ transform: drag ? `translateY(${drag}px)` : undefined }">
    <header @pointerdown="down" @pointermove="move" @pointerup="up" @pointercancel="up">
      <button class="icon" aria-label="Fechar" @click="emit('close')"><Icon name="chevron-down" :size="28" /></button>
      <div class="where">
        <strong>LUMNA Music</strong>
        <small>{{ albumOf(track) }}</small>
      </div>
      <button class="icon" aria-label="Mais opções" @click="openMenu(track, $event)"><Icon name="more" :size="24" /></button>
    </header>

    <div class="scroll">
      <div class="cover">
        <img v-if="coverUrl(track)" :src="coverUrl(track)!" alt="" draggable="false" />
        <Icon v-else name="music" :size="90" />
      </div>

      <div class="titles">
        <div>
          <strong>{{ track.title }}</strong>
          <small>{{ artistOf(track) }}</small>
        </div>
        <button class="heart" :class="{ on: favorites.has(track.path) }" :aria-label="favorites.has(track.path) ? 'Tirar das favoritas' : 'Favoritar'" @click="toggleFavorite(track)">
          <Icon name="heart" :size="24" />
        </button>
      </div>

      <input
        class="seek"
        type="range"
        min="0"
        :max="Math.max(1, music.duration || track.duration)"
        step="0.5"
        :value="music.position"
        :style="{ '--p': `${((music.position || 0) / Math.max(1, music.duration || track.duration)) * 100}%` }"
        @input="seek(+($event.target as HTMLInputElement).value)"
      />
      <div class="times"><span>{{ clock(music.position) }}</span><span>{{ clock(music.duration || track.duration) }}</span></div>

      <div class="controls">
        <button class="icon" :class="{ on: music.shuffle }" aria-label="Aleatório" @click="musicMode({ shuffle: !music.shuffle }).catch(() => {})">
          <Icon name="shuffle" :size="28" />
        </button>
        <button class="icon big" aria-label="Anterior" @click="act('previous')"><Icon name="skip-back" :size="36" /></button>
        <button class="play" :aria-label="music.playing ? 'Pausar' : 'Tocar'" @click="act('toggle')">
          <Icon :name="music.playing ? 'pause' : 'media-play'" :size="38" />
        </button>
        <button class="icon big" aria-label="Próxima" @click="act('next')"><Icon name="skip-forward" :size="36" /></button>
        <button class="icon" :class="{ on: music.repeat !== 'off' }" aria-label="Repetir" @click="cycleRepeat()">
          <Icon :name="music.repeat === 'one' ? 'repeat-one' : 'repeat'" :size="28" />
        </button>
      </div>

      <div class="row">
        <OutputPicker phone big up />
        <button class="emma" @click="toggleEmma().catch(() => {})">
          <svg class="gem" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 1.5 21.5 12 12 22.5 2.5 12z" fill="#8b5cf6" /></svg>
          <span>Emma</span>
          <Icon name="forward" :size="20" />
        </button>
      </div>

      <div class="divider" />
      <h2 v-if="panel === 'fila'">A seguir</h2>
      <QueueList v-if="panel === 'fila'" :music="music" />
      <LyricsView v-else-if="panel === 'letras'" :track="track" :position="music.position" />
      <DetailsView v-else :track="track" />
    </div>

    <nav class="tabs">
      <button :class="{ on: panel === 'fila' }" @click="panel = 'fila'"><Icon name="list" :size="24" /> Fila</button>
      <button :class="{ on: panel === 'letras' }" @click="panel = 'letras'"><Icon name="document" :size="24" /> Letras</button>
      <button :class="{ on: panel === 'detalhes' }" @click="panel = 'detalhes'"><Icon name="info" :size="24" /> Detalhes</button>
    </nav>
  </div>
</template>

<style scoped>
.now {
  position: absolute;
  inset: 0;
  z-index: 20;
  display: flex;
  flex-direction: column;
  background: radial-gradient(120% 60% at 70% 0%, rgba(109, 40, 217, 0.45), transparent 70%), #0b0912;
  transition: transform 0.12s;
}
header {
  flex: none;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 8px 10px 4px;
  touch-action: none;
}
.where {
  display: flex;
  flex-direction: column;
  align-items: center;
  min-width: 0;
}
.where strong {
  font-size: 16px;
  font-weight: 500;
}
.where small {
  max-width: 60vw;
  overflow: hidden;
  color: var(--lumna-muted);
  font-size: 14px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.icon {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-text);
}
.icon.on,
.controls .icon:first-child,
.controls .icon:last-child {
  color: var(--lumna-violet-soft);
}
.controls .icon:not(.on):first-child,
.controls .icon:not(.on):last-child {
  opacity: 0.55;
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 6px 30px 16px;
}
.cover {
  aspect-ratio: 1;
  max-height: 46vh;
  margin: 8px auto 0;
  display: grid;
  place-items: center;
  overflow: hidden;
  border: 1px solid rgba(167, 139, 250, 0.45);
  border-radius: 14px;
  background: linear-gradient(135deg, #2a1f45, #120f1d);
  color: rgba(167, 139, 250, 0.6);
}
.cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.titles {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-top: 22px;
}
.titles > div {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.titles strong {
  overflow: hidden;
  font-size: 26px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.titles small {
  overflow: hidden;
  color: #b7a6ea;
  font-size: 18px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.heart {
  flex: none;
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 50%;
  background: rgba(30, 24, 46, 0.8);
  color: var(--lumna-muted);
}
.heart.on {
  color: #b9a2fb;
}
.heart.on :deep(svg) {
  fill: currentColor;
}
.seek {
  -webkit-appearance: none;
  appearance: none;
  width: 100%;
  height: 26px;
  margin-top: 18px;
  background: transparent;
}
.seek::-webkit-slider-runnable-track {
  height: 6px;
  border-radius: 3px;
  background: linear-gradient(90deg, #a78bfa var(--p, 0%), rgba(255, 255, 255, 0.14) var(--p, 0%));
}
.seek::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 22px;
  height: 22px;
  margin-top: -8px;
  border-radius: 50%;
  background: #b9a2fb;
  box-shadow: 0 0 0 4px rgba(167, 139, 250, 0.25);
}
.times {
  display: flex;
  justify-content: space-between;
  color: var(--lumna-muted);
  font-size: 14px;
  font-variant-numeric: tabular-nums;
}
.controls {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-top: 14px;
}
.icon.big {
  width: 60px;
  height: 60px;
}
.play {
  width: 84px;
  height: 84px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: #a78bfa;
  color: #1a1030;
  box-shadow: 0 10px 30px rgba(139, 92, 246, 0.35);
}
.row {
  display: grid;
  gap: 10px !important;
  grid-template-columns: minmax(0, 1fr) minmax(0, 1fr);
  gap: 14px;
  margin-top: 22px;
}
.emma {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 60px;
  padding: 0 14px 0 18px;
  border: 1px solid rgba(139, 92, 246, 0.6);
  border-radius: 999px;
  background: rgba(76, 29, 149, 0.25);
  color: var(--lumna-text);
  font: inherit;
  font-size: 17px;
}
.emma span {
  flex: 1;
  text-align: left;
}
.gem {
  width: 28px;
  height: 28px;
}
.divider {
  height: 1px;
  margin: 22px 0 14px;
  background: rgba(167, 139, 250, 0.16);
}
h2 {
  margin: 0 0 8px;
  font-size: 18px;
  font-weight: 500;
}
.tabs {
  flex: none;
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
  padding: 10px 20px 14px;
  border-top: 1px solid rgba(167, 139, 250, 0.12);
}
.tabs button {
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
  height: 52px;
  border: 0;
  border-radius: 999px;
  background: none;
  color: #cfc6ee;
  font: inherit;
  font-size: 16px;
}
.tabs button.on {
  background: rgba(76, 29, 149, 0.35);
  color: var(--lumna-violet-soft);
}
</style>
