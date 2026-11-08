#include "teclado_layout.h"

#include <glib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- os layouts ----
 * Cada linha é uma lista de teclas separadas por espaço. Uma letra/símbolo é a própria tecla; as especiais começam com @ e podem levar
 * =destino e *peso: @pag=simb1*1.5. Os pesos dão a largura relativa na linha (1 é uma letra). */

typedef struct layout {
	const char *nome, *sigla;
	const char *linhas[3];  /* as três linhas de letras (a de números e a de baixo são comuns) */
	const char *topo;       /* a quarta: Shift, as letras de baixo e o apagar */
} layout;

static const layout LAYOUTS[] = {
	{ "pt-BR (ABNT2)", "PT", { "q w e r t y u i o p", "a s d f g h j k l ç", NULL }, "@shift*1.5 z x c v b n m @apagar*1.5" },
	{ "English (US)", "EN", { "q w e r t y u i o p", "a s d f g h j k l ;", NULL }, "@shift*1.5 z x c v b n m @apagar*1.5" },
};
#define N_LAYOUTS ((int)(sizeof(LAYOUTS) / sizeof(LAYOUTS[0])))

int tl_layouts_n(void) { return N_LAYOUTS; }
const char *tl_layout_nome(int i) { return LAYOUTS[i < 0 || i >= N_LAYOUTS ? 0 : i].nome; }
const char *tl_layout_sigla(int i) { return LAYOUTS[i < 0 || i >= N_LAYOUTS ? 0 : i].sigla; }

int tl_layout_do_orbit(const char *layout, const char *variante) {
	if (layout && (!strcmp(layout, "br") || !strncmp(layout, "br,", 3) || !strcmp(layout, "pt") || !strncmp(layout, "pt,", 3))) {
		return 0;
	}
	(void)variante;
	return 1;
}

/* ---- as variantes de uma tecla segurada ---- */
static const struct { const char *base, *alts; } VARIANTES[] = {
	{ "a", "á à â ã ä å" }, { "e", "é è ê ë" }, { "i", "í ì î ï" }, { "o", "ó ò ô õ ö" }, { "u", "ú ù û ü" },
	{ "c", "ç" }, { "ç", "c" }, { "n", "ñ" }, { "y", "ý ÿ" }, { "s", "ß š" },
	{ ".", ", ? ! : ; … ' \"" }, { ",", ". ? ! ;" }, { "-", "– — _" }, { "?", "¿" }, { "!", "¡" },
	{ "0", "° º" }, { "1", "¹ ½" }, { "2", "² ⅔" }, { "3", "³ ¾" }, { "$", "€ £ ¥ ¢" }, { "\"", "“ ” « »" }, { "'", "‘ ’" },
	{ "/", "\\ |" }, { "=", "≠ ≈ ±" }, { "%", "‰" },
};

static const char *variantes_de(const char *base) {
	for (size_t i = 0; i < sizeof(VARIANTES) / sizeof(VARIANTES[0]); i++) {
		if (!strcmp(VARIANTES[i].base, base)) {
			return VARIANTES[i].alts;
		}
	}
	return NULL;
}

void tl_maiuscula(const char *texto, char *saida, size_t tam) {
	char *g = g_utf8_strup(texto, -1);
	snprintf(saida, tam, "%s", g ? g : texto);
	g_free(g);
}

/* ---- emojis ---- */
#define N_POR_CATEGORIA 24
static const struct { const char *rotulo, *nome; const char *emojis[N_POR_CATEGORIA]; } CATEGORIAS[] = {
	{ "🕘", "Recentes", { NULL } },
	{ "😀", "Carinhas", { "😀", "😃", "😄", "😁", "😆", "😅", "😂", "🤣", "😊", "😇", "🙂", "😉", "😍", "🥰", "😘", "😋", "😎", "🤩", "🥳", "😏", "😒", "😞", "😢", "😭" } },
	{ "👍", "Gestos", { "👍", "👎", "👌", "✌️", "🤞", "🤟", "🤘", "👏", "🙌", "🙏", "💪", "👋", "🤝", "☝️", "👆", "👇", "👈", "👉", "✊", "👊", "🤙", "🖐️", "✋", "🤚" } },
	{ "❤️", "Corações", { "❤️", "🧡", "💛", "💚", "💙", "💜", "🖤", "🤍", "🤎", "💔", "❣️", "💕", "💞", "💓", "💗", "💖", "💘", "💝", "💟", "♥️", "😻", "💑", "💋", "🔥" } },
	{ "🐶", "Animais", { "🐶", "🐱", "🐭", "🐹", "🐰", "🦊", "🐻", "🐼", "🐨", "🐯", "🦁", "🐮", "🐷", "🐸", "🐵", "🐔", "🐧", "🐦", "🦆", "🦉", "🐺", "🐴", "🦄", "🐝" } },
	{ "🍔", "Comida", { "🍎", "🍌", "🍇", "🍓", "🍉", "🍍", "🥑", "🍅", "🌽", "🥕", "🍞", "🧀", "🍳", "🥓", "🍔", "🍟", "🍕", "🌭", "🌮", "🍣", "🍦", "🍰", "🍫", "☕" } },
	{ "⚽", "Atividades", { "⚽", "🏀", "🏈", "⚾", "🎾", "🏐", "🎱", "🏓", "🥊", "🎮", "🕹️", "🎲", "🎯", "🎳", "🎸", "🎹", "🥁", "🎤", "🎧", "🎬", "🎨", "🏆", "🥇", "🎁" } },
	{ "✈️", "Viagem", { "✈️", "🚗", "🚕", "🚌", "🚓", "🚑", "🚒", "🚲", "🏍️", "🚂", "🚀", "🛸", "⛵", "🏠", "🏢", "🏫", "🏥", "🏖️", "🏔️", "🌋", "🗺️", "🌍", "🌙", "☀️" } },
	{ "💡", "Objetos", { "💡", "📱", "💻", "🖥️", "⌨️", "🖱️", "📷", "📚", "📖", "✏️", "📌", "📎", "🔑", "🔒", "🔔", "⏰", "💰", "💳", "🔧", "🔨", "📦", "🧸", "🎈", "🎉" } },
	{ "🔣", "Símbolos", { "✅", "❌", "⚠️", "❓", "❗", "➕", "➖", "➡️", "⬅️", "⬆️", "⬇️", "🔄", "✔️", "⭐", "💯", "💤", "♻️", "🔴", "🟢", "🔵", "⚫", "⚪", "🆗", "🚫" } },
};
#define N_CATEGORIAS ((int)(sizeof(CATEGORIAS) / sizeof(CATEGORIAS[0])))

static char recentes[TL_MAX_RECENTES][16];
static int n_recentes;

int tl_categorias_n(void) { return N_CATEGORIAS; }
const char *tl_categoria_rotulo(int i) { return CATEGORIAS[i < 0 || i >= N_CATEGORIAS ? 1 : i].rotulo; }
const char *tl_categoria_nome(int i) { return CATEGORIAS[i < 0 || i >= N_CATEGORIAS ? 1 : i].nome; }

int tl_categoria_emojis(int i, const char *const **lista) {
	if (i <= 0 || i >= N_CATEGORIAS) {
		static const char *rec[TL_MAX_RECENTES];
		for (int k = 0; k < n_recentes; k++) {
			rec[k] = recentes[k];
		}
		*lista = rec;
		return n_recentes;
	}
	*lista = CATEGORIAS[i].emojis;
	int n = 0;
	while (n < N_POR_CATEGORIA && CATEGORIAS[i].emojis[n]) {
		n++;
	}
	return n;
}

void tl_recentes_usar(const char *emoji) {
	if (!emoji || !*emoji || strlen(emoji) >= sizeof(recentes[0])) {
		return;
	}
	int onde = n_recentes < TL_MAX_RECENTES ? n_recentes : TL_MAX_RECENTES - 1;
	for (int k = 0; k < n_recentes; k++) {
		if (!strcmp(recentes[k], emoji)) {
			onde = k;
			break;
		}
	}
	for (int k = onde; k > 0; k--) {
		memcpy(recentes[k], recentes[k - 1], sizeof(recentes[0]));
	}
	snprintf(recentes[0], sizeof(recentes[0]), "%s", emoji);
	if (onde == n_recentes && n_recentes < TL_MAX_RECENTES) {
		n_recentes++;
	}
}
int tl_recentes_n(void) { return n_recentes; }
const char *tl_recentes_get(int i) { return i >= 0 && i < n_recentes ? recentes[i] : ""; }
void tl_recentes_limpar(void) { n_recentes = 0; }

/* ---- montar a página ---- */

static tl_tecla *nova(tl_pagina *p) {
	if (p->total >= TL_MAX_TECLAS || p->por_linha[p->nlinhas - 1] >= TL_MAX_POR_LINHA) {
		return NULL;
	}
	tl_tecla *t = &p->teclas[p->total++];
	memset(t, 0, sizeof(*t));
	t->peso = 1;
	p->por_linha[p->nlinhas - 1]++;
	return t;
}

static void nova_linha(tl_pagina *p) {
	if (p->nlinhas >= TL_MAX_LINHAS) {
		return;
	}
	p->primeira[p->nlinhas] = p->total;
	p->por_linha[p->nlinhas] = 0;
	p->nlinhas++;
}

static void tecla_texto(tl_pagina *p, const char *texto, bool maiuscula, bool com_variantes) {
	tl_tecla *t = nova(p);
	if (!t) {
		return;
	}
	t->tipo = TL_CHAR;
	if (maiuscula) {
		tl_maiuscula(texto, t->texto, sizeof(t->texto));
	} else {
		snprintf(t->texto, sizeof(t->texto), "%s", texto);
	}
	snprintf(t->rotulo, sizeof(t->rotulo), "%s", t->texto);
	const char *alts = com_variantes ? variantes_de(texto) : NULL;
	if (alts) {
		if (maiuscula) {
			tl_maiuscula(alts, t->alts, sizeof(t->alts));
		} else {
			snprintf(t->alts, sizeof(t->alts), "%s", alts);
		}
	}
}

static const struct { const char *nome; enum tl_tipo tipo; const char *rotulo; } ESPECIAIS[] = {
	{ "shift", TL_SHIFT, "⇧" }, { "apagar", TL_APAGAR, "⌫" }, { "enter", TL_ENTER, "↵" }, { "espaco", TL_ESPACO, "" },
	{ "pag", TL_PAGINA, "" }, { "idioma", TL_IDIOMA, "" }, { "fechar", TL_FECHAR, "⌄" }, { "tab", TL_TAB, "⇥" }, { "esc", TL_ESC, "Esc" },
	{ "esq", TL_SETA, "←" }, { "dir", TL_SETA, "→" }, { "cima", TL_SETA, "↑" }, { "baixo", TL_SETA, "↓" },
};

static int pagina_do_nome(const char *n) {
	return !strcmp(n, "letras") ? TL_LETRAS : !strcmp(n, "simb1") ? TL_SIMB1 : !strcmp(n, "simb2") ? TL_SIMB2 : TL_EMOJI;
}

/* Uma linha da descrição entra na página. */
static void montar_linha(tl_pagina *p, const char *descricao, const tl_estado *e, bool letras) {
	nova_linha(p);
	char *copia = g_strdup(descricao);
	for (char *tok = strtok(copia, " "); tok; tok = strtok(NULL, " ")) {
		if (tok[0] != '@' || tok[1] == '\0') { /* o '@' sozinho é a tecla @ */
			tecla_texto(p, tok, letras && e->shift != TL_SHIFT_NAO, true);
			continue;
		}
		float peso = 1;
		char *estrela = strchr(tok, '*');
		if (estrela) {
			*estrela = '\0';
			peso = (float)atof(estrela + 1);
		}
		char *igual = strchr(tok, '=');
		const char *arg = NULL;
		if (igual) {
			*igual = '\0';
			arg = igual + 1;
		}
		for (size_t k = 0; k < sizeof(ESPECIAIS) / sizeof(ESPECIAIS[0]); k++) {
			if (strcmp(tok + 1, ESPECIAIS[k].nome)) {
				continue;
			}
			tl_tecla *t = nova(p);
			if (!t) {
				break;
			}
			t->tipo = ESPECIAIS[k].tipo;
			t->peso = peso;
			snprintf(t->rotulo, sizeof(t->rotulo), "%s", ESPECIAIS[k].rotulo);
			if (t->tipo == TL_SHIFT) {
				t->ativa = e->shift != TL_SHIFT_NAO;
				t->destino = e->shift == TL_SHIFT_TRAVADO;
			} else if (t->tipo == TL_PAGINA && arg) {
				t->destino = pagina_do_nome(arg);
				const char *r = t->destino == TL_LETRAS ? "ABC" : t->destino == TL_SIMB1 ? "?123" : t->destino == TL_SIMB2 ? "=\\<" : "😀";
				snprintf(t->rotulo, sizeof(t->rotulo), "%s", r);
			} else if (t->tipo == TL_IDIOMA) {
				snprintf(t->rotulo, sizeof(t->rotulo), "%s", tl_layout_sigla(e->layout));
			} else if (t->tipo == TL_SETA) {
				t->destino = !strcmp(tok + 1, "esq") ? TL_ESQ : !strcmp(tok + 1, "dir") ? TL_DIR : !strcmp(tok + 1, "cima") ? TL_CIMA : TL_BAIXO;
			}
			break;
		}
	}
	g_free(copia);
}

static const char *NUMEROS = "1 2 3 4 5 6 7 8 9 0";

void tl_montar(const tl_estado *e, tl_pagina *p) {
	memset(p, 0, sizeof(*p));
	const layout *l = &LAYOUTS[e->layout < 0 || e->layout >= N_LAYOUTS ? 0 : e->layout];
	switch (e->pagina) {
	case TL_LETRAS:
		montar_linha(p, NUMEROS, e, false);
		montar_linha(p, l->linhas[0], e, true);
		montar_linha(p, l->linhas[1], e, true);
		montar_linha(p, l->topo, e, true);
		montar_linha(p, e->largo ? "@fechar @pag=simb1*1.3 @idioma @pag=emoji @esc @tab , @espaco*4 . @esq @cima @baixo @dir @enter*1.6"
		                         : "@fechar @pag=simb1*1.3 @idioma @pag=emoji , @espaco*3.4 . @enter*1.6", e, false);
		break;
	case TL_SIMB1:
		montar_linha(p, NUMEROS, e, false);
		montar_linha(p, "@ # $ _ & - + ( ) /", e, false);
		montar_linha(p, "@pag=simb2*1.5 * \" ' : ; ! ? @apagar*1.5", e, false);
		montar_linha(p, "@fechar @pag=letras*1.3 @pag=emoji , @espaco*3.4 . @enter*1.6", e, false);
		break;
	case TL_SIMB2:
		montar_linha(p, NUMEROS, e, false);
		montar_linha(p, "~ ` | · √ π ÷ × ¶ ∆", e, false);
		montar_linha(p, "@pag=simb1*1.5 [ ] { } < > ^ = @apagar*1.5", e, false);
		montar_linha(p, "@fechar @pag=letras*1.3 € £ ¥ © ® ™ % @enter*1.6", e, false);
		break;
	case TL_EMOJI: {
		nova_linha(p);
		for (int c = 0; c < N_CATEGORIAS; c++) {
			tl_tecla *t = nova(p);
			if (!t) {
				break;
			}
			t->tipo = TL_CATEGORIA;
			t->destino = c;
			t->ativa = c == e->categoria;
			snprintf(t->rotulo, sizeof(t->rotulo), "%s", CATEGORIAS[c].rotulo);
		}
		const char *const *lista;
		int n = tl_categoria_emojis(e->categoria, &lista);
		int por_linha = e->largo ? 12 : 8;
		int linhas = N_POR_CATEGORIA / por_linha;
		for (int r = 0; r < linhas; r++) {
			nova_linha(p);
			for (int k = 0; k < por_linha; k++) {
				int i = r * por_linha + k;
				tl_tecla *t = nova(p);
				if (!t) {
					break;
				}
				if (i < n) {
					t->tipo = TL_CHAR;
					snprintf(t->texto, sizeof(t->texto), "%s", lista[i]);
					snprintf(t->rotulo, sizeof(t->rotulo), "%s", lista[i]);
				} else {
					t->tipo = TL_CHAR; /* um lugar vazio (os recentes ainda não enchem a grade) */
				}
			}
		}
		montar_linha(p, "@pag=letras*1.5 @espaco*5 @apagar*1.5", e, false);
		break;
	}
	}
}

/* ---- geometria ---- */

int tl_geometria(const tl_pagina *p, float largura, float altura, float folga, tl_ret *rets) {
	if (p->nlinhas <= 0) {
		return 0;
	}
	float alt_tecla = (altura - folga * (p->nlinhas + 1)) / p->nlinhas;
	for (int r = 0; r < p->nlinhas; r++) {
		int ini = p->primeira[r], n = p->por_linha[r];
		float soma = 0;
		for (int k = 0; k < n; k++) {
			soma += p->teclas[ini + k].peso;
		}
		float util = largura - folga * (n + 1);
		float x = folga;
		float y = folga + r * (alt_tecla + folga);
		for (int k = 0; k < n; k++) {
			float w = util * p->teclas[ini + k].peso / (soma > 0 ? soma : 1);
			rets[ini + k] = (tl_ret){ x, y, w, alt_tecla };
			x += w + folga;
		}
	}
	return p->total;
}

int tl_tecla_em(const tl_ret *rets, int n, float x, float y, float folga) {
	int melhor = -1;
	float menor = 1e9f;
	for (int i = 0; i < n; i++) {
		float dx = x < rets[i].x ? rets[i].x - x : x > rets[i].x + rets[i].w ? x - rets[i].x - rets[i].w : 0;
		float dy = y < rets[i].y ? rets[i].y - y : y > rets[i].y + rets[i].h ? y - rets[i].y - rets[i].h : 0;
		float d = sqrtf(dx * dx + dy * dy);
		if (d < menor) {
			menor = d;
			melhor = i;
		}
	}
	return menor <= folga ? melhor : -1;
}

/* ---- o que uma tecla faz ---- */

tl_acao tl_tocar(tl_estado *e, const tl_tecla *t) {
	tl_acao a = { TL_NADA, "", TL_ESQ };
	switch (t->tipo) {
	case TL_CHAR:
		if (!t->texto[0]) {
			break;
		}
		snprintf(a.texto, sizeof(a.texto), "%s", t->texto);
		a.resultado = TL_TEXTO;
		if (e->pagina == TL_EMOJI) {
			tl_recentes_usar(t->texto);
		}
		if (e->shift == TL_SHIFT_UMA) {
			e->shift = TL_SHIFT_NAO; /* a maiúscula vale uma letra */
		}
		break;
	case TL_ESPACO:
		snprintf(a.texto, sizeof(a.texto), " ");
		a.resultado = TL_TEXTO;
		break;
	case TL_SHIFT:
		e->shift = e->shift == TL_SHIFT_NAO ? TL_SHIFT_UMA : e->shift == TL_SHIFT_UMA ? TL_SHIFT_TRAVADO : TL_SHIFT_NAO;
		a.resultado = TL_REDESENHAR;
		break;
	case TL_PAGINA:
		e->pagina = (enum tl_pagina_id)t->destino;
		if (e->pagina == TL_EMOJI && e->categoria == 0 && tl_recentes_n() == 0) {
			e->categoria = 1; /* sem recentes ainda, abre nas carinhas */
		}
		a.resultado = TL_REDESENHAR;
		break;
	case TL_IDIOMA:
		e->layout = (e->layout + 1) % N_LAYOUTS;
		a.resultado = TL_REDESENHAR;
		break;
	case TL_CATEGORIA:
		e->categoria = t->destino;
		a.resultado = TL_REDESENHAR;
		break;
	case TL_APAGAR: a.resultado = TL_APAGAR_UM; break;
	case TL_ENTER: a.resultado = TL_ENTER_; break;
	case TL_TAB: a.resultado = TL_TAB_; break;
	case TL_ESC: a.resultado = TL_ESC_; break;
	case TL_SETA:
		a.seta = (enum tl_seta)t->destino;
		a.resultado = TL_SETA_;
		break;
	case TL_FECHAR: a.resultado = TL_ESCONDER; break;
	}
	return a;
}

bool tl_capitalizar_agora(const char *antes) {
	if (!antes || !*antes) {
		return true;
	}
	int n = (int)strlen(antes);
	int i = n;
	bool espaco = false;
	while (i > 0 && (antes[i - 1] == ' ' || antes[i - 1] == '\t')) {
		i--;
		espaco = true;
	}
	if (i == 0) {
		return true; /* só espaços */
	}
	if (antes[i - 1] == '\n') {
		return true;
	}
	return espaco && (antes[i - 1] == '.' || antes[i - 1] == '!' || antes[i - 1] == '?');
}
