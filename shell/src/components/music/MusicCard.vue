<script setup lang="ts">
// O que está tocando no LUMNA Music, num cartão: na central e no bloqueio (aqui sem abrir o app).
// Só aparece com uma música carregada.
import { computed } from "vue";
import Icon from "../Icon.vue";
import { haptic, musicControl, musicOpen, type MusicAction } from "../../system";
import { artistOf, clock, coverUrl, useMusic } from "./music";

const props = defineProps<{ openable?: boolean }>();
const emit = defineEmits<{ opened: [] }>();
const music = useMusic();
const track = computed(() => music.value?.current ?? null);
const progress = computed(() => {
  const s = music.value;
  return s && s.duration > 0 ? Math.min(100, (s.position / s.duration) * 100) : 0;
});

function act(action: MusicAction) {
  haptic(10).catch(() => {});
  musicControl(action).catch(() => {});
}
function open() {
  if (!props.openable) return;
  emit("opened");
  musicOpen().catch(() => {});
}
</script>

<template>
  <div v-if="track && music" class="music-card">
    <button class="art" :class="{ openable }" :aria-label="openable ? 'Abrir o LUMNA Music' : undefined" @click="open()">
      <img v-if="coverUrl(track)" :src="coverUrl(track)!" alt="" draggable="false" />
      <Icon v-else name="music" :size="26" />
    </button>
    <div class="info" @click="open()">
      <strong>{{ track.title }}</strong>
      <small>{{ artistOf(track) }}</small>
      <div class="bar"><span :style="{ width: `${progress}%` }" /></div>
      <small class="time">{{ clock(music.position) }} / {{ clock(music.duration || track.duration) }}</small>
    </div>
    <div class="buttons">
      <button aria-label="Anterior" @click="act('previous')"><Icon name="skip-back" :size="22" /></button>
      <button class="play" :aria-label="music.playing ? 'Pausar' : 'Tocar'" @click="act('toggle')">
        <Icon :name="music.playing ? 'pause' : 'media-play'" :size="24" />
      </button>
      <button aria-label="Próxima" @click="act('next')"><Icon name="skip-forward" :size="22" /></button>
    </div>
  </div>
</template>

<style scoped>
.music-card {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 12px;
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
  color: var(--lumna-text);
}
.art {
  flex: none;
  width: 56px;
  height: 56px;
  display: grid;
  place-items: center;
  overflow: hidden;
  padding: 0;
  border: 0;
  border-radius: 12px;
  background: linear-gradient(135deg, #c026d3, #6d28d9);
  color: #fff;
}
.art img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.info {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 1px;
}
.info strong,
.info small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.info strong {
  font-size: 15px;
}
.info small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.bar {
  height: 3px;
  margin: 6px 0 2px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.12);
  overflow: hidden;
}
.bar span {
  display: block;
  height: 100%;
  background: var(--lumna-violet-soft);
}
.time {
  font-variant-numeric: tabular-nums;
}
.buttons {
  flex: none;
  display: flex;
  align-items: center;
}
.buttons button {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-text);
}
.buttons .play {
  width: 46px;
  height: 46px;
  background: var(--lumna-violet-soft);
  color: #1a1030;
}
</style>
