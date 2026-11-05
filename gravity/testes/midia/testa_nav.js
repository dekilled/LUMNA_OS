// A navegação por setas da extensão (extensao-firefox/nav.js) num Chromium de verdade, com teclas de verdade.
// Uso: node testa_nav.js   (usa o playwright que já vem na máquina)
const { chromium } = require("playwright");
const fs = require("fs");
const path = require("path");

const nav = fs.readFileSync(path.join(__dirname, "../../extensao-firefox/nav.js"), "utf8");
const html = `<!doctype html><meta charset=utf-8><style>
body{margin:0;font:16px sans-serif;background:#111;color:#eee}
.grade{display:grid;grid-template-columns:repeat(3,200px);gap:40px;padding:60px}
.grade a{display:block;height:120px;background:#345;color:#fff;padding:10px}
#sozinho{margin:20px 60px;width:180px;height:50px;background:#543}
#entrada{margin:0 60px}
#baixo{margin-top:1800px;display:block;padding:20px}
</style>
<div class=grade>
 <a href="#a1" id=a1>1</a><a href="#a2" id=a2>2</a><a href="#a3" id=a3>3</a>
 <a href="#b1" id=b1>4</a><a href="#b2" id=b2>5</a><a href="#b3" id=b3>6</a>
</div>
<div id=sozinho onclick="window.cliques=(window.cliques||0)+1">div com onclick</div>
<input id=entrada value="texto">
<a id=baixo href="#fim">no fim da página</a><div style="height:3000px"></div>`;

(async () => {
	const browser = await chromium.launch();
	const page = await browser.newPage({ viewport: { width: 1000, height: 700 } });
	await page.setContent(html);
	await page.addScriptTag({ content: nav });
	const foco = () => page.evaluate(() => document.activeElement && (document.activeElement.id || document.activeElement.tagName));
	const falhas = [];
	const confere = (c, m) => { if (!c) { falhas.push(m); console.log("FALHOU:", m); } };

	await page.keyboard.press("ArrowRight");
	const primeiro = await foco();
	confere(["b2", "a2", "b1", "b3", "a1", "a3"].includes(primeiro), `a primeira seta devia pôr o foco num item da grade: ${primeiro}`);
	// vai para o item do meio da linha de cima e anda por ela
	await page.evaluate(() => window.__lumnaNav.marcar(document.getElementById("a1")));
	await page.keyboard.press("ArrowRight");
	confere((await foco()) === "a2", `direita de 1 é 2: ${await foco()}`);
	await page.keyboard.press("ArrowRight");
	confere((await foco()) === "a3", `direita de 2 é 3: ${await foco()}`);
	await page.keyboard.press("ArrowDown");
	confere((await foco()) === "b3", `abaixo de 3 é 6: ${await foco()}`);
	await page.keyboard.press("ArrowLeft");
	confere((await foco()) === "b2", `esquerda de 6 é 5: ${await foco()}`);
	await page.keyboard.press("ArrowUp");
	confere((await foco()) === "a2", `acima de 5 é 2: ${await foco()}`);
	const contorno = await page.evaluate(() => document.getElementById("a2").hasAttribute("data-lumna-foco") && !document.getElementById("a1").hasAttribute("data-lumna-foco"));
	confere(contorno, "só o item em foco leva o contorno");

	// o OK clica (um link e um div com onclick)
	await page.evaluate(() => { window.cliques = 0; document.getElementById("a2").addEventListener("click", (e) => { e.preventDefault(); window.linkClicado = true; }); });
	await page.keyboard.press("Enter");
	confere(await page.evaluate(() => window.linkClicado === true), "Enter num link clica nele");
	await page.keyboard.press("ArrowDown"); // b2
	await page.keyboard.press("ArrowDown"); // o div
	confere((await foco()) === "sozinho", `abaixo da grade vem o div: ${await foco()}`);
	await page.keyboard.press("Enter");
	confere((await page.evaluate(() => window.cliques)) === 1, "Enter num div com onclick clica nele (sem ser botão nem link)");

	// em campo de texto, as setas são do texto
	await page.focus("#entrada");
	await page.keyboard.press("ArrowLeft");
	confere((await foco()) === "entrada", "as setas não saem de um campo de texto");

	// sem para onde ir, a seta rola a página
	await page.evaluate(() => window.__lumnaNav.marcar(document.getElementById("baixo")));
	await page.evaluate(() => window.scrollTo(0, 0));
	const antes = await page.evaluate(() => scrollY);
	await page.evaluate(() => document.activeElement.blur());
	await page.evaluate(() => { document.querySelectorAll("a,input,#sozinho").forEach((e) => e.remove()); });
	await page.keyboard.press("ArrowDown");
	await page.waitForTimeout(300);
	const depois = await page.evaluate(() => scrollY);
	confere(depois > antes, `sem nada clicável a seta rola a página (${antes} -> ${depois})`);

	await browser.close();
	if (falhas.length) { process.exit(1); }
	console.log("navegação por setas ok");
})();
