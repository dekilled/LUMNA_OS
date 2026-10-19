<script setup lang="ts">
// Celular: a central. Abre deslizando a barra de status pra baixo; fecha deslizando pra cima (ou
// tocando na barrinha). Duas abas, como no esboço: Notificações e Atalhos (deslize pros lados).
import { computed, onMounted, onUnmounted, ref } from "vue";
import Icon from "../Icon.vue";
import MusicCard from "../music/MusicCard.vue";
import {
  audioSetVolume,
  audioState,
  audioToggleMute,
  bluetoothPower,
  bluetoothState,
  haptic,
  mobilePanel,
  notificationAction,
  notificationOpen,
  notificationClosed,
  on,
  powerAction,
  powerSetProfile,
  powerState,
  quickAirplane,
  captureStart,
  quickBrightness,
  quickFlashlight,
  quickMobileData,
  quickQuiet,
  quickState,
  settingsOpen,
  status,
  toggleEmma,
  wifiPower,
  wifiState,
  type AudioNode,
  type BluetoothState,
  type Notification,
  type PowerState,
  type Quick,
  type SettingsPage,
  type Status,
  type WifiState,
} from "../../system";
import { swipe } from "../../swipe";

type Tab = "notificacoes" | "atalhos";
const tab = ref<Tab>("notificacoes");

// ---- relógio e status ----
const now = ref(new Date());
const time = computed(() => now.value.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" }));
const date = computed(() => {
  const d = now.value.toLocaleDateString("pt-BR", { weekday: "long", day: "numeric", month: "long" });
  return d.charAt(0).toUpperCase() + d.slice(1);
});
const sys = ref<Status | null>(null);

// ---- notificações ----
interface Item extends Notification {
  at: number;
}
const HISTORY = "lumna.center.history";
const items = ref<Item[]>([]);
const history = ref<Item[]>([]);
const showHistory = ref(false);
const expanded = ref<number | null>(null);
try {
  history.value = JSON.parse(localStorage.getItem(HISTORY) ?? "[]");
} catch {}
const saveHistory = () => {
  try {
    localStorage.setItem(HISTORY, JSON.stringify(history.value.slice(0, 50)));
  } catch {}
};

const plain = (html: string) => new DOMParser().parseFromString(html, "text/html").body.textContent ?? "";
function ago(at: number): string {
  const min = Math.floor((now.value.getTime() - at) / 60000);
  if (min < 1) return "agora";
  if (min < 60) return `há ${min} min`;
  const h = Math.floor(min / 60);
  if (h < 24) return `há ${h} h`;
  return new Date(at).toLocaleDateString("pt-BR", { day: "numeric", month: "short" });
}
function glyph(n: Notification): [string, string] {
  const app = n.app_name.toLowerCase();
  if (/m[uú]sica|music|spotify|player/.test(app)) return ["music", "#a78bfa"];
  if (/arquivo|files|download|transfer/.test(app)) return ["folder", "#7c9cf8"];
  if (/emma/.test(app)) return ["mic", "#a78bfa"];
  if (/telefone|phone|chamada|call|sms|mensage/.test(app)) return ["call", "#4ade80"];
  if (/sistema|system|lumna|atualiza|update|pacman|bateria|battery/.test(app)) return ["settings", "#cbd5e1"];
  return ["bell", "#a78bfa"];
}

function forget(n: Item, reason: 1 | 2 = 2) {
  items.value = items.value.filter((x) => x.id !== n.id);
  history.value = [n, ...history.value.filter((x) => x.id !== n.id || x.at !== n.at)];
  saveHistory();
  notificationClosed(n.id, reason).catch(() => {});
}
function clearAll() {
  for (const n of [...items.value]) forget(n);
}
async function tapNotification(n: Item) {
  if (n.open) {
    items.value = items.value.filter((x) => x.id !== n.id);
    close();
    await notificationOpen(n.open).catch(() => {});
  } else if (n.actions.some(([k]) => k === "default")) {
    await notificationAction(n.id, "default").catch(() => {});
    items.value = items.value.filter((x) => x.id !== n.id);
    close();
  } else expanded.value = expanded.value === n.id ? null : n.id;
}
async function act(n: Item, key: string) {
  await notificationAction(n.id, key).catch(() => {});
  items.value = items.value.filter((x) => x.id !== n.id);
}

// ---- atalhos ----
const quick = ref<Quick | null>(null);
const recording = ref(false);
const wifi = ref<WifiState | null>(null);
const bt = ref<BluetoothState | null>(null);
const output = ref<AudioNode | null>(null);
const power = ref<PowerState | null>(null);
const brightness = ref(50);
const volume = ref(50);
/** Os apps tocando som agora (volume de cada um: o WirePlumber lembra na próxima vez). */
const apps = ref<AudioNode[]>([]);
const appsOpen = ref(false);
const editing = ref(false);
const powerOpen = ref(false);
const error = ref("");
const HIDDEN = "lumna.center.hidden";
const hidden = ref<string[]>([]);
try {
  hidden.value = JSON.parse(localStorage.getItem(HIDDEN) ?? "[]");
} catch {}
const saveHidden = () => {
  try {
    localStorage.setItem(HIDDEN, JSON.stringify(hidden.value));
  } catch {}
};
const QUIET = "lumna.center.quiet";

interface Tile {
  id: string;
  label: string;
  icon: string;
  /** null = indisponível neste aparelho */
  on: boolean | null;
  sub: string;
  toggle?: () => Promise<unknown>;
  page?: SettingsPage;
}

const tiles = computed<Tile[]>(() => {
  const q = quick.value;
  const saver = power.value?.profiles.includes("power-saver") ? power.value.profile === "power-saver" : null;
  return [
    {
      id: "wifi",
      label: "Wi-Fi",
      icon: "wifi",
      on: wifi.value?.available ? wifi.value.powered : null,
      sub: !wifi.value?.available ? "Indisponível" : !wifi.value.powered ? "Desativado" : wifi.value.connected ?? "Ativado",
      toggle: () => wifiPower(!wifi.value?.powered),
      page: "rede",
    },
    {
      id: "bluetooth",
      label: "Bluetooth",
      icon: "bluetooth",
      on: bt.value?.available ? bt.value.powered : null,
      sub: !bt.value?.available ? "Indisponível" : bt.value.powered ? (bt.value.devices.find((d) => d.connected)?.name ?? "Ativado") : "Desativado",
      toggle: () => bluetoothPower(!bt.value?.powered),
      page: "bluetooth",
    },
    {
      id: "dados",
      label: "Dados móveis",
      icon: "signal",
      on: q?.mobile_data ?? null,
      sub: q?.mobile_data == null ? "Sem conexão" : q.mobile_data ? (q.mobile_tech ?? "Ativado") : "Desativado",
      toggle: () => quickMobileData(!q?.mobile_data),
      page: "dados-moveis",
    },
    {
      id: "som",
      label: "Som",
      icon: output.value?.muted ? "mute" : "volume",
      on: output.value ? !output.value.muted : null,
      sub: !output.value ? "Indisponível" : output.value.muted ? "Silencioso" : "Ativado",
      toggle: async () => output.value && audioToggleMute(output.value.id),
      page: "som",
    },
    {
      id: "rotacao",
      label: "Rotação",
      icon: "rotate",
      on: null,
      sub: q?.rotation_sensor ? "Em breve" : "Sem sensor",
    },
    {
      id: "lanterna",
      label: "Lanterna",
      icon: "flashlight",
      on: q?.flashlight ?? null,
      sub: q?.flashlight == null ? "Indisponível" : q.flashlight ? "Ativada" : "Desativada",
      toggle: () => quickFlashlight(!q?.flashlight),
    },
    {
      id: "silencio",
      label: "Não perturbe",
      icon: "moon",
      on: q?.quiet ?? false,
      sub: q?.quiet ? "Ativado" : "Desativado",
      toggle: async () => {
        const next = !q?.quiet;
        await quickQuiet(next);
        try {
          localStorage.setItem(QUIET, next ? "1" : "");
        } catch {}
      },
    },
    {
      id: "aviao",
      label: "Modo avião",
      icon: "plane",
      on: q ? q.airplane : null,
      sub: q?.airplane ? "Ativado" : "Desativado",
      toggle: async () => {
        const next = !q?.airplane;
        await quickAirplane(next);
        if (next && bt.value?.powered) await bluetoothPower(false).catch(() => {});
      },
    },
    {
      id: "captura",
      label: recording.value ? "Parar gravação" : "Captura",
      icon: recording.value ? "stop" : "crop",
      on: recording.value,
      sub: recording.value ? "Gravando a tela" : "Escolher a área",
      // gravando, o mesmo atalho para (a captura::start sabe)
      toggle: () => captureStart(),
    },
    {
      id: "economia",
      label: "Economia",
      icon: "leaf",
      on: saver,
      sub: saver == null ? "Indisponível" : saver ? "Ativada" : "Desativada",
      toggle: () => powerSetProfile(saver ? "balanced" : "power-saver"),
      page: "energia",
    },
  ];
});
const shownTiles = computed(() => (editing.value ? tiles.value : tiles.value.filter((t) => !hidden.value.includes(t.id))));

async function refresh() {
  const [q, w, b, a, p, s] = await Promise.all([
    quickState().catch(() => null),
    wifiState().catch(() => null),
    bluetoothState().catch(() => null),
    audioState().catch(() => null),
    powerState().catch(() => null),
    status().catch(() => null),
  ]);
  quick.value = q;
  wifi.value = w;
  bt.value = b;
  output.value = a?.outputs.find((o) => o.default) ?? a?.outputs[0] ?? null;
  // enquanto arrasta um, a lista não troca debaixo do dedo
  if (!sliding) apps.value = a?.apps ?? [];
  if (!apps.value.length) appsOpen.value = false;
  power.value = p;
  sys.value = s;
  if (q?.brightness != null && !sliding) brightness.value = q.brightness;
  if (output.value && !sliding) volume.value = output.value.volume;
}

async function tapTile(t: Tile) {
  if (editing.value) {
    hidden.value = hidden.value.includes(t.id) ? hidden.value.filter((x) => x !== t.id) : [...hidden.value, t.id];
    saveHidden();
    return;
  }
  if (t.on == null || !t.toggle) return;
  haptic(20).catch(() => {});
  try {
    await t.toggle();
    error.value = "";
  } catch (e) {
    error.value = String(e);
  }
  window.setTimeout(refresh, 400);
}

// segurar um atalho abre a página dele nos Ajustes
let holdTimer = 0;
let held = false;
function holdStart(t: Tile) {
  held = false;
  clearTimeout(holdTimer);
  if (!t.page || editing.value) return;
  holdTimer = window.setTimeout(() => {
    held = true;
    haptic(30).catch(() => {});
    openSettings(t.page);
  }, 550);
}
function holdCancel() {
  clearTimeout(holdTimer);
}
// O toque vira clique uma vez só (o pointerup pode vir em dobro no WebKitGTK).
function tileClick(t: Tile) {
  if (held) {
    held = false;
    return;
  }
  tapTile(t);
}

let sliding = false;
let brightnessTimer = 0;
function setBrightness() {
  sliding = true;
  clearTimeout(brightnessTimer);
  brightnessTimer = window.setTimeout(() => {
    quickBrightness(brightness.value).catch((e) => (error.value = String(e)));
    sliding = false;
  }, 40);
}
let volumeTimer = 0;
function setVolume() {
  sliding = true;
  clearTimeout(volumeTimer);
  volumeTimer = window.setTimeout(() => {
    if (output.value) audioSetVolume(output.value.id, volume.value).catch((e) => (error.value = String(e)));
    sliding = false;
  }, 40);
}

const appTimers = new Map<number, number>();
function setAppVolume(app: AudioNode, value: number) {
  app.volume = value;
  sliding = true;
  clearTimeout(appTimers.get(app.id));
  appTimers.set(
    app.id,
    window.setTimeout(() => {
      audioSetVolume(app.id, value).catch((e) => (error.value = String(e)));
      sliding = false;
    }, 40),
  );
}
async function muteApp(app: AudioNode) {
  app.muted = !app.muted;
  await audioToggleMute(app.id).catch((e) => (error.value = String(e)));
}

async function openSettings(page?: SettingsPage) {
  await close();
  await settingsOpen(page);
}
async function askEmma() {
  await close();
  await toggleEmma();
}
async function doPower(action: "suspend" | "reboot" | "poweroff") {
  powerOpen.value = false;
  await close();
  await powerAction(action).catch((e) => (error.value = String(e)));
}

const close = () => mobilePanel("mobile-center", false);

// ---- ciclo de vida ----
const root = ref<HTMLElement>();
let clock = 0;
let poll = 0;
const stops: (() => void)[] = [];
onMounted(async () => {
  clock = window.setInterval(() => (now.value = new Date()), 10000);
  // o não perturbe vive no shell; volta como estava depois de reiniciar
  try {
    if (localStorage.getItem(QUIET)) quickQuiet(true).catch(() => {});
  } catch {}
  stops.push(
    await on<Notification>("lumna://notification", (n) => {
      const item = { ...n, at: Date.now() };
      const index = items.value.findIndex((x) => x.id === n.id);
      if (index >= 0) items.value[index] = item;
      else items.value = [item, ...items.value];
    }),
  );
  stops.push(await on<number>("lumna://notification-close", (id) => (items.value = items.value.filter((x) => x.id !== id))));
  stops.push(await on<Status>("lumna://status", (s) => (sys.value = s)));
  stops.push(await on<{ recording: boolean }>("lumna://recording", (r) => (recording.value = r.recording)));
  stops.push(
    await on<boolean>("lumna://mobile-panel", (shown) => {
      clearInterval(poll);
      powerOpen.value = false;
      editing.value = false;
      showHistory.value = false;
      error.value = "";
      if (!shown) return;
      now.value = new Date();
      // primeiro a central aparece (com os dados que já tinha), depois atualiza
      requestAnimationFrame(() => refresh());
      poll = window.setInterval(refresh, 3000);
    }),
  );
  // já deixa os atalhos carregados: a primeira abertura não espera nmcli, pw-dump...
  refresh();
  if (root.value) {
    stops.push(
      swipe(root.value, {
        on: {
          up: close,
          left: () => (tab.value = "atalhos"),
          right: () => (tab.value = "notificacoes"),
        },
        threshold: 60,
        // nas listas o arrasto vertical rola (touch-action: pan-y); o lateral troca de aba
        ignore: "input",
      }),
    );
  }
});
onUnmounted(() => {
  clearInterval(clock);
  clearInterval(poll);
  stops.forEach((s) => s());
});
</script>

<template>
  <main ref="root" class="center">
    <div class="top">
      <div class="brand"><span class="gem" /> LUMNA</div>
      <div class="icons">
        <Icon v-if="quick?.mobile_data" name="signal" :size="15" />
        <Icon :name="sys?.network === 'wifi' ? 'wifi' : sys?.network === 'ethernet' ? 'ethernet' : 'offline'" :size="15" />
        <span v-if="sys?.battery != null" class="battery">
          <Icon :name="sys.charging ? 'bolt' : 'battery'" :size="15" />{{ sys.battery }}%
        </span>
      </div>
    </div>
    <div class="clock">
      <div class="time">{{ time }}</div>
      <div class="date">{{ date }}</div>
    </div>

    <!-- tocando no LUMNA Music (nas duas abas) -->
    <MusicCard class="now-playing" openable @opened="close()" />

    <nav class="tabs">
      <button :class="{ on: tab === 'notificacoes' }" @click="tab = 'notificacoes'">Notificações</button>
      <button :class="{ on: tab === 'atalhos' }" @click="tab = 'atalhos'">Atalhos</button>
    </nav>

    <!-- notificações -->
    <section v-if="tab === 'notificacoes'" class="page">
      <div class="section-head">
        <span>{{ showHistory ? "Histórico" : "Hoje" }}</span>
        <button v-if="!showHistory && items.length" class="link" @click="clearAll()">Limpar</button>
        <button v-else-if="showHistory && history.length" class="link" @click="history = []; saveHistory()">Apagar</button>
      </div>
      <div class="scroll">
        <article
          v-for="n in showHistory ? history : items"
          :key="`${n.id}-${n.at}`"
          class="note"
          :class="{ urgent: n.urgent, old: showHistory }"
          @click="!showHistory && tapNotification(n)"
        >
          <span class="note-icon" :style="{ color: glyph(n)[1] }"><Icon :name="glyph(n)[0]" :size="24" /></span>
          <div class="note-body">
            <div class="note-head">
              <span>{{ n.app_name || "Aviso" }}</span>
              <span class="when">{{ ago(n.at) }}</span>
              <button v-if="!showHistory" class="chev" @click.stop="expanded = expanded === n.id ? null : n.id">
                <Icon :name="expanded === n.id ? 'chevron-up' : 'chevron-down'" :size="18" />
              </button>
            </div>
            <strong>{{ plain(n.summary) }}</strong>
            <p v-if="n.body" :class="{ full: expanded === n.id }">{{ plain(n.body) }}</p>
            <div v-if="expanded === n.id && !showHistory" class="note-actions">
              <button v-for="[key, label] in n.actions.filter(([k]) => k !== 'default')" :key="key" @click.stop="act(n, key)">
                {{ label }}
              </button>
              <button @click.stop="forget(n)">Dispensar</button>
            </div>
          </div>
        </article>
        <div v-if="!(showHistory ? history : items).length" class="empty">
          <Icon name="bell" :size="36" />
          <span>{{ showHistory ? "O histórico está vazio." : "Nenhuma notificação por aqui." }}</span>
        </div>
      </div>
      <footer class="foot">
        <button class="link" @click="showHistory = !showHistory">
          {{ showHistory ? "Voltar pras notificações" : "Histórico de notificações" }}
        </button>
        <button class="gear" aria-label="Configurações" @click="openSettings('sistema')"><Icon name="settings" :size="22" /></button>
      </footer>
    </section>

    <!-- atalhos -->
    <section v-else class="page">
      <div class="scroll">
        <div class="grid">
          <button
            v-for="t in shownTiles"
            :key="t.id"
            class="tile"
            :class="{ on: t.on === true, off: t.on == null, hiddenTile: editing && hidden.includes(t.id) }"
            @pointerdown="holdStart(t)"
            @pointerup="holdCancel"
            @pointercancel="holdCancel"
            @click="tileClick(t)"
            @contextmenu.prevent
          >
            <span class="tile-icon"><Icon :name="t.icon" :size="24" /></span>
            <span class="tile-label">{{ t.label }}</span>
            <span class="tile-sub">{{ t.sub }}</span>
            <span v-if="editing" class="tile-edit"><Icon :name="hidden.includes(t.id) ? 'plus' : 'check'" :size="14" /></span>
          </button>
        </div>

        <label class="slider" :class="{ off: quick?.brightness == null }">
          <Icon name="sun" :size="24" />
          <span>
            <small>Brilho</small>
            <input v-model.number="brightness" type="range" min="2" max="100" :disabled="quick?.brightness == null" @input="setBrightness" />
          </span>
        </label>
        <label class="slider" :class="{ off: !output }">
          <Icon :name="output?.muted ? 'mute' : 'volume'" :size="24" />
          <span>
            <small>Volume</small>
            <input v-model.number="volume" type="range" min="0" max="100" :disabled="!output" @input="setVolume" />
          </span>
          <button
            v-if="apps.length"
            class="apps-toggle"
            :class="{ on: appsOpen }"
            :aria-label="appsOpen ? 'Esconder o volume dos apps' : 'Volume de cada app'"
            @click.prevent="appsOpen = !appsOpen"
          >
            <Icon :name="appsOpen ? 'chevron-up' : 'chevron-down'" :size="20" />
            <small>{{ apps.length }}</small>
          </button>
        </label>
        <div v-if="appsOpen" class="app-volumes">
          <div v-for="a in apps" :key="a.id" class="app-volume">
            <button class="mute" :class="{ muted: a.muted }" :aria-label="a.muted ? `Ligar o som de ${a.name}` : `Silenciar ${a.name}`" @click="muteApp(a)">
              <Icon :name="a.muted ? 'mute' : 'volume'" :size="20" />
            </button>
            <span>
              <small>{{ a.name }}</small>
              <input type="range" min="0" max="100" :value="Math.min(100, a.volume)" @input="setAppVolume(a, +($event.target as HTMLInputElement).value)" />
            </span>
          </div>
        </div>

        <button class="emma" @click="askEmma()">
          <span class="emma-icon"><Icon name="mic" :size="24" /></span>
          <span class="emma-text">
            <strong>Perguntar à Emma</strong>
            <small>Pronta para ajudar quando você precisar</small>
          </span>
          <Icon name="forward" :size="20" />
        </button>

        <div class="actions">
          <button :class="{ on: editing }" @click="editing = !editing"><Icon :name="editing ? 'check' : 'edit'" :size="22" /> {{ editing ? "Pronto" : "Editar" }}</button>
          <button @click="openSettings()"><Icon name="settings" :size="22" /> Configurações</button>
          <button class="pw" aria-label="Energia" @click="powerOpen = !powerOpen"><Icon name="power" :size="24" /></button>
        </div>
        <div v-if="powerOpen" class="power">
          <button @click="doPower('suspend')"><Icon name="moon" :size="20" /> Suspender</button>
          <button @click="doPower('reboot')"><Icon name="refresh" :size="20" /> Reiniciar</button>
          <button class="danger" @click="doPower('poweroff')"><Icon name="power" :size="20" /> Desligar</button>
        </div>
        <p v-if="editing" class="hint">Toque nos atalhos pra mostrar ou esconder.</p>
        <p v-if="error" class="error" @click="error = ''">{{ error }}</p>
      </div>
    </section>

    <div class="dots">
      <span :class="{ on: tab === 'notificacoes' }" />
      <span :class="{ on: tab === 'atalhos' }" />
    </div>
    <button class="handle-area" aria-label="Fechar" @click="close()"><div class="handle" /></button>
  </main>
</template>

<style scoped>
.center {
  height: 100vh;
  box-sizing: border-box;
  padding: 14px 16px 0;
  display: flex;
  flex-direction: column;
  overflow: hidden;
  background:
    radial-gradient(circle at 115% -5%, transparent 44%, rgba(167, 139, 250, 0.45) 45%, rgba(91, 33, 182, 0.2) 47%, transparent 56%),
    radial-gradient(ellipse at 30% 60%, rgba(29, 18, 64, 0.97), rgba(7, 4, 15, 0.98) 70%);
  touch-action: none;
  user-select: none;
}
.top {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 0 4px;
}
.brand {
  display: flex;
  align-items: center;
  gap: 14px;
  letter-spacing: 0.5em;
  font-size: 13px;
  color: var(--lumna-violet-soft);
}
.gem {
  width: 14px;
  height: 14px;
  transform: rotate(45deg);
  border: 2px solid var(--lumna-violet-soft);
  border-radius: 3px;
}
.icons,
.battery {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 13px;
}
.battery {
  gap: 2px;
}
.clock {
  margin: 22px 4px 18px;
}
.time {
  font-size: 56px;
  font-weight: 300;
  line-height: 1;
}
.date {
  margin-top: 6px;
  color: var(--lumna-muted);
  font-size: 16px;
}
.now-playing {
  margin: 0 0 10px;
}
.tabs {
  display: flex;
  border-bottom: 1px solid rgba(255, 255, 255, 0.1);
}
.tabs button {
  flex: 1;
  padding: 10px 0 12px;
  border: 0;
  border-bottom: 2px solid transparent;
  margin-bottom: -1px;
  background: none;
  color: var(--lumna-muted);
  font: inherit;
  font-size: 16px;
}
.tabs button.on {
  color: var(--lumna-text);
  border-bottom-color: var(--lumna-violet-soft);
}
.page {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
}
.scroll {
  flex: 1;
  min-height: 0;
  overflow-y: auto;
  touch-action: pan-y;
  padding-top: 12px;
  scrollbar-width: none;
}
.section-head {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 16px 4px 0;
  font-size: 16px;
}
.link {
  border: 0;
  background: none;
  color: var(--lumna-violet-soft);
  font: inherit;
  font-size: 14px;
}
.note {
  display: flex;
  gap: 12px;
  margin-bottom: 8px;
  padding: 14px;
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
}
.note.urgent {
  border-color: rgba(248, 113, 113, 0.5);
}
.note.old {
  opacity: 0.7;
}
.note-icon {
  flex: none;
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: rgba(167, 139, 250, 0.12);
}
.note-body {
  flex: 1;
  min-width: 0;
}
.note-head {
  display: flex;
  align-items: center;
  gap: 8px;
  color: var(--lumna-muted);
  font-size: 12px;
}
.note-head span:first-child {
  flex: 1;
}
.chev {
  width: 26px;
  height: 22px;
  display: grid;
  place-items: center;
  border: 0;
  background: none;
  color: var(--lumna-muted);
}
.note strong {
  display: block;
  margin-top: 2px;
  font-size: 16px;
  font-weight: 500;
}
.note p {
  margin: 3px 0 0;
  color: var(--lumna-muted);
  font-size: 14px;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
}
.note p.full {
  white-space: normal;
}
.note-actions {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  margin-top: 10px;
}
.note-actions button {
  padding: 8px 14px;
  border: 0;
  border-radius: 10px;
  background: rgba(167, 139, 250, 0.16);
  color: var(--lumna-text);
  font: inherit;
  font-size: 13px;
}
.empty {
  display: flex;
  flex-direction: column;
  align-items: center;
  gap: 12px;
  padding: 60px 30px;
  color: var(--lumna-muted);
  text-align: center;
}
.foot {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 10px 4px 0;
}
.gear {
  border: 0;
  background: none;
  color: var(--lumna-muted);
}
.grid {
  display: grid;
  grid-template-columns: repeat(3, 1fr);
  gap: 8px;
}
.tile {
  position: relative;
  min-height: 98px;
  padding: 12px;
  display: flex;
  flex-direction: column;
  align-items: flex-start;
  border: 1px solid rgba(255, 255, 255, 0.05);
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
  color: inherit;
  font: inherit;
  text-align: left;
}
.tile-icon {
  width: 42px;
  height: 42px;
  display: grid;
  place-items: center;
  margin-bottom: 8px;
  border-radius: 12px;
  background: rgba(255, 255, 255, 0.08);
}
.tile.on .tile-icon {
  background: #b9a2fb;
  color: #1a1030;
}
.tile.off {
  opacity: 0.45;
}
.tile.hiddenTile {
  opacity: 0.3;
}
.tile-label {
  font-size: 14px;
}
.tile-sub {
  max-width: 100%;
  overflow: hidden;
  white-space: nowrap;
  text-overflow: ellipsis;
  color: var(--lumna-muted);
  font-size: 12px;
}
.tile.on .tile-sub {
  color: var(--lumna-violet-soft);
}
.tile-edit {
  position: absolute;
  top: 8px;
  right: 8px;
  width: 22px;
  height: 22px;
  display: grid;
  place-items: center;
  border-radius: 50%;
  background: var(--lumna-violet-soft);
  color: #1a1030;
}
.slider {
  margin-top: 8px;
  padding: 10px 16px;
  display: flex;
  align-items: center;
  gap: 14px;
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
}
.apps-toggle {
  display: flex;
  align-items: center;
  gap: 2px;
  height: 40px;
  padding: 0 10px;
  border: 0;
  border-radius: 12px;
  background: rgba(167, 139, 250, 0.12);
  color: var(--lumna-violet-soft);
}
.apps-toggle small {
  color: inherit;
}
.apps-toggle.on {
  background: rgba(167, 139, 250, 0.24);
}
.app-volumes {
  margin-top: 6px;
  padding: 6px 12px;
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
}
.app-volume {
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 4px 0;
}
.app-volume > span {
  flex: 1;
  min-width: 0;
  display: flex;
  flex-direction: column;
}
.app-volume small {
  overflow: hidden;
  color: var(--lumna-muted);
  font-size: 12px;
  text-overflow: ellipsis;
  white-space: nowrap;
}
.app-volume .mute {
  width: 40px;
  height: 40px;
  display: grid;
  place-items: center;
  border: 0;
  border-radius: 50%;
  background: none;
  color: var(--lumna-text);
}
.app-volume .mute.muted {
  color: #fca5a5;
}
.slider.off {
  opacity: 0.45;
}
.slider > span {
  flex: 1;
  display: flex;
  flex-direction: column;
}
.slider small {
  color: var(--lumna-muted);
  font-size: 12px;
}
input[type="range"] {
  width: 100%;
  height: 28px;
  accent-color: var(--lumna-violet-soft);
  touch-action: pan-x;
}
.emma {
  width: 100%;
  margin-top: 8px;
  padding: 12px 14px;
  display: flex;
  align-items: center;
  gap: 12px;
  border: 0;
  border-radius: 18px;
  background: rgba(24, 21, 32, 0.92);
  color: inherit;
  font: inherit;
  text-align: left;
}
.emma-icon {
  width: 44px;
  height: 44px;
  display: grid;
  place-items: center;
  border-radius: 12px;
  background: #3c3355;
  color: var(--lumna-violet-soft);
}
.emma-text {
  flex: 1;
  display: flex;
  flex-direction: column;
}
.emma-text small {
  color: var(--lumna-muted);
  font-size: 12px;
}
.actions {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}
.actions button,
.power button {
  flex: 1;
  padding: 14px 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  gap: 8px;
  border: 0;
  border-radius: 16px;
  background: rgba(24, 21, 32, 0.92);
  color: inherit;
  font: inherit;
  font-size: 14px;
}
.actions button.on {
  color: var(--lumna-violet-soft);
}
.actions .pw {
  flex: 0 0 64px;
}
.power {
  display: flex;
  gap: 8px;
  margin-top: 8px;
}
.power .danger {
  color: #fca5a5;
}
.hint,
.error {
  margin: 10px 4px 0;
  font-size: 12px;
  color: var(--lumna-muted);
}
.error {
  color: #fca5a5;
}
.dots {
  display: flex;
  justify-content: center;
  gap: 10px;
  padding-top: 10px;
}
.dots span {
  width: 7px;
  height: 7px;
  border-radius: 50%;
  background: rgba(255, 255, 255, 0.25);
}
.dots span.on {
  background: var(--lumna-violet-soft);
}
.handle-area {
  width: 100%;
  height: 34px;
  border: 0;
  background: none;
}
.handle {
  margin: 0 auto;
  width: 120px;
  height: 4px;
  border-radius: 2px;
  background: rgba(236, 232, 255, 0.7);
}
</style>
