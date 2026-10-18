<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import { computed, nextTick, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "../Icon.vue";
import WindowBar from "../WindowBar.vue";
import FileArt from "./FileArt.vue";
import { crumbs, formatDate, formatSize, KIND_LABEL } from "./format";
import {
  connectShare,
  connectTargets,
  type ConnectTarget,
  filesInfo,
  filesList,
  filesMkdir,
  filesMount,
  filesOpen,
  filesPaste,
  filesPlaces,
  filesRename,
  filesStamp,
  filesTerminal,
  filesTrash,
  filesTrashDelete,
  filesTrashList,
  filesTrashRestore,
  filesUnmount,
  filesWindow,
  on,
  type Drive,
  type FileEntry,
  type FileInfo,
  type Listing,
  type Places,
  type TrashItem,
} from "../../system";

const TRASH = "lumna:trash";
type SortKey = "name" | "modified" | "size" | "kind";

const win = getCurrentWindow();
const store = {
  get: (key: string) => {
    try {
      return localStorage.getItem(`lumna.files.${key}`);
    } catch {
      return null;
    }
  },
  set: (key: string, value: string) => {
    try {
      localStorage.setItem(`lumna.files.${key}`, value);
    } catch {
      /* sem armazenamento: só não lembra */
    }
  },
};

// ---- estado ----

const places = ref<Places | null>(null);
const path = ref("");
const listing = ref<Listing | null>(null);
const trash = ref<TrashItem[]>([]);
const back = ref<string[]>([]);
const forward = ref<string[]>([]);
const view = ref<"grid" | "list">(store.get("view") === "list" ? "list" : "grid");
const showHidden = ref(store.get("hidden") === "1");
const showDetails = ref(store.get("details") === "1");
const sort = ref<{ key: SortKey; asc: boolean }>({ key: "name", asc: true });
const query = ref("");
const selected = ref(new Set<string>());
const anchor = ref(-1);
const renaming = ref<string | null>(null);
const renameText = ref("");
const creating = ref(false);
const newName = ref("");
const editingPath = ref(false);
const pathText = ref("");
const info = ref<FileInfo | null>(null);
const progress = ref<{ done: number; total: number; current: string } | null>(null);
const toast = ref<{ text: string; error?: boolean } | null>(null);
const menu = ref<{ x: number; y: number; items: MenuItem[] } | null>(null);
const ask = ref<{ title: string; text: string; ok: string; danger?: boolean; resolve: (yes: boolean) => void } | null>(null);
const clipboard = ref<{ paths: string[]; cut: boolean } | null>(readClipboard());

const content = ref<HTMLElement>();
const filterInput = ref<HTMLInputElement>();
let stamp = 0;

interface MenuItem {
  label?: string;
  icon?: string;
  hint?: string;
  danger?: boolean;
  disabled?: boolean;
  action?: () => void;
}

const inTrash = computed(() => path.value === TRASH);
const home = computed(() => places.value?.home ?? "");

/** Busca sem acento: "musica" acha "Música". */
const fold = (text: string) => text.normalize("NFD").replace(/\p{Diacritic}/gu, "").toLowerCase();

const entries = computed<FileEntry[]>(() => {
  const list = listing.value?.entries ?? [];
  const q = fold(query.value.trim());
  const filtered = q ? list.filter((e) => fold(e.name).includes(q)) : list;
  const dir = sort.value.asc ? 1 : -1;
  const collator = new Intl.Collator("pt-BR", { numeric: true, sensitivity: "base" });
  return [...filtered].sort((a, b) => {
    // Pastas sempre antes dos arquivos.
    if (a.dir !== b.dir) return a.dir ? -1 : 1;
    let c = 0;
    if (sort.value.key === "modified") c = a.modified - b.modified;
    else if (sort.value.key === "size") c = a.size - b.size;
    else if (sort.value.key === "kind") c = collator.compare(KIND_LABEL[a.kind], KIND_LABEL[b.kind]);
    return (c || collator.compare(a.name, b.name)) * dir;
  });
});

const trashShown = computed(() => {
  const q = fold(query.value.trim());
  return q ? trash.value.filter((t) => fold(t.name).includes(q)) : trash.value;
});

/** Chave de cada item: caminho (pastas) ou id (lixeira). */
const keys = computed(() => (inTrash.value ? trashShown.value.map((t) => t.id) : entries.value.map((e) => e.path)));
const selectedEntries = computed(() => entries.value.filter((e) => selected.value.has(e.path)));
const selectedSize = computed(() => selectedEntries.value.reduce((sum, e) => sum + e.size, 0));
const single = computed(() => (selectedEntries.value.length === 1 ? selectedEntries.value[0] : null));
const crumbList = computed(() => (inTrash.value ? [{ name: "Lixeira", path: TRASH }] : crumbs(path.value, home.value)));

function title(p: string) {
  if (p === TRASH) return "Lixeira";
  if (p === home.value) return "Início";
  return p.split("/").filter(Boolean).pop() ?? "Computador";
}

// ---- área de transferência (compartilhada entre as janelas do Explorador) ----

function readClipboard(): { paths: string[]; cut: boolean } | null {
  try {
    return JSON.parse(store.get("clipboard") ?? "null");
  } catch {
    return null;
  }
}

function setClipboard(value: { paths: string[]; cut: boolean } | null) {
  clipboard.value = value;
  store.set("clipboard", JSON.stringify(value));
}

function onStorage(event: StorageEvent) {
  if (event.key === "lumna.files.clipboard") clipboard.value = readClipboard();
}

// ---- avisos ----

let toastTimer: number | undefined;
function notify(text: string, error = false) {
  toast.value = { text, error };
  window.clearTimeout(toastTimer);
  toastTimer = window.setTimeout(() => (toast.value = null), error ? 6000 : 3000);
}

const confirm = (title: string, text: string, ok: string, danger = false) =>
  new Promise<boolean>((resolve) => (ask.value = { title, text, ok, danger, resolve }));
function answer(yes: boolean) {
  ask.value?.resolve(yes);
  ask.value = null;
}

// ---- navegação ----

async function load(to: string, keepSelection = false) {
  try {
    if (to === TRASH) {
      trash.value = await filesTrashList();
      listing.value = null;
      path.value = TRASH;
    } else {
      const result = await filesList(to, showHidden.value);
      listing.value = result;
      path.value = result.path;
      stamp = await filesStamp(result.path);
    }
    if (!keepSelection) {
      selected.value = new Set();
      anchor.value = -1;
    } else {
      const alive = new Set(keys.value);
      selected.value = new Set([...selected.value].filter((k) => alive.has(k)));
    }
    win.setTitle(`${title(path.value)} · Arquivos`).catch(() => {});
    return true;
  } catch (e) {
    notify(String(e), true);
    return false;
  }
}

async function navigate(to: string) {
  if (to === path.value) return;
  const from = path.value;
  if (await load(to)) {
    if (from) back.value.push(from);
    forward.value = [];
    query.value = "";
    renaming.value = null;
    creating.value = false;
    content.value?.scrollTo({ top: 0 });
  }
}

async function goBack() {
  const to = back.value.pop();
  if (to === undefined) return;
  forward.value.push(path.value);
  await load(to);
}

async function goForward() {
  const to = forward.value.pop();
  if (to === undefined) return;
  back.value.push(path.value);
  await load(to);
}

function goUp() {
  if (inTrash.value) return navigate(home.value);
  const parent = listing.value?.parent;
  if (parent) navigate(parent);
}

function editPath() {
  if (inTrash.value) return;
  pathText.value = path.value;
  editingPath.value = true;
  nextTick(() => (document.querySelector(".pathbar input") as HTMLInputElement | null)?.select());
}

function submitPath() {
  editingPath.value = false;
  const to = pathText.value.trim();
  if (to && to !== path.value) navigate(to);
}

// ---- seleção ----

function select(event: MouseEvent, key: string, index: number) {
  const next = new Set(selected.value);
  if (event.shiftKey && anchor.value >= 0) {
    const [a, b] = [anchor.value, index].sort((x, y) => x - y);
    if (!event.ctrlKey) next.clear();
    keys.value.slice(a, b + 1).forEach((k) => next.add(k));
  } else if (event.ctrlKey) {
    next.has(key) ? next.delete(key) : next.add(key);
    anchor.value = index;
  } else {
    next.clear();
    next.add(key);
    anchor.value = index;
  }
  selected.value = next;
}

function selectOnly(key: string) {
  selected.value = new Set([key]);
  anchor.value = keys.value.indexOf(key);
}

function clearSelection() {
  selected.value = new Set();
  anchor.value = -1;
}

/** Quantas colunas a grade tem agora (para as setas para cima e para baixo). */
function columns() {
  if (view.value === "list") return 1;
  const grid = content.value?.querySelector(".grid") as HTMLElement | null;
  const tile = grid?.querySelector(".tile") as HTMLElement | null;
  if (!grid || !tile) return 1;
  return Math.max(1, Math.round(grid.clientWidth / (tile.offsetWidth + 8)));
}

function move(step: number, extend: boolean) {
  const count = keys.value.length;
  if (!count) return;
  const from = anchor.value < 0 ? (step > 0 ? -1 : count) : anchor.value;
  const to = Math.min(count - 1, Math.max(0, from + step));
  const key = keys.value[to];
  if (extend && anchor.value >= 0) {
    const next = new Set(selected.value);
    next.add(key);
    selected.value = next;
    anchor.value = to;
  } else {
    selectOnly(key);
  }
  nextTick(() => content.value?.querySelector(`[data-index="${to}"]`)?.scrollIntoView({ block: "nearest" }));
}

// ---- abrir ----

function open(entry: FileEntry) {
  if (entry.dir) navigate(entry.path);
  else filesOpen(entry.path).catch((e) => notify(String(e), true));
}

function openSelected() {
  const list = selectedEntries.value;
  if (list.length === 1 && list[0].dir) return navigate(list[0].path);
  list.filter((e) => !e.dir).forEach(open);
}

// ---- operações ----

async function trashSelected() {
  const paths = [...selected.value];
  if (!paths.length || inTrash.value) return;
  try {
    await filesTrash(paths);
    notify(paths.length === 1 ? "Movido para a lixeira." : `${paths.length} itens movidos para a lixeira.`);
    await Promise.all([load(path.value, true), refreshPlaces()]);
  } catch (e) {
    notify(String(e), true);
  }
}

function copy(cut: boolean) {
  const paths = [...selected.value];
  if (!paths.length || inTrash.value) return;
  setClipboard({ paths, cut });
  notify(`${paths.length} ${paths.length === 1 ? "item" : "itens"} para ${cut ? "mover" : "copiar"}. Cole com Ctrl + V.`);
}

async function paste() {
  const clip = clipboard.value;
  if (!clip?.paths.length || inTrash.value || progress.value) return;
  progress.value = { done: 0, total: 0, current: "" };
  try {
    const created = await filesPaste(clip.paths, path.value, clip.cut);
    if (clip.cut) setClipboard(null);
    await load(path.value);
    selected.value = new Set(created);
  } catch (e) {
    notify(String(e), true);
    await load(path.value, true);
  } finally {
    progress.value = null;
  }
}

function startNewFolder() {
  if (inTrash.value || !listing.value?.writable) return;
  const names = new Set(listing.value.entries.map((e) => e.name));
  let name = "Nova pasta";
  for (let n = 2; names.has(name); n++) name = `Nova pasta (${n})`;
  newName.value = name;
  creating.value = true;
  nextTick(() => (content.value?.querySelector(".creating input") as HTMLInputElement | null)?.select());
}

async function finishNewFolder(save: boolean) {
  if (!creating.value) return;
  creating.value = false;
  if (!save || !newName.value.trim()) return;
  try {
    const created = await filesMkdir(path.value, newName.value);
    await load(path.value);
    selectOnly(created);
  } catch (e) {
    notify(String(e), true);
  }
}

function startRename(entry?: FileEntry | null) {
  const target = entry ?? single.value;
  if (!target || inTrash.value) return;
  renaming.value = target.path;
  renameText.value = target.name;
  nextTick(() => {
    const input = content.value?.querySelector(".renaming input") as HTMLInputElement | null;
    if (!input) return;
    input.focus();
    // Seleciona só o nome, sem a extensão.
    const dot = target.dir ? -1 : target.name.lastIndexOf(".");
    input.setSelectionRange(0, dot > 0 ? dot : target.name.length);
  });
}

async function finishRename(save: boolean) {
  const from = renaming.value;
  if (!from) return;
  renaming.value = null;
  const current = entries.value.find((e) => e.path === from);
  if (!save || !current || renameText.value === current.name) return;
  try {
    const to = await filesRename(from, renameText.value);
    await load(path.value);
    selectOnly(to);
  } catch (e) {
    notify(String(e), true);
  }
}

// ---- Connect: "Enviar para" o celular (no PC) ou o PC (no celular) ----

const targets = ref<ConnectTarget[]>([]);
async function loadTargets() {
  targets.value = await connectTargets().catch(() => []);
}
async function share(target: ConnectTarget) {
  const paths = [...selected.value];
  if (!paths.length) return;
  try {
    await connectShare(target, paths);
    notify(`Enviando ${paths.length === 1 ? "1 arquivo" : `${paths.length} arquivos`} para ${target.name}…`);
  } catch (e) {
    notify(String(e), true);
  }
}

async function copyPath() {
  const paths = selected.value.size ? [...selected.value] : [path.value];
  await navigator.clipboard.writeText(paths.join("\n")).catch(() => {});
  notify("Caminho copiado.");
}

// ---- lixeira ----

async function restoreSelected() {
  const ids = [...selected.value];
  if (!ids.length) return;
  try {
    await filesTrashRestore(ids);
    notify(ids.length === 1 ? "Restaurado." : `${ids.length} itens restaurados.`);
  } catch (e) {
    notify(String(e), true);
  }
  await Promise.all([load(TRASH), refreshPlaces()]);
}

async function deleteForever(all: boolean) {
  const ids = all ? null : [...selected.value];
  if (ids && !ids.length) return;
  const what = all ? "todos os itens da lixeira" : ids!.length === 1 ? "este item" : `estes ${ids!.length} itens`;
  if (!(await confirm("Apagar de vez?", `Isso apaga ${what} para sempre. Não dá para desfazer.`, "Apagar", true))) return;
  try {
    await filesTrashDelete(ids);
  } catch (e) {
    notify(String(e), true);
  }
  await Promise.all([load(TRASH), refreshPlaces()]);
}

// ---- discos ----

async function refreshPlaces() {
  places.value = await filesPlaces();
}

const driveName = (d: Drive) => d.label || `Disco de ${formatSize(d.size)}`;

async function openDrive(d: Drive) {
  try {
    const mountpoint = d.mountpoint ?? (await filesMount(d.device));
    await refreshPlaces();
    navigate(mountpoint);
  } catch (e) {
    notify(String(e), true);
  }
}

async function eject(d: Drive) {
  try {
    if (d.mountpoint && (path.value === d.mountpoint || path.value.startsWith(d.mountpoint + "/"))) await navigate(home.value);
    await filesUnmount(d.device);
    notify(`${driveName(d)} pode ser removido.`);
  } catch (e) {
    notify(String(e), true);
  }
  await refreshPlaces();
}

// ---- menu de contexto ----

function openMenu(event: MouseEvent, entry?: FileEntry, trashItem?: TrashItem) {
  event.preventDefault();
  if (entry && !selected.value.has(entry.path)) selectOnly(entry.path);
  if (trashItem && !selected.value.has(trashItem.id)) selectOnly(trashItem.id);
  if (!entry && !trashItem) clearSelection();
  const n = selected.value.size;
  let items: MenuItem[];
  if (inTrash.value) {
    items = n
      ? [
          { label: "Restaurar", icon: "restore", action: restoreSelected },
          { label: "Apagar de vez", icon: "trash", danger: true, action: () => deleteForever(false) },
        ]
      : [{ label: "Esvaziar lixeira", icon: "trash", danger: true, disabled: !trash.value.length, action: () => deleteForever(true) }];
  } else if (entry) {
    items = [
      { label: entry.dir ? "Abrir" : "Abrir", icon: entry.dir ? "folder" : "file", hint: "Enter", action: openSelected },
      ...(entry.dir && n === 1 ? [{ label: "Abrir em nova janela", icon: "window", action: () => filesWindow(entry.path) }] : []),
      {},
      { label: "Copiar", icon: "copy", hint: "Ctrl+C", action: () => copy(false) },
      { label: "Recortar", icon: "cut", hint: "Ctrl+X", action: () => copy(true) },
      ...(entry.dir && clipboard.value?.paths.length ? [{ label: "Colar dentro", icon: "paste", action: () => pasteInto(entry.path) }] : []),
      { label: "Renomear", icon: "edit", hint: "F2", disabled: n !== 1, action: () => startRename(entry) },
      { label: "Copiar caminho", icon: "link", action: copyPath },
      ...(targets.value.length && !entry.dir ? [{}] : []),
      ...(entry.dir ? [] : targets.value.map((t) => ({ label: `Enviar para ${t.name}`, icon: "send", action: () => share(t) }))),
      {},
      { label: "Mover para a lixeira", icon: "trash", hint: "Delete", danger: true, action: trashSelected },
      {},
      { label: "Propriedades", icon: "info", action: () => (showDetails.value = true) },
    ];
  } else {
    items = [
      { label: "Nova pasta", icon: "folder-plus", hint: "Ctrl+Shift+N", disabled: !listing.value?.writable, action: startNewFolder },
      { label: "Colar", icon: "paste", hint: "Ctrl+V", disabled: !clipboard.value?.paths.length, action: paste },
      {},
      { label: "Abrir terminal aqui", icon: "terminal", action: () => filesTerminal(path.value) },
      { label: "Nova janela aqui", icon: "window", hint: "Ctrl+N", action: () => filesWindow(path.value) },
      { label: showHidden.value ? "Esconder ocultos" : "Mostrar ocultos", icon: "eye", hint: "Ctrl+H", action: toggleHidden },
    ];
  }
  // Mantém o menu dentro da janela.
  const width = 240;
  const height = items.reduce((h, i) => h + (i.label ? 34 : 9), 12);
  menu.value = {
    x: Math.min(event.clientX, window.innerWidth - width - 8),
    y: Math.min(event.clientY, window.innerHeight - height - 8),
    items,
  };
}

async function pasteInto(dir: string) {
  const clip = clipboard.value;
  if (!clip) return;
  progress.value = { done: 0, total: 0, current: "" };
  try {
    await filesPaste(clip.paths, dir, clip.cut);
    if (clip.cut) setClipboard(null);
    await load(path.value, true);
  } catch (e) {
    notify(String(e), true);
  } finally {
    progress.value = null;
  }
}

function runMenu(item: MenuItem) {
  menu.value = null;
  if (!item.disabled) item.action?.();
}

function toggleHidden() {
  showHidden.value = !showHidden.value;
}

function setSort(key: SortKey) {
  sort.value = sort.value.key === key ? { key, asc: !sort.value.asc } : { key, asc: key === "name" || key === "kind" };
}

// ---- teclado ----

let typed = "";
let typedTimer: number | undefined;

function onKey(event: KeyboardEvent) {
  const target = event.target as HTMLElement;
  const typing = target.tagName === "INPUT";
  if (event.key === "Escape") {
    if (menu.value) menu.value = null;
    else if (ask.value) answer(false);
    else if (!typing) clearSelection();
    return;
  }
  if (ask.value) return;
  const ctrl = event.ctrlKey;
  const key = event.key.toLowerCase();
  if (ctrl && key === "l") {
    event.preventDefault();
    return editPath();
  }
  if (ctrl && key === "f") {
    event.preventDefault();
    return filterInput.value?.focus();
  }
  if (typing) return;

  const actions: Record<string, () => void> = {
    "ctrl+a": () => (selected.value = new Set(keys.value)),
    "ctrl+c": () => copy(false),
    "ctrl+x": () => copy(true),
    "ctrl+v": paste,
    "ctrl+h": toggleHidden,
    "ctrl+n": () => filesWindow(inTrash.value ? undefined : path.value),
    "ctrl+shift+n": startNewFolder,
    "ctrl+w": () => win.close(),
    f2: () => startRename(),
    delete: () => (inTrash.value ? deleteForever(false) : trashSelected()),
    enter: () => (inTrash.value ? undefined : openSelected()),
    backspace: goBack,
    "alt+arrowleft": goBack,
    "alt+arrowright": goForward,
    "alt+arrowup": goUp,
  };
  const combo = `${ctrl ? "ctrl+" : ""}${event.shiftKey && ctrl ? "shift+" : ""}${event.altKey ? "alt+" : ""}${key}`;
  if (actions[combo]) {
    event.preventDefault();
    actions[combo]();
    return;
  }
  const arrows: Record<string, number> = { arrowright: 1, arrowleft: -1, arrowdown: columns(), arrowup: -columns() };
  if (!ctrl && !event.altKey && key in arrows) {
    event.preventDefault();
    if (view.value === "list" && (key === "arrowright" || key === "arrowleft")) return;
    move(arrows[key], event.shiftKey);
    return;
  }
  // Digitar o começo de um nome pula até ele.
  if (event.key.length === 1 && !ctrl && !event.altKey && event.key !== " ") {
    typed += fold(event.key);
    window.clearTimeout(typedTimer);
    typedTimer = window.setTimeout(() => (typed = ""), 800);
    const names = inTrash.value ? trashShown.value.map((t) => t.name) : entries.value.map((e) => e.name);
    const index = names.findIndex((n) => fold(n).startsWith(typed));
    if (index >= 0) {
      selectOnly(keys.value[index]);
      nextTick(() => content.value?.querySelector(`[data-index="${index}"]`)?.scrollIntoView({ block: "nearest" }));
    }
  }
}

// ---- detalhes ----

let infoTimer: number | undefined;
watch([single, showDetails, path], () => {
  info.value = null;
  window.clearTimeout(infoTimer);
  if (!showDetails.value || inTrash.value) return;
  const target = single.value?.path ?? path.value;
  if (!target) return;
  infoTimer = window.setTimeout(async () => {
    info.value = await filesInfo(target).catch(() => null);
  }, 200);
});

watch(view, (v) => store.set("view", v));
watch(showDetails, (v) => store.set("details", v ? "1" : "0"));
watch(showHidden, (v) => {
  store.set("hidden", v ? "1" : "0");
  if (!inTrash.value) load(path.value, true);
});

// ---- início ----

const stops: (() => void)[] = [];
let poll: number | undefined;
let placesPoll: number | undefined;

onMounted(async () => {
  loadTargets();
  stops.push(await on("lumna://connect", loadTargets));
  stops.push(await on("lumna://remote-devices", loadTargets));
  window.addEventListener("keydown", onKey);
  window.addEventListener("storage", onStorage);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(() => window.removeEventListener("storage", onStorage));
  stops.push(
    await on<{ done: number; total: number; current: string }>("lumna://files-progress", (p) => {
      if (progress.value) progress.value = p;
    }),
  );
  await refreshPlaces();
  const start = decodeURIComponent(location.hash.slice(1)) || home.value || "/";
  if (!(await load(start))) await load(home.value || "/");
  // A pasta mudou por fora (outro programa, outra janela): relê.
  poll = window.setInterval(async () => {
    if (inTrash.value || renaming.value || creating.value || !path.value) return;
    const now = await filesStamp(path.value).catch(() => 0);
    if (now && now !== stamp) load(path.value, true);
  }, 1500);
  // Pendrive colocado ou tirado.
  placesPoll = window.setInterval(refreshPlaces, 4000);
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  window.clearInterval(poll);
  window.clearInterval(placesPoll);
});
</script>

<template>
  <div class="files" @click="menu = null">
    <WindowBar title="Arquivos" />

    <div class="body">
      <!-- Lugares, discos e lixeira -->
      <nav class="sidebar">
        <p class="section">Lugares</p>
        <button
          v-for="p in places?.places"
          :key="p.path"
          :class="{ active: path === p.path }"
          @click="navigate(p.path)"
        >
          <Icon :name="p.icon" :size="17" /> <span>{{ p.name }}</span>
        </button>
        <button :class="{ active: path === '/' }" @click="navigate('/')">
          <Icon name="computer" :size="17" /> <span>Computador</span>
        </button>

        <template v-if="places?.drives.length">
          <p class="section">Discos</p>
          <div v-for="d in places.drives" :key="d.device" class="drive" :class="{ active: d.mountpoint && path.startsWith(d.mountpoint) }">
            <button class="drive-open" :title="d.device" @click="openDrive(d)">
              <Icon :name="d.removable ? 'usb' : 'drive'" :size="17" />
              <span>{{ driveName(d) }}</span>
            </button>
            <button v-if="d.mountpoint" class="eject" title="Ejetar" @click="eject(d)"><Icon name="eject" :size="14" /></button>
          </div>
        </template>

        <div class="spacer" />
        <button class="trash" :class="{ active: inTrash }" @click="navigate(TRASH)">
          <Icon name="trash" :size="17" /> <span>Lixeira</span>
          <span v-if="places?.trash" class="count">{{ places.trash }}</span>
        </button>
      </nav>

      <section class="main">
        <!-- Barra: voltar/avançar/subir, caminho, filtro e visualização -->
        <header class="toolbar">
          <div class="nav">
            <button :disabled="!back.length" title="Voltar (Alt + ←)" @click="goBack"><Icon name="back" :size="18" /></button>
            <button :disabled="!forward.length" title="Avançar (Alt + →)" @click="goForward"><Icon name="forward" :size="18" /></button>
            <button :disabled="!inTrash && !listing?.parent" title="Pasta acima (Alt + ↑)" @click="goUp"><Icon name="up" :size="18" /></button>
          </div>

          <div class="pathbar" @click.self="editPath">
            <input
              v-if="editingPath"
              v-model="pathText"
              spellcheck="false"
              @keydown.enter.prevent="submitPath"
              @keydown.esc.prevent="editingPath = false"
              @blur="editingPath = false"
            />
            <template v-else>
              <template v-for="(c, i) in crumbList" :key="c.path">
                <span v-if="i" class="sep">›</span>
                <button class="crumb" :class="{ last: i === crumbList.length - 1 }" @click="navigate(c.path)">
                  <Icon v-if="c.home" name="home" :size="14" />
                  {{ c.name }}
                </button>
              </template>
            </template>
          </div>

          <label class="filter">
            <Icon name="search" :size="15" />
            <input ref="filterInput" v-model="query" placeholder="Filtrar" spellcheck="false" @keydown.esc="query = ''" />
          </label>

          <div class="views">
            <button :class="{ on: view === 'grid' }" title="Grade" @click="view = 'grid'"><Icon name="grid" :size="16" /></button>
            <button :class="{ on: view === 'list' }" title="Lista" @click="view = 'list'"><Icon name="list" :size="16" /></button>
            <button :class="{ on: showDetails }" title="Detalhes" @click="showDetails = !showDetails"><Icon name="info" :size="16" /></button>
          </div>
        </header>

        <!-- Ações da lixeira -->
        <div v-if="inTrash" class="trashbar">
          <span>Itens apagados ficam aqui até você esvaziar a lixeira.</span>
          <button class="btn" :disabled="!selected.size" @click="restoreSelected"><Icon name="restore" :size="14" /> Restaurar</button>
          <button class="btn danger" :disabled="!trash.length" @click="deleteForever(true)"><Icon name="trash" :size="14" /> Esvaziar</button>
        </div>

        <div class="split">
          <div ref="content" class="content" :class="`view-${view}`" @click.self="clearSelection" @contextmenu.self="openMenu($event)">
            <!-- Lixeira -->
            <template v-if="inTrash">
              <p v-if="!trashShown.length" class="empty"><Icon name="trash" :size="40" /> A lixeira está vazia.</p>
              <div v-else class="rows">
                <div class="row head">
                  <span class="c-name">Nome</span><span class="c-where">Estava em</span><span class="c-date">Apagado</span>
                </div>
                <div
                  v-for="(t, i) in trashShown"
                  :key="t.id"
                  :data-index="i"
                  class="row"
                  :class="{ selected: selected.has(t.id) }"
                  @click.stop="select($event, t.id, i)"
                  @contextmenu.stop="openMenu($event, undefined, t)"
                >
                  <span class="c-name"><FileArt :path="''" :kind="t.kind" :size="0" /> {{ t.name }}</span>
                  <span class="c-where">{{ t.original.replace(home, "~").split("/").slice(0, -1).join("/") || "/" }}</span>
                  <span class="c-date">{{ t.deleted.replace("T", " ").slice(0, 16) }}</span>
                </div>
              </div>
            </template>

            <!-- Pasta: grade -->
            <template v-else-if="view === 'grid'">
              <p v-if="!entries.length && !creating" class="empty">
                <Icon name="folder" :size="40" /> {{ query ? `Nada com "${query}" aqui.` : "Pasta vazia." }}
              </p>
              <div class="grid" @click.self="clearSelection" @contextmenu.self="openMenu($event)">
                <div v-if="creating" class="tile creating selected">
                  <FileArt path="" kind="folder" :size="0" big />
                  <input
                    v-model="newName"
                    spellcheck="false"
                    @keydown.enter.prevent="finishNewFolder(true)"
                    @keydown.esc.prevent="finishNewFolder(false)"
                    @blur="finishNewFolder(true)"
                  />
                </div>
                <div
                  v-for="(e, i) in entries"
                  :key="e.path"
                  :data-index="i"
                  class="tile"
                  :class="{ selected: selected.has(e.path), renaming: renaming === e.path, hidden: e.hidden, cut: clipboard?.cut && clipboard.paths.includes(e.path) }"
                  :title="e.name"
                  @click.stop="select($event, e.path, i)"
                  @dblclick="open(e)"
                  @contextmenu.stop="openMenu($event, e)"
                >
                  <FileArt :path="e.path" :kind="e.kind" :size="e.size" big />
                  <input
                    v-if="renaming === e.path"
                    v-model="renameText"
                    spellcheck="false"
                    @click.stop
                    @dblclick.stop
                    @keydown.enter.prevent="finishRename(true)"
                    @keydown.esc.prevent="finishRename(false)"
                    @blur="finishRename(true)"
                  />
                  <span v-else class="name">{{ e.name }}</span>
                  <Icon v-if="e.link" name="link" :size="11" class="link" />
                </div>
              </div>
            </template>

            <!-- Pasta: lista -->
            <template v-else>
              <div class="rows">
                <div class="row head">
                  <button class="c-name" @click="setSort('name')">Nome <i v-if="sort.key === 'name'">{{ sort.asc ? "↑" : "↓" }}</i></button>
                  <button class="c-date" @click="setSort('modified')">Modificado <i v-if="sort.key === 'modified'">{{ sort.asc ? "↑" : "↓" }}</i></button>
                  <button class="c-size" @click="setSort('size')">Tamanho <i v-if="sort.key === 'size'">{{ sort.asc ? "↑" : "↓" }}</i></button>
                  <button class="c-kind" @click="setSort('kind')">Tipo <i v-if="sort.key === 'kind'">{{ sort.asc ? "↑" : "↓" }}</i></button>
                </div>
                <div v-if="creating" class="row creating selected">
                  <span class="c-name">
                    <FileArt path="" kind="folder" :size="0" />
                    <input
                      v-model="newName"
                      spellcheck="false"
                      @keydown.enter.prevent="finishNewFolder(true)"
                      @keydown.esc.prevent="finishNewFolder(false)"
                      @blur="finishNewFolder(true)"
                    />
                  </span>
                </div>
                <div
                  v-for="(e, i) in entries"
                  :key="e.path"
                  :data-index="i"
                  class="row"
                  :class="{ selected: selected.has(e.path), renaming: renaming === e.path, hidden: e.hidden, cut: clipboard?.cut && clipboard.paths.includes(e.path) }"
                  @click.stop="select($event, e.path, i)"
                  @dblclick="open(e)"
                  @contextmenu.stop="openMenu($event, e)"
                >
                  <span class="c-name">
                    <FileArt :path="e.path" :kind="e.kind" :size="e.size" />
                    <input
                      v-if="renaming === e.path"
                      v-model="renameText"
                      spellcheck="false"
                      @click.stop
                      @dblclick.stop
                      @keydown.enter.prevent="finishRename(true)"
                      @keydown.esc.prevent="finishRename(false)"
                      @blur="finishRename(true)"
                    />
                    <span v-else class="name">{{ e.name }}</span>
                  </span>
                  <span class="c-date">{{ formatDate(e.modified) }}</span>
                  <span class="c-size">{{ e.dir ? "-" : formatSize(e.size) }}</span>
                  <span class="c-kind">{{ KIND_LABEL[e.kind] }}</span>
                </div>
              </div>
              <p v-if="!entries.length && !creating" class="empty">
                <Icon name="folder" :size="40" /> {{ query ? `Nada com "${query}" aqui.` : "Pasta vazia." }}
              </p>
            </template>
          </div>

          <!-- Detalhes -->
          <aside v-if="showDetails && !inTrash" class="details">
            <template v-if="single">
              <div class="preview"><FileArt :path="single.path" :kind="single.kind" :size="single.size" big /></div>
              <h3>{{ single.name }}</h3>
              <p class="kind">{{ KIND_LABEL[single.kind] }}</p>
            </template>
            <template v-else>
              <div class="preview"><FileArt path="" kind="folder" :size="0" big /></div>
              <h3>{{ selected.size ? `${selected.size} itens selecionados` : title(path) }}</h3>
              <p class="kind">{{ selected.size ? formatSize(selectedSize) : `${entries.length} itens` }}</p>
            </template>
            <dl v-if="info && (single || !selected.size)">
              <dt>Tamanho</dt>
              <dd>{{ formatSize(info.size) }}<template v-if="info.items"> · {{ info.items }} itens</template></dd>
              <dt>Modificado</dt>
              <dd>{{ formatDate(info.modified) }}</dd>
              <dt>Permissões</dt>
              <dd class="mono">{{ info.mode }} · {{ info.owner }}</dd>
              <dt>Local</dt>
              <dd class="path">{{ info.path }}</dd>
            </dl>
            <p v-else-if="single || !selected.size" class="kind">Calculando…</p>
          </aside>
        </div>

        <!-- Rodapé: contagem, área de transferência, andamento -->
        <footer class="status">
          <span v-if="inTrash">{{ trash.length }} {{ trash.length === 1 ? "item" : "itens" }} na lixeira</span>
          <span v-else-if="selected.size">
            {{ selected.size }} {{ selected.size === 1 ? "selecionado" : "selecionados" }}
            <template v-if="selectedSize"> · {{ formatSize(selectedSize) }}</template>
          </span>
          <span v-else>{{ entries.length }} {{ entries.length === 1 ? "item" : "itens" }}</span>
          <span v-if="listing && !listing.writable" class="ro">Só leitura</span>
          <span class="grow" />
          <span v-if="progress" class="progress">
            <span class="bar"><span :style="{ width: progress.total ? `${(progress.done / progress.total) * 100}%` : '30%' }" /></span>
            {{ progress.current ? `Copiando ${progress.current}` : "Preparando…" }}
          </span>
          <span v-else-if="clipboard?.paths.length" class="clip">
            {{ clipboard.paths.length }} para {{ clipboard.cut ? "mover" : "colar" }}
            <button @click="setClipboard(null)" title="Esquecer"><Icon name="close" :size="12" /></button>
          </span>
        </footer>
      </section>
    </div>

    <!-- Menu de contexto próprio -->
    <div v-if="menu" class="menu" :style="{ left: `${menu.x}px`, top: `${menu.y}px` }" @click.stop @contextmenu.prevent>
      <template v-for="(item, i) in menu.items" :key="i">
        <div v-if="!item.label" class="divider" />
        <button v-else :disabled="item.disabled" :class="{ danger: item.danger }" @click="runMenu(item)">
          <Icon :name="item.icon ?? 'file'" :size="15" />
          <span>{{ item.label }}</span>
          <kbd v-if="item.hint">{{ item.hint }}</kbd>
        </button>
      </template>
    </div>

    <transition name="toast">
      <div v-if="toast" class="toast" :class="{ error: toast.error }">{{ toast.text }}</div>
    </transition>

    <div v-if="ask" class="veil" @click.self="answer(false)">
      <div class="dialog">
        <h3>{{ ask.title }}</h3>
        <p>{{ ask.text }}</p>
        <div class="buttons">
          <button class="btn" @click="answer(false)">Cancelar</button>
          <button class="btn" :class="ask.danger ? 'danger-solid' : 'primary'" @click="answer(true)">{{ ask.ok }}</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.files {
  height: var(--app-h, 100vh);
  display: flex;
  flex-direction: column;
  background: rgba(16, 12, 30, 0.95);
}
.body {
  flex: 1;
  min-height: 0;
  display: flex;
}
button {
  font: inherit;
  color: inherit;
}

/* ---- barra lateral ---- */
.sidebar {
  width: 210px;
  flex: none;
  display: flex;
  flex-direction: column;
  gap: 2px;
  padding: 10px 10px 12px;
  border-right: 1px solid var(--lumna-line);
  overflow-y: auto;
}
.section {
  margin: 12px 10px 6px;
  font-size: 11px;
  font-weight: 600;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  color: var(--lumna-muted);
}
.sidebar > button,
.drive-open {
  height: 36px;
  display: flex;
  align-items: center;
  gap: 11px;
  padding: 0 10px;
  border: 0;
  border-radius: 10px;
  background: transparent;
  color: var(--lumna-muted);
  font-size: 13.5px;
  text-align: left;
  min-width: 0;
}
.sidebar > button span,
.drive-open span {
  flex: 1;
  min-width: 0;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.sidebar > button:hover,
.drive:hover {
  background: rgba(167, 139, 250, 0.1);
  color: var(--lumna-text);
}
.sidebar > button.active,
.drive.active {
  background: rgba(139, 92, 246, 0.22);
  color: #fff;
}
.drive {
  display: flex;
  align-items: center;
  border-radius: 10px;
  color: var(--lumna-muted);
}
.drive-open {
  flex: 1;
  color: inherit;
}
.drive-open:hover {
  background: transparent;
}
.eject {
  width: 28px;
  height: 28px;
  margin-right: 4px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--lumna-muted);
}
.eject:hover {
  background: rgba(167, 139, 250, 0.22);
  color: #fff;
}
.spacer {
  flex: 1;
  min-height: 12px;
}
.count {
  flex: none !important;
  font-size: 11px;
  padding: 1px 7px;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.08);
}

/* ---- barra de ferramentas ---- */
.main {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.toolbar {
  height: 52px;
  flex: none;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 14px;
  border-bottom: 1px solid var(--lumna-line);
}
.nav,
.views {
  display: flex;
  gap: 2px;
}
.nav button,
.views button {
  width: 34px;
  height: 32px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 9px;
  background: transparent;
  color: var(--lumna-muted);
}
.nav button:hover:not(:disabled),
.views button:hover {
  background: rgba(167, 139, 250, 0.14);
  color: var(--lumna-text);
}
.nav button:disabled {
  opacity: 0.3;
}
.views button.on {
  background: rgba(139, 92, 246, 0.25);
  color: #fff;
}
.pathbar {
  flex: 1;
  min-width: 0;
  height: 34px;
  display: flex;
  align-items: center;
  gap: 2px;
  padding: 0 6px;
  border-radius: 11px;
  background: rgba(0, 0, 0, 0.25);
  border: 1px solid transparent;
  overflow: hidden;
  cursor: text;
}
.pathbar:hover {
  border-color: var(--lumna-line);
}
.pathbar input {
  flex: 1;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
  padding: 0 6px;
}
.crumb {
  flex: none;
  max-width: 220px;
  height: 26px;
  display: flex;
  align-items: center;
  gap: 5px;
  padding: 0 8px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--lumna-muted);
  font-size: 13px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  cursor: pointer;
}
.crumb:hover {
  background: rgba(167, 139, 250, 0.16);
  color: var(--lumna-text);
}
.crumb.last {
  color: #fff;
  font-weight: 600;
}
.sep {
  color: rgba(165, 156, 201, 0.5);
}
.filter {
  width: 180px;
  height: 34px;
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 0 10px;
  border-radius: 11px;
  background: rgba(0, 0, 0, 0.25);
  color: var(--lumna-muted);
}
.filter:focus-within {
  box-shadow: 0 0 0 1px var(--lumna-violet);
}
.filter input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}

.trashbar {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 10px 16px;
  font-size: 12.5px;
  color: var(--lumna-muted);
  border-bottom: 1px solid var(--lumna-line);
}
.trashbar span {
  flex: 1;
}

/* ---- conteúdo ---- */
.split {
  flex: 1;
  min-height: 0;
  display: flex;
}
.content {
  flex: 1;
  min-width: 0;
  overflow-y: auto;
  padding: 14px;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(112px, 1fr));
  gap: 8px;
  align-content: start;
  min-height: calc(100% - 8px);
}
.tile {
  position: relative;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 8px;
  padding: 12px 6px 10px;
  border-radius: 14px;
  border: 1px solid transparent;
  text-align: center;
  transition: background-color 120ms ease;
}
.tile:hover {
  background: rgba(167, 139, 250, 0.08);
}
.tile.selected {
  background: rgba(139, 92, 246, 0.22);
  border-color: rgba(167, 139, 250, 0.5);
}
.tile .name {
  max-width: 100%;
  font-size: 12.5px;
  line-height: 1.3;
  overflow-wrap: anywhere;
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}
.tile.selected .name {
  -webkit-line-clamp: 4;
}
.tile input,
.row input {
  width: 100%;
  box-sizing: border-box;
  padding: 3px 6px;
  border: 1px solid var(--lumna-violet);
  border-radius: 6px;
  outline: 0;
  background: rgba(0, 0, 0, 0.5);
  color: #fff;
  font: inherit;
  font-size: 12.5px;
  text-align: center;
}
.row input {
  text-align: left;
  max-width: 360px;
}
.hidden,
.cut {
  opacity: 0.55;
}
.link {
  position: absolute;
  top: 10px;
  right: 18px;
  color: var(--lumna-violet-soft);
}

.rows {
  font-size: 13px;
}
.row {
  display: grid;
  grid-template-columns: minmax(0, 1fr) 150px 100px 150px;
  align-items: center;
  gap: 12px;
  height: 40px;
  padding: 0 10px;
  border-radius: 10px;
  border: 1px solid transparent;
}
.rows .row:not(.head):hover {
  background: rgba(167, 139, 250, 0.07);
}
.row.selected {
  background: rgba(139, 92, 246, 0.22) !important;
  border-color: rgba(167, 139, 250, 0.45);
}
.row.head {
  height: 32px;
  color: var(--lumna-muted);
  font-size: 12px;
  position: sticky;
  top: -14px;
  background: rgba(16, 12, 30, 0.98);
  z-index: 1;
}
.row.head button {
  border: 0;
  background: transparent;
  color: inherit;
  font-size: 12px;
  text-align: left;
  padding: 0;
  cursor: pointer;
}
.row.head i {
  font-style: normal;
  color: var(--lumna-violet-soft);
}
.c-name {
  display: flex;
  align-items: center;
  gap: 10px;
  min-width: 0;
}
.c-name .name {
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.c-date,
.c-size,
.c-kind,
.c-where {
  color: var(--lumna-muted);
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
  font-variant-numeric: tabular-nums;
}
.content .rows .row:has(.c-where) {
  grid-template-columns: minmax(0, 1fr) minmax(0, 1fr) 150px;
}
.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  margin: 70px 0 0;
  color: var(--lumna-muted);
  font-size: 14px;
  pointer-events: none;
}

/* ---- detalhes ---- */
.details {
  width: 260px;
  flex: none;
  padding: 22px 18px;
  border-left: 1px solid var(--lumna-line);
  overflow-y: auto;
  user-select: text;
}
.preview {
  display: grid;
  place-items: center;
  height: 120px;
  margin-bottom: 14px;
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.03);
}
.preview :deep(.art.big.photo) {
  width: 200px;
  height: 112px;
}
.details h3 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  overflow-wrap: anywhere;
}
.details .kind {
  margin: 4px 0 16px;
  font-size: 12.5px;
  color: var(--lumna-muted);
}
.details dl {
  display: grid;
  grid-template-columns: 1fr;
  gap: 2px;
  margin: 0;
  font-size: 12.5px;
}
.details dt {
  margin-top: 10px;
  color: var(--lumna-muted);
  font-size: 11.5px;
}
.details dd {
  margin: 0;
  overflow-wrap: anywhere;
}
.mono {
  font-family: ui-monospace, monospace;
}
.path {
  font-size: 12px;
  color: var(--lumna-violet-soft);
}

/* ---- rodapé ---- */
.status {
  height: 32px;
  flex: none;
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 0 16px;
  border-top: 1px solid var(--lumna-line);
  font-size: 12px;
  color: var(--lumna-muted);
}
.grow {
  flex: 1;
}
.ro {
  padding: 1px 8px;
  border-radius: 999px;
  background: rgba(251, 191, 36, 0.14);
  color: #fcd34d;
}
.progress {
  display: flex;
  align-items: center;
  gap: 10px;
  max-width: 50%;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.bar {
  width: 140px;
  height: 6px;
  flex: none;
  border-radius: 3px;
  background: rgba(255, 255, 255, 0.1);
  overflow: hidden;
}
.bar span {
  display: block;
  height: 100%;
  background: linear-gradient(90deg, #a78bfa, #7c3aed);
  transition: width 150ms linear;
}
.clip {
  display: flex;
  align-items: center;
  gap: 6px;
  color: var(--lumna-violet-soft);
}
.clip button {
  display: grid;
  place-items: center;
  width: 20px;
  height: 20px;
  border: 0;
  border-radius: 6px;
  background: transparent;
}
.clip button:hover {
  background: rgba(255, 255, 255, 0.1);
}

/* ---- menu, avisos, diálogo ---- */
.menu {
  position: fixed;
  z-index: 10;
  width: 240px;
  padding: 6px;
  border-radius: 14px;
  background: rgba(26, 20, 51, 0.98);
  border: 1px solid var(--lumna-line);
  box-shadow: 0 18px 50px rgba(5, 2, 15, 0.6);
  animation: pop 120ms ease;
}
@keyframes pop {
  from {
    opacity: 0;
    transform: scale(0.97);
  }
}
.menu button {
  width: 100%;
  height: 34px;
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 0 10px;
  border: 0;
  border-radius: 9px;
  background: transparent;
  font-size: 13px;
  text-align: left;
}
.menu button span {
  flex: 1;
}
.menu button:hover:not(:disabled) {
  background: rgba(139, 92, 246, 0.25);
}
.menu button.danger:hover:not(:disabled) {
  background: rgba(239, 68, 68, 0.7);
}
.menu button:disabled {
  opacity: 0.4;
}
.menu kbd {
  font: inherit;
  font-size: 11px;
  color: var(--lumna-muted);
}
.divider {
  height: 1px;
  margin: 4px 6px;
  background: var(--lumna-line);
}

.toast {
  position: fixed;
  left: 50%;
  bottom: 48px;
  transform: translateX(-50%);
  max-width: 70%;
  padding: 10px 16px;
  border-radius: 12px;
  background: rgba(30, 24, 58, 0.97);
  border: 1px solid var(--lumna-line);
  box-shadow: 0 14px 40px rgba(5, 2, 15, 0.5);
  font-size: 13px;
  white-space: pre-line;
}
.toast.error {
  border-color: rgba(248, 113, 113, 0.6);
  color: #fecaca;
}
.toast-enter-active,
.toast-leave-active {
  transition: opacity 180ms ease, transform 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.toast-enter-from,
.toast-leave-to {
  opacity: 0;
  transform: translate(-50%, 8px);
}

.btn {
  height: 32px;
  padding: 0 14px;
  border: 0;
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.08);
  font-size: 13px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.btn:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.22);
}
.btn.primary {
  background: var(--lumna-violet);
  color: #fff;
}
.btn.danger:hover:not(:disabled),
.btn.danger-solid {
  background: rgba(239, 68, 68, 0.75);
  color: #fff;
}
.btn:disabled {
  opacity: 0.45;
}
.veil {
  position: fixed;
  inset: 0;
  z-index: 20;
  display: grid;
  place-items: center;
  background: rgba(5, 2, 15, 0.55);
}
.dialog {
  width: 400px;
  padding: 24px;
  border-radius: 20px;
  background: #1a1433;
  border: 1px solid var(--lumna-line);
  box-shadow: 0 24px 60px rgba(5, 2, 15, 0.6);
}
.dialog h3 {
  margin: 0;
  font-size: 16px;
}
.dialog p {
  margin: 10px 0 20px;
  font-size: 13px;
  line-height: 1.55;
  color: var(--lumna-muted);
}
.buttons {
  display: flex;
  justify-content: flex-end;
  gap: 8px;
}
</style>
