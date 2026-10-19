<script setup lang="ts">
// O ⋯ de uma música: tocar a seguir, pôr na fila, favoritar, pôr numa playlist (ou criar uma),
// ir pro álbum/artista e ver os detalhes. No PC abre onde clicou; no celular é uma folha embaixo.
import { computed, ref, watch } from "vue";
import Icon from "../Icon.vue";
import { artistOf, coverUrl, isQobuz } from "./music";
import {
  addToPlaylist,
  addToQueue,
  albumOfTrack,
  favorites,
  go,
  lists,
  menu,
  panel,
  playNext,
  removeFromPlaylist,
  savePlaylist,
  toggleFavorite,
} from "./store";

const props = defineProps<{ narrow: boolean }>();
const emit = defineEmits<{ details: [] }>();
const sub = ref<"" | "playlists" | "nova">("");
const newName = ref("");

watch(menu, () => {
  sub.value = "";
  newName.value = "";
});

const pos = computed(() => {
  const m = menu.value;
  if (!m || props.narrow) return {};
  const w = 260;
  const h = sub.value ? 360 : 330;
  return {
    left: `${Math.min(m.x, window.innerWidth - w - 8)}px`,
    top: `${Math.max(8, Math.min(m.y, window.innerHeight - h - 8))}px`,
  };
});

function close() {
  menu.value = null;
}
function act(f: () => unknown) {
  f();
  close();
}
async function createPlaylist() {
  const m = menu.value;
  const name = newName.value.trim();
  if (!m || !name) return;
  const before = new Set(lists.value.playlists.map((p) => p.id));
  await savePlaylist(name);
  const made = lists.value.playlists.find((p) => !before.has(p.id));
  if (made) await addToPlaylist(made.id, [m.track]);
  close();
}
function showDetails() {
  panel.value = "detalhes";
  emit("details");
  close();
}
</script>

<template>
  <div v-if="menu" class="veil" :class="{ narrow }" @click.self="close()" @contextmenu.prevent="close()">
    <div class="menu" :style="pos">
      <div v-if="narrow" class="head">
        <span class="thumb">
          <img v-if="coverUrl(menu.track)" :src="coverUrl(menu.track)!" alt="" />
          <Icon v-else name="music" :size="20" />
        </span>
        <span class="names"><strong>{{ menu.track.title }}</strong><small>{{ artistOf(menu.track) }}</small></span>
      </div>

      <template v-if="sub === ''">
        <button @click="act(() => playNext([menu!.track]))"><Icon name="skip-forward" :size="19" /> Tocar a seguir</button>
        <button @click="act(() => addToQueue([menu!.track]))"><Icon name="list" :size="19" /> Adicionar à fila</button>
        <template v-if="!isQobuz(menu.track)">
        <button @click="act(() => toggleFavorite(menu!.track))">
          <Icon name="heart" :size="19" /> {{ favorites.has(menu.track.path) ? "Tirar das favoritas" : "Favoritar" }}
        </button>
        <button @click="sub = 'playlists'"><Icon name="plus-circle" :size="19" /> Adicionar à playlist <Icon class="chev" name="forward" :size="16" /></button>
        <button v-if="menu.playlist" @click="act(() => removeFromPlaylist(menu!.playlist!, [menu!.track]))">
          <Icon name="minus" :size="19" /> Tirar desta playlist
        </button>
        <div class="sep" />
        <button
          @click="act(() => { const a = albumOfTrack(menu!.track); if (a) go({ kind: 'album', key: a.key }); })"
        >
          <Icon name="disc" :size="19" /> Ir para o álbum
        </button>
        <button @click="act(() => go({ kind: 'artista', name: artistOf(menu!.track) }))"><Icon name="user" :size="19" /> Ir para o artista</button>
        <button @click="showDetails()"><Icon name="info" :size="19" /> Detalhes</button>
        </template>
        <p v-else class="note">Do Qobuz: favoritar, playlists e detalhes chegam junto com o tocar.</p>
      </template>

      <template v-else-if="sub === 'playlists'">
        <button class="back" @click="sub = ''"><Icon name="back" :size="18" /> Adicionar à playlist</button>
        <button v-for="p in lists.playlists" :key="p.id" @click="act(() => addToPlaylist(p.id, [menu!.track]))">
          <Icon :name="p.icon" :size="19" /> {{ p.name }}
          <Icon v-if="p.tracks.includes(menu.track.path)" class="chev" name="check" :size="16" />
        </button>
        <button @click="sub = 'nova'"><Icon name="plus" :size="19" /> Nova playlist…</button>
      </template>

      <form v-else class="new" @submit.prevent="createPlaylist()">
        <button type="button" class="back" @click="sub = 'playlists'"><Icon name="back" :size="18" /> Nova playlist</button>
        <input v-model="newName" placeholder="Nome (ex.: Viagens)" maxlength="60" autofocus />
        <button class="primary" :disabled="!newName.trim()">Criar e adicionar</button>
      </form>
    </div>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
  z-index: 40;
}
.veil.narrow {
  display: flex;
  align-items: flex-end;
  background: rgba(4, 3, 10, 0.55);
}
.menu {
  position: absolute;
  width: 260px;
  padding: 6px;
  display: flex;
  flex-direction: column;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 14px;
  background: #17141f;
  box-shadow: 0 14px 40px rgba(0, 0, 0, 0.55);
}
.narrow .menu {
  position: static;
  width: 100%;
  padding: 10px 10px calc(14px + var(--kb-inset, 0px));
  border-radius: 22px 22px 0 0;
  border-width: 1px 0 0;
}
.head {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 6px 8px 12px;
}
.thumb {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 10px;
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
}
.names strong,
.names small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.names small {
  color: var(--lumna-muted);
}
.menu button {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 40px;
  padding: 0 12px;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
  text-align: left;
}
.narrow .menu button {
  min-height: 50px;
  font-size: 15px;
}
.menu button:hover {
  background: rgba(167, 139, 250, 0.1);
}
.menu button :deep(svg) {
  flex: none;
  color: var(--lumna-violet-soft);
}
.chev {
  margin-left: auto;
}
.back {
  color: var(--lumna-muted) !important;
}
.note {
  margin: 4px 12px 8px;
  color: var(--lumna-muted);
  font-size: 13px;
  line-height: 1.5;
}
.sep {
  height: 1px;
  margin: 4px 8px;
  background: rgba(167, 139, 250, 0.12);
}
.new {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.new input {
  height: 42px;
  margin: 0 6px;
  padding: 0 12px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 10px;
  background: #1c1828;
  color: inherit;
  font: inherit;
  outline: none;
}
.new .primary {
  justify-content: center;
  margin: 0 6px 4px;
  background: linear-gradient(135deg, #a855f7, #6d28d9);
  color: #fff;
  font-weight: 600;
}
.new .primary:disabled {
  opacity: 0.5;
}
</style>
