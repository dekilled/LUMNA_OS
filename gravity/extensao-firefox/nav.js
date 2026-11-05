/*
 * LUMNA Mídia: navegação por setas nos sites feitos para mouse (Netflix, Prime, Disney, Max...). O controle do celular manda
 * as setas e o OK como teclas; sem isto elas só rolam a página e o OK não acha nada para clicar. Aqui as setas movem o foco
 * para o item clicável mais perto naquela direção (com um contorno violeta), o OK clica, e quando não há para onde ir a seta
 * volta a fazer o que fazia: rolar a página.
 *
 * Fica de fora do YouTube TV (que tem a navegação dele) e de um vídeo tocando em tela cheia (as setas ali são do player).
 * Só vale no perfil da Mídia, que é separado do Firefox de todo dia.
 */
"use strict";
(function () {
	if (window.__lumnaNav) {
		return;
	}
	const CLICAVEL = [
		"a[href]", "button", "input:not([type=hidden])", "select", "textarea", "summary",
		"[role=button]", "[role=link]", "[role=menuitem]", "[role=tab]", "[role=option]", "[role=checkbox]", "[role=radio]",
		"[onclick]", "[tabindex]:not([tabindex='-1'])", "[data-lumna-nav]",
	].join(",");
	const NATIVOS = new Set(["A", "BUTTON", "INPUT", "SELECT", "TEXTAREA", "SUMMARY"]);
	const DIRECOES = { ArrowUp: [0, -1], ArrowDown: [0, 1], ArrowLeft: [-1, 0], ArrowRight: [1, 0] };

	const estilo = document.createElement("style");
	estilo.textContent = "[data-lumna-foco]{outline:4px solid #8B5CF6 !important;outline-offset:3px !important;border-radius:6px;" +
		"box-shadow:0 0 0 7px rgba(139,92,246,.35) !important;}";
	(document.head || document.documentElement).appendChild(estilo);

	let atual = null;

	function visivel(el) {
		const r = el.getBoundingClientRect();
		if (r.width < 8 || r.height < 8) {
			return null;
		}
		const margem = 400; // um pouco fora da tela também vale: a rolagem leva até lá
		if (r.bottom < -margem || r.top > innerHeight + margem || r.right < -margem || r.left > innerWidth + margem) {
			return null;
		}
		const css = getComputedStyle(el);
		if (css.visibility === "hidden" || css.display === "none" || css.pointerEvents === "none" || Number(css.opacity) === 0) {
			return null;
		}
		if (el.disabled || el.getAttribute("aria-hidden") === "true" || el.closest("[inert]")) {
			return null;
		}
		return r;
	}

	function candidatos() {
		const lista = [];
		for (const el of document.querySelectorAll(CLICAVEL)) {
			const r = visivel(el);
			if (!r) {
				continue;
			}
			// o que está tapado por outra coisa (um menu por cima, um painel fechado) não conta
			const cx = Math.min(Math.max(r.left + r.width / 2, 0), innerWidth - 1);
			const cy = Math.min(Math.max(r.top + r.height / 2, 0), innerHeight - 1);
			if (r.top >= 0 && r.bottom <= innerHeight && r.left >= 0 && r.right <= innerWidth) {
				const topo = document.elementFromPoint(cx, cy);
				if (topo && topo !== el && !el.contains(topo) && !topo.contains(el)) {
					continue;
				}
			}
			// um clicável dentro de outro clicável (o ícone dentro do botão): fica o de dentro só se for ele o que se vê mais perto
			lista.push({ el, r, cx: r.left + r.width / 2, cy: r.top + r.height / 2 });
		}
		return lista;
	}

	function pontua(de, para, dx, dy) {
		// a distância no eixo da seta, mais o desvio para o lado (que pesa o dobro), e só vale o que está do lado certo
		const ax = (para.cx - de.cx) * dx + (para.cy - de.cy) * dy;
		if (ax <= 2) {
			return Infinity;
		}
		const lado = Math.abs((para.cx - de.cx) * dy) + Math.abs((para.cy - de.cy) * dx);
		const sobrepoe = dx !== 0
			? Math.min(de.r.bottom, para.r.bottom) - Math.max(de.r.top, para.r.top) > 0
			: Math.min(de.r.right, para.r.right) - Math.max(de.r.left, para.r.left) > 0;
		return ax + lado * (sobrepoe ? 1 : 3);
	}

	function marcar(el) {
		if (atual && atual !== el) {
			atual.removeAttribute("data-lumna-foco");
		}
		atual = el;
		el.setAttribute("data-lumna-foco", "");
		if (!NATIVOS.has(el.tagName) && !el.hasAttribute("tabindex")) {
			el.setAttribute("tabindex", "-1"); // um div com onclick não recebe foco sem isto
		}
		el.focus({ preventScroll: true });
		el.scrollIntoView({ block: "nearest", inline: "nearest" });
	}

	// um vídeo grande tocando: as setas são do player do site
	function videoTocando() {
		for (const v of document.querySelectorAll("video")) {
			if (!v.paused && !v.ended && v.readyState > 2) {
				const r = v.getBoundingClientRect();
				if (r.width * r.height > innerWidth * innerHeight * 0.5) {
					return true;
				}
			}
		}
		return false;
	}

	function digitando() {
		const a = document.activeElement;
		if (!a) {
			return false;
		}
		if (a.isContentEditable || a.tagName === "TEXTAREA") {
			return true;
		}
		return a.tagName === "INPUT" && !/^(button|submit|reset|checkbox|radio|range|image|file|color)$/i.test(a.type);
	}

	function tecla(e) {
		if (e.defaultPrevented || e.altKey || e.ctrlKey || e.metaKey || e.shiftKey) {
			return;
		}
		if (e.key === "Enter" && atual && atual === document.activeElement && !NATIVOS.has(atual.tagName)) {
			e.preventDefault();
			e.stopPropagation();
			atual.click();
			return;
		}
		const d = DIRECOES[e.key];
		if (!d || digitando() || videoTocando()) {
			return;
		}
		const lista = candidatos();
		if (!lista.length) {
			return; // nada clicável: a seta rola a página, como sempre
		}
		let de = null;
		const foco = document.activeElement;
		if (foco && foco !== document.body && foco !== document.documentElement) {
			de = lista.find((c) => c.el === foco || c.el.contains(foco)) || null;
			if (!de) {
				const r = foco.getBoundingClientRect();
				if (r.width && r.height) {
					de = { el: foco, r, cx: r.left + r.width / 2, cy: r.top + r.height / 2 };
				}
			}
		}
		if (!de) {
			// sem foco ainda: começa pelo item mais perto do meio da tela
			const mx = innerWidth / 2, my = innerHeight / 2;
			let melhor = null, dist = Infinity;
			for (const c of lista) {
				const x = Math.hypot(c.cx - mx, c.cy - my);
				if (x < dist) {
					dist = x;
					melhor = c;
				}
			}
			e.preventDefault();
			e.stopPropagation();
			marcar(melhor.el);
			return;
		}
		let alvo = null, nota = Infinity;
		for (const c of lista) {
			if (c.el === de.el || de.el.contains(c.el) || c.el.contains(de.el)) {
				continue;
			}
			const n = pontua(de, c, d[0], d[1]);
			if (n < nota) {
				nota = n;
				alvo = c;
			}
		}
		if (!alvo) {
			return; // fim da linha: a seta rola a página
		}
		e.preventDefault();
		e.stopPropagation();
		marcar(alvo.el);
	}

	window.addEventListener("keydown", tecla, true);
	window.__lumnaNav = { candidatos, marcar };
})();
