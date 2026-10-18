<script setup lang="ts">
// Ajustes → Desenvolvedor: ajustes finos pra quem está construindo o LUMNA. Por enquanto, as
// animações do Hyprland, aplicadas na hora (e guardadas pro próximo início).
import { onMounted, ref } from "vue";
import { animationsGet, animationsReset, animationsSet, type Animations } from "../../../system";

const anim = ref<Animations | null>(null);
const error = ref("");
let timer = 0;

function change(patch: Partial<Animations>) {
  if (!anim.value) return;
  anim.value = { ...anim.value, ...patch };
  clearTimeout(timer);
  // arrastando o controle: aplica quando ele para
  timer = window.setTimeout(async () => {
    error.value = "";
    await animationsSet(anim.value!).catch((e) => (error.value = String(e)));
  }, 250);
}
async function reset() {
  anim.value = await animationsReset().catch((e) => ((error.value = String(e)), anim.value));
}
onMounted(async () => (anim.value = await animationsGet().catch(() => null)));

const SPEEDS: { key: "windows" | "layers" | "fade" | "workspaces"; title: string; sub: string }[] = [
  { key: "windows", title: "Abrir um app novo", sub: "A janela aparecendo pela primeira vez" },
  { key: "layers", title: "Barras e painéis", sub: "Central, recentes, teclado, avisos" },
  { key: "fade", title: "Esmaecer", sub: "Aparecer e sumir" },
  { key: "workspaces", title: "Trocar de app", sub: "Ir pra Início e voltar pra um app" },
];
const STYLES = [
  { id: "popin", label: "Crescer" },
  { id: "slide", label: "Deslizar" },
  { id: "gnomed", label: "Encolher" },
] as const;
const SWITCHES = [
  { id: "slidefade", label: "Suave" },
  { id: "slide", label: "Deslizar" },
  { id: "slidevert", label: "Subir" },
  { id: "fade", label: "Esmaecer" },
] as const;
const CURVES = [
  { id: "lumna", label: "Suave" },
  { id: "rapida", label: "Direta" },
  { id: "mola", label: "Mola" },
] as const;
const ms = (v: number) => `${Math.round(v * 100)} ms`;
</script>

<template>
  <div class="ms-scroll">
    <template v-if="anim">
      <div class="ms-label">Animações</div>
      <div class="ms-card">
        <div class="ms-row">
          <span class="txt"><strong>Animações</strong><small>{{ anim.enabled ? "Ligadas" : "Desligadas: tudo aparece na hora" }}</small></span>
          <button class="ms-switch" :class="{ on: anim.enabled }" aria-label="Animações" @click="change({ enabled: !anim.enabled })" />
        </div>
      </div>

      <div class="ms-card speeds" :class="{ off: !anim.enabled }">
        <div v-for="s in SPEEDS" :key="s.key" class="speed">
          <div class="head"><strong>{{ s.title }}</strong><span>{{ ms(anim[s.key]) }}</span></div>
          <small>{{ s.sub }}</small>
          <input
            class="ms-slider"
            type="range"
            min="0.5"
            max="6"
            step="0.25"
            :value="anim[s.key]"
            :disabled="!anim.enabled"
            @input="change({ [s.key]: +($event.target as HTMLInputElement).value })"
          />
        </div>
      </div>

      <div class="ms-label">Trocar de app e ir pra Início</div>
      <div class="chips">
        <button v-for="o in SWITCHES" :key="o.id" :class="{ on: anim.switch_style === o.id }" @click="change({ switch_style: o.id })">{{ o.label }}</button>
      </div>

      <div class="ms-label">Abrir um app novo</div>
      <div class="chips">
        <button v-for="o in STYLES" :key="o.id" :class="{ on: anim.windows_style === o.id }" @click="change({ windows_style: o.id })">{{ o.label }}</button>
      </div>

      <div class="ms-label">Curva</div>
      <div class="chips">
        <button v-for="o in CURVES" :key="o.id" :class="{ on: anim.curve === o.id }" @click="change({ curve: o.id })">{{ o.label }}</button>
      </div>

      <p v-if="error" class="ms-error">{{ error }}</p>
      <p class="ms-note">Vale na hora. Abra e feche um app pra sentir; fica salvo pro próximo início.</p>
      <div class="ms-actions">
        <button class="ms-btn" @click="reset()">Voltar ao padrão</button>
      </div>
    </template>
  </div>
</template>

<style scoped>
.speeds {
  margin-top: 14px;
  padding: 6px 18px;
}
.speeds.off {
  opacity: 0.45;
}
.speed {
  padding: 12px 0 6px;
}
.speed + .speed {
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.head {
  display: flex;
  justify-content: space-between;
  font-size: 16px;
}
.head strong {
  font-weight: 500;
}
.head span {
  color: var(--lumna-violet-soft);
  font-variant-numeric: tabular-nums;
}
small {
  color: var(--lumna-muted);
  font-size: 13px;
}
.chips {
  display: flex;
  gap: 8px;
}
.chips button {
  flex: 1;
  padding: 12px 0;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 14px;
  background: rgba(20, 17, 30, 0.9);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.chips button.on {
  border-color: transparent;
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
</style>
