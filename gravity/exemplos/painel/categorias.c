/*
 * As categorias que ainda são só o plano (PAINEL.md): cada seção diz o que vai ter. Quando uma
 * ganhar ajustes de verdade, passa para o seu arquivo (como a Aparência).
 */
#include "painel.h"

#define N(lista) (int)(sizeof(lista) / sizeof(lista[0]))

extern const pn_categoria pn_aparencia;
extern const pn_categoria pn_desenvolvedor;

/* ---- Geral ---- */

static const pn_secao rede[] = {
	{ "wifi", "wifi", "Wi-Fi", "As redes sem fio por perto e as que já conhece.", NULL,
		"Ligar e desligar;As redes por perto, com o sinal;Conectar com senha;As redes guardadas;Detalhes: IP, DNS e proxy" },
	{ "cabo", "ethernet", "Cabo", "A ligação por cabo de rede.", NULL, "Estado e velocidade;IP automático ou fixo;DNS" },
	{ "vpn", "shield", "VPN", "Redes privadas.", NULL, "Adicionar uma VPN (WireGuard, OpenVPN);Ligar e desligar" },
	{ "proxy", "globe", "Proxy", "Um proxy para a internet.", NULL, "Automático, manual ou nenhum" },
	{ "ponto", "signal", "Ponto de acesso", "Partilhar a internet deste aparelho.", NULL, "Nome e senha;Ligar e desligar" },
};
const pn_categoria pn_rede = { "rede", "wifi", "Rede e internet", "Wi-Fi, cabo e conexões", PN_GERAL, rede, N(rede) };

static const pn_secao bluetooth[] = {
	{ "dispositivos", "bluetooth", "Dispositivos", "Os aparelhos pareados e os que estão por perto.", NULL,
		"Ligar e desligar o Bluetooth;Parear, conectar e esquecer;A bateria dos fones e controles" },
	{ "visibilidade", "eye", "Nome e visibilidade", "Como os outros aparelhos veem este.", NULL,
		"O nome deste aparelho;Visível para parear" },
	{ "arquivos", "download", "Receber arquivos", "Arquivos mandados por Bluetooth.", NULL, "Aceitar e onde guardar" },
};
const pn_categoria pn_bluetooth = { "bluetooth", "bluetooth", "Bluetooth", "Dispositivos e acessórios", PN_GERAL, bluetooth,
	N(bluetooth) };

static const pn_secao aplicativos[] = {
	{ "instalados", "grid", "Instalados", "Os apps deste computador.", NULL,
		"A lista com o tamanho de cada um;Desinstalar;Abrir na Loja" },
	{ "padrao", "check", "Apps padrão", "Que app abre cada coisa.", NULL,
		"Navegador, música, vídeo, imagens, e-mail e terminal" },
	{ "permissoes", "shield", "Permissões", "O que cada app pode usar.", NULL, "Câmera, microfone, localização e arquivos" },
	{ "atualizacoes", "refresh", "Atualizações", "Manter os apps em dia.", NULL, "Procurar e instalar;Automático" },
};
const pn_categoria pn_aplicativos = { "aplicativos", "grid", "Aplicativos", "Instalação e permissões", PN_GERAL, aplicativos,
	N(aplicativos) };

static const pn_secao telas[] = {
	{ "monitores", "screens", "Monitores", "Resolução, escala e posição das telas.", NULL,
		"Resolução, escala, taxa e rotação;A posição de cada tela;Qual é a principal" },
	{ "luz", "moon", "Luz noturna", "Cores mais quentes à noite.", NULL, "Ligar, intensidade e horário" },
	{ "modos", "desktop", "Modos", "Desktop, Gaming e Mídia.", NULL, "O que cada modo mostra;O modo ao ligar" },
	{ "areas", "stack", "Áreas de trabalho", "As áreas e como se troca entre elas.", NULL, "Quantas;Atalhos e gestos" },
};
const pn_categoria pn_telas = { "telas", "desktop", "Telas e modos", "Monitores, Desktop, Gaming e Mídia", PN_GERAL, telas,
	N(telas) };

static const pn_secao som[] = {
	{ "saida", "speaker", "Saída", "Alto-falantes, fones e HDMI.", NULL, "A saída padrão;Volume e balanço" },
	{ "entrada", "mic", "Entrada", "Microfones.", NULL, "O microfone padrão;Volume e o nível ao vivo" },
	{ "apps", "sliders", "Apps", "O volume de cada app que está tocando.", NULL, "Volume e mudo por app;Para que saída vai" },
	{ "sons", "bell", "Sons do sistema", "Os sons dos avisos e das ações.", NULL, "Ligar e desligar;Volume dos avisos" },
};
const pn_categoria pn_som = { "som", "volume", "Som", "Saída, microfone e volume", PN_GERAL, som, N(som) };

static const pn_secao celular[] = {
	{ "parear", "phone", "Pareamento", "Ligar o seu celular a este computador.", NULL, "Parear com um código;Os celulares ligados" },
	{ "transferencia", "clipboard", "Área de transferência", "Copiar num e colar no outro.", NULL, "Sincronizar;O histórico" },
	{ "avisos", "bell", "Avisos do celular", "As notificações do celular aqui.", NULL, "Que apps;Responder daqui" },
	{ "arquivos", "folder", "Arquivos", "Mandar e receber arquivos.", NULL, "Onde guardar" },
};
const pn_categoria pn_celular = { "celular", "phone", "Celular Connect", "Conecte seu celular", PN_GERAL, celular, N(celular) };

static const pn_secao emma[] = {
	{ "voz", "mic", "Voz", "Como a Emma fala e ouve.", NULL, "A voz;A velocidade;O microfone" },
	{ "ativacao", "sparkle", "Oi Emma", "Chamar a Emma pela voz.", NULL, "Ligar e desligar;Sensibilidade" },
	{ "provedores", "globe", "Provedores e modelos", "De onde vem a inteligência da Emma.", NULL, "O provedor e o modelo;Chaves" },
	{ "privacidade", "shield", "Privacidade", "O que a Emma guarda.", NULL, "O histórico;Apagar as conversas" },
	{ "automacoes", "zap", "Automações", "Tarefas que a Emma faz sozinha.", NULL, "As automações e quando correm" },
};
const pn_categoria pn_emma = { "emma", "mic", "Emma", "Voz e preferências", PN_GERAL, emma, N(emma) };

/* ---- Inicialização ---- */

static const pn_secao inicializacao[] = {
	{ "entrar", "enter", "Ao entrar", "O que acontece quando você entra.", NULL,
		"O modo inicial: Desktop, Gaming ou Mídia;Reabrir as janelas da última sessão" },
	{ "apps", "grid", "Apps ao iniciar", "Os apps que abrem sozinhos.", NULL, "A lista (~/.config/autostart);Adicionar e remover" },
	{ "entrada", "lock", "Entrada", "Como se entra no LUMNA.", NULL, "Login automático, PIN ou senha;A tela de boas-vindas" },
	{ "fundo", "stack", "Em segundo plano", "Os serviços que correm por trás.", NULL, "Os serviços do usuário;Ligar e desligar" },
};
const pn_categoria pn_inicializacao = { "inicializacao", "power", "Inicialização", "Ao ligar e ao entrar", PN_INICIALIZACAO,
	inicializacao, N(inicializacao) };

/* ---- Sistema ---- */

static const pn_secao sobre[] = {
	{ "computador", "computer", "Este computador", "O que tem dentro.", NULL, "Modelo, processador e memória;A versão do LUMNA" },
	{ "atualizacoes", "refresh", "Atualizações do sistema", "Manter o LUMNA em dia.", NULL, "Procurar e instalar;O histórico" },
	{ "legal", "info", "Avisos legais", "As licenças.", NULL, "As licenças do LUMNA e dos programas" },
};
const pn_categoria pn_sobre = { "sobre", "info", "Sobre", "LUMNA OS 1.0.0 neste computador", PN_SISTEMA, sobre, N(sobre) };

static const pn_secao armazenamento[] = {
	{ "uso", "drive", "Uso do disco", "O espaço de cada pasta.", NULL, "O espaço livre;O que ocupa mais" },
	{ "limpeza", "trash", "Limpeza", "Liberar espaço.", NULL, "Lixeira, cache e pacotes antigos" },
	{ "discos", "usb", "Discos e pendrives", "Os discos ligados.", NULL, "Montar, ejetar e formatar" },
};
const pn_categoria pn_armazenamento = { "armazenamento", "drive", "Armazenamento", "Espaço usado e limpeza", PN_SISTEMA,
	armazenamento, N(armazenamento) };

static const pn_secao energia[] = {
	{ "perfil", "zap", "Perfil de energia", "Economia, equilibrado ou desempenho.", NULL, "O perfil;Automático na bateria" },
	{ "bateria", "battery", "Bateria", "A carga e a saúde da bateria.", NULL, "A porcentagem na barra;A saúde" },
	{ "suspensao", "moon", "Tela e suspensão", "Quando a tela apaga e o computador dorme.", NULL,
		"Desligar a tela depois de;Suspender depois de" },
	{ "botao", "power", "Botão de ligar", "O que o botão faz.", NULL, "Desligar, suspender ou perguntar" },
};
const pn_categoria pn_energia = { "energia", "battery", "Energia", "Bateria e suspensão", PN_SISTEMA, energia, N(energia) };

static const pn_secao teclado[] = {
	{ "teclado", "keyboard", "Teclado", "Layout e repetição.", NULL, "O layout;A repetição das teclas" },
	{ "atalhos", "keyboard", "Atalhos", "As teclas que fazem as coisas.", NULL, "Ver e trocar os atalhos" },
	{ "mouse", "mouse", "Mouse e touchpad", "O ponteiro.", NULL, "Velocidade;Toque para clicar;Rolagem natural" },
	{ "tela", "keyboard", "Teclado na tela", "Escrever sem teclado.", NULL, "Ligar;O tamanho" },
};
const pn_categoria pn_teclado = { "teclado", "keyboard", "Teclado e mouse", "Layout, atalhos e ponteiro", PN_SISTEMA, teclado,
	N(teclado) };

static const pn_secao data[] = {
	{ "hora", "clock", "Data e hora", "Acertar o relógio.", NULL, "Automática pela internet;Manual" },
	{ "fuso", "globe", "Fuso horário", "Onde você está.", NULL, "Automático;Escolher" },
	{ "formato", "text", "Formato", "Como as horas aparecem.", NULL, "24 horas;A data na barra" },
};
const pn_categoria pn_data = { "data", "clock", "Data e hora", "Fuso horário e formato", PN_SISTEMA, data, N(data) };

static const pn_secao idioma[] = {
	{ "idioma", "globe", "Idioma", "O idioma do sistema.", NULL, "O idioma;Os idiomas extra" },
	{ "formatos", "text", "Formatos regionais", "Números, moeda e datas.", NULL, "A região" },
	{ "entrada", "keyboard", "Métodos de entrada", "Escrever noutros alfabetos.", NULL, "Adicionar e trocar" },
};
const pn_categoria pn_idioma = { "idioma", "globe", "Idioma e região", "Português (Brasil)", PN_SISTEMA, idioma, N(idioma) };

static const pn_secao usuarios[] = {
	{ "conta", "user", "Sua conta", "O seu nome e a sua foto.", NULL, "Foto e nome" },
	{ "senha", "lock", "Senha e PIN", "Como você entra.", NULL, "Trocar a senha;O PIN" },
	{ "outros", "users", "Outros usuários", "As outras contas.", NULL, "Adicionar e remover" },
};
const pn_categoria pn_usuarios = { "usuarios", "users", "Usuários", "Contas e senhas", PN_SISTEMA, usuarios, N(usuarios) };

static const pn_secao privacidade[] = {
	{ "permissoes", "shield", "Permissões dos apps", "Câmera, microfone e localização.", NULL, "Por app" },
	{ "historico", "clock", "Histórico e recentes", "O que o LUMNA lembra.", NULL, "Limpar os recentes do lançador" },
	{ "diagnostico", "info", "Diagnóstico", "Relatórios de erro.", NULL, "Mandar ou não" },
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
