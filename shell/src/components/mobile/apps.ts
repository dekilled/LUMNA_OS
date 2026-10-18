// Celular: tem app aberto na frente? A navegação (dock) some e vira só a faixa da barrinha, a
// barra de status fica grafite e as bordas laterais ganham o gesto de voltar pra Início.
import { ref } from "vue";
import { emit } from "@tauri-apps/api/event";
import { invoke } from "@tauri-apps/api/core";
import { isMinimized, monitors, onCompositorEvent, windows } from "../../compositor";

export const appUp = ref(false);

// app "na tela" = não minimizado e na área de trabalho em uso (na troca animada, o que saiu
// ainda não foi guardado por um instante)
async function check() {
  const [list, mons] = await Promise.all([windows().catch(() => null), monitors().catch(() => null)]);
  if (!list) return;
  const active = mons?.find((m) => m.focused)?.active_workspace;
  appUp.value = list.some((w) => !isMinimized(w) && (active == null || w.workspace === active));
}

/** Mostra um app (ou a Início, sem endereço) com a animação de troca de área. */
export const showApp = (address?: string) => invoke<void>("mobile_show", { address: address ?? null });

let watching: Promise<() => void> | null = null;
/** Começa a acompanhar (cada camada que usa chama; o resto é ignorado). */
export function watchApps() {
  if (!watching) {
    check();
    watching = onCompositorEvent((e) => {
      if (["window_opened", "window_closed", "active_window_changed", "workspaces_changed", "workspace_focused"].includes(e.kind)) check();
    });
  }
}

/** Volta para a tela Início: minimiza o que estiver aberto. (A foto pros recentes não é tirada
 * aqui: o grim trava o compositor um instante a cada troca; ela sai ao abrir os recentes.) */
export async function showHome(tab: "inicio" | "apps" = "inicio") {
  appUp.value = false;
  await showApp().catch(() => {});
  await emit("lumna://mobile-tab", tab);
}
