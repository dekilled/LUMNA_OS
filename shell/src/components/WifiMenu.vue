<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import WifiList from "./WifiList.vue";
import { layerHide, on, settingsOpen } from "../system";

const visible = ref(false);
const list = ref<InstanceType<typeof WifiList>>();

function opened() {
  visible.value = true;
  list.value?.start();
}

function close() {
  visible.value = false;
  // Espera a animação de saída antes de esconder a camada.
  window.setTimeout(() => !visible.value && layerHide(), 160);
}

function more() {
  close();
  settingsOpen("rede");
}

function onKey(event: KeyboardEvent) {
  if (visible.value && event.key === "Escape") close();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://wifi-opened", opened));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <!-- O véu é transparente; clicar nele fecha o menu. -->
  <div class="veil" @click.self="close()">
    <section class="panel" :class="{ shown: visible }">
      <WifiList ref="list" :auto-start="false" />
      <button class="more" @click="more()">Configurações de rede…</button>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
}

.panel {
  position: absolute;
  top: 8px;
  right: 10px;
  width: 340px;
  max-height: calc(100vh - 24px);
  display: flex;
  flex-direction: column;
  box-sizing: border-box;
  padding: 14px;
  background: rgba(18, 13, 36, 0.9);
  border: 1px solid var(--lumna-line);
  border-radius: 20px;
  box-shadow: 0 20px 50px rgba(5, 2, 15, 0.55);
  opacity: 0;
  transform: translateY(-8px) scale(0.98);
  transform-origin: top right;
  transition: opacity 150ms ease, transform 220ms cubic-bezier(0.22, 1, 0.36, 1);
}
.panel.shown {
  opacity: 1;
  transform: none;
}

.more {
  margin-top: 8px;
  height: 36px;
  border: 0;
  border-top: 1px solid var(--lumna-line);
  background: transparent;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 13px;
  text-align: left;
  padding: 8px 10px 0;
}
.more:hover {
  color: var(--lumna-text);
}
</style>
