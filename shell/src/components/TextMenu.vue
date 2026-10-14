<script setup lang="ts">
// Menu do botão direito do LUMNA, em todas as janelas. O menu nativo do WebKit é um popup do GTK,
// e popup nativo pendurado numa camada do layer-shell derruba o shell; aqui ele nunca abre.
// Nos campos de texto aparece este: Recortar, Copiar, Colar e Selecionar tudo.
import { onMounted, onUnmounted, ref } from "vue";
import { clipboardRead, clipboardWrite } from "../system";

type Field = HTMLInputElement | HTMLTextAreaElement;

const menu = ref<{ x: number; y: number; field: Field; hasSelection: boolean; editable: boolean } | null>(null);

const TEXT_TYPES = new Set(["text", "search", "url", "email", "password", "tel", "number", ""]);

function textField(target: EventTarget | null): Field | null {
  if (target instanceof HTMLTextAreaElement) return target;
  if (target instanceof HTMLInputElement && TEXT_TYPES.has(target.type)) return target;
  return null;
}

function onContextMenu(event: MouseEvent) {
  // As telas com menu próprio (Arquivos) já trataram o evento.
  if (event.defaultPrevented) return;
  event.preventDefault();
  const field = textField(event.target);
  if (!field || field.disabled) {
    menu.value = null;
    return;
  }
  field.focus();
  const hasSelection = (field.selectionStart ?? 0) !== (field.selectionEnd ?? 0);
  // Fica dentro da janela (a camada pode ser pequena).
  const x = Math.min(event.clientX, window.innerWidth - 180);
  const y = Math.min(event.clientY, window.innerHeight - 170);
  menu.value = { x: Math.max(4, x), y: Math.max(4, y), field, hasSelection, editable: !field.readOnly };
}

function selection(field: Field): string {
  return field.value.slice(field.selectionStart ?? 0, field.selectionEnd ?? 0);
}

/** Troca a seleção por `text` e avisa o Vue (v-model escuta o "input"). */
function replaceSelection(field: Field, text: string) {
  field.setRangeText(text, field.selectionStart ?? field.value.length, field.selectionEnd ?? field.value.length, "end");
  field.dispatchEvent(new Event("input", { bubbles: true }));
}

async function run(action: "cut" | "copy" | "paste" | "all") {
  const current = menu.value;
  menu.value = null;
  if (!current) return;
  const { field } = current;
  field.focus();
  // Campo de senha não deixa a senha sair para a área de transferência.
  const secret = field instanceof HTMLInputElement && field.type === "password";
  if (action === "all") field.select();
  if (action === "copy" && !secret) await clipboardWrite(selection(field)).catch(() => {});
  if (action === "cut" && !secret) {
    await clipboardWrite(selection(field)).catch(() => {});
    replaceSelection(field, "");
  }
  if (action === "paste") {
    const text = await clipboardRead().catch(() => "");
    // Campo de uma linha: a quebra de linha que veio junto (comum ao copiar uma chave) sai.
    replaceSelection(field, field instanceof HTMLInputElement ? text.replace(/[\r\n]+/g, "").trim() : text);
  }
}

function close() {
  menu.value = null;
}

function onKey(event: KeyboardEvent) {
  if (event.key === "Escape") close();
}

onMounted(() => {
  window.addEventListener("contextmenu", onContextMenu);
  window.addEventListener("pointerdown", close);
  window.addEventListener("keydown", onKey);
  window.addEventListener("blur", close);
});

onUnmounted(() => {
  window.removeEventListener("contextmenu", onContextMenu);
  window.removeEventListener("pointerdown", close);
  window.removeEventListener("keydown", onKey);
  window.removeEventListener("blur", close);
});
</script>

<template>
  <div v-if="menu" class="text-menu" :style="{ left: `${menu.x}px`, top: `${menu.y}px` }" @pointerdown.stop>
    <button :disabled="!menu.hasSelection || !menu.editable" @click="run('cut')">Recortar</button>
    <button :disabled="!menu.hasSelection" @click="run('copy')">Copiar</button>
    <button :disabled="!menu.editable" @click="run('paste')">Colar</button>
    <span class="line" />
    <button @click="run('all')">Selecionar tudo</button>
  </div>
</template>

<style scoped>
.text-menu {
  position: fixed;
  z-index: 10000;
  min-width: 170px;
  display: flex;
  flex-direction: column;
  padding: 5px;
  background: #1e1f23;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 10px;
  box-shadow: 0 12px 30px rgba(0, 0, 0, 0.45);
}
button {
  height: 30px;
  padding: 0 12px;
  border: 0;
  border-radius: 6px;
  background: transparent;
  color: #ececf1;
  font: inherit;
  font-size: 13px;
  text-align: left;
}
button:hover:not(:disabled) {
  background: #7d46d7;
}
button:disabled {
  color: #6b6b75;
}
.line {
  height: 1px;
  margin: 4px 6px;
  background: rgba(255, 255, 255, 0.08);
}
</style>
