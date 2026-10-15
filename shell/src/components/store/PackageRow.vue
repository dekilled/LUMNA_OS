<script setup lang="ts">
import { computed, ref } from "vue";
import Icon from "../Icon.vue";
import { SOURCE_LABEL } from "./curated";
import { jobs, keyOf } from "./jobs";
import type { StoreSource } from "../../system";

const props = defineProps<{
  source: StoreSource;
  name: string;
  title: string;
  summary: string;
  version?: string;
  icon?: string | null;
  installed: boolean;
  essential?: boolean;
  /** Cartão (Descobrir) ou linha (busca, instalados). */
  card?: boolean;
}>();
const emit = defineEmits<{ install: []; remove: [] }>();

const job = computed(() => jobs[keyOf(props)]);
const broken = ref(false);
const letter = computed(() => props.title.trim().charAt(0).toUpperCase() || "?");
</script>

<template>
  <article class="pkg" :class="{ card }">
    <div class="art">
      <img v-if="icon && !broken" :src="icon" alt="" loading="lazy" @error="broken = true" />
      <span v-else class="letter" :class="source">{{ letter }}</span>
    </div>
    <div class="text">
      <div class="top">
        <span class="title">{{ title }}</span>
        <span class="badge" :class="source">{{ SOURCE_LABEL[source] }}</span>
      </div>
      <p class="summary">{{ summary || name }}</p>
      <p v-if="job?.running" class="progress"><span class="spinner" /> {{ job.line || "Começando…" }}</p>
      <p v-else-if="job?.error" class="error">{{ job.error }}</p>
      <p v-else-if="!card && version" class="version">{{ version }}</p>
    </div>
    <div class="act">
      <button v-if="job?.running" class="btn" disabled>{{ installed ? "Removendo…" : "Instalando…" }}</button>
      <span v-else-if="installed && essential" class="essential" title="Peça do sistema">Essencial</span>
      <button v-else-if="installed" class="btn ghost" @click="emit('remove')"><Icon name="trash" :size="14" /> Remover</button>
      <button v-else class="btn primary" @click="emit('install')"><Icon name="plus" :size="14" /> Instalar</button>
    </div>
  </article>
</template>

<style scoped>
.pkg {
  display: flex;
  align-items: center;
  gap: 14px;
  padding: 12px 14px;
  border-radius: 16px;
}
.pkg:not(.card) + .pkg:not(.card) {
  border-top: 1px solid rgba(167, 139, 250, 0.08);
}
.pkg.card {
  flex-direction: column;
  align-items: stretch;
  gap: 12px;
  padding: 16px;
  background: rgba(255, 255, 255, 0.04);
  border: 1px solid var(--lumna-line);
  transition: transform 200ms cubic-bezier(0.22, 1, 0.36, 1), background-color 200ms ease;
}
.pkg.card:hover {
  transform: translateY(-2px);
  background: rgba(139, 92, 246, 0.08);
}

.art {
  width: 48px;
  height: 48px;
  flex: none;
}
.card .art {
  width: 64px;
  height: 64px;
}
.art img {
  width: 100%;
  height: 100%;
  object-fit: contain;
}
.letter {
  width: 100%;
  height: 100%;
  display: grid;
  place-items: center;
  border-radius: 14px;
  font-size: 22px;
  font-weight: 700;
  color: #fff;
  background: linear-gradient(135deg, #a78bfa, #6d28d9);
}
.letter.arch {
  background: linear-gradient(135deg, #60a5fa, #1d4ed8);
}
.letter.aur {
  background: linear-gradient(135deg, #fbbf24, #b45309);
}

.text {
  flex: 1;
  min-width: 0;
}
.top {
  display: flex;
  align-items: center;
  gap: 8px;
}
.title {
  font-size: 15px;
  font-weight: 600;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.badge {
  font-size: 10px;
  font-weight: 600;
  letter-spacing: 0.04em;
  padding: 2px 7px;
  border-radius: 999px;
  flex: none;
}
.badge.arch {
  background: rgba(96, 165, 250, 0.16);
  color: #93c5fd;
}
.badge.flathub {
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
.badge.aur {
  background: rgba(251, 191, 36, 0.16);
  color: #fcd34d;
}
.summary {
  margin: 4px 0 0;
  font-size: 13px;
  color: var(--lumna-muted);
  overflow: hidden;
  display: -webkit-box;
  -webkit-line-clamp: 2;
  -webkit-box-orient: vertical;
}
.card .summary {
  min-height: 36px;
}
.version,
.progress,
.error {
  margin: 6px 0 0;
  font-size: 12px;
  color: var(--lumna-muted);
}
.progress {
  display: flex;
  align-items: center;
  gap: 8px;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.error {
  color: #fca5a5;
  white-space: pre-line;
}

.btn {
  height: 34px;
  padding: 0 14px;
  border: 0;
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.08);
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 6px;
  white-space: nowrap;
}
.card .btn {
  width: 100%;
}
.btn.primary {
  background: var(--lumna-violet);
  color: #fff;
}
.btn.primary:hover {
  background: #9b72f8;
}
.btn.ghost:hover {
  background: rgba(239, 68, 68, 0.7);
}
.btn:disabled {
  opacity: 0.6;
}
.essential {
  font-size: 12px;
  color: var(--lumna-muted);
  padding: 0 10px;
}
.spinner {
  width: 12px;
  height: 12px;
  flex: none;
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
