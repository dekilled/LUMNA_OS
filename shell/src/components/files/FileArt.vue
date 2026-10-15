<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { KIND_ICON } from "./format";
import { convertFileSrc, filesThumb, type FileKind } from "../../system";

// Ícone do arquivo, ou a miniatura (imagem/vídeo), pedida só quando aparece na tela.
const props = defineProps<{ path: string; kind: FileKind; size: number; big?: boolean }>();

const thumb = ref<string | null>(null);
const root = ref<HTMLElement>();
let observer: IntersectionObserver | undefined;

onMounted(() => {
  if (!props.path || (props.kind !== "image" && props.kind !== "video")) return;
  observer = new IntersectionObserver(async (entries) => {
    if (!entries.some((e) => e.isIntersecting)) return;
    observer?.disconnect();
    const file = await filesThumb(props.path).catch(() => null);
    if (file) thumb.value = convertFileSrc(file);
  });
  if (root.value) observer.observe(root.value);
});

onUnmounted(() => observer?.disconnect());
</script>

<template>
  <div ref="root" class="art" :class="[kind, { big, photo: thumb }]">
    <img v-if="thumb" :src="thumb" alt="" draggable="false" @error="thumb = null" />
    <Icon v-else :name="KIND_ICON[kind]" :size="big ? 34 : 20" />
    <span v-if="thumb && kind === 'video'" class="play" />
  </div>
</template>

<style scoped>
.art {
  position: relative;
  width: 36px;
  height: 36px;
  flex: none;
  display: grid;
  place-items: center;
  border-radius: 10px;
  color: #fff;
  background: linear-gradient(135deg, #6b6688, #3f3a5c);
}
.art.big {
  width: 76px;
  height: 64px;
  border-radius: 16px;
}
.art.photo {
  background: rgba(0, 0, 0, 0.3);
  overflow: hidden;
}
.art.big.photo {
  width: 104px;
  height: 72px;
  border-radius: 12px;
}
img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.play {
  position: absolute;
  width: 0;
  height: 0;
  border-left: 12px solid rgba(255, 255, 255, 0.9);
  border-top: 8px solid transparent;
  border-bottom: 8px solid transparent;
  filter: drop-shadow(0 0 4px rgba(0, 0, 0, 0.6));
}
.folder {
  background: linear-gradient(135deg, #a78bfa, #6d28d9);
}
.image {
  background: linear-gradient(135deg, #f9a8d4, #db2777);
}
.video {
  background: linear-gradient(135deg, #fca5a5, #dc2626);
}
.audio {
  background: linear-gradient(135deg, #86efac, #16a34a);
}
.pdf {
  background: linear-gradient(135deg, #fda4af, #be123c);
}
.document {
  background: linear-gradient(135deg, #93c5fd, #2563eb);
}
.sheet {
  background: linear-gradient(135deg, #6ee7b7, #059669);
}
.slides {
  background: linear-gradient(135deg, #fdba74, #ea580c);
}
.archive {
  background: linear-gradient(135deg, #fcd34d, #b45309);
}
.code {
  background: linear-gradient(135deg, #67e8f9, #0e7490);
}
.app {
  background: linear-gradient(135deg, #c4b5fd, #7c3aed);
}
.disk {
  background: linear-gradient(135deg, #94a3b8, #475569);
}
</style>
