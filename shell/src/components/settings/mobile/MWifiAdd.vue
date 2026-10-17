<script setup lang="ts">
// Adicionar rede à mão: nome, segurança, senha e se ela é oculta.
import { ref } from "vue";
import { wifiAdd } from "../../../system";

defineProps<{ arg?: string }>();
const emit = defineEmits<{ back: [] }>();
const ssid = ref("");
const security = ref<"psk" | "open">("psk");
const password = ref("");
const hidden = ref(true);
const busy = ref(false);
const error = ref("");

async function save() {
  busy.value = true;
  error.value = "";
  try {
    await wifiAdd(ssid.value.trim(), security.value === "open" ? "" : password.value, hidden.value);
    emit("back");
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
}
</script>

<template>
  <div class="ms-scroll">
    <label class="ms-field">
      <small>Nome da rede (SSID)</small>
      <input v-model="ssid" placeholder="Minha rede" autocomplete="off" />
    </label>
    <label class="ms-field">
      <small>Segurança</small>
      <select v-model="security">
        <option value="psk">WPA/WPA2/WPA3 (senha)</option>
        <option value="open">Aberta (sem senha)</option>
      </select>
    </label>
    <label v-if="security === 'psk'" class="ms-field">
      <small>Senha</small>
      <input v-model="password" type="password" autocomplete="off" />
    </label>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Rede oculta</strong><small>Ela não aparece na lista de redes</small></span>
        <button class="ms-switch" :class="{ on: hidden }" aria-label="Rede oculta" @click="hidden = !hidden" />
      </div>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>
    <div class="ms-actions">
      <button class="ms-btn primary" :disabled="busy || !ssid.trim() || (security === 'psk' && password.length < 8)" @click="save()">
        {{ busy ? "Conectando…" : "Conectar" }}
      </button>
    </div>
  </div>
</template>
