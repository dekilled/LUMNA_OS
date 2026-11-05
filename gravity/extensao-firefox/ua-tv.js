/*
 * LUMNA Mídia: o YouTube só serve a interface de TV (youtube.com/tv) a quem se anuncia como TV. O fundo.js troca o User-Agent
 * que vai no pedido; este arquivo troca o que a própria página lê em navigator.userAgent (o app da TV confere os dois).
 * Só vale no YouTube (manifest.json) e só no perfil da Mídia, que é separado do Firefox de todo dia.
 */
"use strict";
(function () {
	const UA_TV = "Mozilla/5.0 (SMART-TV; Linux; Tizen 5.0) AppleWebKit/537.36 (KHTML, like Gecko) 69.0.3497.106.1/5.0 TV Safari/537.36";
	const alvo = Object.getPrototypeOf(window.wrappedJSObject.navigator);
	const trocar = (nome, valor) => {
		Object.defineProperty(alvo, nome, { get: exportFunction(() => valor, window), configurable: true, enumerable: true });
	};
	trocar("userAgent", UA_TV);
	trocar("appVersion", UA_TV.replace(/^Mozilla\//, ""));
	trocar("platform", "Linux armv7l");
})();
