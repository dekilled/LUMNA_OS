<script setup lang="ts">
// Arquivos no celular (tela estreita), como no esboço: abas (Meu espaço e os lugares), trilha,
// "Pastas" e "Arquivos" em lista. Tocar num arquivo abre o cartão dele (prévia e Abrir / Mover /
// Mais); tocar de novo abre. Mover e copiar: escolhe, navega até a pasta e "Mover aqui".
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "../Icon.vue";
import { crumbs, formatDate, formatSize, KIND_ICON, KIND_LABEL } from "./format";
import {
  connectShare,
  connectTargets,
  convertFileSrc,
  filesInfo,
  filesList,
  filesMkdir,
  filesOpen,
  filesPaste,
  filesPlaces,
  filesRename,
  filesTrash,
  on,
  type ConnectTarget,
  type FileEntry,
  type FileInfo,
  type Listing,
  type Places,
} from "../../system";

const home = ref("");
const places = ref<Places | null>(null);
const path = ref("");
const listing = ref<Listing | null>(null);
const counts = ref<Record<string, number>>({});
const open = ref<string | null>(null);
const searching = ref(false);
const query = ref("");
const toast = ref<{ text: string; error?: boolean } | null>(null);
const sheet = ref<null | "more" | "new" | "rename" | "folder-name" | "info">(null);
const target = ref<FileEntry | null>(null);
const nameInput = ref("");
const info = ref<FileInfo | null>(null);
/** Movendo/copiando: o que e pra onde (a pasta aberta na hora de "Mover aqui"). */
const carrying = ref<{ paths: string[]; name: string; cut: boolean } | null>(null);
const targets = ref<ConnectTarget[]>([]);

const tabs = computed(() => {
  const list = [{ name: "Meu espaço", path: home.value }];
  for (const p of places.value?.places ?? []) if (p.path !== home.value) list.push({ name: p.name, path: p.path });
  return list;
});
// a aba é o lugar mais "fundo" que contém a pasta aberta
const activeTab = computed(() => {
  let best = "";
  for (const t of tabs.value) if ((path.value === t.path || path.value.startsWith(`${t.path}/`)) && t.path.length > best.length) best = t.path;
  return best;
});
const visible = computed(() => {
  const q = query.value.trim().toLowerCase();
  return (listing.value?.entries ?? []).filter((e) => !e.hidden && (!q || e.name.toLowerCase().includes(q)));
});
const folders = computed(() => visible.value.filter((e) => e.dir));
const files = computed(() => visible.value.filter((e) => !e.dir));
const trail = computed(() => (home.value ? crumbs(path.value, home.value) : []));
const title = computed(() => (path.value === home.value ? "Arquivos" : (path.value.split("/").pop() ?? "Arquivos")));

function notify(text: string, error = false) {
  toast.value = { text, error };
  window.setTimeout(() => (toast.value = null), error ? 5000 : 2500);
}
async function load(p: string) {
  try {
    listing.value = await filesList(p, false);
    path.value = listing.value.path;
    open.value = null;
    loadCounts();
  } catch (e) {
    notify(String(e), true);
  }
}
async function loadCounts() {
  const want = folders.value.filter((f) => counts.value[f.path] == null).slice(0, 40);
  for (const f of want) {
    const i = await filesInfo(f.path).catch(() => null);
    if (i) counts.value = { ...counts.value, [f.path]: i.items };
  }
}
const reload = () => load(path.value);
function up() {
  if (listing.value?.parent && path.value !== activeTab.value) load(listing.value.parent);
  else if (listing.value?.parent && path.value !== home.value) load(home.value);
}

function tap(e: FileEntry) {
  if (e.dir) {
    query.value = "";
    load(e.path);
  } else if (open.value === e.path) filesOpen(e.path).catch((err) => notify(String(err), true));
  else open.value = e.path;
}
function more(e: FileEntry) {
  target.value = e;
  sheet.value = "more";
  connectTargets().then((t) => (targets.value = t)).catch(() => (targets.value = []));
}
// segurar uma pasta abre o "Mais" dela
let holdTimer = 0;
let held = false;
function holdStart(e: FileEntry) {
  held = false;
  holdTimer = window.setTimeout(() => {
    held = true;
    more(e);
  }, 550);
}
function holdEnd() {
  clearTimeout(holdTimer);
}
function tapFolder(e: FileEntry) {
  if (!held) tap(e);
  held = false;
}

function carry(e: FileEntry, cut: boolean) {
  carrying.value = { paths: [e.path], name: e.name, cut };
  sheet.value = null;
  open.value = null;
}
async function dropHere() {
  const c = carrying.value;
  if (!c) return;
  try {
    await filesPaste(c.paths, path.value, c.cut);
    notify(c.cut ? `${c.name} movido.` : `${c.name} copiado.`);
    carrying.value = null;
    reload();
  } catch (e) {
    notify(String(e), true);
  }
}
async function trash(e: FileEntry) {
  sheet.value = null;
  try {
    await filesTrash([e.path]);
    notify(`${e.name} foi pra lixeira.`);
    reload();
  } catch (err) {
    notify(String(err), true);
  }
}
async function share(t: ConnectTarget, e: FileEntry) {
  sheet.value = null;
  try {
    await connectShare(t, [e.path]);
    notify(`Enviando pra ${t.name}…`);
  } catch (err) {
    notify(String(err), true);
  }
}
function startRename(e: FileEntry) {
  target.value = e;
  nameInput.value = e.name;
  sheet.value = "rename";
}
async function saveName() {
  const name = nameInput.value.trim();
  if (!name) return;
  try {
    if (sheet.value === "rename" && target.value) await filesRename(target.value.path, name);
    else await filesMkdir(path.value, name);
    sheet.value = null;
    reload();
  } catch (e) {
    notify(String(e), true);
  }
}
async function showInfo(e: FileEntry) {
  target.value = e;
  info.value = null;
  sheet.value = "info";
  info.value = await filesInfo(e.path).catch(() => null);
}
function newFolder() {
  nameInput.value = "Nova pasta";
  sheet.value = "folder-name";
}

const isImage = (e: FileEntry) => e.kind === "image";
watch(searching, (s) => {
  if (!s) query.value = "";
});

const stops: (() => void)[] = [];
onMounted(async () => {
  places.value = await filesPlaces().catch(() => null);
  home.value = places.value?.home ?? "/";
  await load(decodeURIComponent(location.hash.slice(1)) || home.value);
  const refresh = () => document.visibilityState === "visible" && reload();
  document.addEventListener("visibilitychange", refresh);
  stops.push(() => document.removeEventListener("visibilitychange", refresh));
  // arquivo chegando pelo Connect: a pasta de recebidos atualiza
  stops.push(await on("lumna://connect-transfers", () => reload()));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="mf">
    <header>
      <button v-if="path !== home" class="ib" aria-label="Voltar" @click="up()"><Icon name="back" :size="26" /></button>
      <input v-if="searching" v-model="query" class="search" placeholder="Buscar nesta pasta" autofocus />
      <h1 v-else>{{ title }}</h1>
      <button class="ib" :aria-label="searching ? 'Fechar busca' : 'Buscar'" @click="searching = !searching">
        <Icon :name="searching ? 'close' : 'search'" :size="24" />
      </button>
      <button class="ib" aria-label="Novo" :disabled="!listing?.writable" @click="sheet = 'new'"><Icon name="plus" :size="26" /></button>
    </header>

    <nav class="tabs">
      <button v-for="t in tabs" :key="t.path" :class="{ on: activeTab === t.path }" @click="load(t.path)">{{ t.name }}</button>
    </nav>

    <div class="scroll">
      <div class="trail">
        <template v-for="(c, i) in trail" :key="c.path">
          <span v-if="i" class="sep">/</span>
          <button :class="{ here: i === trail.length - 1 }" @click="load(c.path)">{{ c.home ? "Meu espaço" : c.name }}</button>
        </template>
      </div>

      <template v-if="folders.length">
        <h2>Pastas</h2>
        <button
          v-for="f in folders"
          :key="f.path"
          class="row"
          @pointerdown="holdStart(f)"
          @pointerup="holdEnd()"
          @pointerleave="holdEnd()"
          @pointercancel="holdEnd()"
          @click="tapFolder(f)"
          @contextmenu.prevent="more(f)"
        >
          <span class="folder"><Icon name="folder" :size="30" /></span>
          <span class="txt">
            <strong>{{ f.name }}</strong>
            <small>{{ counts[f.path] == null ? "Pasta" : counts[f.path] === 1 ? "1 item" : `${counts[f.path]} itens` }}</small>
          </span>
          <Icon class="chev" name="forward" :size="22" />
        </button>
      </template>

      <template v-if="files.length">
        <h2>Arquivos</h2>
        <div v-for="f in files" :key="f.path" class="file" :class="{ open: open === f.path }">
          <div class="row" @click="tap(f)">
            <span class="kind"><Icon :name="KIND_ICON[f.kind] ?? 'file'" :size="28" /></span>
            <span class="txt">
              <strong>{{ f.name }}</strong>
              <small>{{ KIND_LABEL[f.kind] ?? "Arquivo" }} · {{ formatSize(f.size) }}</small>
            </span>
            <button class="dots" aria-label="Mais" @click.stop="more(f)"><Icon name="more" :size="22" /></button>
          </div>
          <template v-if="open === f.path">
            <div v-if="isImage(f)" class="preview"><img :src="convertFileSrc(f.path)" alt="" /></div>
            <div v-else class="meta">Modificado {{ formatDate(f.modified) }}</div>
            <div class="acts">
              <button @click="filesOpen(f.path).catch((e) => notify(String(e), true))"><Icon name="external" :size="22" /><span>Abrir</span></button>
              <button @click="carry(f, true)"><Icon name="folder" :size="22" /><span>Mover</span></button>
              <button @click="more(f)"><Icon name="more" :size="22" /><span>Mais</span></button>
            </div>
          </template>
        </div>
      </template>

      <p v-if="listing && !visible.length" class="empty">{{ query ? `Nada com "${query}" aqui.` : "Pasta vazia." }}</p>
      <p v-if="listing" class="foot">
        {{ folders.length === 1 ? "1 pasta" : `${folders.length} pastas` }} · {{ files.length === 1 ? "1 arquivo" : `${files.length} arquivos` }}
      </p>
    </div>

    <!-- movendo / copiando -->
    <div v-if="carrying" class="carry">
      <div class="ctext">
        <strong>{{ carrying.cut ? "Mover" : "Copiar" }} {{ carrying.name }}</strong>
        <small>Abra a pasta de destino</small>
      </div>
      <button class="cbtn" @click="carrying = null">Cancelar</button>
      <button class="cbtn go" :disabled="!listing?.writable" @click="dropHere()">{{ carrying.cut ? "Mover aqui" : "Colar aqui" }}</button>
    </div>

    <transition name="toast">
      <div v-if="toast" class="toast" :class="{ error: toast.error }">{{ toast.text }}</div>
    </transition>

    <!-- folhas -->
    <div v-if="sheet" class="veil" @click.self="sheet = null">
      <div class="sheet">
        <template v-if="sheet === 'more' && target">
          <h3>{{ target.name }}</h3>
          <button v-if="target.dir" @click="(sheet = null), load(target.path)"><Icon name="folder" :size="22" /> Abrir</button>
          <button @click="carry(target, true)"><Icon name="folder" :size="22" /> Mover</button>
          <button @click="carry(target, false)"><Icon name="copy" :size="22" /> Copiar</button>
          <button @click="startRename(target)"><Icon name="edit" :size="22" /> Renomear</button>
          <template v-if="!target.dir">
            <button v-for="t in targets" :key="t.id" @click="share(t, target)"><Icon name="send" :size="22" /> Enviar para {{ t.name }}</button>
          </template>
          <button @click="showInfo(target)"><Icon name="info" :size="22" /> Propriedades</button>
          <button class="danger" @click="trash(target)"><Icon name="trash" :size="22" /> Mover para a lixeira</button>
        </template>

        <template v-else-if="sheet === 'new'">
          <h3>Nesta pasta</h3>
          <button @click="newFolder()"><Icon name="folder-plus" :size="22" /> Nova pasta</button>
        </template>

        <template v-else-if="sheet === 'rename' || sheet === 'folder-name'">
          <h3>{{ sheet === "rename" ? "Renomear" : "Nova pasta" }}</h3>
          <form @submit.prevent="saveName()">
            <input v-model="nameInput" class="name" autofocus autocomplete="off" />
            <div class="two">
              <button type="button" class="pill" @click="sheet = null">Cancelar</button>
              <button type="submit" class="pill go" :disabled="!nameInput.trim()">{{ sheet === "rename" ? "Renomear" : "Criar" }}</button>
            </div>
          </form>
        </template>

        <template v-else-if="sheet === 'info' && target">
          <h3>{{ target.name }}</h3>
          <dl v-if="info">
            <dt>Tipo</dt><dd>{{ KIND_LABEL[target.kind] ?? "Arquivo" }}</dd>
            <dt>Tamanho</dt><dd>{{ formatSize(info.size) }}<template v-if="target.dir"> · {{ info.items }} itens</template></dd>
            <dt>Modificado</dt><dd>{{ formatDate(info.modified) }}</dd>
            <dt>Onde</dt><dd class="where">{{ info.path.replace(home, "~") }}</dd>
            <dt>Dono</dt><dd>{{ info.owner }} · {{ info.mode }}</dd>
          </dl>
          <p v-else class="empty">Lendo…</p>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.mf {
  height: var(--app-h, 100vh);
  display: flex;
  flex-direction: column;
  background: #08060f;
  color: var(--lumna-text);
  user-select: none;
}
header {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 14px 12px 6px 14px;
}
h1 {
  flex: 1;
  margin: 0 0 0 6px;
  font-size: 30px;
  font-weight: 400;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.search {
  flex: 1;
  min-width: 0;
  height: 44px;
  padding: 0 16px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 999px;
  background: rgba(20, 17, 30, 0.9);
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
  outline: none;
}
.ib {
  width: 48px;
  height: 48px;
  flex: none;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: inherit;
}
.ib:disabled {
  opacity: 0.35;
}
.tabs {
  display: flex;
  overflow-x: auto;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  scrollbar-width: none;
}
.tabs button {
  flex: 1 0 auto;
  min-width: 33%;
  padding: 14px 16px 12px;
  border: 0;
  border-bottom: 3px solid transparent;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 16px;
}
.tabs button.on {
  border-bottom-color: #b9a2fb;
  color: #c4b1ff;
  font-weight: 500;
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  padding: 0 18px 120px;
}
.trail {
  display: flex;
  flex-wrap: wrap;
  align-items: center;
  gap: 4px;
  padding: 14px 0 6px;
  font-size: 14px;
}
.trail button {
  padding: 2px 2px;
  border: 0;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
}
.trail button.here {
  color: var(--lumna-text);
}
.sep {
  margin: 0 6px;
  color: var(--lumna-muted);
}
h2 {
  margin: 18px 0 6px;
  font-size: 16px;
  font-weight: 500;
  color: #cfc6ee;
}
.row {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 18px;
  padding: 12px 6px;
  border: 0;
  border-top: 1px solid rgba(255, 255, 255, 0.07);
  background: none;
  color: inherit;
  font: inherit;
  text-align: left;
}
.folder {
  color: #c4a6ff;
}
.folder :deep(svg) {
  fill: #c4a6ff;
}
.kind {
  color: #e7e2f5;
}
.txt {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 3px;
}
.txt strong {
  font-weight: 400;
  font-size: 17px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.txt small {
  color: var(--lumna-muted);
  font-size: 14px;
}
.chev {
  color: var(--lumna-muted);
}
.dots {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.dots :deep(svg),
.acts button:last-child :deep(svg) {
  stroke-width: 3.4;
}
.file.open {
  margin: 4px -6px;
  padding: 0 6px 4px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 18px;
  background: rgba(30, 24, 52, 0.55);
}
.file.open .row {
  border-top: 0;
}
.file.open .kind {
  color: #c4a6ff;
}
.file.open + .file .row {
  border-top: 0;
}
.preview {
  margin: 0 6px 8px;
  border-radius: 14px;
  overflow: hidden;
  border: 1px solid rgba(255, 255, 255, 0.08);
  max-height: 34vh;
}
.preview img {
  display: block;
  width: 100%;
  max-height: 34vh;
  object-fit: cover;
}
.meta {
  margin: 0 6px 8px 58px;
  color: var(--lumna-muted);
  font-size: 13.5px;
}
.acts {
  display: flex;
}
.acts button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  padding: 10px 0 8px;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.acts button + button {
  border-left: 1px solid rgba(255, 255, 255, 0.08);
}
.acts :deep(svg) {
  color: #c4a6ff;
}
.empty {
  margin: 30px 0;
  text-align: center;
  color: var(--lumna-muted);
}
.foot {
  margin: 18px 0 0;
  padding-top: 14px;
  border-top: 1px solid rgba(255, 255, 255, 0.07);
  color: var(--lumna-muted);
  font-size: 14px;
}
.carry {
  position: fixed;
  left: 12px;
  right: 12px;
  bottom: calc(18px + var(--kb-inset, 0px));
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 12px 12px 16px;
  border: 1px solid rgba(139, 92, 246, 0.4);
  border-radius: 20px;
  background: #1c1633;
  box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
}
.ctext {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.ctext strong {
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.ctext small {
  color: var(--lumna-muted);
}
.cbtn {
  padding: 10px 12px;
  border: 0;
  border-radius: 12px;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
}
.cbtn.go {
  background: #6d4fc4;
  color: #fff;
  font-weight: 600;
}
.cbtn:disabled {
  opacity: 0.4;
}
.toast {
  position: fixed;
  left: 50%;
  bottom: calc(96px + var(--kb-inset, 0px));
  transform: translateX(-50%);
  max-width: 86vw;
  padding: 10px 16px;
  border-radius: 14px;
  background: #2a2147;
  font-size: 14px;
}
.toast.error {
  background: #4a1d2a;
  color: #fecaca;
}
.toast-enter-active,
.toast-leave-active {
  transition: opacity 0.2s;
}
.toast-enter-from,
.toast-leave-to {
  opacity: 0;
}
.veil {
  position: fixed;
  inset: 0 0 var(--kb-inset, 0px) 0;
  display: flex;
  align-items: flex-end;
  background: rgba(5, 3, 14, 0.6);
}
.sheet {
  width: 100%;
  max-height: 80vh;
  overflow-y: auto;
  box-sizing: border-box;
  padding: 18px 14px 26px;
  border-radius: 26px 26px 0 0;
  background: #17132a;
}
.sheet h3 {
  margin: 0 8px 10px;
  font-size: 18px;
  font-weight: 500;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.sheet > button {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 14px 10px;
  border: 0;
  border-radius: 14px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
  text-align: left;
}
.sheet > button :deep(svg) {
  color: #c4a6ff;
}
.sheet > button.danger,
.sheet > button.danger :deep(svg) {
  color: #fca5a5;
}
.name {
  width: 100%;
  box-sizing: border-box;
  padding: 14px 16px;
  border: 1px solid rgba(255, 255, 255, 0.14);
  border-radius: 14px;
  background: rgba(10, 8, 18, 0.7);
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
  outline: none;
}
.two {
  display: flex;
  gap: 10px;
  margin-top: 14px;
}
.pill {
  flex: 1;
  padding: 13px 0;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 15px;
}
.pill.go {
  border: 0;
  background: #6d4fc4;
  color: #fff;
  font-weight: 600;
}
dl {
  display: grid;
  grid-template-columns: auto 1fr;
  gap: 10px 16px;
  margin: 6px 8px 0;
  font-size: 15px;
}
dt {
  color: var(--lumna-muted);
}
dd {
  margin: 0;
}
.where {
  word-break: break-all;
}
</style>
