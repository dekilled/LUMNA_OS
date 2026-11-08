/*
 * As categorias que ainda são só o plano (PAINEL.md): cada seção diz o que vai ter. Quando uma
 * ganhar ajustes de verdade, passa para o seu arquivo (como a Aparência).
 */
#include "painel.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))

extern const pn_categoria pn_aparencia;
extern const pn_categoria pn_desenvolvedor;

/* Telas e modos → Modos (modos.c) */
void pn_modos_desenhar(void);
bool pn_modos_pendente(void);
void pn_modos_aplicar(void);
void pn_modos_descartar(void);
void pn_modos_restaurar(void);
void pn_modos_abrir(void);
void pn_areas_desenhar(void);
bool pn_areas_pendente(void);
void pn_areas_aplicar(void);
void pn_areas_descartar(void);
void pn_areas_restaurar(void);
void pn_areas_abrir(void);

/* Emma → Provedores e modelos (emma.c) */
void pn_emma_prov_desenhar(void);
void pn_emma_pers_desenhar(void);
bool pn_emma_pers_pendente(void);
void pn_emma_pers_aplicar(void);
void pn_emma_pers_descartar(void);
void pn_emma_pers_abrir(void);
void pn_emma_imp_desenhar(void);
void pn_emma_imp_abrir(void);
void pn_emma_ativ_desenhar(void);
void pn_emma_ativ_abrir(void);
void pn_emma_priv_desenhar(void);
void pn_emma_priv_abrir(void);
void pn_emma_auto_desenhar(void);
void pn_emma_auto_abrir(void);
void pn_emma_mcp_desenhar(void);
void pn_emma_mcp_abrir(void);
void pn_emma_rec_desenhar(void);
void pn_emma_rec_abrir(void);
void pn_emma_voz_desenhar(void);
void pn_emma_voz_abrir(void);
void pn_som_abrir(void);
void pn_som_saida_desenhar(void);
void pn_som_entrada_desenhar(void);
void pn_som_apps_desenhar(void);
void pn_cc_abrir(void);
void pn_cc_parear_desenhar(void);
void pn_cc_transf_desenhar(void);
void pn_cc_avisos_desenhar(void);
void pn_cc_arquivos_desenhar(void);
void pn_emma_prov_abrir(void);
void pn_emma_rota_desenhar(void);
bool pn_emma_rota_pendente(void);
void pn_emma_rota_aplicar(void);
void pn_emma_rota_descartar(void);

/* Sistema → Sobre (sobre.c) */
void pn_sobre_pc_desenhar(void);
void pn_sobre_pc_abrir(void);
void pn_sobre_atu_desenhar(void);
void pn_sobre_atu_abrir(void);
void pn_sobre_legal_desenhar(void);

/* Sistema → Data e hora (datahora.c) */
void pn_armaz_abrir(void);
void pn_armaz_uso_desenhar(void);
void pn_armaz_limpeza_desenhar(void);
void pn_armaz_discos_desenhar(void);
void pn_monitores_desenhar(void);
void pn_monitores_abrir(void);
bool pn_monitores_pendente(void);
void pn_monitores_aplicar(void);
void pn_monitores_descartar(void);
void pn_monitores_restaurar(void);
void pn_teclado_desenhar(void);
void pn_teclado_abrir(void);
bool pn_teclado_pendente(void);
void pn_teclado_aplicar(void);
void pn_teclado_descartar(void);
void pn_teclado_restaurar(void);
void pn_mouse_desenhar(void);
void pn_mouse_restaurar(void);
void pn_apps_desenhar(void);
void pn_apps_abrir(void);
void pn_apps_atu_desenhar(void);
void pn_hora_desenhar(void);
void pn_hora_abrir(void);
void pn_fuso_desenhar(void);
void pn_fuso_abrir(void);
bool pn_fuso_pendente(void);
void pn_fuso_aplicar(void);
void pn_fuso_descartar(void);
void pn_formato_desenhar(void);
void pn_formato_abrir(void);
bool pn_formato_pendente(void);
void pn_formato_aplicar(void);
void pn_formato_descartar(void);
void pn_formato_restaurar(void);

/* Inicialização (inicializacao.c) */
void pn_entrar_desenhar(void);
bool pn_entrar_pendente(void);
void pn_entrar_aplicar(void);
void pn_entrar_descartar(void);
void pn_entrar_restaurar(void);
void pn_entrar_abrir(void);
void pn_apps_ini_desenhar(void);
void pn_apps_ini_abrir(void);
void pn_entrada_desenhar(void);
void pn_entrada_abrir(void);
void pn_fundo_desenhar(void);
void pn_fundo_abrir(void);

/* Apps e privacidade (privacidade_pg.c) */
void pn_padrao_desenhar(void);
void pn_padrao_abrir(void);
bool pn_padrao_pendente(void);
void pn_padrao_aplicar(void);
void pn_padrao_descartar(void);
void pn_perm_abrir(void);
void pn_perm_por_permissao_desenhar(void);
void pn_perm_por_app_desenhar(void);
void pn_hist_abrir(void);
void pn_hist_desenhar(void);
void pn_diag_abrir(void);
void pn_diag_desenhar(void);

/* Sistema → Energia (energia.c) */
void pn_energia_abrir(void);
bool pn_energia_pendente(void);
void pn_energia_aplicar(void);
void pn_energia_descartar(void);
void pn_energia_restaurar(void);
void pn_perfil_desenhar(void);
void pn_bateria_desenhar(void);
void pn_suspensao_desenhar(void);
void pn_botao_desenhar(void);

/* Telas e modos → Luz noturna (luz.c) */
void pn_ktela_desenhar(void);
void pn_ktela_abrir(void);
bool pn_ktela_pendente(void);
void pn_ktela_aplicar(void);
void pn_ktela_descartar(void);
void pn_ktela_restaurar(void);
void pn_luz_desenhar(void);
void pn_luz_abrir(void);
bool pn_luz_pendente(void);
void pn_luz_aplicar(void);
void pn_luz_descartar(void);
void pn_luz_restaurar(void);

/* Som → Sons do sistema (sons.c) */
void pn_sons_desenhar(void);
void pn_sons_abrir(void);
bool pn_sons_pendente(void);
void pn_sons_aplicar(void);
void pn_sons_descartar(void);
void pn_sons_restaurar(void);

/* Sistema → Usuários (usuarios.c) */
void pn_usuarios_abrir(void);
void pn_conta_desenhar(void);
void pn_senha_desenhar(void);
void pn_outros_desenhar(void);

/* Sistema → Idioma e região (idioma.c) */
void pn_idioma_desenhar(void);
void pn_idioma_abrir(void);
bool pn_idioma_pendente(void);
void pn_idioma_aplicar(void);
void pn_idioma_descartar(void);
void pn_formatos_desenhar(void);
void pn_ime_desenhar(void);
void pn_ime_abrir(void);

/* Sistema → Teclado e mouse → Atalhos (atalhos.c) */
void pn_atalhos_desenhar(void);
bool pn_atalhos_pendente(void);
void pn_atalhos_aplicar(void);
void pn_atalhos_descartar(void);
void pn_atalhos_restaurar(void);
void pn_atalhos_abrir(void);

/* ---- Geral ---- */

/* Rede e internet e Bluetooth: rede.c e bluetooth.c */
extern const pn_categoria pn_rede;
extern const pn_categoria pn_bluetooth;

static const pn_secao aplicativos[] = {
	{ "instalados", "grid", "Instalados", "Os apps deste computador.", pn_apps_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_apps_abrir },
	{ "padrao", "check", "Apps padrão", "Que app abre cada coisa.", pn_padrao_desenhar, NULL, pn_padrao_pendente, pn_padrao_aplicar,
		pn_padrao_descartar, NULL, pn_padrao_abrir },
	{ "permissoes", "shield", "Permissões", "O que cada app pode usar.", pn_perm_por_permissao_desenhar, NULL, NULL, NULL, NULL, NULL, pn_perm_abrir },
	{ "atualizacoes", "refresh", "Atualizações", "Manter os apps em dia.", pn_apps_atu_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_apps_abrir },
};
const pn_categoria pn_aplicativos = { "aplicativos", "grid", "Aplicativos", "Instalação e permissões", PN_GERAL, aplicativos,
	N(aplicativos) };

static const pn_secao telas[] = {
	{ "monitores", "screens", "Monitores", "Resolução, escala e posição das telas.", pn_monitores_desenhar, NULL, pn_monitores_pendente,
		pn_monitores_aplicar, pn_monitores_descartar, pn_monitores_restaurar, pn_monitores_abrir },
	{ "luz", "moon", "Luz noturna", "Cores mais quentes à noite.", pn_luz_desenhar, NULL, pn_luz_pendente, pn_luz_aplicar, pn_luz_descartar,
		pn_luz_restaurar, pn_luz_abrir },
	{ "modos", "desktop", "Modos", "Desktop, Gaming e Mídia: o modo ao ligar e o que cada um faz.", pn_modos_desenhar, NULL, pn_modos_pendente,
		pn_modos_aplicar, pn_modos_descartar, pn_modos_restaurar, pn_modos_abrir },
	{ "areas", "stack", "Áreas de trabalho", "Quantas áreas o Desktop usa e como se troca entre elas.", pn_areas_desenhar, NULL, pn_areas_pendente,
		pn_areas_aplicar, pn_areas_descartar, pn_areas_restaurar, pn_areas_abrir },
};
const pn_categoria pn_telas = { "telas", "desktop", "Telas e modos", "Monitores, Desktop, Gaming e Mídia", PN_GERAL, telas,
	N(telas) };

static const pn_secao som[] = {
	{ "saida", "speaker", "Saída", "Alto-falantes, fones e HDMI.", pn_som_saida_desenhar, NULL, NULL, NULL, NULL, NULL, pn_som_abrir },
	{ "entrada", "mic", "Entrada", "Microfones.", pn_som_entrada_desenhar, NULL, NULL, NULL, NULL, NULL, pn_som_abrir },
	{ "apps", "sliders", "Apps", "O volume de cada app que está tocando.", pn_som_apps_desenhar, NULL, NULL, NULL, NULL, NULL, pn_som_abrir },
	{ "sons", "bell", "Sons do sistema", "Os sons dos avisos e das ações.", pn_sons_desenhar, NULL, pn_sons_pendente, pn_sons_aplicar, pn_sons_descartar,
		pn_sons_restaurar, pn_sons_abrir },
};
const pn_categoria pn_som = { "som", "volume", "Som", "Saída, microfone e volume", PN_GERAL, som, N(som) };

static const pn_secao celular[] = {
	{ "parear", "phone", "Pareamento", "Ligar o seu celular a este computador.", pn_cc_parear_desenhar, NULL, NULL, NULL, NULL, NULL, pn_cc_abrir },
	{ "transferencia", "clipboard", "Área de transferência", "Copiar num e colar no outro.", pn_cc_transf_desenhar, NULL, NULL, NULL, NULL, NULL, pn_cc_abrir },
	{ "avisos", "bell", "Avisos do celular", "As notificações do celular aqui.", pn_cc_avisos_desenhar, NULL, NULL, NULL, NULL, NULL, pn_cc_abrir },
	{ "arquivos", "folder", "Arquivos", "Mandar e receber arquivos.", pn_cc_arquivos_desenhar, NULL, NULL, NULL, NULL, NULL, pn_cc_abrir },
};
const pn_categoria pn_celular = { "celular", "phone", "LUMNA Connect", "Conecte seu celular", PN_GERAL, celular, N(celular) };

static const pn_secao emma[] = {
	{ "voz", "mic", "Voz", "Como a Emma fala e ouve.", pn_emma_voz_desenhar, NULL, NULL, NULL, NULL, NULL, pn_emma_voz_abrir },
	{ "ativacao", "sparkle", "Oi Emma", "Chamar a Emma pela voz.", pn_emma_ativ_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_emma_ativ_abrir },
	{ "provedores", "globe", "Provedores", "As IAs que a Emma usa: endereço, chave de API e modelos.", pn_emma_prov_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_emma_prov_abrir },
	{ "roteamento", "sliders", "Roteamento", "Qual provedor e modelo atende cada tarefa.", pn_emma_rota_desenhar, NULL, pn_emma_rota_pendente,
		pn_emma_rota_aplicar, pn_emma_rota_descartar, NULL, pn_emma_prov_abrir },
	{ "personalidade", "sparkle", "Personalidade", "Quem a Emma é e as regras dela.", pn_emma_pers_desenhar, NULL, pn_emma_pers_pendente, pn_emma_pers_aplicar,
		pn_emma_pers_descartar, NULL, pn_emma_pers_abrir },
	{ "recursos", "sliders", "Recursos", "Liga e desliga cada capacidade: imagens, visão, MCP, Oi Emma.", pn_emma_rec_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_emma_rec_abrir },
	{ "mcp", "zap", "Servidores MCP", "Ferramentas de fora que a Emma pode usar (arquivos, web, GitHub...).", pn_emma_mcp_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_emma_mcp_abrir },
	{ "importar", "download", "Importar do harness", "Traz conversas, memórias e a personalidade de um backup do harness.", pn_emma_imp_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_emma_imp_abrir },
	{ "privacidade", "shield", "Privacidade", "O que a Emma guarda.", pn_emma_priv_desenhar, NULL, NULL, NULL, NULL, NULL, pn_emma_priv_abrir },
	{ "automacoes", "zap", "Automações", "Tarefas que a Emma faz sozinha.", pn_emma_auto_desenhar, NULL, NULL, NULL, NULL, NULL, pn_emma_auto_abrir },
};
const pn_categoria pn_emma = { "emma", "mic", "Emma", "Voz e preferências", PN_GERAL, emma, N(emma) };

/* ---- Inicialização ---- */

static const pn_secao inicializacao[] = {
	{ "entrar", "enter", "Ao entrar", "O que acontece quando você entra.", pn_entrar_desenhar, NULL, pn_entrar_pendente, pn_entrar_aplicar,
		pn_entrar_descartar, pn_entrar_restaurar, pn_entrar_abrir },
	{ "apps", "grid", "Apps ao iniciar", "Os apps que abrem sozinhos.", pn_apps_ini_desenhar, NULL, NULL, NULL, NULL, NULL, pn_apps_ini_abrir },
	{ "entrada", "lock", "Entrada", "Como se entra no LUMNA.", pn_entrada_desenhar, NULL, NULL, NULL, NULL, NULL, pn_entrada_abrir },
	{ "fundo", "stack", "Em segundo plano", "Os serviços que correm por trás.", pn_fundo_desenhar, NULL, NULL, NULL, NULL, NULL, pn_fundo_abrir },
};
const pn_categoria pn_inicializacao = { "inicializacao", "power", "Inicialização", "Ao ligar e ao entrar", PN_INICIALIZACAO,
	inicializacao, N(inicializacao) };

/* ---- Sistema ---- */

static const pn_secao sobre[] = {
	{ "computador", "computer", "Este computador", "O que tem dentro.", pn_sobre_pc_desenhar, NULL, NULL, NULL, NULL, NULL, pn_sobre_pc_abrir },
	{ "atualizacoes", "refresh", "Atualizações do sistema", "Manter o LUMNA em dia.", pn_sobre_atu_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_sobre_atu_abrir },
	{ "legal", "info", "Avisos legais", "As licenças.", pn_sobre_legal_desenhar },
};
const pn_categoria pn_sobre = { "sobre", "info", "Sobre", "LUMNA OS 1.0.0 neste computador", PN_SISTEMA, sobre, N(sobre) };

static const pn_secao armazenamento[] = {
	{ "uso", "drive", "Uso do disco", "O que ocupa espaço, como no Android.", pn_armaz_uso_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_armaz_abrir },
	{ "limpeza", "trash", "Limpeza", "Liberar espaço.", pn_armaz_limpeza_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_armaz_abrir },
	{ "discos", "usb", "Discos e pendrives", "Os discos ligados.", pn_armaz_discos_desenhar, NULL, NULL, NULL, NULL, NULL,
		pn_armaz_abrir },
};
const pn_categoria pn_armazenamento = { "armazenamento", "drive", "Armazenamento", "Espaço usado e limpeza", PN_SISTEMA,
	armazenamento, N(armazenamento) };

static const pn_secao energia[] = {
	{ "perfil", "zap", "Perfil de energia", "Economia, equilibrado ou desempenho.", pn_perfil_desenhar, NULL, pn_energia_pendente, pn_energia_aplicar,
		pn_energia_descartar, pn_energia_restaurar, pn_energia_abrir },
	{ "bateria", "battery", "Bateria", "A carga e a saúde da bateria.", pn_bateria_desenhar, NULL, NULL, NULL, NULL, NULL, pn_energia_abrir },
	{ "suspensao", "moon", "Tela e suspensão", "Quando a tela apaga e o computador dorme.", pn_suspensao_desenhar, NULL, pn_energia_pendente,
		pn_energia_aplicar, pn_energia_descartar, pn_energia_restaurar, pn_energia_abrir },
	{ "botao", "power", "Botão de ligar", "O que o botão faz.", pn_botao_desenhar, NULL, pn_energia_pendente, pn_energia_aplicar, pn_energia_descartar,
		pn_energia_restaurar, pn_energia_abrir },
};
const pn_categoria pn_energia = { "energia", "battery", "Energia", "Bateria e suspensão", PN_SISTEMA, energia, N(energia) };

static const pn_secao teclado[] = {
	{ "teclado", "keyboard", "Teclado", "Layout e repetição.", pn_teclado_desenhar, NULL, pn_teclado_pendente, pn_teclado_aplicar,
		pn_teclado_descartar, pn_teclado_restaurar, pn_teclado_abrir },
	{ "atalhos", "keyboard", "Atalhos", "As teclas que fazem as coisas.", pn_atalhos_desenhar, NULL, pn_atalhos_pendente,
		pn_atalhos_aplicar, pn_atalhos_descartar, pn_atalhos_restaurar, pn_atalhos_abrir },
	{ "mouse", "mouse", "Mouse e touchpad", "Botões, velocidade e rolagem.", pn_mouse_desenhar, NULL, pn_teclado_pendente, pn_teclado_aplicar,
		pn_teclado_descartar, pn_mouse_restaurar, pn_teclado_abrir },
	{ "tela", "keyboard", "Teclado na tela", "Escrever sem teclado.", pn_ktela_desenhar, NULL, pn_ktela_pendente, pn_ktela_aplicar,
		pn_ktela_descartar, pn_ktela_restaurar, pn_ktela_abrir },
};
const pn_categoria pn_teclado = { "teclado", "keyboard", "Teclado e mouse", "Layout, atalhos e ponteiro", PN_SISTEMA, teclado,
	N(teclado) };

static const pn_secao data[] = {
	{ "hora", "clock", "Data e hora", "Acertar o relógio.", pn_hora_desenhar, NULL, NULL, NULL, NULL, NULL, pn_hora_abrir },
	{ "fuso", "globe", "Fuso horário", "Onde você está.", pn_fuso_desenhar, NULL, pn_fuso_pendente, pn_fuso_aplicar, pn_fuso_descartar, NULL,
		pn_fuso_abrir },
	{ "formato", "text", "Formato", "Como as horas aparecem.", pn_formato_desenhar, NULL, pn_formato_pendente, pn_formato_aplicar, pn_formato_descartar,
		pn_formato_restaurar, pn_formato_abrir },
};
const pn_categoria pn_data = { "data", "clock", "Data e hora", "Fuso horário e formato", PN_SISTEMA, data, N(data) };

static const pn_secao idioma[] = {
	{ "idioma", "globe", "Idioma", "O idioma do sistema.", pn_idioma_desenhar, NULL, pn_idioma_pendente, pn_idioma_aplicar, pn_idioma_descartar, NULL,
		pn_idioma_abrir },
	{ "formatos", "text", "Formatos regionais", "Números, moeda e datas.", pn_formatos_desenhar, NULL, pn_idioma_pendente, pn_idioma_aplicar,
		pn_idioma_descartar, NULL, pn_idioma_abrir },
	{ "entrada", "keyboard", "Métodos de entrada", "Escrever noutros alfabetos.", pn_ime_desenhar, NULL, NULL, NULL, NULL, NULL, pn_ime_abrir },
};
const pn_categoria pn_idioma = { "idioma", "globe", "Idioma e região", "Português (Brasil)", PN_SISTEMA, idioma, N(idioma) };

static const pn_secao usuarios[] = {
	{ "conta", "user", "Sua conta", "O seu nome e a sua foto.", pn_conta_desenhar, NULL, NULL, NULL, NULL, NULL, pn_usuarios_abrir },
	{ "senha", "lock", "Senha e PIN", "Como você entra.", pn_senha_desenhar, NULL, NULL, NULL, NULL, NULL, pn_usuarios_abrir },
	{ "outros", "users", "Outros usuários", "As outras contas.", pn_outros_desenhar, NULL, NULL, NULL, NULL, NULL, pn_usuarios_abrir },
};
const pn_categoria pn_usuarios = { "usuarios", "users", "Usuários", "Contas e senhas", PN_SISTEMA, usuarios, N(usuarios) };

static const pn_secao privacidade[] = {
	{ "permissoes", "shield", "Permissões dos apps", "Câmera, microfone e localização.", pn_perm_por_app_desenhar, NULL, NULL, NULL, NULL, NULL, pn_perm_abrir },
	{ "historico", "clock", "Histórico e recentes", "O que o LUMNA lembra.", pn_hist_desenhar, NULL, NULL, NULL, NULL, NULL, pn_hist_abrir },
	{ "diagnostico", "info", "Diagnóstico", "Relatórios de erro.", pn_diag_desenhar, NULL, NULL, NULL, NULL, NULL, pn_diag_abrir },
};
const pn_categoria pn_privacidade = { "privacidade", "shield", "Privacidade", "Permissões e segurança", PN_SISTEMA, privacidade,
	N(privacidade) };

/* a ordem da grade: as do Geral em duas colunas (esquerda e depois direita), como no esboço */
const pn_categoria *const pn_categorias[] = {
	&pn_aparencia, &pn_rede, &pn_bluetooth, &pn_aplicativos, &pn_telas, &pn_som, &pn_celular, &pn_emma,
	&pn_inicializacao,
	&pn_sobre, &pn_armazenamento, &pn_energia, &pn_teclado, &pn_data, &pn_idioma, &pn_usuarios, &pn_privacidade,
	&pn_desenvolvedor,
};
const int pn_ncategorias = N(pn_categorias);
