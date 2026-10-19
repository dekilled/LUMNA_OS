<script setup lang="ts">
// LUMNA Music (offline), como nos esboços. PC: a barra lateral (Ouvir agora, biblioteca, álbuns,
// artistas, pastas, playlists), o centro com o que está tocando e a biblioteca em tabela, a fila
// à direita com a Emma, e a barra do player embaixo. Celular: abas em cima, as listas, a barrinha
// do que está tocando e, tocando nela, o player grande (Fila, Letras, Detalhes).
// Quem toca é o shell: fechar o app não para a música.
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { getCurrentWindow } from "@tauri-apps/api/window";
import Icon from "../Icon.vue";
import WindowBar from "../WindowBar.vue";
import {
  artUrl,
  filesWindow,
  haptic,
  musicControl,
  musicFolder,
  on,
  profileGet,
  settingsOpen,
  type QobuzStatus,
  type Track,
} from "../../system";
import DetailsView from "./DetailsView.vue";
import EmmaBox from "./EmmaBox.vue";
import LyricsView from "./LyricsView.vue";
import NowPlaying from "./NowPlaying.vue";
import PlayerBar from "./PlayerBar.vue";
import QueueList from "./QueueList.vue";
import TrackMenu from "./TrackMenu.vue";
import TrackTable from "./TrackTable.vue";
import { albumOf, artistOf, coverUrl, useMusic } from "./music";
import {
  albumByKey,
  albums,
  artists,
  back,
  deletePlaylist,
  error,
  favorites,
  favoriteTracks,
  folders,
  go,
  history,
  lists,
  load,
  loading,
  openMenu,
  panel,
  play,
  playlistTracks,
  query,
  recent,
  savePlaylist,
  songs,
  toggleFavorite,
  tracks,
  view,
  qobuz,
  qPlaylists,
  qTracks,
  qLoading,
  loadQobuz,
  loadQobuzTracks,
  connectQobuz,
  disconnectQobuz,
  type Album,
  type View,
} from "./store";

const narrow = ref(window.innerWidth < 760);
const music = useMusic();
const current = computed(() => music.value?.current ?? null);
const playing = computed(() => !!music.value?.playing);
const queueOpen = ref(true);
const playerOpen = ref(false);
const searching = ref(false);
const libTab = ref<"musicas" | "albuns" | "recentes">("musicas");
const photo = ref<string | null>(null);
const folder = ref("");
const settingsSheet = ref(false);
const toast = ref("");

// ---- a tela aberta ----

const album = computed(() => (view.value.kind === "album" ? albumByKey(view.value.key) : null));
const artist = computed(() => (view.value.kind === "artista" ? artists.value.find((a) => a.name === (view.value as { name: string }).name) ?? null : null));
const folderView = computed(() => (view.value.kind === "pasta" ? folders.value.find((f) => f.path === (view.value as { path: string }).path) ?? null : null));
const playlist = computed(() => (view.value.kind === "playlist" ? lists.value.playlists.find((p) => p.id === (view.value as { id: string }).id) ?? null : null));
const listTracks = computed<Track[]>(() =>
  view.value.kind === "favoritas" ? favoriteTracks.value : playlist.value ? playlistTracks(playlist.value.id) : folderView.value?.tracks ?? [],
);

const NAV: { kind: View["kind"]; label: string; icon: string }[] = [
  { kind: "ouvir", label: "Ouvir agora", icon: "home" },
  { kind: "biblioteca", label: "Minha biblioteca", icon: "music" },
  { kind: "albuns", label: "Álbuns", icon: "disc" },
  { kind: "artistas", label: "Artistas", icon: "user" },
  { kind: "pastas", label: "Pastas", icon: "folder" },
];
const MOBILE_NAV: { kind: View["kind"]; label: string }[] = [
  { kind: "ouvir", label: "Ouvir agora" },
  { kind: "biblioteca", label: "Músicas" },
  { kind: "albuns", label: "Álbuns" },
  { kind: "artistas", label: "Artistas" },
  { kind: "playlists", label: "Playlists" },
  { kind: "pastas", label: "Pastas" },
  { kind: "qobuz", label: "Qobuz" },
];
const top = computed(() => history.value[0].kind);
const navTo = (kind: View["kind"]) => go({ kind } as View, true);
const isNav = (kind: View["kind"]) => history.value.length === 1 && top.value === kind;
const isList = (kind: string, id?: string) => view.value.kind === kind && (!id || (view.value as { id?: string }).id === id);

const title = computed(() => {
  const v = view.value;
  switch (v.kind) {
    case "ouvir":
      return "Ouvir agora";
    case "biblioteca":
      return narrow.value ? "Músicas" : "Minha biblioteca";
    case "albuns":
      return "Álbuns";
    case "artistas":
      return "Artistas";
    case "pastas":
      return "Pastas";
    case "playlists":
      return "Playlists";
    case "favoritas":
      return "Favoritas";
    case "playlist":
      return playlist.value?.name ?? "Playlist";
    case "album":
      return album.value?.name ?? "Álbum";
    case "artista":
      return v.name;
    case "pasta":
      return folderView.value?.name ?? "Pasta";
    case "qobuz":
      return "Qobuz";
    case "qplaylist":
      return qPlaylist.value?.name ?? "Playlist do Qobuz";
    case "qfavoritas":
      return "Favoritas do Qobuz";
  }
  return "Música";
});

// ---- Qobuz ----

const qPlaylist = computed(() => (view.value.kind === "qplaylist" ? qPlaylists.value?.find((p) => p.id === (view.value as { id: string }).id) ?? null : null));
/** Uma tela do Qobuz (não depende da pasta Músicas). */
const isQ = computed(() => ["qobuz", "qplaylist", "qfavoritas"].includes(view.value.kind));
const qKey = computed(() => (view.value.kind === "qfavoritas" ? "fav" : view.value.kind === "qplaylist" ? (view.value as { id: string }).id : ""));
const qList = computed<Track[]>(() => (qKey.value ? qTracks.value.get(qKey.value) ?? [] : []));
watch(qKey, (k) => k && loadQobuzTracks(k), { immediate: true });
const hours = (seconds: number) => {
  const m = Math.round(seconds / 60);
  return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60} min` : `${m} min`;
};

const count = (n: number, one: string, many: string) => `${n} ${n === 1 ? one : many}`;
const minutes = (list: Track[]) => {
  const m = Math.round(list.reduce((n, t) => n + t.duration, 0) / 60);
  return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60} min` : `${m} min`;
};

// buscar volta pras listas
watch(query, (q) => {
  if (q.trim() && !["ouvir", "biblioteca", "albuns", "artistas"].includes(view.value.kind)) go({ kind: "biblioteca" }, true);
});

// ---- playlists ----

const ICONS = ["music", "heart", "plane", "disc", "moon", "sun", "zap", "leaf", "star", "headphones"];
const playlistForm = ref<{ id?: string; name: string; icon: string } | null>(null);
const confirmDelete = ref(false);
function newPlaylist() {
  playlistForm.value = { name: "", icon: "music" };
}
function editPlaylist() {
  const p = playlist.value;
  if (p) playlistForm.value = { id: p.id, name: p.name, icon: p.icon };
}
async function submitPlaylist() {
  const f = playlistForm.value;
  if (!f?.name.trim()) return;
  const before = new Set(lists.value.playlists.map((p) => p.id));
  await savePlaylist(f.name, f.id, f.icon);
  playlistForm.value = null;
  const made = lists.value.playlists.find((p) => !before.has(p.id));
  if (made) go({ kind: "playlist", id: made.id });
}
async function removePlaylist() {
  if (!playlist.value) return;
  if (!confirmDelete.value) {
    confirmDelete.value = true;
    return;
  }
  confirmDelete.value = false;
  await deletePlaylist(playlist.value.id);
}
watch(view, () => (confirmDelete.value = false));

// ---- outros ----

function addMusic() {
  filesWindow(folder.value || undefined).catch(() => {});
  flash("Copie as músicas pra esta pasta (ou mande do PC pelo Connect): elas aparecem aqui sozinhas.");
}
function flash(text: string) {
  toast.value = text;
  window.setTimeout(() => toast.value === text && (toast.value = ""), 4200);
}
const emmaContext = computed(
  () => `LUMNA Music • ${tracks.value.length} músicas na biblioteca${current.value ? ` • tocando "${current.value.title}" de ${artistOf(current.value)}` : ""}`,
);
const showAlbum = (a: Album) => go({ kind: "album", key: a.key });

function onKey(e: KeyboardEvent) {
  const tag = (e.target as HTMLElement).tagName;
  if (tag === "INPUT" || tag === "TEXTAREA") return;
  if (e.key === " ") {
    e.preventDefault();
    musicControl("toggle").catch(() => {});
  } else if (e.key === "Escape") {
    if (playerOpen.value) playerOpen.value = false;
    else back();
  } else if (e.key === "f" && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    searching.value = true;
  }
}

const stops: (() => void)[] = [];
const onResize = () => (narrow.value = window.innerWidth < 760);
onMounted(async () => {
  window.addEventListener("keydown", onKey);
  window.addEventListener("resize", onResize);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(() => window.removeEventListener("resize", onResize));
  profileGet()
    .then((p) => (photo.value = p.photo ? artUrl(p.photo) : null))
    .catch(() => {});
  musicFolder()
    .then((f) => (folder.value = f))
    .catch(() => {});
  loadQobuz();
  // o login do Qobuz terminou (na janelinha dele)
  stops.push(
    await on<QobuzStatus>("lumna://qobuz", (st) => {
      qobuz.value = { connected: st.connected, name: st.name };
      if (st.error) flash(`Qobuz: ${st.error}`);
      else if (st.connected) {
        flash(`Qobuz conectado: ${st.name}`);
        loadQobuz();
        go({ kind: "qobuz" }, true);
      }
    }),
  );
  await load();
  // voltou pro app: pode ter música nova (copiada pelo Connect)
  stops.push(await getCurrentWindow().onFocusChanged(({ payload }) => payload && !loading.value && load()));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="music" :class="{ narrow }">
    <WindowBar v-if="!narrow" title="LUMNA Music" class="winbar" />

    <div class="body">
      <!-- PC: a barra lateral -->
      <aside v-if="!narrow" class="side">
        <nav>
          <button v-for="n in NAV" :key="n.kind" :class="{ on: isNav(n.kind) }" @click="navTo(n.kind)">
            <Icon :name="n.icon" :size="22" /> {{ n.label }}
          </button>
        </nav>
        <div class="sep" />
        <div class="lists-head">
          <span>Playlists</span>
          <button aria-label="Nova playlist" @click="newPlaylist()"><Icon name="plus" :size="20" /></button>
        </div>
        <nav class="lists">
          <button :class="{ on: isList('favoritas') }" @click="go({ kind: 'favoritas' }, true)"><Icon name="heart" :size="22" /> Favoritas</button>
          <button v-for="p in lists.playlists" :key="p.id" :class="{ on: isList('playlist', p.id) }" @click="go({ kind: 'playlist', id: p.id }, true)">
            <Icon :name="p.icon" :size="22" /> {{ p.name }}
          </button>
        </nav>
        <div class="sep" />
        <div class="lists-head"><span>Qobuz</span></div>
        <nav class="lists">
          <template v-if="qobuz.connected">
            <button :class="{ on: isNav('qobuz') || view.kind === 'qplaylist' }" @click="navTo('qobuz')"><Icon name="list" :size="22" /> Minhas playlists</button>
            <button :class="{ on: isList('qfavoritas') }" @click="go({ kind: 'qfavoritas' }, true)"><Icon name="heart" :size="22" /> Favoritas</button>
          </template>
          <button v-else @click="connectQobuz()"><Icon name="link" :size="22" /> Conectar Qobuz</button>
        </nav>
        <div class="grow" />
        <nav>
          <button @click="addMusic()"><Icon name="folder-plus" :size="22" /> Adicionar músicas</button>
          <button @click="settingsSheet = true"><Icon name="settings" :size="22" /> Configurações</button>
        </nav>
      </aside>

      <main class="main">
        <!-- busca (e voltar) -->
        <header class="top">
          <button v-if="history.length > 1" class="round" aria-label="Voltar" @click="back()"><Icon name="back" :size="22" /></button>
          <template v-if="narrow && !searching">
            <h1 class="mtitle">{{ history.length > 1 ? title : "Música" }}</h1>
            <button class="round" aria-label="Buscar" @click="searching = true"><Icon name="search" :size="22" /></button>
          </template>
          <label v-else class="search">
            <Icon name="search" :size="20" />
            <input v-model="query" placeholder="Buscar na sua biblioteca" :autofocus="narrow" />
            <button v-if="narrow || query" class="clear" aria-label="Fechar a busca" @click.prevent="query = ''; searching = false"><Icon name="close" :size="18" /></button>
          </label>
          <button class="round avatar" aria-label="Seu perfil" @click="settingsOpen().catch(() => {})">
            <img v-if="photo" :src="photo" alt="" />
            <Icon v-else name="user" :size="22" />
          </button>
        </header>
        <nav v-if="narrow && history.length === 1" class="chips">
          <button v-for="n in MOBILE_NAV" :key="n.kind" :class="{ on: top === n.kind }" @click="navTo(n.kind)">{{ n.label }}</button>
        </nav>

        <div class="scroll">
          <p v-if="loading && !tracks.length && !isQ" class="empty">Procurando músicas…</p>
          <div v-else-if="!tracks.length && !isQ" class="empty">
            <Icon name="music" :size="46" />
            <p>Nenhuma música ainda. Coloque os arquivos (MP3, FLAC, OGG, M4A…) na pasta <b>Músicas</b>, ou mande do PC pelo Connect.</p>
            <button class="pill primary" @click="addMusic()"><Icon name="folder-plus" :size="18" /> Abrir a pasta Músicas</button>
          </div>

          <!-- Ouvir agora -->
          <template v-else-if="view.kind === 'ouvir'">
            <h1 v-if="!narrow" class="h1">Ouvir agora</h1>
            <section class="hero">
              <div class="cover big" @click="narrow && current && (playerOpen = true)">
                <img v-if="coverUrl(current)" :src="coverUrl(current)!" alt="" draggable="false" />
                <Icon v-else name="music" :size="64" />
              </div>
              <div class="hero-info">
                <template v-if="current">
                  <span class="kicker">{{ playing ? "Em reprodução" : "Pausada" }}</span>
                  <strong>{{ current.title }}</strong>
                  <span class="artist">{{ artistOf(current) }}</span>
                  <span class="album">{{ albumOf(current) }}</span>
                  <div class="hero-actions">
                    <button class="circle" :class="{ on: favorites.has(current.path) }" aria-label="Favoritar" @click="toggleFavorite(current)"><Icon name="heart" :size="22" /></button>
                    <button class="circle" aria-label="Mais opções" @click="openMenu(current, $event)"><Icon name="more" :size="22" /></button>
                  </div>
                </template>
                <template v-else>
                  <span class="kicker">Nada tocando</span>
                  <strong>Sua música, do seu jeito</strong>
                  <span class="album">{{ count(tracks.length, "música", "músicas") }} · {{ count(albums.length, "álbum", "álbuns") }}</span>
                  <div class="hero-actions">
                    <button class="pill primary" @click="play(songs, 0, true)"><Icon name="shuffle" :size="18" /> Tocar aleatório</button>
                  </div>
                </template>
              </div>
            </section>

            <h2 class="h2">Sua biblioteca</h2>
            <div class="chips inline">
              <button :class="{ on: libTab === 'musicas' }" @click="libTab = 'musicas'">Músicas</button>
              <button :class="{ on: libTab === 'albuns' }" @click="libTab = 'albuns'">Álbuns</button>
              <button :class="{ on: libTab === 'recentes' }" @click="libTab = 'recentes'">{{ narrow ? "Recentes" : "Adicionadas recentemente" }}</button>
            </div>
            <div v-if="libTab === 'albuns'" class="grid">
              <button v-for="a in albums" :key="a.key" class="album-card" @click="showAlbum(a)">
                <div class="cover"><img v-if="coverUrl(a.cover)" :src="coverUrl(a.cover)!" alt="" loading="lazy" draggable="false" /><Icon v-else name="disc" :size="40" /></div>
                <strong>{{ a.name }}</strong><small>{{ a.artist }}</small>
              </button>
            </div>
            <TrackTable v-else :tracks="libTab === 'recentes' ? recent : songs" :narrow="narrow" :current="current?.path" :playing="playing" />
          </template>

          <!-- Minha biblioteca (todas as músicas) -->
          <template v-else-if="view.kind === 'biblioteca'">
            <div class="list-head">
              <div>
                <h1 v-if="!narrow" class="h1">Minha biblioteca</h1>
                <p class="meta">{{ count(songs.length, "música", "músicas") }} · {{ minutes(songs) }}</p>
              </div>
              <div class="actions">
                <button class="pill primary" :disabled="!songs.length" @click="play(songs)"><Icon name="media-play" :size="18" /> Tocar</button>
                <button class="pill" :disabled="!songs.length" @click="play(songs, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
              </div>
            </div>
            <TrackTable :tracks="songs" :narrow="narrow" :current="current?.path" :playing="playing" />
            <p v-if="query && !songs.length" class="empty small">Nada com “{{ query }}”.</p>
          </template>

          <!-- Álbuns -->
          <template v-else-if="view.kind === 'albuns'">
            <h1 v-if="!narrow" class="h1">Álbuns</h1>
            <div class="grid">
              <button v-for="a in albums" :key="a.key" class="album-card" @click="showAlbum(a)">
                <div class="cover"><img v-if="coverUrl(a.cover)" :src="coverUrl(a.cover)!" alt="" loading="lazy" draggable="false" /><Icon v-else name="disc" :size="40" /></div>
                <strong>{{ a.name }}</strong><small>{{ a.artist }}{{ a.year ? ` · ${a.year}` : "" }}</small>
              </button>
            </div>
          </template>

          <!-- Artistas -->
          <template v-else-if="view.kind === 'artistas'">
            <h1 v-if="!narrow" class="h1">Artistas</h1>
            <div class="grid artists">
              <button v-for="a in artists" :key="a.name" class="artist-card" @click="go({ kind: 'artista', name: a.name })">
                <div class="cover round"><img v-if="coverUrl(a.albums[0].cover)" :src="coverUrl(a.albums[0].cover)!" alt="" loading="lazy" draggable="false" /><Icon v-else name="user" :size="40" /></div>
                <strong>{{ a.name }}</strong><small>{{ count(a.tracks.length, "música", "músicas") }}</small>
              </button>
            </div>
          </template>

          <!-- Pastas -->
          <template v-else-if="view.kind === 'pastas'">
            <h1 v-if="!narrow" class="h1">Pastas</h1>
            <div class="rows">
              <button v-for="f in folders" :key="f.path" class="row-btn" @click="go({ kind: 'pasta', path: f.path })">
                <span class="ico"><Icon name="folder" :size="22" /></span>
                <span class="names"><strong>{{ f.name }}</strong><small>{{ count(f.tracks.length, "música", "músicas") }} · {{ f.path }}</small></span>
                <Icon name="forward" :size="18" />
              </button>
            </div>
          </template>

          <!-- Playlists (celular) -->
          <template v-else-if="view.kind === 'playlists'">
            <div class="rows">
              <button class="row-btn" @click="go({ kind: 'favoritas' })">
                <span class="ico fav"><Icon name="heart" :size="22" /></span>
                <span class="names"><strong>Favoritas</strong><small>{{ count(favoriteTracks.length, "música", "músicas") }}</small></span>
                <Icon name="forward" :size="18" />
              </button>
              <button v-for="p in lists.playlists" :key="p.id" class="row-btn" @click="go({ kind: 'playlist', id: p.id })">
                <span class="ico"><Icon :name="p.icon" :size="22" /></span>
                <span class="names"><strong>{{ p.name }}</strong><small>{{ count(playlistTracks(p.id).length, "música", "músicas") }}</small></span>
                <Icon name="forward" :size="18" />
              </button>
              <button class="row-btn new" @click="newPlaylist()">
                <span class="ico"><Icon name="plus" :size="22" /></span>
                <span class="names"><strong>Nova playlist</strong></span>
              </button>
            </div>
          </template>

          <!-- um álbum -->
          <template v-else-if="view.kind === 'album' && album">
            <section class="hero">
              <div class="cover big"><img v-if="coverUrl(album.cover)" :src="coverUrl(album.cover)!" alt="" draggable="false" /><Icon v-else name="disc" :size="64" /></div>
              <div class="hero-info">
                <span class="kicker">Álbum</span>
                <strong>{{ album.name }}</strong>
                <button class="artist link" @click="go({ kind: 'artista', name: album.artist })">{{ album.artist }}</button>
                <span class="album">{{ album.year ? `${album.year} · ` : "" }}{{ count(album.tracks.length, "música", "músicas") }} · {{ minutes(album.tracks) }}</span>
                <div class="hero-actions">
                  <button class="pill primary" @click="play(album.tracks)"><Icon name="media-play" :size="18" /> Tocar</button>
                  <button class="pill" @click="play(album.tracks, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
                </div>
              </div>
            </section>
            <TrackTable :tracks="album.tracks" :narrow="narrow" :current="current?.path" :playing="playing" album-numbers hide-album />
          </template>

          <!-- um artista -->
          <template v-else-if="view.kind === 'artista' && artist">
            <section class="hero">
              <div class="cover big round"><img v-if="coverUrl(artist.albums[0].cover)" :src="coverUrl(artist.albums[0].cover)!" alt="" draggable="false" /><Icon v-else name="user" :size="64" /></div>
              <div class="hero-info">
                <span class="kicker">Artista</span>
                <strong>{{ artist.name }}</strong>
                <span class="album">{{ count(artist.albums.length, "álbum", "álbuns") }} · {{ count(artist.tracks.length, "música", "músicas") }}</span>
                <div class="hero-actions">
                  <button class="pill primary" @click="play(artist.tracks)"><Icon name="media-play" :size="18" /> Tocar</button>
                  <button class="pill" @click="play(artist.tracks, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
                </div>
              </div>
            </section>
            <h2 class="h2">Álbuns</h2>
            <div class="grid">
              <button v-for="a in artist.albums" :key="a.key" class="album-card" @click="showAlbum(a)">
                <div class="cover"><img v-if="coverUrl(a.cover)" :src="coverUrl(a.cover)!" alt="" loading="lazy" draggable="false" /><Icon v-else name="disc" :size="40" /></div>
                <strong>{{ a.name }}</strong><small>{{ a.year || count(a.tracks.length, "música", "músicas") }}</small>
              </button>
            </div>
            <h2 class="h2">Músicas</h2>
            <TrackTable :tracks="artist.tracks" :narrow="narrow" :current="current?.path" :playing="playing" />
          </template>

          <!-- Qobuz: as playlists da conta -->
          <template v-else-if="view.kind === 'qobuz'">
            <h1 v-if="!narrow" class="h1">Qobuz</h1>
            <div v-if="!qobuz.connected" class="empty">
              <Icon name="music" :size="46" />
              <p>Conecte a sua conta do Qobuz: você entra no site deles numa janelinha e as suas playlists aparecem aqui.</p>
              <button class="pill primary" @click="connectQobuz()"><Icon name="link" :size="18" /> Conectar Qobuz</button>
            </div>
            <template v-else>
              <p class="meta qmeta">Conectado como <b>{{ qobuz.name }}</b></p>
              <p v-if="qLoading && !qPlaylists" class="empty small">Buscando as suas playlists…</p>
              <div class="grid">
                <button class="album-card" @click="go({ kind: 'qfavoritas' })">
                  <div class="cover fav"><Icon name="heart" :size="44" /></div>
                  <strong>Favoritas</strong><small>As músicas que você curtiu</small>
                </button>
                <button v-for="p in qPlaylists ?? []" :key="p.id" class="album-card" @click="go({ kind: 'qplaylist', id: p.id })">
                  <div class="cover"><img v-if="p.cover" :src="p.cover" alt="" loading="lazy" draggable="false" /><Icon v-else name="list" :size="40" /></div>
                  <strong>{{ p.name }}</strong><small>{{ count(p.count, "música", "músicas") }}</small>
                </button>
              </div>
              <p v-if="qPlaylists && !qPlaylists.length" class="empty small">Nenhuma playlist na sua conta ainda.</p>
            </template>
          </template>

          <!-- Qobuz: uma playlist ou as favoritas -->
          <template v-else-if="view.kind === 'qplaylist' || view.kind === 'qfavoritas'">
            <section class="hero">
              <div class="cover big" :class="{ fav: view.kind === 'qfavoritas' }">
                <img v-if="qPlaylist?.cover" :src="qPlaylist.cover" alt="" draggable="false" />
                <Icon v-else :name="view.kind === 'qfavoritas' ? 'heart' : 'list'" :size="64" />
              </div>
              <div class="hero-info">
                <span class="kicker">Qobuz</span>
                <strong>{{ title }}</strong>
                <span v-if="qPlaylist?.description" class="album">{{ qPlaylist.description }}</span>
                <span class="album">
                  {{ count(qList.length || qPlaylist?.count || 0, "música", "músicas") }}{{ qPlaylist?.duration ? ` · ${hours(qPlaylist.duration)}` : "" }}
                </span>
                <div class="hero-actions">
                  <button class="pill primary" :disabled="!qList.length" @click="play(qList)"><Icon name="media-play" :size="18" /> Tocar</button>
                  <button class="pill" :disabled="!qList.length" @click="play(qList, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
                  <button class="circle" aria-label="Atualizar" :disabled="qLoading" @click="loadQobuzTracks(qKey, true)"><Icon name="refresh" :size="20" /></button>
                </div>
              </div>
            </section>
            <p v-if="qLoading && !qList.length" class="empty small">Buscando as músicas…</p>
            <TrackTable v-else-if="qList.length" :tracks="qList" :narrow="narrow" :current="current?.path" :playing="playing" />
            <p v-else class="empty small">Nenhuma música aqui.</p>
          </template>

          <!-- favoritas, uma playlist, uma pasta -->
          <template v-else-if="['favoritas', 'playlist', 'pasta'].includes(view.kind)">
            <section class="hero">
              <div class="cover big plain" :class="{ fav: view.kind === 'favoritas' }">
                <img v-if="listTracks[0] && coverUrl(listTracks[0])" :src="coverUrl(listTracks[0])!" alt="" draggable="false" />
                <Icon v-else :name="view.kind === 'favoritas' ? 'heart' : view.kind === 'pasta' ? 'folder' : playlist?.icon ?? 'music'" :size="64" />
              </div>
              <div class="hero-info">
                <span class="kicker">{{ view.kind === "pasta" ? "Pasta" : "Playlist" }}</span>
                <strong>{{ title }}</strong>
                <span class="album">{{ count(listTracks.length, "música", "músicas") }}{{ listTracks.length ? ` · ${minutes(listTracks)}` : "" }}</span>
                <div class="hero-actions">
                  <button class="pill primary" :disabled="!listTracks.length" @click="play(listTracks)"><Icon name="media-play" :size="18" /> Tocar</button>
                  <button class="pill" :disabled="!listTracks.length" @click="play(listTracks, 0, true)"><Icon name="shuffle" :size="18" /> Aleatório</button>
                  <template v-if="playlist">
                    <button class="circle" aria-label="Renomear" @click="editPlaylist()"><Icon name="edit" :size="20" /></button>
                    <button class="circle" :class="{ danger: confirmDelete }" :aria-label="confirmDelete ? 'Tocar de novo apaga' : 'Apagar a playlist'" @click="removePlaylist()">
                      <Icon name="trash" :size="20" />
                    </button>
                  </template>
                </div>
                <small v-if="confirmDelete" class="warn">Toque de novo na lixeira pra apagar a playlist (as músicas continuam).</small>
              </div>
            </section>
            <TrackTable
              v-if="listTracks.length"
              :tracks="listTracks"
              :narrow="narrow"
              :current="current?.path"
              :playing="playing"
              :playlist="playlist?.id"
            />
            <p v-else class="empty small">
              {{ view.kind === "favoritas" ? "Toque no coração de uma música pra ela aparecer aqui." : "Vazia. Use o ⋯ de uma música → Adicionar à playlist." }}
            </p>
          </template>

          <p v-if="error" class="error" @click="error = ''">{{ error }}</p>
        </div>
      </main>

      <!-- PC: a fila (ou a letra, os detalhes) e a Emma -->
      <aside v-if="!narrow && queueOpen" class="queue-panel">
        <header>
          <Icon :name="panel === 'fila' ? 'list' : panel === 'letras' ? 'document' : 'info'" :size="24" />
          <h2>{{ panel === "fila" ? "Fila de reprodução" : panel === "letras" ? "Letra" : "Detalhes" }}</h2>
          <div class="seg">
            <button :class="{ on: panel === 'fila' }" aria-label="Fila" @click="panel = 'fila'"><Icon name="list" :size="18" /></button>
            <button :class="{ on: panel === 'letras' }" aria-label="Letra" @click="panel = 'letras'"><Icon name="document" :size="18" /></button>
            <button :class="{ on: panel === 'detalhes' }" aria-label="Detalhes" @click="panel = 'detalhes'"><Icon name="info" :size="18" /></button>
          </div>
        </header>
        <div class="panel-body">
          <template v-if="panel === 'fila'">
            <p class="sub">A seguir</p>
            <QueueList v-if="music" :music="music" />
          </template>
          <LyricsView v-else-if="panel === 'letras'" :track="current" :position="music?.position ?? 0" />
          <DetailsView v-else :track="current" />
          <p v-if="panel !== 'fila' && !current" class="sub">Toque uma música primeiro.</p>
        </div>
        <EmmaBox :context="emmaContext" />
      </aside>
    </div>

    <!-- PC: a barra do player -->
    <PlayerBar v-if="!narrow && music" :music="music" :queue-open="queueOpen" @queue="queueOpen = !queueOpen" @cover="go({ kind: 'ouvir' }, true)" />

    <!-- celular: a barrinha do que está tocando -->
    <footer v-if="narrow && current && music && !playerOpen" class="mini" @click="playerOpen = true">
      <div class="progress"><span :style="{ width: `${music.duration ? (music.position / music.duration) * 100 : 0}%` }" /></div>
      <span class="thumb"><img v-if="coverUrl(current)" :src="coverUrl(current)!" alt="" draggable="false" /><Icon v-else name="music" :size="20" /></span>
      <span class="names"><strong>{{ current.title }}</strong><small>{{ artistOf(current) }}</small></span>
      <button class="round" :aria-label="playing ? 'Pausar' : 'Tocar'" @click.stop="haptic(10).catch(() => {}); musicControl('toggle').catch(() => {})">
        <Icon :name="playing ? 'pause' : 'media-play'" :size="24" />
      </button>
      <button class="round" aria-label="Próxima" @click.stop="musicControl('next').catch(() => {})"><Icon name="skip-forward" :size="22" /></button>
    </footer>
    <NowPlaying v-if="narrow && playerOpen && current && music" :music="music" @close="playerOpen = false" />

    <TrackMenu :narrow="narrow" @details="narrow ? (playerOpen = !!current) : (queueOpen = true)" />

    <!-- nova playlist / renomear -->
    <div v-if="playlistForm" class="veil" @click.self="playlistForm = null">
      <form class="dialog" @submit.prevent="submitPlaylist()">
        <h2>{{ playlistForm.id ? "Renomear playlist" : "Nova playlist" }}</h2>
        <input v-model="playlistForm.name" placeholder="Nome (ex.: Viagens, Foco)" maxlength="60" autofocus />
        <div class="icons">
          <button v-for="i in ICONS" :key="i" type="button" :class="{ on: playlistForm.icon === i }" :aria-label="i" @click="playlistForm.icon = i">
            <Icon :name="i" :size="22" />
          </button>
        </div>
        <div class="dialog-actions">
          <button type="button" class="pill" @click="playlistForm = null">Cancelar</button>
          <button class="pill primary" :disabled="!playlistForm.name.trim()">{{ playlistForm.id ? "Salvar" : "Criar" }}</button>
        </div>
      </form>
    </div>

    <!-- configurações do app -->
    <div v-if="settingsSheet" class="veil" @click.self="settingsSheet = false">
      <div class="dialog">
        <h2>Configurações</h2>
        <p class="meta">As músicas vêm da pasta:</p>
        <p class="path">{{ folder || "~/Músicas" }}</p>
        <p class="meta">{{ count(tracks.length, "música", "músicas") }} · {{ count(albums.length, "álbum", "álbuns") }} · {{ count(artists.length, "artista", "artistas") }}</p>
        <h2 class="sub-h">Qobuz</h2>
        <p class="meta">{{ qobuz.connected ? `Conectado como ${qobuz.name}. O login fica guardado só neste aparelho.` : "Não conectado. Você entra no site do Qobuz e as suas playlists aparecem no Music." }}</p>
        <div class="dialog-actions">
          <button v-if="qobuz.connected" class="pill" @click="disconnectQobuz()"><Icon name="close" :size="18" /> Desconectar</button>
          <button v-else class="pill primary" @click="connectQobuz(); settingsSheet = false"><Icon name="link" :size="18" /> Conectar Qobuz</button>
        </div>
        <h2 class="sub-h">Biblioteca</h2>
        <div class="dialog-actions">
          <button class="pill" @click="filesWindow(folder || undefined).catch(() => {})"><Icon name="folder" :size="18" /> Abrir a pasta</button>
          <button class="pill primary" :disabled="loading" @click="load()"><Icon name="refresh" :size="18" /> {{ loading ? "Lendo…" : "Reler a biblioteca" }}</button>
        </div>
        <button class="close-x" aria-label="Fechar" @click="settingsSheet = false"><Icon name="close" :size="20" /></button>
      </div>
    </div>

    <p v-if="toast" class="toast" @click="toast = ''">{{ toast }}</p>
  </div>
</template>

<style scoped>
.music {
  position: relative;
  display: flex;
  flex-direction: column;
  height: var(--app-h, 100vh);
  background: radial-gradient(80% 60% at 75% 10%, rgba(91, 33, 182, 0.28), transparent 65%), #0b0912;
  color: var(--lumna-text);
  overflow: hidden;
}
.body {
  flex: 1;
  min-height: 0;
  display: flex;
}

/* ---- barra lateral ---- */
.side {
  flex: none;
  width: 236px;
  display: flex;
  flex-direction: column;
  padding: 16px 0 14px;
  border-right: 1px solid rgba(167, 139, 250, 0.1);
  background: rgba(11, 9, 18, 0.6);
}
.side nav {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.side nav button {
  position: relative;
  display: flex;
  align-items: center;
  gap: 16px;
  height: 50px;
  margin: 0 10px;
  padding: 0 16px;
  border: 0;
  border-radius: 10px;
  background: none;
  color: #e4defa;
  font: inherit;
  font-size: 15px;
  text-align: left;
}
.side nav button :deep(svg) {
  flex: none;
  color: #c4b5fd;
}
.side nav button:hover {
  background: rgba(167, 139, 250, 0.07);
}
.side nav button.on {
  background: rgba(76, 29, 149, 0.32);
}
.side nav button.on::before {
  content: "";
  position: absolute;
  left: -10px;
  top: 8px;
  bottom: 8px;
  width: 4px;
  border-radius: 0 4px 4px 0;
  background: #8b5cf6;
}
.side nav button.on :deep(svg) {
  color: #a78bfa;
}
.side .sep {
  height: 1px;
  margin: 16px 24px;
  background: rgba(167, 139, 250, 0.14);
}
.lists-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 18px 6px 26px;
  color: #b7a6ea;
  font-size: 15px;
}
.lists-head button {
  width: 34px;
  height: 34px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: #b7a6ea;
}
.side .lists {
  overflow-y: auto;
}
.grow {
  flex: 1;
}

/* ---- centro ---- */
.main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.top {
  flex: none;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 14px 22px 4px;
}
.narrow .top {
  padding: 12px 14px 4px 18px;
}
.mtitle {
  flex: 1;
  min-width: 0;
  margin: 0;
  overflow: hidden;
  font-size: 26px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.search {
  flex: 1;
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 12px;
  height: 46px;
  padding: 0 8px 0 16px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 999px;
  background: rgba(20, 16, 34, 0.75);
  color: var(--lumna-muted);
}
.search input {
  flex: 1;
  min-width: 0;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  outline: none;
}
.search .clear {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.round {
  flex: none;
  width: 46px;
  height: 46px;
  display: grid;
  place-items: center;
  overflow: hidden;
  padding: 0;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 50%;
  background: rgba(30, 24, 46, 0.7);
  color: var(--lumna-text);
}
.round img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 0 22px 24px;
}
.narrow .scroll {
  padding: 0 10px 16px;
}
.h1 {
  margin: 8px 0 18px;
  font-size: 34px;
  letter-spacing: -0.01em;
}
.h2 {
  margin: 22px 0 12px;
  font-size: 24px;
}
.narrow .h2 {
  margin: 18px 8px 10px;
  font-size: 20px;
}
.chips {
  flex: none;
  display: flex;
  gap: 8px;
  padding: 8px 18px 12px;
  overflow-x: auto;
  scrollbar-width: none;
}
.chips.inline {
  padding: 0 0 12px;
}
.narrow .chips.inline {
  padding: 0 8px 10px;
}
.chips button {
  flex: none;
  height: 40px;
  padding: 0 22px;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 999px;
  background: rgba(20, 16, 34, 0.6);
  color: #e4defa;
  font: inherit;
  font-size: 15px;
}
.chips button.on {
  border-color: transparent;
  background: #8b5cf6;
  color: #fff;
}

/* ---- topo de uma página (tocando, álbum, artista, playlist) ---- */
.hero {
  display: flex;
  align-items: center;
  gap: 32px;
  padding: 4px 0 8px;
}
.narrow .hero {
  gap: 16px;
  padding: 6px 8px 8px;
}
.cover {
  aspect-ratio: 1;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 10px;
  background: linear-gradient(135deg, #2a1f45, #120f1d);
  color: rgba(167, 139, 250, 0.6);
}
.cover img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.cover.round {
  border-radius: 50%;
}
.cover.big {
  flex: none;
  width: 232px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.45);
}
.narrow .cover.big {
  width: 38%;
  max-width: 170px;
}
.cover.fav {
  background: linear-gradient(135deg, #7c3aed, #db2777);
  color: #fff;
}
.hero-info {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.kicker {
  color: #b7a6ea;
  font-size: 12px;
  letter-spacing: 0.22em;
  text-transform: uppercase;
}
.hero-info strong {
  font-size: 32px;
  line-height: 1.15;
}
.narrow .hero-info strong {
  font-size: 22px;
}
.hero-info .artist {
  color: #b7a6ea;
  font-size: 20px;
}
.narrow .hero-info .artist {
  font-size: 16px;
}
.hero-info .album {
  color: var(--lumna-muted);
  font-size: 15px;
}
.link {
  align-self: flex-start;
  padding: 0;
  border: 0;
  background: none;
  font: inherit;
  text-align: left;
}
.hero-actions {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 12px;
  margin-top: 16px;
}
.narrow .hero-actions {
  margin-top: 8px;
  gap: 8px;
}
.circle {
  width: 50px;
  height: 50px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(167, 139, 250, 0.22);
  border-radius: 50%;
  background: rgba(30, 24, 46, 0.7);
  color: var(--lumna-text);
}
.circle.on {
  background: rgba(76, 29, 149, 0.5);
  color: #c4b5fd;
}
.circle.on :deep(svg) {
  fill: currentColor;
}
.circle.danger {
  border-color: rgba(239, 68, 68, 0.5);
  color: #fca5a5;
}
.warn {
  color: #fca5a5;
}
.pill {
  display: inline-flex;
  align-items: center;
  gap: 8px;
  height: 44px;
  padding: 0 20px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 999px;
  background: rgba(20, 16, 34, 0.6);
  color: #e4defa;
  font: inherit;
  font-size: 15px;
}
.pill.primary {
  border-color: transparent;
  background: linear-gradient(135deg, #a855f7, #6d28d9);
  color: #fff;
  font-weight: 600;
}
.pill:disabled {
  opacity: 0.5;
}
.list-head {
  display: flex;
  align-items: flex-end;
  justify-content: space-between;
  flex-wrap: wrap;
  gap: 12px;
  padding: 0 0 12px;
}
.narrow .list-head {
  padding: 4px 8px 10px;
}
.list-head .h1 {
  margin-bottom: 4px;
}
.list-head .actions {
  display: flex;
  gap: 10px;
}
.meta {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 14px;
}

/* ---- grades e listas ---- */
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(168px, 1fr));
  gap: 20px 16px;
}
.narrow .grid {
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 16px 12px;
  padding: 0 8px;
}
.album-card,
.artist-card {
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 3px;
  padding: 0;
  border: 0;
  background: none;
  color: inherit;
  text-align: left;
}
.artist-card {
  align-items: center;
  text-align: center;
}
.album-card .cover,
.artist-card .cover {
  width: 100%;
}
.album-card strong,
.artist-card strong,
.album-card small,
.artist-card small {
  max-width: 100%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.album-card strong,
.artist-card strong {
  margin-top: 8px;
  font-size: 15px;
  font-weight: 500;
}
.album-card small,
.artist-card small {
  color: var(--lumna-muted);
  font-size: 13px;
}
.rows {
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.row-btn {
  display: flex;
  align-items: center;
  gap: 14px;
  min-height: 64px;
  padding: 6px 10px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  text-align: left;
}
.row-btn:hover {
  background: rgba(167, 139, 250, 0.06);
}
.ico {
  flex: none;
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: rgba(76, 29, 149, 0.3);
  color: #c4b5fd;
}
.ico.fav {
  background: linear-gradient(135deg, #7c3aed, #db2777);
  color: #fff;
}
.names {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.names strong,
.names small {
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.names strong {
  color: var(--lumna-text);
  font-weight: 500;
  font-size: 15px;
}
.names small {
  font-size: 13px;
}
.empty {
  max-width: 440px;
  margin: 50px auto;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  color: var(--lumna-muted);
  text-align: center;
  line-height: 1.6;
}
.empty.small {
  margin: 20px auto;
}
.error {
  margin: 12px 8px;
  color: #fca5a5;
}

/* ---- fila (PC) ---- */
.queue-panel {
  flex: none;
  width: 372px;
  display: flex;
  flex-direction: column;
  padding: 18px 24px 20px;
  border-left: 1px solid rgba(167, 139, 250, 0.1);
  background: rgba(11, 9, 18, 0.55);
}
.queue-panel header {
  display: flex;
  align-items: center;
  gap: 14px;
  color: #c4b5fd;
}
.queue-panel h2 {
  flex: 1;
  margin: 0;
  color: var(--lumna-text);
  font-size: 22px;
  font-weight: 600;
}
.seg {
  display: flex;
  gap: 2px;
}
.seg button {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 8px;
  background: none;
  color: var(--lumna-muted);
}
.seg button.on {
  background: rgba(76, 29, 149, 0.4);
  color: #c4b5fd;
}
.panel-body {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  margin: 14px 0;
}
.sub {
  margin: 0 0 10px;
  color: #d9d3f0;
  font-size: 15px;
}

/* ---- celular: a barrinha ---- */
.mini {
  position: relative;
  flex: none;
  display: flex;
  align-items: center;
  gap: 12px;
  margin: 0 10px 10px;
  padding: 8px 8px 8px 8px;
  border: 1px solid rgba(167, 139, 250, 0.16);
  border-radius: 18px;
  background: #1a1626;
  overflow: hidden;
}
.mini .progress {
  position: absolute;
  inset: auto 0 0;
  height: 3px;
  background: rgba(255, 255, 255, 0.08);
}
.mini .progress span {
  display: block;
  height: 100%;
  background: var(--lumna-violet-soft);
}
.mini .thumb {
  flex: none;
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border-radius: 10px;
  background: #241e35;
  color: var(--lumna-muted);
}
.mini .thumb img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.mini .round {
  border: 0;
  background: none;
}

/* ---- janelinhas ---- */
.veil {
  position: absolute;
  inset: 0;
  z-index: 50;
  display: grid;
  place-items: center;
  padding: 16px;
  background: rgba(4, 3, 10, 0.6);
}
.dialog {
  position: relative;
  width: min(420px, 100%);
  display: flex;
  flex-direction: column;
  gap: 12px;
  padding: 22px;
  border: 1px solid rgba(167, 139, 250, 0.22);
  border-radius: 20px;
  background: #17141f;
  box-shadow: 0 20px 60px rgba(0, 0, 0, 0.6);
}
.dialog h2 {
  margin: 0;
  font-size: 20px;
}
.dialog input {
  height: 46px;
  padding: 0 14px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 12px;
  background: #1c1828;
  color: inherit;
  font: inherit;
  font-size: 15px;
  outline: none;
}
.icons {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.icons button {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(167, 139, 250, 0.16);
  border-radius: 12px;
  background: none;
  color: #c4b5fd;
}
.icons button.on {
  border-color: #8b5cf6;
  background: rgba(76, 29, 149, 0.4);
}
.dialog-actions {
  display: flex;
  flex-wrap: wrap;
  justify-content: flex-end;
  gap: 10px;
  margin-top: 6px;
}
.path {
  margin: 0;
  padding: 10px 12px;
  border-radius: 10px;
  background: #1c1828;
  font-size: 14px;
  overflow-wrap: anywhere;
}
.sub-h {
  margin: 10px 0 -4px !important;
  font-size: 16px !important;
}
.qmeta {
  margin: -8px 0 16px;
}
.narrow .qmeta {
  margin: 0 8px 12px;
}
.close-x {
  position: absolute;
  top: 14px;
  right: 14px;
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.toast {
  position: absolute;
  left: 50%;
  bottom: 120px;
  z-index: 60;
  width: min(440px, calc(100% - 32px));
  margin: 0;
  padding: 12px 16px;
  transform: translateX(-50%);
  border-radius: 14px;
  background: #241e35;
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
  font-size: 14px;
  line-height: 1.5;
}
</style>
