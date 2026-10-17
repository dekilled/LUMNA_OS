<script setup lang="ts">
// Celular: o menu de energia (segurar o botão power). Toque fora fecha.
import { ref } from "vue";
import Icon from "../Icon.vue";
import { lockNow, on, powerAction, powerMenuClose, quickAirplane, quickState } from "../../system";

const confirm = ref<"reboot" | "poweroff" | null>(null);
const airplane = ref(false);
const error = ref("");

async function act(a: "reboot" | "poweroff") {
  await powerAction(a).catch((e) => (error.value = String(e)));
}
async function lock() {
  await powerMenuClose();
  await lockNow();
}
async function toggleAirplane() {
  await quickAirplane(!airplane.value).catch((e) => (error.value = String(e)));
  airplane.value = (await quickState().catch(() => null))?.airplane ?? airplane.value;
}
on<boolean>("lumna://mobile-panel", async (shown) => {
  if (!shown) return;
  confirm.value = null;
  error.value = "";
  airplane.value = (await quickState().catch(() => null))?.airplane ?? false;
});
</script>

<template>
  <main class="veil" @click.self="powerMenuClose()">
    <div class="menu">
      <template v-if="!confirm">
        <button class="item danger" @click="confirm = 'poweroff'">
          <span class="ico"><Icon name="power" :size="28" /></span>Desligar
        </button>
        <button class="item" @click="confirm = 'reboot'">
          <span class="ico"><Icon name="refresh" :size="28" /></span>Reiniciar
        </button>
        <button class="item" @click="lock()">
          <span class="ico"><Icon name="lock" :size="28" /></span>Bloquear
        </button>
        <button class="item" :class="{ on: airplane }" @click="toggleAirplane()">
          <span class="ico"><Icon name="plane" :size="28" /></span>Modo avião
        </button>
      </template>
      <template v-else>
        <p class="ask">{{ confirm === "reboot" ? "Reiniciar o celular?" : "Desligar o celular?" }}</p>
        <div class="row">
          <button class="pill" @click="confirm = null">Cancelar</button>
          <button class="pill go" @click="act(confirm!)">{{ confirm === "reboot" ? "Reiniciar" : "Desligar" }}</button>
        </div>
      </template>
      <p v-if="error" class="error">{{ error }}</p>
    </div>
  </main>
</template>

<style scoped>
.veil {
  height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  background: rgba(5, 3, 14, 0.72);
  user-select: none;
}
.menu {
  width: 300px;
  padding: 18px;
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 12px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 28px;
  background: rgba(22, 18, 34, 0.96);
}
.item {
  padding: 18px 0 14px;
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  border: 0;
  border-radius: 20px;
  background: rgba(255, 255, 255, 0.05);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.ico {
  width: 58px;
  height: 58px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.08);
}
.item.danger .ico {
  background: rgba(239, 68, 68, 0.25);
  color: #fca5a5;
}
.item.on .ico {
  background: #b9a2fb;
  color: #1a1030;
}
.ask {
  grid-column: 1 / -1;
  margin: 6px 0;
  text-align: center;
  font-size: 18px;
}
.row {
  grid-column: 1 / -1;
  display: flex;
  gap: 10px;
}
.pill {
  flex: 1;
  padding: 14px 0;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 16px;
  background: none;
  color: inherit;
  font: inherit;
}
.pill.go {
  border: 0;
  background: #ef4444;
  color: #fff;
  font-weight: 600;
}
.error {
  grid-column: 1 / -1;
  margin: 0;
  color: #fca5a5;
  font-size: 13px;
  text-align: center;
}
</style>
