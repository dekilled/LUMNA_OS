/*
 * Privacidade → Histórico e recentes e Diagnóstico: o que o LUMNA lembra e os relatórios (logs) deste computador.
 * Nada daqui é enviado a ninguém. O ajuste fica em ~/.config/lumna/privacidade.ini:
 *   lembrar_recentes = sim|nao   os arquivos recentes (o lançador e os apps que usam o recently-used.xbel)
 */
#ifndef LUMNA_PRIVACIDADE_H
#define LUMNA_PRIVACIDADE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LOGS_MAX 48

struct privacidade {
	bool lembrar_recentes;
};

void privacidade_ler(struct privacidade *p);
/* grava o ini e acerta o gtk-recent-files-enabled do GTK 3 e 4 (os apps GTK passam a seguir) */
bool privacidade_gravar(const struct privacidade *p);
/* esvazia o recently-used.xbel */
bool privacidade_limpar_recentes(void);
/* quantos arquivos recentes há agora */
int privacidade_recentes_n(void);
/* esvazia o histórico da área de transferência (no Orbit) */
bool privacidade_limpar_copiados(void);

struct log_arquivo {
	char nome[128];
	int64_t tamanho;
	int64_t quando; /* unix */
};
/* os logs de ~/.local/state/lumna (*.log), o mais novo primeiro */
int logs_listar(struct log_arquivo *lista, int max);
bool logs_apagar(const char *nome); /* "" ou NULL: todos */
char *logs_pasta(void);

#endif
