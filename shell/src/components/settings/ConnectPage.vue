<script setup lang="ts">
// Configurações → Celular (PC): o Celular Connect daqui. Liga/desliga, pareia (o código de 6
// dígitos vale 5 minutos e uma vez só, enquanto a página está aberta), os recursos de cada
// celular, a pasta de recebidos, os arquivos indo e vindo e os agendamentos do PC.
import { onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import Schedules from "../connect/Schedules.vue";
import Transfers from "../connect/Transfers.vue";
import {
  connectSettings,
  connectSettingsSet,
  on,
  remoteDeviceForget,
  remoteDevices,
  remoteDeviceSet,
  remotePairing,
  remoteSendClipboard,
  remoteStopPairing,
  scheduleAdd,
  scheduleList,
  scheduleRemove,
  type ConnectFeatures,
  type ConnectSettings,
  type Pairing,
  type RemoteDevice,
  type ScheduleTask,
} from "../../system";

const FEATURES: { key: keyof ConnectFeatures; title: string; sub: string }[] = [
  { key: "notifications", title: "Avisos deste PC no celular", sub: "As notificações daqui aparecem lá" },
  { key: "phone_notifications", title: "Avisos do celular aqui", sub: "Notificações e ligações chegando" },
  { key: "clipboard", title: "Área de transferência", sub: "O que copiar num aparece no outro" },
  { key: "files", title: "Receber arquivos do celular", sub: "Vão pra pasta de recebidos" },
  { key: "control", title: "Controle de longe", sub: "Energia, Emma, senhas e agendamentos" },
];

const pairing = ref<Pairing | null>(null);
const left = ref(0);
const paired = ref("");
const error = ref("");
const note = ref("");
const settings = ref<ConnectSettings | null>(null);
const dir = ref("");
const devices = ref<RemoteDevice[]>([]);
const open = ref<string | null>(null);
const tasks = ref<ScheduleTask[]>([]);
let timer = 0;

async function refresh() {
  try {
    pairing.value = await remotePairing();
    left.value = pairing.value.expires_in;
    error.value = "";
  } catch (e) {
    error.value = String(e);
  }
}
async function setEnabled(enabled: boolean) {
  settings.value = await connectSettingsSet({ enabled }).catch((e) => ((error.value = String(e)), settings.value));
}
async function saveDir() {
  try {
    settings.value = await connectSettingsSet({ receiveDir: dir.value.trim() === settings.value?.default_dir ? "" : dir.value.trim() });
    dir.value = settings.value.receive_dir;
    flash("Pasta salva.");
  } catch (e) {
    error.value = String(e);
  }
}
async function toggle(d: RemoteDevice, key: keyof ConnectFeatures) {
  d.features = { ...d.features, [key]: !d.features[key] };
  await remoteDeviceSet(d.id, d.features).catch((e) => (error.value = String(e)));
}
async function sendClipboard(d: RemoteDevice) {
  try {
    await remoteSendClipboard(d.id);
    flash(`Mandei o que você copiou pro ${d.name}.`);
  } catch (e) {
    error.value = String(e);
  }
}
async function forget(d: RemoteDevice) {
  await remoteDeviceForget(d.id).catch((e) => (error.value = String(e)));
  devices.value = await remoteDevices();
}
async function addTask(task: Omit<ScheduleTask, "id" | "by">) {
  await scheduleAdd(task).catch((e) => (error.value = String(e)));
}
async function removeTask(id: string) {
  await scheduleRemove(id).catch((e) => (error.value = String(e)));
}
function flash(text: string) {
  note.value = text;
  window.setTimeout(() => (note.value = ""), 3000);
}

const stops: (() => void)[] = [];
onMounted(async () => {
  await refresh();
  settings.value = await connectSettings().catch(() => null);
  dir.value = settings.value?.receive_dir ?? "";
  devices.value = await remoteDevices().catch(() => []);
  tasks.value = await scheduleList().catch(() => []);
  timer = window.setInterval(() => {
    left.value = Math.max(0, left.value - 1);
    if (left.value === 0) refresh();
  }, 1000);
  stops.push(
    await on<string>("lumna://remote-paired", (name) => {
      paired.value = name;
      refresh();
    }),
  );
  stops.push(await on<RemoteDevice[]>("lumna://remote-devices", (l) => (devices.value = l)));
  stops.push(await on<ScheduleTask[]>("lumna://schedule", (l) => (tasks.value = l)));
});
onUnmounted(() => {
  clearInterval(timer);
  remoteStopPairing().catch(() => {});
  stops.forEach((s) => s());
});
</script>

<template>
  <section class="page">
    <h1>Celular</h1>
    <p class="lead">
      O <b>Celular Connect</b> junta este PC e o LUMNA no celular: avisos, ligações, área de transferência, arquivos, pedidos de
      senha, a Emma e comandos de longe. O app <b>LUMNA Controle</b> também pareia por aqui.
    </p>
    <p v-if="error" class="error">{{ error }}</p>

    <div class="card">
      <div class="row">
        <div class="grow">
          <div class="title">Celular Connect</div>
          <div class="sub">{{ settings?.enabled === false ? "Desligado: nenhum celular conecta (nem o LUMNA Controle)" : "Ligado" }}</div>
        </div>
        <button class="switch" :class="{ on: settings?.enabled }" :disabled="!settings" @click="setEnabled(!settings?.enabled)" />
      </div>
    </div>

    <template v-if="settings?.enabled !== false">
      <h2>Código de pareamento</h2>
      <div class="card padded code-card">
        <div class="code">{{ pairing?.code.replace(/(\d{3})(\d{3})/, "$1 $2") ?? "······" }}</div>
        <div class="note">
          No celular: Ajustes → Celular Connect → Adicionar. Vale por mais {{ Math.floor(left / 60) }}:{{ String(left % 60).padStart(2, "0") }} e uma vez só.
        </div>
        <div v-if="pairing?.addresses.length" class="note">
          Se o celular não achar este PC sozinho, digite: <b>{{ pairing.addresses.join(" ou ") }}</b>
        </div>
        <div v-if="paired" class="ok"><Icon name="check" :size="16" /> {{ paired }} pareado!</div>
      </div>

      <h2>Celulares pareados</h2>
      <div class="card">
        <template v-for="d in devices" :key="d.id">
          <button class="row" @click="open = open === d.id ? null : d.id">
            <span class="badge" :class="{ live: d.online }"><Icon name="phone" :size="18" /></span>
            <div class="grow">
              <div class="title">{{ d.name }}</div>
              <div class="sub" :class="{ online: d.online }">{{ d.online ? "Conectado" : "Desconectado" }}</div>
            </div>
            <Icon :name="open === d.id ? 'chevron-up' : 'chevron-down'" :size="18" />
          </button>
          <div v-if="open === d.id" class="device">
            <div v-for="f in FEATURES" :key="f.key" class="feature">
              <div class="grow">
                <div class="title">{{ f.title }}</div>
                <div class="sub">{{ f.sub }}</div>
              </div>
              <button class="switch" :class="{ on: d.features[f.key] }" @click="toggle(d, f.key)" />
            </div>
            <div class="buttons">
              <button class="btn" :disabled="!d.online" @click="sendClipboard(d)"><Icon name="clipboard" :size="16" /> Mandar o que copiei</button>
              <button class="btn danger" @click="forget(d)">Esquecer este celular</button>
            </div>
            <p class="hint">Pra mandar arquivos: no Arquivos, botão direito → Enviar para {{ d.name }}.</p>
          </div>
        </template>
        <div v-if="!devices.length" class="row"><div class="grow sub">Nenhum ainda.</div></div>
      </div>
      <p v-if="note" class="okline">{{ note }}</p>

      <h2>Arquivos recebidos</h2>
      <div class="card padded">
        <div class="dir">
          <input v-model="dir" :placeholder="settings?.default_dir" @keydown.enter="saveDir()" />
          <button class="btn" :disabled="!dir.trim() || dir === settings?.receive_dir" @click="saveDir()">Salvar</button>
        </div>
        <Transfers class="transfers" />
      </div>

      <h2>Agendamentos</h2>
      <p class="away">Tarefas que este PC faz na hora marcada. O celular também agenda daqui de longe.</p>
      <Schedules :tasks="tasks" @add="addTask" @remove="removeTask" />

      <h2>Fora de casa</h2>
      <p class="away">
        Com o <b>Tailscale</b> ligado neste PC e no celular, o Connect funciona de qualquer lugar: no celular, adicione o PC pelo
        endereço 100.x.y.z do Tailscale.
      </p>
    </template>
  </section>
</template>

<style scoped>
.code-card {
  text-align: center;
}
.code {
  font-size: 44px;
  letter-spacing: 0.15em;
  font-variant-numeric: tabular-nums;
  margin: 6px 0 8px;
}
.ok {
  margin-top: 10px;
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #4ade80;
}
.okline {
  margin: 8px 2px 0;
  color: #4ade80;
  font-size: 14px;
}
.badge.live {
  background: rgba(139, 92, 246, 0.3);
  color: var(--lumna-violet-soft);
}
.sub.online {
  color: #4ade80;
}
.device {
  padding: 4px 18px 16px 66px;
}
.feature {
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 10px 0;
}
.feature .grow {
  flex: 1;
  min-width: 0;
}
.feature .title {
  font-size: 14px;
}
.feature .sub {
  margin-top: 2px;
  color: var(--lumna-muted);
  font-size: 12.5px;
}
.feature + .feature {
  border-top: 1px solid rgba(255, 255, 255, 0.05);
}
.buttons {
  margin-top: 10px;
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}
.buttons .btn {
  display: inline-flex;
  align-items: center;
  gap: 6px;
}
.hint {
  margin: 10px 0 0;
  color: var(--lumna-muted);
  font-size: 13px;
}
.dir {
  display: flex;
  gap: 8px;
}
.dir input {
  flex: 1;
  min-width: 0;
  padding: 9px 12px;
  border: 1px solid rgba(255, 255, 255, 0.12);
  border-radius: 10px;
  background: rgba(10, 8, 18, 0.5);
  color: var(--lumna-text);
  font: inherit;
  font-size: 14px;
  outline: none;
}
.transfers {
  margin-top: 12px;
}
.away {
  margin: 0 0 10px;
  color: var(--lumna-muted);
  font-size: 14px;
  line-height: 1.5;
}
</style>
