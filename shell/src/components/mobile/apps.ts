// Celular: tem app aberto na frente? A navegação (dock) some e vira só a faixa da barrinha, a
// barra de status fica grafite e as bordas laterais ganham o gesto de voltar pra Início.
import { ref } from "vue";
import { emit } from "@tauri-apps/api/event";
import { isMinimized, onCompositorEvent, windowControl, windows } from "../../compositor";

export const appUp = ref(false);

async function check() {
  const list = await windows().catch(() => null);
  if (list) appUp.value = list.some((w) => !isMinimized(w));
}

let watching: Promise<() => void> | null = null;
/** Começa a acompanhar (cada camada que usa chama; o resto é ignorado). */
export function watchApps() {
  if (!watching) {
    check();
    watching = onCompositorEvent((e) => {
      if (["window_opened", "window_closed", "active_window_changed", "workspaces_changed"].includes(e.kind)) check();
    });
  }
}

/** Volta para a tela Início: minimiza o que estiver aberto. (A foto pros recentes não é tirada
 * aqui: o grim trava o compositor um instante a cada troca; ela sai ao abrir os recentes.) */
export async function showHome(tab: "inicio" | "apps" = "inicio") {
  const list = await windows().catch(() => []);
  for (const w of list) if (!isMinimized(w)) await windowControl("minimize", w.address).catch(() => {});
  appUp.value = false;
  await emit("lumna://mobile-tab", tab);
}
