<script setup lang="ts">
// Ajustes → Vibração e gestos: liga/desliga a vibração (toque e ligações) e mostra os gestos do
// sistema. (Mais pra frente: um app pra escolher os gestos e o que cada um faz.)
import { onMounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { haptic, hapticsGet, hapticsSet, type HapticsConfig } from "../../../system";

const config = ref<HapticsConfig | null>(null);
async function set(change: Partial<HapticsConfig>) {
  if (!config.value) return;
  config.value = { ...config.value, ...change };
  await hapticsSet(config.value).catch(() => {});
  if (change.touch) haptic(30).catch(() => {});
}
onMounted(async () => (config.value = await hapticsGet().catch(() => ({ touch: true, ring: true }))));

const GESTURES = [
  { icon: "keyboard", what: "Tocar ou deslizar curto a barrinha", does: "Abre e fecha o teclado" },
  { icon: "stack", what: "Deslizar longo a barrinha pra cima", does: "Apps recentes" },
  { icon: "home", what: "Da borda lateral, na diagonal pra baixo", does: "Volta pra tela Início" },
  { icon: "bell", what: "Puxar a barra de status pra baixo", does: "Notificações e atalhos" },
  { icon: "power", what: "Segurar o botão power", does: "Menu de energia" },
];
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-label">Vibração</div>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Vibrar ao tocar</strong><small>Teclado, gestos e botões</small></span>
        <button class="ms-switch" :class="{ on: config?.touch }" :disabled="!config" aria-label="Vibrar ao tocar" @click="set({ touch: !config?.touch })" />
      </div>
      <div class="ms-row">
        <span class="txt"><strong>Vibrar com ligação</strong><small>Enquanto uma ligação está chegando</small></span>
        <button class="ms-switch" :class="{ on: config?.ring }" :disabled="!config" aria-label="Vibrar com ligação" @click="set({ ring: !config?.ring })" />
      </div>
    </div>

    <div class="ms-label">Gestos</div>
    <div class="ms-card">
      <div v-for="g in GESTURES" :key="g.what" class="ms-row">
        <span class="ico violet"><Icon :name="g.icon" :size="24" /></span>
        <span class="txt"><strong>{{ g.does }}</strong><small>{{ g.what }}</small></span>
      </div>
    </div>
    <p class="ms-note">Em breve: escolher os gestos e o que cada um faz.</p>
  </div>
</template>
