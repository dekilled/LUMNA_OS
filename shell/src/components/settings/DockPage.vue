<script setup lang="ts">
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import {
  apps,
  DOCK_EMMA,
  DOCK_LAUNCHER,
  DOCK_SEPARATOR,
  dockConfig,
  dockConfigReset,
  dockConfigSet,
  dockMonitors,
  on,
  type App,
  type DockBehavior,
  type DockConfig,
  type DockPosition,
} from "../../system";

// Configurações do dock. Cada mudança vale na hora (o dock se redesenha sozinho).
const config = ref<DockConfig | null>(null);
const installed = ref<App[]>([]);
const monitors = ref<string[]>([]);
const error = ref("");

const BEHAVIORS: { id: DockBehavior; title: string; sub: string }[] = [
  { id: "always", title: "Sempre visível", sub: "O dock fica sempre ali e as janelas não passam por baixo dele." },
  {
    id: "smart",
    title: "Esconder com janela maximizada",
    sub: "Some quando uma janela é maximizada. Passe o mouse na borda da tela para ele subir.",
  },
  {
    id: "expand",
    title: "Virar barra de tarefas",
    sub: "Com janela maximizada, ocupa a largura inteira com os ícones no centro, como no Windows.",
  },
];

const POSITIONS: { id: DockPosition; label: string }[] = [
  { id: "bottom", label: "Embaixo" },
  { id: "left", label: "Esquerda" },
  { id: "right", label: "Direita" },
];

const monitorChoices = computed(() => [
  { id: "primary", label: "Só no principal" },
  { id: "all", label: "Em todos" },
  // Escolher um monitor pelo nome só faz sentido com mais de um ligado.
  ...(monitors.value.length > 1 ? monitors.value.map((name) => ({ id: name, label: `Só no ${name}` })) : []),
]);

let saveTimer: number | undefined;

/** Muda na tela na hora e grava logo depois (os controles deslizantes mandam muitas mudanças). */
function update(patch: Partial<DockConfig>) {
  if (!config.value) return;
  config.value = { ...config.value, ...patch };
  error.value = "";
  window.clearTimeout(saveTimer);
  const next = config.value;
  saveTimer = window.setTimeout(() => {
    dockConfigSet(next).catch((e) => (error.value = String(e)));
  }, 120);
}

async function reset() {
  error.value = "";
  try {
    config.value = await dockConfigReset();
  } catch (e) {
    error.value = String(e);
  }
}

// ---- itens do dock ----

function describe(id: string): { name: string; icon?: string | null; glyph?: string } {
  if (id === DOCK_SEPARATOR) return { name: "Separador", glyph: "list" };
  if (id === DOCK_LAUNCHER) return { name: "Todos os apps", glyph: "grid" };
  if (id === DOCK_EMMA) return { name: "Emma", glyph: "sparkle" };
  if (id === "lumna-installer") return { name: "Instalar LUMNA OS (só aparece no pendrive)", glyph: "download" };
  const app = installed.value.find((a) => a.id === id);
  return app ? { name: app.name, icon: app.icon } : { name: `${id} (não instalado)`, glyph: "app" };
}

function move(index: number, by: number) {
  const list = [...(config.value?.pinned ?? [])];
  const to = index + by;
  if (to < 0 || to >= list.length) return;
  [list[index], list[to]] = [list[to], list[index]];
  update({ pinned: list });
}

function removeAt(index: number) {
  const list = [...(config.value?.pinned ?? [])];
  list.splice(index, 1);
  update({ pinned: list });
}

function append(id: string) {
  update({ pinned: [...(config.value?.pinned ?? []), id] });
}

const missing = computed(() =>
  [DOCK_LAUNCHER, DOCK_EMMA].filter((id) => !config.value?.pinned.includes(id)),
);

const stops: (() => void)[] = [];

onMounted(async () => {
  try {
    const [c, list, names] = await Promise.all([dockConfig(), apps(), dockMonitors()]);
    config.value = c;
    installed.value = list;
    monitors.value = names;
  } catch (e) {
    error.value = String(e);
  }
  // "Fixar no dock" pelo próprio dock enquanto a página está aberta.
  stops.push(await on<DockConfig>("lumna://dock-config", (c) => (config.value = c)));
});

onUnmounted(() => stops.forEach((stop) => stop()));
</script>

<template>
  <section class="page">
    <h1>Dock</h1>
    <p class="lead">Do seu jeito: o que fica nele, o tamanho, onde aparece e o que faz quando uma janela é maximizada.</p>
    <p v-if="error" class="error">{{ error }}</p>

    <template v-if="config">
      <h2>Quando uma janela é maximizada</h2>
      <div class="behaviors">
        <button
          v-for="b in BEHAVIORS"
          :key="b.id"
          class="behavior"
          :class="{ chosen: config.behavior === b.id }"
          @click="update({ behavior: b.id })"
        >
          <span class="preview" :class="b.id">
            <span class="win" />
            <span class="bar" />
          </span>
          <span class="title">{{ b.title }}</span>
          <span class="sub">{{ b.sub }}</span>
        </button>
      </div>

      <h2>Posição</h2>
      <div class="segments">
        <button
          v-for="p in POSITIONS"
          :key="p.id"
          :class="{ chosen: config.position === p.id }"
          @click="update({ position: p.id })"
        >
          {{ p.label }}
        </button>
      </div>

      <h2>Monitores</h2>
      <div class="segments">
        <button
          v-for="m in monitorChoices"
          :key="m.id"
          :class="{ chosen: config.monitors === m.id }"
          @click="update({ monitors: m.id })"
        >
          {{ m.label }}
        </button>
      </div>

      <h2>Aparência</h2>
      <div class="card padded sliders">
        <label>
          <span>Tamanho dos ícones</span>
          <input type="range" min="28" max="72" step="2" :value="config.size" @input="update({ size: +($event.target as HTMLInputElement).value })" />
          <output>{{ config.size }} px</output>
        </label>
        <label>
          <span>Espaçamento</span>
          <input type="range" min="0" max="24" :value="config.spacing" @input="update({ spacing: +($event.target as HTMLInputElement).value })" />
          <output>{{ config.spacing }} px</output>
        </label>
        <label>
          <span>Opacidade do fundo</span>
          <input
            type="range"
            min="0"
            max="100"
            :value="Math.round(config.opacity * 100)"
            @input="update({ opacity: +($event.target as HTMLInputElement).value / 100 })"
          />
          <output>{{ Math.round(config.opacity * 100) }}%</output>
        </label>
      </div>

      <h2>Itens do dock</h2>
      <p class="note">Para fixar um app, clique nele no dock com o botão direito e escolha "Fixar no dock".</p>
      <div class="card items">
        <div v-for="(id, i) in config.pinned" :key="`${i}:${id}`" class="row" :class="{ separator: id === DOCK_SEPARATOR }">
          <img v-if="describe(id).icon" class="icon" :src="describe(id).icon!" alt="" />
          <span v-else class="badge"><Icon :name="describe(id).glyph ?? 'app'" :size="18" /></span>
          <span class="grow title">{{ describe(id).name }}</span>
          <button class="tool" title="Subir" :disabled="i === 0" @click="move(i, -1)"><Icon name="chevron-up" :size="16" /></button>
          <button class="tool" title="Descer" :disabled="i === config.pinned.length - 1" @click="move(i, 1)">
            <Icon name="chevron-down" :size="16" />
          </button>
          <button class="tool danger" title="Tirar do dock" @click="removeAt(i)"><Icon name="close" :size="15" /></button>
        </div>
        <p v-if="!config.pinned.length" class="empty">O dock está vazio: só os apps abertos aparecem nele.</p>
      </div>
      <div class="actions">
        <button class="btn" @click="append(DOCK_SEPARATOR)"><Icon name="plus" :size="15" /> Separador</button>
        <button v-for="id in missing" :key="id" class="btn" @click="append(id)">
          <Icon name="plus" :size="15" /> {{ describe(id).name }}
        </button>
        <span class="spacer" />
        <button class="btn" @click="reset"><Icon name="restore" :size="15" /> Restaurar o padrão</button>
      </div>
    </template>
  </section>
</template>

<style scoped>
.behaviors {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(210px, 1fr));
  gap: 12px;
}
.behavior {
  display: flex;
  flex-direction: column;
  gap: 6px;
  padding: 12px;
  border: 2px solid transparent;
  border-radius: 14px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  text-align: left;
  cursor: pointer;
}
.behavior:hover {
  background: rgba(167, 139, 250, 0.08);
}
.behavior.chosen {
  border-color: #8b5cf6;
  box-shadow: 0 0 0 3px rgba(139, 92, 246, 0.2);
}
.behavior .title {
  font-size: 14px;
  font-weight: 600;
}
.behavior .sub {
  font-size: 12px;
  color: var(--lumna-muted);
  line-height: 1.4;
}

/* Miniatura: uma tela com a janela maximizada e o dock. */
.preview {
  position: relative;
  height: 74px;
  border-radius: 8px;
  background: linear-gradient(135deg, #2a1857, #120c24);
  overflow: hidden;
  margin-bottom: 4px;
}
.preview .win {
  position: absolute;
  inset: 6px 6px 22px;
  border-radius: 4px;
  background: rgba(236, 236, 241, 0.16);
  border: 1px solid rgba(236, 236, 241, 0.2);
}
.preview .bar {
  position: absolute;
  left: 50%;
  bottom: 5px;
  width: 54px;
  height: 11px;
  transform: translateX(-50%);
  border-radius: 4px;
  background: #8b5cf6;
}
.preview.smart .win {
  inset: 6px 6px 6px;
}
.preview.smart .bar {
  bottom: 0;
  height: 2px;
  width: 100%;
  border-radius: 0;
  background: linear-gradient(90deg, transparent, #a78bfa, transparent);
}
.preview.expand .win {
  inset: 6px 6px 20px;
}
.preview.expand .bar {
  bottom: 0;
  width: 100%;
  height: 14px;
  border-radius: 0;
  background: linear-gradient(90deg, #6d28d9, #8b5cf6, #6d28d9);
}

.segments {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.segments button {
  height: 34px;
  padding: 0 16px;
  border: 1px solid var(--lumna-line);
  border-radius: 10px;
  background: rgba(255, 255, 255, 0.04);
  color: inherit;
  font: inherit;
  font-size: 13px;
  cursor: pointer;
}
.segments button:hover {
  background: rgba(167, 139, 250, 0.1);
}
.segments button.chosen {
  border-color: #8b5cf6;
  background: rgba(139, 92, 246, 0.25);
}

.sliders {
  display: flex;
  flex-direction: column;
  gap: 12px;
}
.sliders label {
  display: grid;
  grid-template-columns: 170px 1fr 60px;
  align-items: center;
  gap: 14px;
  font-size: 14px;
}
.sliders output {
  font-size: 13px;
  color: var(--lumna-muted);
  text-align: right;
}

.items .icon {
  width: 36px;
  height: 36px;
  border-radius: 10px;
  flex: none;
}
.items .separator .title {
  color: var(--lumna-muted);
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
.tool.danger:hover {
  background: rgba(239, 68, 68, 0.8);
}
.tool:disabled {
  opacity: 0.3;
  cursor: default;
}
.actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 12px;
}
.actions .spacer {
  flex: 1;
}
</style>
