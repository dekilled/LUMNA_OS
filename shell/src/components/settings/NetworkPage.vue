<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import WifiList from "../WifiList.vue";
import { networkInterfaces, wifiState, type NetInterface } from "../../system";

const interfaces = ref<NetInterface[]>([]);
const hasWifi = ref(true);
let timer: number | undefined;

async function refresh() {
  interfaces.value = await networkInterfaces();
}

function status(i: NetInterface) {
  if (i.connected && i.ipv4.length) return "Conectado";
  if (i.connected) return "Obtendo endereço…";
  return i.kind === "ethernet" ? "Cabo desconectado" : "Desconectado";
}

onMounted(async () => {
  hasWifi.value = (await wifiState()).available;
  await refresh();
  timer = window.setInterval(refresh, 3000);
});

onUnmounted(() => window.clearInterval(timer));
</script>

<template>
  <h1>Rede</h1>
  <p class="lead">Wi-Fi e cabo. O endereço vem sozinho do roteador (DHCP).</p>

  <section v-if="hasWifi" class="card padded wifi-card">
    <WifiList />
  </section>

  <h2>Conexões</h2>
  <div v-if="!interfaces.length" class="card"><p class="empty">Nenhuma placa de rede encontrada.</p></div>
  <section v-for="i in interfaces" :key="i.name" class="card padded conn">
    <div class="head">
      <span class="badge"><Icon :name="i.kind === 'wifi' ? 'wifi' : 'ethernet'" :size="18" /></span>
      <div class="grow">
        <div class="title">{{ i.kind === "wifi" ? "Wi-Fi" : "Cabo (LAN)" }}</div>
        <div class="sub">{{ i.name }}</div>
      </div>
      <span class="chip" :class="{ off: !i.connected }">{{ status(i) }}</span>
    </div>
    <dl v-if="i.connected">
      <template v-if="i.ipv4.length">
        <dt>Endereço IPv4</dt>
        <dd>{{ i.ipv4.join(", ") }}</dd>
      </template>
      <template v-if="i.ipv6.length">
        <dt>Endereço IPv6</dt>
        <dd>{{ i.ipv6.join(", ") }}</dd>
      </template>
      <template v-if="i.gateway">
        <dt>Roteador</dt>
        <dd>{{ i.gateway }}</dd>
      </template>
      <template v-if="i.dns.length">
        <dt>DNS</dt>
        <dd>{{ i.dns.join(", ") }}</dd>
      </template>
      <template v-if="i.speed">
        <dt>Velocidade</dt>
        <dd>{{ i.speed >= 1000 ? `${i.speed / 1000} Gb/s` : `${i.speed} Mb/s` }}</dd>
      </template>
      <dt>Endereço físico</dt>
      <dd>{{ i.mac }}</dd>
    </dl>
  </section>
</template>

<style scoped>
.wifi-card {
  max-width: 560px;
}
.conn {
  margin-bottom: 10px;
}
.head {
  display: flex;
  align-items: center;
  gap: 14px;
}
.head .badge {
  display: grid;
  place-items: center;
  width: 36px;
  height: 36px;
  border-radius: 10px;
  background: rgba(139, 92, 246, 0.16);
  color: var(--lumna-violet-soft);
}
.head .grow {
  flex: 1;
}
.head .title {
  font-size: 14px;
}
.head .sub {
  font-size: 12px;
  color: var(--lumna-muted);
}
dl {
  margin: 14px 0 2px 50px !important;
}
</style>
