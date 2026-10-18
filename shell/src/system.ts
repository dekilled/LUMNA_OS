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
  /** Chegou no "não perturbe": vai só pra central do celular. */
  quiet: boolean;
  /** Avisos do próprio shell: o que abrir ao tocar (ex.: o controle do Mídia). */
  open?: string;
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

export type SettingsPage = "rede" | "dados-moveis" | "bluetooth" | "celular" | "som" | "impressoras" | "telas" | "teclado" | "energia" | "papel-de-parede" | "dock" | "sistema";
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

// ---- Instalador e tela de login ----

export interface Disk {
  path: string;
  model: string;
  size: number;
  transport: string;
  contents: string;
}
export interface InstallPlan {
  disk: string;
  user: string;
  name: string;
  hostname: string;
  timezone: string;
  keyboard: string;
  locale: string;
}
export const installerDisks = () => invoke<Disk[]>("installer_disks");
export const installerTimezones = () => invoke<string[]>("installer_timezones");
export const installerRun = (plan: InstallPlan, password: string) => invoke<void>("installer_run", { plan, password });
export const installerReboot = () => invoke<void>("installer_reboot");

export interface GreeterUser {
  username: string;
  name: string;
}
export interface GreeterSession {
  id: string;
  name: string;
}
export const greeterUsers = () => invoke<GreeterUser[]>("greeter_users");
export const greeterSessions = () => invoke<GreeterSession[]>("greeter_sessions");
export const greeterLogin = (username: string, password: string, session: string | null) =>
  invoke<void>("greeter_login", { username, password, session });
export const greeterPower = (action: "poweroff" | "reboot" | "suspend") => invoke<void>("greeter_power", { action });

/** Ajusta a camada desta janela ao tamanho do conteúdo. */
/** Celular: liga/desliga as faixas das bordas (gesto de voltar pra Início). */
export const mobileEdges = (show: boolean) => invoke<void>("mobile_edges", { show });
export const layerResize = (width: number, height: number) =>
  invoke<void>("layer_resize", { width: Math.ceil(width), height: Math.ceil(height) });
export const layerHide = () => invoke<void>("layer_hide");

export const notificationClosed = (id: number, reason: 1 | 2) => invoke<void>("notification_closed", { id, reason });
export const notificationAction = (id: number, key: string) => invoke<void>("notification_action", { id, key });
export const notificationOpen = (target: string) => invoke<void>("notification_open", { target });

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

/** Página de um serviço guardada ao sair dela (Início ou troca de serviço). */
export interface Recent {
  id: string;
  service: string;
  title: string;
  url: string;
  thumb: string | null;
  /** Quando saiu da página (segundos desde 1970). */
  at: number;
}

export interface TvState {
  /** Serviço na frente neste monitor. */
  playing: string | null;
  /** Serviços vivos (na frente ou estacionados) neste monitor. */
  live: string[];
  recents: Recent[];
}

export const tvState = (monitor: string | null) => invoke<TvState>("tv_state", { monitor });
export const tvOpen = (monitor: string | null, service: string, url: string | null = null) =>
  invoke<void>("tv_open", { monitor, service, url });
export const tvResume = (monitor: string | null, id: string) => invoke<void>("tv_resume", { monitor, id });
export const tvHome = (monitor: string | null, focus: string | null = null) => invoke<void>("tv_home", { monitor, focus });
export const tvForget = (monitor: string | null, id: string) => invoke<void>("tv_forget", { monitor, id });
export const tvbarClose = () => invoke<void>("tvbar_close");

/** "há 5 min", "há 2 h", "ontem"... */
export function timeAgo(seconds: number): string {
  const diff = Math.max(0, Date.now() / 1000 - seconds);
  if (diff < 60) return "agora";
  if (diff < 3600) return `há ${Math.floor(diff / 60)} min`;
  if (diff < 86400) return `há ${Math.floor(diff / 3600)} h`;
  if (diff < 172800) return "ontem";
  return `há ${Math.floor(diff / 86400)} dias`;
}
export const remotePairing = () => invoke<Pairing>("remote_pairing");
export const remoteStopPairing = () => invoke<void>("remote_stop_pairing");
export const remoteForget = () => invoke<void>("remote_forget");

// ---- Emma ----

export interface EmmaStatus {
  url: string;
  email: string;
  /** Já tem como entrar (token salvo ou conta local do pacote lumna-emma). */
  configured: boolean;
  online: boolean;
}

export interface EmmaTool {
  id: string;
  name: string;
  args: Record<string, unknown>;
  status: string;
  summary: string;
}

/** Eventos da conversa (`lumna://emma`): os do SSE do backend e `tool_ran` (a ação rodou aqui). */
export type EmmaEvent =
  | { type: "meta" }
  | { type: "delta"; content: string }
  | { type: "rewrite"; content: string }
  | { type: "tool_start"; tool: EmmaTool }
  | { type: "tool_confirm"; tool: EmmaTool; label: string; reason: string }
  | { type: "tool_client"; tool: EmmaTool }
  | { type: "tool_ran"; id: string; ok: boolean; summary: string }
  | { type: "tool_end"; tool: EmmaTool }
  | { type: "done"; message: { content: string } }
  | { type: "error"; message: string };

export interface EmmaMessage {
  role: "user" | "assistant";
  content: string;
  tools?: EmmaTool[] | null;
}

export type ActionRisk = "read" | "act" | "danger";
export type ActionPermission = "auto" | "ask" | "off";

export interface ActionInfo {
  name: string;
  label: string;
  description: string;
  risk: ActionRisk;
  permission: ActionPermission;
}

export const toggleEmma = () => invoke<void>("toggle_emma");
export const emmaStatus = () => invoke<EmmaStatus>("emma_status");
export const emmaLogin = (url: string, email: string, password: string) =>
  invoke<void>("emma_login", { url, email, password });
export const emmaLogout = () => invoke<void>("emma_logout");
export const emmaSend = (text: string, voice = false) => invoke<void>("emma_send", { text, voice });
export const emmaDecide = (callId: string, allow: boolean) => invoke<void>("emma_decide", { callId, allow });
export const emmaNewConversation = () => invoke<void>("emma_new_conversation");
export const emmaHistory = () => invoke<EmmaMessage[]>("emma_history");
export interface ProviderPreset {
  preset: string;
  name: string;
  base_url: string;
  models: string[];
}

export const emmaProviders = () =>
  invoke<{ presets: ProviderPreset[]; current: { name: string; model: string; base_url: string } | null }>(
    "emma_providers",
  );
export const emmaSetProvider = (name: string, baseUrl: string, apiKey: string, model: string) =>
  invoke<void>("emma_set_provider", { name, baseUrl, apiKey, model });
export const emmaPermissions = () => invoke<ActionInfo[]>("emma_permissions");
export const emmaSetPermission = (name: string, permission: ActionPermission) =>
  invoke<void>("emma_set_permission", { name, permission });

// ---- Automação (macros e gatilhos) ----

export type MacroStep = { action: string; args?: Record<string, unknown> } | { emma: string } | { wait: number };

export interface Macro {
  name: string;
  description: string;
  steps: MacroStep[];
}

export interface Automation {
  id: string;
  name: string;
  enabled: boolean;
  when: { type: string; [key: string]: unknown };
  macro: string;
  /** "Dias úteis às 08:00". */
  description: string;
}

export interface AutomationRun {
  at: string;
  by: string;
  macro: string;
  ok: boolean;
  summary: string;
}

export interface ActionOutcome {
  ok: boolean;
  summary: string;
  content: string;
}

export const automationOverview = () =>
  invoke<{ macros: Macro[]; automations: Automation[]; history: AutomationRun[] }>("automation_overview");
export const automationSetEnabled = (id: string, enabled: boolean) =>
  invoke<void>("automation_set_enabled", { id, enabled });
export const automationRunMacro = (name: string) => invoke<ActionOutcome>("automation_run_macro", { name });

// ---- Área de transferência (menu de texto do LUMNA) ----

export const clipboardRead = () => invoke<string>("clipboard_read");
export const clipboardWrite = (text: string) => invoke<void>("clipboard_write", { text });

// ---- Desktop (fundo) ----

export const desktopWallpaper = () => invoke<string | null>("desktop_wallpaper");

// ---- Papel de parede ----

export interface Wallpaper {
  path: string;
  name: string;
  /** Adicionada pela pessoa (pode sair da lista). */
  user: boolean;
}

/** Parado, zoom lento ou trocando entre as imagens da grade. */
export type WallpaperMotion = "none" | "zoom" | "slideshow";
export const wallpaperList = () =>
  invoke<{
    current: string;
    lock: string | null;
    desktop_motion: WallpaperMotion;
    lock_motion: WallpaperMotion;
    items: Wallpaper[];
  }>("wallpaper_list");
export const wallpaperMotionSet = (target: "desktop" | "lock", motion: WallpaperMotion) =>
  invoke<void>("wallpaper_motion_set", { target, motion });
export const restOff = () => invoke<void>("rest_off");
/** Fundo só da tela de bloqueio; `null` = o mesmo do desktop. */
export const wallpaperSetLock = (path: string | null) => invoke<void>("wallpaper_set_lock", { path });
export const wallpaperSet = (path: string) => invoke<void>("wallpaper_set", { path });
export const wallpaperAdd = () => invoke<string[]>("wallpaper_add");
export const wallpaperRemove = (path: string) => invoke<void>("wallpaper_remove", { path });

// ---- Dock ----

export type DockBehavior = "always" | "smart" | "expand";
export type DockPosition = "bottom" | "left" | "right";
/** Normal, escondido na borda (volta com o mouse) ou barra de largura inteira. */
export type DockLook = "normal" | "hidden" | "expanded";

export interface DockConfig {
  /** Ids de apps, "|" (separador), "lumna:launcher" e "lumna:emma", na ordem. */
  pinned: string[];
  size: number;
  spacing: number;
  /** Opacidade do fundo, 0 a 1. */
  opacity: number;
  behavior: DockBehavior;
  position: DockPosition;
  /** "primary", "all" ou o nome de uma saída. */
  monitors: string;
}

export const DOCK_SEPARATOR = "|";
export const DOCK_LAUNCHER = "lumna:launcher";
export const DOCK_EMMA = "lumna:emma";

export const dockState = () => invoke<{ look: DockLook; config: DockConfig }>("dock_state");
export const dockConfig = () => invoke<DockConfig>("dock_config");
export const dockConfigSet = (config: DockConfig) => invoke<DockConfig>("dock_config_set", { config });
export const dockConfigReset = () => invoke<DockConfig>("dock_config_reset");
export const dockPin = (id: string, pinned: boolean) => invoke<DockConfig>("dock_pin", { id, pinned });
export const dockMonitors = () => invoke<string[]>("dock_monitors");

// ---- Telas ----

export type DisplayAlign = "top" | "center" | "bottom";
export interface Displays {
  /** Da esquerda para a direita. */
  monitors: import("./compositor").Monitor[];
  align: DisplayAlign;
}

export const displays = () => invoke<Displays>("displays");
export const displaysArrange = (order: string[], align: DisplayAlign) =>
  invoke<Displays>("displays_arrange", { order, align });

// ---- Teclado e idioma ----

export interface KeyboardLayout {
  layout: string;
  /** Vazio = a variante padrão. */
  variant: string;
}
export interface XkbLayout {
  id: string;
  name: string;
  variants: { id: string; name: string }[];
}
export interface KeyboardState {
  layouts: KeyboardLayout[];
  catalog: XkbLayout[];
  locale: string;
  locales: { code: string; name: string }[];
}

export const keyboardState = () => invoke<KeyboardState>("keyboard_state");
export const keyboardSet = (layouts: KeyboardLayout[]) => invoke<void>("keyboard_set", { layouts });
export const localeSet = (code: string) => invoke<void>("locale_set", { code });

// ---- Energia ----

export type PowerAction = "lock" | "logout" | "suspend" | "hibernate" | "reboot" | "poweroff";
export interface PowerState {
  can_suspend: boolean;
  can_hibernate: boolean;
  can_reboot: boolean;
  can_power_off: boolean;
  /** `power-saver`, `balanced` ou `performance`; `null` sem o power-profiles-daemon. */
  profile: string | null;
  profiles: string[];
}

export const powerState = () => invoke<PowerState>("power_state");
export const powerSetProfile = (profile: string) => invoke<void>("power_set_profile", { profile });
export const powerAction = (action: PowerAction) => invoke<void>("power_action", { action });
export interface IdleConfig {
  /** Minutos parado (0 = nunca). */
  rest_after: number;
  lock_after: number;
  screen_off_after: number;
  suspend_after: number;
  lock_on_suspend: boolean;
}
export const idleConfig = () => invoke<IdleConfig>("idle_config");
export const idleConfigSet = (config: IdleConfig) => invoke<IdleConfig>("idle_config_set", { config });
export const lockScreen = () => invoke<void>("lock_screen");

/** Abre ou fecha o menu de energia (no monitor desta janela). */
export const powerMenu = () => invoke<void>("power_menu");

// ---- Bandeja do sistema ----

export interface TrayItem {
  key: string;
  title: string;
  /** Data URL. */
  icon: string | null;
  /** `Active`, `Passive` (escondido) ou `NeedsAttention`. */
  status: string;
  menu: boolean;
  item_is_menu: boolean;
}
export interface TrayMenuNode {
  id: number;
  label: string;
  enabled: boolean;
  separator: boolean;
  toggle: string | null;
  checked: boolean;
  children: TrayMenuNode[];
}

export const trayItems = () => invoke<TrayItem[]>("tray_items");
export const trayActivate = (key: string, x: number, y: number, secondary = false) =>
  invoke<void>("tray_activate", { key, x, y, secondary });
export const trayMenuOpen = (key: string, x: number) => invoke<void>("tray_menu_open", { key, x });
export const trayMenuClick = (key: string, id: number) => invoke<void>("tray_menu_click", { key, id });

// ---- LUMNA Keyboard (teclado na tela do celular) ----

export const keyboardType = (text: string) => invoke<void>("keyboard_type", { text });
export const keyboardKey = (key: string, mods: string[] = []) => invoke<void>("keyboard_key", { key, mods });
export const keyboardShow = (show: boolean) => invoke<void>("keyboard_show", { show });
export const keyboardToggle = () => invoke<void>("keyboard_toggle");
export const haptic = (ms: number) => invoke<void>("haptic", { ms });
export interface HapticsConfig {
  touch: boolean;
  ring: boolean;
}
export const hapticsGet = () => invoke<HapticsConfig>("haptics_get");
export const hapticsSet = (config: HapticsConfig) => invoke<void>("haptics_set", { config });

/** Modo desenvolvedor: as animações do Hyprland (velocidade em décimos de segundo). */
export interface Animations {
  enabled: boolean;
  windows: number;
  windows_style: "popin" | "slide" | "gnomed";
  layers: number;
  fade: number;
  workspaces: number;
  curve: "lumna" | "rapida" | "mola";
  switch_style: "slidefade" | "slide" | "slidevert" | "fade";
}
export const animationsGet = () => invoke<Animations>("animations_get");
export const animationsSet = (config: Animations) => invoke<void>("animations_set", { config });
export const animationsReset = () => invoke<Animations>("animations_reset");

// ---- celular: central e apps recentes ----

export type MobilePanel = "mobile-center" | "mobile-recents";
/** Sem `show`, alterna. */
export const mobilePanel = (name: MobilePanel, show?: boolean) => invoke<void>("mobile_panel", { name, show: show ?? null });

export interface Recent {
  address: string;
  class: string;
  title: string;
  pid: number;
  /** KiB do app e dos processos filhos; `null` = janela do próprio LUMNA. */
  memory_kb: number | null;
  shot: string | null;
  shot_at: number;
  focus_history: number;
}
export interface Recents {
  apps: Recent[];
  total_kb: number;
  available_kb: number;
}
export const recents = () => invoke<Recents>("recents");
/** Fotografa as janelas visíveis antes de elas saírem da tela (pra aparecerem nos recentes). */
export const recentsSnapshot = () => invoke<void>("recents_snapshot").catch(() => {});
export const recentsKill = (address: string) => invoke<void>("recents_kill", { address });

export interface Quick {
  brightness: number | null;
  flashlight: boolean | null;
  mobile_data: boolean | null;
  mobile_tech: string | null;
  airplane: boolean;
  rotation_sensor: boolean;
  quiet: boolean;
}
export const quickState = () => invoke<Quick>("quick_state");
export const quickBrightness = (percent: number) => invoke<void>("quick_brightness", { percent });
export const quickFlashlight = (on: boolean) => invoke<void>("quick_flashlight", { on });
export const quickMobileData = (on: boolean) => invoke<void>("quick_mobile_data", { on });
export const quickAirplane = (on: boolean) => invoke<void>("quick_airplane", { on });
export const quickQuiet = (on: boolean) => invoke<void>("quick_quiet", { on });

// ---- celular: telefone, contatos e dados móveis ----

export interface Call {
  path: string;
  number: string;
  state: "dialing" | "ringing-out" | "incoming" | "active" | "held" | "waiting" | "ended" | "unknown";
  incoming: boolean;
  active_since: number | null;
}
export interface CallLogEntry {
  number: string;
  incoming: boolean;
  missed: boolean;
  at: number;
  duration: number;
}
export interface ContactPhone {
  label: string;
  number: string;
}
export interface Contact {
  id: string;
  first: string;
  last: string;
  phones: ContactPhone[];
  email: string;
  company: string;
  address: string;
  birthday: string;
  favorite: boolean;
}
export type PhoneTab = "teclado" | "recentes" | "contatos";
export const phoneOpen = (tab?: PhoneTab) => invoke<void>("phone_open", { tab: tab ?? null });
export const phoneCalls = () => invoke<Call[]>("phone_calls");
export const phoneCall = (number: string) => invoke<void>("phone_call", { number });
export const phoneAccept = (path: string) => invoke<void>("phone_accept", { path });
export const phoneHangup = (path?: string) => invoke<void>("phone_hangup", { path: path ?? null });
export const phoneDtmf = (path: string, tone: string) => invoke<void>("phone_dtmf", { path, tone });
export const phoneMute = (on: boolean) => invoke<void>("phone_mute", { on });
export const phoneLog = () => invoke<CallLogEntry[]>("phone_log");
export const phoneLogClear = () => invoke<void>("phone_log_clear");
export const contactsList = () => invoke<Contact[]>("contacts_list");
export const contactsSave = (contact: Contact) => invoke<Contact>("contacts_save", { contact });
export const contactsDelete = (id: string) => invoke<void>("contacts_delete", { id });

/** Só os dígitos (e o + do começo). */
export const digits = (n: string) => n.replace(/[^\d+]/g, "");
/** Mesmo número? Compara os últimos 8 dígitos (com ou sem +55, DDD, 9 na frente...). */
export const sameNumber = (a: string, b: string) => {
  const x = a.replace(/\D/g, "");
  const y = b.replace(/\D/g, "");
  return x.length >= 8 && y.length >= 8 && x.slice(-8) === y.slice(-8);
};
export const contactName = (c: Contact) => `${c.first} ${c.last}`.trim() || c.phones[0]?.number || "Sem nome";
export const contactFor = (list: Contact[], number: string) =>
  list.find((c) => c.phones.some((p) => sameNumber(p.number, number)));
/** Número brasileiro bonito: +55 (11) 98765-4321. Outros ficam como vieram. */
export function formatNumber(n: string): string {
  let d = n.replace(/\D/g, "");
  // a operadora manda o número nacional com o 0 na frente (038 99171-7439): sai o 0
  if (!n.startsWith("+") && d.startsWith("0") && (d.length === 11 || d.length === 12)) d = d.slice(1);
  const br = d.startsWith("55") && d.length >= 12 ? d.slice(2) : n.startsWith("+") ? null : d;
  if (br && (br.length === 10 || br.length === 11)) {
    const ddd = br.slice(0, 2);
    const rest = br.slice(2);
    const cut = rest.length - 4;
    return `${n.startsWith("+") ? "+55 " : ""}(${ddd}) ${rest.slice(0, cut)}-${rest.slice(cut)}`;
  }
  return n;
}

export interface ModeOption {
  id: string;
  label: string;
}
export interface MobileData {
  modem: boolean;
  operator: string;
  state: string;
  tech: string | null;
  signal: number | null;
  number: string;
  connection: { name: string; apn: string; roaming: boolean; active: boolean } | null;
  modes: ModeOption[];
  mode: string;
  usage: number;
  month: string;
}
export const mobileDataInfo = () => invoke<MobileData>("mobile_data_info");
export const mobileDataSet = (key: "apn" | "roaming" | "modes", value: string) => invoke<void>("mobile_data_set", { key, value });

// ---- celular: Ajustes ----

export interface WifiDetails {
  ssid: string;
  signal: number;
  frequency: string;
  security: string;
  rate: string;
  ipv4: string;
  gateway: string;
  dns: string[];
  mac: string;
}
export const wifiAdd = (ssid: string, password: string, hidden: boolean) => invoke<void>("wifi_add", { ssid, password, hidden });
export const wifiDetails = () => invoke<WifiDetails | null>("wifi_details");
/** Fotos das pastas comuns (Imagens, Downloads, DCIM...), as mais novas primeiro. */
export const wallpaperGallery = () => invoke<string[]>("wallpaper_gallery");
/** Copia uma foto da galeria pra pasta de papéis de parede; devolve o caminho novo. */
export const wallpaperImport = (path: string) => invoke<string>("wallpaper_import", { path });

export interface Battery {
  percent: number | null;
  status: string;
  health: number | null;
  temperature: number | null;
  voltage: number | null;
  power: number | null;
  cycles: number | null;
  technology: string;
}
export const batteryInfo = () => invoke<Battery>("battery_info");

export interface Profile {
  name: string;
  user: string;
  host: string;
  photo: string | null;
}
export const profileGet = () => invoke<Profile>("profile_get");
export const profileSetName = (name: string) => invoke<void>("profile_set_name", { name });
export const profileSetPhoto = (path: string) => invoke<void>("profile_set_photo", { path });

// ---- celular: tela de bloqueio e botão power ----

export interface LockConfig {
  enabled: boolean;
  /** segundos parado até apagar a tela e bloquear (0 = nunca) */
  screen_off_after: number;
}
export interface LockState {
  locked: boolean;
  screen_on: boolean;
  config: LockConfig;
}
export const lockState = () => invoke<LockState>("lock_state");
export const lockUnlock = (password: string) => invoke<void>("lock_unlock", { password });
export const lockNow = () => invoke<void>("lock_now");
export const lockConfigSet = (config: LockConfig) => invoke<LockConfig>("lock_config_set", { config });
export const powerMenuClose = () => invoke<void>("power_menu_close");

// ---- celular: Connect (o PC pelo celular) ----

/** O que um aparelho pareado pode fazer (cada lado guarda os seus). */
export interface ConnectFeatures {
  notifications: boolean;
  phone_notifications: boolean;
  clipboard: boolean;
  files: boolean;
  control: boolean;
}
export interface Computer {
  id: string;
  name: string;
  host: string;
  port: number;
  online: boolean;
  features: ConnectFeatures;
}
export interface FoundComputer {
  name: string;
  host: string;
  port: number;
}
export interface ConnectAsk {
  kind: "auth" | "emma";
  computer: string;
  computer_name: string;
  cookie?: string;
  message?: string;
  user?: string;
  error?: string | null;
  call_id?: string;
  label?: string;
  reason?: string;
}
export const connectComputers = () => invoke<Computer[]>("connect_computers");
export const connectDiscover = () => invoke<FoundComputer[]>("connect_discover");
export const connectPair = (host: string, code: string, port?: number) =>
  invoke<Computer>("connect_pair", { host, code, port: port ?? null });
export const connectForget = (id: string) => invoke<void>("connect_forget", { id });
// eslint-disable-next-line @typescript-eslint/no-explicit-any
export const connectSend = (id: string, message: Record<string, unknown>) => invoke<any>("connect_send", { id, message });
export const connectAskClose = () => invoke<void>("connect_ask_close");
export const connectSetFeatures = (id: string, features: ConnectFeatures) => invoke<void>("connect_set_features", { id, features });
export const connectSetAddress = (id: string, host: string, port?: number) => invoke<void>("connect_set_address", { id, host, port });
export const connectSendFiles = (id: string, paths: string[]) => invoke<void>("connect_send_files", { id, paths });
export const connectSendClipboard = (id: string) => invoke<void>("connect_send_clipboard", { id });

export interface ConnectTarget {
  id: string;
  name: string;
  kind: "pc" | "phone";
}
/** Pra quem dá pra mandar arquivos agora (PCs, se este é o celular; celulares, se é o PC). */
export const connectTargets = () => invoke<ConnectTarget[]>("connect_targets");
export const connectShare = (target: ConnectTarget, paths: string[]) => invoke<void>("connect_share", { kind: target.kind, id: target.id, paths });

export interface ConnectSettings {
  enabled: boolean;
  media_notify: boolean;
  receive_dir: string;
  default_dir: string;
}
export const connectSettings = () => invoke<ConnectSettings>("connect_settings");
export const connectSettingsSet = (change: { enabled?: boolean; receiveDir?: string; mediaNotify?: boolean }) =>
  invoke<ConnectSettings>("connect_settings_set", { enabled: change.enabled, receiveDir: change.receiveDir, mediaNotify: change.mediaNotify });
/** Celular: os monitores de cada PC que estão no modo Mídia (por id do PC). */
export const connectMedia = () => invoke<Record<string, string[]>>("connect_media");
/** Celular: abre o LUMNA Controle (o controle do modo Mídia), de um PC ou do primeiro conectado. */
export const remoteOpen = (pc?: string) => invoke<void>("remote_open", { pc });
export interface PcMonitor {
  name: string;
  label: string;
  width: number;
  height: number;
  focused: boolean;
  media: boolean;
}

export interface Transfer {
  id: string;
  name: string;
  peer: string;
  direction: "in" | "out";
  done: number;
  size: number;
  state: "running" | "done" | "error";
  error: string | null;
  path: string | null;
}
export const connectTransfers = () => invoke<Transfer[]>("connect_transfers");

export interface RecentFile {
  path: string;
  name: string;
  size: number;
  modified: number;
}
export const connectRecentFiles = () => invoke<RecentFile[]>("connect_recent_files");

/** No PC: os celulares pareados. */
export interface RemoteDevice {
  id: string;
  name: string;
  online: boolean;
  features: ConnectFeatures;
}
export const remoteDevices = () => invoke<RemoteDevice[]>("remote_devices");
export const remoteDeviceSet = (id: string, features: ConnectFeatures) => invoke<void>("remote_device_set", { id, features });
export const remoteDeviceForget = (id: string) => invoke<void>("remote_device_forget", { id });
export const remoteSendClipboard = (id: string) => invoke<void>("remote_send_clipboard", { id });

/** Agendamentos do PC. */
export type ScheduleAction = "poweroff" | "reboot" | "suspend" | "lock" | "remind" | "emma";
export interface ScheduleTask {
  id: string;
  at: number;
  action: ScheduleAction;
  text: string;
  daily: boolean;
  by: string;
}
export const scheduleList = () => invoke<ScheduleTask[]>("schedule_list");
export const scheduleAdd = (task: Omit<ScheduleTask, "id" | "by">) => invoke<ScheduleTask>("schedule_add", { task });
export const scheduleRemove = (id: string) => invoke<void>("schedule_remove", { id });
