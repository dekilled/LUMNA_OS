<script setup lang="ts">
// Cortina da Mídia: a mesma transição para todos os serviços. Fecha com a cor e o nome do serviço
// (ou "Início"), o LUMNA troca as janelas por baixo (abre, traz de volta ou estaciona) e ela abre
// de novo, mostrando o que ficou. Sem página branca carregando nem janela sumindo de repente.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { SERVICE_COLOR, SERVICE_STYLE } from "../mediaBrand";
import { on } from "../system";

const closed = ref(false);
const service = ref<string | null>(null);
const name = ref("Início");

const background = computed(() =>
  service.value && SERVICE_STYLE[service.value] ? SERVICE_STYLE[service.value] : "linear-gradient(180deg, #0b0918, #150f2b)",
);
const accent = computed(() => (service.value && SERVICE_COLOR[service.value]) || "#a78bfa");

/** Depois do fade de saída o LUMNA tira a camada da tela (main.rs, `curtain`). */
function apply(data: { closed: boolean; service: string | null; name: string }) {
  if (data.closed) {
    service.value = data.service;
    name.value = data.name;
  }
  closed.value = data.closed;
}

const stops: (() => void)[] = [];
onMounted(async () => stops.push(await on("lumna://tvcurtain", apply)));
onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="curtain" :class="{ closed }" :style="{ background, '--accent': accent }">
    <div class="center">
      <Icon v-if="!service" name="home" :size="44" />
      <Icon v-else-if="service === 'music'" name="music" :size="44" />
      <strong>{{ name }}</strong>
      <span class="bar"><span /></span>
    </div>
  </div>
</template>

<style scoped>
.curtain {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  color: #fff;
  opacity: 0;
  transition: opacity 380ms ease;
}
.curtain.closed {
  opacity: 1;
  transition-duration: 240ms;
}
.center {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 18px;
  transform: scale(0.96);
  transition: transform 420ms cubic-bezier(0.22, 1, 0.36, 1);
}
.closed .center {
  transform: none;
}
strong {
  font-size: 52px;
  font-weight: 700;
  letter-spacing: 0.01em;
  color: var(--accent);
  text-shadow: 0 6px 40px rgba(0, 0, 0, 0.5);
}
/* Barrinha de carregando, discreta. */
.bar {
  width: 160px;
  height: 3px;
  border-radius: 2px;
  background: rgba(255, 255, 255, 0.14);
  overflow: hidden;
}
.bar span {
  display: block;
  width: 40%;
  height: 100%;
  border-radius: 2px;
  background: var(--accent);
  animation: slide 1.1s ease-in-out infinite;
}
@keyframes slide {
  from {
    transform: translateX(-100%);
  }
  to {
    transform: translateX(250%);
  }
}
</style>
