<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "../Icon.vue";
import WindowBar from "../WindowBar.vue";
import PackageRow from "./PackageRow.vue";
import { iconFor, SHELVES, SOURCE_LABEL } from "./curated";
import { anyRunning, followJobs, jobs, keyOf, runJob } from "./jobs";
import {
  on,
  storeInstall,
  storeInstalled,
  storeRemove,
  storeSearch,
  storeSetAur,
  storeSetFlathub,
  storeSetMultilib,
  storeSources,
  storeUpdates,
  storeUpgrade,
  type StorePackage,
  type StoreSource,
  type StoreSources,
  type StoreTab,
  type StoreUpdate,
} from "../../system";

const TABS: { id: StoreTab; label: string }[] = [
  { id: "descobrir", label: "Descobrir" },
  { id: "instalados", label: "Instalados" },
  { id: "atualizacoes", label: "Atualizações" },
  { id: "fontes", label: "Fontes" },
];
const pickTab = (id: string | null | undefined): StoreTab => TABS.find((t) => t.id === id)?.id ?? "descobrir";

const tab = ref<StoreTab>(pickTab(location.hash.slice(1)));
const query = ref("");
const filter = ref<StoreSource | "all">("all");
const results = ref<StorePackage[]>([]);
const searching = ref(false);
const installed = ref<StorePackage[]>([]);
const installedFilter = ref("");
const updates = ref<StoreUpdate[] | null>(null);
const sources = ref<StoreSources | null>(null);

/** Pergunta na tela (AUR, remover). */
const ask = ref<{ title: string; text: string; ok: string; resolve: (yes: boolean) => void } | null>(null);
const confirm = (title: string, text: string, ok: string) =>
  new Promise<boolean>((resolve) => (ask.value = { title, text, ok, resolve }));
function answer(yes: boolean) {
  ask.value?.resolve(yes);
  ask.value = null;
}

const installedKeys = computed(() => new Set(installed.value.map(keyOf)));
const isInstalled = (p: { source: StoreSource; name: string }) => installedKeys.value.has(keyOf(p));

const shown = computed(() => results.value.filter((p) => filter.value === "all" || p.source === filter.value));
const counts = computed(() => {
  const c: Record<string, number> = { all: results.value.length, arch: 0, flathub: 0, aur: 0 };
  for (const p of results.value) c[p.source]++;
  return c;
});

const installedShown = computed(() => {
  const q = installedFilter.value.trim().toLowerCase();
  return installed.value.filter(
    (p) => (filter.value === "all" || p.source === filter.value) && (!q || p.title.toLowerCase().includes(q) || p.name.includes(q)),
  );
});

const updatesBySource = computed(() => {
  const groups: Partial<Record<StoreSource, StoreUpdate[]>> = {};
  for (const u of updates.value ?? []) (groups[u.source] ??= []).push(u);
  return groups;
});

// ---- busca ----

let searchTimer: number | undefined;
let searchSeq = 0;
watch(query, (q) => {
  window.clearTimeout(searchTimer);
  if (q.trim().length < 2) {
    results.value = [];
    searching.value = false;
    return;
  }
  searching.value = true;
  searchTimer = window.setTimeout(async () => {
    const seq = ++searchSeq;
    const found = await storeSearch(q);
    // Uma busca mais nova pode ter terminado antes desta.
    if (seq !== searchSeq) return;
    results.value = found;
    searching.value = false;
  }, 450);
});

// ---- ações ----

async function refreshInstalled() {
  installed.value = await storeInstalled();
}

async function install(p: { source: StoreSource; name: string; repo: string; title: string }) {
  if (p.source === "aur" && localStorage.getItem("lumna.store.aur-ok") !== "1") {
    const yes = await confirm(
      "Pacote do AUR",
      "Pacotes do AUR são feitos pela comunidade, não pelo Arch. Eles são compilados aqui, o que pode demorar, e quem garante que são seguros é quem os mantém. Instalar mesmo assim?",
      "Instalar",
    );
    if (!yes) return;
    localStorage.setItem("lumna.store.aur-ok", "1");
  }
  if (p.source === "arch" && p.repo === "multilib" && sources.value && !sources.value.multilib) {
    const yes = await confirm(
      "Programas de 32 bits",
      `${p.title} precisa do repositório multilib (programas de 32 bits). Ele vai ser ligado antes de instalar.`,
      "Ligar e instalar",
    );
    if (!yes) return;
  }
  const ok = await runJob(keyOf(p), () => storeInstall(p));
  if (ok) {
    await refreshInstalled();
    results.value = results.value.map((r) => (keyOf(r) === keyOf(p) ? { ...r, installed: true } : r));
    if (p.repo === "multilib") sources.value = await storeSources();
  }
}

async function remove(p: { source: StoreSource; name: string; title: string }) {
  const yes = await confirm(`Remover ${p.title}?`, "O programa sai do computador. Seus arquivos e configurações ficam.", "Remover");
  if (!yes) return;
  const ok = await runJob(keyOf(p), () => storeRemove(p));
  if (ok) {
    await refreshInstalled();
    results.value = results.value.map((r) => (keyOf(r) === keyOf(p) ? { ...r, installed: false } : r));
  }
}

async function loadUpdates() {
  updates.value = null;
  updates.value = await storeUpdates();
}

async function upgrade(source: StoreSource) {
  const ok = await runJob(`upgrade:${source}`, () => storeUpgrade(source));
  if (ok) {
    await loadUpdates();
    await refreshInstalled();
  }
}

async function toggleMultilib() {
  if (!sources.value) return;
  await runJob("source:multilib", () => storeSetMultilib(!sources.value!.multilib));
  sources.value = await storeSources();
}

async function toggleFlathub() {
  if (!sources.value) return;
  await runJob("source:flathub", () => storeSetFlathub(!sources.value!.flathub));
  sources.value = await storeSources();
}

async function toggleAur() {
  if (!sources.value) return;
  await storeSetAur(!sources.value.aur);
  sources.value = await storeSources();
}

watch(tab, (t) => {
  query.value = "";
  filter.value = "all";
  if (t === "atualizacoes" && updates.value === null) loadUpdates();
});

const stops: (() => void)[] = [];

onMounted(async () => {
  stops.push(await followJobs());
  stops.push(await on<string>("lumna://store-tab", (id) => (tab.value = pickTab(id))));
  sources.value = await storeSources();
  await refreshInstalled();
  if (tab.value === "atualizacoes") loadUpdates();
});

onUnmounted(() => stops.forEach((stop) => stop()));

const upgradeLabel: Record<StoreSource, string> = {
  arch: "Sistema e programas do Arch",
  flathub: "Apps do Flathub",
  aur: "Pacotes do AUR",
};
</script>

<template>
  <div class="store">
    <WindowBar title="Loja" />

    <header class="hero">
      <div class="search" :class="{ active: query }">
        <Icon name="search" :size="20" />
        <input v-model="query" placeholder="Procurar programas (Arch, Flathub e AUR)" spellcheck="false" />
        <span v-if="searching" class="spinner" />
        <button v-else-if="query" class="clear" title="Limpar" @click="query = ''"><Icon name="close" :size="16" /></button>
      </div>
      <nav class="tabs">
        <button v-for="t in TABS" :key="t.id" :class="{ active: tab === t.id && !query }" @click="tab = t.id; query = ''">
          {{ t.label }}
          <span v-if="t.id === 'atualizacoes' && updates?.length" class="count">{{ updates.length }}</span>
        </button>
      </nav>
    </header>

    <main class="content">
      <!-- Busca: vale em qualquer aba enquanto há texto. -->
      <template v-if="query">
        <div class="chips">
          <button
            v-for="s in ['all', 'arch', 'flathub', 'aur'] as const"
            :key="s"
            :class="[s, { active: filter === s }]"
            @click="filter = s"
          >
            {{ s === "all" ? "Tudo" : SOURCE_LABEL[s] }} <span>{{ counts[s] }}</span>
          </button>
        </div>
        <p v-if="!searching && query.trim().length >= 2 && !shown.length" class="empty">Nada encontrado para "{{ query }}".</p>
        <section class="list">
          <PackageRow
            v-for="p in shown"
            :key="keyOf(p)"
            v-bind="p"
            :installed="p.installed || isInstalled(p)"
            @install="install(p)"
            @remove="remove(p)"
          />
        </section>
      </template>

      <template v-else-if="tab === 'descobrir'">
        <section v-for="shelf in SHELVES" :key="shelf.title" class="shelf">
          <h2>{{ shelf.title }}</h2>
          <div class="grid">
            <PackageRow
              v-for="p in shelf.picks"
              :key="keyOf(p)"
              card
              :source="p.source"
              :name="p.name"
              :title="p.title"
              :summary="p.summary"
              :icon="iconFor(p)"
              :installed="isInstalled(p)"
              @install="install(p)"
              @remove="remove(p)"
            />
          </div>
        </section>
      </template>

      <template v-else-if="tab === 'instalados'">
        <div class="toolbar">
          <div class="chips">
            <button v-for="s in ['all', 'arch', 'flathub', 'aur'] as const" :key="s" :class="[s, { active: filter === s }]" @click="filter = s">
              {{ s === "all" ? "Tudo" : SOURCE_LABEL[s] }}
            </button>
          </div>
          <input v-model="installedFilter" class="mini" placeholder="Filtrar" spellcheck="false" />
        </div>
        <section class="list">
          <PackageRow
            v-for="p in installedShown"
            :key="keyOf(p)"
            v-bind="p"
            installed
            @install="install(p)"
            @remove="remove(p)"
          />
        </section>
        <p class="note">Do Arch aparecem os programas instalados de propósito; as dependências ficam escondidas.</p>
      </template>

      <template v-else-if="tab === 'atualizacoes'">
        <div class="toolbar">
          <h2>Atualizações</h2>
          <button class="btn" :disabled="updates === null || anyRunning()" @click="loadUpdates()">
            <Icon name="refresh" :size="14" /> Procurar de novo
          </button>
        </div>
        <p v-if="updates === null" class="empty"><span class="spinner" /> Procurando atualizações…</p>
        <p v-else-if="!updates.length" class="empty">Tudo em dia. ✨</p>
        <section v-for="(list, source) in updatesBySource" :key="source" class="group">
          <div class="group-head">
            <div>
              <h3>{{ upgradeLabel[source as StoreSource] }}</h3>
              <p class="sub">{{ list!.length }} {{ list!.length === 1 ? "atualização" : "atualizações" }}</p>
            </div>
            <button class="btn primary" :disabled="anyRunning()" @click="upgrade(source as StoreSource)">
              {{ jobs[`upgrade:${source}`]?.running ? "Atualizando…" : "Atualizar" }}
            </button>
          </div>
          <p v-if="jobs[`upgrade:${source}`]?.running" class="progress">
            <span class="spinner" /> {{ jobs[`upgrade:${source}`].line || "Começando…" }}
          </p>
          <p v-else-if="jobs[`upgrade:${source}`]?.error" class="error">{{ jobs[`upgrade:${source}`].error }}</p>
          <ul>
            <li v-for="u in list" :key="u.name">
              <span>{{ u.name }}</span>
              <span class="ver">{{ u.from ? `${u.from} → ` : "" }}{{ u.to }}</span>
            </li>
          </ul>
        </section>
      </template>

      <template v-else-if="tab === 'fontes' && sources">
        <h2>De onde vêm os programas</h2>
        <section class="source">
          <span class="dot arch" />
          <div class="grow">
            <h3>Arch Linux</h3>
            <p>Os repositórios oficiais: a base do sistema e os programas mais testados.</p>
            <div class="option">
              <div class="grow">
                <strong>Programas de 32 bits (multilib)</strong>
                <p>Necessário para a Steam e o Wine.</p>
              </div>
              <span v-if="jobs['source:multilib']?.running" class="spinner" />
              <button class="switch" :class="{ on: sources.multilib }" :disabled="anyRunning()" @click="toggleMultilib()" />
            </div>
            <p v-if="jobs['source:multilib']?.error" class="error">{{ jobs["source:multilib"].error }}</p>
          </div>
        </section>
        <section class="source">
          <span class="dot flathub" />
          <div class="grow">
            <h3>Flathub</h3>
            <p>Apps isolados do resto do sistema (Spotify, Discord, OBS...). Instalam só para você, sem senha.</p>
            <p v-if="!sources.flatpak" class="error">O Flatpak não está instalado.</p>
            <p v-if="jobs['source:flathub']?.error" class="error">{{ jobs["source:flathub"].error }}</p>
          </div>
          <span v-if="jobs['source:flathub']?.running" class="spinner" />
          <button class="switch" :class="{ on: sources.flathub }" :disabled="!sources.flatpak || anyRunning()" @click="toggleFlathub()" />
        </section>
        <section class="source">
          <span class="dot aur" />
          <div class="grow">
            <h3>AUR</h3>
            <p>
              Pacotes feitos pela comunidade e compilados no seu computador. Tem quase tudo, mas quem garante cada
              pacote é quem o mantém, não o Arch.
            </p>
            <p v-if="!sources.aur_tools" class="error">Para compilar faltam o git e o base-devel.</p>
          </div>
          <button class="switch" :class="{ on: sources.aur }" @click="toggleAur()" />
        </section>
      </template>
    </main>

    <div v-if="ask" class="veil" @click.self="answer(false)">
      <div class="dialog">
        <h3>{{ ask.title }}</h3>
        <p>{{ ask.text }}</p>
        <div class="buttons">
          <button class="btn" @click="answer(false)">Cancelar</button>
          <button class="btn primary" @click="answer(true)">{{ ask.ok }}</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.store {
  height: var(--app-h, 100vh);
  display: flex;
  flex-direction: column;
  background: rgba(16, 12, 30, 0.95);
}

.hero {
  flex: none;
  padding: 26px 32px 0;
  background: radial-gradient(120% 140% at 50% -40%, rgba(139, 92, 246, 0.32), transparent 60%);
  border-bottom: 1px solid var(--lumna-line);
}
.search {
  max-width: 640px;
  margin: 0 auto;
  height: 52px;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0 16px 0 18px;
  border-radius: 18px;
  background: rgba(0, 0, 0, 0.3);
  border: 1px solid var(--lumna-line);
  color: var(--lumna-muted);
  transition: border-color 200ms ease, box-shadow 200ms ease;
}
.search:focus-within {
  border-color: var(--lumna-violet);
  box-shadow: 0 0 0 4px rgba(139, 92, 246, 0.18);
}
.search input {
  flex: 1;
  min-width: 0;
  border: 0;
  outline: 0;
  background: transparent;
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
}
.clear {
  display: grid;
  place-items: center;
  width: 28px;
  height: 28px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: var(--lumna-muted);
}

.tabs {
  display: flex;
  justify-content: center;
  gap: 6px;
  margin-top: 18px;
}
.tabs button {
  position: relative;
  height: 40px;
  padding: 0 16px;
  border: 0;
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 14px;
  display: flex;
  align-items: center;
  gap: 8px;
}
.tabs button::after {
  content: "";
  position: absolute;
  left: 14px;
  right: 14px;
  bottom: -1px;
  height: 3px;
  border-radius: 3px 3px 0 0;
  background: var(--lumna-violet);
  transform: scaleX(0);
  transition: transform 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.tabs button.active {
  color: #fff;
}
.tabs button.active::after {
  transform: scaleX(1);
}
.count {
  font-size: 11px;
  min-width: 18px;
  height: 18px;
  padding: 0 5px;
  box-sizing: border-box;
  border-radius: 9px;
  display: grid;
  place-items: center;
  background: var(--lumna-violet);
  color: #fff;
}

.content {
  flex: 1;
  overflow-y: auto;
  padding: 24px 32px 40px;
}
h2 {
  margin: 0 0 14px;
  font-size: 17px;
  font-weight: 600;
}
h3 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
}
.shelf + .shelf {
  margin-top: 30px;
}
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 14px;
}
.list {
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--lumna-line);
  border-radius: 18px;
}
.list:empty {
  display: none;
}

.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-bottom: 16px;
}
.chips button {
  height: 32px;
  padding: 0 14px;
  border-radius: 999px;
  border: 1px solid var(--lumna-line);
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 13px;
}
.chips button span {
  opacity: 0.6;
  margin-left: 4px;
}
.chips button.active {
  color: #fff;
  border-color: transparent;
  background: rgba(139, 92, 246, 0.35);
}
.chips button.arch.active {
  background: rgba(59, 130, 246, 0.35);
}
.chips button.aur.active {
  background: rgba(217, 119, 6, 0.4);
}

.toolbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 16px;
}
.toolbar .chips,
.toolbar h2 {
  margin: 0;
}
.mini {
  height: 32px;
  width: 220px;
  padding: 0 12px;
  border-radius: 10px;
  border: 1px solid var(--lumna-line);
  background: rgba(0, 0, 0, 0.25);
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
  outline: 0;
}

.empty {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 24px 4px;
  color: var(--lumna-muted);
  font-size: 14px;
}
.note,
.sub {
  margin: 4px 0 0;
  font-size: 12px;
  color: var(--lumna-muted);
}
.note {
  margin-top: 14px;
}
.error {
  margin: 8px 0 0;
  font-size: 12px;
  color: #fca5a5;
  white-space: pre-line;
}
.progress {
  display: flex;
  align-items: center;
  gap: 8px;
  margin: 10px 0 0;
  font-size: 12px;
  color: var(--lumna-muted);
}

.group {
  padding: 18px 20px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--lumna-line);
  margin-bottom: 14px;
}
.group-head {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
}
.group ul {
  list-style: none;
  margin: 14px 0 0;
  padding: 0;
  max-height: 240px;
  overflow-y: auto;
  font-size: 13px;
}
.group li {
  display: flex;
  justify-content: space-between;
  gap: 12px;
  padding: 6px 0;
  border-top: 1px solid rgba(167, 139, 250, 0.08);
}
.ver {
  color: var(--lumna-muted);
  font-variant-numeric: tabular-nums;
}

.source {
  display: flex;
  align-items: flex-start;
  gap: 16px;
  padding: 18px 20px;
  margin-bottom: 12px;
  border-radius: 18px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--lumna-line);
}
.source p {
  margin: 4px 0 0;
  font-size: 13px;
  color: var(--lumna-muted);
  line-height: 1.5;
}
.grow {
  flex: 1;
  min-width: 0;
}
.dot {
  width: 12px;
  height: 12px;
  margin-top: 5px;
  border-radius: 50%;
  flex: none;
}
.dot.arch {
  background: #60a5fa;
}
.dot.flathub {
  background: var(--lumna-violet-soft);
}
.dot.aur {
  background: #fbbf24;
}
.option {
  display: flex;
  align-items: center;
  gap: 12px;
  margin-top: 14px;
  padding-top: 14px;
  border-top: 1px solid rgba(167, 139, 250, 0.1);
}
.option strong {
  font-size: 13px;
  font-weight: 600;
}

.btn {
  height: 34px;
  padding: 0 14px;
  border: 0;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--lumna-text);
  font: inherit;
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
.btn.primary:hover:not(:disabled) {
  background: #9b72f8;
}
.btn:disabled {
  opacity: 0.55;
}

.switch {
  width: 42px;
  height: 24px;
  flex: none;
  border: 0;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.14);
  position: relative;
  transition: background-color 180ms ease;
}
.switch::after {
  content: "";
  position: absolute;
  top: 3px;
  left: 3px;
  width: 18px;
  height: 18px;
  border-radius: 50%;
  background: #fff;
  transition: transform 200ms cubic-bezier(0.22, 1, 0.36, 1);
}
.switch.on {
  background: var(--lumna-violet);
}
.switch.on::after {
  transform: translateX(18px);
}
.switch:disabled {
  opacity: 0.5;
}

.spinner {
  width: 14px;
  height: 14px;
  flex: none;
  border-radius: 50%;
  border: 2px solid rgba(255, 255, 255, 0.2);
  border-top-color: var(--lumna-violet-soft);
  animation: spin 800ms linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}

.veil {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(5, 2, 15, 0.55);
}
.dialog {
  width: 420px;
  padding: 24px;
  border-radius: 20px;
  background: #1a1433;
  border: 1px solid var(--lumna-line);
  box-shadow: 0 24px 60px rgba(5, 2, 15, 0.6);
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
