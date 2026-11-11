/*
 * O pré-setup (lumna-setup): o que o assistente de primeiro uso grava e como ele aplica cada escolha. Tudo que é de um usuário
 * fica em ~/.config/lumna (o Painel e os apps leem de lá); nada aqui toca em outro usuário. A interface está em exemplos/setup.c.
 *
 *   setup.ini     [setup] concluido=sim|nao  nao_mostrar=sim|nao  aberturas=N  perfil=diario|gaming|midia|dev
 *                 [email] endereco=  imap=  imap_porta=  smtp=  smtp_porta=
 *                 [nuvem] provedor=drive|onedrive|dropbox  pasta=~/Nuvem  segundo_plano=sim|nao  remoto=nome-no-rclone
 *   sugestoes.ini [perfil] id=...   [pacotes] lista=a b c   (as sugestões de apps do perfil; nada é instalado sozinho)
 *
 * Para testar, $XDG_CONFIG_HOME e $HOME apontam para pastas falsas e o PATH para timedatectl, rclone, localectl e pkexec falsos.
 */
#ifndef LUMNA_SETUP_H
#define LUMNA_SETUP_H

#include <stdbool.h>
#include <stddef.h>

struct setup_estado {
	bool concluido;     /* terminou (ou pulou tudo de propósito): nunca mais abre sozinho */
	bool nao_mostrar;   /* "não mostrar mais": nem abrindo, nem avisando */
	int aberturas;      /* quantas vezes abriu sozinho */
	char perfil[16];    /* "diario" | "gaming" | "midia" | "dev" ("" se não escolheu) */
};

void setup_ler(struct setup_estado *e);
bool setup_gravar(const struct setup_estado *e);
/* o que fazer no login: 0 nada, 1 abrir o assistente (primeira vez), 2 só lembrar por aviso (já abriu e não terminou) */
int setup_no_login(const struct setup_estado *e);

/* ---- o perfil de uso ---- */
enum { PERFIL_DIARIO, PERFIL_GAMING, PERFIL_MIDIA, PERFIL_DEV, PERFIL_N };
const char *setup_perfil_id(int p);
const char *setup_perfil_nome(int p);
const char *setup_perfil_descricao(int p);
const char *setup_perfil_icone(int p);
int setup_perfil_de_id(const char *id);   /* -1 se não for */
/* os pacotes sugeridos (separados por espaço) */
const char *setup_perfil_pacotes(int p);
/* o modo em que o sistema começa + as sugestões (sugestoes.ini); nada é instalado */
bool setup_perfil_aplicar(int p, char *erro, size_t tam);
/* as sugestões gravadas (para a Loja): devolve a lista separada por espaço ("" se não há); g_free */
char *setup_sugestoes_ler(void);

/* ---- região e hora ---- */
bool setup_hora_formato(bool h24);
/* timedatectl set-timezone (o polkit pede a senha); erro recebe o motivo */
bool setup_fuso(const char *fuso, char *erro, size_t tam);
/* o teclado do usuário (entrada.ini, que o Orbit aplica na hora): "br"+"abnt2", "us"+"", "us"+"intl" */
bool setup_teclado(const char *layout, const char *variante, char *erro, size_t tam);

/* ---- contas ---- */
bool setup_email_valido(const char *endereco);
/* guarda o e-mail (e, se vier, o servidor manual) para o app de e-mail usar depois */
bool setup_email_guardar(const char *endereco, const char *imap, int imap_porta, const char *smtp, int smtp_porta, char *erro, size_t tam);
void setup_email_ler(char *endereco, size_t tam);

/* a nuvem (rclone): provedores 0 Google Drive, 1 OneDrive, 2 Dropbox */
#define NUVEM_N 3
const char *setup_nuvem_nome(int provedor);
const char *setup_nuvem_tipo(int provedor);   /* o tipo do rclone: "drive", "onedrive", "dropbox" */
bool setup_rclone_existe(void);
/* cria o acesso (rclone config create; abre o navegador para entrar na conta) e a pasta local; bloqueia: numa linha à parte */
bool setup_nuvem_conectar(int provedor, const char *pasta, char *erro, size_t tam);
/* guarda a escolha e, com segundo_plano, põe o rclone mount para abrir ao entrar (e abre agora) */
bool setup_nuvem_aplicar(int provedor, const char *pasta, bool segundo_plano, char *erro, size_t tam);
/* o que está guardado: provedor (-1 nenhum) e pasta */
int setup_nuvem_ler(char *pasta, size_t tam, bool *segundo_plano);
/* "~/Nuvem" → o caminho de verdade (g_free) */
char *setup_pasta_expandir(const char *pasta);

/* ---- a Emma ---- */
/* liga/desliga a Emma no sistema: grava emma-sistema.ini e, se desligou, para o serviço e o chat; se ligou, sobe o serviço */
bool setup_emma_definir(bool ativa, char *erro, size_t tam);
/* "Responder a Oi Emma": guardado em setup.ini e mandado ao serviço se ele estiver de pé (senão vale na próxima vez) */
bool setup_emma_oi(bool ligar);
/* o provedor de IA: grava em emma/provedores.json (o formato do Painel > Emma > Provedores) com as rotas padrão; não apaga o que já existe */
bool setup_emma_provedor(const char *preset, const char *nome, const char *url, const char *chave, const char *modelo, char *erro, size_t tam);
/* os provedores prontos (os mesmos do Painel): n, e o que cada um traz */
int setup_emma_presets(const char **ids, const char **nomes, const char **urls, const char **modelos, int max);

#endif
