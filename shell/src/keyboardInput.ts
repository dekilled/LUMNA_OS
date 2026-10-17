// LUMNA Keyboard nos campos do próprio LUMNA: o teclado da tela entrega texto e teclas direto para
// esta página (evento lumna://keyboard-input), e aqui elas viram edição no campo em foco. As teclas
// também saem como eventos de teclado (keydown), para os atalhos das telas (Enter manda a mensagem
// da Emma, setas andam na lista...) funcionarem como num teclado de verdade.
import { invoke } from "@tauri-apps/api/core";
import { getCurrentWebviewWindow } from "@tauri-apps/api/webviewWindow";
import { clipboardRead, clipboardWrite, keyboardShow, on } from "./system";

type Input = { kind: "text"; text: string } | { kind: "key"; key: string; mods: string[] };

const label = getCurrentWebviewWindow().label;

const editable = (el: EventTarget | null): el is HTMLElement =>
  el instanceof HTMLTextAreaElement ||
  (el instanceof HTMLElement && el.isContentEditable) ||
  (el instanceof HTMLInputElement && !["button", "checkbox", "radio", "range", "file", "color", "submit"].includes(el.type));

// nome da tecla no xkb (o que o teclado manda) -> KeyboardEvent.key
const DOM_KEY: Record<string, string> = {
  BackSpace: "Backspace",
  Return: "Enter",
  Tab: "Tab",
  Escape: "Escape",
  Left: "ArrowLeft",
  Right: "ArrowRight",
  Up: "ArrowUp",
  Down: "ArrowDown",
  Home: "Home",
  End: "End",
  Delete: "Delete",
  space: " ",
};

// Dispara keydown/keyup no campo; devolve true se alguém (a tela) tratou a tecla.
function press(target: HTMLElement, key: string, mods: string[]) {
  const init: KeyboardEventInit = {
    key,
    bubbles: true,
    cancelable: true,
    ctrlKey: mods.includes("ctrl"),
    altKey: mods.includes("alt"),
    shiftKey: mods.includes("shift"),
    metaKey: mods.includes("logo"),
  };
  const handled = !target.dispatchEvent(new KeyboardEvent("keydown", init));
  target.dispatchEvent(new KeyboardEvent("keyup", init));
  return handled;
}

function insert(text: string) {
  // insertText passa pelo desfazer do navegador e avisa o v-model (evento input)
  if (!document.execCommand("insertText", false, text)) {
    const el = document.activeElement as HTMLInputElement | null;
    if (!el || el.selectionStart == null) return;
    el.setRangeText(text, el.selectionStart, el.selectionEnd ?? el.selectionStart, "end");
    el.dispatchEvent(new Event("input", { bubbles: true }));
  }
}

function moveCaret(el: HTMLElement, key: string) {
  const field = el as HTMLInputElement | HTMLTextAreaElement;
  if (field.selectionStart == null) return;
  const pos = field.selectionStart;
  const end = field.value.length;
  const lineStart = field.value.lastIndexOf("\n", pos - 1) + 1;
  const lineEnd = (i => (i < 0 ? end : i))(field.value.indexOf("\n", pos));
  const to =
    key === "ArrowLeft" ? Math.max(0, pos - 1)
    : key === "ArrowRight" ? Math.min(end, pos + 1)
    : key === "Home" ? (field instanceof HTMLTextAreaElement ? lineStart : 0)
    : key === "End" ? (field instanceof HTMLTextAreaElement ? lineEnd : end)
    : pos;
  field.setSelectionRange(to, to);
}

async function shortcut(el: HTMLElement, letter: string) {
  const field = el as HTMLInputElement;
  const selected = field.value?.slice(field.selectionStart ?? 0, field.selectionEnd ?? 0) ?? "";
  if (letter === "a") field.select?.();
  else if (letter === "c" && selected) await clipboardWrite(selected);
  else if (letter === "x" && selected) {
    await clipboardWrite(selected);
    document.execCommand("delete");
  } else if (letter === "v") {
    const text = await clipboardRead().catch(() => "");
    if (text) insert(text);
  } else if (letter === "z") document.execCommand("undo");
}

async function apply(input: Input) {
  const el = document.activeElement;
  if (!editable(el)) return;
  if (input.kind === "text") {
    insert(input.text);
    return;
  }
  const { key, mods } = input;
  if ((mods.includes("ctrl") || mods.includes("logo")) && /^[a-z]$/.test(key)) {
    if (!press(el, key, mods)) await shortcut(el, key);
    return;
  }
  const domKey = DOM_KEY[key] ?? key;
  if (press(el, domKey, mods)) return; // a tela tratou (ex.: Enter na Emma)
  switch (domKey) {
    case "Backspace":
      document.execCommand("delete");
      break;
    case "Delete":
      document.execCommand("forwardDelete");
      break;
    case " ":
      insert(" ");
      break;
    case "Enter":
      if (el instanceof HTMLTextAreaElement || el.isContentEditable) insert("\n");
      else (el as HTMLInputElement).form?.requestSubmit();
      break;
    case "Tab":
      break;
    default:
      moveCaret(el, domKey);
  }
}

// Campo do LUMNA ganhou foco: o teclado aparece e as teclas passam a vir para esta janela.
// Perdeu (e nenhum outro pegou): as teclas voltam para os outros apps e o teclado sai.
export function setupKeyboardInput() {
  if (label === "keyboard") return;
  let hide = 0;
  document.addEventListener("focusin", (e) => {
    if (!editable(e.target)) return;
    clearTimeout(hide);
    invoke("keyboard_target", { label, focused: true }).catch(() => {});
    keyboardShow(true).catch(() => {});
  });
  document.addEventListener("focusout", (e) => {
    if (!editable(e.target)) return;
    hide = window.setTimeout(() => {
      if (editable(document.activeElement)) return;
      invoke("keyboard_target", { label, focused: false }).catch(() => {});
      keyboardShow(false).catch(() => {});
    }, 200);
  });
  on<Input>("lumna://keyboard-input", (input) => apply(input).catch((e) => console.error(e)));
}
