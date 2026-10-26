/*
 * O som pelo libpulse (o pipewire-pulse responde a ele). Um pa_threaded_mainloop corre numa
 * linha à parte: a cada mudança assinada (saídas, apps, servidor) relê as três listas para um
 * rascunho e, quando as três chegam, publica o rascunho e escreve no eventfd de aviso.
 */
#define _GNU_SOURCE
#include "som.h"

#include <math.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

static struct {
	pa_threaded_mainloop *laco;
	pa_context *ctx;
	int aviso;
	pthread_mutex_t trava;
	struct som_estado publicado;
	struct som_estado rascunho;
	char padrao[128];
	char padrao_entrada[128];
	int faltam;    /* listas ainda a chegar nesta leitura */
	bool de_novo;  /* mudou durante uma leitura: lê outra vez no fim */
} s = { .aviso = -1, .trava = PTHREAD_MUTEX_INITIALIZER };

static void copiar(char *destino, size_t tamanho, const char *origem) {
	snprintf(destino, tamanho, "%s", origem ? origem : "");
}

static float media(const pa_cvolume *v) {
	return (float)pa_cvolume_avg(v) / PA_VOLUME_NORM;
}

static void avisar(void) {
	uint64_t um = 1;
	if (write(s.aviso, &um, sizeof(um)) < 0) {
		/* o contador cheio também acorda quem lê */
	}
}

static void ler_tudo(void);

static void lista_chegou(void) {
	if (--s.faltam > 0) {
		return;
	}
	for (int i = 0; i < s.rascunho.nsaidas; i++) {
		s.rascunho.saidas[i].padrao = !strcmp(s.rascunho.saidas[i].nome, s.padrao);
	}
	for (int i = 0; i < s.rascunho.nentradas; i++) {
		s.rascunho.entradas[i].padrao = !strcmp(s.rascunho.entradas[i].nome, s.padrao_entrada);
	}
	s.rascunho.ligado = true;
	pthread_mutex_lock(&s.trava);
	s.publicado = s.rascunho;
	pthread_mutex_unlock(&s.trava);
	avisar();
	if (s.de_novo) {
		s.de_novo = false;
		ler_tudo();
	}
}

static void servidor_cb(pa_context *c, const pa_server_info *info, void *dados) {
	if (info) {
		copiar(s.padrao, sizeof(s.padrao), info->default_sink_name);
		copiar(s.padrao_entrada, sizeof(s.padrao_entrada), info->default_source_name);
	}
	lista_chegou();
}

static void saida_cb(pa_context *c, const pa_sink_info *info, int fim, void *dados) {
	if (fim) {
		lista_chegou();
		return;
	}
	if (!info || s.rascunho.nsaidas >= SOM_MAX_SAIDAS) {
		return;
	}
	struct som_no *n = &s.rascunho.saidas[s.rascunho.nsaidas++];
	*n = (struct som_no){ .indice = info->index, .canais = info->volume.channels, .volume = media(&info->volume),
		.mudo = info->mute };
	copiar(n->nome, sizeof(n->nome), info->name);
	copiar(n->descricao, sizeof(n->descricao), info->description ? info->description : info->name);
	copiar(n->icone, sizeof(n->icone), pa_proplist_gets(info->proplist, PA_PROP_DEVICE_ICON_NAME));
}

static void entrada_cb(pa_context *c, const pa_source_info *info, int fim, void *dados) {
	if (fim) {
		lista_chegou();
		return;
	}
	/* o "monitor" de uma saída (o que está tocando) não é um microfone */
	if (!info || info->monitor_of_sink != PA_INVALID_INDEX || s.rascunho.nentradas >= SOM_MAX_SAIDAS) {
		return;
	}
	struct som_no *n = &s.rascunho.entradas[s.rascunho.nentradas++];
	*n = (struct som_no){ .indice = info->index, .canais = info->volume.channels, .volume = media(&info->volume),
		.mudo = info->mute };
	copiar(n->nome, sizeof(n->nome), info->name);
	copiar(n->descricao, sizeof(n->descricao), info->description ? info->description : info->name);
	copiar(n->icone, sizeof(n->icone), pa_proplist_gets(info->proplist, PA_PROP_DEVICE_ICON_NAME));
}

static void app_cb(pa_context *c, const pa_sink_input_info *info, int fim, void *dados) {
	if (fim) {
		lista_chegou();
		return;
	}
	if (!info || s.rascunho.napps >= SOM_MAX_APPS) {
		return;
	}
	/* os sons curtos do sistema (cliques, avisos) não são "uma app tocando" */
	const char *papel = pa_proplist_gets(info->proplist, PA_PROP_MEDIA_ROLE);
	if (papel && !strcmp(papel, "event")) {
		return;
	}
	struct som_no *n = &s.rascunho.apps[s.rascunho.napps++];
	*n = (struct som_no){ .indice = info->index, .canais = info->volume.channels, .volume = media(&info->volume),
		.mudo = info->mute };
	const char *nome = pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_NAME);
	copiar(n->nome, sizeof(n->nome), info->name);
	copiar(n->descricao, sizeof(n->descricao), nome ? nome : info->name);
	copiar(n->icone, sizeof(n->icone), pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_ICON_NAME));
	copiar(n->binario, sizeof(n->binario), pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_BINARY));
}

static void soltar(pa_operation *op) {
	if (op) {
		pa_operation_unref(op);
	}
}

static void ler_tudo(void) {
	if (s.faltam > 0) {
		s.de_novo = true;
		return;
	}
	memset(&s.rascunho, 0, sizeof(s.rascunho));
	s.faltam = 4;
	soltar(pa_context_get_server_info(s.ctx, servidor_cb, NULL));
	soltar(pa_context_get_sink_info_list(s.ctx, saida_cb, NULL));
	soltar(pa_context_get_sink_input_info_list(s.ctx, app_cb, NULL));
	soltar(pa_context_get_source_info_list(s.ctx, entrada_cb, NULL));
}

static void mudou_cb(pa_context *c, pa_subscription_event_type_t tipo, uint32_t indice, void *dados) {
	ler_tudo();
}

static void estado_cb(pa_context *c, void *dados) {
	switch (pa_context_get_state(c)) {
	case PA_CONTEXT_READY:
		pa_context_set_subscribe_callback(c, mudou_cb, NULL);
		soltar(pa_context_subscribe(c, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_SOURCE |
			PA_SUBSCRIPTION_MASK_SERVER, NULL, NULL));
		ler_tudo();
		break;
	case PA_CONTEXT_FAILED:
	case PA_CONTEXT_TERMINATED:
		/* o servidor caiu (ou não existe): avisa "desligado" e tenta outra vez daqui a pouco */
		pthread_mutex_lock(&s.trava);
		memset(&s.publicado, 0, sizeof(s.publicado));
		pthread_mutex_unlock(&s.trava);
		avisar();
		break;
	default:
		break;
	}
}

int som_iniciar(const char *nome_app) {
	s.aviso = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
	s.laco = pa_threaded_mainloop_new();
	if (s.aviso < 0 || !s.laco) {
		return -1;
	}
	pa_proplist *props = pa_proplist_new();
	pa_proplist_sets(props, PA_PROP_APPLICATION_NAME, nome_app ? nome_app : "LUMNA");
	pa_proplist_sets(props, PA_PROP_APPLICATION_ID, "os.lumna.gravity");
	s.ctx = pa_context_new_with_proplist(pa_threaded_mainloop_get_api(s.laco), NULL, props);
	pa_proplist_free(props);
	pa_context_set_state_callback(s.ctx, estado_cb, NULL);
	if (pa_context_connect(s.ctx, NULL, PA_CONTEXT_NOFAIL, NULL) < 0 || pa_threaded_mainloop_start(s.laco) < 0) {
		fprintf(stderr, "som: sem servidor de som (%s)\n", pa_strerror(pa_context_errno(s.ctx)));
	}
	return s.aviso;
}

void som_ler(struct som_estado *estado) {
	uint64_t n;
	while (read(s.aviso, &n, sizeof(n)) > 0) {
	}
	pthread_mutex_lock(&s.trava);
	*estado = s.publicado;
	pthread_mutex_unlock(&s.trava);
}

void som_parar(void) {
	if (s.laco) {
		pa_threaded_mainloop_stop(s.laco);
		pa_context_disconnect(s.ctx);
		pa_context_unref(s.ctx);
		pa_threaded_mainloop_free(s.laco);
		s.laco = NULL;
	}
}

/* ---- pedidos (da linha da interface: com a trava do laço) ---- */

static pa_cvolume volume_de(int canais, float volume) {
	pa_cvolume v;
	if (volume < 0) {
		volume = 0;
	}
	if (volume > 1.5f) {
		volume = 1.5f;
	}
	pa_cvolume_set(&v, canais > 0 ? canais : 2, (pa_volume_t)lroundf(volume * PA_VOLUME_NORM));
	return v;
}

static int canais_de(uint32_t indice, int tipo) { /* 0 saída, 1 app, 2 entrada */
	int canais = 2;
	pthread_mutex_lock(&s.trava);
	const struct som_no *lista = tipo == 1 ? s.publicado.apps : tipo == 2 ? s.publicado.entradas : s.publicado.saidas;
	int n = tipo == 1 ? s.publicado.napps : tipo == 2 ? s.publicado.nentradas : s.publicado.nsaidas;
	for (int i = 0; i < n; i++) {
		if (lista[i].indice == indice) {
			canais = lista[i].canais;
		}
	}
	pthread_mutex_unlock(&s.trava);
	return canais;
}

#define PEDIDO(chamada)                               \
	do {                                              \
		if (!s.laco) {                                \
			return;                                   \
		}                                             \
		pa_threaded_mainloop_lock(s.laco);            \
		soltar(chamada);                              \
		pa_threaded_mainloop_unlock(s.laco);          \
	} while (0)

void som_volume_saida(uint32_t indice, float volume) {
	pa_cvolume v = volume_de(canais_de(indice, 0), volume);
	PEDIDO(pa_context_set_sink_volume_by_index(s.ctx, indice, &v, NULL, NULL));
}

void som_mudo_saida(uint32_t indice, bool mudo) {
	PEDIDO(pa_context_set_sink_mute_by_index(s.ctx, indice, mudo, NULL, NULL));
}

void som_saida_padrao(const char *nome) {
	PEDIDO(pa_context_set_default_sink(s.ctx, nome, NULL, NULL));
}

void som_volume_entrada(uint32_t indice, float volume) {
	pa_cvolume v = volume_de(canais_de(indice, 2), volume);
	PEDIDO(pa_context_set_source_volume_by_index(s.ctx, indice, &v, NULL, NULL));
}

void som_mudo_entrada(uint32_t indice, bool mudo) {
	PEDIDO(pa_context_set_source_mute_by_index(s.ctx, indice, mudo, NULL, NULL));
}

void som_entrada_padrao(const char *nome) {
	PEDIDO(pa_context_set_default_source(s.ctx, nome, NULL, NULL));
}

void som_volume_app(uint32_t indice, float volume) {
	pa_cvolume v = volume_de(canais_de(indice, 1), volume);
	PEDIDO(pa_context_set_sink_input_volume(s.ctx, indice, &v, NULL, NULL));
}

void som_mudo_app(uint32_t indice, bool mudo) {
	PEDIDO(pa_context_set_sink_input_mute(s.ctx, indice, mudo, NULL, NULL));
}

const struct som_no *som_saida_atual(const struct som_estado *estado) {
	for (int i = 0; i < estado->nsaidas; i++) {
		if (estado->saidas[i].padrao) {
			return &estado->saidas[i];
		}
	}
	return estado->nsaidas ? &estado->saidas[0] : NULL;
}

const struct som_no *som_entrada_atual(const struct som_estado *estado) {
	for (int i = 0; i < estado->nentradas; i++) {
		if (estado->entradas[i].padrao) {
			return &estado->entradas[i];
		}
	}
	return estado->nentradas ? &estado->entradas[0] : NULL;
}
