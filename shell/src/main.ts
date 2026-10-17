import { createApp } from "vue";
import App from "./App.vue";
import "./styles.css";

import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import { keyboardShow } from "./system";

createApp(App).mount("#app");

// LUMNA Keyboard: campo de texto do LUMNA ganhou foco, o teclado da tela aparece; perdeu (e nenhum
// outro pegou), ele sai. No PC o backend ignora (não existe a camada do teclado).
if (getCurrentWebviewWindow().label !== "keyboard") {
  const editable = (el: EventTarget | null) =>
    el instanceof HTMLTextAreaElement ||
    (el instanceof HTMLElement && el.isContentEditable) ||
    (el instanceof HTMLInputElement && !["button", "checkbox", "radio", "range", "file", "color", "submit"].includes(el.type));
  let hide = 0;
  document.addEventListener("focusin", (e) => {
    if (!editable(e.target)) return;
    clearTimeout(hide);
    keyboardShow(true).catch(() => {});
  });
  document.addEventListener("focusout", (e) => {
    if (!editable(e.target)) return;
    hide = window.setTimeout(() => {
      if (!editable(document.activeElement)) keyboardShow(false).catch(() => {});
    }, 200);
  });
}
