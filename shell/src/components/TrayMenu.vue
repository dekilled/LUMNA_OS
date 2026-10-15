<script setup lang="ts">
// Menu de um ícone da bandeja (o menu do próprio app, pelo dbusmenu), no visual dos menus do LUMNA.
// Submenus abrem dentro do mesmo painel. Clique fora ou Esc fecha.
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "./Icon.vue";
import { layerHide, on, trayMenuClick, type TrayMenuNode } from "../system";

const visible = ref(false);
const key = ref("");
const title = ref("");
const x = ref(0);
const nodes = ref<TrayMenuNode[]>([]);
/** Submenus abertos (pelo id). */
const expanded = ref<Set<number>>(new Set());

const WIDTH = 260;
/** O mesmo clique que abriu (soltar o botão direito por cima do véu) não fecha. */
let openedAt = 0;
// Embaixo do ícone, sem sair da tela. Em CSS: quando o menu abre, a camada pode ainda não ter a
// largura da tela (o `innerWidth` daquele instante deixava o menu no canto esquerdo).
const left = computed(() => `clamp(8px, ${x.value - WIDTH / 2}px, calc(100vw - ${WIDTH + 8}px))`);

function opened(data: { key: string; title: string; x: number; nodes: TrayMenuNode[] }) {
  key.value = data.key;
  title.value = data.title;
  x.value = data.x;
  nodes.value = data.nodes;
  expanded.value = new Set();
  visible.value = true;
  openedAt = Date.now();
}

function close() {
  if (Date.now() - openedAt < 350) return;
  visible.value = false;
  window.setTimeout(() => !visible.value && layerHide(), 140);
}

function choose(node: TrayMenuNode) {
  if (!node.enabled || node.separator) return;
  if (node.children.length) {
    const next = new Set(expanded.value);
    if (next.has(node.id)) next.delete(node.id);
    else next.add(node.id);
    expanded.value = next;
    return;
  }
  visible.value = false;
  trayMenuClick(key.value, node.id).catch((e) => console.error(e));
}

/** A árvore achatada para desenhar, com a profundidade de cada item (submenus abertos inclusos). */
const rows = computed(() => {
  const out: { node: TrayMenuNode; depth: number }[] = [];
  const walk = (list: TrayMenuNode[], depth: number) => {
    for (const node of list) {
      out.push({ node, depth });
      if (node.children.length && expanded.value.has(node.id)) walk(node.children, depth + 1);
    }
  };
  walk(nodes.value, 0);
  return out;
});

function onKey(event: KeyboardEvent) {
  if (visible.value && event.key === "Escape") close();
}

const stops: (() => void)[] = [];

onMounted(async () => {
  window.addEventListener("keydown", onKey);
  stops.push(() => window.removeEventListener("keydown", onKey));
  stops.push(await on("lumna://tray-menu", opened));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <div class="veil" @click.self="close()" @contextmenu.self.prevent="close()">
    <section v-if="visible" class="menu" :style="{ left, width: `${WIDTH}px` }">
      <span class="menu-title">{{ title }}</span>
      <template v-for="{ node, depth } in rows" :key="`${depth}:${node.id}`">
        <span v-if="node.separator" class="line" />
        <button v-else :class="{ off: !node.enabled }" :style="{ paddingLeft: `${10 + depth * 14}px` }" @click="choose(node)">
          <span class="mark">
            <Icon v-if="node.toggle && node.checked" name="check" :size="13" />
          </span>
          <span class="label">{{ node.label }}</span>
          <Icon v-if="node.children.length" :name="expanded.has(node.id) ? 'chevron-up' : 'chevron-down'" :size="13" />
        </button>
      </template>
      <p v-if="!rows.length" class="empty">O app não mandou nenhum item.</p>
    </section>
  </div>
</template>

<style scoped>
.veil {
  position: fixed;
  inset: 0;
}

/* Mesmo visual dos outros menus (grafite, acento violeta). */
.menu {
  position: absolute;
  top: 42px;
  display: flex;
  flex-direction: column;
  max-height: calc(100vh - 60px);
  overflow-y: auto;
  padding: 6px;
  background: #1e1f23;
  border: 1px solid rgba(255, 255, 255, 0.08);
  border-radius: 12px;
  box-shadow: 0 16px 40px rgba(0, 0, 0, 0.5);
  animation: pop 140ms cubic-bezier(0.22, 1, 0.36, 1);
}
@keyframes pop {
  from {
    opacity: 0;
    transform: translateY(-4px) scale(0.98);
  }
}
.menu-title {
  padding: 4px 10px 6px;
  font-size: 12px;
  font-weight: 600;
  color: #9a9aa5;
}
.menu button {
  display: flex;
  align-items: center;
  gap: 8px;
  min-height: 32px;
  padding: 0 10px;
  border: 0;
  border-radius: 8px;
  background: transparent;
  color: #ececf1;
  font: inherit;
  font-size: 13px;
  text-align: left;
}
.menu button:hover:not(.off) {
  background: #7d46d7;
}
.menu button.off {
  color: #6b6b75;
  cursor: default;
}
.mark {
  width: 14px;
  display: grid;
  place-items: center;
  flex: none;
}
.label {
  flex: 1;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.line {
  height: 1px;
  margin: 4px 8px;
  background: rgba(255, 255, 255, 0.08);
}
.empty {
  margin: 4px 10px 8px;
  font-size: 12px;
  color: #9a9aa5;
}
</style>
