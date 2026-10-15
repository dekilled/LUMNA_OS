<script setup lang="ts">
// Tela cheia dos modos Gaming e Mídia, atrás das janelas: a biblioteca de jogos e a tela de
// streaming e vídeos.
import { onMounted, onUnmounted, ref } from "vue";
import { modes, monitorName, on, type Mode, type ModesSnapshot } from "../system";
import GameLibrary from "./GameLibrary.vue";
import MediaHome from "./MediaHome.vue";

const monitor = ref<string | null>(null);
const mode = ref<Mode>("desktop");

function apply(snapshot: ModesSnapshot) {
  if (monitor.value) mode.value = snapshot.modes[monitor.value] ?? "desktop";
}

const stops: (() => void)[] = [];

onMounted(async () => {
  stops.push(await on<ModesSnapshot>("lumna://modes", apply));
  // Monitores religados (acordou da suspensão, plugou ou tirou um): esta tela pode estar noutro.
  stops.push(
    await on("lumna://monitors-relayout", async () => {
      monitor.value = await monitorName().catch(() => null);
      apply(await modes());
    }),
  );
  monitor.value = await monitorName().catch(() => null);
  apply(await modes());
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <!-- Sem <Transition>: enquanto a camada está escondida o WebKit não desenha quadros, e uma
       transição "out-in" ficaria parada no meio, mostrando o modo anterior. A entrada é uma
       animação CSS, que começa quando a camada aparece. -->
  <GameLibrary v-if="mode === 'gaming'" :monitor="monitor" />
  <MediaHome v-else-if="mode === 'media'" :monitor="monitor" />
</template>
