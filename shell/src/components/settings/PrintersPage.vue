<script setup lang="ts">
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  printerAdd,
  printerRemove,
  printers,
  printersDiscover,
  printerSetDefault,
  printerTestPage,
  type FoundPrinter,
  type Printer,
} from "../../system";

const list = ref<Printer[]>([]);
const found = ref<FoundPrinter[] | null>(null);
const searching = ref(false);
const busy = ref<string | null>(null);
const message = ref<{ key: string; text: string; ok?: boolean } | null>(null);
let timer: number | undefined;

const STATE: Record<Printer["state"], string> = { idle: "Pronta", printing: "Imprimindo", disabled: "Pausada" };

async function refresh() {
  list.value = await printers();
}

async function run(key: string, action: () => Promise<unknown>, done?: string) {
  busy.value = key;
  message.value = null;
  try {
    await action();
    if (done) message.value = { key, text: done, ok: true };
  } catch (e) {
    message.value = { key, text: String(e) };
  } finally {
    busy.value = null;
    await refresh();
  }
}

async function search() {
  searching.value = true;
  message.value = null;
  try {
    found.value = await printersDiscover();
  } catch (e) {
    found.value = [];
    message.value = { key: "search", text: String(e) };
  } finally {
    searching.value = false;
  }
}

async function add(p: FoundPrinter) {
  await run(p.uri, () => printerAdd(p.uri, p.info));
  if (!message.value) found.value = found.value?.filter((f) => f.uri !== p.uri) ?? null;
}

onMounted(async () => {
  await refresh();
  timer = window.setInterval(refresh, 4000);
});

onUnmounted(() => window.clearInterval(timer));
</script>

<template>
  <h1>Impressoras</h1>
  <p class="lead">Impressoras na rede (Wi-Fi ou cabo) e no USB, sem precisar instalar driver.</p>

  <h2>Minhas impressoras</h2>
  <div class="card">
    <p v-if="!list.length" class="empty">Nenhuma impressora adicionada.</p>
    <template v-for="p in list" :key="p.name">
      <div class="row">
        <span class="badge"><Icon name="printer" :size="18" /></span>
        <div class="grow">
          <div class="title">{{ p.name.replace(/_/g, " ") }}</div>
          <div class="sub">
            {{ STATE[p.state] }}<template v-if="p.jobs"> · {{ p.jobs }} na fila</template>
          </div>
        </div>
        <span v-if="p.default" class="chip">Padrão</span>
        <span v-if="busy === p.name" class="spinner" />
      </div>
      <div class="actions">
        <button class="btn" :disabled="busy !== null" @click="run(p.name, () => printerTestPage(p.name), 'Página de teste enviada.')">
          Página de teste
        </button>
        <button v-if="!p.default" class="btn" :disabled="busy !== null" @click="run(p.name, () => printerSetDefault(p.name))">
          <Icon name="star" :size="14" /> Tornar padrão
        </button>
        <button class="btn danger" :disabled="busy !== null" @click="run(p.name, () => printerRemove(p.name))">
          <Icon name="trash" :size="14" /> Remover
        </button>
      </div>
      <p v-if="message?.key === p.name" class="error" :class="{ ok: message.ok }">{{ message.text }}</p>
    </template>
  </div>

  <h2>
    Adicionar impressora
    <span v-if="searching" class="spinner" />
  </h2>
  <div class="card">
    <div v-if="found === null" class="row">
      <span class="grow sub">Ligue a impressora e deixe-a na mesma rede (ou no USB).</span>
      <button class="btn primary" :disabled="searching" @click="search()">
        <Icon name="search" :size="14" /> {{ searching ? "Procurando…" : "Procurar" }}
      </button>
    </div>
    <template v-else>
      <p v-if="!found.length" class="empty">Nenhuma impressora nova encontrada.</p>
      <template v-for="f in found" :key="f.uri">
        <div class="row">
          <span class="badge"><Icon :name="f.class === 'direct' ? 'printer' : 'wifi'" :size="18" /></span>
          <div class="grow">
            <div class="title">{{ f.info }}</div>
            <div class="sub">{{ f.class === "direct" ? "USB" : "Rede" }}</div>
          </div>
          <span v-if="busy === f.uri" class="spinner" />
          <button class="btn primary" :disabled="busy !== null" @click="add(f)"><Icon name="plus" :size="14" /> Adicionar</button>
        </div>
        <p v-if="message?.key === f.uri" class="error">{{ message.text }}</p>
      </template>
      <div class="row">
        <span class="grow" />
        <button class="btn" :disabled="searching" @click="search()">Procurar de novo</button>
      </div>
    </template>
    <p v-if="message?.key === 'search'" class="error">{{ message.text }}</p>
  </div>
  <p class="note">
    Funciona com impressoras compatíveis com AirPrint / IPP Everywhere (a maioria das feitas a partir de 2015).
    Modelos antigos que exigem driver do fabricante ainda não aparecem aqui.
  </p>
</template>

<style scoped>
.error.ok {
  color: #86efac;
}
</style>
