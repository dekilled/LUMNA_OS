<script setup lang="ts">
// Teclado e entrada: os mesmos ajustes do LUMNA Keyboard (que também ficam no próprio teclado).
// Ficam no armazenamento local, que o teclado escuta e aplica na hora.
import { ref } from "vue";
import { haptic } from "../../../system";

defineProps<{ arg?: string }>();
const KEY = "lumna.keyboard.settings";
const WORDS = "lumna.keyboard.words";
const s = ref({ repeat: true, repeatDelay: 500, repeatRate: 120, vibrate: true, vibrateMs: 30, suggestions: true });
try {
  s.value = { ...s.value, ...JSON.parse(localStorage.getItem(KEY) ?? "{}") };
} catch {}
const save = () => {
  try {
    localStorage.setItem(KEY, JSON.stringify(s.value));
  } catch {}
};
const SPEEDS = [
  { label: "Lenta", rate: 200 },
  { label: "Média", rate: 120 },
  { label: "Rápida", rate: 60 },
];
const BUZZ = [
  { label: "Leve", ms: 20 },
  { label: "Média", ms: 30 },
  { label: "Forte", ms: 45 },
];
const forgot = ref(false);
function forget() {
  try {
    localStorage.removeItem(WORDS);
  } catch {}
  forgot.value = true;
}
</script>

<template>
  <div class="ms-scroll">
    <div class="ms-label">Toque</div>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Vibrar ao tocar</strong><small>Um toque curto a cada tecla</small></span>
        <button class="ms-switch" :class="{ on: s.vibrate }" aria-label="Vibrar" @click="s.vibrate = !s.vibrate; save()" />
      </div>
      <div class="ms-row column" :class="{ off: !s.vibrate }">
        <span class="txt"><strong>Força da vibração</strong></span>
        <div class="seg">
          <button v-for="b in BUZZ" :key="b.ms" :class="{ on: s.vibrateMs === b.ms }" :disabled="!s.vibrate" @click="s.vibrateMs = b.ms; save(); haptic(b.ms)">
            {{ b.label }}
          </button>
        </div>
      </div>
    </div>

    <div class="ms-label">Apagar</div>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Segurar repete</strong><small>Segurando o apagar, ele continua apagando</small></span>
        <button class="ms-switch" :class="{ on: s.repeat }" aria-label="Repetir" @click="s.repeat = !s.repeat; save()" />
      </div>
      <div class="ms-row column" :class="{ off: !s.repeat }">
        <span class="txt"><strong>Espera antes de repetir</strong><small>{{ (s.repeatDelay / 1000).toFixed(1).replace(".", ",") }} s</small></span>
        <input v-model.number="s.repeatDelay" class="ms-slider" type="range" min="250" max="1200" step="50" :disabled="!s.repeat" @change="save()" />
      </div>
      <div class="ms-row column" :class="{ off: !s.repeat }">
        <span class="txt"><strong>Velocidade</strong></span>
        <div class="seg">
          <button v-for="sp in SPEEDS" :key="sp.rate" :class="{ on: s.repeatRate === sp.rate }" :disabled="!s.repeat" @click="s.repeatRate = sp.rate; save()">
            {{ sp.label }}
          </button>
        </div>
      </div>
    </div>

    <div class="ms-label">Sugestões</div>
    <div class="ms-card">
      <div class="ms-row">
        <span class="txt"><strong>Sugestões de palavras</strong><small>Aprende as palavras que você digita</small></span>
        <button class="ms-switch" :class="{ on: s.suggestions }" aria-label="Sugestões" @click="s.suggestions = !s.suggestions; save()" />
      </div>
      <button class="ms-row" :disabled="forgot" @click="forget()">
        <span class="txt"><strong class="danger">{{ forgot ? "Palavras esquecidas" : "Esquecer as palavras aprendidas" }}</strong></span>
      </button>
    </div>
  </div>
</template>

<style scoped>
.column {
  flex-direction: column;
  align-items: stretch;
  gap: 10px;
}
.off {
  opacity: 0.45;
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
  padding: 9px 0;
  border: 0;
  border-radius: 10px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.seg button.on {
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
.danger {
  color: #fca5a5;
}
</style>
