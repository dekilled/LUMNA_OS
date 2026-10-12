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
}

export interface Window {
  address: string;
  class: string;
  title: string;
  workspace: number;
}

export type CompositorEvent =
  | { kind: "workspace_focused"; id: number }
  | { kind: "workspaces_changed" }
  | { kind: "monitor_focused"; name: string }
  | { kind: "monitors_changed" }
  | { kind: "window_opened"; address: string; class: string; title: string }
  | { kind: "window_closed"; address: string };

export const workspaces = () => invoke<Workspace[]>("workspaces");
export const monitors = () => invoke<Monitor[]>("monitors");
export const windows = () => invoke<Window[]>("windows");
export const focusWorkspace = (id: number) => invoke<void>("focus_workspace", { id });

export const onCompositorEvent = (handler: (event: CompositorEvent) => void) =>
  listen<CompositorEvent>("lumna://compositor", (e) => handler(e.payload));
