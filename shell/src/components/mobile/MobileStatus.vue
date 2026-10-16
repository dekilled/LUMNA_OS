<script setup lang="ts">
// Celular: barra de status (hora à esquerda, rede e bateria à direita), por cima do notch.
import { onMounted, onUnmounted, ref } from "vue";
import type { UnlistenFn } from "@tauri-apps/api/event";
import Icon from "../Icon.vue";
import { on, status, type Status } from "../../system";

const sys = ref<Status | null>(null);
const time = ref("");
let timer = 0;
let stop: UnlistenFn | null = null;

const tick = () => (time.value = new Date().toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" }));

onMounted(async () => {
  tick();
  timer = window.setInterval(tick, 5000);
  sys.value = await status().catch(() => null);
  stop = await on<Status>("lumna://status", (s) => (sys.value = s));
});
onUnmounted(() => {
  clearInterval(timer);
  stop?.();
});
</script>

<template>
  <div class="status">
    <span class="time">{{ time }}</span>
    <span class="icons">
      <Icon :name="sys?.network === 'wifi' ? 'wifi' : sys?.network === 'ethernet' ? 'ethernet' : 'offline'" :size="15" />
      <span v-if="sys?.battery != null" class="battery">
        <Icon :name="sys.charging ? 'bolt' : 'battery'" :size="15" />{{ sys.battery }}%
      </span>
    </span>
  </div>
</template>

<style scoped>
.status {
  height: 100vh;
  box-sizing: border-box;
  /* o notch do 6T fica no meio: hora e ícones nas pontas */
  padding: 8px 18px 0;
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-size: 13px;
  font-weight: 500;
  background: linear-gradient(rgba(5, 3, 14, 0.85), rgba(5, 3, 14, 0));
}
.icons,
.battery {
  display: flex;
  align-items: center;
  gap: 6px;
}
.battery {
  gap: 2px;
}
</style>
