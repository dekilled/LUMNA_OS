import { convertFileSrc, invoke } from "@tauri-apps/api/core";
import { listen, type UnlistenFn } from "@tauri-apps/api/event";

export interface App {
  id: string;
  name: string;
  comment: string;
  icon: string | null;
  wm_class: string | null;
}

export interface Status {
  network: "wifi" | "ethernet" | "none";
  volume: number | null;
  muted: boolean;
  battery: number | null;
  charging: boolean;
}

export interface Notification {
  id: number;
  app_name: string;
  summary: string;
  body: string;
  actions: [string, string][];
  timeout: number;
  urgent: boolean;
}

export const apps = () => invoke<App[]>("apps");
export const launchApp = (id: string) => invoke<void>("launch_app", { id });
export const status = () => invoke<Status>("status");
export const toggleMute = () => invoke<void>("toggle_mute");
export const changeVolume = (step: number) => invoke<void>("change_volume", { step });
export const toggleLauncher = () => invoke<void>("toggle_launcher");

export interface WifiNetwork {
  path: string;
  name: string;
  security: "open" | "psk" | "8021x" | string;
  /** 0 a 4 tracinhos. */
  signal: number;
  known: boolean;
  connected: boolean;
}

export interface WifiState {
  available: boolean;
  powered: boolean;
  scanning: boolean;
  state: string;
  connected: string | null;
  networks: WifiNetwork[];
}

export const toggleWifi = () => invoke<void>("toggle_wifi");
export const wifiState = () => invoke<WifiState>("wifi_state");
export const wifiScan = () => invoke<void>("wifi_scan");
export const wifiConnect = (path: string, passphrase?: string) =>
  invoke<void>("wifi_connect", { path, passphrase: passphrase ?? null });
export const wifiDisconnect = () => invoke<void>("wifi_disconnect");
export const wifiForget = (path: string) => invoke<void>("wifi_forget", { path });
export const wifiPower = (on: boolean) => invoke<void>("wifi_power", { on });

/** Ajusta a camada desta janela ao tamanho do conteúdo. */
export const layerResize = (width: number, height: number) =>
  invoke<void>("layer_resize", { width: Math.ceil(width), height: Math.ceil(height) });
export const layerHide = () => invoke<void>("layer_hide");

export const notificationClosed = (id: number, reason: 1 | 2) => invoke<void>("notification_closed", { id, reason });
export const notificationAction = (id: number, key: string) => invoke<void>("notification_action", { id, key });

/** Escuta um evento do Rust (os emitidos para todas as janelas e os desta janela). */
export const on = <T>(event: string, handler: (payload: T) => void): Promise<UnlistenFn> =>
  listen<T>(event, (e) => handler(e.payload));

/** Mantém a camada do tamanho do elemento, inclusive quando ele cresce ou encolhe. */
export function fitLayerTo(element: HTMLElement): () => void {
  const observer = new ResizeObserver(() => {
    const rect = element.getBoundingClientRect();
    layerResize(rect.width, rect.height);
  });
  observer.observe(element);
  return () => observer.disconnect();
}

export type Mode = "desktop" | "gaming" | "media";
export type Layout = "per-screen" | "extend";

export interface ModesSnapshot {
  /** Modo de cada monitor, pelo nome do monitor no compositor. */
  modes: Record<string, Mode>;
  layout: Layout;
  focused: string | null;
}

export const MODE_LABEL: Record<Mode, string> = { desktop: "Desktop", gaming: "Gaming", media: "Mídia" };

export const modes = () => invoke<ModesSnapshot>("modes");
export const switchTo = (mode: Mode) => invoke<void>("switch_to", { mode });
export const changeLayout = (layout: Layout) => invoke<void>("change_layout", { layout });
export const showSwitcher = () => invoke<void>("show_switcher");
/** Monitor (nome no compositor) desta janela; `null` se ainda não deu para saber. */
export const monitorName = () => invoke<string | null>("monitor_name");

export interface Game {
  id: string;
  source: "steam" | "epic" | "gog" | "amazon" | "lutris";
  title: string;
  /** Caminho local ou URL. */
  cover: string | null;
  hero: string | null;
  last_played: number;
}

export type PadAction = "up" | "down" | "left" | "right" | "accept" | "back" | "menu";

export const games = () => invoke<Game[]>("games");
export const launchGame = (id: string) => invoke<void>("launch_game", { id });

/** Imagem de um jogo pronta para o `<img>`: arquivo local pelo protocolo `asset`, ou a URL. */
export const artUrl = (path: string | null) => (!path ? null : path.startsWith("/") ? convertFileSrc(path) : path);

/** Ações do controle (e do teclado, nas telas que repassam) que valem para este monitor. */
export const onPad = (monitor: () => string | null, handler: (action: PadAction) => void) =>
  on<{ action: PadAction; monitor: string | null }>("lumna://pad", (e) => {
    if (!e.monitor || e.monitor === monitor()) handler(e.action);
  });

/** Setas, Enter e Esc viram as mesmas ações do controle. */
export function keyToPad(event: KeyboardEvent): PadAction | null {
  const map: Record<string, PadAction> = {
    ArrowUp: "up",
    ArrowDown: "down",
    ArrowLeft: "left",
    ArrowRight: "right",
    Enter: "accept",
    Escape: "back",
  };
  return map[event.key] ?? null;
}

export interface Service {
  id: string;
  name: string;
}

export interface Video {
  id: string;
  title: string;
  thumb: string | null;
  progress: number;
  watched_at: number;
}

export interface Pairing {
  code: string;
  addresses: string[];
  port: number;
  expires_in: number;
  devices: string[];
}

export const mediaServices = () => invoke<Service[]>("media_services");
export const videos = () => invoke<Video[]>("videos");
export const playVideo = (id: string) => invoke<void>("play_video", { id });
export const openService = (id: string) => invoke<void>("open_service", { id });
export const remotePairing = () => invoke<Pairing>("remote_pairing");
export const remoteStopPairing = () => invoke<void>("remote_stop_pairing");
export const remoteForget = () => invoke<void>("remote_forget");
