/*
 * A biblioteca de jogos do modo Gaming (lumna-jogos): lê o que está instalado na Steam (appmanifest), no Lutris (pga.db) e no Heroic
 * (Epic e GOG), mais os jogos avulsos que se adiciona à mão, e abre e desinstala cada um pelo gerenciador de origem.
 * O que é nosso fica em ~/.config/lumna/jogos/: avulsos.json (os avulsos) e props.json (favorito, última sessão, horas
 * e as propriedades de inicialização de cada jogo).
 *
 * Para testar, apontam para pastas falsas: $LUMNA_STEAM_DIR (a raiz da Steam), $LUMNA_LUTRIS_DB (o pga.db), $LUMNA_HEROIC_DIR (~/.config/heroic),
 * $XDG_CONFIG_HOME e o PATH para um steam, lutris e heroic de mentira.
 */
#ifndef LUMNA_JOGOS_H
#define LUMNA_JOGOS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JOGOS_MAX 400
#define JOGO_ENV_MAX 8

enum jogo_origem { ORIGEM_STEAM, ORIGEM_LUTRIS, ORIGEM_HEROIC, ORIGEM_AVULSO, ORIGEM_N };

struct jogo {
	char id[64];            /* "steam:1234", "lutris:slug", "heroic:legendary:app", "avulso:abc123" */
	char nome[128];
	enum jogo_origem origem;
	char ref[64];           /* o que o gerenciador de origem entende: appid, slug, "runner/app" */
	char pasta[400];        /* onde está instalado ("" se não souber) */
	char versao[48];
	int64_t tamanho;        /* bytes em disco (0 se não souber) */
	char capa[400];         /* a capa vertical (arquivo), "" se não há */
	char fundo[400];        /* o fundo largo (arquivo), "" se não há */
	int64_t ultima;         /* última sessão (time_t), 0 = nunca */
	double horas;           /* tempo de jogo */
	bool favorito;
	/* propriedades de inicialização: valem para os jogos avulsos e do Lutris/Heroic que a gente abre; a Steam guarda as dela */
	char exec[400], opcoes[300], dir[400];
	struct { char k[48], v[120]; } env[JOGO_ENV_MAX];
	int nenv;
	bool compat;            /* usar camada de compatibilidade (Proton/Wine) */
	bool gpu;               /* preferir a GPU dedicada */
	char janela[16];        /* "cheia" | "janela" */
};

/* a lista completa, em ordem de nome; devolve quantos */
int jogos_listar(struct jogo *lista, int max);
/* o nome da origem ("Steam", "Lutris", "Heroic", "Avulso") */
const char *jogos_origem_nome(enum jogo_origem o);
/* as propriedades de inicialização podem ser mudadas aqui? (a Steam e o Heroic guardam as próprias) */
bool jogos_props_editaveis(const struct jogo *j);

/* abre o jogo (sem esperar); anota a última sessão. erro recebe o motivo se falhar */
bool jogos_lancar(const struct jogo *j, char *erro, size_t tam);
/* a linha de comando que se abriria (para testes e para mostrar): g_free */
char *jogos_comando(const struct jogo *j);

bool jogos_favoritar(const struct jogo *j, bool favorito);
bool jogos_props_gravar(const struct jogo *j);
void jogos_props_restaurar(struct jogo *j);

/* adiciona um jogo avulso (executável, com nome); id recebe o id novo */
bool jogos_avulso_adicionar(const char *nome, const char *exec, const char *dir, char *id, size_t tam, char *erro, size_t erro_tam);

/* desinstala: Steam pede ao Steam (steam://uninstall); avulso tira da lista e, com apagar_arquivos, apaga a pasta dele;
 * Lutris e Heroic abrem o gerenciador (a desinstalação é dele). *saiu_da_lista diz se o jogo deixou a biblioteca agora. */
bool jogos_desinstalar(const struct jogo *j, bool apagar_arquivos, bool *saiu_da_lista, char *erro, size_t tam);
/* o que a desinstalação deste jogo faz, em uma frase (para a confirmação) */
const char *jogos_desinstalar_texto(const struct jogo *j);

#endif
