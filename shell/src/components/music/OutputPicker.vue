<script setup lang="ts">
// Onde o som sai: "Neste celular/computador" (o alto-falante), o fone, a caixa Bluetooth...
// Troca a saída padrão do sistema (a música vai junto).
import { computed, onMounted, ref } from "vue";
import Icon from "../Icon.vue";
import { loadOutputs, outputLabel, outputs, setOutput } from "./store";

const props = defineProps<{ phone: boolean; up?: boolean; big?: boolean }>();
const open = ref(false);
const current = computed(() => outputs.value.find((o) => o.default) ?? outputs.value[0]);
const icon = (name: string) => (/bluetooth|bt|fone|headphone|headset/i.test(name) ? "headphones" : props.phone ? "phone" : "desktop");

async function toggle() {
  open.value = !open.value;
  if (open.value) await loadOutputs();
}
onMounted(loadOutputs);
</script>

<template>
  <div class="output" :class="{ big }">
    <button class="pick" @click="toggle()">
      <Icon :name="current ? icon(current.name) : phone ? 'phone' : 'desktop'" :size="big ? 22 : 18" />
      <span>{{ outputLabel(current, phone) }}</span>
      <Icon name="chevron-down" :size="big ? 18 : 16" />
    </button>
    <div v-if="open" class="veil" @click="open = false" />
    <ul v-if="open" class="list" :class="{ up }">
      <li v-for="o in outputs" :key="o.id">
        <button :class="{ on: o.default }" @click="setOutput(o); open = false">
          <Icon :name="icon(o.name)" :size="18" />
          <span>{{ outputLabel(o, phone) === o.name ? o.name : `${outputLabel(o, phone)} · ${o.name}` }}</span>
          <Icon v-if="o.default" name="check" :size="16" />
        </button>
      </li>
      <li v-if="!outputs.length" class="none">Nenhuma saída de som encontrada.</li>
    </ul>
  </div>
</template>

<style scoped>
.output {
  position: relative;
}
.pick {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 10px;
  height: 42px;
  padding: 0 12px;
  border: 1px solid rgba(167, 139, 250, 0.16);
  border-radius: 10px;
  background: rgba(24, 21, 36, 0.8);
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}
.big .pick {
  height: 60px;
  gap: 8px;
  padding: 0 12px 0 16px;
  border-radius: 999px;
  font-size: 15px;
}
.pick span {
  flex: 1;
  min-width: 0;
  overflow: hidden;
  text-align: left;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.pick :deep(svg) {
  flex: none;
  color: var(--lumna-violet-soft);
}
.veil {
  position: fixed;
  inset: 0;
  z-index: 30;
}
.list {
  position: absolute;
  left: 0;
  top: calc(100% + 6px);
  z-index: 31;
  min-width: 100%;
  width: max-content;
  max-width: 320px;
  margin: 0;
  padding: 6px;
  list-style: none;
  border: 1px solid rgba(167, 139, 250, 0.2);
  border-radius: 14px;
  background: #17141f;
  box-shadow: 0 14px 40px rgba(0, 0, 0, 0.55);
}
.list.up {
  top: auto;
  bottom: calc(100% + 6px);
  right: 0;
  left: auto;
}
.list button {
  width: 100%;
  display: flex;
  align-items: center;
  gap: 10px;
  min-height: 42px;
  padding: 0 10px;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
  text-align: left;
}
.list button:hover,
.list button.on {
  background: rgba(167, 139, 250, 0.12);
}
.list button span {
  flex: 1;
}
.none {
  padding: 10px;
  color: var(--lumna-muted);
  font-size: 13px;
}
</style>
