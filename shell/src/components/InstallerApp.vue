<script setup lang="ts">
import { getCurrentWindow } from "@tauri-apps/api/window";
import { computed, onMounted, onUnmounted, ref, watch } from "vue";
import Icon from "./Icon.vue";
import WindowBar from "./WindowBar.vue";
import { formatSize } from "./files/format";
import { installerDisks, installerReboot, installerRun, installerTimezones, on, type Disk } from "../system";

const STEPS = ["Boas-vindas", "Região", "Disco", "Você", "Resumo", "Instalação"] as const;
const LOCALES = [
  { id: "pt_BR.UTF-8", label: "Português (Brasil)" },
  { id: "en_US.UTF-8", label: "English (US)" },
];
const KEYBOARDS = [
  { id: "br abnt2", label: "Português (ABNT2)" },
  { id: "us", label: "Inglês (EUA)" },
  { id: "us intl", label: "Inglês internacional (com acentos)" },
];

const step = ref(0);
const locale = ref("pt_BR.UTF-8");
const keyboard = ref("br abnt2");
const timezone = ref("America/Sao_Paulo");
const timezones = ref<string[]>([]);
const disks = ref<Disk[] | null>(null);
const disk = ref<string | null>(null);
const agreed = ref(false);
const name = ref("");
const user = ref("");
const userTouched = ref(false);
const password = ref("");
const confirm = ref("");
const hostname = ref("lumna");
const progress = ref({ percent: 0, text: "" });
const state = ref<"idle" | "running" | "done" | "error">("idle");
const error = ref("");

const chosen = computed(() => disks.value?.find((d) => d.path === disk.value) ?? null);

/** "Luan Éric" → "luan": sem acento, minúsculo, a primeira palavra. */
function suggestUser(full: string) {
  return (
    full
      .normalize("NFD")
      .replace(/\p{Diacritic}/gu, "")
      .toLowerCase()
      .split(/\s+/)[0]
      ?.replace(/[^a-z0-9_-]/g, "")
      .replace(/^[^a-z_]+/, "")
      .slice(0, 32) ?? ""
  );
}
watch(name, (n) => {
  if (!userTouched.value) user.value = suggestUser(n);
});

const userError = computed(() => {
  if (!user.value) return "";
  if (!/^[a-z_][a-z0-9_-]{0,31}$/.test(user.value)) return "Use letras minúsculas, números, - e _ (começando por letra).";
  if (["root", "greeter", "lumna-live"].includes(user.value)) return "Esse nome é reservado pelo sistema.";
  return "";
});
const passwordError = computed(() => {
  if (!password.value) return "";
  if (password.value.length < 4) return "Use pelo menos 4 caracteres.";
  if (confirm.value && confirm.value !== password.value) return "As senhas não são iguais.";
  return "";
});
const hostError = computed(() => (/^[A-Za-z0-9][A-Za-z0-9-]{0,62}$/.test(hostname.value) ? "" : "Só letras, números e -."));

const canNext = computed(() => {
  switch (step.value) {
    case 1:
      return !!timezone.value && (!timezones.value.length || timezones.value.includes(timezone.value));
    case 2:
      return !!disk.value && agreed.value;
    case 3:
      return (
        !!name.value.trim() && !!user.value && !userError.value && !!password.value && password.value === confirm.value &&
        !passwordError.value && !hostError.value
      );
    default:
      return true;
  }
});

async function loadDisks() {
  disks.value = null;
  disks.value = await installerDisks();
  if (disk.value && !disks.value.some((d) => d.path === disk.value)) disk.value = null;
}

watch(step, (s) => {
  if (s === 2 && disks.value === null) loadDisks();
});
watch(disk, () => (agreed.value = false));

async function install() {
  step.value = 5;
  state.value = "running";
  error.value = "";
  progress.value = { percent: 0, text: "Começando…" };
  try {
    await installerRun(
      {
        disk: disk.value!,
        user: user.value,
        name: name.value.trim(),
        hostname: hostname.value,
        timezone: timezone.value,
        keyboard: keyboard.value,
        locale: locale.value,
      },
      password.value,
    );
    progress.value = { percent: 100, text: "Pronto" };
    state.value = "done";
  } catch (e) {
    error.value = String(e);
    state.value = "error";
  }
}

const transport = (t: string) => ({ nvme: "NVMe", sata: "SATA", usb: "USB", mmc: "Cartão" })[t] ?? t.toUpperCase();

const stops: (() => void)[] = [];
onMounted(async () => {
  stops.push(await on<{ percent: number; text: string }>("lumna://install-progress", (p) => (progress.value = p)));
  timezones.value = await installerTimezones();
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="installer">
    <WindowBar title="Instalar LUMNA OS" />
    <div class="body">
      <nav class="steps">
        <div class="brand">
          <svg viewBox="0 0 24 24" aria-hidden="true">
            <path d="M12 1.5 19 9l-7 13.5L5 9z" fill="#a78bfa" />
            <path d="M12 1.5 15 9l-3 13.5L9 9z" fill="#fff" opacity=".3" />
          </svg>
          LUMNA OS
        </div>
        <div v-for="(label, i) in STEPS" :key="label" class="step" :class="{ active: i === step, done: i < step }">
          <span class="dot"><Icon v-if="i < step" name="check" :size="13" /><template v-else>{{ i + 1 }}</template></span>
          {{ label }}
        </div>
      </nav>

      <main class="page">
        <!-- 0. Boas-vindas -->
        <section v-if="step === 0" class="welcome">
          <svg class="crystal" viewBox="0 0 24 24" aria-hidden="true">
            <defs>
              <linearGradient id="inst-g" x1="0" y1="0" x2="1" y2="1">
                <stop offset="0" stop-color="#c4b5fd" />
                <stop offset="1" stop-color="#7c3aed" />
              </linearGradient>
            </defs>
            <path d="M12 1.5 19 9l-7 13.5L5 9z" fill="url(#inst-g)" />
            <path d="M12 1.5 15 9l-3 13.5L9 9z" fill="#fff" opacity=".25" />
          </svg>
          <h1>Bem-vindo ao LUMNA OS</h1>
          <p>
            Você está usando o LUMNA direto do pendrive. Para deixá-lo no computador, este assistente prepara um
            disco, copia o sistema e cria a sua conta. Leva alguns minutos e não precisa de internet.
          </p>
          <div class="actions">
            <button class="btn" @click="getCurrentWindow().close()">Continuar testando</button>
            <button class="btn primary" @click="step = 1">Instalar</button>
          </div>
        </section>

        <!-- 1. Região -->
        <section v-else-if="step === 1">
          <h2>Idioma, teclado e fuso</h2>
          <label class="field">
            <span>Idioma do sistema</span>
            <select v-model="locale">
              <option v-for="l in LOCALES" :key="l.id" :value="l.id">{{ l.label }}</option>
            </select>
          </label>
          <label class="field">
            <span>Teclado</span>
            <select v-model="keyboard">
              <option v-for="k in KEYBOARDS" :key="k.id" :value="k.id">{{ k.label }}</option>
            </select>
          </label>
          <label class="field">
            <span>Fuso horário</span>
            <input v-model="timezone" list="timezones" spellcheck="false" placeholder="America/Sao_Paulo" />
            <datalist id="timezones">
              <option v-for="t in timezones" :key="t" :value="t" />
            </datalist>
            <small v-if="timezone && timezones.length && !timezones.includes(timezone)" class="bad">Fuso desconhecido.</small>
          </label>
          <label class="field try">
            <span>Teste o teclado aqui</span>
            <input placeholder="ç á ã ê ? / @" />
          </label>
        </section>

        <!-- 2. Disco -->
        <section v-else-if="step === 2">
          <h2>Onde instalar</h2>
          <p class="lead">Escolha o disco. <strong>Tudo o que houver nele será apagado.</strong></p>
          <p v-if="disks === null" class="muted"><span class="spinner" /> Procurando discos…</p>
          <p v-else-if="!disks.length" class="bad">Nenhum disco encontrado (o pendrive do LUMNA não aparece aqui).</p>
          <div class="disks">
            <button v-for="d in disks ?? []" :key="d.path" class="disk" :class="{ on: disk === d.path }" @click="disk = d.path">
              <Icon :name="d.transport === 'usb' ? 'usb' : 'drive'" :size="26" />
              <div class="grow">
                <strong>{{ d.model }}</strong>
                <span>{{ formatSize(d.size) }} · {{ transport(d.transport) || "Disco" }} · {{ d.path }}</span>
                <span class="contents">Hoje: {{ d.contents }}</span>
              </div>
              <span class="radio" />
            </button>
          </div>
          <button class="link" @click="loadDisks">Procurar de novo</button>
          <label v-if="chosen" class="agree">
            <input v-model="agreed" type="checkbox" />
            Entendi que <strong>{{ chosen.model }}</strong> ({{ formatSize(chosen.size) }}) será apagado por inteiro.
          </label>
        </section>

        <!-- 3. Você -->
        <section v-else-if="step === 3">
          <h2>Sua conta</h2>
          <label class="field">
            <span>Seu nome</span>
            <input v-model="name" placeholder="Luan Eric" />
          </label>
          <label class="field">
            <span>Nome de usuário</span>
            <input v-model="user" spellcheck="false" @input="userTouched = true" />
            <small v-if="userError" class="bad">{{ userError }}</small>
          </label>
          <div class="row2">
            <label class="field">
              <span>Senha</span>
              <input v-model="password" type="password" />
            </label>
            <label class="field">
              <span>Confirme a senha</span>
              <input v-model="confirm" type="password" />
            </label>
          </div>
          <small v-if="passwordError" class="bad">{{ passwordError }}</small>
          <label class="field">
            <span>Nome do computador</span>
            <input v-model="hostname" spellcheck="false" />
            <small v-if="hostError" class="bad">{{ hostError }}</small>
          </label>
          <p class="muted small">A mesma senha serve para entrar e para as permissões de administrador.</p>
        </section>

        <!-- 4. Resumo -->
        <section v-else-if="step === 4">
          <h2>Tudo certo?</h2>
          <dl class="summary">
            <dt>Disco (será apagado)</dt>
            <dd class="danger">{{ chosen?.model }} · {{ formatSize(chosen?.size ?? 0) }} · {{ chosen?.path }}</dd>
            <dt>Conta</dt>
            <dd>{{ name }} ({{ user }})</dd>
            <dt>Computador</dt>
            <dd>{{ hostname }}</dd>
            <dt>Idioma e teclado</dt>
            <dd>{{ LOCALES.find((l) => l.id === locale)?.label }} · {{ KEYBOARDS.find((k) => k.id === keyboard)?.label }}</dd>
            <dt>Fuso</dt>
            <dd>{{ timezone }}</dd>
            <dt>Como fica o disco</dt>
            <dd>Partição de boot de 1 GB + o resto em Btrfs (com espaço para as "voltas seguras" no futuro)</dd>
          </dl>
        </section>

        <!-- 5. Instalação -->
        <section v-else class="installing">
          <template v-if="state === 'done'">
            <div class="big ok"><Icon name="check" :size="40" /></div>
            <h2>O LUMNA está instalado</h2>
            <p class="muted">Tire o pendrive e reinicie. Na próxima vez o computador inicia direto no LUMNA.</p>
            <div class="actions">
              <button class="btn" @click="getCurrentWindow().close()">Depois</button>
              <button class="btn primary" @click="installerReboot">Reiniciar agora</button>
            </div>
          </template>
          <template v-else-if="state === 'error'">
            <div class="big bad-bg"><Icon name="close" :size="40" /></div>
            <h2>A instalação parou</h2>
            <pre class="error">{{ error }}</pre>
            <div class="actions">
              <button class="btn" @click="step = 4; state = 'idle'">Voltar</button>
            </div>
          </template>
          <template v-else>
            <h2>Instalando…</h2>
            <p class="muted">{{ progress.text }}</p>
            <div class="bar"><span :style="{ width: `${progress.percent}%` }" /></div>
            <p class="pct">{{ progress.percent }}%</p>
            <p class="muted small">Não desligue o computador nem tire o pendrive.</p>
          </template>
        </section>

        <footer v-if="step > 0 && step < 5" class="nav">
          <button class="btn" @click="step--">Voltar</button>
          <button v-if="step < 4" class="btn primary" :disabled="!canNext" @click="step++">Continuar</button>
          <button v-else class="btn danger" @click="install">Apagar o disco e instalar</button>
        </footer>
      </main>
    </div>
  </div>
</template>

<style scoped>
.installer {
  height: 100vh;
  display: flex;
  flex-direction: column;
  background: rgba(16, 12, 30, 0.96);
  user-select: text;
}
.body {
  flex: 1;
  min-height: 0;
  display: flex;
}
.steps {
  width: 230px;
  flex: none;
  padding: 26px 18px;
  border-right: 1px solid var(--lumna-line);
  display: flex;
  flex-direction: column;
  gap: 6px;
  user-select: none;
}
.brand {
  display: flex;
  align-items: center;
  gap: 10px;
  margin: 0 0 22px 6px;
  font-weight: 700;
  letter-spacing: 0.08em;
}
.brand svg {
  width: 20px;
  height: 20px;
}
.step {
  display: flex;
  align-items: center;
  gap: 12px;
  height: 40px;
  padding: 0 10px;
  border-radius: 10px;
  color: var(--lumna-muted);
  font-size: 14px;
}
.step.active {
  background: rgba(139, 92, 246, 0.2);
  color: #fff;
}
.dot {
  width: 24px;
  height: 24px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  font-size: 12px;
  border: 1px solid var(--lumna-line);
}
.step.active .dot {
  background: var(--lumna-violet);
  border-color: transparent;
}
.step.done .dot {
  background: rgba(139, 92, 246, 0.35);
  border-color: transparent;
  color: #fff;
}

.page {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  padding: 34px 40px 24px;
  overflow-y: auto;
}
section {
  flex: 1;
}
h1 {
  margin: 18px 0 12px;
  font-size: 30px;
}
h2 {
  margin: 0 0 22px;
  font-size: 22px;
}
p {
  line-height: 1.6;
}
.lead {
  margin: -10px 0 18px;
  color: var(--lumna-muted);
}
.muted {
  color: var(--lumna-muted);
}
.small {
  font-size: 12.5px;
}
.welcome {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
  max-width: 560px;
  margin: 0 auto;
}
.welcome p {
  color: var(--lumna-muted);
}
.crystal {
  width: 84px;
  height: 84px;
  filter: drop-shadow(0 0 22px rgba(139, 92, 246, 0.7));
}

.field {
  display: flex;
  flex-direction: column;
  gap: 7px;
  max-width: 460px;
  margin-bottom: 16px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.field input,
.field select {
  height: 40px;
  padding: 0 12px;
  border-radius: 10px;
  border: 1px solid var(--lumna-line);
  background: rgba(0, 0, 0, 0.3);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
  outline: 0;
}
.field select {
  appearance: none;
  padding-right: 36px;
  background-image: url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='none' stroke='%23a59cc9' stroke-width='2' stroke-linecap='round'%3E%3Cpath d='M6 9l6 6 6-6'/%3E%3C/svg%3E");
  background-repeat: no-repeat;
  background-position: right 12px center;
  background-size: 16px;
}
.field select option {
  background: #1a1433;
  color: var(--lumna-text);
}
.field input:focus,
.field select:focus {
  border-color: var(--lumna-violet);
}
.field.try {
  margin-top: 26px;
}
.row2 {
  display: flex;
  gap: 14px;
  max-width: 460px;
}
.row2 .field {
  flex: 1;
}
.bad {
  color: #fca5a5;
  font-size: 12.5px;
}
.danger {
  color: #fca5a5;
}

.disks {
  display: flex;
  flex-direction: column;
  gap: 10px;
  max-width: 620px;
}
.disk {
  display: flex;
  align-items: center;
  gap: 16px;
  padding: 16px 18px;
  border-radius: 14px;
  border: 1px solid var(--lumna-line);
  background: rgba(255, 255, 255, 0.03);
  color: var(--lumna-text);
  font: inherit;
  text-align: left;
}
.disk:hover {
  background: rgba(139, 92, 246, 0.08);
}
.disk.on {
  border-color: var(--lumna-violet);
  background: rgba(139, 92, 246, 0.14);
}
.disk .grow {
  flex: 1;
  display: flex;
  flex-direction: column;
  gap: 3px;
  font-size: 13px;
  color: var(--lumna-muted);
}
.disk strong {
  font-size: 15px;
  color: var(--lumna-text);
}
.contents {
  font-size: 12px;
}
.radio {
  width: 18px;
  height: 18px;
  border-radius: 50%;
  border: 2px solid var(--lumna-muted);
  box-sizing: border-box;
}
.disk.on .radio {
  border: 6px solid var(--lumna-violet);
}
.link {
  margin-top: 10px;
  border: 0;
  background: transparent;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 13px;
  padding: 0;
}
.agree {
  display: flex;
  align-items: center;
  gap: 10px;
  max-width: 620px;
  margin-top: 22px;
  padding: 14px 16px;
  border-radius: 12px;
  background: rgba(239, 68, 68, 0.1);
  border: 1px solid rgba(248, 113, 113, 0.35);
  font-size: 13.5px;
}
.agree input {
  width: 18px;
  height: 18px;
  accent-color: #ef4444;
}

.summary {
  display: grid;
  grid-template-columns: max-content 1fr;
  gap: 12px 28px;
  margin: 0;
  font-size: 14px;
}
.summary dt {
  color: var(--lumna-muted);
}
.summary dd {
  margin: 0;
}

.installing {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  text-align: center;
}
.bar {
  width: min(520px, 90%);
  height: 10px;
  margin-top: 18px;
  border-radius: 5px;
  background: rgba(255, 255, 255, 0.08);
  overflow: hidden;
}
.bar span {
  display: block;
  height: 100%;
  background: linear-gradient(90deg, #a78bfa, #7c3aed);
  transition: width 400ms ease;
}
.pct {
  font-size: 28px;
  font-weight: 600;
  margin: 14px 0 0;
}
.big {
  width: 76px;
  height: 76px;
  border-radius: 50%;
  display: grid;
  place-items: center;
  margin-bottom: 18px;
}
.big.ok {
  background: linear-gradient(135deg, #a78bfa, #6d28d9);
}
.big.bad-bg {
  background: rgba(239, 68, 68, 0.75);
}
.error {
  max-width: 600px;
  white-space: pre-wrap;
  text-align: left;
  font-size: 12.5px;
  color: #fecaca;
  background: rgba(0, 0, 0, 0.3);
  padding: 12px 14px;
  border-radius: 10px;
}

.actions,
.nav {
  display: flex;
  gap: 10px;
  margin-top: 26px;
}
.nav {
  justify-content: space-between;
  padding-top: 18px;
  border-top: 1px solid var(--lumna-line);
}
.btn {
  height: 40px;
  padding: 0 20px;
  border: 0;
  border-radius: 11px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.btn:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.22);
}
.btn.primary {
  background: var(--lumna-violet);
  color: #fff;
}
.btn.primary:hover:not(:disabled) {
  background: #9b72f8;
}
.btn.danger {
  background: #dc2626;
  color: #fff;
}
.btn.danger:hover {
  background: #ef4444;
}
.btn:disabled {
  opacity: 0.45;
}
.spinner {
  display: inline-block;
  width: 13px;
  height: 13px;
  vertical-align: -2px;
  border-radius: 50%;
  border: 2px solid rgba(255, 255, 255, 0.2);
  border-top-color: var(--lumna-violet-soft);
  animation: spin 800ms linear infinite;
}
@keyframes spin {
  to {
    transform: rotate(360deg);
  }
}
</style>
