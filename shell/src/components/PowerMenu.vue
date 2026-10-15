<script setup lang="ts">
// Menu de energia: véu com blur na tela toda e os botões no centro. Setas e Enter navegam,
// Esc ou clique fora fecha.
import { computed, nextTick, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { layerHide, on, powerAction, powerState, type PowerAction, type PowerState } from "../system";

const visible = ref(false);
const state = ref<PowerState | null>(null);
const error = ref("");
const busy = ref<PowerAction | null>(null);
const buttons = ref<HTMLButtonElement[]>([]);

const ACTIONS = computed(() => {
  const s = state.value;
  const all: { id: PowerAction; label: string; icon: string; show: boolean }[] = [
    { id: "lock", label: "Bloquear", icon: "lock", show: true },
    { id: "logout", label: "Sair", icon: "back", show: true },
    { id: "suspend", label: "Suspender", icon: "moon", show: s?.can_suspend ?? true },
    { id: "hibernate", label: "Hibernar", icon: "download", show: s?.can_hibernate ?? false },
    { id: "reboot", label: "Reiniciar", icon: "refresh", show: s?.can_reboot ?? true },
    { id: "poweroff", label: "Desligar", icon: "power", show: s?.can_power_off ?? true },
  ];
  return all.filter((a) => a.show);
});

async function opened() {
  error.value = "";
  busy.value = null;
  visible.value = true;
  state.value = await powerState().catch(() => null);
  await nextTick();
  buttons.value[buttons.value.length - 1]?.focus();
}

function close() {
  visible.value = false;
  window.setTimeout(() => !visible.value && layerHide(), 160);
}

async function run(action: PowerAction) {
  busy.value = action;
  error.value = "";
  try {
    await powerAction(action);
    visible.value = false;
  } catch (e) {
    error.value = String(e);
    busy.value = null;
  }
}

function onKey(event: KeyboardEvent) {
  if (!visible.value) return;
  if (event.key === "Escape") close();
  const index = buttons.value.findIndex((b) => b === document.activeElement);
  if (event.key === "ArrowRight") buttons.value[(index + 1) % buttons.value.length]?.focus();
  if (event.key === "ArrowLeft") buttons.value[(index - 1 + buttons.value.length) % buttons.value.length]?.focus();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://power-opened", opened));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="veil" :class="{ shown: visible }" @click.self="close()">
    <section class="panel">
      <h1>Energia</h1>
      <div class="buttons">
        <button
          v-for="a in ACTIONS"
          :key="a.id"
          ref="buttons"
          class="action"
          :class="[a.id, { busy: busy === a.id }]"
          :disabled="busy !== null"
          @click="run(a.id)"
        >
          <span class="circle"><Icon :name="busy === a.id ? 'refresh' : a.icon" :size="26" /></span>
          <span class="label">{{ a.label }}</span>
        </button>
      </div>
      <p v-if="error" class="error">{{ error }}</p>
      <p class="hint">Esc para voltar</p>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
  display: grid;
  place-items: center;
  background: rgba(8, 5, 18, 0.55);
  opacity: 0;
  transition: opacity 160ms ease;
}
.veil.shown {
  opacity: 1;
}

.panel {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 22px;
  padding: 28px 34px 20px;
  border-radius: 24px;
  background: rgba(30, 31, 35, 0.92);
  border: 1px solid rgba(255, 255, 255, 0.08);
  box-shadow: 0 24px 60px rgba(0, 0, 0, 0.5);
  transform: scale(0.96);
  transition: transform 200ms cubic-bezier(0.22, 1, 0.36, 1);
}
.veil.shown .panel {
  transform: scale(1);
}
h1 {
  margin: 0;
  font-size: 15px;
  font-weight: 600;
  color: #9a9aa5;
  letter-spacing: 0.04em;
  text-transform: uppercase;
}

.buttons {
  display: flex;
  gap: 18px;
}
.action {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 10px;
  width: 96px;
  padding: 0;
  border: 0;
  background: transparent;
  color: #ececf1;
  font: inherit;
  cursor: pointer;
  outline: none;
}
.circle {
  display: grid;
  place-items: center;
  width: 72px;
  height: 72px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.06);
  border: 1px solid rgba(255, 255, 255, 0.08);
  transition: background 140ms ease, transform 160ms cubic-bezier(0.22, 1, 0.36, 1);
}
.action:hover .circle,
.action:focus-visible .circle,
.action:focus .circle {
  background: #7d46d7;
  transform: translateY(-2px);
}
.action.poweroff:hover .circle,
.action.poweroff:focus .circle {
  background: rgba(239, 68, 68, 0.9);
}
.action.busy .circle :deep(svg) {
  animation: spin 900ms linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
.action:disabled {
  cursor: default;
}
.label {
  font-size: 13px;
}
.error {
  margin: 0;
  max-width: 480px;
  color: #fca5a5;
  font-size: 13px;
  text-align: center;
}
.hint {
  margin: 0;
  font-size: 11px;
  color: #6b6b75;
}
</style>
