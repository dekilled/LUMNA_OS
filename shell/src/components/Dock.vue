<script setup lang="ts">
// Dock de um monitor. O que mostrar, o tamanho, a posição e o comportamento vêm das configurações
// (`~/.config/lumna/dock.json`, página "Dock"); o lado Rust diz a aparência atual:
// - normal: flutuando na borda;
// - hidden: uma janela está maximizada — só uma faixa fina na borda, que traz o dock com o mouse;
// - expanded: uma janela está maximizada — barra de largura inteira, ícones no centro (estilo Windows).
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import { activeWindow, isMinimized, onCompositorEvent, windowControl, windows, type Window } from "../compositor";
import {
  apps,
  DOCK_EMMA,
  DOCK_LAUNCHER,
  DOCK_SEPARATOR,
  dockPin,
  dockState,
  launchApp,
  layerResize,
  on,
  settingsOpen,
  toggleEmma,
  toggleLauncher,
  type App,
  type DockConfig,
  type DockLook,
} from "../system";
import Icon from "./Icon.vue";

type Entry =
  | { kind: "app"; key: string; app: App; running: boolean; pinned: boolean }
  | { kind: "separator"; key: string }
  | { kind: "launcher"; key: string }
  | { kind: "emma"; key: string };

const config = ref<DockConfig | null>(null);
const look = ref<DockLook>("normal");
/** Na aparência escondida: o mouse chegou na borda e o dock subiu. */
const revealed = ref(false);
const installed = ref<App[]>([]);
const open = ref<Window[]>([]);
const root = ref<HTMLElement>();

const position = computed(() => config.value?.position ?? "bottom");
const vertical = computed(() => position.value !== "bottom");
const showDock = computed(() => look.value !== "hidden" || revealed.value);

/** Classe de janela de cada app: a declarada no `.desktop` ou o próprio id. */
const classOf = (app: App) => (app.wm_class ?? app.id).toLowerCase();

const entries = computed<Entry[]>(() => {
  const pinnedIds = config.value?.pinned ?? [];
  const running = new Set(open.value.map((w) => w.class.toLowerCase()));
  const list: Entry[] = [];
  for (const [i, id] of pinnedIds.entries()) {
    const key = `${i}:${id}`;
    if (id === DOCK_SEPARATOR) list.push({ kind: "separator", key });
    else if (id === DOCK_LAUNCHER) list.push({ kind: "launcher", key });
    else if (id === DOCK_EMMA) list.push({ kind: "emma", key });
    else {
      // Apps fixos que não estão instalados (ex.: o instalador, fora do pendrive) não aparecem.
      const app = installed.value.find((a) => a.id === id);
      if (app) list.push({ kind: "app", key, app, running: running.has(classOf(app)), pinned: true });
    }
  }
  // Apps abertos que não estão fixos entram depois do último app fixo.
  const extra: Entry[] = installed.value
    .filter((a) => !pinnedIds.includes(a.id) && running.has(classOf(a)))
    .map((app) => ({ kind: "app", key: `open:${app.id}`, app, running: true, pinned: false }));
  const lastApp = list.map((e) => e.kind).lastIndexOf("app");
  list.splice(lastApp + 1, 0, ...extra);
  // Sem separadores sobrando nas pontas ou em dobro (quando um app fixo não está instalado).
  return list.filter(
    (e, i) =>
      e.kind !== "separator" || (i > 0 && i < list.length - 1 && list[i - 1].kind !== "separator"),
  );
});

const windowsOf = (app: App) => open.value.filter((w) => w.class.toLowerCase() === classOf(app));

/** Clique no app: sem janela abre; com janelas, passa para a próxima (e traz de volta a minimizada). */
async function activate(app: App) {
  closeMenu();
  const list = windowsOf(app);
  if (list.length === 0) {
    launchApp(app.id);
    return;
  }
  const current = await activeWindow().catch(() => null);
  const index = list.findIndex((w) => w.address === current?.address);
  // A em foco já é deste app: vai para a seguinte; senão, para a primeira (a minimizada primeiro).
  const next = index >= 0 ? list[(index + 1) % list.length] : (list.find(isMinimized) ?? list[0]);
  windowControl("focus", next.address).catch((e) => console.error(e));
}

// Menu do botão direito: janelas abertas, nova janela, fixar, fechar e as configurações do dock.
const menu = ref<Entry | null>(null);

function openMenu(event: MouseEvent, entry: Entry) {
  event.preventDefault();
  menu.value = menu.value?.key === entry.key ? null : entry;
}

function closeMenu() {
  menu.value = null;
}

function menuAction(run: () => unknown) {
  closeMenu();
  Promise.resolve(run()).catch((e) => console.error(e));
}

/** O menu aberto, quando é de um app. */
const appMenu = computed(() => (menu.value?.kind === "app" ? menu.value : null));

const closeAll = (app: App) => Promise.all(windowsOf(app).map((w) => windowControl("close", w.address)));

function newWindow() {
  const entry = appMenu.value;
  if (entry) menuAction(() => launchApp(entry.app.id));
}

function togglePin() {
  const entry = appMenu.value;
  if (entry) menuAction(() => dockPin(entry.app.id, !entry.pinned));
}

function closeAppWindows() {
  const entry = appMenu.value;
  if (entry) menuAction(() => closeAll(entry.app));
}

function removeSpecial(entry: Entry) {
  const id = entry.kind === "launcher" ? DOCK_LAUNCHER : DOCK_EMMA;
  return dockPin(id, false);
}

async function refreshWindows() {
  open.value = await windows().catch(() => []);
}

// ---- escondido na borda ----

let hideTimer: number | undefined;

function reveal() {
  window.clearTimeout(hideTimer);
  if (look.value === "hidden") revealed.value = true;
}

/**
 * O mouse saiu da camada: depois de um instante fecha o menu e, se estiver escondido, desce.
 * Se ele voltar (ou a saída foi só um aviso solto do compositor), o `reveal` cancela.
 */
function leave() {
  window.clearTimeout(hideTimer);
  hideTimer = window.setTimeout(() => {
    closeMenu();
    revealed.value = false;
  }, 450);
}

watch(look, (value) => {
  if (value !== "hidden") revealed.value = false;
});

// ---- tamanho da camada ----

// A camada acompanha o conteúdo. Esticado (escondido ou expandido), o compositor já dá a borda
// inteira; o tamanho pedido só vale na outra direção.
let observer: ResizeObserver | undefined;

function fit() {
  if (!root.value) return;
  const rect = root.value.getBoundingClientRect();
  layerResize(Math.ceil(rect.width), Math.ceil(rect.height));
}

const style = computed(() => {
  const c = config.value;
  const size = c?.size ?? 44;
  return {
    "--icon": `${size}px`,
    "--radius": `${Math.round(size * 0.27)}px`,
    "--gap": `${c?.spacing ?? 6}px`,
    "--bg": `rgba(14, 10, 28, ${c?.opacity ?? 0.55})`,
    "--glyph": `${Math.round(size * 0.4)}px`,
  };
});

const glyph = computed(() => Math.round((config.value?.size ?? 44) * 0.5));

const stops: (() => void)[] = [];

function applyState(state: { look: DockLook; config: DockConfig }) {
  look.value = state.look;
  config.value = state.config;
}

onMounted(async () => {
  const [state, list] = await Promise.all([dockState(), apps()]);
  applyState(state);
  installed.value = list;
  await refreshWindows();
  stops.push(await on<{ look: DockLook; config: DockConfig }>("lumna://dock", applyState));
  // Mudou nas Configurações (ou "Fixar no dock" em outro monitor).
  stops.push(await on<DockConfig>("lumna://dock-config", (c) => (config.value = c)));
  stops.push(
    await onCompositorEvent((event) => {
      if (event.kind === "window_opened" || event.kind === "window_closed" || event.kind === "workspaces_changed")
        refreshWindows();
    }),
  );
  if (root.value) {
    observer = new ResizeObserver(fit);
    observer.observe(root.value);
    stops.push(() => observer?.disconnect());
  }
  // O dock não recebe cliques fora dele: o mouse sair da camada fecha o menu (e esconde o dock).
  // O aviso vem do GTK; o `pointerleave` fica de reserva.
  stops.push(await on<null>("lumna://pointer-left", leave));
  document.documentElement.addEventListener("pointerleave", leave);
  stops.push(() => document.documentElement.removeEventListener("pointerleave", leave));
});

onUnmounted(() => {
  window.clearTimeout(hideTimer);
  stops.forEach((stop) => stop());
});
</script>

<template>
  <div
    ref="root"
    class="dock-frame"
    :class="[`at-${position}`, `look-${look}`, { vertical, revealed }]"
    :style="style"
    @pointerenter="reveal"
    @pointermove="reveal"
  >
    <span v-if="!showDock" class="edge" />
    <template v-else>
      <!-- O menu fica dentro da camada do dock (que cresce enquanto ele está aberto). -->
      <div v-if="menu" class="menu" @contextmenu.prevent>
        <template v-if="appMenu">
          <span class="menu-title">{{ appMenu.app.name }}</span>
          <button v-for="w in windowsOf(appMenu.app)" :key="w.address" @click="menuAction(() => windowControl('focus', w.address))">
            <Icon name="app" :size="14" />
            <span class="label">{{ w.title || appMenu.app.name }}</span>
            <small v-if="isMinimized(w)">minimizada</small>
          </button>
          <span v-if="windowsOf(appMenu.app).length" class="line" />
          <button @click="newWindow"><Icon name="plus" :size="14" /><span class="label">Nova janela</span></button>
          <button @click="togglePin">
            <Icon name="pin" :size="14" />
            <span class="label">{{ appMenu.pinned ? "Desafixar do dock" : "Fixar no dock" }}</span>
          </button>
          <button v-if="windowsOf(appMenu.app).length" class="danger" @click="closeAppWindows">
            <Icon name="close" :size="14" />
            <span class="label">{{ windowsOf(appMenu.app).length > 1 ? "Fechar todas" : "Fechar" }}</span>
          </button>
        </template>
        <template v-else>
          <span class="menu-title">{{ menu.kind === "emma" ? "Emma" : "Todos os apps" }}</span>
          <button @click="menuAction(() => removeSpecial(menu!))">
            <Icon name="close" :size="14" /><span class="label">Tirar do dock</span>
          </button>
        </template>
        <span class="line" />
        <button @click="menuAction(() => settingsOpen('dock'))">
          <Icon name="settings" :size="14" /><span class="label">Configurações do dock…</span>
        </button>
      </div>
      <nav class="dock">
        <template v-for="entry in entries" :key="entry.key">
          <span v-if="entry.kind === 'separator'" class="divider" />
          <button
            v-else-if="entry.kind === 'app'"
            class="app"
            :class="{ open: menu?.key === entry.key }"
            :title="entry.app.name"
            @click="activate(entry.app)"
            @contextmenu="openMenu($event, entry)"
          >
            <img v-if="entry.app.icon" :src="entry.app.icon" alt="" />
            <span v-else class="fallback">{{ entry.app.name.charAt(0) }}</span>
            <span class="dot" :class="{ on: entry.running }" />
          </button>
          <button
            v-else
            class="app"
            :class="{ open: menu?.key === entry.key }"
            :title="entry.kind === 'emma' ? 'Emma (Super + A)' : 'Todos os apps'"
            @click="entry.kind === 'emma' ? toggleEmma() : toggleLauncher()"
            @contextmenu="openMenu($event, entry)"
          >
            <span class="all" :class="{ emma: entry.kind === 'emma' }">
              <Icon :name="entry.kind === 'emma' ? 'sparkle' : 'grid'" :size="glyph" />
            </span>
            <span class="dot" />
          </button>
        </template>
      </nav>
    </template>
  </div>
</template>

<style scoped>
/* A moldura tem folga para a sombra e o zoom do hover não serem cortados pela camada. */
.dock-frame {
  --lift-x: 0px;
  --lift-y: -3px;
  /* Esticado (100vw/100vh), a folga entra na conta: senão a camada cresce sem parar. */
  box-sizing: border-box;
  width: max-content;
  height: max-content;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  padding: 8px 10px 0;
}
.dock-frame.at-left {
  --lift-x: 3px;
  --lift-y: 0px;
  flex-direction: row-reverse;
  padding: 10px 8px 10px 0;
}
.dock-frame.at-right {
  --lift-x: -3px;
  --lift-y: 0px;
  flex-direction: row;
  padding: 10px 0 10px 8px;
}

/* Esticado na borda inteira (escondido ou expandido). */
.dock-frame.look-hidden.at-bottom,
.dock-frame.look-expanded.at-bottom {
  width: 100vw;
}
.dock-frame.look-hidden.vertical,
.dock-frame.look-expanded.vertical {
  height: 100vh;
}
.dock-frame.look-hidden.revealed.at-bottom {
  padding-bottom: 10px;
}
.dock-frame.look-hidden.revealed.at-left {
  padding-left: 10px;
}
.dock-frame.look-hidden.revealed.at-right {
  padding-right: 10px;
}
.dock-frame.look-hidden:not(.revealed) {
  padding: 0;
}
.dock-frame.look-expanded {
  padding: 0;
}

/* A faixa fina na borda, enquanto o dock está escondido. */
.edge {
  display: block;
  width: 100%;
  height: 3px;
  background: linear-gradient(90deg, transparent, rgba(167, 139, 250, 0.35), transparent);
}
.vertical .edge {
  width: 3px;
  height: 100%;
  background: linear-gradient(180deg, transparent, rgba(167, 139, 250, 0.35), transparent);
}

.dock {
  display: flex;
  align-items: center;
  gap: var(--gap);
  padding: 7px 9px 4px;
  background: var(--bg);
  border: 1px solid var(--lumna-line);
  border-radius: 20px;
  box-shadow: 0 10px 30px rgba(5, 2, 15, 0.45);
}
.at-left .dock {
  flex-direction: column;
  padding: 9px 7px 9px 4px;
}
.at-right .dock {
  flex-direction: column;
  padding: 9px 4px 9px 7px;
}
.look-hidden.revealed .dock {
  animation: rise 160ms cubic-bezier(0.22, 1, 0.36, 1);
}
@keyframes rise {
  from {
    opacity: 0;
    transform: translate(calc(var(--lift-x) * -6), calc(var(--lift-y) * -6));
  }
}

/* Estilo barra de tarefas: largura inteira, ícones no centro, sem cantos. */
.look-expanded .dock {
  align-self: stretch;
  justify-content: center;
  border-radius: 0;
  border-width: 1px 0 0;
  box-shadow: none;
  padding: 5px 12px 2px;
}
.look-expanded.vertical .dock {
  border-width: 0;
  padding: 12px 2px 12px 5px;
}
.look-expanded.at-left .dock {
  border-right-width: 1px;
}
.look-expanded.at-right .dock {
  border-left-width: 1px;
  padding: 12px 5px 12px 2px;
}

.menu {
  align-self: stretch;
  display: flex;
  flex-direction: column;
  margin-bottom: 8px;
  padding: 6px;
  min-width: 220px;
  background: #1e1f23;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  box-shadow: 0 12px 30px rgba(0, 0, 0, 0.45);
}
.look-expanded .menu,
.look-hidden .menu {
  align-self: center;
}
.vertical .menu {
  align-self: center;
  margin: 0 8px;
}
.menu-title {
  padding: 4px 10px 6px;
  font-size: 12px;
  font-weight: 600;
  color: #9a9aa5;
}
.menu button {
  display: flex;
  align-items: center;
  gap: 10px;
  height: 32px;
  padding: 0 10px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: #ececf1;
  font: inherit;
  font-size: 13px;
  text-align: left;
}
.menu button:hover {
  background: #7d46d7;
}
.menu .danger:hover {
  background: rgba(239, 68, 68, 0.85);
}
.menu .label {
  flex: 1;
  max-width: 260px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.menu small {
  font-size: 11px;
  color: #9a9aa5;
}
.menu .line {
  height: 1px;
  margin: 4px 8px;
  background: rgba(255, 255, 255, 0.08);
}

.app {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 3px;
  border: 0;
  background: transparent;
  padding: 0;
  transition: transform 180ms cubic-bezier(0.22, 1, 0.36, 1);
}
/* O ponto de "aberto" fica do lado da borda da tela. */
.at-left .app {
  flex-direction: row-reverse;
}
.at-right .app {
  flex-direction: row;
}
.app:hover,
.app.open {
  transform: translate(var(--lift-x), var(--lift-y)) scale(1.08);
}
.app:active {
  transform: scale(0.94);
}

.app img,
.fallback,
.all {
  width: var(--icon);
  height: var(--icon);
  border-radius: var(--radius);
}
.fallback,
.all {
  display: grid;
  place-items: center;
  background: rgba(139, 92, 246, 0.22);
  color: var(--lumna-text);
  font-weight: 700;
  font-size: var(--glyph);
}

.emma {
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
}

.dot {
  width: 4px;
  height: 4px;
  flex: none;
  border-radius: 2px;
  background: transparent;
}
.dot.on {
  background: var(--lumna-violet-soft);
  box-shadow: 0 0 6px var(--lumna-violet);
}

.divider {
  width: 1px;
  height: calc(var(--icon) * 0.8);
  margin: 0 4px 7px;
  background: var(--lumna-line);
}
.vertical .divider {
  width: calc(var(--icon) * 0.8);
  height: 1px;
  margin: 4px 0;
}
</style>
