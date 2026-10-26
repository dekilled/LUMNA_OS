#define _DEFAULT_SOURCE /* IFF_*, ip_mreq, SO_REUSEPORT */
#include "mdns.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <glib-unix.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MDNS_PORTA 5353
#define MDNS_GRUPO "224.0.0.251"
#define TTL_S 120

enum { T_A = 1, T_PTR = 12, T_TXT = 16, T_SRV = 33, T_ANY = 255 };

static int fd = -1;
static int porta_servico;
static char nome_servico[96]; /* _lumna-remote._tcp.local */
static char nome_instancia[200];
static char nome_host[96];
static char rotulo_instancia[96];

/* os endereços IPv4 desta máquina que valem na rede local */
static int enderecos(struct in_addr *saida, int max) {
	struct ifaddrs *lista = NULL;
	int n = 0;
	if (getifaddrs(&lista) < 0) {
		return 0;
	}
	for (struct ifaddrs *i = lista; i && n < max; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP) || (i->ifa_flags & IFF_LOOPBACK) ||
			!(i->ifa_flags & IFF_MULTICAST)) {
			continue;
		}
		saida[n++] = ((struct sockaddr_in *)i->ifa_addr)->sin_addr;
	}
	freeifaddrs(lista);
	return n;
}

int mdns_enderecos(char (*saida)[16], int max) {
	struct in_addr ends[8];
	int n = enderecos(ends, 8);
	int k = 0;
	for (int i = 0; i < n && k < max; i++) {
		inet_ntop(AF_INET, &ends[i], saida[k++], 16);
	}
	return k;
}

static void entrar_no_grupo(void) {
	struct in_addr ends[8];
	int n = enderecos(ends, 8);
	for (int i = 0; i < n; i++) {
		struct ip_mreq m = { .imr_multiaddr.s_addr = inet_addr(MDNS_GRUPO), .imr_interface = ends[i] };
		/* já estar no grupo dá erro: normal */
		setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &m, sizeof(m));
	}
}

/* ---- escrita de pacote ---- */

struct pacote {
	uint8_t b[1400];
	int n;
	bool cheio;
};

static void p8(struct pacote *p, uint8_t v) {
	if (p->n < (int)sizeof(p->b)) {
		p->b[p->n++] = v;
	} else {
		p->cheio = true;
	}
}

static void p16(struct pacote *p, uint16_t v) {
	p8(p, (uint8_t)(v >> 8));
	p8(p, (uint8_t)v);
}

static void p32(struct pacote *p, uint32_t v) {
	p16(p, (uint16_t)(v >> 16));
	p16(p, (uint16_t)v);
}

static void pnome(struct pacote *p, const char *nome) {
	const char *ini = nome;
	for (const char *c = nome;; c++) {
		if (*c == '.' || !*c) {
			p8(p, (uint8_t)(c - ini));
			for (const char *k = ini; k < c; k++) {
				p8(p, (uint8_t)*k);
			}
			ini = c + 1;
			if (!*c) {
				break;
			}
		}
	}
	p8(p, 0);
}

/* o cabeçalho de um registro; devolve onde fica o tamanho, a preencher em fim_rr */
static int inicio_rr(struct pacote *p, const char *nome, uint16_t tipo, bool unico, uint32_t ttl) {
	pnome(p, nome);
	p16(p, tipo);
	p16(p, (uint16_t)(1 | (unico ? 0x8000 : 0)));
	p32(p, ttl);
	int onde = p->n;
	p16(p, 0);
	return onde;
}

static void fim_rr(struct pacote *p, int onde) {
	int tam = p->n - onde - 2;
	p->b[onde] = (uint8_t)(tam >> 8);
	p->b[onde + 1] = (uint8_t)tam;
}

/* os quatro tipos de registro deste serviço; ttl 0 é a despedida */
static void registro(struct pacote *p, int qual, uint32_t ttl) {
	struct in_addr ends[8];
	switch (qual) {
	case T_PTR: {
		int o = inicio_rr(p, nome_servico, T_PTR, false, ttl);
		pnome(p, nome_instancia);
		fim_rr(p, o);
		break;
	}
	case T_SRV: {
		int o = inicio_rr(p, nome_instancia, T_SRV, true, ttl);
		p16(p, 0);
		p16(p, 0);
		p16(p, (uint16_t)porta_servico);
		pnome(p, nome_host);
		fim_rr(p, o);
		break;
	}
	case T_TXT: {
		int o = inicio_rr(p, nome_instancia, T_TXT, true, ttl);
		p8(p, 3);
		p8(p, 'v');
		p8(p, '=');
		p8(p, '1');
		fim_rr(p, o);
		break;
	}
	default: {
		int n = enderecos(ends, 8);
		for (int i = 0; i < n; i++) {
			int o = inicio_rr(p, nome_host, T_A, true, ttl);
			for (int k = 0; k < 4; k++) {
				p8(p, ((uint8_t *)&ends[i])[k]);
			}
			fim_rr(p, o);
		}
	}
	}
}

static int conta_a(void) {
	struct in_addr ends[8];
	return enderecos(ends, 8);
}

/* uma resposta: o que se perguntou nas respostas, o resto como adicional */
static void montar(struct pacote *p, uint32_t ttl, bool ptr, bool srv, bool txt, bool a) {
	memset(p, 0, sizeof(*p));
	int na = conta_a();
	int respostas = (ptr ? 1 : 0) + (srv ? 1 : 0) + (txt ? 1 : 0) + (a ? na : 0);
	int adicionais = (!ptr ? 1 : 0) + (!srv ? 1 : 0) + (!txt ? 1 : 0) + (!a ? na : 0);
	p16(p, 0);
	p16(p, 0x8400);
	p16(p, 0);
	p16(p, (uint16_t)respostas);
	p16(p, 0);
	p16(p, (uint16_t)adicionais);
	if (ptr) registro(p, T_PTR, ttl);
	if (srv) registro(p, T_SRV, ttl);
	if (txt) registro(p, T_TXT, ttl);
	if (a) registro(p, T_A, ttl);
	if (!ptr) registro(p, T_PTR, ttl);
	if (!srv) registro(p, T_SRV, ttl);
	if (!txt) registro(p, T_TXT, ttl);
	if (!a) registro(p, T_A, ttl);
}

/* para o grupo, saindo por cada interface (não se sabe por qual a pergunta veio) */
static void enviar_grupo(const struct pacote *p) {
	struct sockaddr_in destino = { .sin_family = AF_INET, .sin_port = htons(MDNS_PORTA), .sin_addr.s_addr = inet_addr(MDNS_GRUPO) };
	struct in_addr ends[8];
	int n = enderecos(ends, 8);
	for (int i = 0; i < n; i++) {
		setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, &ends[i], sizeof(ends[i]));
		if (sendto(fd, p->b, (size_t)p->n, MSG_NOSIGNAL, (struct sockaddr *)&destino, sizeof(destino)) < 0) {
		}
	}
}

static void anunciar(uint32_t ttl) {
	struct pacote p;
	montar(&p, ttl, true, true, true, true);
	if (!p.cheio) {
		enviar_grupo(&p);
	}
}

/* ---- leitura de pergunta ---- */

/* um nome (com ponteiros de compressão) em minúsculas, "a.b.c"; devolve o byte seguinte, ou -1 */
static int ler_nome(const uint8_t *b, int tam, int off, char *saida, size_t max) {
	size_t n = 0;
	int depois = -1, saltos = 0;
	for (;;) {
		if (off >= tam) {
			return -1;
		}
		int c = b[off];
		if ((c & 0xc0) == 0xc0) {
			if (off + 1 >= tam || ++saltos > 8) {
				return -1;
			}
			if (depois < 0) {
				depois = off + 2;
			}
			off = ((c & 0x3f) << 8) | b[off + 1];
			continue;
		}
		off++;
		if (c == 0) {
			break;
		}
		if (off + c > tam || n + (size_t)c + 2 > max) {
			return -1;
		}
		if (n) {
			saida[n++] = '.';
		}
		for (int k = 0; k < c; k++) {
			saida[n++] = (char)g_ascii_tolower((char)b[off + k]);
		}
		off += c;
	}
	saida[n] = '\0';
	return depois >= 0 ? depois : off;
}

static gboolean chegou(gint arquivo, GIOCondition cond, gpointer dados) {
	uint8_t b[1500];
	struct sockaddr_in de;
	socklen_t dl = sizeof(de);
	ssize_t tam = recvfrom(arquivo, b, sizeof(b), MSG_DONTWAIT, (struct sockaddr *)&de, &dl);
	if (tam < 12 || (b[2] & 0x80)) {
		return G_SOURCE_CONTINUE; /* curto demais, ou uma resposta (inclusive as nossas) */
	}
	int perguntas = (b[4] << 8) | b[5], off = 12;
	bool ptr = false, srv = false, txt = false, a = false;
	for (int i = 0; i < perguntas; i++) {
		char nome[256];
		off = ler_nome(b, (int)tam, off, nome, sizeof(nome));
		if (off < 0 || off + 4 > tam) {
			return G_SOURCE_CONTINUE;
		}
		int tipo = (b[off] << 8) | b[off + 1];
		off += 4;
		char *servico = g_ascii_strdown(nome_servico, -1), *instancia = g_ascii_strdown(nome_instancia, -1), *host = g_ascii_strdown(nome_host, -1);
		if (!strcmp(nome, servico) && (tipo == T_PTR || tipo == T_ANY)) {
			ptr = true;
		}
		if (!strcmp(nome, instancia)) {
			srv = srv || tipo == T_SRV || tipo == T_ANY;
			txt = txt || tipo == T_TXT || tipo == T_ANY;
		}
		if (!strcmp(nome, host) && (tipo == T_A || tipo == T_ANY)) {
			a = true;
		}
		g_free(servico);
		g_free(instancia);
		g_free(host);
	}
	if (!ptr && !srv && !txt && !a) {
		return G_SOURCE_CONTINUE;
	}
	struct pacote p;
	montar(&p, TTL_S, ptr, srv, txt, a);
	if (p.cheio) {
		return G_SOURCE_CONTINUE;
	}
	if (ntohs(de.sin_port) != MDNS_PORTA) {
		/* uma pergunta de fora da porta 5353 (ferramenta de teste, resolvedor simples): resposta direta */
		if (sendto(arquivo, p.b, (size_t)p.n, MSG_NOSIGNAL, (struct sockaddr *)&de, dl) < 0) {
		}
	} else {
		enviar_grupo(&p);
	}
	return G_SOURCE_CONTINUE;
}

static gboolean de_tempos_em_tempos(gpointer dados) {
	entrar_no_grupo(); /* uma interface nova (Wi-Fi que voltou) passa a ouvir */
	anunciar(TTL_S);
	return G_SOURCE_CONTINUE;
}

static gboolean segundo_anuncio(gpointer dados) {
	anunciar(TTL_S);
	return G_SOURCE_REMOVE;
}

bool mdns_anunciar(const char *tipo, int porta) {
	porta_servico = porta;
	char host[64];
	snprintf(host, sizeof(host), "%s", g_get_host_name());
	char *ponto = strchr(host, '.');
	if (ponto) {
		*ponto = '\0';
	}
	if (!host[0]) {
		snprintf(host, sizeof(host), "lumna");
	}
	snprintf(nome_servico, sizeof(nome_servico), "%s.local", tipo);
	snprintf(rotulo_instancia, sizeof(rotulo_instancia), "LUMNA %s", host);
	snprintf(nome_instancia, sizeof(nome_instancia), "%s.%s", rotulo_instancia, nome_servico);
	snprintf(nome_host, sizeof(nome_host), "%s.local", host);

	fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (fd < 0) {
		return false;
	}
	int um = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &um, sizeof(um));
	setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &um, sizeof(um));
	unsigned char ttl = 255;
	setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));
	struct sockaddr_in end = { .sin_family = AF_INET, .sin_port = htons(MDNS_PORTA), .sin_addr.s_addr = htonl(INADDR_ANY) };
	if (bind(fd, (struct sockaddr *)&end, sizeof(end)) < 0) {
		close(fd);
		fd = -1;
		return false;
	}
	entrar_no_grupo();
	g_unix_fd_add(fd, G_IO_IN, chegou, NULL);
	anunciar(TTL_S);
	g_timeout_add_seconds(1, segundo_anuncio, NULL);
	g_timeout_add_seconds(60, de_tempos_em_tempos, NULL);
	return true;
}
