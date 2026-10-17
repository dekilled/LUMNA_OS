<script setup lang="ts">
// Galeria de fotos do celular (Imagens, Downloads, DCIM...), pra escolher uma imagem.
// As miniaturas carregam aos poucos, conforme aparecem na tela.
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { convertFileSrc, filesThumb, wallpaperGallery } from "../../../system";

const emit = defineEmits<{ pick: [path: string]; close: [] }>();
const photos = ref<string[]>([]);
const thumbs = ref<Record<string, string | null>>({});
const loading = ref(true);
let observer: IntersectionObserver | null = null;

function watchCell(el: unknown, path: string) {
  if (!(el instanceof HTMLElement) || !observer) return;
  el.dataset.path = path;
  observer.observe(el);
}

onMounted(async () => {
  observer = new IntersectionObserver((entries) => {
    for (const e of entries) {
      const path = (e.target as HTMLElement).dataset.path;
      if (!e.isIntersecting || !path || path in thumbs.value) continue;
      thumbs.value[path] = null;
      filesThumb(path)
        .then((t) => (thumbs.value[path] = t ? convertFileSrc(t) : ""))
        .catch(() => (thumbs.value[path] = ""));
      observer?.unobserve(e.target);
    }
  });
  photos.value = await wallpaperGallery().catch(() => []);
  loading.value = false;
});
onUnmounted(() => observer?.disconnect());
</script>

<template>
  <div class="ms-sheet-veil" @click.self="emit('close')">
    <div class="ms-sheet gallery">
      <div class="top">
        <h2>Escolher foto</h2>
        <button class="ms-icon-btn" aria-label="Fechar" @click="emit('close')"><Icon name="close" :size="22" /></button>
      </div>
      <div class="grid">
        <button v-for="p in photos" :key="p" :ref="(el) => watchCell(el, p)" class="cell" @click="emit('pick', p)">
          <img v-if="thumbs[p]" :src="thumbs[p]!" alt="" loading="lazy" />
          <Icon v-else name="image" :size="26" />
        </button>
      </div>
      <p v-if="!loading && !photos.length" class="ms-note">
        Nenhuma foto em Imagens, Downloads ou DCIM. Coloque imagens numa dessas pastas (pelo Arquivos ou pela USB).
      </p>
    </div>
  </div>
</template>

<style scoped>
.gallery {
  max-height: 82vh;
  display: flex;
  flex-direction: column;
}
.top {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.top h2 {
  margin: 0;
}
.grid {
  flex: 1;
  min-height: 0;
  margin-top: 12px;
  overflow-y: auto;
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 6px;
}
.cell {
  aspect-ratio: 1;
  display: grid;
  place-items: center;
  overflow: hidden;
  border: 0;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.06);
  color: var(--lumna-muted);
}
.cell img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
</style>
