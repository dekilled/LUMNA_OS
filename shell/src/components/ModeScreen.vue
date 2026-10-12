<script setup lang="ts">
// Tela cheia dos modos Gaming e Mídia, atrás das janelas. O conteúdo de verdade (biblioteca de
// jogos, streaming e vídeos) chega nas Fases 3 e 4; aqui fica a moldura de cada modo.
import { onMounted, onUnmounted, ref } from "vue";
import { modes, monitorName, on, type Mode, type ModesSnapshot } from "../system";

const monitor = ref<string | null>(null);
const mode = ref<Mode>("desktop");

function apply(snapshot: ModesSnapshot) {
  if (monitor.value) mode.value = snapshot.modes[monitor.value] ?? "desktop";
}

const stops: (() => void)[] = [];

onMounted(async () => {
  stops.push(await on<ModesSnapshot>("lumna://modes", apply));
  monitor.value = await monitorName().catch(() => null);
  apply(await modes());
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <!-- Sem <Transition>: enquanto a camada está escondida o WebKit não desenha quadros, e uma
       transição "out-in" ficaria parada no meio, mostrando o modo anterior. A entrada é uma
       animação CSS, que começa quando a camada aparece. -->
  <main v-if="mode === 'gaming'" key="gaming" class="screen gaming">
    <h1>Sua biblioteca</h1>
    <nav class="chips">
      <span class="chip active">Todos</span>
      <span class="chip">Ação</span>
      <span class="chip">Corrida</span>
      <span class="chip">Aventura</span>
      <span class="chip">RPG</span>
      <span class="chip">Estratégia</span>
    </nav>
    <section class="empty">
      <strong>Seus jogos aparecem aqui</strong>
      <p>A biblioteca da Steam, do Heroic e do Lutris chega na Fase 3.</p>
    </section>
  </main>

  <main v-else-if="mode === 'media'" key="media" class="screen media">
    <h1>Início</h1>
    <section class="empty">
      <strong>Streaming e seus vídeos aparecem aqui</strong>
      <p>Netflix, YouTube, Prime Video, Disney+ e os vídeos locais chegam na Fase 4.</p>
    </section>
  </main>
</template>

<style scoped>
.screen {
  animation: enter 320ms cubic-bezier(0.22, 1, 0.36, 1);
  position: fixed;
  inset: 0;
  padding: 48px 64px;
  box-sizing: border-box;
  display: flex;
  flex-direction: column;
  gap: 24px;
}
.gaming {
  background:
    radial-gradient(circle at 80% 0%, rgba(124, 58, 237, 0.35), transparent 55%),
    linear-gradient(180deg, #0d0a1c, #120c26);
}
.media {
  background:
    radial-gradient(circle at 20% 10%, rgba(167, 139, 250, 0.22), transparent 50%),
    linear-gradient(180deg, #0b0918, #150f2b);
}

h1 {
  margin: 0;
  font-size: 34px;
  font-weight: 700;
}

.chips {
  display: flex;
  gap: 10px;
}
.chip {
  padding: 9px 20px;
  border-radius: 999px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.04);
  color: var(--lumna-muted);
  font-size: 14px;
}
.chip.active {
  background: linear-gradient(135deg, #a78bfa, #7c3aed);
  border-color: transparent;
  color: #fff;
}

.empty {
  flex: 1;
  display: grid;
  place-content: center;
  text-align: center;
  gap: 6px;
  border: 1px dashed var(--lumna-line);
  border-radius: 24px;
  color: var(--lumna-muted);
}
.empty strong {
  color: var(--lumna-text);
  font-size: 18px;
}
.empty p {
  margin: 0;
}

@keyframes enter {
  from {
    opacity: 0;
    transform: scale(1.02);
  }
}
</style>
