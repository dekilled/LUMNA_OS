<script setup lang="ts">
// Telefone: teclado de discagem, chamadas recentes e contatos (com o editor de contato).
// As ligações em si aparecem na camada `mobile-call` (tocando, chamando, em conversa).
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import ContactEditor from "./ContactEditor.vue";
import {
  contactFor,
  contactName,
  contactsDelete,
  contactsList,
  contactsSave,
  formatNumber,
  haptic,
  mobileDataInfo,
  on,
  phoneCall,
  phoneLog,
  phoneLogClear,
  settingsOpen,
  type CallLogEntry,
  type Contact,
  type PhoneTab,
} from "../../system";
import { avatarColor, initial } from "./avatar";

const TABS: { id: PhoneTab; label: string; icon: string }[] = [
  { id: "teclado", label: "Teclado", icon: "dialpad" },
  { id: "recentes", label: "Recentes", icon: "clock" },
  { id: "contatos", label: "Contatos", icon: "users" },
];
const pick = (t: string) => (TABS.some((x) => x.id === t) ? (t as PhoneTab) : "teclado");
const tab = ref<PhoneTab>(pick(location.hash.slice(1)));

const contacts = ref<Contact[]>([]);
const log = ref<CallLogEntry[]>([]);
const sim = ref("SIM 1");
const error = ref("");
const menuOpen = ref(false);
// editor (novo ou editar) e detalhe de um contato
const editing = ref<Contact | null>(null);
const viewing = ref<Contact | null>(null);

async function reload() {
  contacts.value = await contactsList().catch(() => []);
  log.value = await phoneLog().catch(() => []);
}

async function call(number: string) {
  error.value = "";
  try {
    await phoneCall(number);
  } catch (e) {
    error.value = String(e);
  }
}

// ---- teclado ----
const KEYS: [string, string][] = [
  ["1", ""], ["2", "ABC"], ["3", "DEF"],
  ["4", "GHI"], ["5", "JKL"], ["6", "MNO"],
  ["7", "PQRS"], ["8", "TUV"], ["9", "WXYZ"],
  ["*", ""], ["0", "+"], ["#", ""],
];
const number = ref("");
const matched = computed(() => (number.value.length >= 8 ? contactFor(contacts.value, number.value) : undefined));
const suggestions = computed(() => {
  const d = number.value.replace(/\D/g, "");
  if (d.length < 3 || matched.value) return [];
  return contacts.value.filter((c) => c.phones.some((p) => p.number.replace(/\D/g, "").includes(d))).slice(0, 3);
});

// segurar o 0 põe "+"; segurar o apagar limpa tudo
let holdTimer = 0;
let held = false;
function keyDown(key: string) {
  held = false;
  clearTimeout(holdTimer);
  if (key !== "0") return;
  holdTimer = window.setTimeout(() => {
    held = true;
    haptic(30).catch(() => {});
    number.value += "+";
  }, 500);
}
function keyUp() {
  clearTimeout(holdTimer);
}
function press(key: string) {
  if (held) {
    held = false;
    return;
  }
  haptic(18).catch(() => {});
  if (number.value.length < 20) number.value += key;
}
function eraseDown() {
  held = false;
  clearTimeout(holdTimer);
  holdTimer = window.setTimeout(() => {
    held = true;
    haptic(30).catch(() => {});
    number.value = "";
  }, 500);
}
function erase() {
  if (held) {
    held = false;
    return;
  }
  number.value = number.value.slice(0, -1);
}
function dial() {
  if (!number.value) {
    // sem número: repete o último discado, como nos celulares
    const last = log.value.find((l) => !l.incoming);
    if (last) number.value = last.number;
    return;
  }
  call(number.value);
}
function addFromDialer() {
  editing.value = blankContact(number.value);
}

// ---- recentes ----
function dayOf(at: number) {
  const d = new Date(at * 1000);
  const today = new Date();
  const yesterday = new Date(Date.now() - 86400000);
  if (d.toDateString() === today.toDateString()) return "Hoje";
  if (d.toDateString() === yesterday.toDateString()) return "Ontem";
  return d.toLocaleDateString("pt-BR", { weekday: "long", day: "numeric", month: "long" });
}
const grouped = computed(() => {
  const groups: { day: string; items: CallLogEntry[] }[] = [];
  for (const entry of log.value) {
    const day = dayOf(entry.at);
    const last = groups[groups.length - 1];
    if (last?.day === day) last.items.push(entry);
    else groups.push({ day, items: [entry] });
  }
  return groups;
});
const hour = (at: number) => new Date(at * 1000).toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
function duration(s: number) {
  if (!s) return "";
  const m = Math.floor(s / 60);
  return m ? `${m} min ${s % 60} s` : `${s} s`;
}
const logName = (e: CallLogEntry) => {
  const c = contactFor(contacts.value, e.number);
  return c ? contactName(c) : formatNumber(e.number) || "Número privado";
};
async function clearLog() {
  menuOpen.value = false;
  await phoneLogClear().catch(() => {});
  log.value = [];
}

// ---- contatos ----
const query = ref("");
const favorites = computed(() => contacts.value.filter((c) => c.favorite));
const filtered = computed(() => {
  const q = query.value.trim().toLowerCase();
  const d = q.replace(/\D/g, "");
  if (!q) return contacts.value;
  return contacts.value.filter(
    (c) => contactName(c).toLowerCase().includes(q) || (d.length >= 2 && c.phones.some((p) => p.number.replace(/\D/g, "").includes(d))),
  );
});
const sections = computed(() => {
  const out: { letter: string; items: Contact[] }[] = [];
  for (const c of filtered.value) {
    const letter = initial(contactName(c));
    const last = out[out.length - 1];
    if (last?.letter === letter) last.items.push(c);
    else out.push({ letter, items: [c] });
  }
  return out;
});
function blankContact(phone = ""): Contact {
  return { id: "", first: "", last: "", phones: [{ label: "Celular", number: phone }], email: "", company: "", address: "", birthday: "", favorite: false };
}
async function saveContact(c: Contact) {
  try {
    const saved = await contactsSave(c);
    editing.value = null;
    await reload();
    if (viewing.value?.id === saved.id || !viewing.value) viewing.value = tab.value === "contatos" ? saved : null;
    if (tab.value === "teclado") number.value = "";
  } catch (e) {
    error.value = String(e);
  }
}
async function toggleFavorite(c: Contact) {
  const saved = await contactsSave({ ...c, favorite: !c.favorite }).catch(() => null);
  await reload();
  if (saved) viewing.value = saved;
}
async function removeContact(c: Contact) {
  await contactsDelete(c.id).catch(() => {});
  viewing.value = null;
  await reload();
}

const title = computed(() => (tab.value === "teclado" ? "Telefone" : TABS.find((t) => t.id === tab.value)!.label));

const stops: (() => void)[] = [];
onMounted(async () => {
  await reload();
  mobileDataInfo()
    .then((d) => (sim.value = d.operator ? `SIM 1 · ${d.operator}` : "SIM 1"))
    .catch(() => {});
  stops.push(await on<string>("lumna://phone-tab", (t) => (tab.value = pick(t))));
  // uma ligação acabou: o histórico mudou
  stops.push(await on<unknown[]>("lumna://calls", () => window.setTimeout(reload, 600)));
});
onUnmounted(() => stops.forEach((s) => s()));
</script>

<template>
  <div class="phone">
    <!-- editor de contato por cima de tudo -->
    <ContactEditor v-if="editing" :contact="editing" @cancel="editing = null" @save="saveContact" />

    <!-- detalhe do contato -->
    <section v-else-if="viewing" class="detail">
      <header class="bar">
        <button class="icon-btn" aria-label="Voltar" @click="viewing = null"><Icon name="arrow-left" :size="24" /></button>
        <span />
        <button class="icon-btn" :class="{ on: viewing.favorite }" aria-label="Favorito" @click="toggleFavorite(viewing)">
          <Icon name="heart" :size="22" />
        </button>
        <button class="icon-btn" aria-label="Editar" @click="editing = { ...viewing, phones: viewing.phones.map((p) => ({ ...p })) }">
          <Icon name="edit" :size="22" />
        </button>
      </header>
      <div class="big-avatar" :style="{ background: avatarColor(contactName(viewing)) }">{{ initial(contactName(viewing)) }}</div>
      <h2>{{ contactName(viewing) }}</h2>
      <p v-if="viewing.company" class="muted center">{{ viewing.company }}</p>
      <div class="card">
        <div v-for="(p, i) in viewing.phones" :key="i" class="row">
          <div class="row-text">
            <strong>{{ formatNumber(p.number) }}</strong>
            <small>{{ p.label }}</small>
          </div>
          <button class="round call" aria-label="Ligar" @click="call(p.number)"><Icon name="call" :size="22" /></button>
        </div>
        <div v-if="viewing.email" class="row">
          <div class="row-text"><strong>{{ viewing.email }}</strong><small>E-mail</small></div>
          <Icon name="mail" :size="20" />
        </div>
        <div v-if="viewing.address" class="row">
          <div class="row-text"><strong>{{ viewing.address }}</strong><small>Endereço</small></div>
        </div>
        <div v-if="viewing.birthday" class="row">
          <div class="row-text"><strong>{{ viewing.birthday }}</strong><small>Aniversário</small></div>
        </div>
      </div>
      <button class="danger-link" @click="removeContact(viewing)"><Icon name="trash" :size="18" /> Excluir contato</button>
      <p v-if="error" class="error" @click="error = ''">{{ error }}</p>
    </section>

    <template v-else>
      <header class="brand-row">
        <div class="brand"><span class="gem" /> LUMNA</div>
        <button class="icon-btn" aria-label="Mais" @click="menuOpen = !menuOpen"><Icon name="dots" :size="24" /></button>
      </header>
      <div v-if="menuOpen" class="menu" @click.self="menuOpen = false">
        <div class="menu-box">
          <button @click="clearLog()"><Icon name="trash" :size="18" /> Limpar histórico de chamadas</button>
          <button @click="menuOpen = false; settingsOpen('dados-moveis')"><Icon name="sim" :size="18" /> Chip e dados móveis</button>
        </div>
      </div>
      <div class="title-row">
        <h1>{{ title }}</h1>
        <button v-if="tab === 'contatos'" class="round plus" aria-label="Novo contato" @click="editing = blankContact()">
          <Icon name="plus" :size="24" />
        </button>
        <button v-if="tab === 'recentes' && log.length" class="link" @click="clearLog()">Limpar</button>
      </div>

      <!-- teclado -->
      <section v-if="tab === 'teclado'" class="dialer">
        <div class="number">
          <span :class="{ placeholder: !number }">{{ number ? formatNumber(number) : "Digite um número" }}</span>
          <button class="icon-btn" aria-label="Adicionar contato" @click="addFromDialer()"><Icon name="user-plus" :size="24" /></button>
        </div>
        <div class="match">
          <template v-if="matched">{{ contactName(matched) }}</template>
          <button v-for="c in suggestions" :key="c.id" class="chip" @click="number = c.phones[0].number">{{ contactName(c) }}</button>
        </div>
        <div class="keys">
          <button
            v-for="[k, letters] in KEYS"
            :key="k"
            class="key"
            @pointerdown="keyDown(k)"
            @pointerup="keyUp"
            @pointercancel="keyUp"
            @click="press(k)"
            @contextmenu.prevent
          >
            <span class="digit">{{ k }}</span>
            <small>{{ letters }}</small>
          </button>
        </div>
        <div class="sim"><Icon name="sim" :size="18" /> {{ sim }}</div>
        <div class="actions">
          <button class="dial" @click="dial()"><Icon name="call" :size="26" /> Ligar</button>
          <button
            class="erase"
            aria-label="Apagar"
            @pointerdown="eraseDown"
            @pointerup="keyUp"
            @pointercancel="keyUp"
            @click="erase()"
            @contextmenu.prevent
          >
            <Icon name="backspace" :size="26" />
          </button>
        </div>
      </section>

      <!-- recentes -->
      <section v-else-if="tab === 'recentes'" class="list">
        <div v-if="!log.length" class="empty"><Icon name="clock" :size="40" /><span>Nenhuma chamada ainda.</span></div>
        <template v-for="g in grouped" :key="g.day">
          <div class="letter">{{ g.day }}</div>
          <div v-for="(e, i) in g.items" :key="`${e.at}-${i}`" class="person">
            <span class="avatar" :style="{ background: avatarColor(logName(e)) }">{{ initial(logName(e)) }}</span>
            <div class="row-text">
              <strong :class="{ missed: e.missed }">{{ logName(e) }}</strong>
              <small :class="{ missed: e.missed }">
                <Icon :name="e.incoming ? 'call-in' : 'call-out'" :size="14" />
                {{ e.missed ? "Perdida" : e.incoming ? "Recebida" : "Feita" }} · {{ hour(e.at) }}
                <template v-if="e.duration"> · {{ duration(e.duration) }}</template>
              </small>
            </div>
            <button class="icon-btn call-icon" aria-label="Ligar" @click="call(e.number)"><Icon name="call" :size="24" /></button>
          </div>
        </template>
      </section>

      <!-- contatos -->
      <section v-else class="list">
        <label class="search">
          <Icon name="search" :size="20" />
          <input v-model="query" placeholder="Buscar nome ou número" />
        </label>
        <div v-if="favorites.length && !query" class="favorites">
          <span class="fav-title">Favoritos</span>
          <button v-for="c in favorites" :key="c.id" class="fav" @click="viewing = c">
            <span class="avatar big" :style="{ background: avatarColor(contactName(c)) }">{{ initial(contactName(c)) }}</span>
            <span>{{ c.first || contactName(c) }}</span>
          </button>
        </div>
        <div v-if="!contacts.length" class="empty">
          <Icon name="users" :size="40" />
          <span>Nenhum contato ainda.</span>
          <button class="link" @click="editing = blankContact()">Criar o primeiro</button>
        </div>
        <template v-for="s in sections" :key="s.letter">
          <div class="letter">{{ s.letter }}</div>
          <div v-for="c in s.items" :key="c.id" class="person" @click="viewing = c">
            <span class="avatar" :style="{ background: avatarColor(contactName(c)) }">{{ initial(contactName(c)) }}</span>
            <div class="row-text">
              <strong>{{ contactName(c) }}</strong>
              <small>{{ c.phones[0]?.label ?? "" }}</small>
            </div>
            <button v-if="c.phones.length" class="icon-btn call-icon" aria-label="Ligar" @click.stop="call(c.phones[0].number)">
              <Icon name="call" :size="24" />
            </button>
          </div>
        </template>
      </section>

      <p v-if="error" class="error" @click="error = ''">{{ error }}</p>

      <nav class="tabs">
        <button v-for="t in TABS" :key="t.id" :class="{ on: tab === t.id }" @click="tab = t.id">
          <Icon :name="t.icon" :size="24" />
          <span>{{ t.label }}</span>
        </button>
      </nav>
    </template>
  </div>
</template>

<style scoped>
.phone {
  height: var(--app-h, 100vh);
  box-sizing: border-box;
  padding: 14px 16px 10px;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background:
    radial-gradient(circle at 105% -12%, transparent 30%, rgba(167, 139, 250, 0.5) 31%, rgba(91, 33, 182, 0.25) 33%, transparent 42%),
    #090713;
  color: var(--lumna-text);
  user-select: none;
}
.brand-row,
.bar {
  display: flex;
  align-items: center;
  justify-content: space-between;
}
.bar span {
  flex: 1;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  letter-spacing: 0.5em;
  font-size: 14px;
}
.gem {
  width: 16px;
  height: 16px;
  transform: rotate(45deg);
  border-radius: 3px;
  background: var(--lumna-violet-soft);
}
.icon-btn {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: inherit;
}
.icon-btn.on {
  color: #f472b6;
}
.title-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin: 18px 4px 16px;
}
h1 {
  margin: 0;
  font-size: 34px;
  font-weight: 600;
}
.round {
  width: 48px;
  height: 48px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 50%;
  background: rgba(30, 26, 46, 0.9);
  color: inherit;
}
.link {
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 15px;
}
.menu {
  position: fixed;
  inset: 0 0 var(--kb-inset, 0px) 0;
  z-index: 5;
}
.menu-box {
  position: absolute;
  top: 56px;
  right: 16px;
  display: flex;
  flex-direction: column;
  padding: 6px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 14px;
  background: #1a1626;
}
.menu-box button {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 12px 14px;
  border: 0;
  border-radius: 10px;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 14px;
  text-align: left;
}
/* teclado */
.dialer {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
.number {
  display: flex;
  align-items: center;
  padding: 8px 8px 8px 18px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 18px;
  background: rgba(24, 21, 36, 0.9);
  font-size: 26px;
  min-height: 48px;
}
.number span {
  flex: 1;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.number .placeholder {
  color: var(--lumna-muted);
  font-size: 20px;
}
.number .icon-btn {
  color: var(--lumna-violet-soft);
}
.match {
  height: 30px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  color: var(--lumna-violet-soft);
  font-size: 14px;
}
.chip {
  padding: 4px 10px;
  border: 1px solid rgba(167, 139, 250, 0.3);
  border-radius: 999px;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 13px;
}
.keys {
  flex: 1;
  min-height: 0;
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  grid-auto-rows: 1fr;
  gap: 10px;
}
.key {
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 20px;
  background: rgba(24, 21, 36, 0.9);
  color: inherit;
  font: inherit;
}
.key:active {
  background: rgba(60, 51, 85, 0.95);
}
.digit {
  font-size: 32px;
  font-weight: 500;
  line-height: 1.1;
}
.key small {
  min-height: 14px;
  color: var(--lumna-muted);
  font-size: 12px;
  letter-spacing: 0.15em;
}
.sim {
  align-self: center;
  margin: 12px 0;
  padding: 8px 18px;
  display: flex;
  align-items: center;
  gap: 10px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 999px;
  color: var(--lumna-text);
  font-size: 14px;
}
.sim :deep(svg) {
  color: var(--lumna-violet-soft);
}
.actions {
  display: flex;
  gap: 12px;
  margin-bottom: 12px;
}
.dial {
  flex: 1;
  height: 60px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  border: 0;
  border-radius: 999px;
  background: #6d4fc4;
  color: #fff;
  font: inherit;
  font-size: 20px;
  font-weight: 600;
}
.erase {
  width: 72px;
  height: 60px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 20px;
  background: rgba(24, 21, 36, 0.9);
  color: var(--lumna-violet-soft);
}
/* listas */
.list {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  scrollbar-width: none;
}
.search {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 0 16px;
  height: 50px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 999px;
  background: rgba(24, 21, 36, 0.9);
  color: var(--lumna-muted);
}
.search input {
  flex: 1;
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 16px;
  outline: none;
}
.favorites {
  margin-top: 12px;
  padding: 12px 14px;
  display: flex;
  align-items: flex-start;
  gap: 14px;
  overflow-x: auto;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 18px;
  background: rgba(20, 17, 30, 0.8);
}
.fav-title {
  color: var(--lumna-muted);
  font-size: 13px;
  margin-right: 4px;
}
.fav {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 6px;
  border: 0;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 13px;
}
.letter {
  margin: 14px 4px 4px;
  padding-bottom: 6px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.08);
  color: var(--lumna-muted);
  font-size: 14px;
  text-transform: capitalize;
}
.person {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 10px 4px;
  border-bottom: 1px solid rgba(255, 255, 255, 0.05);
}
.avatar {
  flex: none;
  width: 46px;
  height: 46px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  font-size: 18px;
  font-weight: 600;
}
.avatar.big {
  width: 54px;
  height: 54px;
  font-size: 20px;
}
.row-text {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.row-text strong {
  font-size: 17px;
  font-weight: 500;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.row-text small {
  display: flex;
  align-items: center;
  gap: 4px;
  color: var(--lumna-muted);
  font-size: 13px;
}
.missed {
  color: #f87171 !important;
}
.call-icon {
  color: var(--lumna-violet-soft);
}
.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 60px 20px;
  color: var(--lumna-muted);
}
.tabs {
  margin-top: 8px;
  height: 70px;
  display: flex;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 22px;
  background: rgba(16, 13, 26, 0.95);
}
.tabs button {
  flex: 1;
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: 4px;
  border: 0;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 12px;
}
.tabs button.on {
  color: var(--lumna-violet-soft);
}
/* detalhe */
.detail {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
}
.big-avatar {
  margin: 10px auto 12px;
  width: 104px;
  height: 104px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  font-size: 42px;
  font-weight: 600;
}
h2 {
  margin: 0;
  text-align: center;
  font-size: 26px;
  font-weight: 500;
}
.muted {
  color: var(--lumna-muted);
}
.center {
  text-align: center;
  margin: 4px 0 0;
}
.card {
  margin-top: 20px;
  padding: 4px 16px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 18px;
  background: rgba(24, 21, 36, 0.9);
}
.row {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 0;
}
.row + .row {
  border-top: 1px solid rgba(255, 255, 255, 0.06);
}
.round.call {
  width: 46px;
  height: 46px;
  background: #6d4fc4;
  border: 0;
}
.danger-link {
  margin: 20px auto 0;
  display: flex;
  align-items: center;
  gap: 8px;
  border: 0;
  background: none;
  color: #fca5a5;
  font: inherit;
  font-size: 15px;
}
.error {
  margin: 8px 4px 0;
  color: #fca5a5;
  font-size: 13px;
  text-align: center;
}
</style>
