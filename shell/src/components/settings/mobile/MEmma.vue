<script setup lang="ts">
// Emma: se ela está no ar e o que ela pode fazer sozinha, perguntando ou nunca.
import { onMounted, ref } from "vue";
import Icon from "../../Icon.vue";
import {
  emmaPermissions,
  emmaSetPermission,
  emmaStatus,
  emmaAppOpen,
  type ActionInfo,
  type ActionPermission,
  type EmmaStatus,
} from "../../../system";

defineProps<{ arg?: string }>();
const status = ref<EmmaStatus | null>(null);
const actions = ref<ActionInfo[]>([]);
const error = ref("");
const OPTIONS: { id: ActionPermission; label: string }[] = [
  { id: "auto", label: "Sozinha" },
  { id: "ask", label: "Pergunta" },
  { id: "off", label: "Nunca" },
];
const RISK: Record<string, string> = { read: "Só lê", act: "Age no sistema", danger: "Pode apagar ou desligar" };

async function set(a: ActionInfo, p: ActionPermission) {
  try {
    await emmaSetPermission(a.name, p);
    a.permission = p;
  } catch (e) {
    error.value = String(e);
  }
}
onMounted(async () => {
  status.value = await emmaStatus().catch(() => null);
  actions.value = await emmaPermissions().catch(() => []);
});
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-card">
      <button class="ms-row" @click="emmaAppOpen()">
        <span class="ico violet"><Icon name="mic" :size="26" /></span>
        <span class="txt">
          <strong>Emma</strong>
          <small :class="{ on: status?.online }">{{ status?.online ? "No ar" : status?.configured ? "Fora do ar" : "Sem servidor configurado" }}</small>
        </span>
        <span class="value">Abrir</span>
      </button>
    </div>

    <div class="ms-label">O que ela pode fazer</div>
    <div class="ms-card">
      <div v-for="a in actions" :key="a.name" class="ms-row column">
        <span class="txt">
          <strong>{{ a.label }}</strong>
          <small :class="{ danger: a.risk === 'danger' }">{{ RISK[a.risk] ?? a.description }}</small>
        </span>
        <div class="seg">
          <button v-for="o in OPTIONS" :key="o.id" :class="{ on: a.permission === o.id }" @click="set(a, o.id)">{{ o.label }}</button>
        </div>
      </div>
      <p v-if="!actions.length" class="ms-note pad">Nenhuma ação disponível.</p>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>
  </div>
</template>

<style scoped>
.column {
  flex-direction: column;
  align-items: stretch;
  gap: 10px;
}
.pad {
  padding: 16px 18px;
  margin: 0;
}
.danger {
  color: #fca5a5 !important;
}
.seg {
  display: flex;
  gap: 4px;
  padding: 4px;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.05);
}
.seg button {
  flex: 1;
  padding: 8px 0;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}
.seg button.on {
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
</style>
