<script setup lang="ts">
// Novo contato / editar contato, como no esboço: nome, sobrenome, telefones (com o código do
// país), e-mail e "mais informações" (empresa, endereço e aniversário).
import { computed, ref } from "vue";
import Icon from "../Icon.vue";
import { contactName, type Contact } from "../../system";
import { avatarColor, initial } from "./avatar";

const props = defineProps<{ contact: Contact }>();
const emit = defineEmits<{ cancel: []; save: [contact: Contact] }>();

const COUNTRIES = [
  { code: "+55", flag: "🇧🇷" },
  { code: "+1", flag: "🇺🇸" },
  { code: "+351", flag: "🇵🇹" },
  { code: "+54", flag: "🇦🇷" },
  { code: "+34", flag: "🇪🇸" },
  { code: "+44", flag: "🇬🇧" },
  { code: "+49", flag: "🇩🇪" },
];
const LABELS = ["Celular", "Trabalho", "Casa", "Outro"];

// cada telefone vira país + número local (o número salvo junta os dois)
function split(number: string) {
  const n = number.trim();
  const country = COUNTRIES.find((c) => n.startsWith(c.code));
  if (country) return { code: country.code, local: n.slice(country.code.length).trim() };
  return { code: "+55", local: n };
}

const draft = ref({ ...props.contact });
const phones = ref(props.contact.phones.length ? props.contact.phones.map((p) => ({ label: p.label, ...split(p.number) })) : [{ label: "Celular", code: "+55", local: "" }]);
const more = ref(!!(props.contact.company || props.contact.address || props.contact.birthday));
const isNew = !props.contact.id;
const display = computed(() => contactName({ ...draft.value, phones: [] }) || "?");

// (11) 98765-4321 enquanto digita, pros números do Brasil
function mask(p: { code: string; local: string }) {
  if (p.code !== "+55") return;
  const d = p.local.replace(/\D/g, "").slice(0, 11);
  if (d.length <= 2) p.local = d;
  else if (d.length <= 6) p.local = `(${d.slice(0, 2)}) ${d.slice(2)}`;
  else {
    const cut = d.length === 11 ? 7 : 6;
    p.local = `(${d.slice(0, 2)}) ${d.slice(2, cut)}-${d.slice(cut)}`;
  }
}

function save() {
  emit("save", {
    ...draft.value,
    phones: phones.value
      .filter((p) => p.local.replace(/\D/g, ""))
      .map((p) => ({ label: p.label, number: `${p.code} ${p.local.replace(/\D/g, "")}` })),
  });
}
</script>

<template>
  <section class="editor">
    <header class="bar">
      <button class="icon-btn" aria-label="Voltar" @click="emit('cancel')"><Icon name="arrow-left" :size="24" /></button>
      <h1>{{ isNew ? "Novo contato" : "Editar contato" }}</h1>
      <button class="save" @click="save()">Salvar</button>
    </header>

    <div class="scroll">
      <div class="photo">
        <div class="circle" :style="draft.first || draft.last ? { background: avatarColor(display) } : {}">
          <span v-if="draft.first || draft.last">{{ initial(display) }}</span>
          <Icon v-else name="user" :size="44" />
        </div>
      </div>

      <label class="field">
        <small>Nome</small>
        <input v-model="draft.first" placeholder="Nome do contato" autocomplete="off" />
      </label>
      <label class="field">
        <small>Sobrenome</small>
        <input v-model="draft.last" placeholder="Opcional" autocomplete="off" />
      </label>

      <div class="field">
        <small>Telefone</small>
        <div v-for="(p, i) in phones" :key="i" class="phone-row">
          <div class="number">
            <select v-model="p.code" class="country">
              <option v-for="c in COUNTRIES" :key="c.code" :value="c.code">{{ c.flag }} {{ c.code }}</option>
            </select>
            <input v-model="p.local" type="tel" inputmode="tel" placeholder="(DDD) Número" @input="mask(p)" />
          </div>
          <div class="labels">
            <button v-for="l in LABELS" :key="l" :class="{ on: p.label === l }" @click="p.label = l">{{ l }}</button>
            <button v-if="phones.length > 1" class="remove" aria-label="Remover telefone" @click="phones.splice(i, 1)">
              <Icon name="close" :size="16" />
            </button>
          </div>
        </div>
        <button class="add" @click="phones.push({ label: 'Celular', code: '+55', local: '' })">
          <span class="plus"><Icon name="plus" :size="20" /></span> Adicionar telefone
        </button>
      </div>

      <label class="field">
        <small>E-mail</small>
        <input v-model="draft.email" type="email" placeholder="nome@exemplo.com" autocomplete="off" />
      </label>

      <button class="row" @click="more = !more">
        <Icon name="list" :size="22" />
        <span class="row-text"><strong>Mais informações</strong><small>Empresa, endereço e aniversário</small></span>
        <Icon :name="more ? 'chevron-up' : 'chevron-down'" :size="20" />
      </button>
      <template v-if="more">
        <label class="field">
          <small>Empresa</small>
          <input v-model="draft.company" placeholder="Opcional" />
        </label>
        <label class="field">
          <small>Endereço</small>
          <input v-model="draft.address" placeholder="Opcional" />
        </label>
        <label class="field">
          <small>Aniversário</small>
          <input v-model="draft.birthday" placeholder="dd/mm" />
        </label>
      </template>

      <div class="row static">
        <Icon name="phone" :size="22" />
        <span class="row-text"><strong>Salvar em</strong><small>Neste dispositivo</small></span>
      </div>
    </div>
  </section>
</template>

<style scoped>
.editor {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
.bar {
  display: flex;
  align-items: center;
  gap: 8px;
  margin-bottom: 8px;
}
.bar h1 {
  flex: 1;
  margin: 0;
  font-size: 24px;
  font-weight: 500;
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
.save {
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 18px;
  font-weight: 600;
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  scrollbar-width: none;
  padding-bottom: 20px;
}
.photo {
  display: flex;
  justify-content: center;
  margin: 8px 0 18px;
}
.circle {
  width: 100px;
  height: 100px;
  display: grid;
  place-items: center;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 50%;
  background: rgba(40, 36, 56, 0.9);
  color: var(--lumna-muted);
  font-size: 40px;
  font-weight: 600;
}
.circle span {
  color: var(--lumna-text);
}
.field {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-bottom: 12px;
  padding: 12px 18px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 16px;
  background: rgba(24, 21, 36, 0.9);
}
.field small {
  color: var(--lumna-muted);
  font-size: 13px;
}
input,
select {
  border: 0;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 17px;
  outline: none;
}
input::placeholder {
  color: var(--lumna-muted);
}
.phone-row {
  display: flex;
  flex-direction: column;
  gap: 8px;
  margin-top: 4px;
}
.number {
  display: flex;
  align-items: center;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 12px;
  overflow: hidden;
}
.country {
  padding: 12px 8px 12px 12px;
  border-right: 1px solid rgba(255, 255, 255, 0.12);
  background: #1a1626;
}
.number input {
  flex: 1;
  min-width: 0;
  padding: 12px;
}
.labels {
  display: flex;
  flex-wrap: wrap;
  gap: 6px;
}
.labels button {
  padding: 5px 12px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 999px;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 13px;
}
.labels button.on {
  border-color: var(--lumna-violet-soft);
  color: var(--lumna-violet-soft);
}
.labels .remove {
  margin-left: auto;
  padding: 5px 8px;
}
.add {
  margin-top: 10px;
  display: flex;
  align-items: center;
  gap: 12px;
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 16px;
}
.plus {
  width: 32px;
  height: 32px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  color: #1a1030;
}
.row {
  width: 100%;
  margin-bottom: 12px;
  padding: 16px 18px;
  display: flex;
  align-items: center;
  gap: 16px;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 16px;
  background: rgba(24, 21, 36, 0.9);
  color: inherit;
  font: inherit;
  text-align: left;
}
.row-text {
  flex: 1;
  display: flex;
  flex-direction: column;
}
.row-text strong {
  font-size: 16px;
  font-weight: 500;
}
.row-text small {
  color: var(--lumna-muted);
  font-size: 13px;
}
</style>
