<script setup lang="ts">
// A letra da música: sincronizada (um .lrc com o mesmo nome do arquivo, ou a tag em LRC) acende a
// linha da vez e rola junto; a da tag sem tempo aparece inteira.
import { computed, nextTick, ref, watch } from "vue";
import { musicLyrics, musicSeek, type Lyrics, type Track } from "../../system";

const props = defineProps<{ track: Track | null; position: number }>();
const lyrics = ref<Lyrics | null>(null);
const box = ref<HTMLElement>();

watch(
  () => props.track?.path,
  async (path) => {
    lyrics.value = null;
    if (path) lyrics.value = await musicLyrics(path).catch(() => ({ synced: [], plain: "" }));
  },
  { immediate: true },
);

const active = computed(() => {
  const lines = lyrics.value?.synced ?? [];
  let at = -1;
  for (let i = 0; i < lines.length && lines[i].time <= props.position + 0.3; i++) at = i;
  return at;
});
watch(active, async (i) => {
  await nextTick();
  box.value?.querySelector<HTMLElement>(`[data-i="${i}"]`)?.scrollIntoView({ block: "center", behavior: "smooth" });
});
</script>

<template>
  <div ref="box" class="lyrics">
    <p v-if="!lyrics" class="none">Procurando a letra…</p>
    <template v-else-if="lyrics.synced.length">
      <p
        v-for="(l, i) in lyrics.synced"
        :key="i"
        :data-i="i"
        class="line"
        :class="{ on: i === active, past: i < active }"
        @click="musicSeek(l.time).catch(() => {})"
      >
        {{ l.text || "♪" }}
      </p>
    </template>
    <p v-else-if="lyrics.plain" class="plain">{{ lyrics.plain }}</p>
    <p v-else class="none">
      Esta música não tem letra. Pra aparecer aqui, coloque um arquivo <b>.lrc</b> com o mesmo nome dela, na mesma pasta.
    </p>
  </div>
</template>

<style scoped>
.lyrics {
  padding: 4px 2px 20px;
}
.line {
  margin: 0;
  padding: 7px 0;
  color: rgba(236, 232, 255, 0.45);
  font-size: 20px;
  font-weight: 600;
  line-height: 1.35;
  cursor: pointer;
  transition: color 0.2s;
}
.line.past {
  color: rgba(236, 232, 255, 0.3);
}
.line.on {
  color: #f3efff;
}
.plain {
  margin: 0;
  color: #d9d3f0;
  font-size: 16px;
  line-height: 1.7;
  white-space: pre-wrap;
}
.none {
  color: var(--lumna-muted);
  line-height: 1.6;
}
</style>
