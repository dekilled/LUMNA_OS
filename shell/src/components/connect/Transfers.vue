<script setup lang="ts">
// Arquivos indo e vindo pelo Connect, com o progresso (o mesmo no celular e no PC).
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { connectTransfers, on, type Transfer } from "../../system";
import { formatSize } from "../files/format";

const props = defineProps<{ peer?: string }>();
const list = ref<Transfer[]>([]);
const shown = computed(() => list.value.filter((t) => !props.peer || t.peer === props.peer).slice(0, 8));

const stops: (() => void)[] = [];
onMounted(async () => {
  list.value = await connectTransfers().catch(() => []);
  stops.push(await on<Transfer[]>("lumna://connect-transfers", (l) => (list.value = l)));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div v-if="shown.length" class="transfers">
    <div v-for="t in shown" :key="t.id" class="t" :class="t.state">
      <Icon :name="t.direction === 'in' ? 'download' : 'upload'" :size="18" />
      <div class="info">
        <div class="name">{{ t.name }}</div>
        <div class="bar"><i :style="{ width: `${t.size ? Math.round((t.done / t.size) * 100) : 100}%` }" /></div>
        <small>
          <template v-if="t.state === 'running'">{{ formatSize(t.done) }} de {{ formatSize(t.size) }}</template>
          <template v-else-if="t.state === 'done'">{{ t.direction === "in" ? "Recebido" : "Enviado" }} · {{ formatSize(t.size) }}</template>
          <template v-else>{{ t.error ?? "Falhou" }}</template>
          <template v-if="!peer"> · {{ t.peer }}</template>
        </small>
      </div>
    </div>
  </div>
</template>

<style scoped>
.transfers {
  display: flex;
  flex-direction: column;
  gap: 8px;
}
.t {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 11px 14px;
  border-radius: 14px;
  background: rgba(20, 17, 30, 0.9);
  border: 1px solid rgba(255, 255, 255, 0.07);
  color: var(--lumna-violet-soft);
}
.info {
  flex: 1;
  min-width: 0;
  color: var(--lumna-text);
}
.name {
  font-size: 14px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.bar {
  margin: 6px 0 4px;
  height: 4px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.08);
  overflow: hidden;
}
.bar i {
  display: block;
  height: 100%;
  background: #8b5cf6;
}
.t.done .bar i {
  background: #4ade80;
}
.t.error .bar i {
  background: #f87171;
}
small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.t.error small {
  color: #fca5a5;
}
</style>
