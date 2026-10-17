// Gestos de arrastar do celular (abrir a central, os recentes, fechar um app...).
//
// Um arrasto conta quando passa do limite na direção pedida e anda mais nela do que na outra.
// Se virou gesto, o clique que o toque geraria é engolido (o botão embaixo do dedo não dispara).

export type Direction = "up" | "down" | "left" | "right";

export interface SwipeOptions {
  /** Chamado uma vez, quando o arrasto passa do limite. */
  on: Partial<Record<Direction, () => void>>;
  /** Pixels até virar gesto. */
  threshold?: number;
  /** Acompanha o dedo (pra animar enquanto arrasta); dx/dy desde o começo. */
  move?: (dx: number, dy: number) => void;
  /** Soltou sem virar gesto (pra voltar a animação). */
  cancel?: () => void;
  /** Toques que começam aqui dentro não viram gesto (sliders, listas que rolam...). */
  ignore?: string;
}

export function swipe(el: HTMLElement, options: SwipeOptions): () => void {
  const threshold = options.threshold ?? 40;
  let start: { x: number; y: number; id: number } | null = null;
  let fired = false;
  let swallow = false;

  const down = (e: PointerEvent) => {
    if (start) return;
    if (options.ignore && (e.target as Element).closest?.(options.ignore)) return;
    start = { x: e.clientX, y: e.clientY, id: e.pointerId };
    fired = false;
  };
  const move = (e: PointerEvent) => {
    if (!start || e.pointerId !== start.id) return;
    const dx = e.clientX - start.x;
    const dy = e.clientY - start.y;
    options.move?.(dx, dy);
    if (fired) return;
    const dir: Direction | null =
      Math.abs(dy) >= Math.abs(dx)
        ? dy <= -threshold ? "up" : dy >= threshold ? "down" : null
        : dx <= -threshold ? "left" : dx >= threshold ? "right" : null;
    if (dir && options.on[dir]) {
      fired = true;
      swallow = true;
      try {
        el.setPointerCapture(e.pointerId);
      } catch {}
      options.on[dir]!();
    }
  };
  const up = (e: PointerEvent) => {
    if (!start || e.pointerId !== start.id) return;
    if (!fired) options.cancel?.();
    start = null;
    // o clique vem logo depois do pointerup
    if (swallow) window.setTimeout(() => (swallow = false), 350);
  };
  const click = (e: MouseEvent) => {
    if (swallow) {
      e.stopPropagation();
      e.preventDefault();
      swallow = false;
    }
  };
  el.addEventListener("pointerdown", down);
  el.addEventListener("pointermove", move);
  el.addEventListener("pointerup", up);
  el.addEventListener("pointercancel", up);
  el.addEventListener("click", click, true);
  return () => {
    el.removeEventListener("pointerdown", down);
    el.removeEventListener("pointermove", move);
    el.removeEventListener("pointerup", up);
    el.removeEventListener("pointercancel", up);
    el.removeEventListener("click", click, true);
  };
}
