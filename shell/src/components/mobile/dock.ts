// Celular: os 4 atalhos da barra da Início (o do meio é sempre Apps) e os apps recentes da gaveta.
// Ficam no localStorage, que a barra e a Início (camadas diferentes, mesma origem) dividem: a que
// muda avisa a outra pelo evento "storage".
import { filesWindow, launchApp, settingsOpen, toggleEmma, type App } from "../../system";
import { windows } from "../../compositor";
import { showApp, showHome } from "./apps";

/** Um atalho: um dos do LUMNA ("inicio", "arquivos", "ajustes", "emma") ou "app:<id>". */
export type Slot = string;

export const SLOTS_KEY = "lumna.nav.slots";
export const RECENT_KEY = "lumna.apps.recent";
export const DEFAULT_SLOTS: Slot[] = ["inicio", "arquivos", "ajustes", "emma"];

export const BUILTIN: Record<string, { label: string; icon: string }> = {
  inicio: { label: "Início", icon: "home" },
  arquivos: { label: "Arquivos", icon: "folder" },
  ajustes: { label: "Ajustes", icon: "settings" },
  emma: { label: "Emma", icon: "waves" },
};

export function readSlots(): Slot[] {
  try {
    const s = JSON.parse(localStorage.getItem(SLOTS_KEY) ?? "null");
    if (Array.isArray(s) && s.length === 4 && s.every((x) => typeof x === "string")) return s;
  } catch {}
  return [...DEFAULT_SLOTS];
}
export function writeSlots(slots: Slot[]) {
  try {
    localStorage.setItem(SLOTS_KEY, JSON.stringify(slots));
  } catch {}
}

export function readRecent(): string[] {
  try {
    const r = JSON.parse(localStorage.getItem(RECENT_KEY) ?? "[]");
    if (Array.isArray(r)) return r.filter((x) => typeof x === "string").slice(0, 8);
  } catch {}
  return [];
}
function remember(id: string) {
  try {
    localStorage.setItem(RECENT_KEY, JSON.stringify([id, ...readRecent().filter((x) => x !== id)].slice(0, 8)));
  } catch {}
}

/** Abre um app instalado (um por vez: o que estava na frente sai) e lembra dele nos recentes. */
export async function openApp(app: App) {
  remember(app.id);
  await showApp().catch(() => {});
  await launchApp(app.id);
}

// Como no celular: um app por vez. Se a janela já existe (mesmo minimizada), volta para ela em vez
// de abrir outra. (o Arquivos põe a pasta no título: "Downloads · Arquivos")
async function bringUp(title: string, open: () => Promise<void>) {
  const list = await windows().catch(() => []);
  const found = list.find((w) => w.title === title || w.title.endsWith(` · ${title}`));
  if (found) await showApp(found.address).catch(() => {});
  else {
    await showApp().catch(() => {});
    await open();
  }
}

/** Os Ajustes (numa página) sozinhos na tela: os de antes voltam se já estavam abertos. */
export async function openSettingsAlone(page?: Parameters<typeof settingsOpen>[0]) {
  const list = await windows().catch(() => []);
  const found = list.find((w) => w.title === "Configurações" || w.title.startsWith("Ajustes"));
  await showApp(found?.address).catch(() => {});
  await settingsOpen(page);
}

/** O que um atalho da barra faz ao ser tocado. */
export async function runSlot(slot: Slot, apps: App[]) {
  if (slot === "inicio") return showHome("inicio");
  if (slot === "arquivos") return bringUp("Arquivos", () => filesWindow());
  if (slot === "ajustes") return openSettingsAlone();
  if (slot === "emma") return toggleEmma();
  const app = apps.find((a) => `app:${a.id}` === slot);
  if (app) return openApp(app);
}

/** Nome e ícone de um atalho (o ícone do app, ou o do LUMNA). */
export function slotView(slot: Slot, apps: App[]): { label: string; icon: string; img: string | null } {
  const b = BUILTIN[slot];
  if (b) return { ...b, img: null };
  const app = apps.find((a) => `app:${a.id}` === slot);
  return { label: app?.name ?? "App", icon: "app", img: app?.icon ?? null };
}

/** A área morta das bordas pro deslizar da Início (o painel de gestos vai poder mudar). */
export function edgeDeadZone(): number {
  try {
    const v = Number(localStorage.getItem("lumna.gestos.borda"));
    if (Number.isFinite(v) && v >= 0 && v <= 120 && localStorage.getItem("lumna.gestos.borda") !== null) return v;
  } catch {}
  return 28;
}
