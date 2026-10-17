<script setup lang="ts">
// Agendamentos do PC (Connect): a lista e o "Agendar". Serve no celular (Ajustes → o PC) e no
// PC (Configurações → Celular); quem usa passa a lista e cuida de criar/apagar.
import { computed, ref } from "vue";
import Icon from "../Icon.vue";
import type { ScheduleAction, ScheduleTask } from "../../system";

const props = defineProps<{ tasks: ScheduleTask[]; disabled?: boolean }>();
const emit = defineEmits<{ add: [task: Omit<ScheduleTask, "id" | "by">]; remove: [id: string] }>();

const ACTIONS: { id: ScheduleAction; label: string; icon: string }[] = [
  { id: "remind", label: "Lembrete", icon: "bell" },
  { id: "emma", label: "Emma", icon: "sparkle" },
  { id: "poweroff", label: "Desligar", icon: "power" },
  { id: "reboot", label: "Reiniciar", icon: "refresh" },
  { id: "suspend", label: "Suspender", icon: "moon" },
  { id: "lock", label: "Bloquear", icon: "lock" },
];
const LABEL = Object.fromEntries(ACTIONS.map((a) => [a.id, a.label])) as Record<ScheduleAction, string>;
const ICON = Object.fromEntries(ACTIONS.map((a) => [a.id, a.icon])) as Record<ScheduleAction, string>;

const open = ref(false);
const action = ref<ScheduleAction>("remind");
const hour = ref(0);
const minute = ref(0);
const text = ref("");
const daily = ref(false);

function startAdd() {
  const d = new Date(Date.now() + 30 * 60_000);
  hour.value = d.getHours();
  minute.value = Math.ceil(d.getMinutes() / 5) * 5;
  if (minute.value === 60) {
    minute.value = 0;
    hour.value = (hour.value + 1) % 24;
  }
  action.value = "remind";
  text.value = "";
  daily.value = false;
  open.value = true;
}
function bump(which: "h" | "m", delta: number) {
  if (which === "h") hour.value = (hour.value + delta + 24) % 24;
  else minute.value = (minute.value + delta + 60) % 60;
}
function inMinutes(n: number) {
  const d = new Date(Date.now() + n * 60_000);
  hour.value = d.getHours();
  minute.value = d.getMinutes();
}
/** A próxima vez que o relógio marca hh:mm (hoje ou amanhã). */
const when = computed(() => {
  const d = new Date();
  d.setHours(hour.value, minute.value, 0, 0);
  if (d.getTime() <= Date.now()) d.setDate(d.getDate() + 1);
  return d;
});
const needsText = computed(() => action.value === "remind" || action.value === "emma");
function save() {
  emit("add", { at: Math.floor(when.value.getTime() / 1000), action: action.value, text: text.value.trim(), daily: daily.value });
  open.value = false;
}

const two = (n: number) => String(n).padStart(2, "0");
function day(at: number) {
  const d = new Date(at * 1000);
  const today = new Date();
  const tomorrow = new Date(Date.now() + 86_400_000);
  const time = `${two(d.getHours())}:${two(d.getMinutes())}`;
  if (d.toDateString() === today.toDateString()) return `Hoje, ${time}`;
  if (d.toDateString() === tomorrow.toDateString()) return `Amanhã, ${time}`;
  return `${d.toLocaleDateString("pt-BR", { weekday: "short", day: "numeric", month: "short" })}, ${time}`;
}
function whenLabel(d: Date) {
  return d.toDateString() === new Date().toDateString() ? "hoje" : "amanhã";
}
</script>

<template>
  <div class="sched">
    <div v-for="t in props.tasks" :key="t.id" class="task">
      <span class="ico"><Icon :name="ICON[t.action] ?? 'clock'" :size="20" /></span>
      <span class="txt">
        <strong>{{ t.text || LABEL[t.action] }}</strong>
        <small>{{ t.daily ? `Todo dia, ${day(t.at).split(", ")[1]}` : day(t.at) }} · {{ LABEL[t.action] }}<template v-if="t.by"> · por {{ t.by }}</template></small>
      </span>
      <button class="x" aria-label="Cancelar agendamento" :disabled="disabled" @click="emit('remove', t.id)"><Icon name="close" :size="18" /></button>
    </div>
    <p v-if="!props.tasks.length" class="empty">Nada agendado.</p>
    <button class="add" :disabled="disabled" @click="startAdd()"><Icon name="plus" :size="18" /> Agendar</button>

    <div v-if="open" class="veil" @click.self="open = false">
      <div class="sheet">
        <h2>Agendar no PC</h2>
        <div class="chips">
          <button v-for="a in ACTIONS" :key="a.id" :class="{ on: action === a.id }" @click="action = a.id">
            <Icon :name="a.icon" :size="18" /> {{ a.label }}
          </button>
        </div>

        <div class="clock">
          <div class="unit">
            <button @click="bump('h', 1)">+</button><span>{{ two(hour) }}</span><button @click="bump('h', -1)">−</button>
          </div>
          <span class="colon">:</span>
          <div class="unit">
            <button @click="bump('m', 5)">+</button><span>{{ two(minute) }}</span><button @click="bump('m', -5)">−</button>
          </div>
        </div>
        <div class="quick">
          <button @click="inMinutes(15)">em 15 min</button>
          <button @click="inMinutes(60)">em 1 h</button>
          <button @click="inMinutes(120)">em 2 h</button>
        </div>
        <p class="hint">{{ daily ? "Todo dia" : `Vai ser ${whenLabel(when)}` }} às {{ two(hour) }}:{{ two(minute) }}.</p>

        <input
          v-if="needsText"
          v-model="text"
          class="text"
          :placeholder="action === 'emma' ? 'O que a Emma deve fazer? (ex.: rodar o build e me contar)' : 'Lembrar de…'"
        />
        <label class="repeat">
          <span>Repetir todo dia</span>
          <button class="sw" :class="{ on: daily }" aria-label="Repetir todo dia" @click="daily = !daily" />
        </label>
        <div class="row">
          <button class="btn" @click="open = false">Cancelar</button>
          <button class="btn go" :disabled="needsText && !text.trim()" @click="save()">Agendar</button>
        </div>
      </div>
    </div>
  </div>
</template>

<style scoped>
.task {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 12px 14px;
  border-radius: 16px;
  background: rgba(20, 17, 30, 0.9);
  border: 1px solid rgba(255, 255, 255, 0.07);
}
.task + .task {
  margin-top: 8px;
}
.ico {
  width: 38px;
  height: 38px;
  flex: none;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: rgba(139, 92, 246, 0.2);
  color: var(--lumna-violet-soft);
}
.txt {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
  gap: 2px;
}
.txt strong {
  font-weight: 500;
  font-size: 15px;
  overflow: hidden;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.txt small {
  color: var(--lumna-muted);
  font-size: 12.5px;
}
.x {
  width: 36px;
  height: 36px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-muted);
}
.empty {
  margin: 4px 2px 10px;
  color: var(--lumna-muted);
  font-size: 14px;
}
.add {
  margin-top: 10px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 10px 16px;
  border: 1px solid rgba(139, 92, 246, 0.45);
  border-radius: 999px;
  background: rgba(139, 92, 246, 0.14);
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 14px;
}
button:disabled {
  opacity: 0.45;
}
.veil {
  position: fixed;
  inset: 0;
  z-index: 50;
  display: flex;
  align-items: flex-end;
  justify-content: center;
  background: rgba(5, 3, 14, 0.6);
}
.sheet {
  width: min(520px, 100%);
  box-sizing: border-box;
  padding: 20px 18px 24px;
  border-radius: 26px 26px 0 0;
  background: #17132a;
  border: 1px solid rgba(255, 255, 255, 0.08);
}
@media (min-width: 600px) {
  .veil {
    align-items: center;
  }
  .sheet {
    border-radius: 22px;
  }
}
h2 {
  margin: 0 0 14px;
  font-size: 19px;
  font-weight: 500;
}
.chips {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
}
.chips button {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  padding: 9px 13px;
  border: 1px solid rgba(255, 255, 255, 0.1);
  border-radius: 999px;
  background: none;
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
}
.chips button.on {
  border-color: transparent;
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
.clock {
  margin: 18px 0 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 10px;
}
.unit {
  display: flex;
  flex-direction: column;
  align-items: center;
}
.unit span {
  font-size: 46px;
  font-weight: 300;
  font-variant-numeric: tabular-nums;
  line-height: 1.1;
}
.unit button {
  width: 54px;
  height: 36px;
  border: 0;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.06);
  color: var(--lumna-text);
  font-size: 20px;
}
.colon {
  font-size: 40px;
  font-weight: 300;
}
.quick {
  display: flex;
  justify-content: center;
  gap: 8px;
}
.quick button {
  padding: 7px 12px;
  border: 0;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.06);
  color: var(--lumna-muted);
  font: inherit;
  font-size: 13px;
}
.hint {
  margin: 10px 0 12px;
  text-align: center;
  color: var(--lumna-muted);
  font-size: 13.5px;
}
.text {
  width: 100%;
  box-sizing: border-box;
  padding: 13px 14px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  background: rgba(10, 8, 18, 0.6);
  color: var(--lumna-text);
  font: inherit;
  font-size: 15px;
  outline: none;
}
.repeat {
  margin-top: 12px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  font-size: 15px;
}
.sw {
  position: relative;
  width: 48px;
  height: 28px;
  border: 0;
  border-radius: 999px;
  background: rgba(255, 255, 255, 0.15);
}
.sw::after {
  content: "";
  position: absolute;
  top: 3px;
  left: 3px;
  width: 22px;
  height: 22px;
  border-radius: 50%;
  background: #fff;
  transition: transform 0.15s;
}
.sw.on {
  background: #7c5ce6;
}
.sw.on::after {
  transform: translateX(20px);
}
.row {
  margin-top: 18px;
  display: flex;
  gap: 10px;
}
.btn {
  flex: 1;
  padding: 13px 0;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 14px;
  background: none;
  color: inherit;
  font: inherit;
  font-size: 15px;
}
.btn.go {
  border: 0;
  background: #6d4fc4;
  color: #fff;
  font-weight: 600;
}
</style>
