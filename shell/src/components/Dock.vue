<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import { activeWindow, isMinimized, onCompositorEvent, windowControl, windows, type Window } from "../compositor";
import { apps, fitLayerTo, launchApp, toggleEmma, toggleLauncher, type App } from "../system";
import Icon from "./Icon.vue";

// Apps fixos do dock, pelo id do `.desktop`. Os que não estiverem instalados não aparecem.
// "Instalar LUMNA OS" só existe no pendrive; no sistema instalado ele some sozinho do dock.
const PINNED = ["lumna-installer", "lumna-files", "firefox", "org.xfce.mousepad", "foot"];

const installed = ref<App[]>([]);
const open = ref<Window[]>([]);
const root = ref<HTMLElement>();

/** Classe de janela de cada app: a declarada no `.desktop` ou o próprio id. */
const classOf = (app: App) => (app.wm_class ?? app.id).toLowerCase();

const items = computed(() => {
  const running = new Set(open.value.map((w) => w.class.toLowerCase()));
  const pinned = PINNED.map((id) => installed.value.find((a) => a.id === id)).filter((a): a is App => !!a);
  // Apps abertos que não estão fixos entram depois dos fixos.
  const extra = installed.value.filter((a) => !PINNED.includes(a.id) && running.has(classOf(a)));
  return [...pinned, ...extra].map((app) => ({ app, running: running.has(classOf(app)) }));
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

// Menu do botão direito: nova janela, as janelas abertas (para escolher) e fechar.
const menu = ref<App | null>(null);

function openMenu(event: MouseEvent, app: App) {
  event.preventDefault();
  menu.value = menu.value?.id === app.id ? null : app;
}

function closeMenu() {
  menu.value = null;
}

function menuAction(run: () => unknown) {
  closeMenu();
  Promise.resolve(run()).catch((e) => console.error(e));
}

const closeAll = (app: App) => Promise.all(windowsOf(app).map((w) => windowControl("close", w.address)));

async function refreshWindows() {
  open.value = await windows().catch(() => []);
}

const stops: (() => void)[] = [];

onMounted(async () => {
  installed.value = await apps();
  await refreshWindows();
  stops.push(
    await onCompositorEvent((event) => {
      if (event.kind === "window_opened" || event.kind === "window_closed" || event.kind === "workspaces_changed")
        refreshWindows();
    }),
  );
  if (root.value) stops.push(fitLayerTo(root.value));
  // O dock não recebe cliques fora dele: o mouse sair da camada fecha o menu.
  document.documentElement.addEventListener("pointerleave", closeMenu);
  stops.push(() => document.documentElement.removeEventListener("pointerleave", closeMenu));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div ref="root" class="dock-frame">
    <!-- O menu fica dentro da camada do dock (que cresce para cima enquanto ele está aberto). -->
    <div v-if="menu" class="menu" @contextmenu.prevent>
      <span class="menu-title">{{ menu.name }}</span>
      <button v-for="w in windowsOf(menu)" :key="w.address" @click="menuAction(() => windowControl('focus', w.address))">
        <Icon name="app" :size="14" />
        <span class="label">{{ w.title || menu.name }}</span>
        <small v-if="isMinimized(w)">minimizada</small>
      </button>
      <span v-if="windowsOf(menu).length" class="line" />
      <button @click="menuAction(() => launchApp(menu!.id))"><Icon name="plus" :size="14" /><span class="label">Nova janela</span></button>
      <button v-if="windowsOf(menu).length" class="danger" @click="menuAction(() => closeAll(menu!))">
        <Icon name="close" :size="14" />
        <span class="label">{{ windowsOf(menu).length > 1 ? "Fechar todas" : "Fechar" }}</span>
      </button>
    </div>
    <nav class="dock">
      <button
        v-for="{ app, running } in items"
        :key="app.id"
        class="app"
        :class="{ open: menu?.id === app.id }"
        :title="app.name"
        @click="activate(app)"
        @contextmenu="openMenu($event, app)"
      >
        <img v-if="app.icon" :src="app.icon" alt="" />
        <span v-else class="fallback">{{ app.name.charAt(0) }}</span>
        <span class="dot" :class="{ on: running }" />
      </button>
      <span class="divider" />
      <button class="app" title="Todos os apps" @click="toggleLauncher()">
        <span class="all"><Icon name="grid" :size="22" /></span>
        <span class="dot" />
      </button>
      <button class="app" title="Emma (Super + A)" @click="toggleEmma()">
        <span class="all emma"><Icon name="sparkle" :size="22" /></span>
        <span class="dot" />
      </button>
    </nav>
  </div>
</template>

<style scoped>
/* A moldura tem folga para a sombra e o zoom do hover não serem cortados pela camada. */
.dock-frame {
  width: max-content;
  min-width: 240px;
  display: flex;
  flex-direction: column;
  align-items: center;
  padding: 8px 10px 0;
}

.menu {
  align-self: stretch;
  display: flex;
  flex-direction: column;
  margin-bottom: 8px;
  padding: 6px;
  background: #1e1f23;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  box-shadow: 0 12px 30px rgba(0, 0, 0, 0.45);
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
.app.open {
  transform: translateY(-3px);
}

.dock {
  display: flex;
  align-items: center;
  gap: 6px;
  padding: 7px 9px 4px;
  background: var(--lumna-glass);
  border: 1px solid var(--lumna-line);
  border-radius: 20px;
  box-shadow: 0 10px 30px rgba(5, 2, 15, 0.45);
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
.app:hover {
  transform: translateY(-3px) scale(1.08);
}
.app:active {
  transform: scale(0.94);
}

.app img,
.fallback,
.all {
  width: 44px;
  height: 44px;
  border-radius: 12px;
}
.fallback,
.all {
  display: grid;
  place-items: center;
  background: rgba(139, 92, 246, 0.22);
  color: var(--lumna-text);
  font-weight: 700;
  font-size: 18px;
}

.emma {
  background: linear-gradient(135deg, #8b5cf6, #6d28d9);
}

.dot {
  width: 4px;
  height: 4px;
  border-radius: 2px;
  background: transparent;
}
.dot.on {
  background: var(--lumna-violet-soft);
  box-shadow: 0 0 6px var(--lumna-violet);
}

.divider {
  width: 1px;
  height: 36px;
  margin: 0 4px 7px;
  background: var(--lumna-line);
}
</style>
