<script setup lang="ts">
// Configurações → Celular (PC): parear o LUMNA do celular (Celular Connect) ou o app LUMNA
// Controle. Enquanto a página está aberta, o código de 6 dígitos vale (5 minutos, uma vez só).
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import { on, remoteForget, remotePairing, remoteStopPairing, type Pairing } from "../../system";

const pairing = ref<Pairing | null>(null);
const left = ref(0);
const paired = ref("");
const error = ref("");
let timer = 0;

async function refresh() {
  try {
    pairing.value = await remotePairing();
    left.value = pairing.value.expires_in;
    error.value = "";
  } catch (e) {
    error.value = String(e);
  }
}
async function forget() {
  await remoteForget().catch((e) => (error.value = String(e)));
  refresh();
}
const stops: (() => void)[] = [];
onMounted(async () => {
  await refresh();
  timer = window.setInterval(() => {
    left.value = Math.max(0, left.value - 1);
    if (left.value === 0) refresh();
  }, 1000);
  stops.push(
    await on<string>("lumna://remote-paired", (name) => {
      paired.value = name;
      refresh();
    }),
  );
});
onUnmounted(() => {
  clearInterval(timer);
  remoteStopPairing().catch(() => {});
  stops.forEach((s) => s());
});
</script>

<template>
  <section class="page">
    <h1>Celular</h1>
    <p class="lead">
      Pareie o <b>LUMNA no celular</b> (Ajustes → Celular Connect) ou o app <b>LUMNA Controle</b>. O celular recebe os avisos
      deste PC, pedidos de senha e confirmações da Emma, e pode mandar comandos de longe.
    </p>
    <p v-if="error" class="error">{{ error }}</p>

    <h2>Código de pareamento</h2>
    <div class="card padded code-card">
      <div class="code">{{ pairing?.code.replace(/(\d{3})(\d{3})/, "$1 $2") ?? "······" }}</div>
      <div class="note">Vale por mais {{ Math.floor(left / 60) }}:{{ String(left % 60).padStart(2, "0") }} e uma vez só.</div>
      <div v-if="pairing?.addresses.length" class="note">
        Se o celular não achar este PC sozinho, digite: <b>{{ pairing.addresses.join(" ou ") }}</b>
      </div>
      <div v-if="paired" class="ok"><Icon name="check" :size="16" /> {{ paired }} pareado!</div>
    </div>

    <h2>Celulares pareados</h2>
    <div class="card">
      <div v-for="d in pairing?.devices ?? []" :key="d" class="row">
        <span class="badge"><Icon name="phone" :size="18" /></span>
        <div class="grow"><div class="title">{{ d }}</div></div>
      </div>
      <div v-if="!pairing?.devices.length" class="row"><div class="grow sub">Nenhum ainda.</div></div>
    </div>
    <div class="forget">
      <button class="btn danger" :disabled="!pairing?.devices.length" @click="forget()">Esquecer todos os celulares</button>
    </div>

    <h2>Fora de casa</h2>
    <p class="away">
      Com o <b>Tailscale</b> ligado neste PC e no celular, o Connect funciona de qualquer lugar: no celular, adicione o PC pelo
      endereço 100.x.y.z do Tailscale.
    </p>
  </section>
</template>

<style scoped>
.code-card {
  text-align: center;
}
.code {
  font-size: 44px;
  letter-spacing: 0.15em;
  font-variant-numeric: tabular-nums;
  margin: 6px 0 8px;
}
.forget {
  margin-top: 10px;
}
.away {
  margin: 0;
  color: var(--lumna-muted);
  font-size: 14px;
  line-height: 1.5;
}
.ok {
  margin-top: 10px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #4ade80;
}
</style>
