<script setup lang="ts">
// Propriedades da rede conectada: sinal, segurança, frequência, IP; desconectar ou esquecer.
import { onMounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { wifiDetails, wifiDisconnect, wifiForget, wifiState, type WifiDetails } from "../../../system";

defineProps<{ arg?: string }>();
const emit = defineEmits<{ back: [] }>();
const d = ref<WifiDetails | null>(null);
const error = ref("");
const loading = ref(true);

const band = (freq: string) => (parseInt(freq) >= 5000 ? "5 GHz" : parseInt(freq) > 0 ? "2,4 GHz" : "");

async function disconnect() {
  try {
    await wifiDisconnect();
    emit("back");
  } catch (e) {
    error.value = String(e);
  }
}
async function forget() {
  const net = (await wifiState().catch(() => null))?.networks.find((n) => n.connected);
  if (!net) return;
  try {
    await wifiForget(net.path);
    emit("back");
  } catch (e) {
    error.value = String(e);
  }
}
onMounted(async () => {
  d.value = await wifiDetails().catch(() => null);
  loading.value = false;
});
</script>

<template>
  <div class="ms-scroll">
    <div v-if="d" class="hero">
      <span class="big"><Icon name="wifi" :size="40" /></span>
      <h2>{{ d.ssid }}</h2>
      <p>Conectado · sinal {{ d.signal }}%</p>
    </div>
    <p v-else class="ms-note">{{ loading ? "Lendo a rede…" : "Nenhuma rede conectada." }}</p>

    <div v-if="d" class="ms-card">
      <div class="ms-row"><span class="txt"><strong>Segurança</strong></span><span class="value">{{ d.security }}</span></div>
      <div class="ms-row"><span class="txt"><strong>Frequência</strong></span><span class="value">{{ band(d.frequency) || d.frequency }}</span></div>
      <div v-if="d.rate" class="ms-row"><span class="txt"><strong>Velocidade</strong></span><span class="value">{{ d.rate }}</span></div>
      <div class="ms-row"><span class="txt"><strong>Endereço IP</strong></span><span class="value">{{ d.ipv4 || "—" }}</span></div>
      <div class="ms-row"><span class="txt"><strong>Roteador</strong></span><span class="value">{{ d.gateway || "—" }}</span></div>
      <div v-if="d.dns.length" class="ms-row"><span class="txt"><strong>DNS</strong></span><span class="value">{{ d.dns.join(", ") }}</span></div>
      <div class="ms-row"><span class="txt"><strong>MAC</strong></span><span class="value">{{ d.mac }}</span></div>
    </div>
    <div v-if="d" class="ms-actions">
      <button class="ms-btn" @click="disconnect()">Desconectar</button>
      <button class="ms-btn danger" @click="forget()">Esquecer rede</button>
    </div>
    <p v-if="error" class="ms-error">{{ error }}</p>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 8px 0 20px;
}
.big {
  width: 84px;
  height: 84px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
h2 {
  margin: 14px 0 2px;
  font-size: 24px;
  font-weight: 500;
}
.hero p {
  margin: 0;
  color: var(--lumna-violet-soft);
}
.value {
  max-width: 60%;
  text-align: right;
  overflow-wrap: anywhere;
}
</style>
