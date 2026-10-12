<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { audioSetDefault, audioSetVolume, audioState, audioToggleMute, type AudioNode, type AudioState } from "../../system";

const state = ref<AudioState | null>(null);
/** Controle sendo arrastado: a atualização periódica não mexe nele. */
const dragging = ref<number | null>(null);
let timer: number | undefined;
let pending: number | undefined;

async function refresh() {
  const fresh = await audioState();
  const id = dragging.value;
  if (id !== null && state.value) {
    // Mantém o valor que está sob o dedo.
    for (const list of [fresh.outputs, fresh.inputs, fresh.apps]) {
      const node = list.find((n) => n.id === id);
      const old = [...state.value.outputs, ...state.value.inputs, ...state.value.apps].find((n) => n.id === id);
      if (node && old) node.volume = old.volume;
    }
  }
  state.value = fresh;
}

function slide(node: AudioNode, event: Event) {
  node.volume = Number((event.target as HTMLInputElement).value);
  dragging.value = node.id;
  // Uma chamada a cada 60 ms basta para o som acompanhar o controle.
  window.clearTimeout(pending);
  pending = window.setTimeout(() => audioSetVolume(node.id, node.volume), 60);
}

async function mute(node: AudioNode) {
  await audioToggleMute(node.id).catch(() => {});
  await refresh();
}

async function choose(node: AudioNode) {
  if (node.default) return;
  await audioSetDefault(node.id).catch(() => {});
  await refresh();
}

onMounted(async () => {
  await refresh();
  timer = window.setInterval(refresh, 1500);
});

onUnmounted(() => window.clearInterval(timer));
</script>

<template>
  <h1>Som</h1>
  <p class="lead">Escolha por onde o som sai e entra, e o volume de cada app.</p>

  <template v-for="section in [
    { key: 'outputs', title: 'Saída', icon: 'volume', empty: 'Nenhuma saída de som encontrada.' },
    { key: 'inputs', title: 'Entrada (microfone)', icon: 'mic', empty: 'Nenhum microfone encontrado.' },
  ] as const" :key="section.key">
    <h2>{{ section.title }}</h2>
    <div class="card">
      <p v-if="!state?.[section.key].length" class="empty">{{ section.empty }}</p>
      <div v-for="n in state?.[section.key]" :key="n.id" class="row device" :class="{ chosen: n.default }">
        <button class="pick" :title="n.default ? 'Em uso' : 'Usar este'" @click="choose(n)">
          <span class="radio" />
          <span class="title">{{ n.name }}</span>
        </button>
        <div class="volume">
          <button class="mute" :title="n.muted ? 'Ligar o som' : 'Silenciar'" @click="mute(n)">
            <Icon :name="n.muted ? 'mute' : section.icon" :size="17" />
          </button>
          <input
            type="range"
            min="0"
            max="150"
            step="1"
            :value="n.volume"
            :class="{ muted: n.muted }"
            @input="slide(n, $event)"
            @change="dragging = null"
          />
          <span class="value">{{ n.volume }}%</span>
        </div>
      </div>
    </div>
  </template>

  <h2>Apps</h2>
  <div class="card">
    <p v-if="!state?.apps.length" class="empty">Nenhum app tocando som agora.</p>
    <div v-for="n in state?.apps" :key="n.id" class="row device">
      <span class="title app">{{ n.name }}</span>
      <div class="volume">
        <button class="mute" :title="n.muted ? 'Ligar o som' : 'Silenciar'" @click="mute(n)">
          <Icon :name="n.muted ? 'mute' : 'volume'" :size="17" />
        </button>
        <input
          type="range"
          min="0"
          max="150"
          step="1"
          :value="n.volume"
          :class="{ muted: n.muted }"
          @input="slide(n, $event)"
          @change="dragging = null"
        />
        <span class="value">{{ n.volume }}%</span>
      </div>
    </div>
  </div>
  <p class="note">Acima de 100% o som é amplificado e pode distorcer.</p>
</template>

<style scoped>
.device {
  flex-wrap: wrap;
}
.pick {
  flex: 1 1 220px;
  min-width: 0;
  display: flex;
  align-items: center;
  gap: 12px;
  border: 0;
  background: transparent;
  color: inherit;
  font: inherit;
  text-align: left;
  padding: 6px 0;
}
.radio {
  width: 16px;
  height: 16px;
  flex: none;
  border-radius: 50%;
  border: 2px solid var(--lumna-muted);
  box-sizing: border-box;
}
.chosen .radio {
  border: 5px solid var(--lumna-violet);
}
.app {
  flex: 1 1 220px;
}
.volume {
  flex: 1 1 260px;
  display: flex;
  align-items: center;
  gap: 10px;
}
.mute {
  display: grid;
  place-items: center;
  width: 32px;
  height: 32px;
  border: 0;
  border-radius: 9px;
  background: transparent;
  color: var(--lumna-muted);
}
.mute:hover {
  background: rgba(167, 139, 250, 0.14);
  color: var(--lumna-text);
}
input.muted {
  opacity: 0.4;
}
.value {
  width: 42px;
  text-align: right;
  font-size: 12px;
  color: var(--lumna-muted);
  font-variant-numeric: tabular-nums;
}
</style>
