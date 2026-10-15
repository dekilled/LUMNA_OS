<script setup lang="ts">
import { onMounted, onUnmounted, ref, watch } from "vue";
import { fitLayerTo, layerHide, notificationAction, notificationClosed, on, type Notification } from "../system";
import Icon from "./Icon.vue";

const items = ref<Notification[]>([]);
const root = ref<HTMLElement>();
const timers = new Map<number, number>();

/** O corpo pode vir com marcação simples (<b>, <a>); aqui mostramos só o texto. */
const plain = (html: string) => new DOMParser().parseFromString(html, "text/html").body.textContent ?? "";

function remove(id: number) {
  window.clearTimeout(timers.get(id));
  timers.delete(id);
  items.value = items.value.filter((n) => n.id !== id);
}

function schedule(n: Notification) {
  window.clearTimeout(timers.get(n.id));
  if (n.timeout > 0) {
    timers.set(
      n.id,
      window.setTimeout(() => {
        remove(n.id);
        notificationClosed(n.id, 1);
      }, n.timeout),
    );
  }
}

function dismiss(n: Notification) {
  remove(n.id);
  notificationClosed(n.id, 2);
}

function act(n: Notification, key: string) {
  remove(n.id);
  notificationAction(n.id, key);
}

function onClick(n: Notification) {
  if (n.actions.some(([key]) => key === "default")) act(n, "default");
  else dismiss(n);
}

// Sem avisos, a camada some (não fica uma janela vazia por cima dos apps).
watch(
  () => items.value.length,
  (count) => {
    if (count === 0) layerHide();
  },
);

const stops: (() => void)[] = [];

onMounted(async () => {
  stops.push(
    await on<Notification>("lumna://notification", (n) => {
      const index = items.value.findIndex((x) => x.id === n.id);
      if (index >= 0) items.value[index] = n;
      else items.value = [n, ...items.value].slice(0, 5);
      schedule(n);
    }),
  );
  stops.push(await on<number>("lumna://notification-close", remove));
  if (root.value) stops.push(fitLayerTo(root.value));
});

onUnmounted(() => {
  stops.forEach((stop) => stop());
  timers.forEach((t) => window.clearTimeout(t));
});
</script>

<template>
  <div ref="root" class="stack">
    <TransitionGroup name="toast">
      <article v-for="n in items" :key="n.id" class="toast" :class="{ urgent: n.urgent }" @click="onClick(n)">
        <header>
          <span class="app">{{ n.app_name || "Aviso" }}</span>
          <button class="close" title="Fechar" @click.stop="dismiss(n)"><Icon name="close" :size="14" /></button>
        </header>
        <strong>{{ plain(n.summary) }}</strong>
        <p v-if="n.body">{{ plain(n.body) }}</p>
        <footer v-if="n.actions.some(([key]) => key !== 'default')">
          <button
            v-for="[key, label] in n.actions.filter(([key]) => key !== 'default')"
            :key="key"
            @click.stop="act(n, key)"
          >
            {{ label }}
          </button>
        </footer>
      </article>
    </TransitionGroup>
  </div>
</template>

<style scoped>
.stack {
  width: 380px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  padding: 4px 4px 16px;
  box-sizing: border-box;
}

.toast {
  padding: 12px 14px;
  background: rgba(18, 13, 36, 0.88);
  border: 1px solid var(--lumna-line);
  border-radius: 16px;
  box-shadow: 0 12px 30px rgba(5, 2, 15, 0.5);
  font-size: 13px;
  cursor: default;
}
.toast.urgent {
  border-color: rgba(248, 113, 113, 0.6);
}

header {
  display: flex;
  align-items: center;
  justify-content: space-between;
  color: var(--lumna-muted);
  font-size: 11px;
  margin-bottom: 4px;
}
.close {
  display: grid;
  place-items: center;
  width: 20px;
  height: 20px;
  border: 0;
  border-radius: 6px;
  background: transparent;
  color: var(--lumna-muted);
}
.close:hover {
  background: rgba(167, 139, 250, 0.14);
}

strong {
  display: block;
  font-weight: 600;
}
p {
  margin: 4px 0 0;
  color: #cfc8ef;
  line-height: 1.4;
  display: -webkit-box;
  -webkit-line-clamp: 4;
  -webkit-box-orient: vertical;
  overflow: hidden;
}

footer {
  display: flex;
  gap: 6px;
  margin-top: 10px;
}
footer button {
  flex: 1;
  height: 30px;
  border: 0;
  border-radius: 9px;
  background: rgba(139, 92, 246, 0.25);
  color: var(--lumna-text);
  font: inherit;
  font-size: 12px;
}
footer button:hover {
  background: var(--lumna-violet);
}

.toast-enter-active,
.toast-leave-active {
  transition: opacity 220ms ease, transform 260ms cubic-bezier(0.22, 1, 0.36, 1);
}
.toast-enter-from,
.toast-leave-to {
  opacity: 0;
  transform: translateX(30px);
}
</style>
