<script setup lang="ts">
// "A seguir": as próximas da fila. Tocar pula pra ela; arrastar pelo ≡ muda de lugar; o × tira.
import { ref } from "vue";
import Icon from "../Icon.vue";
import { musicJump, musicQueueMove, musicQueueRemove, type MusicState } from "../../system";
import { artistOf, clock, coverUrl } from "./music";

const props = defineProps<{ music: MusicState; limit?: number }>();

const ROW = 72;
const drag = ref<{ from: number; start: number; dy: number; id: number } | null>(null);

const shown = () => props.music.next.slice(0, props.limit ?? 50);
/** Posição na fila inteira da i-ésima próxima. */
const abs = (i: number) => (props.music.index ?? 0) + 1 + i;

function down(i: number, e: PointerEvent) {
  e.preventDefault();
  // o WebKitGTK manda o mesmo dedo também como "mouse": vale o primeiro
  if (drag.value) return;
  drag.value = { from: i, start: e.clientY, dy: 0, id: e.pointerId };
  try {
    (e.currentTarget as HTMLElement).setPointerCapture(e.pointerId);
  } catch {}
}
function move(e: PointerEvent) {
  if (drag.value?.id === e.pointerId) drag.value.dy = e.clientY - drag.value.start;
}
function target(): number {
  const d = drag.value!;
  return Math.max(0, Math.min(shown().length - 1, d.from + Math.round(d.dy / ROW)));
}
async function up(e: PointerEvent) {
  const d = drag.value;
  if (!d || d.id !== e.pointerId) return;
  const to = target();
  drag.value = null;
  if (to !== d.from) await musicQueueMove(abs(d.from), abs(to)).catch(() => {});
}
/** Enquanto arrasta: a linha vai com o dedo e as do caminho abrem espaço. */
function style(i: number) {
  const d = drag.value;
  if (!d) return {};
  if (i === d.from) return { transform: `translateY(${d.dy}px)`, zIndex: 2, background: "#211c30" };
  const to = target();
  if (d.from < to && i > d.from && i <= to) return { transform: `translateY(-${ROW}px)` };
  if (d.from > to && i < d.from && i >= to) return { transform: `translateY(${ROW}px)` };
  return {};
}
</script>

<template>
  <ul class="queue">
    <li v-for="(t, i) in shown()" :key="`${abs(i)}-${t.path}`" :style="style(i)" :class="{ dragging: drag?.from === i }" @click="musicJump(abs(i)).catch(() => {})">
      <span class="thumb">
        <img v-if="coverUrl(t)" :src="coverUrl(t)!" alt="" loading="lazy" draggable="false" />
        <Icon v-else name="music" :size="18" />
      </span>
      <span class="names"><strong>{{ t.title }}</strong><small>{{ artistOf(t) }} · {{ clock(t.duration) }}</small></span>
      <button class="x" aria-label="Tirar da fila" @click.stop="musicQueueRemove(abs(i)).catch(() => {})"><Icon name="close" :size="16" /></button>
      <span
        class="handle"
        aria-label="Arrastar"
        @click.stop
        @pointerdown="down(i, $event)"
        @pointermove="move"
        @pointerup="up"
        @pointercancel="up"
      >
        <Icon name="menu" :size="22" />
      </span>
    </li>
    <li v-if="!music.next.length" class="empty">Nada depois desta. Use o ⋯ de uma música pra pôr na fila.</li>
  </ul>
</template>

<style scoped>
.queue {
  margin: 0;
  padding: 0;
  list-style: none;
}
.queue li {
  position: relative;
  height: 72px;
  box-sizing: border-box;
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 0 4px;
  border-radius: 12px;
  cursor: pointer;
  transition: transform 0.15s;
}
.queue li.dragging {
  transition: none;
  box-shadow: 0 8px 24px rgba(0, 0, 0, 0.5);
}
.thumb {
  flex: none;
  width: 58px;
  height: 58px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 8px;
  background: #1c1828;
  color: var(--lumna-muted);
}
.thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.names {
  flex: 1;
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
  font-size: 15px;
}
.names small {
  color: var(--lumna-violet-soft);
  opacity: 0.85;
  font-size: 13px;
}
.x {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
  opacity: 0;
}
.queue li:hover .x {
  opacity: 1;
}
@media (hover: none) {
  .x {
    display: none;
  }
}
.handle {
  width: 40px;
  height: 56px;
  display: grid;
  place-items: center;
  color: var(--lumna-muted);
  touch-action: none;
  cursor: grab;
}
.empty {
  height: auto !important;
  padding: 14px 4px !important;
  color: var(--lumna-muted);
  font-size: 14px;
  cursor: default !important;
}
</style>
