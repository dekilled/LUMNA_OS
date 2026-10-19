<script setup lang="ts">
// Os detalhes do arquivo da música: formato, qualidade, tamanho e onde está.
import { computed, ref, watch } from "vue";
import { filesWindow, musicDetails, type Track, type TrackDetails } from "../../system";
import { albumOf, artistOf, clock } from "./music";

const props = defineProps<{ track: Track | null }>();
const details = ref<TrackDetails | null>(null);

watch(
  () => props.track?.path,
  async (path) => {
    details.value = null;
    if (path) details.value = await musicDetails(path).catch(() => null);
  },
  { immediate: true },
);

const quality = computed(() => {
  const d = details.value;
  if (!d) return "";
  const parts = [];
  if (d.bitrate) parts.push(`${d.bitrate} kbps`);
  if (d.sample_rate) parts.push(`${(d.sample_rate / 1000).toLocaleString("pt-BR")} kHz`);
  if (d.bit_depth) parts.push(`${d.bit_depth} bits`);
  if (d.channels) parts.push(d.channels === 1 ? "mono" : d.channels === 2 ? "estéreo" : `${d.channels} canais`);
  return parts.join(" · ");
});
const size = (b: number) => (b < 1024 * 1024 ? `${Math.max(1, Math.round(b / 1024))} KB` : `${(b / 1024 / 1024).toFixed(1).replace(".", ",")} MB`);
const rows = computed(() => {
  const t = props.track;
  const d = details.value;
  if (!t) return [];
  return [
    ["Música", t.title],
    ["Artista", t.artist || artistOf(t)],
    ["Álbum", albumOf(t)],
    ["Faixa", t.track ? `${t.track}${t.disc ? ` (disco ${t.disc})` : ""}` : ""],
    ["Ano", t.year ? String(t.year) : ""],
    ["Gênero", d?.genre ?? ""],
    ["Duração", clock(t.duration)],
    ["Formato", d?.format ?? ""],
    ["Qualidade", quality.value],
    ["Tamanho", d?.size ? size(d.size) : ""],
  ].filter(([, v]) => v);
});
</script>

<template>
  <div class="details">
    <dl>
      <template v-for="[k, v] in rows" :key="k">
        <dt>{{ k }}</dt>
        <dd>{{ v }}</dd>
      </template>
    </dl>
    <button v-if="details?.folder" class="folder" @click="filesWindow(details.folder).catch(() => {})">{{ details.folder }}</button>
  </div>
</template>

<style scoped>
.details {
  padding: 4px 2px 20px;
}
dl {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 10px 18px;
  margin: 0;
}
dt {
  color: var(--lumna-muted);
  font-size: 14px;
}
dd {
  margin: 0;
  min-width: 0;
  overflow-wrap: anywhere;
  font-size: 14px;
}
.folder {
  margin-top: 16px;
  padding: 10px 12px;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 12px;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 13px;
  text-align: left;
  overflow-wrap: anywhere;
}
</style>
