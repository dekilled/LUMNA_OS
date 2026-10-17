<script setup lang="ts">
// Celular Connect: os PCs pareados (toque = página do PC) e adicionar um novo: achar na rede (ou
// digitar o endereço, inclusive o do Tailscale) e o código de 6 dígitos que o PC mostra.
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../../Icon.vue";
import { connectComputers, connectDiscover, connectPair, on, type Computer, type FoundComputer } from "../../../system";

defineProps<{ arg?: string }>();
const emit = defineEmits<{ open: [page: "connect-pc", arg: string] }>();

const computers = ref<Computer[]>([]);
const adding = ref(false);
const searching = ref(false);
const found = ref<FoundComputer[]>([]);
const host = ref("");
const code = ref("");
const step = ref<"escolher" | "codigo">("escolher");
const busy = ref(false);
const error = ref("");

async function search() {
  searching.value = true;
  found.value = await connectDiscover().catch(() => []);
  searching.value = false;
}
function startAdd() {
  adding.value = true;
  step.value = "escolher";
  host.value = "";
  code.value = "";
  error.value = "";
  search();
}
function choose(h: string) {
  host.value = h;
  step.value = "codigo";
  error.value = "";
}
async function pair() {
  busy.value = true;
  error.value = "";
  try {
    const pc = await connectPair(host.value, code.value);
    adding.value = false;
    computers.value = await connectComputers();
    emit("open", "connect-pc", pc.id);
  } catch (e) {
    error.value = String(e);
  }
  busy.value = false;
}

const stops: (() => void)[] = [];
onMounted(async () => {
  computers.value = await connectComputers().catch(() => []);
  stops.push(await on<Computer[]>("lumna://connect", (list) => (computers.value = list)));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="ms-scroll">
    <div class="hero">
      <span class="big"><Icon name="desktop" :size="38" /></span>
      <p>Seu PC com o LUMNA no bolso: avisos da Emma, senhas, comandos de longe.</p>
    </div>

    <div class="ms-label">Meus computadores</div>
    <div class="ms-card">
      <button v-for="c in computers" :key="c.id" class="ms-row" @click="emit('open', 'connect-pc', c.id)">
        <span class="ico" :class="{ violet: c.online }"><Icon name="desktop" :size="26" /></span>
        <span class="txt">
          <strong>{{ c.name }}</strong>
          <small :class="{ on: c.online }"><span class="dot" :class="{ on: c.online }" /> {{ c.online ? "Conectado" : "Fora do ar" }} · {{ c.host }}</small>
        </span>
        <Icon class="chev" name="forward" :size="20" />
      </button>
      <p v-if="!computers.length" class="ms-note pad">Nenhum computador pareado ainda.</p>
    </div>

    <div class="ms-card add">
      <button class="ms-row" @click="startAdd()">
        <span class="ico violet"><Icon name="plus" :size="24" /></span>
        <span class="txt"><strong>Adicionar computador</strong><small>Na mesma rede ou pelo Tailscale</small></span>
      </button>
    </div>

    <div v-if="adding" class="ms-sheet-veil" @click.self="adding = false">
      <div class="ms-sheet">
        <template v-if="step === 'escolher'">
          <h2>Adicionar computador</h2>
          <p class="ms-note">No PC, abra <b>Configurações → Celular</b> pra ver o código.</p>
          <div class="ms-card list">
            <button v-for="f in found" :key="f.host" class="ms-row" @click="choose(f.host)">
              <span class="ico violet"><Icon name="desktop" :size="24" /></span>
              <span class="txt"><strong>{{ f.name }}</strong><small>{{ f.host }}</small></span>
              <Icon class="chev" name="forward" :size="20" />
            </button>
            <p v-if="searching" class="ms-note pad">Procurando na rede…</p>
            <button v-else-if="!found.length" class="ms-row" @click="search()">
              <span class="txt"><strong>Nada encontrado</strong><small>Toque pra procurar de novo</small></span>
              <Icon class="chev" name="refresh" :size="20" />
            </button>
          </div>
          <label class="ms-field manual">
            <small>Ou digite o endereço (IP da rede ou do Tailscale)</small>
            <input v-model="host" placeholder="192.168.0.10 ou 100.x.y.z" autocomplete="off" />
          </label>
          <div class="ms-actions">
            <button class="ms-btn" @click="adding = false">Cancelar</button>
            <button class="ms-btn primary" :disabled="!host.trim()" @click="choose(host)">Continuar</button>
          </div>
        </template>
        <template v-else>
          <h2>Código do PC</h2>
          <p class="ms-note">Digite os 6 números que aparecem em <b>Configurações → Celular</b> no PC ({{ host }}).</p>
          <label class="ms-field">
            <small>Código</small>
            <input v-model="code" class="code" inputmode="numeric" maxlength="6" placeholder="000000" autocomplete="off" @keydown.enter="pair()" />
          </label>
          <p v-if="error" class="ms-error">{{ error }}</p>
          <div class="ms-actions">
            <button class="ms-btn" @click="step = 'escolher'">Voltar</button>
            <button class="ms-btn primary" :disabled="busy || code.replace(/\D/g, '').length !== 6" @click="pair()">
              {{ busy ? "Pareando…" : "Parear" }}
            </button>
          </div>
        </template>
      </div>
    </div>
  </div>
</template>

<style scoped>
.hero {
  display: flex;
  flex-direction: column;
  align-items: center;
  margin: 4px 0 4px;
  text-align: center;
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
.hero p {
  margin: 12px 20px 0;
  color: var(--lumna-muted);
  font-size: 14px;
}
.pad {
  padding: 16px 18px;
  margin: 0;
}
.add {
  margin-top: 14px;
}
.dot {
  display: inline-block;
  width: 7px;
  height: 7px;
  margin-right: 2px;
  border-radius: 50%;
  background: #94a3b8;
  vertical-align: middle;
}
.dot.on {
  background: #22c55e;
}
.list {
  margin: 12px 0;
  max-height: 34vh;
  overflow-y: auto;
}
.manual {
  margin-top: 4px;
}
.code {
  font-size: 30px !important;
  letter-spacing: 0.35em;
  font-variant-numeric: tabular-nums;
}
</style>
