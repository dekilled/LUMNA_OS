// O estado do app LUMNA Music (uma janela só): a biblioteca, as favoritas e playlists, o que está
// aberto e as ações (tocar, fila, playlists). Quem toca é o shell; aqui é a biblioteca.
import { computed, ref } from "vue";
import {
  audioSetDefault,
  audioState,
  haptic,
  musicFavorite,
  musicLibrary,
  musicLists,
  musicMode,
  musicPlay,
  musicPlaylistDelete,
  musicPlaylistSave,
  musicPlaylistTracks,
  musicQueueAdd,
  qobuzFavorites,
  qobuzLogin,
  qobuzLogout,
  qobuzPlaylists,
  qobuzPlaylistTracks,
  qobuzStatus,
  type AudioNode,
  type MusicLists,
  type QobuzPlaylist,
  type QobuzStatus,
  type Track,
} from "../../system";
import { albumOf, artistOf, isQobuz } from "./music";

export interface Album {
  key: string;
  name: string;
  artist: string;
  year: number;
  cover: Track;
  tracks: Track[];
}
export interface Artist {
  name: string;
  albums: Album[];
  tracks: Track[];
}
export interface Folder {
  path: string;
  name: string;
  tracks: Track[];
}

export type View =
  | { kind: "ouvir" }
  | { kind: "biblioteca" }
  | { kind: "albuns" }
  | { kind: "artistas" }
  | { kind: "pastas" }
  | { kind: "playlists" }
  | { kind: "favoritas" }
  | { kind: "playlist"; id: string }
  | { kind: "album"; key: string }
  | { kind: "artista"; name: string }
  | { kind: "pasta"; path: string }
  | { kind: "qobuz" }
  | { kind: "qplaylist"; id: string }
  | { kind: "qfavoritas" };

export const tracks = ref<Track[]>([]);
export const loading = ref(true);
export const lists = ref<MusicLists>({ favorites: [], playlists: [] });
export const query = ref("");
export const error = ref("");
/** O caminho de telas (voltar tira a última). */
export const history = ref<View[]>([{ kind: "ouvir" }]);
export const view = computed(() => history.value[history.value.length - 1]);

export function go(v: View, replace = false) {
  history.value = replace ? [v] : [...history.value, v];
}
export function back() {
  if (history.value.length > 1) history.value = history.value.slice(0, -1);
}

export async function load() {
  loading.value = true;
  const [lib, l] = await Promise.all([musicLibrary().catch(() => []), musicLists().catch(() => null)]);
  tracks.value = lib;
  if (l) lists.value = l;
  loading.value = false;
}

const norm = (t: string) => t.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
export const filtered = computed(() => {
  const q = norm(query.value.trim());
  if (!q) return tracks.value;
  return tracks.value.filter((t) => norm(`${t.title} ${t.artist} ${t.album_artist} ${t.album}`).includes(q));
});
export const byPath = computed(() => new Map(tracks.value.map((t) => [t.path, t])));
export const songs = computed(() => [...filtered.value].sort((a, b) => a.title.localeCompare(b.title, "pt-BR")));
export const recent = computed(() => [...filtered.value].sort((a, b) => b.mtime - a.mtime).slice(0, 100));

export const albums = computed<Album[]>(() => {
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
export const artists = computed<Artist[]>(() => {
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
export const folders = computed<Folder[]>(() => {
  const map = new Map<string, Folder>();
  for (const t of filtered.value) {
    const path = t.path.slice(0, t.path.lastIndexOf("/"));
    let f = map.get(path);
    if (!f) {
      f = { path, name: path.slice(path.lastIndexOf("/") + 1) || path, tracks: [] };
      map.set(path, f);
    }
    f.tracks.push(t);
  }
  return [...map.values()].sort((a, b) => a.name.localeCompare(b.name, "pt-BR"));
});

export const favorites = computed(() => new Set(lists.value.favorites));
export const favoriteTracks = computed(() => lists.value.favorites.map((p) => byPath.value.get(p)).filter((t): t is Track => !!t));
export const playlistTracks = (id: string) =>
  (lists.value.playlists.find((p) => p.id === id)?.tracks ?? []).map((p) => byPath.value.get(p)).filter((t): t is Track => !!t);

export const albumByKey = (key: string) => albums.value.find((a) => a.key === key) ?? null;
export const albumOfTrack = (t: Track) => albums.value.find((a) => a.key === `${artistOf(t)}\u0000${albumOf(t)}`) ?? null;

// ---- ações ----

async function run(f: () => Promise<unknown>) {
  error.value = "";
  try {
    await f();
  } catch (e) {
    error.value = String(e);
  }
}

/** Tocar do Qobuz vem na próxima etapa (agora: login e playlists). */
function notYet(list: Track[]) {
  if (!list.some(isQobuz)) return false;
  error.value = "Tocar do Qobuz chega na próxima atualização: por enquanto dá pra ver as suas playlists e favoritas.";
  return true;
}

export function play(list: Track[], index = 0, shuffle = false) {
  if (!list.length || notYet(list)) return;
  haptic(10).catch(() => {});
  return run(async () => {
    await musicMode({ shuffle });
    await musicPlay(list, shuffle ? Math.floor(Math.random() * list.length) : index);
  });
}
export const playNext = (t: Track[]) => !notYet(t) && run(() => musicQueueAdd(t, true));
export const addToQueue = (t: Track[]) => !notYet(t) && run(() => musicQueueAdd(t, false));
export const toggleFavorite = (t: Track) =>
  run(async () => {
    haptic(10).catch(() => {});
    lists.value = await musicFavorite(t.path, !favorites.value.has(t.path));
  });
export const savePlaylist = (name: string, id?: string, icon?: string) =>
  run(async () => {
    lists.value = await musicPlaylistSave(name, id, icon);
  });
export const deletePlaylist = (id: string) =>
  run(async () => {
    lists.value = await musicPlaylistDelete(id);
    if (view.value.kind === "playlist" && view.value.id === id) back();
  });
export const addToPlaylist = (id: string, t: Track[]) =>
  run(async () => {
    lists.value = await musicPlaylistTracks(id, t.map((x) => x.path));
  });
export const removeFromPlaylist = (id: string, t: Track[]) =>
  run(async () => {
    lists.value = await musicPlaylistTracks(id, t.map((x) => x.path), true);
  });

// ---- menu de uma música (⋯) ----

export const menu = ref<{ track: Track; x: number; y: number; playlist?: string } | null>(null);
export function openMenu(track: Track, e: MouseEvent, playlist?: string) {
  menu.value = { track, x: e.clientX, y: e.clientY, playlist };
}

/** Detalhes/letras pedidos pelo menu (o painel da direita no PC, a aba no celular). */
export const panel = ref<"fila" | "letras" | "detalhes">("fila");

// ---- saída do som ----

export const outputs = ref<AudioNode[]>([]);
export async function loadOutputs() {
  outputs.value = (await audioState().catch(() => null))?.outputs ?? [];
}
export const setOutput = (o: AudioNode) =>
  run(async () => {
    await audioSetDefault(o.id);
    await loadOutputs();
  });
/** "Neste celular" (o alto-falante do aparelho) ou o nome da saída (o fone, a caixa Bluetooth). */
export function outputLabel(o: AudioNode | undefined, phone: boolean): string {
  if (!o) return phone ? "Neste celular" : "Neste computador";
  if (/speaker|alto.?falante|built.?in|interno|analog/i.test(o.name)) return phone ? "Neste celular" : "Neste computador";
  return o.name;
}

// ---- Qobuz ----

export const qobuz = ref<QobuzStatus>({ connected: false, name: "" });
export const qPlaylists = ref<QobuzPlaylist[] | null>(null);
/** As músicas de cada playlist do Qobuz já abertas ("fav" = as favoritas). */
export const qTracks = ref(new Map<string, Track[]>());
export const qLoading = ref(false);

export async function loadQobuz() {
  qobuz.value = await qobuzStatus().catch(() => qobuz.value);
  if (!qobuz.value.connected) {
    qPlaylists.value = null;
    return;
  }
  qLoading.value = true;
  await run(async () => {
    qPlaylists.value = await qobuzPlaylists();
  });
  qLoading.value = false;
}

/** As músicas de uma playlist do Qobuz (ou "fav"), buscando na primeira vez. */
export async function loadQobuzTracks(id: string, again = false) {
  if (qTracks.value.has(id) && !again) return;
  qLoading.value = true;
  await run(async () => {
    const list = id === "fav" ? await qobuzFavorites() : await qobuzPlaylistTracks(id);
    qTracks.value = new Map(qTracks.value).set(id, list);
  });
  qLoading.value = false;
}

export const connectQobuz = () => run(() => qobuzLogin());
export const disconnectQobuz = () =>
  run(async () => {
    await qobuzLogout();
    qobuz.value = { connected: false, name: "" };
    qPlaylists.value = null;
    qTracks.value = new Map();
    if (["qobuz", "qplaylist", "qfavoritas"].includes(view.value.kind)) go({ kind: "ouvir" }, true);
  });
