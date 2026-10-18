<script setup lang="ts">
// Celular: tela Início (relógio grande e "Pergunte à Emma") e aba Apps (busca e grade).
// Fica embaixo das janelas; a barra de navegação troca a aba pelo evento lumna://mobile-tab.
import { computed, onMounted, onUnmounted, ref } from "vue";
import type { UnlistenFn } from "@tauri-apps/api/event";
import Icon from "../Icon.vue";
import { showApp } from "./apps";
import { apps, artUrl, desktopWallpaper, launchApp, on, toggleEmma, type App } from "../../system";

const tab = ref<"inicio" | "apps">("inicio");
const wallpaper = ref<string | null>(null);
const list = ref<App[]>([]);
const query = ref("");
const time = ref("");
const date = ref("");
let timer = 0;
const stops: UnlistenFn[] = [];

function tick() {
  const now = new Date();
  time.value = now.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
  const text = now.toLocaleDateString("pt-BR", { weekday: "long", day: "numeric", month: "long" });
  date.value = text.charAt(0).toUpperCase() + text.slice(1);
}

// Busca sem acento, como no launcher do PC.
const plain = (s: string) => s.normalize("NFD").replace(/[̀-ͯ]/g, "").toLowerCase();
const shown = computed(() => {
  const q = plain(query.value.trim());
  return q ? list.value.filter((a) => plain(a.name).includes(q)) : list.value;
});

async function open(app: App) {
  query.value = "";
  // um app por vez: o que estiver aberto sai da frente
  // (sem foto pros recentes aqui: o grim trava o compositor um instante a cada troca de app; a
  // miniatura é tirada ao abrir os recentes)
  await showApp().catch(() => {});
  await launchApp(app.id);
}

onMounted(async () => {
  tick();
  timer = window.setInterval(tick, 5000);
  wallpaper.value = await desktopWallpaper().catch(() => null);
  list.value = await apps().catch(() => []);
  stops.push(await on<"inicio" | "apps">("lumna://mobile-tab", async (t) => {
    tab.value = t;
    if (t === "apps") list.value = await apps().catch(() => list.value);
  }));
  stops.push(await on<string>("lumna://wallpaper", (w) => (wallpaper.value = w)));
});
onUnmounted(() => {
  clearInterval(timer);
  stops.forEach((s) => s());
});
</script>

<template>
  <main class="home" :style="wallpaper ? { backgroundImage: `url(${artUrl(wallpaper)})` } : {}">
    <header class="brand"><span class="gem" /> LUMNA</header>

    <section v-if="tab === 'inicio'" class="inicio">
      <div class="clock">
        <div class="time">{{ time }}</div>
        <div class="date">{{ date }}</div>
      </div>
      <button class="ask" @click="toggleEmma()">
        <Icon name="mic" :size="22" />
        <span>Pergunte à Emma</span>
        <Icon name="forward" :size="18" />
      </button>
    </section>

    <section v-else class="apps">
      <h1>Aplicativos</h1>
      <label class="search">
        <Icon name="search" :size="20" />
        <input v-model="query" placeholder="Buscar aplicativos" />
      </label>
      <div class="grid">
        <button v-for="app in shown" :key="app.id" class="app" @click="open(app)">
          <img v-if="app.icon" :src="app.icon" alt="" />
          <Icon v-else name="app" :size="34" />
          <span>{{ app.name }}</span>
        </button>
      </div>
    </section>
  </main>
</template>

<style scoped>
.home {
  /* o #app não estica: a camada inteira é a viewport */
  height: 100vh;
  box-sizing: border-box;
  padding: 44px 20px 104px;
  display: flex;
  flex-direction: column;
  /* sem papel de parede: o eclipse roxo do sketch */
  background:
    radial-gradient(circle at 115% -5%, transparent 44%, rgba(167, 139, 250, 0.55) 45%, rgba(91, 33, 182, 0.25) 47%, transparent 56%),
    radial-gradient(ellipse at 30% 60%, #1d1240, #07040f 70%);
  background-size: cover;
  background-position: center;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  letter-spacing: 0.5em;
  font-size: 15px;
}
.gem {
  width: 18px;
  height: 18px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.inicio {
  flex: 1;
  display: flex;
  flex-direction: column;
  justify-content: space-between;
}
.clock {
  margin-top: 18vh;
  text-align: center;
}
.time {
  font-size: 84px;
  font-weight: 200;
  line-height: 1;
}
.date {
  margin-top: 10px;
  font-size: 16px;
}
.ask,
.search {
  display: flex;
  align-items: center;
  gap: 14px;
  height: 50px;
  padding: 0 18px;
  border: 1px solid var(--lumna-line);
  border-radius: 25px;
  background: rgba(16, 12, 30, 0.85);
  color: var(--lumna-muted);
  font: inherit;
  font-size: 15px;
}
.ask :first-child {
  color: var(--lumna-violet-soft);
}
.ask span {
  flex: 1;
  text-align: left;
}
.apps {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
h1 {
  margin: 24px 0 16px;
  font-size: 32px;
  font-weight: 500;
}
.search input {
  flex: 1;
  border: 0;
  outline: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
}
.grid {
  margin-top: 20px;
  flex: 1;
  overflow-y: auto;
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  grid-auto-rows: 112px;
  gap: 14px;
}
.app {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 10px;
  border: 1px solid var(--lumna-line);
  border-radius: 18px;
  background: rgba(16, 12, 30, 0.75);
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}
.app img {
  width: 40px;
  height: 40px;
}
.app span {
  max-width: 90%;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
</style>
