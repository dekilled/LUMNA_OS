/*
 * LUMNA Mídia: o lado do Firefox. Conversa com o serviço do LUMNA (lumna-midia servico) por um WebSocket
 * só em 127.0.0.1 e faz o que ele pede com as abas: abrir o serviço, trazer, fechar e tirar a
 * miniatura. Avisa o que muda (endereço, título, se está carregando).
 *
 * Isso no lugar do WebDriver BiDi: com ele ligado o Firefox se anuncia aos sites como automatizado
 * (navigator.webdriver) e o Google recusa o login. Uma extensão não liga essa marca.
 *
 * Mensagens, uma por quadro, em JSON:
 *   daqui:  {"evento":"ola","versao":"1.0.0"}
 *           {"evento":"aba","servico":"netflix","tab":7,"url":"...","titulo":"...","carregando":true}
 *           {"evento":"fechada","servico":"netflix","tab":7}
 *           {"id":3,"ok":true,"dados":"data:image/jpeg;base64,..."}      resposta a um pedido
 *   de lá:  {"id":3,"cmd":"abrir","servico":"netflix","url":"https://..."}
 *           {"id":4,"cmd":"ativar"|"fechar"|"capturar","servico":"netflix"}
 *           {"id":6,"cmd":"ir","servico":"netflix","url":"https://..."}   leva a aba que já existe a outro endereço
 *           {"id":5,"cmd":"estado"}
 *           {"id":7,"cmd":"pausar","servico":"netflix"}   pausa todo vídeo e áudio da aba (sair do serviço não o deixa tocando por trás)
 *           {"id":8,"cmd":"voltar","servico":"netflix"}   volta uma página; {"ok":false,"erro":"sem histórico"} na primeira
 */
"use strict";

const PORTA = 47822;
const abas = new Map(); // id da aba -> serviço
let ws = null;
let religar = null;

function enviar(objeto) {
	if (ws && ws.readyState === WebSocket.OPEN) {
		ws.send(JSON.stringify(objeto));
	}
}

function descrever(aba, servico) {
	return {
		evento: "aba",
		servico,
		tab: aba.id,
		url: aba.url || "",
		titulo: aba.title || "",
		carregando: aba.status === "loading",
	};
}

async function abaDoServico(servico) {
	for (const [id, s] of abas) {
		if (s !== servico) {
			continue;
		}
		try {
			return await browser.tabs.get(id);
		} catch (e) {
			abas.delete(id); // fechada por fora
		}
	}
	return null;
}

async function executar(c) {
	switch (c.cmd) {
	case "abrir": {
		const existente = await abaDoServico(c.servico);
		if (existente) {
			await browser.tabs.update(existente.id, { active: true }); // já está viva: só vem para a frente
			return { ok: true };
		}
		// a aba em branco do começo é reaproveitada, para não sobrar uma vazia
		const todas = await browser.tabs.query({});
		const vazia = todas.find((t) => !abas.has(t.id) && ["about:blank", "about:newtab", "about:home"].includes(t.url));
		const aba = vazia
			? await browser.tabs.update(vazia.id, { url: c.url, active: true })
			: await browser.tabs.create({ url: c.url, active: true });
		abas.set(aba.id, c.servico);
		enviar(descrever(aba, c.servico));
		return { ok: true };
	}
	case "ir": {
		// a aba já existe e é para levá-la a outro endereço (a página de entrada de uma conta)
		const aba = await abaDoServico(c.servico);
		if (!aba) {
			return { ok: false, erro: "sem aba" };
		}
		await browser.tabs.update(aba.id, { url: c.url, active: true });
		return { ok: true };
	}
	case "ativar": {
		const aba = await abaDoServico(c.servico);
		if (!aba) {
			return { ok: false, erro: "sem aba" };
		}
		await browser.tabs.update(aba.id, { active: true });
		return { ok: true };
	}
	case "fechar": {
		const aba = await abaDoServico(c.servico);
		if (aba) {
			await browser.tabs.remove(aba.id);
		}
		return { ok: true };
	}
	case "pausar": {
		const aba = await abaDoServico(c.servico);
		if (!aba) {
			return { ok: false, erro: "sem aba" };
		}
		// todo <video> e <audio> da aba, também nos quadros de dentro (os players costumam morar num iframe)
		await browser.tabs.executeScript(aba.id, {
			allFrames: true,
			matchAboutBlank: true,
			code: "document.querySelectorAll('video,audio').forEach(function (m) { try { m.pause(); } catch (e) {} });",
		}).catch(() => []);
		return { ok: true };
	}
	case "voltar": {
		const aba = await abaDoServico(c.servico);
		if (!aba) {
			return { ok: false, erro: "sem aba" };
		}
		const antes = aba.url || "";
		try {
			await browser.tabs.goBack(aba.id);
		} catch (e) {
			return { ok: false, erro: "sem histórico" };
		}
		// sem histórico o goBack não dá erro: só não faz nada. Voltou se o endereço mudou (espera um pouco a navegação)
		let depois = antes;
		for (let i = 0; i < 10 && depois === antes; i++) {
			await new Promise((r) => setTimeout(r, 100));
			depois = (await browser.tabs.get(aba.id)).url || "";
		}
		if (depois === antes) {
			return { ok: false, erro: "sem histórico" };
		}
		// a aba em branco do começo é a primeira entrada do histórico: voltar até ela é não ter para onde voltar
		if (["", "about:blank", "about:newtab", "about:home"].includes(depois)) {
			await browser.tabs.goForward(aba.id).catch(() => {});
			return { ok: false, erro: "sem histórico" };
		}
		return { ok: true };
	}
	case "capturar": {
		const aba = await abaDoServico(c.servico);
		if (!aba) {
			return { ok: false, erro: "sem aba" };
		}
		const dados = await browser.tabs.captureTab(aba.id, { format: "jpeg", quality: 60 });
		return { ok: true, dados };
	}
	case "estado": {
		for (const [id, servico] of abas) {
			try {
				enviar(descrever(await browser.tabs.get(id), servico));
			} catch (e) {
				abas.delete(id);
			}
		}
		return { ok: true };
	}
	default:
		return { ok: false, erro: "comando desconhecido" };
	}
}

function conectar() {
	clearTimeout(religar);
	try {
		ws = new WebSocket("ws://127.0.0.1:" + PORTA + "/extensao");
	} catch (e) {
		religar = setTimeout(conectar, 1000);
		return;
	}
	ws.onopen = () => enviar({ evento: "ola", versao: browser.runtime.getManifest().version });
	ws.onmessage = async (m) => {
		let c;
		try {
			c = JSON.parse(m.data);
		} catch (e) {
			return;
		}
		let r;
		try {
			r = await executar(c);
		} catch (e) {
			r = { ok: false, erro: String(e) };
		}
		if (c.id !== undefined) {
			enviar(Object.assign({ id: c.id }, r));
		}
	};
	ws.onclose = () => {
		ws = null;
		religar = setTimeout(conectar, 1000); // o serviço ainda não subiu, ou reiniciou
	};
	ws.onerror = () => {
		try {
			ws.close();
		} catch (e) {
			// já fechou
		}
	};
}

browser.tabs.onUpdated.addListener((id, info, aba) => {
	const servico = abas.get(id);
	if (servico && (info.status !== undefined || info.url !== undefined || info.title !== undefined)) {
		enviar(descrever(aba, servico));
	}
});

browser.tabs.onRemoved.addListener((id) => {
	const servico = abas.get(id);
	if (servico) {
		abas.delete(id);
		enviar({ evento: "fechada", servico, tab: id });
	}
});

// O YouTube só serve a interface de TV (youtube.com/tv) a quem se anuncia como TV: troca o User-Agent dos pedidos a ele (e o ua-tv.js, o que a página lê).
// Só no perfil da Mídia. Os pedidos ao Google da conta (accounts.google.com) seguem como o Firefox, para o login funcionar.
const UA_TV = "Mozilla/5.0 (SMART-TV; Linux; Tizen 5.0) AppleWebKit/537.36 (KHTML, like Gecko) 69.0.3497.106.1/5.0 TV Safari/537.36";
browser.webRequest.onBeforeSendHeaders.addListener(
	(d) => {
		for (const h of d.requestHeaders) {
			if (h.name.toLowerCase() === "user-agent") {
				h.value = UA_TV;
			}
		}
		return { requestHeaders: d.requestHeaders };
	},
	{ urls: ["*://*.youtube.com/*"] },
	["blocking", "requestHeaders"],
);

conectar();
