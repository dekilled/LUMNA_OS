<script setup lang="ts">
// Celular: barra de navegação de baixo. Início e Apps são abas da tela Início (e escondem os
// apps abertos); Arquivos, Ajustes e Emma abrem as janelas e o painel que o PC já tem.
// A barrinha de baixo: toque ou deslize curto pra cima = teclado; deslize longo = apps recentes.
// Com um app aberto o dock some: a camada encolhe pra só a faixa da barrinha (o app desce até
// ela) e as bordas laterais ganham o gesto de voltar (MobileEdge.vue).
import { onMounted, ref, watch } from "vue";
import HandleStrip from "./HandleStrip.vue";
import Icon from "../Icon.vue";
import { filesWindow, layerResize, mobileEdges, on, settingsOpen, toggleEmma } from "../../system";
import { windows } from "../../compositor";
import { appUp, showApp, showHome, watchApps } from "./apps";

type Tab = "inicio" | "apps" | "arquivos" | "ajustes" | "emma";
const active = ref<Tab>("inicio");

const items: { tab: Tab; label: string; icon: string }[] = [
  { tab: "inicio", label: "Início", icon: "home" },
  { tab: "apps", label: "Apps", icon: "grid" },
  { tab: "arquivos", label: "Arquivos", icon: "folder" },
  { tab: "ajustes", label: "Ajustes", icon: "settings" },
  { tab: "emma", label: "Emma", icon: "waves" },
];

const FULL = 92;
const HANDLE = 26;
watch(appUp, (up) => {
  layerResize(-1, up ? HANDLE : FULL).catch(() => {});
  mobileEdges(up).catch(() => {});
  if (!up && active.value !== "inicio" && active.value !== "apps") active.value = "inicio";
});
// com o teclado aberto quem desenha a barrinha (no mesmo lugar) é ele: a nossa some por baixo
const keyboardOver = ref(false);
onMounted(async () => {
  watchApps();
  await on<{ shown: boolean; lumna: boolean }>("lumna://keyboard", (k) => (keyboardOver.value = k.shown && k.lumna));
  await on<string>("lumna://mobile-tab", (t) => (active.value = t as Tab));
});

// Como no celular: um app por vez. Se a janela já existe (mesmo minimizada), volta para ela em vez
// de abrir outra; e o que estava na frente sai de cena.
// (o Arquivos põe a pasta no título: "Downloads · Arquivos")
async function bringUp(title: string, open: () => Promise<void>) {
  const list = await windows().catch(() => []);
  const found = list.find((w) => w.title === title || w.title.endsWith(` · ${title}`));
  // (sem foto pros recentes aqui: o grim trava o compositor um instante a cada troca de app; a
  // miniatura é tirada ao abrir os recentes)
  // já aberto: vem com a troca animada; senão abre numa área vazia (a Início sai animada antes)
  if (found) await showApp(found.address).catch(() => {});
  else {
    await showApp().catch(() => {});
    await open();
  }
}

async function go(tab: Tab) {
  active.value = tab;
  if (tab === "inicio" || tab === "apps") await showHome(tab); else if (tab === "arquivos") await bringUp("Arquivos", () => filesWindow());
  else if (tab === "ajustes") await bringUp("Configurações", () => settingsOpen());
  else await toggleEmma();
}

</script>

<template>
  <div class="root" :class="{ compact: appUp }">
  <nav v-show="!appUp" class="nav">
    <button v-for="item in items" :key="item.tab" :class="{ on: active === item.tab }" @click="go(item.tab)">
      <Icon :name="item.icon" :size="24" />
      <span>{{ item.label }}</span>
    </button>
  </nav>
  <HandleStrip :hidden="keyboardOver" />
  </div>
</template>

<style scoped>
.root {
  height: 100vh;
  touch-action: none;
}
/* com um app aberto: só a faixa da barrinha, fechada (nada do fundo aparece embaixo do app) */
.root.compact {
  background: #111015;
}
.nav {
  margin: 0 12px;
  height: 66px;
  display: flex;
  border: 1px solid var(--lumna-line);
  border-radius: 22px;
  background: rgba(16, 12, 30, 0.92);
}
button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 11px;
}
button.on {
  color: var(--lumna-violet-soft);
}
</style>
