import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";

export interface Workspace {
  id: number;
  name: string;
  monitor: string;
  windows: number;
}

export interface Monitor {
  id: number;
  name: string;
  focused: boolean;
  active_workspace: number;
  x: number;
  y: number;
  /** Tamanho no layout (já com a escala). */
  width: number;
  height: number;
  scale: number;
  /** Fabricante e modelo. */
  label: string;
}

export interface Window {
  address: string;
  class: string;
  title: string;
  workspace: number;
  /** "special:minimized" = minimizada pelo LUMNA. */
  workspace_name: string;
  /** Id do monitor. */
  monitor: number;
  /** 0 = normal, 1 = maximizada, 2 = tela cheia. */
  fullscreen: number;
}

export type WindowAction = "close" | "minimize" | "maximize" | "fullscreen" | "focus";

export type CompositorEvent =
  | { kind: "workspace_focused"; id: number }
  | { kind: "workspaces_changed" }
  | { kind: "monitor_focused"; name: string }
  | { kind: "monitors_changed" }
  | { kind: "window_opened"; address: string; class: string; title: string }
  | { kind: "window_closed"; address: string }
  | { kind: "active_window_changed" }
  | { kind: "fullscreen_changed" };

export const workspaces = () => invoke<Workspace[]>("workspaces");
export const monitors = () => invoke<Monitor[]>("monitors");
export const windows = () => invoke<Window[]>("windows");
export const activeWindow = () => invoke<Window | null>("active_window");
/** Sem `address`, vale para a janela em foco. */
export const windowControl = (action: WindowAction, address?: string) =>
  invoke<void>("window_control", { action, address: address ?? null });
/** As janelas têm barra de título própria (plugin hyprbars)? */
export const windowBars = () => invoke<boolean>("window_bars");

export const isMinimized = (w: Window) => w.workspace_name === "special:minimized";
export const focusWorkspace = (id: number) => invoke<void>("focus_workspace", { id });

export const onCompositorEvent = (handler: (event: CompositorEvent) => void) =>
  listen<CompositorEvent>("lumna://compositor", (e) => handler(e.payload));
