<script setup lang="ts">
import { onMounted, ref } from "vue";
import Icon from "../Icon.vue";
import { powerAction, powerMenu, powerSetProfile, powerState, type PowerAction, type PowerState } from "../../system";

const state = ref<PowerState | null>(null);
const error = ref("");
const confirm = ref<PowerAction | null>(null);

const PROFILES: Record<string, { label: string; sub: string; icon: string }> = {
  "power-saver": { label: "Economia", sub: "Menos consumo e calor, bateria dura mais.", icon: "battery" },
  balanced: { label: "Equilibrado", sub: "O padrão: desempenho quando precisa.", icon: "star" },
  performance: { label: "Desempenho", sub: "Tudo no máximo (jogos, renderização).", icon: "zap" },
};

const ACTIONS: { id: PowerAction; label: string; icon: string; ask: string }[] = [
  { id: "logout", label: "Sair da sessão", icon: "back", ask: "Fechar todos os apps e voltar para a tela de login?" },
  { id: "suspend", label: "Suspender", icon: "moon", ask: "" },
  { id: "hibernate", label: "Hibernar", icon: "download", ask: "Salvar tudo no disco e desligar?" },
  { id: "reboot", label: "Reiniciar", icon: "refresh", ask: "Fechar todos os apps e reiniciar?" },
  { id: "poweroff", label: "Desligar", icon: "power", ask: "Fechar todos os apps e desligar?" },
];

const available = (id: PowerAction) => {
  const s = state.value;
  if (!s) return id === "logout";
  return { logout: true, suspend: s.can_suspend, hibernate: s.can_hibernate, reboot: s.can_reboot, poweroff: s.can_power_off }[id];
};

async function setProfile(profile: string) {
  error.value = "";
  try {
    await powerSetProfile(profile);
    if (state.value) state.value.profile = profile;
  } catch (e) {
    error.value = String(e);
  }
}

async function run(action: PowerAction) {
  const def = ACTIONS.find((a) => a.id === action)!;
  if (def.ask && confirm.value !== action) {
    confirm.value = action;
    return;
  }
  confirm.value = null;
  error.value = "";
  try {
    await powerAction(action);
  } catch (e) {
    error.value = String(e);
  }
}

onMounted(async () => {
  try {
    state.value = await powerState();
  } catch (e) {
    error.value = String(e);
  }
});
</script>

<template>
  <section class="page">
    <h1>Energia</h1>
    <p class="lead">Perfil de energia e as ações de desligar. O menu rápido abre com <b>Super + Shift + E</b> ou pelo botão no canto da topbar.</p>
    <p v-if="error" class="error">{{ error }}</p>

    <h2>Perfil de energia</h2>
    <div v-if="state?.profile" class="profiles">
      <button
        v-for="p in state.profiles"
        :key="p"
        class="profile"
        :class="{ chosen: state.profile === p }"
        @click="setProfile(p)"
      >
        <Icon :name="PROFILES[p]?.icon ?? 'zap'" :size="20" />
        <span class="title">{{ PROFILES[p]?.label ?? p }}</span>
        <span class="sub">{{ PROFILES[p]?.sub ?? "" }}</span>
      </button>
    </div>
    <p v-else class="note">O controle de perfil (power-profiles-daemon) não está rodando neste computador.</p>

    <h2>Desligar e sessão</h2>
    <div class="card">
      <template v-for="a in ACTIONS" :key="a.id">
        <div v-if="available(a.id)" class="row">
          <span class="badge"><Icon :name="a.icon" :size="18" /></span>
          <span class="grow title">{{ confirm === a.id ? a.ask : a.label }}</span>
          <template v-if="confirm === a.id">
            <button class="btn" @click="confirm = null">Cancelar</button>
            <button class="btn primary" :class="{ danger: a.id === 'poweroff' }" @click="run(a.id)">{{ a.label }}</button>
          </template>
          <button v-else class="btn" @click="run(a.id)">{{ a.label }}</button>
        </div>
      </template>
    </div>
    <div class="toolbar">
      <button class="btn" @click="powerMenu()"><Icon name="power" :size="15" /> Abrir o menu de energia</button>
    </div>
  </section>
</template>

<style scoped>
.profiles {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(190px, 1fr));
  gap: 12px;
}
.profile {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 14px;
  border: 2px solid transparent;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
.profile:hover {
  background: rgba(167, 139, 250, 0.08);
}
.profile.chosen {
  border-color: #8b5cf6;
  box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.2);
}
.profile .title {
  font-size: 14px;
  font-weight: 600;
}
.profile .sub {
  font-size: 12px;
  color: var(--lumna-muted);
}
.btn.danger {
  background: rgba(239, 68, 68, 0.9);
}
.toolbar {
  display: flex;
  margin-top: 12px;
}
</style>
