<script setup lang="ts">
// PC: a barra do player embaixo (o esboço). A música com o coração, os controles com o tempo, o
// volume do player, a saída do som e o botão da fila.
import { computed } from "vue";
import Icon from "../Icon.vue";
import { haptic, musicControl, musicMode, musicSeek, musicVolume, type MusicAction, type MusicState } from "../../system";
import OutputPicker from "./OutputPicker.vue";
import { artistOf, clock, coverUrl } from "./music";
import { favorites, toggleFavorite } from "./store";

const props = defineProps<{ music: MusicState; queueOpen: boolean }>();
const emit = defineEmits<{ queue: []; cover: [] }>();
const track = computed(() => props.music.current);
const total = computed(() => props.music.duration || track.value?.duration || 0);

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
let volTimer = 0;
function volume(v: number) {
  props.music.volume = v;
  clearTimeout(volTimer);
  volTimer = window.setTimeout(() => musicVolume(v).catch(() => {}), 60);
}
</script>

<template>
  <footer class="bar">
    <div class="song">
      <template v-if="track">
        <button class="cover" aria-label="Ver o que está tocando" @click="emit('cover')">
          <img v-if="coverUrl(track)" :src="coverUrl(track)!" alt="" draggable="false" />
          <Icon v-else name="music" :size="26" />
        </button>
        <div class="names">
          <strong>{{ track.title }}</strong>
          <small>{{ artistOf(track) }}</small>
        </div>
        <button class="heart" :class="{ on: favorites.has(track.path) }" aria-label="Favoritar" @click="toggleFavorite(track)">
          <Icon name="heart" :size="22" />
        </button>
      </template>
      <span v-else class="idle">Nada tocando</span>
    </div>

    <div class="center">
      <div class="controls">
        <button class="icon side" :class="{ on: music.shuffle }" aria-label="Aleatório" @click="musicMode({ shuffle: !music.shuffle }).catch(() => {})">
          <Icon name="shuffle" :size="20" />
        </button>
        <button class="icon" aria-label="Anterior" :disabled="!track" @click="act('previous')"><Icon name="skip-back" :size="24" /></button>
        <button class="play" :aria-label="music.playing ? 'Pausar' : 'Tocar'" :disabled="!track" @click="act('toggle')">
          <Icon :name="music.playing ? 'pause' : 'media-play'" :size="26" />
        </button>
        <button class="icon" aria-label="Próxima" :disabled="!track" @click="act('next')"><Icon name="skip-forward" :size="24" /></button>
        <button class="icon side" :class="{ on: music.repeat !== 'off' }" aria-label="Repetir" @click="cycleRepeat()">
          <Icon :name="music.repeat === 'one' ? 'repeat-one' : 'repeat'" :size="20" />
        </button>
      </div>
      <div class="time">
        <span>{{ clock(music.position) }}</span>
        <input
          type="range"
          min="0"
          :max="Math.max(1, total)"
          step="0.5"
          :value="music.position"
          :disabled="!track"
          :style="{ '--p': `${(music.position / Math.max(1, total)) * 100}%` }"
          @input="seek(+($event.target as HTMLInputElement).value)"
        />
        <span>{{ clock(total) }}</span>
      </div>
    </div>

    <div class="right">
      <button class="icon" :aria-label="music.volume ? 'Volume' : 'Sem som'" @click="volume(music.volume ? 0 : 80)">
        <Icon :name="music.volume ? 'volume' : 'mute'" :size="22" />
      </button>
      <input
        class="vol"
        type="range"
        min="0"
        max="100"
        :value="music.volume"
        :style="{ '--p': `${music.volume}%` }"
        aria-label="Volume da música"
        @input="volume(+($event.target as HTMLInputElement).value)"
      />
      <OutputPicker :phone="false" up />
      <button class="icon" :class="{ on: queueOpen }" aria-label="Fila de reprodução" @click="emit('queue')"><Icon name="list" :size="22" /></button>
    </div>
  </footer>
</template>

<style scoped>
.bar {
  flex: none;
  display: grid;
  grid-template-columns: minmax(220px, 1fr) minmax(360px, 1.6fr) minmax(220px, 1fr);
  align-items: center;
  gap: 18px;
  height: 100px;
  padding: 0 20px;
  border-top: 1px solid rgba(167, 139, 250, 0.12);
  background: rgba(11, 9, 18, 0.92);
}
.song {
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 14px;
}
.cover {
  flex: none;
  width: 74px;
  height: 74px;
  display: grid;
  place-items: center;
  overflow: hidden;
  padding: 0;
  border: 1px solid rgba(167, 139, 250, 0.25);
  border-radius: 8px;
  background: #1c1828;
  color: var(--lumna-muted);
}
.cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.names {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.names strong,
.names small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.names strong {
  font-weight: 500;
}
.names small {
  color: var(--lumna-muted);
  font-size: 13px;
}
.idle {
  color: var(--lumna-muted);
}
.heart {
  flex: none;
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.heart.on {
  color: #a78bfa;
}
.heart.on :deep(svg) {
  fill: currentColor;
}
.center {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 4px;
}
.controls {
  display: flex;
  align-items: center;
  gap: 18px;
}
.icon {
  width: 42px;
  height: 42px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-text);
}
.icon:disabled,
.play:disabled {
  opacity: 0.4;
}
.icon.side {
  color: var(--lumna-violet-soft);
  opacity: 0.6;
}
.icon.side.on,
.icon.on {
  color: var(--lumna-violet-soft);
  opacity: 1;
}
.play {
  width: 56px;
  height: 56px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: #8b5cf6;
  color: #160d2b;
  box-shadow: 0 6px 22px rgba(139, 92, 246, 0.45);
}
.time {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 14px;
  color: var(--lumna-muted);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
}
input[type="range"] {
  -webkit-appearance: none;
  appearance: none;
  flex: 1;
  height: 20px;
  background: transparent;
}
input[type="range"]::-webkit-slider-runnable-track {
  height: 5px;
  border-radius: 3px;
  background: linear-gradient(90deg, #a78bfa var(--p, 0%), rgba(255, 255, 255, 0.16) var(--p, 0%));
}
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 16px;
  height: 16px;
  margin-top: -5.5px;
  border-radius: 50%;
  background: #b9a2fb;
}
.right {
  display: flex;
  align-items: center;
  justify-content: flex-end;
  gap: 10px;
}
.vol {
  flex: 0 1 120px !important;
  min-width: 60px;
}
.right :deep(.output) {
  min-width: 0;
  max-width: 220px;
}
</style>
