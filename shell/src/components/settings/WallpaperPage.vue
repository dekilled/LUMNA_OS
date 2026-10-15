<script setup lang="ts">
import { convertFileSrc } from "@tauri-apps/api/core";
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { on, wallpaperAdd, wallpaperList, wallpaperRemove, wallpaperSet, wallpaperSetLock, type Wallpaper } from "../../system";

// Um papel de parede para todos os desktops. A grade junta os do LUMNA e os que a pessoa adicionou.
const items = ref<Wallpaper[]>([]);
const current = ref("");
/** Fundo só da tela de bloqueio (`null` = o mesmo do desktop). */
const lock = ref<string | null>(null);
/** Para onde vai a escolha: os desktops ou a tela de bloqueio. */
const target = ref<"desktop" | "lock">("desktop");
const isChosen = (path: string) => (target.value === "desktop" ? path === current.value : path === lock.value);
const adding = ref(false);
const error = ref("");

async function load() {
  const data = await wallpaperList();
  items.value = data.items;
  current.value = data.current;
  lock.value = data.lock;
}

async function useForLock(path: string | null) {
  await wallpaperSetLock(path);
  lock.value = path;
}

async function choose(item: Wallpaper) {
  error.value = "";
  try {
    if (target.value === "lock") {
      await useForLock(item.path);
      return;
    }
    await wallpaperSet(item.path);
    current.value = item.path;
  } catch (e) {
    error.value = String(e);
  }
}

async function add() {
  adding.value = true;
  error.value = "";
  try {
    const added = await wallpaperAdd();
    await load();
    // Uma imagem só: já vira o papel de parede.
    if (added.length === 1) {
      if (target.value === "lock") await useForLock(added[0]);
      else await wallpaperSet(added[0]).then(() => (current.value = added[0]));
    }
  } catch (e) {
    error.value = String(e);
  } finally {
    adding.value = false;
  }
}

async function remove(item: Wallpaper) {
  error.value = "";
  try {
    await wallpaperRemove(item.path);
    await load();
  } catch (e) {
    error.value = String(e);
  }
}

const stops: (() => void)[] = [];

onMounted(async () => {
  await load().catch((e) => (error.value = String(e)));
  stops.push(await on<string>("lumna://wallpaper", (path) => (current.value = path)));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <section class="page">
    <h1>Papel de parede</h1>
    <p class="lead">
      {{
        target === "desktop"
          ? "O mesmo em todos os desktops. Clique numa imagem para usar."
          : "O fundo da tela de bloqueio (com blur). Pode ser o mesmo do desktop ou outro."
      }}
    </p>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="segments">
      <button :class="{ chosen: target === 'desktop' }" @click="target = 'desktop'"><Icon name="desktop" :size="15" /> Desktop</button>
      <button :class="{ chosen: target === 'lock' }" @click="target = 'lock'"><Icon name="lock" :size="15" /> Tela de bloqueio</button>
    </div>

    <div class="grid">
      <div
        v-if="target === 'lock'"
        class="tile same"
        :class="{ chosen: lock === null }"
        role="button"
        title="Usar o mesmo papel de parede do desktop"
        @click="useForLock(null).catch((e) => (error = String(e)))"
      >
        <img v-if="current" :src="convertFileSrc(current)" alt="" draggable="false" />
        <span class="name">Igual ao desktop</span>
        <span v-if="lock === null" class="check"><Icon name="check" :size="14" /></span>
      </div>
      <div
        v-for="item in items"
        :key="item.path"
        class="tile"
        :class="{ chosen: isChosen(item.path) }"
        role="button"
        :title="item.name"
        @click="choose(item)"
      >
        <img :src="convertFileSrc(item.path)" :alt="item.name" loading="lazy" decoding="async" draggable="false" />
        <span class="name">{{ item.name }}</span>
        <span v-if="isChosen(item.path)" class="check"><Icon name="check" :size="14" /></span>
        <button v-if="item.user" class="remove" title="Tirar da lista" @click.stop="remove(item)">
          <Icon name="close" :size="13" />
        </button>
      </div>

      <button class="tile add" :disabled="adding" @click="add()">
        <Icon :name="adding ? 'refresh' : 'plus'" :size="22" />
        <span>{{ adding ? "Escolhendo…" : "Adicionar imagens" }}</span>
      </button>
    </div>
  </section>
</template>

<style scoped>
.segments {
  display: flex;
  gap: 8px;
}
.segments button {
  display: flex;
  align-items: center;
  gap: 8px;
  height: 34px;
  padding: 0 16px;
  border: 1px solid var(--lumna-line);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  font-size: 13px;
  cursor: pointer;
}
.segments button.chosen {
  border-color: #8b5cf6;
  background: rgba(139, 92, 246, 0.25);
}
.tile.same img {
  filter: blur(3px) brightness(0.7);
}
/* Grafite com o acento violeta, como os menus novos (docs/VISUAL.md). */
.grid {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
  gap: 14px;
  margin-top: 18px;
}

.tile {
  position: relative;
  aspect-ratio: 16 / 10;
  overflow: hidden;
  border: 2px solid transparent;
  border-radius: 12px;
  background: #222327;
  cursor: pointer;
  transition: transform 160ms cubic-bezier(0.22, 1, 0.36, 1), border-color 160ms ease;
}
.tile:hover {
  transform: translateY(-2px);
}
.tile.chosen {
  border-color: #8b5cf6;
  box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.25);
}
.tile img {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.name {
  position: absolute;
  left: 0;
  right: 0;
  bottom: 0;
  padding: 18px 10px 7px;
  background: linear-gradient(transparent, rgba(0, 0, 0, 0.7));
  color: #ececf1;
  font-size: 12px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}

.check {
  position: absolute;
  top: 8px;
  left: 8px;
  display: grid;
  place-items: center;
  width: 24px;
  height: 24px;
  border-radius: 12px;
  background: #8b5cf6;
  color: white;
}

.remove {
  position: absolute;
  top: 8px;
  right: 8px;
  display: grid;
  place-items: center;
  width: 26px;
  height: 26px;
  border: 0;
  border-radius: 13px;
  background: rgba(0, 0, 0, 0.55);
  color: #ececf1;
  opacity: 0;
  transition: opacity 120ms ease;
}
.tile:hover .remove {
  opacity: 1;
}
.remove:hover {
  background: rgba(239, 68, 68, 0.9);
}

.add {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 8px;
  border: 2px dashed rgba(255, 255, 255, 0.14);
  background: transparent;
  color: #9a9aa5;
  font: inherit;
  font-size: 13px;
}
.add:hover:not(:disabled) {
  border-color: #8b5cf6;
  color: #ececf1;
}

.error {
  margin-top: 10px;
  color: #fca5a5;
}
</style>
