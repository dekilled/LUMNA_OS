<script setup lang="ts">
// LUMNA Music (offline): as músicas da pasta Músicas, por música, álbum e artista, com busca.
// Quem toca é o shell (a música continua com o app fechado); aqui é a biblioteca e o player:
// a barrinha embaixo com o que está tocando e, tocando nela, o player grande com a fila.
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { getCurrentWindow } from "@tauri-apps/api/window";
import Icon from "../Icon.vue";
import {
  haptic,
  musicControl,
  musicJump,
  musicLibrary,
  musicMode,
  musicPlay,
  musicSeek,
  type MusicAction,
  type Track,
} from "../../system";
import { albumOf, artistOf, clock, coverUrl, useMusic } from "./music";

type Tab = "musicas" | "albuns" | "artistas";
interface Album {
  key: string;
  name: string;
  artist: string;
  year: number;
  cover: Track;
  tracks: Track[];
}
interface Artist {
  name: string;
  albums: Album[];
  tracks: Track[];
}

const tracks = ref<Track[]>([]);
const loading = ref(true);
const tab = ref<Tab>("musicas");
const query = ref("");
const searching = ref(false);
const narrow = ref(window.innerWidth < 640);
/** Um álbum ou artista aberto (por cima da lista). */
const openAlbum = ref<Album | null>(null);
const openArtist = ref<Artist | null>(null);
const playerOpen = ref(false);
const error = ref("");
const music = useMusic();
const current = computed(() => music.value?.current ?? null);

const norm = (t: string) => t.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
const filtered = computed(() => {
  const q = norm(query.value.trim());
  if (!q) return tracks.value;
  return tracks.value.filter((t) => norm(`${t.title} ${t.artist} ${t.album_artist} ${t.album}`).includes(q));
});
const songs = computed(() => [...filtered.value].sort((a, b) => a.title.localeCompare(b.title, "pt-BR")));

const albums = computed<Album[]>(() => {
  const map = new Map<string, Album>();
  for (const t of filtered.value) {
    const key = `${artistOf(t)}\u0000${albumOf(t)}`;
    let a = map.get(key);
    if (!a) {
      a = { key, name: albumOf(t), artist: artistOf(t), year: t.year, cover: t, tracks: [] };
      map.set(key, a);
    }
    a.tracks.push(t);
    if (!a.cover.cover && t.cover) a.cover = t;
    if (!a.year && t.year) a.year = t.year;
  }
  return [...map.values()].sort((a, b) => a.name.localeCompare(b.name, "pt-BR"));
});
const artists = computed<Artist[]>(() => {
  const map = new Map<string, Artist>();
  for (const a of albums.value) {
    let artist = map.get(a.artist);
    if (!artist) {
      artist = { name: a.artist, albums: [], tracks: [] };
      map.set(a.artist, artist);
    }
    artist.albums.push(a);
    artist.tracks.push(...a.tracks);
  }
  return [...map.values()].sort((a, b) => a.name.localeCompare(b.name, "pt-BR"));
});

async function load() {
  loading.value = true;
  tracks.value = await musicLibrary().catch(() => []);
  loading.value = false;
}

async function play(list: Track[], index = 0, shuffle = false) {
  error.value = "";
  haptic(10).catch(() => {});
  try {
    await musicMode({ shuffle });
    await musicPlay(list, shuffle ? Math.floor(Math.random() * list.length) : index);
  } catch (e) {
    error.value = String(e);
  }
}
function act(action: MusicAction) {
  haptic(10).catch(() => {});
  musicControl(action).catch((e) => (error.value = String(e)));
}
function toggleShuffle() {
  if (music.value) musicMode({ shuffle: !music.value.shuffle }).catch(() => {});
}
function cycleRepeat() {
  const r = music.value?.repeat ?? "off";
  musicMode({ repeat: r === "off" ? "all" : r === "all" ? "one" : "off" }).catch(() => {});
}
let seekTimer = 0;
function seek(value: number) {
  if (!music.value) return;
  music.value.position = value;
  clearTimeout(seekTimer);
  seekTimer = window.setTimeout(() => musicSeek(value).catch(() => {}), 120);
}
function jump(offset: number) {
  const s = music.value;
  if (s?.index != null) musicJump(s.index + 1 + offset).catch(() => {});
}

const isCurrent = (t: Track) => current.value?.path === t.path;
const albumLength = (a: Album) => {
  const minutes = Math.round(a.tracks.reduce((n, t) => n + t.duration, 0) / 60);
  return `${a.tracks.length} ${a.tracks.length === 1 ? "música" : "músicas"} · ${minutes} min`;
};

function back() {
  if (openAlbum.value) openAlbum.value = null;
  else if (openArtist.value) openArtist.value = null;
}
function showAlbum(a: Album) {
  openAlbum.value = a;
}
function showArtist(a: Artist) {
  openArtist.value = a;
  openAlbum.value = null;
}
function setTab(t: Tab) {
  tab.value = t;
  openAlbum.value = null;
  openArtist.value = null;
}

// player grande: arrastar pra baixo fecha
let dragY: number | null = null;
const drag = ref(0);
function sheetDown(e: PointerEvent) {
  dragY = e.clientY;
}
function sheetMove(e: PointerEvent) {
  if (dragY != null) drag.value = Math.max(0, e.clientY - dragY);
}
function sheetUp() {
  if (dragY == null) return;
  dragY = null;
  if (drag.value > 110) playerOpen.value = false;
  drag.value = 0;
}

// buscar volta pras listas (a página de um álbum ou artista não filtra)
watch(query, (q) => {
  if (q.trim()) {
    openAlbum.value = null;
    openArtist.value = null;
  }
});

function onKey(e: KeyboardEvent) {
  if ((e.target as HTMLElement).tagName === "INPUT") return;
  if (e.key === " ") {
    e.preventDefault();
    act("toggle");
  } else if (e.key === "Escape") {
    if (playerOpen.value) playerOpen.value = false;
    else back();
  }
}

const stops: (() => void)[] = [];
const onResize = () => (narrow.value = window.innerWidth < 640);
onMounted(async () => {
  window.addEventListener("keydown", onKey);
  window.addEventListener("resize", onResize);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(() => window.removeEventListener("resize", onResize));
  await load();
  // voltou pro app: pode ter música nova (copiada pelo Connect)
  stops.push(await getCurrentWindow().onFocusChanged(({ payload }) => payload && !loading.value && load()));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="music" :class="{ narrow }">
    <header class="top">
      <button v-if="openAlbum || openArtist" class="icon" aria-label="Voltar" @click="back()"><Icon name="back" :size="24" /></button>
      <h1 v-if="!searching">{{ openAlbum ? openAlbum.name : openArtist ? openArtist.name : "Música" }}</h1>
      <input v-else v-model="query" class="search" placeholder="Música, artista ou álbum" autofocus />
      <button class="icon" :class="{ on: searching }" :aria-label="searching ? 'Fechar a busca' : 'Buscar'" @click="searching = !searching; query = ''">
        <Icon :name="searching ? 'close' : 'search'" :size="22" />
      </button>
    </header>
    <nav v-if="!openAlbum && !openArtist" class="tabs">
      <button :class="{ on: tab === 'musicas' }" @click="setTab('musicas')">Músicas <small>{{ songs.length }}</small></button>
      <button :class="{ on: tab === 'albuns' }" @click="setTab('albuns')">Álbuns <small>{{ albums.length }}</small></button>
      <button :class="{ on: tab === 'artistas' }" @click="setTab('artistas')">Artistas <small>{{ artists.length }}</small></button>
    </nav>

    <main class="scroll">
      <p v-if="loading" class="empty">Procurando músicas…</p>
      <div v-else-if="!tracks.length" class="empty">
        <Icon name="music" :size="44" />
        <p>Nenhuma música ainda. Coloque os arquivos (MP3, FLAC, OGG, M4A…) na pasta <b>Músicas</b>, por exemplo mandando do PC pelo Connect.</p>
      </div>

      <!-- um álbum -->
      <section v-else-if="openAlbum" class="album-page">
        <div class="album-head">
          <div class="cover big">
            <img v-if="coverUrl(openAlbum.cover)" :src="coverUrl(openAlbum.cover)!" alt="" draggable="false" />
            <Icon v-else name="disc" :size="56" />
          </div>
          <div class="album-info">
            <strong>{{ openAlbum.name }}</strong>
            <button class="link" @click="showArtist(artists.find((a) => a.name === openAlbum!.artist)!)">{{ openAlbum.artist }}</button>
            <small>{{ openAlbum.year ? `${openAlbum.year} · ` : "" }}{{ albumLength(openAlbum) }}</small>
          </div>
        </div>
        <div class="play-row">
          <button class="primary" @click="play(openAlbum.tracks)"><Icon name="media-play" :size="18" /> Tocar</button>
          <button class="ghost" @click="play(openAlbum.tracks, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
        </div>
        <ul class="list">
          <li v-for="(t, i) in openAlbum.tracks" :key="t.path" :class="{ now: isCurrent(t) }" @click="play(openAlbum.tracks, i)">
            <span class="num">{{ isCurrent(t) && music?.playing ? "♪" : t.track || i + 1 }}</span>
            <span class="txt"><strong>{{ t.title }}</strong><small v-if="t.artist && t.artist !== openAlbum.artist">{{ t.artist }}</small></span>
            <span class="dur">{{ clock(t.duration) }}</span>
          </li>
        </ul>
      </section>

      <!-- um artista -->
      <section v-else-if="openArtist" class="artist-page">
        <div class="play-row">
          <button class="primary" @click="play(openArtist.tracks)"><Icon name="media-play" :size="18" /> Tocar</button>
          <button class="ghost" @click="play(openArtist.tracks, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
        </div>
        <h2>Álbuns</h2>
        <div class="grid">
          <button v-for="a in openArtist.albums" :key="a.key" class="album" @click="showAlbum(a)">
            <div class="cover">
              <img v-if="coverUrl(a.cover)" :src="coverUrl(a.cover)!" alt="" loading="lazy" draggable="false" />
              <Icon v-else name="disc" :size="40" />
            </div>
            <strong>{{ a.name }}</strong>
            <small>{{ a.year || albumLength(a) }}</small>
          </button>
        </div>
      </section>

      <!-- músicas -->
      <template v-else-if="tab === 'musicas'">
        <div class="play-row">
          <button class="primary" :disabled="!songs.length" @click="play(songs, 0, true)"><Icon name="shuffle" :size="18" /> Tocar aleatório</button>
        </div>
        <ul class="list">
          <li v-for="(t, i) in songs" :key="t.path" :class="{ now: isCurrent(t) }" @click="play(songs, i)">
            <span class="thumb">
              <img v-if="coverUrl(t)" :src="coverUrl(t)!" alt="" loading="lazy" draggable="false" />
              <Icon v-else name="music" :size="20" />
            </span>
            <span class="txt"><strong>{{ t.title }}</strong><small>{{ artistOf(t) }} · {{ albumOf(t) }}</small></span>
            <span class="dur">{{ clock(t.duration) }}</span>
          </li>
        </ul>
        <p v-if="query && !songs.length" class="empty">Nada com “{{ query }}”.</p>
      </template>

      <!-- álbuns -->
      <div v-else-if="tab === 'albuns'" class="grid">
        <button v-for="a in albums" :key="a.key" class="album" @click="showAlbum(a)">
          <div class="cover">
            <img v-if="coverUrl(a.cover)" :src="coverUrl(a.cover)!" alt="" loading="lazy" draggable="false" />
            <Icon v-else name="disc" :size="40" />
          </div>
          <strong>{{ a.name }}</strong>
          <small>{{ a.artist }}</small>
        </button>
      </div>

      <!-- artistas -->
      <ul v-else class="list">
        <li v-for="a in artists" :key="a.name" @click="showArtist(a)">
          <span class="thumb round">
            <img v-if="coverUrl(a.albums[0].cover)" :src="coverUrl(a.albums[0].cover)!" alt="" loading="lazy" draggable="false" />
            <Icon v-else name="user" :size="20" />
          </span>
          <span class="txt">
            <strong>{{ a.name }}</strong>
            <small>{{ a.albums.length }} {{ a.albums.length === 1 ? "álbum" : "álbuns" }} · {{ a.tracks.length }} {{ a.tracks.length === 1 ? "música" : "músicas" }}</small>
          </span>
          <Icon class="chev" name="forward" :size="18" />
        </li>
      </ul>
      <p v-if="error" class="error" @click="error = ''">{{ error }}</p>
    </main>

    <!-- tocando agora (a barrinha) -->
    <footer v-if="current && music" class="now-bar" @click="playerOpen = true">
      <div class="progress"><span :style="{ width: `${music.duration ? (music.position / music.duration) * 100 : 0}%` }" /></div>
      <span class="thumb">
        <img v-if="coverUrl(current)" :src="coverUrl(current)!" alt="" draggable="false" />
        <Icon v-else name="music" :size="20" />
      </span>
      <span class="txt"><strong>{{ current.title }}</strong><small>{{ artistOf(current) }}</small></span>
      <button class="icon" :aria-label="music.playing ? 'Pausar' : 'Tocar'" @click.stop="act('toggle')">
        <Icon :name="music.playing ? 'pause' : 'media-play'" :size="24" />
      </button>
      <button class="icon" aria-label="Próxima" @click.stop="act('next')"><Icon name="skip-forward" :size="22" /></button>
    </footer>

    <!-- o player grande -->
    <div v-if="playerOpen && current && music" class="player" :style="{ transform: `translateY(${drag}px)` }">
      <div class="grab" @pointerdown="sheetDown" @pointermove="sheetMove" @pointerup="sheetUp" @pointercancel="sheetUp">
        <button class="icon" aria-label="Fechar" @click="playerOpen = false"><Icon name="chevron-down" :size="26" /></button>
        <span>Tocando agora</span>
        <span class="icon" />
      </div>
      <div class="player-body">
        <div class="cover huge">
          <img v-if="coverUrl(current)" :src="coverUrl(current)!" alt="" draggable="false" />
          <Icon v-else name="music" :size="80" />
        </div>
        <div class="player-side">
          <div class="titles">
            <strong>{{ current.title }}</strong>
            <small>{{ artistOf(current) }} · {{ albumOf(current) }}</small>
          </div>
          <input
            class="seek"
            type="range"
            min="0"
            :max="Math.max(1, music.duration || current.duration)"
            step="0.5"
            :value="music.position"
            @input="seek(+($event.target as HTMLInputElement).value)"
          />
          <div class="times"><span>{{ clock(music.position) }}</span><span>{{ clock(music.duration || current.duration) }}</span></div>
          <div class="controls">
            <button class="icon" :class="{ on: music.shuffle }" aria-label="Aleatório" @click="toggleShuffle()"><Icon name="shuffle" :size="22" /></button>
            <button class="icon big" aria-label="Anterior" @click="act('previous')"><Icon name="skip-back" :size="30" /></button>
            <button class="play" :aria-label="music.playing ? 'Pausar' : 'Tocar'" @click="act('toggle')">
              <Icon :name="music.playing ? 'pause' : 'media-play'" :size="34" />
            </button>
            <button class="icon big" aria-label="Próxima" @click="act('next')"><Icon name="skip-forward" :size="30" /></button>
            <button
              class="icon"
              :class="{ on: music.repeat !== 'off' }"
              :aria-label="music.repeat === 'off' ? 'Repetir a fila' : music.repeat === 'all' ? 'Repetir esta' : 'Não repetir'"
              @click="cycleRepeat()"
            >
              <Icon :name="music.repeat === 'one' ? 'repeat-one' : 'repeat'" :size="22" />
            </button>
          </div>
          <div v-if="music.next.length" class="queue">
            <h2>A seguir</h2>
            <ul class="list">
              <li v-for="(t, i) in music.next.slice(0, 30)" :key="`${i}-${t.path}`" @click="jump(i)">
                <span class="thumb">
                  <img v-if="coverUrl(t)" :src="coverUrl(t)!" alt="" loading="lazy" draggable="false" />
                  <Icon v-else name="music" :size="18" />
                </span>
                <span class="txt"><strong>{{ t.title }}</strong><small>{{ artistOf(t) }}</small></span>
                <span class="dur">{{ clock(t.duration) }}</span>
              </li>
            </ul>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.music {
  position: relative;
  display: flex;
  flex-direction: column;
  height: var(--app-h, 100vh);
  background: #0d0b14;
  color: var(--lumna-text);
  overflow: hidden;
}
.top {
  flex: none;
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 56px;
  padding: 12px 12px 4px 20px;
}
.top:has(.icon:first-child) {
  padding-left: 8px;
}
.top h1 {
  flex: 1;
  min-width: 0;
  margin: 0;
  overflow: hidden;
  font-size: 24px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.search {
  flex: 1;
  min-width: 0;
  height: 42px;
  padding: 0 14px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 999px;
  background: #16131f;
  color: inherit;
  font: inherit;
  font-size: 15px;
  outline: none;
}
.icon {
  flex: none;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-text);
}
.icon.on {
  color: var(--lumna-violet-soft);
}
.tabs {
  flex: none;
  display: flex;
  gap: 8px;
  padding: 6px 20px 10px;
  overflow-x: auto;
  scrollbar-width: none;
}
.tabs button {
  flex: none;
  padding: 8px 14px;
  border: 1px solid rgba(167, 139, 250, 0.18);
  border-radius: 999px;
  background: none;
  color: var(--lumna-text);
  font-size: 14px;
}
.tabs small {
  margin-left: 4px;
  color: var(--lumna-muted);
}
.tabs button.on {
  border-color: transparent;
  background: var(--lumna-violet);
}
.tabs button.on small {
  color: rgba(255, 255, 255, 0.75);
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 0 12px 16px;
}
.empty {
  max-width: 420px;
  margin: 40px auto;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  color: var(--lumna-muted);
  text-align: center;
  line-height: 1.6;
}
.play-row {
  display: flex;
  gap: 10px;
  padding: 4px 8px 12px;
}
.primary,
.ghost {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 42px;
  padding: 0 18px;
  border-radius: 999px;
  font-size: 15px;
}
.primary {
  border: 0;
  background: linear-gradient(135deg, #a855f7, #6d28d9);
  color: #fff;
  font-weight: 600;
}
.primary:disabled {
  opacity: 0.5;
}
.ghost {
  border: 1px solid rgba(167, 139, 250, 0.3);
  background: none;
  color: var(--lumna-violet-soft);
}
.list {
  margin: 0;
  padding: 0;
  list-style: none;
}
.list li {
  display: flex;
  align-items: center;
  gap: 12px;
  min-height: 60px;
  padding: 6px 8px;
  border-radius: 14px;
  cursor: pointer;
}
.list li:active {
  background: rgba(167, 139, 250, 0.08);
}
.list li.now strong,
.list li.now .num {
  color: var(--lumna-violet-soft);
}
.thumb {
  flex: none;
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 10px;
  background: #1c1828;
  color: var(--lumna-muted);
}
.thumb.round {
  border-radius: 50%;
}
.thumb img,
.cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.num {
  flex: none;
  width: 28px;
  color: var(--lumna-muted);
  font-variant-numeric: tabular-nums;
  text-align: center;
}
.txt {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.txt strong,
.txt small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.txt strong {
  font-size: 15px;
  font-weight: 500;
}
.txt small {
  color: var(--lumna-muted);
  font-size: 13px;
}
.dur {
  flex: none;
  color: var(--lumna-muted);
  font-size: 13px;
  font-variant-numeric: tabular-nums;
}
.chev {
  color: var(--lumna-muted);
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(150px, 1fr));
  gap: 16px 12px;
  padding: 4px 8px;
}
.narrow .grid {
  grid-template-columns: repeat(2, 1fr);
}
.album {
  display: flex;
  flex-direction: column;
  gap: 3px;
  min-width: 0;
  padding: 0;
  border: 0;
  background: none;
  color: inherit;
  text-align: left;
}
.album strong,
.album small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.album strong {
  margin-top: 6px;
  font-size: 14px;
  font-weight: 500;
}
.album small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.cover {
  aspect-ratio: 1;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 14px;
  background: linear-gradient(135deg, #2a1f45, #16131f);
  color: rgba(167, 139, 250, 0.6);
}
.album-head {
  display: flex;
  gap: 16px;
  align-items: flex-end;
  padding: 6px 8px 14px;
}
.cover.big {
  flex: none;
  width: 42%;
  max-width: 220px;
}
.album-info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.album-info strong {
  font-size: 20px;
}
.album-info small {
  color: var(--lumna-muted);
}
.link {
  align-self: flex-start;
  padding: 0;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 15px;
}
h2 {
  margin: 12px 8px 8px;
  font-size: 16px;
}
.error {
  margin: 10px 8px;
  color: #fca5a5;
}
.now-bar {
  position: relative;
  flex: none;
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0 8px 8px;
  padding: 8px 6px 8px 8px;
  border-radius: 18px;
  background: #1c1828;
  cursor: pointer;
  overflow: hidden;
}
.now-bar .progress {
  position: absolute;
  inset: auto 0 0;
  height: 3px;
  background: rgba(255, 255, 255, 0.08);
}
.now-bar .progress span {
  display: block;
  height: 100%;
  background: var(--lumna-violet-soft);
}
.now-bar .thumb {
  width: 44px;
  height: 44px;
}
.player {
  position: absolute;
  inset: 0;
  z-index: 5;
  display: flex;
  flex-direction: column;
  background: radial-gradient(120% 70% at 50% 0%, #2b1850, #0d0b14 70%);
  transition: transform 0.12s;
}
.grab {
  flex: none;
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 8px 0;
  color: var(--lumna-muted);
  font-size: 13px;
  touch-action: none;
}
.player-body {
  flex: 1;
  min-height: 0;
  display: flex;
  gap: 28px;
  padding: 10px 24px 20px;
  overflow-y: auto;
}
.narrow .player-body {
  flex-direction: column;
  gap: 18px;
}
.cover.huge {
  flex: none;
  width: min(42vh, 380px);
  align-self: center;
  border-radius: 22px;
  box-shadow: 0 18px 50px rgba(0, 0, 0, 0.5);
}
.narrow .cover.huge {
  width: min(100%, 46vh);
}
.player-side {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  justify-content: center;
}
.titles {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.titles strong {
  font-size: 22px;
}
.titles small {
  color: var(--lumna-muted);
  font-size: 15px;
}
.seek {
  width: 100%;
  height: 30px;
  margin-top: 14px;
  accent-color: var(--lumna-violet-soft);
}
.times {
  display: flex;
  justify-content: space-between;
  color: var(--lumna-muted);
  font-size: 12px;
  font-variant-numeric: tabular-nums;
}
.controls {
  display: flex;
  align-items: center;
  justify-content: space-between;
  max-width: 400px;
  margin: 10px auto 0;
  width: 100%;
}
.icon.big {
  width: 56px;
  height: 56px;
}
.play {
  width: 74px;
  height: 74px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: #f3efff;
  color: #1a1030;
}
.queue {
  margin-top: 18px;
}
.queue h2 {
  margin-left: 4px;
}
</style>
