/*
 * Rede e nuvem do Atlas, sem desenho nenhum (testado em testes/atlas/testa_rede.c):
 *
 *   - compartilhamentos de rede (SMB, SFTP, FTP, WebDAV, NFS): o endereço digitado vira uma URI segura, a lista fica em rede.ini, e conectar/desconectar
 *     é o gvfs (GIO) numa thread própria; o gvfs pede a senha e a pessoa a guarda (ou não) no chaveiro dele. Nada é gravado por aqui.
 *   - contas na nuvem (Nextcloud, WebDAV, Google Drive, Dropbox, OneDrive): o rclone faz a sincronização de uma pasta local com a conta, com um
 *     rclone.conf só do Atlas (0600). Sem rclone no sistema a nuvem aparece como indisponível.
 *
 *   ~/.config/lumna/atlas/rede.ini     os compartilhamentos e as contas
 *   ~/.config/lumna/atlas/rclone.conf  as contas do rclone (a senha ofuscada pelo `rclone obscure`, como o rclone faz)
 */
#ifndef LUMNA_ARQUIVOS_REDE_H
#define LUMNA_ARQUIVOS_REDE_H

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ---- o endereço ---- */
enum ar_proto { AR_P_SMB, AR_P_SFTP, AR_P_FTP, AR_P_WEBDAV, AR_P_NFS, AR_PROTOS_N };
extern const char *const AR_PROTO_NOMES[AR_PROTOS_N];   /* "SMB", "SFTP", "FTP", "WebDAV", "NFS" */
extern const char *const AR_PROTO_DICAS[AR_PROTOS_N];   /* "smb://servidor/pasta" */

/* Transforma o que a pessoa digitou (com ou sem o esquema, `\\servidor\pasta` do Windows também) numa URI que o gvfs entende. Só os esquemas da lista
 * passam (nada de file://, javascript: e afins). *proto sai com o protocolo de fato (um esquema digitado manda sobre o menu). nome é um rótulo
 * sugerido ("servidor/pasta"). Falso, com a razão em erro, se não serve. */
bool ar_rede_normalizar(int proto_escolhido, const char *texto, int *proto, char *uri, size_t tam, char *nome, size_t tam_nome, char *erro, size_t tam_erro);

/* ---- a lista salva ---- */
#define AR_SHARES_MAX 32
#define AR_CONTAS_MAX 16
typedef struct ar_share {
	char nome[96];
	int proto;
	char uri[512];
	bool conectado_antes; /* estava conectado quando o Atlas fechou (para "Reconectar ao abrir") */
} ar_share;

typedef struct ar_conta {
	char nome[96];
	char tipo[16];        /* "nextcloud", "webdav", "drive", "dropbox", "onedrive" */
	char remote[48];      /* o nome no rclone.conf */
	char pasta_local[1024];
	bool segundo_plano;   /* sincronizar sozinho de tempos em tempos enquanto o Atlas está aberto */
	bool sincronizada;    /* já houve a primeira sincronização (a primeira usa --resync) */
	time_t ultima_sync;   /* 0: nunca */
	char ultimo_erro[256];
} ar_conta;

typedef struct ar_rede {
	ar_share shares[AR_SHARES_MAX];
	int n_shares;
	ar_conta contas[AR_CONTAS_MAX];
	int n_contas;
} ar_rede;

void ar_rede_ler(ar_rede *r);
bool ar_rede_gravar(const ar_rede *r);
int ar_rede_adicionar_share(ar_rede *r, const char *nome, int proto, const char *uri); /* índice; o mesmo endereço não repete; -1 se não cabe */
void ar_rede_remover_share(ar_rede *r, int i);

/* o gvfs tem esta URI montada? caminho recebe a pasta local (o gvfs-fuse) quando houver */
bool ar_share_montado(const char *uri, char *caminho, size_t tam);

/* ---- conectar e desconectar (thread própria; o gvfs pede a senha pela interface) ---- */
typedef struct ar_conexao ar_conexao;
enum ar_conexao_fase { AR_CX_CONECTANDO, AR_CX_PEDINDO_SENHA, AR_CX_PERGUNTANDO, AR_CX_OK, AR_CX_ERRO, AR_CX_CANCELADA };
typedef struct ar_conexao_estado {
	int fase;
	char mensagem[256];  /* PEDINDO_SENHA e PERGUNTANDO: o que o gvfs diz; ERRO: a razão */
	char opcoes[4][64];  /* PERGUNTANDO (a chave de um servidor SFTP desconhecido, por exemplo): as respostas possíveis */
	int n_opcoes;
	char usuario[96];    /* sugestões do gvfs para o diálogo */
	char dominio[96];
	bool quer_usuario, quer_dominio, pode_lembrar, anonimo_possivel;
} ar_conexao_estado;
/* conectar (silencioso: sem diálogo; se pedir senha, cancela) ou desconectar. acordar é chamado da thread quando o estado muda. */
ar_conexao *ar_share_conectar(const char *uri, bool silencioso, void (*acordar)(void *), void *dados);
ar_conexao *ar_share_desconectar(const char *uri, void (*acordar)(void *), void *dados);
void ar_conexao_estado_ler(ar_conexao *c, ar_conexao_estado *e);
/* responde ao PEDINDO_SENHA: ok=false cancela. lembrar: o gvfs guarda no chaveiro. anonimo: entra sem usuário. */
void ar_conexao_responder(ar_conexao *c, bool ok, const char *usuario, const char *senha, const char *dominio, bool lembrar, bool anonimo);
/* responde ao PERGUNTANDO com o índice da opção (-1: cancela) */
void ar_conexao_escolher(ar_conexao *c, int opcao);
void ar_conexao_liberar(ar_conexao *c); /* só depois de OK, ERRO ou CANCELADA (junta a thread) */
/* para os testes: no lugar do gvfs, a função faz o "montar" (pode pedir a senha por ar_conexao_pedir_senha) */
typedef bool (*ar_montar_falso)(ar_conexao *c, const char *uri, char *erro, size_t tam_erro);
void ar_rede_usar_montagem_falsa(ar_montar_falso f);
bool ar_conexao_pedir_senha(ar_conexao *c, const char *mensagem, char *usuario_saida, char *senha_saida, size_t tam);

/* ---- nuvem (rclone) ---- */
bool ar_nuvem_disponivel(void); /* tem rclone no PATH */
extern const char *const AR_NUVEM_TIPOS[5];       /* "nextcloud", "webdav", "drive", "dropbox", "onedrive" */
extern const char *const AR_NUVEM_NOMES[5];       /* "Nextcloud", "WebDAV", "Google Drive", "Dropbox", "OneDrive" */
/* Nextcloud/WebDAV: grava a conta no rclone.conf do Atlas (a senha vai ofuscada, nunca na linha de comando) e na lista. Devolve o índice da conta. */
int ar_nuvem_criar_webdav(ar_rede *r, const char *nome, const char *tipo, const char *url, const char *usuario, const char *senha, char *erro, size_t tam_erro);
/* Drive/Dropbox/OneDrive: o rclone abre o navegador para a pessoa entrar (thread; ar_sync_estado_ler diz quando acabou). */
typedef struct ar_sync ar_sync;
enum ar_sync_fase { AR_SY_ANDANDO, AR_SY_OK, AR_SY_ERRO };
typedef struct ar_sync_estado {
	int fase;
	char mensagem[256]; /* ERRO: o fim da saída do rclone */
} ar_sync_estado;
ar_sync *ar_nuvem_entrar(const char *remote, const char *tipo, void (*acordar)(void *), void *dados);
/* Sincroniza a pasta local com a conta (rclone bisync; a primeira vez com --resync). Quem chama marca ultima_sync/sincronizada na conta quando o estado vier OK. */
ar_sync *ar_nuvem_sincronizar(const ar_conta *c, void (*acordar)(void *), void *dados);
void ar_sync_estado_ler(ar_sync *s, ar_sync_estado *e);
void ar_sync_liberar(ar_sync *s);
/* o nome livre no rclone.conf para uma conta nova ("atlas-nextcloud", "atlas-nextcloud2"...) */
void ar_nuvem_nome_remote(const ar_rede *r, const char *nome, char *saida, size_t tam);
/* acrescenta uma conta (sem criar nada no rclone: o nome já existe lá, vindo de ar_nuvem_entrar) */
int ar_nuvem_adicionar_conta(ar_rede *r, const char *nome, const char *tipo, const char *remote);
/* tira a conta da lista e do rclone.conf (a pasta local e os arquivos nela ficam) */
void ar_nuvem_remover(ar_rede *r, int i);
char *ar_nuvem_conf(void); /* o caminho do rclone.conf (g_free) */

#endif
