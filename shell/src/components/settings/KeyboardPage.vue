<script setup lang="ts">
import { computed, onMounted, ref } from "vue";
import Icon from "../Icon.vue";
import { keyboardSet, keyboardState, localeSet, type KeyboardLayout, type KeyboardState } from "../../system";

// Teclados (o primeiro é o padrão; Alt + Shift troca) e o idioma do sistema.
const state = ref<KeyboardState | null>(null);
const layouts = ref<KeyboardLayout[]>([]);
const saved = ref("");
const busy = ref(false);
const error = ref("");
const done = ref("");

const changed = computed(() => JSON.stringify(layouts.value) !== saved.value);

const layoutName = (id: string) => state.value?.catalog.find((l) => l.id === id)?.name ?? id;
const variantsOf = (id: string) => state.value?.catalog.find((l) => l.id === id)?.variants ?? [];

/** A variante gravada que não está na lista do xkb (ex.: "abnt2", o padrão do br) também aparece. */
function variantChoices(l: KeyboardLayout) {
  const list = variantsOf(l.layout);
  const extra = l.variant && !list.some((v) => v.id === l.variant) ? [{ id: l.variant, name: `${layoutName(l.layout)} (${l.variant})` }] : [];
  return [{ id: "", name: `${layoutName(l.layout)} (padrão)` }, ...extra, ...list];
}

// ---- adicionar ----

const adding = ref(false);
const search = ref("");
const results = computed(() => {
  const q = search.value.trim().toLowerCase();
  const list = state.value?.catalog ?? [];
  const used = new Set(layouts.value.map((l) => l.layout));
  return list.filter((l) => !used.has(l.id) && (!q || l.name.toLowerCase().includes(q) || l.id.includes(q))).slice(0, 40);
});

function add(id: string) {
  layouts.value = [...layouts.value, { layout: id, variant: "" }];
  adding.value = false;
  search.value = "";
  done.value = "";
}

function move(index: number, by: number) {
  const list = [...layouts.value];
  const to = index + by;
  if (to < 0 || to >= list.length) return;
  [list[index], list[to]] = [list[to], list[index]];
  layouts.value = list;
  done.value = "";
}

function remove(index: number) {
  layouts.value = layouts.value.filter((_, i) => i !== index);
  done.value = "";
}

function setVariant(index: number, variant: string) {
  layouts.value = layouts.value.map((l, i) => (i === index ? { ...l, variant } : l));
  done.value = "";
}

async function apply() {
  busy.value = true;
  error.value = "";
  try {
    await keyboardSet(layouts.value);
    saved.value = JSON.stringify(layouts.value);
    done.value = "keyboard";
  } catch (e) {
    error.value = String(e);
  } finally {
    busy.value = false;
  }
}

// ---- idioma ----

const locale = ref("");
const localeBusy = ref(false);

async function applyLocale() {
  localeBusy.value = true;
  error.value = "";
  try {
    await localeSet(locale.value);
    if (state.value) state.value.locale = locale.value;
    done.value = "locale";
  } catch (e) {
    error.value = String(e);
  } finally {
    localeBusy.value = false;
  }
}

onMounted(async () => {
  try {
    state.value = await keyboardState();
    layouts.value = state.value.layouts.map((l) => ({ ...l }));
    saved.value = JSON.stringify(layouts.value);
    locale.value = state.value.locale;
  } catch (e) {
    error.value = String(e);
  }
});
</script>

<template>
  <section class="page">
    <h1>Teclado e idioma</h1>
    <p class="lead">Os teclados que você usa e o idioma do sistema.</p>
    <p v-if="error" class="error">{{ error }}</p>

    <template v-if="state">
      <h2>Teclados</h2>
      <p class="note">
        O primeiro é o padrão. Com mais de um, <b>Alt + Shift</b> troca entre eles.
      </p>
      <div class="card">
        <div v-for="(l, i) in layouts" :key="`${i}:${l.layout}`" class="row">
          <span class="badge"><Icon name="keyboard" :size="18" /></span>
          <div class="grow">
            <div class="title">{{ layoutName(l.layout) }}</div>
            <select class="variant" :value="l.variant" @change="setVariant(i, ($event.target as HTMLSelectElement).value)">
              <option v-for="v in variantChoices(l)" :key="v.id" :value="v.id">{{ v.name }}</option>
            </select>
          </div>
          <button class="tool" title="Subir" :disabled="i === 0" @click="move(i, -1)"><Icon name="chevron-up" :size="16" /></button>
          <button class="tool" title="Descer" :disabled="i === layouts.length - 1" @click="move(i, 1)">
            <Icon name="chevron-down" :size="16" />
          </button>
          <button class="tool danger" title="Tirar" :disabled="layouts.length === 1" @click="remove(i)">
            <Icon name="close" :size="15" />
          </button>
        </div>
      </div>

      <div v-if="adding" class="card picker">
        <input v-model="search" class="search" placeholder="Procurar teclado (ex.: português, english, intl)" autofocus />
        <div class="results">
          <button v-for="r in results" :key="r.id" class="row" @click="add(r.id)">
            <span class="grow title">{{ r.name }}</span>
            <small>{{ r.id }}</small>
          </button>
          <p v-if="!results.length" class="empty">Nenhum teclado com esse nome.</p>
        </div>
      </div>

      <div class="toolbar">
        <button v-if="!adding" class="btn" @click="adding = true"><Icon name="plus" :size="15" /> Adicionar teclado</button>
        <button v-else class="btn" @click="(adding = false), (search = '')">Cancelar</button>
        <span class="spacer" />
        <span v-if="done === 'keyboard' && !changed" class="ok"><Icon name="check" :size="15" /> Teclado trocado</span>
        <button class="btn primary" :disabled="!changed || busy || !layouts.length" @click="apply">
          <Icon :name="busy ? 'refresh' : 'check'" :size="15" /> Aplicar
        </button>
      </div>

      <h2>Teste aqui</h2>
      <input class="search" placeholder="Digite para testar: ç á ã ê õ ` ~ ^ / ? ;" />

      <h2>Idioma do sistema</h2>
      <p class="note">Vale para os menus e os apps a partir do próximo login. Pede a senha de administrador.</p>
      <div class="toolbar">
        <select v-model="locale" class="locale">
          <option v-for="l in state.locales" :key="l.code" :value="l.code">{{ l.name }}</option>
        </select>
        <button class="btn primary" :disabled="locale === state.locale || localeBusy" @click="applyLocale">
          <Icon :name="localeBusy ? 'refresh' : 'check'" :size="15" /> Trocar idioma
        </button>
        <span v-if="done === 'locale'" class="ok"><Icon name="check" :size="15" /> Vale no próximo login</span>
      </div>
    </template>
  </section>
</template>

<style scoped>
.variant,
.locale,
.search {
  height: 34px;
  padding: 0 10px;
  border: 1px solid var(--lumna-line);
  border-radius: 9px;
  background: rgba(255, 255, 255, 0.05);
  color: inherit;
  font: inherit;
  font-size: 13px;
}
.variant {
  margin-top: 6px;
  max-width: 100%;
}
.variant option,
.locale option {
  background: #1e1f23;
}
.search {
  width: 100%;
  box-sizing: border-box;
}
.locale {
  min-width: 260px;
}

.picker {
  margin-top: 10px;
  padding: 10px;
}
.results {
  max-height: 260px;
  overflow-y: auto;
  margin-top: 8px;
}
.results small {
  color: var(--lumna-muted);
  font-size: 12px;
}

.tool {
  display: grid;
  place-items: center;
  width: 30px;
  height: 30px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: inherit;
  cursor: pointer;
}
.tool:hover:not(:disabled) {
  background: rgba(167, 139, 250, 0.14);
}
.tool.danger:hover:not(:disabled) {
  background: rgba(239, 68, 68, 0.8);
}
.tool:disabled {
  opacity: 0.3;
  cursor: default;
}
.toolbar {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
}
.spacer {
  flex: 1;
}
.ok {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
  color: #86efac;
}
</style>
