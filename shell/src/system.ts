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

// ---- Configurações ----

export type SettingsPage = "rede" | "bluetooth" | "som" | "impressoras" | "sistema";
export const settingsOpen = (page?: SettingsPage) => invoke<void>("settings_open", { page: page ?? null });

export interface NetInterface {
  name: string;
  kind: "wifi" | "ethernet";
  connected: boolean;
  speed: number | null;
  mac: string;
  ipv4: string[];
  ipv6: string[];
  gateway: string | null;
  dns: string[];
}
export const networkInterfaces = () => invoke<NetInterface[]>("network_interfaces");

export interface BtDevice {
  path: string;
  address: string;
  name: string;
  icon: string;
  paired: boolean;
  connected: boolean;
  battery: number | null;
}
export interface BluetoothState {
  available: boolean;
  powered: boolean;
  discovering: boolean;
  devices: BtDevice[];
}
export interface BtConfirm {
  device: string;
  name: string;
  code: string;
  kind: "confirm" | "type";
}
export const bluetoothState = () => invoke<BluetoothState>("bluetooth_state");
export const bluetoothPower = (on: boolean) => invoke<void>("bluetooth_power", { on });
export const bluetoothScan = (on: boolean) => invoke<void>("bluetooth_scan", { on });
export const bluetoothPair = (path: string) => invoke<void>("bluetooth_pair", { path });
export const bluetoothConnect = (path: string) => invoke<void>("bluetooth_connect", { path });
export const bluetoothDisconnect = (path: string) => invoke<void>("bluetooth_disconnect", { path });
export const bluetoothForget = (path: string) => invoke<void>("bluetooth_forget", { path });
export const bluetoothConfirm = (device: string, accept: boolean) => invoke<void>("bluetooth_confirm", { device, accept });

export interface AudioNode {
  id: number;
  name: string;
  volume: number;
  muted: boolean;
  default: boolean;
}
export interface AudioState {
  outputs: AudioNode[];
  inputs: AudioNode[];
  apps: AudioNode[];
}
export const audioState = () => invoke<AudioState>("audio_state");
export const audioSetDefault = (id: number) => invoke<void>("audio_set_default", { id });
export const audioSetVolume = (id: number, volume: number) => invoke<void>("audio_set_volume", { id, volume });
export const audioToggleMute = (id: number) => invoke<void>("audio_toggle_mute", { id });

export interface Printer {
  name: string;
  state: "idle" | "printing" | "disabled";
  default: boolean;
  uri: string;
  jobs: number;
}
export interface FoundPrinter {
  uri: string;
  info: string;
  class: string;
}
export const printers = () => invoke<Printer[]>("printers");
export const printersDiscover = () => invoke<FoundPrinter[]>("printers_discover");
export const printerAdd = (uri: string, info: string) => invoke<string>("printer_add", { uri, info });
export const printerRemove = (name: string) => invoke<void>("printer_remove", { name });
export const printerSetDefault = (name: string) => invoke<void>("printer_set_default", { name });
export const printerTestPage = (name: string) => invoke<void>("printer_test_page", { name });

export interface SystemInfo {
  hostname: string;
  os: string;
  kernel: string;
  cpu: string;
  cores: number;
  gpus: string[];
  memory: number;
  disk_total: number;
  disk_used: number;
  uptime: number;
}
export const systemInfo = () => invoke<SystemInfo>("system_info");

// ---- senha de administrador (polkit) ----

export interface AuthRequest {
  cookie: string;
  message: string;
  action: string;
  user: string;
  error: string | null;
}
export const authRespond = (cookie: string, password: string | null) => invoke<void>("auth_respond", { cookie, password });

// ---- Loja ----

export type StoreSource = "arch" | "flathub" | "aur";
export type StoreTab = "descobrir" | "instalados" | "atualizacoes" | "fontes";
export interface StorePackage {
  source: StoreSource;
  name: string;
  title: string;
  summary: string;
  version: string;
  repo: string;
  installed: boolean;
  icon: string | null;
  essential: boolean;
}
export interface StoreUpdate {
  source: StoreSource;
  name: string;
  from: string;
  to: string;
}
export interface StoreSources {
  multilib: boolean;
  flatpak: boolean;
  flathub: boolean;
  aur: boolean;
  aur_tools: boolean;
}
export const storeOpen = (tab?: StoreTab) => invoke<void>("store_open", { tab: tab ?? null });
export const storeSearch = (query: string) => invoke<StorePackage[]>("store_search", { query });
export const storeInstalled = () => invoke<StorePackage[]>("store_installed");
export const storeUpdates = () => invoke<StoreUpdate[]>("store_updates");
export const storeSources = () => invoke<StoreSources>("store_sources");
export const storeInstall = (p: Pick<StorePackage, "source" | "name" | "repo">) =>
  invoke<void>("store_install", { source: p.source, name: p.name, repo: p.repo });
export const storeRemove = (p: Pick<StorePackage, "source" | "name">) => invoke<void>("store_remove", { source: p.source, name: p.name });
export const storeUpgrade = (source: StoreSource) => invoke<void>("store_upgrade", { source });
export const storeSetMultilib = (on: boolean) => invoke<void>("store_set_multilib", { on });
export const storeSetFlathub = (on: boolean) => invoke<void>("store_set_flathub", { on });
export const storeSetAur = (on: boolean) => invoke<void>("store_set_aur", { on });

// ---- Arquivos ----

export type FileKind =
  | "folder" | "image" | "video" | "audio" | "document" | "sheet" | "slides" | "pdf"
  | "archive" | "code" | "text" | "app" | "disk" | "file";
export interface FileEntry {
  name: string;
  path: string;
  dir: boolean;
  link: boolean;
  size: number;
  modified: number;
  kind: FileKind;
  hidden: boolean;
}
export interface Listing {
  path: string;
  parent: string | null;
  writable: boolean;
  entries: FileEntry[];
}
export interface Place {
  name: string;
  path: string;
  icon: string;
}
export interface Drive {
  device: string;
  label: string;
  size: number;
  mountpoint: string | null;
  removable: boolean;
  fstype: string;
}
export interface Places {
  home: string;
  places: Place[];
  trash: number;
  drives: Drive[];
}
export interface TrashItem {
  id: string;
  name: string;
  original: string;
  deleted: string;
  dir: boolean;
  kind: FileKind;
}
export interface FileInfo {
  path: string;
  size: number;
  items: number;
  modified: number;
  accessed: number;
  mode: string;
  owner: string;
}
export const filesWindow = (path?: string) => invoke<void>("files_window", { path: path ?? null });
export const filesList = (path: string, hidden: boolean) => invoke<Listing>("files_list", { path, hidden });
export const filesStamp = (path: string) => invoke<number>("files_stamp", { path });
export const filesPlaces = () => invoke<Places>("files_places");
export const filesOpen = (path: string) => invoke<void>("files_open", { path });
export const filesTerminal = (path: string) => invoke<void>("files_terminal", { path });
export const filesMkdir = (parent: string, name: string) => invoke<string>("files_mkdir", { parent, name });
export const filesRename = (path: string, name: string) => invoke<string>("files_rename", { path, name });
export const filesTrash = (paths: string[]) => invoke<void>("files_trash", { paths });
export const filesPaste = (sources: string[], dest: string, cut: boolean) => invoke<string[]>("files_paste", { sources, dest, cut });
export const filesTrashList = () => invoke<TrashItem[]>("files_trash_list");
export const filesTrashRestore = (ids: string[]) => invoke<void>("files_trash_restore", { ids });
export const filesTrashDelete = (ids: string[] | null) => invoke<void>("files_trash_delete", { ids });
export const filesMount = (device: string) => invoke<string>("files_mount", { device });
export const filesUnmount = (device: string) => invoke<void>("files_unmount", { device });
export const filesInfo = (path: string) => invoke<FileInfo>("files_info", { path });
export const filesThumb = (path: string) => invoke<string | null>("files_thumb", { path });
export { convertFileSrc };

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
