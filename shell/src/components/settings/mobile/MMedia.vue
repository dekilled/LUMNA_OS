<script setup lang="ts">
// Ajustes → Modo Mídia (o botão Mídia do topo): no celular o Mídia é o LUMNA Controle, o
// controle do modo Mídia do PC. Abrir o controle, ver quais PCs estão no Mídia e o aviso.
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { connectComputers, connectMedia, connectSettings, connectSettingsSet, on, remoteOpen, type Computer, type ConnectSettings } from "../../../system";

const emit = defineEmits<{ open: [page: "connect"] }>();
const computers = ref<Computer[]>([]);
const media = ref<Record<string, string[]>>({});
const settings = ref<ConnectSettings | null>(null);

async function setNotify(on: boolean) {
  settings.value = await connectSettingsSet({ mediaNotify: on }).catch(() => settings.value);
}
const stops: (() => void)[] = [];
onMounted(async () => {
  computers.value = await connectComputers().catch(() => []);
  media.value = await connectMedia().catch(() => ({}));
  settings.value = await connectSettings().catch(() => null);
  stops.push(await on<Computer[]>("lumna://connect", (l) => (computers.value = l)));
  stops.push(await on<{ computer: string; media: string[] }>("lumna://connect-mode", (m) => (media.value = { ...media.value, [m.computer]: m.media })));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="ms-scroll">
    <div class="hero">
      <span class="big"><Icon name="play" :size="38" /></span>
      <p>No celular, o modo Mídia é o <b>LUMNA Controle</b>: ele comanda o modo Mídia do PC em qualquer tela dele (a TV, o projetor…).</p>
    </div>

    <div class="ms-card">
      <button class="ms-row" @click="remoteOpen()">
        <span class="ico violet"><Icon name="play" :size="24" /></span>
        <span class="txt"><strong>Abrir o controle</strong><small>Tela cheia, com setas, mídia, volume e teclado</small></span>
        <Icon class="chev" name="forward" :size="20" />
      </button>
    </div>

    <div class="ms-label">Computadores</div>
    <div class="ms-card">
      <button v-for="c in computers" :key="c.id" class="ms-row" :disabled="!c.online" @click="remoteOpen(c.id)">
        <span class="ico" :class="{ violet: c.online }"><Icon name="desktop" :size="24" /></span>
        <span class="txt">
          <strong>{{ c.name }}</strong>
          <small :class="{ on: media[c.id]?.length }">
            {{ !c.online ? "Fora do ar" : media[c.id]?.length ? `Mídia ligado em ${media[c.id].join(", ")}` : "No Desktop" }}
          </small>
        </span>
        <Icon class="chev" name="forward" :size="20" />
      </button>
      <button v-if="!computers.length" class="ms-row" @click="emit('open', 'connect')">
        <span class="txt"><strong>Nenhum PC pareado</strong><small>Toque pra parear no Celular Connect</small></span>
        <Icon class="chev" name="forward" :size="20" />
      </button>
    </div>

    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Avisar quando ligar o Mídia</strong><small>Aparece na central; tocar abre o controle</small></span>
        <button class="ms-switch" :class="{ on: settings?.media_notify }" :disabled="!settings" aria-label="Avisar" @click="setNotify(!settings?.media_notify)" />
      </div>
    </div>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 4px 0 18px;
  text-align: center;
}
.hero p {
  margin: 12px 8px 0;
  color: var(--lumna-muted);
  line-height: 1.4;
}
.big {
  width: 80px;
  height: 80px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
.ms-card + .ms-card {
  margin-top: 14px;
}
</style>
