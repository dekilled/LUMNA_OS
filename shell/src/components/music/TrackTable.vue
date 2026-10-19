<script setup lang="ts">
// As músicas de uma lista: no PC uma tabela (#, título, artista, álbum, tempo, ⋯) como no esboço;
// no celular linhas com a capa. Tocar numa música toca a lista a partir dela.
import Icon from "../Icon.vue";
import type { Track } from "../../system";
import { albumOf, artistOf, clock, coverUrl } from "./music";
import { openMenu, play } from "./store";

const props = defineProps<{
  tracks: Track[];
  narrow: boolean;
  /** caminho da música tocando */
  current?: string | null;
  playing?: boolean;
  /** número da faixa no álbum em vez da posição */
  albumNumbers?: boolean;
  /** a playlist desta lista (o menu ganha "Tirar desta playlist") */
  playlist?: string;
  /** sem a coluna do álbum (na página do álbum) */
  hideAlbum?: boolean;
}>();

function number(t: Track, i: number) {
  return props.albumNumbers && t.track ? t.track : i + 1;
}
</script>

<template>
  <div class="tracks" :class="{ narrow, noalbum: hideAlbum }">
    <div v-if="!narrow" class="head">
      <span class="num">#</span>
      <span>Título</span>
      <span>Artista</span>
      <span v-if="!hideAlbum">Álbum</span>
      <span class="dur"><Icon name="clock" :size="17" /></span>
      <span />
    </div>
    <div
      v-for="(t, i) in tracks"
      :key="t.path"
      class="row"
      :class="{ now: t.path === current }"
      @click="play(tracks, i)"
      @contextmenu.prevent="openMenu(t, $event, playlist)"
    >
      <span v-if="!narrow" class="num">
        <span v-if="t.path === current" class="eq" :class="{ on: playing }"><i /><i /><i /></span>
        <template v-else>{{ number(t, i) }}</template>
      </span>
      <span class="title">
        <span class="thumb">
          <img v-if="coverUrl(t)" :src="coverUrl(t)!" alt="" loading="lazy" draggable="false" />
          <Icon v-else name="music" :size="18" />
          <span v-if="narrow && t.path === current" class="eq over" :class="{ on: playing }"><i /><i /><i /></span>
        </span>
        <span class="names">
          <strong>{{ t.title }}</strong>
          <small v-if="narrow">{{ artistOf(t) }}{{ hideAlbum ? "" : ` · ${albumOf(t)}` }}</small>
        </span>
      </span>
      <span v-if="!narrow" class="cell">{{ t.artist || artistOf(t) }}</span>
      <span v-if="!narrow && !hideAlbum" class="cell">{{ albumOf(t) }}</span>
      <span class="dur">{{ clock(t.duration) }}</span>
      <button class="more" aria-label="Mais opções" @click.stop="openMenu(t, $event, playlist)"><Icon name="more" :size="20" /></button>
    </div>
  </div>
</template>

<style scoped>
.tracks {
  --cols: 44px minmax(0, 2.4fr) minmax(0, 1.3fr) minmax(0, 1.3fr) 56px 44px;
}
.tracks.noalbum {
  --cols: 44px minmax(0, 2.6fr) minmax(0, 1.4fr) 56px 44px;
}
.head,
.row {
  display: grid;
  grid-template-columns: var(--cols);
  align-items: center;
  gap: 12px;
  padding: 0 12px;
}
.head {
  height: 38px;
  color: var(--lumna-muted);
  font-size: 13px;
}
.row {
  min-height: 54px;
  border-top: 1px solid rgba(167, 139, 250, 0.08);
  border-radius: 12px;
  font-size: 14px;
  cursor: pointer;
}
.row:hover {
  background: rgba(167, 139, 250, 0.06);
}
.row.now {
  border-color: transparent;
  background: rgba(167, 139, 250, 0.12);
}
.row.now strong {
  color: #f3efff;
}
.num {
  color: var(--lumna-muted);
  text-align: center;
  font-variant-numeric: tabular-nums;
}
.title {
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 14px;
}
.thumb {
  position: relative;
  flex: none;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 6px;
  background: #1c1828;
  color: var(--lumna-muted);
}
.thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.names {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.names strong,
.names small,
.cell {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.names strong {
  font-weight: 500;
  font-size: 15px;
}
.names small {
  color: var(--lumna-muted);
  font-size: 13px;
}
.cell {
  color: #d9d3f0;
}
.dur {
  color: var(--lumna-muted);
  font-variant-numeric: tabular-nums;
  text-align: right;
}
.head .dur {
  display: flex;
  justify-content: flex-end;
}
.more {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.more:hover {
  color: var(--lumna-text);
}
/* o "equalizador" da que está tocando */
.eq {
  display: inline-flex;
  align-items: flex-end;
  gap: 2px;
  height: 16px;
}
.eq i {
  width: 3px;
  height: 40%;
  border-radius: 1px;
  background: var(--lumna-violet-soft);
}
.eq i:nth-child(2) {
  height: 90%;
}
.eq i:nth-child(3) {
  height: 60%;
}
.eq.on i {
  animation: eq 0.9s ease-in-out infinite alternate;
}
.eq.on i:nth-child(2) {
  animation-delay: -0.3s;
}
.eq.on i:nth-child(3) {
  animation-delay: -0.6s;
}
@keyframes eq {
  from {
    height: 25%;
  }
  to {
    height: 100%;
  }
}
.eq.over {
  position: absolute;
  inset: 0;
  justify-content: center;
  align-items: center;
  height: auto;
  background: rgba(8, 6, 15, 0.55);
}
.eq.over i {
  max-height: 18px;
}
/* celular: capa, título e artista; o tempo e o ⋯ */
.narrow .row {
  grid-template-columns: minmax(0, 1fr) auto 40px;
  gap: 8px;
  padding: 4px 4px 4px 8px;
  border-top: 0;
}
.narrow .thumb {
  width: 48px;
  height: 48px;
  border-radius: 10px;
}
.narrow .dur {
  font-size: 13px;
}
</style>
