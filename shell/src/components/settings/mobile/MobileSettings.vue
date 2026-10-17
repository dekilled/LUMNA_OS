<script setup lang="ts">
// Ajustes no celular: a tela do esboço (busca, modo do sistema, Conexões, Seu LUMNA) e uma
// página própria pra cada item, com voltar. As páginas que já servem no celular (Som, Dados
// móveis, Sistema) são as mesmas do PC, dentro do cabeçalho daqui.
import { computed, ref, watch, type Component } from "vue";
import Icon from "../../Icon.vue";
import AudioPage from "../AudioPage.vue";
import MobileDataPage from "../MobileDataPage.vue";
import SystemPage from "../SystemPage.vue";
import MBluetooth from "./MBluetooth.vue";
import MBtDevice from "./MBtDevice.vue";
import MConnect from "./MConnect.vue";
import MConnectPc from "./MConnectPc.vue";
import MDisplay from "./MDisplay.vue";
import MEmma from "./MEmma.vue";
import MEnergy from "./MEnergy.vue";
import MKeyboard from "./MKeyboard.vue";
import MMedia from "./MMedia.vue";
import MProfile from "./MProfile.vue";
import MWifi from "./MWifi.vue";
import MWifiAdd from "./MWifiAdd.vue";
import MWifiDetails from "./MWifiDetails.vue";
import { profileGet, convertFileSrc, toggleEmma, type Profile } from "../../../system";
import "./mobile.css";

export type MPage =
  | "home" | "wifi" | "wifi-detalhes" | "wifi-adicionar" | "bluetooth" | "bt-aparelho" | "connect" | "connect-pc" | "dados"
  | "tela" | "som" | "teclado" | "emma" | "energia" | "sistema" | "perfil" | "midia";

const props = defineProps<{ start?: MPage }>();

interface Entry {
  page: MPage;
  title: string;
  sub: string;
  icon: string;
  keys: string;
}
const CONNECTIONS: Entry[] = [
  { page: "wifi", title: "Wi-Fi", sub: "Redes sem fio", icon: "wifi", keys: "internet rede senha roteador" },
  { page: "dados", title: "Dados móveis", sub: "Chip, APN e uso", icon: "signal", keys: "4g operadora chip sim apn roaming internet" },
  { page: "bluetooth", title: "Bluetooth", sub: "Dispositivos", icon: "bluetooth", keys: "fone caixa parear" },
  { page: "connect", title: "Celular Connect", sub: "Integração com o computador", icon: "desktop", keys: "pc computador emma remoto controle" },
];
const YOURS: Entry[] = [
  { page: "tela", title: "Tela e aparência", sub: "Brilho, bloqueio e papel de parede", icon: "screens", keys: "brilho wallpaper fundo tema bloqueio senha apagar tela power" },
  { page: "som", title: "Som e microfone", sub: "Volume e entrada", icon: "volume", keys: "audio alto-falante fone microfone volume" },
  { page: "teclado", title: "Teclado e entrada", sub: "Vibração, repetição e sugestões", icon: "keyboard", keys: "lumna keyboard digitar vibrar apagar" },
  { page: "emma", title: "Emma", sub: "Voz, contexto e ações", icon: "mic", keys: "assistente ia permissões" },
];
const DEVICE: Entry[] = [
  { page: "energia", title: "Bateria e energia", sub: "Carga, saúde e economia", icon: "battery", keys: "bateria carregar economia desligar reiniciar" },
  { page: "sistema", title: "Sobre o LUMNA", sub: "Aparelho e versão", icon: "info", keys: "sistema versão kernel memória" },
];
const ALL = [...CONNECTIONS, ...YOURS, ...DEVICE];
const TITLES: Record<MPage, string> = {
  home: "Ajustes",
  wifi: "Wi-Fi",
  "wifi-detalhes": "Rede conectada",
  "wifi-adicionar": "Adicionar rede",
  bluetooth: "Bluetooth",
  "bt-aparelho": "Aparelho",
  connect: "Celular Connect",
  "connect-pc": "Computador",
  dados: "Dados móveis",
  tela: "Tela e aparência",
  som: "Som e microfone",
  teclado: "Teclado e entrada",
  emma: "Emma",
  energia: "Bateria e energia",
  sistema: "Sobre o LUMNA",
  perfil: "Perfil",
  midia: "Modo Mídia",
};
// páginas do PC que servem aqui (já trazem o próprio título)
const EMBEDDED: Partial<Record<MPage, Component>> = { som: AudioPage, dados: MobileDataPage, sistema: SystemPage };
const PAGES: Partial<Record<MPage, Component>> = {
  wifi: MWifi,
  "wifi-detalhes": MWifiDetails,
  "wifi-adicionar": MWifiAdd,
  bluetooth: MBluetooth,
  "bt-aparelho": MBtDevice,
  connect: MConnect,
  "connect-pc": MConnectPc,
  tela: MDisplay,
  teclado: MKeyboard,
  emma: MEmma,
  energia: MEnergy,
  perfil: MProfile,
  midia: MMedia,
};

// pilha de navegação: [página, argumento]
const stack = ref<[MPage, string?][]>(props.start && props.start !== "home" ? [["home"], [props.start]] : [["home"]]);
const current = computed(() => stack.value[stack.value.length - 1]);
function open(page: MPage, arg?: string) {
  stack.value = [...stack.value, [page, arg]];
}
function back() {
  if (stack.value.length > 1) stack.value = stack.value.slice(0, -1);
}
watch(
  () => props.start,
  (p) => {
    if (p) stack.value = p === "home" ? [["home"]] : [["home"], [p]];
  },
);

// ---- home ----
const query = ref("");
const results = computed(() => {
  const q = query.value.trim().toLowerCase();
  if (!q) return [];
  return ALL.filter((e) => `${e.title} ${e.sub} ${e.keys}`.toLowerCase().includes(q));
});
// o celular fica sempre no Desktop; o botão Mídia abre os ajustes do modo Mídia (o LUMNA Controle)
const mode = ref<"desktop" | "midia">("desktop");
const modeNote = ref("");
function pickMode(m: "desktop" | "midia") {
  if (m === "midia") open("midia");
}
const profile = ref<Profile | null>(null);
profileGet()
  .then((p) => (profile.value = p))
  .catch(() => {});
watch(current, ([page]) => {
  if (page === "home") profileGet().then((p) => (profile.value = p)).catch(() => {});
});
</script>

<template>
  <div class="ms">
    <!-- home -->
    <template v-if="current[0] === 'home'">
      <div class="ms-scroll">
        <div class="brand"><span class="gem" /> LUMNA</div>
        <div class="title-row">
          <h1>Ajustes</h1>
          <button class="avatar" aria-label="Perfil" @click="open('perfil')">
            <img v-if="profile?.photo" :src="`${convertFileSrc(profile.photo)}?t=${Date.now()}`" alt="" />
            <Icon v-else name="user" :size="24" />
          </button>
        </div>
        <label class="search">
          <Icon name="search" :size="22" />
          <input v-model="query" placeholder="Buscar configuração" />
        </label>

        <template v-if="query.trim()">
          <div class="ms-card results">
            <button v-for="e in results" :key="e.page" class="ms-row" @click="open(e.page)">
              <span class="ico"><Icon :name="e.icon" :size="24" /></span>
              <span class="txt"><strong>{{ e.title }}</strong><small>{{ e.sub }}</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
            <p v-if="!results.length" class="ms-note pad">Nada encontrado pra "{{ query }}".</p>
          </div>
        </template>

        <template v-else>
          <div class="ms-card modes">
            <span>Modo do sistema</span>
            <div class="seg">
              <button :class="{ on: mode === 'desktop' }" @click="pickMode('desktop')">Desktop</button>
              <button :class="{ on: mode === 'midia' }" @click="pickMode('midia')">Mídia</button>
            </div>
          </div>
          <p v-if="modeNote" class="ms-note">{{ modeNote }}</p>

          <div class="ms-label">Conexões</div>
          <div class="ms-card">
            <button v-for="e in CONNECTIONS" :key="e.page" class="ms-row" @click="open(e.page)">
              <span class="ico"><Icon :name="e.icon" :size="26" /></span>
              <span class="txt"><strong>{{ e.title }}</strong><small>{{ e.sub }}</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
          </div>

          <div class="ms-label">Seu LUMNA</div>
          <div class="ms-card">
            <button v-for="e in YOURS" :key="e.page" class="ms-row" @click="open(e.page)">
              <span class="ico"><Icon :name="e.icon" :size="26" /></span>
              <span class="txt"><strong>{{ e.title }}</strong><small>{{ e.sub }}</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
          </div>

          <div class="ms-label">Aparelho</div>
          <div class="ms-card">
            <button v-for="e in DEVICE" :key="e.page" class="ms-row" @click="open(e.page)">
              <span class="ico"><Icon :name="e.icon" :size="26" /></span>
              <span class="txt"><strong>{{ e.title }}</strong><small>{{ e.sub }}</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
          </div>

          <div class="ms-card help">
            <button class="ms-row" @click="toggleEmma()">
              <span class="ico violet"><Icon name="mic" :size="26" /></span>
              <span class="txt"><strong>Precisa de ajuda?</strong><small>Pergunte à Emma</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
          </div>
        </template>
      </div>
    </template>

    <!-- páginas -->
    <template v-else>
      <header class="ms-head">
        <button class="ms-icon-btn" aria-label="Voltar" @click="back()"><Icon name="arrow-left" :size="24" /></button>
        <h1>{{ TITLES[current[0]] }}</h1>
      </header>
      <div v-if="EMBEDDED[current[0]]" class="ms-scroll">
        <div class="settings embed"><component :is="EMBEDDED[current[0]]" /></div>
      </div>
      <component :is="PAGES[current[0]]" v-else :key="current.join(':')" :arg="current[1]" @open="open" @back="back" />
    </template>
  </div>
</template>

<style scoped>
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  margin: 10px 4px 0;
  letter-spacing: 0.5em;
  font-size: 15px;
}
.gem {
  width: 18px;
  height: 18px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin: 22px 4px 18px;
}
.title-row h1 {
  margin: 0;
  font-size: 36px;
  font-weight: 500;
}
.avatar {
  width: 52px;
  height: 52px;
  display: grid;
  place-items: center;
  overflow: hidden;
  border: 1px solid rgba(255, 255, 255, 0.15);
  border-radius: 50%;
  background: rgba(30, 26, 46, 0.9);
  color: inherit;
}
.avatar img {
  width: 100%;
  height: 100%;
  object-fit: cover;
}
.search {
  display: flex;
  align-items: center;
  gap: 14px;
  height: 54px;
  padding: 0 18px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 999px;
  background: rgba(20, 17, 30, 0.9);
  color: var(--lumna-muted);
}
.search input {
  flex: 1;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 17px;
  outline: none;
}
.results {
  margin-top: 14px;
}
.pad {
  padding: 16px 18px;
}
.modes {
  margin-top: 14px;
  padding: 8px 8px 8px 18px;
  display: flex;
  align-items: center;
  gap: 10px;
}
.modes > span {
  flex: 1;
  font-size: 15px;
}
.seg {
  display: flex;
  gap: 4px;
}
.seg button {
  padding: 10px 20px;
  border: 0;
  border-radius: 14px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
}
.seg button.on {
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
.help {
  margin-top: 18px;
}
.embed {
  padding-top: 4px;
}
.embed :deep(h1) {
  display: none;
}
</style>
