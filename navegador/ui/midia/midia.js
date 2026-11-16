// Interface da Mídia (HTML). Fala com o C++ por window.cefQuery (só existe nesta página). O C++ chama lumna.menu(...), lumna.recarregar() e lumna.anim(...).
const q = (pedido) => new Promise((ok, erro) => window.cefQuery({ request: pedido, onSuccess: ok, onFailure: (c, m) => erro(m) }));
const cores = {};   // a cor de cada serviço vem dos dados (s.cor); aqui só o que o catálogo e o menu consultam por id
const corDe = (id) => ((estado.servicos.find((x) => x.id === id) || {}).cor) || cores[id] || '#cfc4ff';
const $ = (id) => document.getElementById(id);
function el(tag, cls, html) { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; return e; }
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

let estado = { servicos: [], salvos: {}, ativo: '', estado: 'inicio' };
// A navegação por zonas (cada zona é uma fila de itens focáveis): início: abas, continuar, servicos | catálogo: abas, filtros, grade | menu: folha | ações: acoes
let zona = 'servicos', idx = 0;
let vista = 'inicio';                 // 'inicio' | 'catalogo'
let cat = { itens: [] };
const filtro = { tipo: 'todos', lista: 'todos', busca: '', genero: '', ordem: 'recentes', visao: 'grade' };
const LISTAS = [['todos', 'Minhas listas'], ['depois', 'Assistir mais tarde'], ['favoritos', 'Favoritos'], ['assistidos', 'Já assisti']];
const TIPOS = [['todos', 'Todos'], ['filme', 'Filmes'], ['serie', 'Séries'], ['video', 'Vídeos']];
const ROTULO_TIPO = { filme: 'Filme', serie: 'Série', video: 'Vídeo', outro: 'Outro' };
const ABA_TIPO = { filmes: 'filme', series: 'serie', videos: 'video' };
const ABAS_ATIVAS = ['inicio', 'filmes', 'series', 'videos'];   // a Música ainda não existe
let abaAtual = 'inicio';
let acoesDe = null;   // o id do item do catálogo cujo menu de ações está aberto
let acoesSvc = null;  // ... ou o id do streaming (menu de ações de um serviço)
let acoesIdxAntes = 0; // o foco de antes de abrir o menu de ações (o idx é um só: sem isto, ao fechar o foco ia para o índice da OPÇÃO do menu)

const restante = (s) => { if (!s.dur) return ''; const f = Math.max(0, s.dur - s.pos), h = Math.floor(f / 3600), m = Math.round((f % 3600) / 60); if (f < 60) return 'menos de 1min restante'; return (h ? h + 'h ' : '') + m + 'min restantes'; };
const duracao = (min) => { if (!min) return ''; const h = Math.floor(min / 60), m = Math.round(min % 60); return h ? `${h}h${String(m).padStart(2, '0')}` : `${m} min`; };

// ---------------------------------------------------------------------------------------------------------------------------- o início
function desenhar() {
  console.log('desenhou salvos=' + Object.keys(estado.salvos).length + ' servicos=' + estado.servicos.length);
  const lista = Object.entries(estado.salvos).filter(([, s]) => s.thumb_url).sort((a, b) => b[1].quando - a[1].quando);
  $('sec-continuar').hidden = lista.length === 0;
  const c = $('continuar'); c.innerHTML = '';
  for (const [id, s] of lista.slice(0, 4)) {
    const nome = (estado.servicos.find((x) => x.id === id) || {}).nome || id;
    const k = el('div', 'cartao');
    k.dataset.id = id;
    k.innerHTML = `<div class="img" style="background-image:url('${s.thumb_url}?t=${s.quando}')">${s.dur ? `<div class="barra"><i style="width:${Math.min(100, 100 * s.pos / s.dur)}%"></i></div>` : ''}</div>` +
      `<div class="leg"><b>${esc(s.titulo || nome)}</b><small>${esc(restante(s) || nome)}</small></div>`;
    k.onclick = () => { zona = 'continuar'; idx = [...c.children].indexOf(k); ativar(); };
    c.appendChild(k);
  }
  const t = $('servicos'); t.innerHTML = '';
  for (const s of estado.servicos) {
    const k = el('div', 'tile', `<span style="color:${s.cor || 'inherit'}">${esc(s.nome)}</span>${s.favorito ? '<span class="fav">★</span>' : ''}`);
    k.dataset.id = s.id; k.onclick = () => { zona = 'servicos'; idx = [...t.children].indexOf(k); ativar(); };
    t.appendChild(k);
  }
  const mais = el('div', 'tile add', '<span>+</span><small>Adicionar</small>'); mais.dataset.acao = 'adicionar';
  mais.onclick = () => { zona = 'servicos'; idx = t.children.length; ativar(); };
  t.appendChild(mais);
  if (!zonaItens('continuar').length && zona === 'continuar') zona = 'servicos';
  mostrarPagina('servicos', 'pontos', zona === 'servicos' ? Math.floor(idx / POR_PAGINA) : Math.min(paginas.servicos || 0, Math.ceil(t.children.length / POR_PAGINA) - 1));
  marcar();
}

// ---------------------------------------------------------------------------------------------------------------------------- o menu de serviços (folha)
function desenharFolha(ativo) {
  $('folha-ativo').textContent = (estado.servicos.find((x) => x.id === ativo) || {}).nome || '';
  const f = $('folha-itens'); f.innerHTML = '';
  const itens = [{ id: 'inicio', nome: '⌂ Início' }, ...estado.servicos];
  for (const s of itens) {
    const k = el('div', 'tile' + (s.id === ativo ? ' em-uso' : ''), `<span style="color:${s.cor || 'inherit'}">${esc(s.nome)}</span>` +
      (s.id === ativo ? '<small>Em uso</small>' : (estado.salvos[s.id] ? '<small class="sub-retomar">Retomar última página</small>' : '')));
    k.dataset.id = s.id; k.onclick = () => { idx = [...f.children].indexOf(k); ativar(); };
    f.appendChild(k);
  }
  idx = Math.max(0, itens.findIndex((s) => s.id === ativo) + 1);  // começa no serviço seguinte ao atual
  if (idx >= itens.length) idx = 0;
  mostrarPagina('folha-itens', 'folha-pontos', Math.floor(idx / POR_PAGINA), true);   // abre já na página do serviço seguinte (sem deslizar)
  marcar();
}

// ---------------------------------------------------------------------------------------------------------------------------- o catálogo
const ICONES = {
  busca: '<svg viewBox="0 0 24 24"><circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/></svg>',
  grade: '<svg viewBox="0 0 24 24"><rect x="4" y="4" width="6" height="6" rx="1"/><rect x="14" y="4" width="6" height="6" rx="1"/><rect x="4" y="14" width="6" height="6" rx="1"/><rect x="14" y="14" width="6" height="6" rx="1"/></svg>',
  lista: '<svg viewBox="0 0 24 24"><path d="M5 6h14M5 12h14M5 18h14"/></svg>',
  coracao: '<svg viewBox="0 0 24 24"><path d="M12 20s-7-4.5-7-10a4 4 0 0 1 7-2.5A4 4 0 0 1 19 10c0 5.500-7 10-7 10z"/></svg>',
  relogio: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/><path d="M12 8v4l3 2"/></svg>',
  link: '<svg viewBox="0 0 24 24" style="width:2.2vh;height:2.2vh;fill:none;stroke:currentColor;stroke-width:2;vertical-align:-.4vh"><path d="M10 14a4 4 0 0 0 6 0l3-3a4 4 0 0 0-6-6l-1 1M14 10a4 4 0 0 0-6 0l-3 3a4 4 0 0 0 6 6l1-1"/></svg>',
  pasta: '<svg viewBox="0 0 24 24" style="width:2.2vh;height:2.2vh;fill:none;stroke:currentColor;stroke-width:2;vertical-align:-.4vh"><path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/></svg>',
};

function generos() { return [...new Set(cat.itens.map((i) => i.genero).filter(Boolean))].sort(); }
function itensFiltrados() {
  let l = cat.itens.slice();
  if (filtro.tipo !== 'todos') l = l.filter((i) => i.tipo === filtro.tipo || (filtro.tipo === 'video' && i.origem === 'local'));
  if (filtro.lista === 'depois') l = l.filter((i) => i.depois); else if (filtro.lista === 'favoritos') l = l.filter((i) => i.favorito); else if (filtro.lista === 'assistidos') l = l.filter((i) => i.assistido);
  if (filtro.genero) l = l.filter((i) => i.genero === filtro.genero);
  const b = filtro.busca.trim().toLowerCase();
  if (b) l = l.filter((i) => (i.titulo + ' ' + i.servico + ' ' + i.genero).toLowerCase().includes(b));
  l.sort(filtro.ordem === 'az' ? (a, b2) => a.titulo.localeCompare(b2.titulo, 'pt-BR') : (a, b2) => b2.salvo_em - a.salvo_em);
  return l;
}
const nomeServico = (i) => i.origem === 'local' ? 'Arquivo local' : ((estado.servicos.find((x) => x.id === i.servico) || {}).nome || i.servico || '');

function desenharFiltros() {
  const f = $('filtros'); f.innerHTML = '';
  for (const [v, nome] of TIPOS) { const c = el('div', 'chip' + (filtro.tipo === v ? ' on' : ''), esc(nome)); c.dataset.acao = 'tipo:' + v; f.appendChild(c); }
  f.appendChild(el('span', 'sep'));
  const b = el('div', 'campo busca', ICONES.busca + `<input id="busca" type="text" placeholder="Buscar no catálogo" value="${esc(filtro.busca)}">`); b.dataset.acao = 'busca'; f.appendChild(b);
  const g = el('div', 'campo sel', esc(filtro.genero || 'Todos os gêneros')); g.dataset.acao = 'genero'; f.appendChild(g);
  const o = el('div', 'campo sel', filtro.ordem === 'az' ? 'Ordem alfabética' : 'Adicionados recentemente'); o.dataset.acao = 'ordem'; f.appendChild(o);
  f.appendChild(el('span', 'sep'));
  const v1 = el('div', 'alterna' + (filtro.visao === 'grade' ? ' on' : ''), ICONES.grade); v1.dataset.acao = 'visao:grade'; f.appendChild(v1);
  const v2 = el('div', 'alterna' + (filtro.visao === 'lista' ? ' on' : ''), ICONES.lista); v2.dataset.acao = 'visao:lista'; f.appendChild(v2);
  for (const c of f.children) c.onclick = () => { zona = 'filtros'; idx = zonaItens('filtros').indexOf(c); ativar(); };
  const ln = LISTAS.find(([v]) => v === filtro.lista); $('listas-nome').textContent = ln ? ln[1] : 'Minhas listas';
  const bi = $('busca'); bi.oninput = () => { filtro.busca = bi.value; desenharGrade(); };
  bi.onkeydown = (e) => { if (['Escape', 'Enter', 'ArrowDown'].includes(e.key)) { e.stopPropagation(); e.preventDefault(); bi.blur(); if (e.key === 'ArrowDown') { zona = 'grade'; idx = 0; } marcar(); } else if (e.key === 'F2') { e.stopPropagation(); } e.stopPropagation(); };
  $('btn-listas').onclick = () => { zona = 'filtros'; idx = zonaItens('filtros').length - 1; ativar(); };
}

function cartaoItem(i) {
  const k = el('div', 'cartao-cat'); k.dataset.id = i.id;
  const cor = corDe(i.servico);
  const prog = i.dur > 0 ? Math.min(100, 100 * i.pos / i.dur) : 0;
  const meta = [ROTULO_TIPO[i.tipo] || 'Outro', i.genero, duracao(i.duracao_min)].filter(Boolean).join(' · ') + (i.assistido ? '<span class="a-lista"> · ✓ Assistido</span>' : '');
  const fundo = i.miniatura_url ? `style="background-image:url('${i.miniatura_url}')"` : '';
  k.innerHTML = `<div class="poster" ${fundo}>${i.miniatura_url ? '' : `<div class="letra">${esc((i.titulo || '?')[0].toUpperCase())}</div>`}` +
    `<span class="ic fav${i.favorito ? ' on' : ''}">${ICONES.coracao}</span><span class="ic dep${i.depois ? ' on' : ''}">${ICONES.relogio}</span>` +
    `${i.assistido ? '<span class="ok">✓ Assistido</span>' : ''}<div class="play"></div>${prog ? `<div class="prog"><i style="width:${prog}%"></i></div>` : ''}</div>` +
    `<div class="info"><div class="tit"><b>${esc(i.titulo)}</b><span class="mais">···</span></div><div class="meta">${meta}</div>` +
    `<div class="serv" style="color:${cor}">${esc(nomeServico(i))}</div><div class="orig">${i.origem === 'local' ? ICONES.pasta + ' Arquivo local' : ICONES.link + ' Link salvo'}</div></div>`;
  k.onclick = () => { zona = 'grade'; idx = [...$('grade').children].indexOf(k); ativar(); };
  const mais = k.querySelector('.mais');
  mais.onclick = (e) => { e.stopPropagation(); zona = 'grade'; idx = [...$('grade').children].indexOf(k); marcar(); abrirAcoes(i.id); };
  return k;
}

function desenharGrade() {
  const g = $('grade'); g.innerHTML = ''; g.className = 'grade' + (filtro.visao === 'lista' ? ' lista' : '');
  const l = itensFiltrados();
  for (const i of l) g.appendChild(cartaoItem(i));
  const v = $('vazio');
  v.hidden = l.length > 0; g.hidden = l.length === 0;
  v.innerHTML = cat.itens.length === 0
    ? 'O catálogo está vazio.<br>Num serviço, aperte <b>LT</b> (ou <b>lumna-navegador --salvar</b>) para guardar o que você está vendo em <b>Assistir mais tarde</b>, e <b>RT</b> para os <b>Favoritos</b>.'
    : 'Nada por aqui com esses filtros.';
  $('cat-sub').textContent = cat.itens.length ? `${l.length} de ${cat.itens.length} no seu espaço` : 'Filmes, séries e vídeos no seu espaço';
  if (l.length) { const m = g.children[0].querySelector('.mais').getBoundingClientRect(); console.log(`mais-rect ${l[0].id} ${Math.round(m.left + m.width / 2)} ${Math.round(m.top + m.height / 2)}`); }
  console.log(`catalogo: ${l.length}/${cat.itens.length} itens tipo=${filtro.tipo} lista=${filtro.lista} genero=${filtro.genero || '-'} busca=${filtro.busca || '-'} ordem=${filtro.ordem} visao=${filtro.visao}`);
  if (zona === 'grade') { idx = Math.max(0, Math.min(idx, l.length - 1)); if (!l.length) { zona = 'filtros'; idx = 0; } }
  marcar();
}

async function carregarCatalogo() { try { cat = JSON.parse(await q('catalogo')); } catch (e) { console.log('erro catalogo: ' + e); } }
async function mostrarCatalogo() {
  await carregarCatalogo();
  vista = 'catalogo';
  document.body.className = 'catalogo';
  $('catalogo').hidden = false; $('home').hidden = true;
  document.scrollingElement.scrollTop = 0;
  desenharFiltros(); desenharGrade();
}
function mostrarInicio() {
  vista = 'inicio'; abaAtual = 'inicio';
  document.scrollingElement.scrollTop = 0;
  document.body.className = 'inicio';
  $('catalogo').hidden = true; $('home').hidden = false;
  marcarAbas();
  const d = (e) => getComputedStyle($(e)).display;
  console.log(`vista inicio: catalogo=${d('catalogo')} home=${d('home')} rolagem=${document.scrollingElement.scrollTop}`);
}
function marcarAbas() { for (const a of $('abas').children) a.classList.toggle('ativa', a.dataset.aba === abaAtual); }

async function ativarAba(nome) {
  if (nome === 'musica') { aviso('A Música da nova Mídia vem em breve.'); return; }
  abaAtual = nome;
  if (nome === 'inicio') { mostrarInicio(); marcarAbas(); marcar(); return; }
  filtro.tipo = ABA_TIPO[nome] || 'todos';
  await mostrarCatalogo();
  marcarAbas();
}

let avisoTimer = null;
function aviso(t) { const a = $('aviso'); a.textContent = t; a.hidden = false; clearTimeout(avisoTimer); avisoTimer = setTimeout(() => { a.hidden = true; }, 2600); }

// ---------------------------------------------------------------------------------------------------------------------------- o menu de ações de um item
function abrirAcoes(id) {
  const i = cat.itens.find((x) => x.id === id); if (!i) return;
  acoesDe = id;
  console.log('acoes abertas ' + id);
  const a = $('acoes-itens'); a.innerHTML = '';
  const opcoes = [
    [i.depois ? 'Tirar de Assistir mais tarde' : 'Assistir mais tarde', 'depois:' + (i.depois ? 0 : 1)],
    [i.favorito ? 'Tirar dos favoritos' : 'Favoritar', 'favorito:' + (i.favorito ? 0 : 1)],
    [i.assistido ? 'Marcar como não assistido' : 'Marcar como assistido', 'assistido:' + (i.assistido ? 0 : 1)],
    ['Remover do catálogo', 'remover'],
  ];
  for (const [nome, acao] of opcoes) { const o = el('div', 'it' + (acao === 'remover' ? ' perigo' : ''), esc(nome)); o.dataset.acao = acao; o.onclick = () => { idx = [...a.children].indexOf(o); zona = 'acoes'; ativar(); }; a.appendChild(o); }
  const card = [...$('grade').children].find((c) => c.dataset.id === id), r = card ? card.getBoundingClientRect() : { left: 200, top: 200, width: 100 };
  const ac = $('acoes'); ac.hidden = false;
  ac.style.left = Math.min(innerWidth - ac.offsetWidth - 20, r.left + r.width * 0.5) + 'px';
  ac.style.top = Math.min(innerHeight - ac.offsetHeight - 20, Math.max(20, r.top + 40)) + 'px';
  acoesIdxAntes = idx; zona = 'acoes'; idx = 0; marcar();
}
function fecharAcoes() { $('acoes').hidden = true; const svc = acoesSvc; acoesDe = null; acoesSvc = null; if (zona === 'acoes') { zona = svc ? 'servicos' : 'grade'; idx = acoesIdxAntes; } marcar(); }

async function mudarItem(id, acao, valor) {   // favorito / depois / assistido / remover; o C++ devolve o catálogo atualizado
  try { cat = JSON.parse(await q(`item:${id}:${acao}${valor === undefined ? '' : ':' + valor}`)); } catch (e) { console.log('erro item: ' + e); return; }
  desenharGrade();
}

// ---------------------------------------------------------------------------------------------------------------------------- adicionar streaming e as ações de um serviço
async function mudarServico(cmd, idDepois) {   // o C++ devolve o estado com a lista nova; true se deu certo
  try { estado = JSON.parse(await q('servico:' + cmd)); } catch (e) { aviso('Não deu: ' + e); return false; }
  desenhar();
  if (idDepois) { const k = estado.servicos.findIndex((x) => x.id === idDepois); if (k >= 0 && zona === 'servicos') { idx = k; marcar(); } }
  return true;
}
async function abrirBiblioteca() {
  let b; try { b = JSON.parse(await q('biblioteca')); } catch (e) { aviso('Não consegui abrir a lista de streamings.'); return; }
  const g = $('bib-itens'); g.innerHTML = '';
  for (const s of b.itens) {
    const k = el('div', 'tile' + (s.tem ? ' tem' : ''), `<span style="color:${s.cor}">${esc(s.nome)}</span>${s.tem ? '<small>Já na sua lista</small>' : ''}`);
    k.dataset.id = s.id; k.dataset.tem = s.tem ? '1' : '';
    k.onclick = () => { zona = 'biblioteca'; idx = [...g.children].indexOf(k); ativar(); };
    g.appendChild(k);
  }
  const o = el('div', 'tile', '<span>Outro site…</span><small>Nome e endereço</small>'); o.dataset.id = 'outro';
  o.onclick = () => { zona = 'biblioteca'; idx = g.children.length - 1; ativar(); };
  g.appendChild(o);
  $('biblioteca').hidden = false; zona = 'biblioteca';
  idx = b.itens.findIndex((s) => !s.tem); if (idx < 0) idx = b.itens.length;
  console.log('biblioteca aberta: ' + b.itens.length + ' itens, ' + b.itens.filter((s) => !s.tem).length + ' disponiveis');
  marcar();
}
function fecharBiblioteca(idServico) {
  $('biblioteca').hidden = true; $('form-site').hidden = true; zona = 'servicos';
  const k = idServico ? estado.servicos.findIndex((x) => x.id === idServico) : -1;
  idx = k >= 0 ? k : Math.max(0, zonaItens('servicos').length - 1); marcar();
}
function abrirForm() { $('site-nome').value = ''; $('site-url').value = ''; $('form-site').hidden = false; zona = 'form'; idx = 0; marcar(); }
function cancelarForm() { $('form-site').hidden = true; zona = 'biblioteca'; idx = Math.max(0, zonaItens('biblioteca').length - 1); marcar(); }
for (const inp of [$('site-nome'), $('site-url')]) inp.onkeydown = (e) => {   // o formulário: setas e Enter passam entre os campos; Esc cancela
  e.stopPropagation();
  if (e.key === 'Escape') { e.preventDefault(); cancelarForm(); }
  else if (e.key === 'ArrowDown' || e.key === 'Enter') { e.preventDefault(); idx = Math.min(3, idx + 1); marcar(); }
  else if (e.key === 'ArrowUp') { e.preventDefault(); idx = Math.max(0, idx - 1); marcar(); }
};
$('site-ok').onclick = () => { zona = 'form'; idx = 2; ativar(); };
$('site-cancela').onclick = () => cancelarForm();

function abrirAcoesServico(id) {
  const s = estado.servicos.find((x) => x.id === id); if (!s) return;
  acoesSvc = id; acoesDe = null;
  const a = $('acoes-itens'); a.innerHTML = '';
  const opcoes = [
    [s.favorito ? 'Tirar dos favoritos' : 'Tornar favorito (o primeiro da lista)', 'svc:favorito'],
    [s.modo === 'cursor' ? 'Controle: usar as setas (jeito de TV)' : 'Controle: usar o cursor', 'svc:modo:' + (s.modo === 'cursor' ? 'setas' : 'cursor')],
    ['Remover da lista', 'svc:remover'],
  ];
  for (const [nome, acao] of opcoes) { const o = el('div', 'it' + (acao === 'svc:remover' ? ' perigo' : ''), esc(nome)); o.dataset.acao = acao; o.onclick = () => { idx = [...a.children].indexOf(o); zona = 'acoes'; ativar(); }; a.appendChild(o); }
  const tile = [...$('servicos').children].find((c) => c.dataset.id === id), r = tile ? tile.getBoundingClientRect() : { left: 200, top: 200, width: 100, height: 60 };
  const ac = $('acoes'); ac.hidden = false;
  ac.style.left = Math.min(innerWidth - ac.offsetWidth - 20, Math.max(20, r.left)) + 'px';
  ac.style.top = Math.max(20, r.top - ac.offsetHeight - 10) + 'px';
  console.log('acoes de servico ' + id);
  acoesIdxAntes = idx; zona = 'acoes'; idx = 0; marcar();
}

// ---------------------------------------------------------------------------------------------------------------------------- foco e navegação
function zonaItens(z) {
  if (z === 'biblioteca') return [...$('bib-itens').children];
  if (z === 'form') return [$('site-nome'), $('site-url'), $('site-ok'), $('site-cancela')];
  if (z === 'folha') return [...$('folha-itens').children];
  if (z === 'filtros') return [...$('filtros').children].filter((c) => c.dataset.acao).concat([$('btn-listas')]);
  if (z === 'grade') return [...$('grade').children];
  if (z === 'acoes') return [...$('acoes-itens').children];
  return [...$(z).children];
}
// As filas de streamings (a do início e o menu de serviços): POR_PAGINA por tela. Ao passar do último, a fila desliza (a transição é do CSS: --pg) e entram os seguintes;
// os pontos mostram a página. Sem isto os cartões iam encolhendo a cada serviço novo, até ficarem minúsculos.
const POR_PAGINA = 6;
const paginas = {};   // trilho → página atual
function mostrarPagina(trilho, pontos, p, semAnim) {
  const t = $(trilho), total = Math.max(1, Math.ceil(t.children.length / POR_PAGINA));
  p = Math.max(0, Math.min(total - 1, p));
  const pt = $(pontos);
  if (pt.children.length !== total) { pt.innerHTML = ''; if (total > 1) for (let i = 0; i < total; i++) pt.appendChild(el('i')); }
  [...pt.children].forEach((d, i) => d.classList.toggle('on', i === p));
  if (p === paginas[trilho] && t.style.getPropertyValue('--pg') === String(p)) return;
  paginas[trilho] = p;
  if (semAnim) { t.style.transition = 'none'; t.style.setProperty('--pg', p); void t.offsetWidth; t.style.transition = ''; }
  else t.style.setProperty('--pg', p);
  setTimeout(() => {   // para o teste: onde estão o primeiro e o último cartões depois de deslizar (x < 0 = fora da tela, à esquerda)
    const k = [...t.children]; if (!k.length) return;
    const r0 = k[0].getBoundingClientRect(), rf = k[Math.min(k.length, (p + 1) * POR_PAGINA) - 1].getBoundingClientRect();
    console.log(`${trilho === 'servicos' ? '' : trilho + '-'}pagina ${p}/${total} primeiro.x=${Math.round(r0.left)} ultimo-da-pagina.x=${Math.round(rf.left)} largura=${innerWidth}`);
  }, 700);
}
function marcar() {
  document.querySelectorAll('.foco').forEach((e) => e.classList.remove('foco'));
  if (zona === 'servicos') mostrarPagina('servicos', 'pontos', Math.floor(idx / POR_PAGINA));
  else if (zona === 'folha') mostrarPagina('folha-itens', 'folha-pontos', Math.floor(idx / POR_PAGINA));
  const it = zonaItens(zona)[idx]; if (it) { it.classList.add('foco'); if (zona === 'grade') it.scrollIntoView({ block: 'nearest' }); }
  if (zona === 'form') { const f = zonaItens('form')[idx]; if (f && f.tagName === 'INPUT') f.focus(); else if (document.activeElement && document.activeElement.tagName === 'INPUT') document.activeElement.blur(); }
  console.log(`foco zona=${zona} idx=${idx}/${zonaItens(zona).length}`);
}

function vizinhoGrade(dir) {   // o item mais próximo na direção (a grade do catálogo e a biblioteca têm linhas e colunas)
  const cs = zonaItens(zona), cur = cs[idx]; if (!cur) return -1;
  const caixa = (c) => (c.querySelector('.poster') || c).getBoundingClientRect();
  const r = caixa(cur), cx = r.left + r.width / 2, cy = r.top + r.height / 2;
  let melhor = -1, nota = 1e12;
  cs.forEach((c, k) => {
    if (k === idx) return;
    const p = caixa(c), px = p.left + p.width / 2, py = p.top + p.height / 2, dx = px - cx, dy = py - cy;
    const ok = dir === 'DIR' ? dx > r.width * .3 && Math.abs(dy) < r.height * .5 : dir === 'ESQ' ? dx < -r.width * .3 && Math.abs(dy) < r.height * .5
      : dir === 'BAIXO' ? dy > r.height * .3 : dy < -r.height * .3;
    if (!ok) return;
    const n = (dir === 'DIR' || dir === 'ESQ') ? Math.abs(dx) + Math.abs(dy) * 4 : Math.abs(dy) + Math.abs(dx) * 3;
    if (n < nota) { nota = n; melhor = k; }
  });
  return melhor;
}

function mover(dir) {   // dir: ESQ, DIR, CIMA, BAIXO
  const n = zonaItens(zona).length;
  if (zona === 'biblioteca') {
    const k = vizinhoGrade(dir); if (k >= 0) idx = k;
  } else if (zona === 'form') {
    if (dir === 'CIMA') idx = Math.max(0, idx - 1); else if (dir === 'BAIXO') idx = Math.min(3, idx + 1); else if (idx >= 2) idx = dir === 'DIR' ? 3 : 2;
  } else if (zona === 'grade') {
    const k = vizinhoGrade(dir);
    if (k >= 0) idx = k; else if (dir === 'CIMA') { zona = 'filtros'; idx = Math.min(zonaItens('filtros').length - 1, 4); }
  } else if (zona === 'acoes' || zona === 'folha') {
    if (zona === 'acoes') { if (dir === 'CIMA') idx = Math.max(0, idx - 1); else if (dir === 'BAIXO') idx = Math.min(n - 1, idx + 1); } else if (dir === 'DIR') idx = Math.min(n - 1, idx + 1); else if (dir === 'ESQ') idx = Math.max(0, idx - 1);
  } else if (dir === 'DIR') idx = Math.min(n - 1, idx + 1);
  else if (dir === 'ESQ') idx = Math.max(0, idx - 1);
  else if (dir === 'CIMA') {
    if (zona === 'abas') return;
    if (zona === 'servicos' && zonaItens('continuar').length) { zona = 'continuar'; idx = Math.min(idx, zonaItens('continuar').length - 1); }
    else { zona = 'abas'; idx = Math.max(0, [...$('abas').children].findIndex((a) => a.dataset.aba === abaAtual)); }
  } else if (dir === 'BAIXO') {
    if (zona === 'abas') { if (vista === 'catalogo') { zona = 'filtros'; idx = 0; } else { zona = zonaItens('continuar').length ? 'continuar' : 'servicos'; idx = 0; } }
    else if (zona === 'continuar') { zona = 'servicos'; idx = Math.min(idx, zonaItens('servicos').length - 1); }
    else if (zona === 'filtros' && $('grade').children.length) { zona = 'grade'; idx = 0; }
  }
  marcar();
}

function trocarAbaPorTecla(passo) {   // LB/RB (Page Up / Page Down): a aba anterior / a próxima
  const k = ABAS_ATIVAS.indexOf(abaAtual); ativarAba(ABAS_ATIVAS[(k + passo + ABAS_ATIVAS.length) % ABAS_ATIVAS.length]);
}

async function ativar() {
  const it = zonaItens(zona)[idx]; if (!it) return;
  try {
    if (zona === 'biblioteca') {
      if (it.dataset.id === 'outro') { abrirForm(); return; }
      if (it.dataset.tem) { aviso('Esse já está na sua lista.'); return; }
      if (await mudarServico('adicionar:' + it.dataset.id, it.dataset.id)) fecharBiblioteca(it.dataset.id);
      return;
    }
    if (zona === 'form') {
      if (idx === 3) { cancelarForm(); return; }
      if (idx < 2) { idx++; marcar(); return; }
      if (await mudarServico('novo:' + $('site-nome').value + '|' + $('site-url').value, null)) fecharBiblioteca(estado.servicos[estado.servicos.length - 1].id);
      return;
    }
    if (zona === 'servicos' && it.dataset.acao === 'adicionar') { await abrirBiblioteca(); return; }
    if (zona === 'abas') { await ativarAba(it.dataset.aba); return; }
    if (zona === 'filtros') {
      const [a, v] = it.dataset.acao ? it.dataset.acao.split(':') : ['listas'];
      if (a === 'tipo') { filtro.tipo = v; desenharFiltros(); desenharGrade(); }
      else if (a === 'visao') { filtro.visao = v; desenharFiltros(); desenharGrade(); }
      else if (a === 'ordem') { filtro.ordem = filtro.ordem === 'az' ? 'recentes' : 'az'; desenharFiltros(); desenharGrade(); }
      else if (a === 'genero') { const gs = ['', ...generos()]; filtro.genero = gs[(gs.indexOf(filtro.genero) + 1) % gs.length]; desenharFiltros(); desenharGrade(); }
      else if (a === 'listas') { const ids = LISTAS.map((x) => x[0]); filtro.lista = ids[(ids.indexOf(filtro.lista) + 1) % ids.length]; desenharFiltros(); desenharGrade(); }
      else if (a === 'busca') { $('busca').focus(); return; }
      marcar();
      return;
    }
    if (zona === 'grade') { const id = it.dataset.id; await q('item:' + id + ':abrir'); return; }
    if (zona === 'acoes' && it.dataset.acao.startsWith('svc:')) {   // ações de um streaming (favorito, modo de controle, remover)
      const id = acoesSvc, acao = it.dataset.acao.slice(4);
      fecharAcoes();
      await mudarServico(id + ':' + acao, id);
      return;
    }
    if (zona === 'acoes') {
      const [acao, val] = it.dataset.acao.split(':'), id = acoesDe;
      fecharAcoes();
      await mudarItem(id, acao, val);
      return;
    }
    const id = it.dataset.id;
    if (zona === 'folha') await q('trocar:' + id);
    else if (zona === 'continuar') await q('retomar:' + id);
    else await q('abrir:' + id);
  } catch (e) { console.log('erro: ' + e); aviso('Não consegui abrir: ' + e); }
}

document.addEventListener('keydown', (e) => {
  console.log('tecla ' + e.key);
  // Os atalhos da janela (F2, Alt+Home) não chegam quando é a INTERFACE que tem o foco (menu aberto, tela de carregamento): então ela os repassa.
  if (e.key === 'F2') { e.preventDefault(); q('menu:alternar'); return; }
  if (e.altKey && e.key === 'Home') { e.preventDefault(); q('inicio'); return; }
  const itemAtual = zona === 'grade' ? zonaItens('grade')[idx] : null;
  const MAPA = { ArrowRight: 'DIR', ArrowLeft: 'ESQ', ArrowUp: 'CIMA', ArrowDown: 'BAIXO' };
  if (MAPA[e.key] && zona !== 'folha' || (zona === 'folha' && (e.key === 'ArrowRight' || e.key === 'ArrowLeft'))) { e.preventDefault(); mover(MAPA[e.key]); return; }
  if (e.key === 'Enter') { e.preventDefault(); ativar(); return; }
  if (e.key === 'm' && zona === 'servicos') {   // as ações do streaming em foco (favorito, modo de controle, remover)
    const it = zonaItens('servicos')[idx];
    if (it && it.dataset.id) { e.preventDefault(); abrirAcoesServico(it.dataset.id); return; }
  }
  if (e.key === 'Escape') {
    e.preventDefault();
    if (zona === 'form') cancelarForm();
    else if (zona === 'biblioteca') fecharBiblioteca();
    else if (zona === 'acoes') fecharAcoes();
    else if (zona === 'folha') q('menu:sair');
    else if (vista === 'catalogo') { ativarAba('inicio'); zona = 'servicos'; idx = 0; marcar(); }
    return;
  }
  if (e.key === 'e' && vista === 'catalogo' && zona === 'grade') { e.preventDefault(); q('emma'); return; }
  if (e.key === 'PageDown' && !['folha', 'biblioteca', 'form', 'acoes'].includes(zona)) { e.preventDefault(); trocarAbaPorTecla(1); return; }
  if (e.key === 'PageUp' && !['folha', 'biblioteca', 'form', 'acoes'].includes(zona)) { e.preventDefault(); trocarAbaPorTecla(-1); return; }
  if (itemAtual) {   // atalhos do cartão em foco
    const id = itemAtual.dataset.id, i = cat.itens.find((x) => x.id === id);
    if (i && e.key === 'f') { e.preventDefault(); mudarItem(id, 'favorito', i.favorito ? 0 : 1); }
    else if (i && e.key === 'w') { e.preventDefault(); mudarItem(id, 'depois', i.depois ? 0 : 1); }
    else if (i && e.key === 'a') { e.preventDefault(); mudarItem(id, 'assistido', i.assistido ? 0 : 1); }
    else if (e.key === 'm') { e.preventDefault(); abrirAcoes(id); }
  }
});

// ---------------------------------------------------------------------------------------------------------------------------- a transição do site
// (ver Transicao no midia.cc): mostra o print no lugar do site, avisa o C++ para tirar o site da frente, anima e avisa de novo.
const anim = $('anim');
const pct = (r) => ({ left: r.x * 100 + '%', top: r.y * 100 + '%', width: r.w * 100 + '%', height: r.h * 100 + '%' });
// (não usa requestAnimationFrame: com o site por cima, o navegador suspende o rAF da interface que está por baixo)
const quadro = (ms = 60) => new Promise((ok) => setTimeout(ok, ms));
async function animar(o) {
  const t0 = performance.now(), T = (m) => console.log('anim#' + o.id + ' ' + Math.round(performance.now() - t0) + 'ms ' + m);
  T('começou ' + (o.src ? 'com print' : 'splash'));
  anim.className = 'anim' + (o.src ? '' : ' splash');
  anim.style.backgroundImage = o.src ? `url("${o.src}")` : '';
  anim.innerHTML = o.src ? '' : `<span>${esc(o.nome)}</span><i></i>`;
  anim.style.transition = 'none';
  Object.assign(anim.style, pct(o.de));
  anim.style.display = o.src ? 'block' : 'grid';
  await quadro(80);                     // o print precisa estar PINTADO antes de o site sair da frente (senão pisca)
  T('pintado'); await q('anim:oculta:' + o.id); T('site escondido');
  await quadro(60);
  anim.style.transition = ['left', 'top', 'width', 'height'].map((p) => `${p} ${o.ms}ms cubic-bezier(.2,.8,.2,1)`).join(',');
  if (o.menu === 1) lumna.menu(true, o.ativo);   // o menu sobe/desce JUNTO com a escala do site (a mesma hora, a mesma duração)
  if (o.menu === 2) lumna.menu(false);
  Object.assign(anim.style, pct(o.para));
  await new Promise((ok) => setTimeout(ok, o.ms + 40));
  T('css acabou'); await q('anim:fim:' + o.id); T('avisou o fim');                  // o C++ põe o site no tamanho final (quando a página também já carregou) e depois manda sumir o print
}

window.lumna = {
  anim(o) { animar(o).catch((e) => console.log('erro na animação: ' + e)); },
  animSumir() { anim.style.display = 'none'; anim.style.transition = 'none'; anim.innerHTML = ''; },
  menu(ligado, ativo) {
    document.body.className = ligado ? 'menu' : 'servico';
    $('folha').setAttribute('aria-hidden', String(!ligado));
    if (ligado) { q('estado').then((r) => { estado = JSON.parse(r); zona = 'folha'; desenharFolha(ativo); }); }
    else if (zona === 'folha') { zona = vista === 'catalogo' ? 'grade' : 'servicos'; idx = 0; marcar(); }
  },
  servicos() {   // a lista de streamings mudou por fora (lumna-navegador --adicionar): redesenha a fila sem mexer no foco
    q('estado').then((r) => { estado = JSON.parse(r); if (document.body.className === 'inicio' || document.body.className === 'catalogo') desenhar(); });
  },
  recarregar() {   // de volta ao início (de um serviço): volta para onde estava, o início ou o catálogo
    q('estado').then(async (r) => {
      estado = JSON.parse(r);
      if (vista === 'catalogo') { await carregarCatalogo(); document.body.className = 'catalogo'; desenharFiltros(); desenharGrade(); desenhar(); zona = 'grade'; idx = Math.min(idx, Math.max(0, $('grade').children.length - 1)); if (!$('grade').children.length) { zona = 'filtros'; idx = 0; } marcar(); marcarAbas(); }
      else { document.body.className = 'inicio'; zona = 'servicos'; idx = 0; desenhar(); }
    });
  },
};

const hora = () => { $('relogio').textContent = new Date().toLocaleString('pt-BR', { weekday: 'short', day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }); };
hora(); setInterval(hora, 20000);
for (const a of $('abas').children) a.onclick = () => { zona = 'abas'; idx = [...$('abas').children].indexOf(a); ativar(); };
$('btn-emma').onclick = () => q('emma');
marcarAbas();
lumna.recarregar();
