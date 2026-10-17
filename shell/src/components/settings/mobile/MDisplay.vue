<script setup lang="ts">
// Tela e aparência: brilho e papel de parede (início e bloqueio), com fotos do celular.
import { computed, onMounted, ref } from "vue";
import Icon from "../../Icon.vue";
import MGallery from "./MGallery.vue";
import {
  convertFileSrc,
  quickBrightness,
  quickState,
  wallpaperImport,
  wallpaperList,
  wallpaperSet,
  wallpaperSetLock,
  type Wallpaper,
} from "../../../system";

defineProps<{ arg?: string }>();
const brightness = ref<number | null>(null);
const items = ref<Wallpaper[]>([]);
const current = ref("");
const lock = ref<string | null>(null);
const target = ref<"inicio" | "bloqueio" | "ambos">("ambos");
const gallery = ref(false);
const error = ref("");

const src = (p: string) => convertFileSrc(p);
const preview = computed(() => (target.value === "bloqueio" ? lock.value ?? current.value : current.value));

async function load() {
  const w = await wallpaperList().catch(() => null);
  if (w) {
    items.value = w.items;
    current.value = w.current;
    lock.value = w.lock;
  }
}
let timer = 0;
function setBrightness(v: number) {
  brightness.value = v;
  clearTimeout(timer);
  timer = window.setTimeout(() => quickBrightness(v).catch((e) => (error.value = String(e))), 40);
}
async function apply(path: string) {
  error.value = "";
  try {
    if (target.value !== "bloqueio") await wallpaperSet(path);
    if (target.value === "bloqueio") await wallpaperSetLock(path);
    if (target.value === "ambos") await wallpaperSetLock(null);
  } catch (e) {
    error.value = String(e);
  }
  load();
}
async function fromGallery(path: string) {
  gallery.value = false;
  try {
    await apply(await wallpaperImport(path));
  } catch (e) {
    error.value = String(e);
  }
}
onMounted(async () => {
  brightness.value = (await quickState().catch(() => null))?.brightness ?? null;
  load();
});
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-label">Tela</div>
    <div class="ms-card pad">
      <div class="bright">
        <Icon name="sun" :size="24" />
        <input
          class="ms-slider"
          type="range"
          min="2"
          max="100"
          :value="brightness ?? 50"
          :disabled="brightness == null"
          @input="setBrightness(+($event.target as HTMLInputElement).value)"
        />
      </div>
      <p v-if="brightness == null" class="ms-note">Esta tela não tem brilho ajustável.</p>
    </div>

    <div class="ms-label">Papel de parede</div>
    <div class="ms-card pad">
      <div class="preview" :style="preview ? { backgroundImage: `url(${src(preview)})` } : {}" />
      <div class="seg">
        <button :class="{ on: target === 'ambos' }" @click="target = 'ambos'">Os dois</button>
        <button :class="{ on: target === 'inicio' }" @click="target = 'inicio'">Início</button>
        <button :class="{ on: target === 'bloqueio' }" @click="target = 'bloqueio'">Bloqueio</button>
      </div>
      <div class="walls">
        <button class="wall add" @click="gallery = true"><Icon name="image" :size="26" /><span>Da galeria</span></button>
        <button
          v-for="w in items"
          :key="w.path"
          class="wall"
          :class="{ on: w.path === preview }"
          :style="{ backgroundImage: `url(${src(w.path)})` }"
          :aria-label="w.name"
          @click="apply(w.path)"
        />
      </div>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>
    <MGallery v-if="gallery" @pick="fromGallery" @close="gallery = false" />
  </div>
</template>

<style scoped>
.bright {
  display: flex;
  align-items: center;
  gap: 14px;
}
.preview {
  height: 220px;
  width: 124px;
  margin: 0 auto;
  border: 1px solid rgba(255, 255, 255, 0.15);
  border-radius: 16px;
  background: radial-gradient(ellipse at 30% 60%, #1d1240, #07040f 70%);
  background-size: cover;
  background-position: center;
}
.seg {
  display: flex;
  gap: 4px;
  margin: 14px 0;
  padding: 4px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.05);
}
.seg button {
  flex: 1;
  padding: 9px 0;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.seg button.on {
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
.walls {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
}
.wall {
  aspect-ratio: 9 / 16;
  border: 2px solid transparent;
  border-radius: 12px;
  background-color: rgba(255, 255, 255, 0.06);
  background-size: cover;
  background-position: center;
}
.wall.on {
  border-color: var(--lumna-violet-soft);
}
.wall.add {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 6px;
  border: 1px dashed rgba(167, 139, 250, 0.5);
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 13px;
}
</style>
