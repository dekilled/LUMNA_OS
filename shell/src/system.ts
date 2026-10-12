import { invoke } from "@tauri-apps/api/core";
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
