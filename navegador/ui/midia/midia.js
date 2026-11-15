// Interface da Mídia (HTML). Fala com o C++ por window.cefQuery (só existe nesta página). O C++ chama lumna.menu(...) e lumna.recarregar().
const q = (pedido) => new Promise((ok, erro) => window.cefQuery({ request: pedido, onSuccess: ok, onFailure: (c, m) => erro(m) }));
const cores = { netflix: '#ff2a35', youtube: '#ff3b3b', disney: '#5b9bff', paramount: '#5a8cff', prime: '#2fc0ee', hbomax: '#a78bfa' };
let estado = { servicos: [], salvos: {}, ativo: '', estado: 'inicio' };
let zona = 'servicos', idx = 0;  // zonas: continuar, servicos (início) e folha (menu)
const $ = (id) => document.getElementById(id);

const restante = (s) => { if (!s.dur) return ''; const f = Math.max(0, s.dur - s.pos), h = Math.floor(f / 3600), m = Math.round((f % 3600) / 60); if (f < 60) return 'menos de 1min restante'; return (h ? h + 'h ' : '') + m + 'min restantes'; };
function el(tag, cls, html) { const e = document.createElement(tag); if (cls) e.className = cls; if (html !== undefined) e.innerHTML = html; return e; }
const esc = (t) => String(t).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

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
    const k = el('div', 'tile', `<span style="color:${cores[s.id] || 'inherit'}">${esc(s.nome)}</span>`);
    k.dataset.id = s.id; k.onclick = () => { zona = 'servicos'; idx = [...t.children].indexOf(k); ativar(); };
    t.appendChild(k);
  }
  if (!zonaItens('continuar').length && zona === 'continuar') zona = 'servicos';
  marcar();
}

function desenharFolha(ativo) {
  $('folha-ativo').textContent = (estado.servicos.find((x) => x.id === ativo) || {}).nome || '';
  const f = $('folha-itens'); f.innerHTML = '';
  const itens = [{ id: 'inicio', nome: '⌂ Início' }, ...estado.servicos];
  for (const s of itens) {
    const k = el('div', 'tile' + (s.id === ativo ? ' em-uso' : ''), `<span style="color:${cores[s.id] || 'inherit'}">${esc(s.nome)}</span>` +
      (s.id === ativo ? '<small>Em uso</small>' : (estado.salvos[s.id] ? '<small class="sub-retomar">Retomar última página</small>' : '')));
    k.dataset.id = s.id; k.onclick = () => { idx = [...f.children].indexOf(k); ativar(); };
    f.appendChild(k);
  }
  idx = Math.max(0, itens.findIndex((s) => s.id === ativo) + 1);  // começa no serviço seguinte ao atual
  if (idx >= itens.length) idx = 0;
  marcar();
}

const zonaItens = (z) => [...($(z === 'folha' ? 'folha-itens' : z).children)];
function marcar() {
  document.querySelectorAll('.foco').forEach((e) => e.classList.remove('foco'));
  const it = zonaItens(zona)[idx]; if (it) it.classList.add('foco');
}
async function ativar() {
  const it = zonaItens(zona)[idx]; if (!it) return;
  const id = it.dataset.id;
  try {
    if (zona === 'folha') await q('trocar:' + id);
    else if (zona === 'continuar') await q('retomar:' + id);
    else await q('abrir:' + id);
  } catch (e) { console.log('erro: ' + e); }
}

document.addEventListener('keydown', (e) => {
  const itens = zonaItens(zona);
  if (e.key === 'ArrowRight') idx = Math.min(itens.length - 1, idx + 1);
  else if (e.key === 'ArrowLeft') idx = Math.max(0, idx - 1);
  else if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && zona !== 'folha') {
    const outra = zona === 'servicos' ? 'continuar' : 'servicos';
    if (e.key === (zona === 'servicos' ? 'ArrowUp' : 'ArrowDown') && zonaItens(outra).length) { zona = outra; idx = Math.min(idx, zonaItens(outra).length - 1); }
  } else if (e.key === 'Enter') { ativar(); return; }
  else if (e.key === 'Escape') { if (zona === 'folha') q('menu:sair'); return; }
  else return;
  e.preventDefault(); marcar();
});

// A transição do site (ver Transicao no midia.cc): mostra o print no lugar do site, avisa o C++ para tirar o site da frente, anima e avisa de novo.
const anim = $('anim');
const pct = (r) => ({ left: r.x * 100 + '%', top: r.y * 100 + '%', width: r.w * 100 + '%', height: r.h * 100 + '%' });
// (não usa requestAnimationFrame: com o site por cima, o navegador suspende o rAF da interface que está por baixo)
const quadro = (ms = 60) => new Promise((ok) => setTimeout(ok, ms));
async function animar(o) {
  anim.className = 'anim' + (o.src ? '' : ' splash');
  anim.style.backgroundImage = o.src ? `url("${o.src}")` : '';
  anim.innerHTML = o.src ? '' : `<span>${esc(o.nome)}</span><i></i>`;
  anim.style.transition = 'none';
  Object.assign(anim.style, pct(o.de));
  anim.style.display = o.src ? 'block' : 'grid';
  await quadro(80);                     // o print precisa estar PINTADO antes de o site sair da frente (senão pisca)
  await q('anim:oculta:' + o.id);
  await quadro(60);
  anim.style.transition = ['left', 'top', 'width', 'height'].map((p) => `${p} ${o.ms}ms cubic-bezier(.2,.8,.2,1)`).join(',');
  Object.assign(anim.style, pct(o.para));
  await new Promise((ok) => setTimeout(ok, o.ms + 40));
  await q('anim:fim:' + o.id);                  // o C++ põe o site no tamanho final (quando a página também já carregou) e depois manda sumir o print
}

window.lumna = {
  anim(o) { animar(o).catch((e) => console.log('erro na animação: ' + e)); },
  animSumir() { anim.style.display = 'none'; anim.style.transition = 'none'; anim.innerHTML = ''; },
  menu(ligado, ativo) {
    document.body.className = ligado ? 'menu' : 'servico';
    $('folha').setAttribute('aria-hidden', String(!ligado));
    if (ligado) { q('estado').then((r) => { estado = JSON.parse(r); zona = 'folha'; desenharFolha(ativo); }); }
    else if (zona === 'folha') { zona = 'servicos'; idx = 0; marcar(); }
  },
  recarregar() { document.body.className = 'inicio'; q('estado').then((r) => { estado = JSON.parse(r); zona = 'servicos'; idx = 0; desenhar(); }); },
};

const hora = () => { $('relogio').textContent = new Date().toLocaleString('pt-BR', { weekday: 'short', day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' }); };
hora(); setInterval(hora, 20000);
lumna.recarregar();
