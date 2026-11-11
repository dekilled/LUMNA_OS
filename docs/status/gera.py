#!/usr/bin/env python3
"""Gera docs/status/progresso.html, a página de status do LUMNA OS ("Onde estamos").

Para atualizar: edite os DADOS abaixo (o que está feito, o que falta, o que precisa de teste) e rode
    python3 docs/status/gera.py
O resultado também vai para o artefato (a página publicada). Regras da casa:
  - "confirmado" só quando o Eric viu funcionando na máquina dele;
  - "testar" é o que está pronto e testado só em simulação (ou nem isso): sempre entra na lista "Para você testar";
  - nada de percentual inventado: as contagens vêm do código (o Painel) ou das listas abaixo.
"""
import html, os, sys
E = html.escape
AQUI = os.path.dirname(os.path.abspath(__file__))
HOJE = "9 de novembro de 2026"

# ---------------------------------------------------------------------------------------------------------- O PAINEL
# (contagem feita lendo gravity/exemplos/painel: página sem função de desenho = esqueleto)
painel = [
 ("Geral", [
  ("Aparência", ["Tema","Cores","Papel de parede","Dock","Barra superior","Tela de bloqueio","Tamanho e textos"], []),
  ("Rede e internet", ["Wi-Fi","Cabo","Adaptadores","Várias conexões","Proxy"], ["VPN","Ponto de acesso"]),
  ("Bluetooth", ["Dispositivos","Nome e visibilidade"], ["Receber arquivos"]),
  ("Aplicativos", ["Instalados","Atualizações","Apps padrão","Permissões"], []),
  ("Telas e modos", ["Monitores","Luz noturna","Modos","Áreas de trabalho"], []),
  ("Som", ["Saída","Entrada","Apps","Sons do sistema"], []),
  ("LUMNA Connect", ["Pareamento","Área de transferência","Avisos do celular","Arquivos"], []),
  ("Emma", ["Voz","Oi Emma","Provedores","Roteamento","Personalidade","Recursos","Servidores MCP","Importar do harness","Privacidade","Automações"], []),
 ]),
 ("Inicialização", [("Inicialização", ["Ao entrar","Apps ao iniciar","Entrada","Em segundo plano"], [])]),
 ("Sistema", [
  ("Sobre", ["Este computador","Atualizações do sistema","Avisos legais"], []),
  ("Armazenamento", ["Uso do disco","Limpeza","Discos e pendrives"], []),
  ("Energia", ["Perfil de energia","Bateria","Tela e suspensão","Botão de ligar"], []),
  ("Teclado e mouse", ["Teclado","Atalhos","Mouse e touchpad","Teclado na tela"], []),
  ("Data e hora", ["Data e hora","Fuso horário","Formato"], []),
  ("Idioma e região", ["Idioma","Formatos regionais","Métodos de entrada"], []),
  ("Usuários", ["Sua conta","Senha e PIN","Outros usuários"], []),
  ("Privacidade", ["Permissões dos apps","Histórico e recentes","Diagnóstico"], []),
  ("Desenvolvedor", ["Desenvolvedor"], []),
 ]),
]
tot_ok = sum(len(o) for _, cats in painel for _, o, s in cats)
tot_sk = sum(len(s) for _, cats in painel for _, o, s in cats)
tot = tot_ok + tot_sk
pct = round(100 * tot_ok / tot)
_sk = [x for _, cats in painel for _, o, sk in cats for x in sk]
FALTAM_PAINEL = ", ".join(_sk[:-1]) + " e " + _sk[-1] if len(_sk) > 1 else "".join(_sk)

# ---------------------------------------------------------------------------------------------- O QUE SE SABE POR ÁREA
# estado: conf = confirmado por você · sim = só simulado · and = em andamento · falta = não existe
areas = [
 ("ISO nova (Orbit + Gravity)", "conf", "Pacotes lumna-orbit e lumna-gravity, sessão do Orbit no login, instalador em Gravity (disco inteiro, UEFI e BIOS, GRUB), agente da senha de administrador, sessão live e atualizações pelo GitHub com snapshot e \"Desfazer\".",
  "Você confirmou: o live rodou num monitor de verdade, com ótimo desempenho, de um pendrive num notebook de 2011; o instalador instalou numa VM em BIOS e em UEFI sem erro; Atualizar pelo GitHub funciona; e no notebook o sistema roda liso, com o arrastar dos apps do Início para a dock, o Atlas e a identificação do monitor, da resolução e da Luz noturna. O NetworkManager não vinha na ISO: o build agora o instala e liga (falta você ver na próxima ISO)."),
 ("Desktop (Orbit + Gravity)", "and", "Janelas livres e encaixe, um modo por tela, barra de título do lado do Orbit, teclado na tela, copiar e colar, arrastar e soltar entre apps e o botão Sair da conta no menu de aplicativos.",
  "Você confirmou: o Orbit no live num monitor de verdade, janelas por monitor, tela cheia, o mouse no lugar, o monitor e a resolução identificados e a Luz noturna funcionando. Corrigido hoje, falta você ver: soltar um ícone da dock fora dela deixava o ícone preso ao mouse (o Orbit não mantinha o foco do ponteiro até soltar o botão). Falta ver em hardware: vários monitores e a tela apagando."),
 ("Atlas (explorador de arquivos)", "and", "Substitui o Thunar. Abas, grade e lista, operações com desfazer, lixeira, menu de contexto e \"...\" em cada item, Propriedades em abas, Início com pastas e partições, barra lateral que reordena arrastando, Rede e nuvem (gvfs e rclone) e arrastar arquivos entre pastas.",
  "Você confirmou: abre no lugar do Thunar, a janela arrasta, minimiza, maximiza e fecha, o Início abre as pastas com um clique e tem menu de contexto, e o tamanho das pastas, layout, barra de endereço, detalhes e barra lateral funcionam bem. O menu de contexto preso à janela já foi corrigido, falta você ver. Novo: pendrives ligados e não montados aparecem no Início com \"Montar\" e, no botão direito, \"Ejetar\" (provado com lsblk e udisks de mentira). Rede e nuvem e arrastar arquivos só em simulação."),
 ("Terminal (Pulse) com a Emma", "and", "Terminal em Gravity com abas, histórico e memória em SQLite. A Emma sugere a correção de um comando que falhou, traduz o que você quer fazer (Ctrl+E) e explica antes de executar um comando perigoso. Nada roda sem a sua confirmação.",
  "Você confirmou: a Emma devolve o comando certo para um pedido em português. Foi o único teste até agora. Correção de erro e explicação antes de executar: só com a Emma de mentira."),
 ("Captura de tela", "and", "Foto e vídeo (tela, área ou janela), editor de anotações, enviar para a Emma ou para o celular, gravação com áudio.",
  "Você confirmou: imagem e vídeo funcionam muito bem, a área de seleção é fluida e o menu flutuante volta na hora. Falta testar o áudio da gravação."),
 ("Teclado na tela", "sim", "Sobe de baixo e empurra as janelas, com acentos por toque longo, símbolos, emojis, vários dedos e a tela larga do PC. Abre sozinho nos apps de fora (input-method). O Painel escolhe quando aparece e o tamanho.",
  "Provado com um app de mentira e toques simulados. Falta ver no Firefox e no celular de verdade."),
 ("Painel de controle", "and", f"{tot_ok} de {tot} páginas prontas ({pct}%). Faltam {FALTAM_PAINEL}: estão no plano e ainda nem têm página. Novo: Armazenamento > Discos e pendrives monta pendrives sozinho (ao conectar e/ou ao entrar).",
  "Você confirmou a página da Emma (voz) e que o tema claro fica bonito. Corrigido: o texto ao passar o mouse no tema claro tinha pouco contraste (o acento ficou mais escuro). O resto foi testado com simulação e com o Orbit sem tela: em máquina de verdade ainda não: gama da Luz noturna, perfil automático da bateria e o idioma do sistema."),
 ("Emma", "conf", "Servidor, assistente, arquivos e comandos, código, perfis SFW e NSFW, imagens e visão, Oi Emma, histórias, MCP e a página Voz do Painel.",
  "Você confirmou: o PIN do perfil NSFW; baixar e cancelar o download do Whisper; a chave do Fish; testar a voz e ouvir; o ícone que mostra quando ela fala; o microfone (ela ouve e fala) e o Oi Emma. Falta o Pocket TTS, as imagens e o MCP."),
 ("Mídia", "and", "Início, seletor, controle pelo celular, nunca dois vídeos juntos e o YouTube em formato de TV.",
  "Você confirmou: extensão, login do Google, os botões do controle no YouTube de desktop e o controle do celular no modo Mídia. Falta o YouTube TV e a Netflix."),
 ("LUMNA Connect (PC e celular)", "and", "Pareamento, área de transferência, avisos, arquivos, controle, Emma por texto e voz, informações do PC, energia, agendamentos. O app Android tem cinco telas: Controle, Compartilhar, Emma, PC e Ajustes. Falta no PC: senhas e confirmações (polkit).",
  "Você confirmou, no Android e no OnePlus: a conexão com o PC, a área de transferência, o controle (inclusive na Mídia), enviar arquivos nos dois sentidos, a conversa com a Emma por texto e por voz e os avisos do celular no PC. Faltam os avisos do PC no celular (o PC não gerou nenhum), energia, agendamentos e informações do PC."),
 ("Telas e monitores", "sim", "Estender, por modo, espelhar, só a da esquerda ou da direita, topbar por monitor e o botão de tela cheia.",
  "Num KDE falso. Você confirmou só as janelas, a tela cheia e o mouse."),
 ("Modo Gaming", "and", "O modo, a ilha por tela e a biblioteca de jogos (Início, Biblioteca, Detalhes, Propriedades, Desinstalar) existem. Falta a Loja de jogos e as abas Sobre e Instalação.", "A biblioteca (2.270 a 2.291) e o controle (2.292 a 2.310)."),
 ("Vídeo", "sim", "O player de vídeo é parte da Galeria (libmpv dentro da janela, com os controles do LUMNA); o mpv separado continua como opção.", "Provado num Orbit sem tela, com o libmpv de verdade e som nulo. Falta você ver no PC: som, vídeos pesados, tela cheia."),
 ("Apps que ainda não existem", "falta", "bloco de notas, Code, Reminders, StoryTeller, Telefone e o navegador. O LUMNA antigo (Vue) está descartado por desempenho e espaço; o que ele tinha serve só de referência.", "—"),
]
lab = {"conf": ("Confirmado", "conf"), "sim": ("Só simulado", "sim"), "and": ("Em andamento", "and"), "falta": ("Não existe", "sk")}

# ------------------------------------------------------------------------------------------------------ O FIO DA MEADA
timeline = [
 ("A base", "Gravity (a biblioteca de interface), Orbit (o compositor) e as peças do desktop: papel de parede, topbar, dock, lançador, avisos, bloqueio e login.", "feito"),
 ("O Painel de controle", "Aparência, Rede, Bluetooth, Aplicativos, Sobre, Armazenamento, Data e hora, Teclado e mouse e Monitores; depois vieram Inicialização, Apps padrão, Permissões, Idioma, Energia, Usuários, Sons, Luz noturna e as da Emma (Privacidade e Automações).", "parou"),
 ("Modos e áreas de trabalho", "Desktop, Gaming e Mídia, as áreas de trabalho com animação, as ilhas por tela, trocar telas e o Início da Mídia.", "feito"),
 ("A Emma", "O serviço em segundo plano, o chat, o painel dela, arquivos e comandos, código na conversa, as histórias, perfis SFW e NSFW, imagens e visão, Oi Emma, MCP e a sessão contínua.", "feito"),
 ("Captura de tela", "Foto e vídeo com áudio, área, janela ou tela inteira, editor de anotações e enviar para a Emma ou para o celular.", "feito"),
 ("LUMNA Connect", "O dia a dia com o celular: pareamento, área de transferência, avisos e arquivos.", "feito"),
 ("Vários monitores", "Uma janela por monitor, mouse corrigido, topbar por monitor, botão de tela cheia, as disposições e o modo por tela. Distribuir os workspaces foi descartado por você.", "feito"),
 ("A Mídia de verdade", "Firefox Developer Edition escolhido sozinho, perfil por Firefox, extensão validada, voltar que sai do serviço, nunca dois vídeos juntos e o YouTube em formato de TV.", "feito"),
 ("A ISO nova", "Auditoria do shell antigo, pacotes do Orbit e do Gravity, agente da senha de administrador, instalador em Gravity, sessão live, GRUB em UEFI e BIOS, atualizações pelo GitHub com snapshot e a ISO de teste no canal teste-orbit. O live já rodou num monitor de verdade, de um pendrive.", "feito"),
 ("Os apps do sistema começam", "Teclado na tela, o terminal Pulse com a Emma por dentro, a barra de título do lado do Orbit e o Atlas no lugar do Thunar, com abas, Rede e nuvem e arrastar e soltar entre apps.", "feito"),
 ("Agora", "Instalar numa máquina real (BIOS e UEFI) e passar pelos apps novos. Depois: o menu de contexto do Atlas, o Gaming e os apps que faltam (Música, Navegador).", "agora"),
]

# ---------------------------------------------------------------------------------------- O QUE AINDA NÃO TEMOS (backlog)
# tag: feito · testar (pronto, falta você ver) · falta · ref (existe no LUMNA antigo: é portar) · pergunta (preciso de uma decisão sua)
backlog = [
 ("1. Modo Gaming", "A ilha por tela, a dock escondida e as janelas de jogo na tela certa já existem. Falta o que se vê e se usa.", [
  ("feito", "O modo, a ilha por tela, dock escondida e não perturbe", "Os jogos (Steam, gamescope, Heroic, Lutris) abrem na tela do Gaming."),
  ("ref", "Biblioteca de jogos em carrossel", "No LUMNA antigo: GameLibrary.vue, com capas, filtros (Todos, Recentes, Steam, Epic, GOG, Amazon, Lutris), controle, teclado e mouse."),
  ("testar", "Gaming: controle (gamepad) e Configurações", "Um serviço (lumna-gamepad) lê os controles USB e Bluetooth direto do kernel, sem capturá-los (o jogo continua lendo o dele): com o LUMNA Jogos em foco o direcional e o analógico viram as setas (e repetem), A confirma, B volta, LB e RB trocam de aba; com um jogo em foco ele não manda tecla nenhuma. O botão Guia, em qualquer janela, abre a biblioteca, vai ao Início, chama a Emma ou não faz nada. A aba Configurações (engrenagem do topo) tem: Controles (lista com bateria, testar vibração, apelido, ignorar, e parear/conectar/remover por Bluetooth), Ajustes (zona morta, vibração, trocar A e B), Botão Guia e Gaming (perfil de energia ao entrar, não perturbar e FPS com MangoHud). Provado só com controles de mentira (leitura com ASAN/UBSAN e o caminho inteiro controle de mentira, serviço, wtype de verdade, Orbit e a biblioteca); não há /dev/input aqui: o seu controle de verdade (botões, analógicos, vibração, bateria, Bluetooth) é o teste que falta. Roteiro: TESTES-ISO 2.292 a 2.310. Detalhes em docs/JOGOS.md."),
 ("testar", "Gaming: biblioteca de jogos (Início, Biblioteca, Detalhes, Propriedades, Desinstalar)", "Pelos seus esboços: o Início com o jogo em destaque e \"Seus jogos\", a Biblioteca com filtros (Todos, Favoritos, Steam, Lutris, Heroic, Avulsos) e \"Adicionar jogo\", os Detalhes com \"Sua jornada\" e o painel de instalação, as Propriedades de inicialização (executável, opções com %command%, variáveis, GPU) e a Desinstalação com confirmação. Lê a Steam (várias bibliotecas, capas e horas), o Lutris e o Heroic (Epic e GOG), e abre cada jogo pelo gerenciador dele; desinstalar pede à Steam, abre o Lutris/Heroic, e nos avulsos tira da lista (e só apaga os arquivos se você marcar). Abre sozinha quando uma tela entra em Gaming. Provada só num Orbit sem tela, com Steam, Lutris, Heroic e launchers de mentira (o serviço com ASAN/UBSAN limpos); falta você ver com seus jogos de verdade. Ainda não tem: Loja de jogos, as abas Sobre e Instalação, conquistas e o indicador \"Conectado\". Roteiro: TESTES-ISO 2.270 a 2.291. Detalhes em docs/JOGOS.md."),
  ("falta", "Ler as bibliotecas e abrir o jogo", "Steam (appmanifest, steam://rungameid), Heroic e Lutris; capas pela Steam ou SteamGridDB."),
  ("falta", "Gamepad como controle da interface", "Direcional, A e B no lugar das setas, OK e voltar."),
  ("falta", "Desempenho enquanto joga (ideia minha)", "Perfil de energia de desempenho e GameMode ao abrir um jogo; voltar ao normal ao sair."),
  ("pergunta", "O que o Gaming deve ter além da biblioteca?", "Por exemplo: ver o FPS, conversar com a Emma por cima do jogo, gravar um clipe com um atalho."),
 ]),
 ("2. A voz da Emma", "A Emma ouve, entende e fala pelo serviço, e você já confirmou quase tudo. Sobra o motor local de voz e a tela de bloqueio.", [
  ("feito", "O serviço fala e ouve", "Microfone, transcrição, resposta falada frase a frase e o Oi Emma funcionam na sua máquina. A voz é falar e ouvir, os dois."),
  ("feito", "Página Voz no Painel", "Você baixa e cancela o download do Whisper, configura a chave do Fish, testa a voz e ouve a Emma falar."),
  ("feito", "Modelos de voz", "Os modelos baixam, com cancelar."),
  ("feito", "O ícone da Emma mostra quando ela fala", "Confirmado por você."),
  ("testar", "Pocket TTS (a voz local)", "Você ainda não testou o motor local de fala, o Pocket TTS."),
  ("falta", "A voz da Emma na tela de bloqueio", "Consta no roteiro."),
 ]),
 ("3. Páginas do Painel de controle", "Quase tudo existe. Faltam só as pontas e duas páginas que o LUMNA antigo tinha.", [
  ("feito", "Teclado na tela", "A página existe: quando o teclado sobe (sozinho, sempre ou só quando você pedir) e o tamanho. O teclado em si é o lumna-teclado."),
 ("falta", "As pontas", "VPN, Ponto de acesso e Receber arquivos por Bluetooth: estão no plano, mas ainda nem têm página."),
 ("ref", "Páginas que o LUMNA antigo tinha", "Impressoras e Dados móveis (PrintersPage.vue e MobileDataPage.vue)."),
 ]),
 ("4. LUMNA Connect (PC e app Android)", "O app é um só, com o controle, arquivos, avisos, energia, agendamento e a Emma. Você já usou o essencial no Android e no OnePlus.", [
  ("feito", "Conexão e controle", "Conecta ao PC no celular Android e no OnePlus. O controle funciona, inclusive no modo Mídia."),
  ("feito", "Arquivos nos dois sentidos", "Do celular para o PC e do PC para o celular."),
  ("feito", "Área de transferência", "Compartilhada nos dois lados."),
  ("feito", "Conversa com a Emma", "Por texto e por voz."),
  ("feito", "Avisos do celular no PC", "Todas as notificações do celular apareceram no PC."),
  ("testar", "Avisos do PC no celular", "O PC não gerou nenhum aviso no teste, então não dá para dizer que funciona. Gere um (um lembrete, por exemplo) e confira. O caminho contrário funciona, então deve funcionar."),
  ("testar", "No app: controle de energia do PC", "Tela PC: bloquear, suspender, reiniciar e desligar (os três últimos pedem confirmação)."),
  ("testar", "No app: agendamento de tarefas", "Tela PC: lembrete, pedido para a Emma, bloquear, suspender, reiniciar ou desligar, a uma hora marcada e com repetição diária."),
  ("testar", "No app: manter conectado e informações do PC", "Um aviso fixo mantém a conexão com o app fechado, e a tela PC mostra as informações do computador."),
  ("falta", "Senhas e confirmações (polkit)", "O PC pede a senha ou uma confirmação e o celular responde. Pede um agente de autenticação do polkit; é um passo grande e separado."),
  ("falta", "Aprovar no celular as ações que a Emma pede", "O PC ainda não avisa o celular desse pedido."),
  ("falta", "Gerenciar o aparelho", "Ao conectar: modelo, versão, backup, atualizar e reinstalar ou recuperar (docs/PROXIMOS.md)."),
 ]),
 ("5. Apps do sistema", "A lista abaixo é o que você pediu. A etiqueta \"Ver no antigo\" diz que o LUMNA antigo (Vue, descartado) tem uma versão para olhar. Tudo novo é em Gravity (C).", [
  ("feito", "Atlas, o explorador de arquivos", "No lugar do Thunar; o essencial você já confirmou (ver o Resumo). Falta testar abas, Cor e Ícone da pasta, Propriedades, o \"...\", arrastar arquivos e Rede e nuvem. Ainda não tem: mover (em vez de copiar) ao arrastar, arrastar com o dedo, discos e USB, sincronização da nuvem com o Atlas fechado, busca dentro das subpastas e as opções avançadas do mockup (pausar transferências, transferências simultâneas, verificar cópias, cache de miniaturas, etiquetas)."),
  ("testar", "Atlas: o menu de contexto fora da janela", "Corrigido: o menu abre por cima da janela, passa dos limites dela e só rola se for mais alto que a tela (o Orbit prendia o popup na janela e o Atlas limitava a altura pela janela). Provado no Orbit sem tela; falta você ver no seu PC."),
  ("testar", "Terminal (Pulse)", "O comando por linguagem natural (Ctrl+E) você confirmou. Falta a correção de erro, a explicação antes de executar e a memória. Detalhes em docs/PULSE.md."),
  ("feito", "Captura de tela", "Imagem e vídeo confirmados por você. Falta o áudio da gravação."),
  ("testar", "Player de música (LUMNA Music)", "Pronto pelo seu esboço: biblioteca local com tags e capas, Álbuns, Artistas, Pastas, playlists e Favoritas, fila (arrastar para reordenar), letra sincronizada, busca, gapless, transição entre faixas, escolha da saída de áudio, MPRIS (tela de bloqueio, celular e Mídia controlam) e as configurações. Testado num Orbit sem tela com o mpv e o D-Bus de verdade (saída de áudio nula, ASAN/UBSAN sem relatório); falta você ouvir no PC: o som, sua biblioteca grande, a troca de saída e o toque. O Qobuz fica para depois. Roteiro: TESTES-ISO 2.180 a 2.199. Detalhes em docs/MUSICA.md."),
  ("testar", "Música em segundo plano, ícone na topbar e widget", "Fechar o LUMNA Music tocando esconde a janela e a música continua; um ícone de música na topbar (que acende tocando) abre um painel com os controles; e o Music vira um cartão (capa, título, botões) na área de trabalho, atrás das janelas (lumna-widgets). Também: abrir uma música pelo app padrão agora toca, só existe uma janela do Music (abrir de novo traz a mesma) e nunca sobra mpv invisível. Provado num Orbit sem tela (ASAN/UBSAN limpos no Music e no widget). Falta você ver no PC. Roteiro: TESTES-ISO 2.239 a 2.252. Detalhes em docs/MUSICA.md e docs/WIDGETS.md."),
  ("testar", "Galeria de fotos e vídeos (fases 1 a 4)", "Pronta pelos seus esboços: biblioteca por data, álbuns, busca, seleção, visualizador com zoom, importar, compartilhar e configurações; o editor de fotos (Ajustar, Recortar, Girar, Filtros), o editor de vídeos (faixa do tempo com recorte, prévia tocando com som, áudio, girar e Exportar cópia em MP4) e o player de vídeo dentro da janela (libmpv: buscar, volume, velocidade, legendas, áudio, tela cheia, MPRIS; o mpv separado continua como opção). As cópias saem ao lado do original, que nunca é alterado. Testada só num Orbit sem tela (com ASAN/UBSAN); falta você ver no PC: disco lento, toque, fotos e vídeos grandes. Roteiro: TESTES-ISO 2.139 a 2.179 e 2.200 a 2.214 (o player). Detalhes em docs/GALERIA.md."),
 ("testar", "Loja de apps (fase 1)", "Pelo seu esboço, expandida: Explorar com categorias, Pacotes com busca e filtros, Instalados, Atualizações e Configurações, com os repositórios do Arch e o AUR (paru ou yay, o que estiver instalado). Instalar e remover pedem a senha pelo LUMNA e mostram o andamento; do AUR sempre pergunta antes. As avaliações da comunidade LUMNA têm a interface pronta e ficam em \"Sem avaliações\" até existir um servidor. Provada só num Orbit sem tela, com pacman, AUR e paru de mentira (ASAN/UBSAN limpos); falta você usar com o pacman de verdade. Roteiro: TESTES-ISO 2.215 a 2.228. Detalhes em docs/LOJA.md."),
 ("testar", "Loja: Drivers e kernels (fase 2)", "Detecta a placa de vídeo, o processador, o kernel em uso e o gerenciador de boot, e diz de cada driver e kernel se serve a este computador e por quê (a geração da placa NVIDIA, o par driver e kernel, o espaço em /boot). O incompatível fica bloqueado (o modo avançado, em Configurações, libera). Kernel instala com os headers, e a Loja confere o /boot e o menu do boot depois (não mexe no boot). Provada só num Orbit sem tela, com lspci, pacman e /boot de mentira (ASAN/UBSAN limpos); falta você ver no PC de verdade. Roteiro: TESTES-ISO 2.229 a 2.238. Detalhes em docs/LOJA.md."),
 ("falta", "Instalador de drivers", "Detectar a placa de vídeo e instalar o driver certo pelo pacman, com a senha pelo polkit. Espera o seu esboço; em aberto: app próprio, página do Painel ou os dois."),
 ("testar", "Pré-setup (configuração inicial)", "Pronto pelos seus esboços: cinco etapas (Boas-vindas com perfil de uso, idioma, teclado, fuso e hora; Rede e som; E-mail e nuvem; Emma; LUMNA Connect), cada uma com Voltar, Pular e Fechar, e \"Não mostrar de novo\". Abre no primeiro login de cada usuário; se você fechar pela metade, só avisa por notificação. O perfil escolhe o modo em que o sistema começa e grava sugestões de apps; o e-mail fica guardado para o app de e-mail; a nuvem vira uma pasta (rclone); a Emma pode ser desligada no sistema inteiro (serviço, chat, atalhos e ícone da barra) e religada; o Connect gera o código e já liga os recursos escolhidos no celular que parear. Provado só num Orbit sem tela, com celular, Emma, timedatectl e rclone de mentira (ASAN/UBSAN limpos); falta você ver no PC de verdade. Ficou de fora, porque nada no sistema responde por isso ainda: QR code, medidor do microfone, Estilo de resposta, Acessibilidade e o botão de som. Roteiro: TESTES-ISO 2.253 a 2.269. Detalhes em docs/SETUP.md."),
  ("falta", "Bloco de notas", "Não existe em nenhum dos dois."),
  ("falta", "LUMNA Code", "Definido por você: a sua versão do VS Code, com a Emma integrada. Em C com o Gravity. Ainda não começou."),
  ("falta", "Reminders", "Definido por você: calendário e lembretes centralizados, que você e a Emma gerenciam. Em C com o Gravity. O agendamento do Connect já é a base dos avisos."),
  ("falta", "StoryTeller", "Definido por você: a sua engine de histórias, com a Emma integrada, em cima do repositório dekilled/world-forge-app. Ainda não comecei; vou ler o repositório antes de propor a estrutura."),
  ("falta", "O Gravity vira o \"Gravity framework\"", "Decidido por você: tudo novo é em C com o Gravity, no estilo do sistema, para construir qualquer interface dentro do LUMNA. Depois do sistema pronto, a interface do celular também vai para C, para todos os apps."),
  ("ref", "Outros que o antigo tinha", "Telefone (PhoneApp.vue). A Loja está na lista acima."),
 ("feito", "Combinado: a ordem e o jeito dos apps do sistema", "Galeria, música, vídeo, Loja, drivers e pré-configuração, nessa ordem, um por vez, completos e testados; você manda o esboço de cada app antes de eu começar o app. O plano está em docs/APPS-SISTEMA.md."),
  ("falta", "Outros em aberto", "Navegador, visualizador de imagens e PDF, calculadora, monitor do sistema. Diga quais entram."),
  ("feito", "Decidido: tudo novo é em Gravity (C)", "O shell Vue está descartado, por desempenho e espaço. O LUMNA antigo serve só de referência."),
 ]),
 ("6. Para o desktop ser a sessão do sistema", "O desktop já tem sessão no login, instalador e ISO de teste, e rodou num monitor de verdade. O que falta é instalar em máquina real e o que só hardware mostra.", [
  ("feito", "Boot do live num monitor de verdade", "Você rodou o live num notebook de 2011, de um pendrive, com muito boa performance. O maior risco do plano (tela preta no Orbit em DRM real) não se confirmou."),
  ("feito", "Instalador (numa VM)", "Instalou em BIOS e em UEFI numa VM sem erro. A sessão do Hyprland antigo vai junto só como emergência."),
  ("testar", "Instalar numa máquina real, em BIOS e em UEFI", "Você faz hoje."),
  ("feito", "Atualizar pelo GitHub", "Painel > Sobre > Atualizações: Atualizar tudo abre o Pulse e atualiza sem erro. Falta testar o Desfazer a última atualização (snapshot do Btrfs)."),
  ("testar", "NetworkManager na ISO", "Não vinha instalado (a rede ficava sem gerente). O build agora o instala e liga, no live e no sistema instalado, e desliga o systemd-networkd e o iwd do releng. Só valerá na próxima ISO (a de 7 de novembro é anterior)."),
  ("testar", "Teclado que abre sozinho nos apps de fora", "Pelo input-method do Orbit: provado com um app de mentira; falta ver no Firefox e nos outros."),
  ("falta", "A tela principal no Orbit real", "tela_principal só vale no aninhado; no real precisa apontar para o Odyssey."),
  ("falta", "Desligar um monitor de verdade", "No \"só um lado\" o código existe e nunca rodou em hardware."),
  ("falta", "Navegador", "Testar Epiphany, Firefox e Chromium no Wayland e avaliar o LUMNA Browser."),
  ("falta", "Instalador: o que ficou de fora", "Particionamento manual, hibernar, tipos e perfis de instalação, instalação automática, tema do GRUB e Secure Boot (docs/PROXIMOS.md). O pacote do repositório ainda não é assinado."),
  ("pergunta", "Quando apagar o shell antigo?", "A UI antiga (Hyprland e Tauri) continua no repositório e na ISO como sessão de emergência. Só tiro depois do seu \"pode apagar\", e antes eu marco o estado com a tag ui-antiga-hyprland."),
 ]),
]

# ------------------------------------------------------------------------------------- PARA VOCÊ TESTAR (em casa)
# Os itens novos vêm primeiro; os antigos (abaixo, sem confirmação sua) entram nos grupos pelo começo do título.
GRUPOS = ["A ISO nova e o instalador", "Apps novos", "Desktop, monitores e modos", "Mídia", "Emma", "Connect e celular"]
testar_novos = [
 ("A ISO nova e o instalador", "Instalar numa máquina real, em BIOS e em UEFI", "O live e o instalador numa VM você já confirmou. Falta instalar de verdade pelo atalho \"Instalar LUMNA OS\" (disco inteiro), reiniciar e entrar no sistema instalado, conferindo o GRUB e o Painel > Sobre. Se falhar, me mande o log (docs/TESTES-ISO.md, seção 6)."),
 ("A ISO nova e o instalador", "NetworkManager", "Na próxima ISO: `systemctl is-active NetworkManager` deve dizer active e o Painel > Rede listar as redes, no live e no sistema instalado."),
 ("A ISO nova e o instalador", "A janela de senha do administrador", "Painel > Sistema > Idioma, depois Aplicar: deve aparecer a janela do lumna-polkit. Senha errada, três vezes seguidas, recusa o pedido; Esc cancela."),
 ("A ISO nova e o instalador", "Desfazer a última atualização", "Painel > Sobre > Atualizações > Desfazer a última atualização volta o snapshot do Btrfs que foi tirado antes. Atualizar tudo você já confirmou."),
 ("A ISO nova e o instalador", "O que só o PC de verdade mostra", "A gama da Luz noturna, a tela apagando (DPMS), suspensão, botão de ligar, bateria, vários monitores e a placa AMD ou Intel (roteiro, seção 5)."),
 ("Apps novos", "Atlas: o que ainda não foi visto", "O menu de contexto agora passa da borda da janela (deixe a janela baixa e abra o menu perto do fim). Abas (Ctrl+T), copiar e colar, renomear, lixeira com desfazer, compactar e extrair; o \"...\" em cada item; os submenus Cor e Ícone da pasta (abrem ao lado do menu) e Propriedades em abas. O que você já confirmou (Início, tamanhos, layout, barra de endereço, detalhes, barra lateral) não precisa repetir."),
 ("Apps novos", "Atlas: arrastar", "Na barra lateral, arraste um atalho para reordenar. Arraste um arquivo (ou vários selecionados) para uma pasta, para um atalho da barra lateral ou para o fundo da lista: copia, e o original fica. Com o dedo, arrastar arquivos ainda não existe."),
 ("Apps novos", "Atlas: Rede e nuvem", "Configurações > Rede e nuvem: conectar a um servidor (smb://, sftp, ftp, WebDAV, nfs) e criar contas de nuvem pelo rclone (Nextcloud, WebDAV, Google Drive, Dropbox, OneDrive). Só foi provado com gvfs e rclone de mentira, então é aqui que mais pode falhar."),
 ("Desktop, monitores e modos", "Instabilidade do Atlas, o flicker e o CPU em repouso", "Você viu o Atlas cair ao montar e ao abrir pasta lotada, as pastas piscando e as miniaturas lentas. Achei e corrigi: (1) o Orbit reenviava o título da janela a todos os apps a cada commit e cada app redesenhava, 60 quadros por segundo parado (era o flicker e o CPU alto em todos os apps Gravity); (2) nome de arquivo que não é UTF-8 (pendrive antigo) derrubava o Atlas ao ordenar; (3) recarregar a lista no meio do desenho lia memória liberada; (4) ordenar 20 mil arquivos recalculava a chave a cada comparação; (5) a pasta mudando por fora relia tudo a cada aviso; (6) as miniaturas agora só pedem o que está à vista. Atualize e diga se ainda cai ou pisca. Segunda rodada (você ainda via cair ao montar e ao dar duplo clique, e os pontinhos coloridos das pastas piscando): os pontinhos eram a prévia do conteúdo da pasta, ordenada com uma comparação errada (a ordem mudava a cada leitura, por isso trocavam de cor) e lida na linha principal, uma pasta por vez; agora a ordem é a do nome, uma thread lê e o resultado fica guardado pela data da pasta. Fotos enormes (PNG, WebP) eram descodificadas inteiras por quatro threads ao mesmo tempo e podiam esgotar a memória; acima de 60 megapixels não há miniatura. Não consegui reproduzir a queda do duplo clique aqui (pastas de 4 mil itens, fotos, links em laço, FIFO, nomes inválidos, sob ASAN), então todo app Gravity agora deixa um relatório em ~/.cache/lumna/crash/ quando cai. Se cair de novo, me mande esse arquivo. Terceira rodada (miniaturas já resolvidas; ainda caía ao clicar em várias pastas do disco grande): achei duas causas. (1) O Atlas relia a pasta a cada 0,3 s para sempre, em toda pasta: ele tirava e punha o vigia de mudanças a cada recarga e o aviso do vigia antigo saindo parecia \"a pasta mudou\". (2) Selecionar uma pasta fazia o painel de detalhes somar o tamanho dela (até 20 mil arquivos) e contar os itens a cada quadro, tudo na linha principal; num NTFS de 1 TB isso deixa o app segundos sem ler o mouse, e o Orbit derruba o app que para de ler (provei com o log \"Data too big for buffer\"). Agora contagem, tamanho e a listagem da pasta rodam em threads, e o app mostra \"Abrindo…\" e \"calculando…\" no lugar. O nome do disco que passava do cartão também agora termina em \"…\"."),
 ("Desktop, monitores e modos", "Dock: hover deslocado na lista de janelas empilhadas", "Com 3 ou mais janelas do mesmo app, feche uma pela lista e depois passe o mouse nas outras: o hover e o clique devem acender a linha onde o mouse está. Era o ponteiro mapeado com a posição antiga da lista (calculado só quando o mouse entrava nela, e o compositor a reposiciona quando ela encolhe); vale para qualquer popup dos apps Gravity. Provei com o Orbit headless e três Atlas, e o teste falha sem a correção."),
 ("Desktop, monitores e modos", "Dock: app que some depois de ir à Mídia e voltar", "Abra um app, vá para Mídia e volte ao Desktop (devagar e depressa). Achei: a troca de modo desliza as janelas; um novo arranjo no meio da animação (a ativação da dock, o foco, a janela da Mídia saindo) a cancelava sem tirar o recorte dela, e a janela ficava ativa e invisível, o que parecia minimizado, e o clique no ícone na dock não fazia nada porque ela já era a ativa. Corrigi no Orbit; o teste faz trocas rápidas e confere a cor do pixel da janela na captura (sem a correção, falha). O Orbit agora diz em `orbit-msg janelas` se cada janela está visível e recortada."),
 ("Apps novos", "Pendrives: Montar no Atlas e montagem automática", "Ligue um pendrive: no Atlas > Início aparece um cartão \"não montado\" com o botão Montar (tocar monta e abre a pasta); no botão direito de um pendrive montado há Ejetar. Em Painel > Armazenamento > Discos e pendrives, ligue \"Ao conectar\" e/ou \"Ao entrar na sessão\": o serviço lumna-discos monta sozinho só os removíveis. Foi provado com lsblk e udisks de mentira; o monitor de volumes do gvfs de verdade é com você."),
 ("Apps novos", "Terminal Pulse: o resto", "O comando por linguagem natural você já viu funcionar. Falta rodar um comando que erra (gti status) e ver o cartão da Emma com a correção, e um rm ou dd, que mostra a explicação antes de executar. Precisa de um provedor em Painel > Emma > Provedores."),
 ("Apps novos", "Teclado na tela", "Toque num campo de texto no Firefox ou no Painel: o teclado sobe e as janelas se ajustam. Teste o toque longo nas letras, Shift duplo, símbolos, emojis, e Painel > Teclado e mouse > Teclado na tela (Sozinho, Sempre, Só quando eu pedir)."),
 ("Desktop, monitores e modos", "Soltar um ícone fora da dock", "Aperte num ícone fixo da dock, arraste para cima e solte fora dela: o arraste deve acabar na hora e o ícone voltar. Era o travamento que você viu. A causa estava no Orbit, então vale para qualquer arraste que sai do app (barra de rolagem, seleção)."),
 ("Desktop, monitores e modos", "Tema claro: o texto ao passar o mouse", "Painel > Aparência > Tema: claro. Passe o mouse nos itens do Atlas, do Painel e dos menus. O acento do lilás ficou mais escuro no claro e o texto suave mais forte. Se ainda achar baixo em algum lugar, me diga qual tela."),
 ("Desktop, monitores e modos", "Sair da conta", "Menu de aplicativos, botão Sair da conta (ao lado do Bloquear): pede confirmação e, ao confirmar, volta para a tela de login. Só provei que o Orbit encerra a sessão; a volta pelo greetd é sua."),
 ("Desktop, monitores e modos", "Arrastar um app do Início para a dock", "Com o mouse, aperte num app do menu e arraste até a dock; com o dedo, segure meio segundo. O menu fica transparente enquanto arrasta, o app fica fixo onde você soltar e o menu fecha. Soltar fora não fixa nada. Na dock, arrastar um ícone fixo reordena."),
 ("Desktop, monitores e modos", "Barra de título das janelas de fora", "Firefox, Blender e outros apps de fora ganham a barra de título que o Orbit desenha, no estilo dos apps nativos. Foi testada só com o foot e um mouse virtual."),
 ("Emma", "O servidor da Emma no notebook de 2011", "Ele caía com SIGILL no import do numpy (a CPU não tem SSE4.2) e o systemd o religava sem fim; o Painel só dizia que o servidor não respondia. Agora o servidor testa o import num processo filho e segue sem o numpy (o RAG calcula em Python puro) e a voz local recusa com uma mensagem clara. Atualize, confira o `systemctl --user status lumna-emma` e me diga se subiu. Pode haver outro módulo nativo que também não rode nessa CPU: o log agora mostra a pilha."),
 ("Emma", "Pocket TTS, a voz local", "Em Painel > Emma > Voz, escolha a fala no próprio PC (Pocket TTS), instale e use o \"Testar a voz da Emma\". Whisper, Fish, o microfone e o Oi Emma você já confirmou."),
 ("Emma", "Imagens e servidores MCP", "Gerar uma imagem e conectar um servidor MCP de verdade. Só foram testados com programas falsos."),
 ("Connect e celular", "Avisos do PC no celular", "Gere um aviso no PC (um lembrete, por exemplo) e veja se chega no celular. Os do celular no PC você já confirmou."),
 ("Connect e celular", "Telas PC do app: energia, agendamentos e informações", "Bloquear, suspender, reiniciar e desligar pelo celular (os três últimos pedem confirmação), um agendamento com hora marcada e repetição diária, e as informações do computador."),
 ("Apps novos", "Captura de tela com áudio", "Gravar com áudio do sistema e do microfone, e Enviar para o celular. Imagem e vídeo você já confirmou."),
]
GRUPO_DE = [  # (começo do título antigo, grupo)
 ("YouTube TV", "Mídia"), ("Nunca dois vídeos", "Mídia"), ("Netflix", "Mídia"), ("Setas nos serviços", "Mídia"), ("YouTube em modo TV", "Mídia"),
 ("Painel: Por modo", "Desktop, monitores e modos"), ("Passar janelas", "Desktop, monitores e modos"), ("Topbar por monitor", "Desktop, monitores e modos"),
 ("Seletor de modo", "Desktop, monitores e modos"), ("Modos: Super+Tab", "Desktop, monitores e modos"), ("Mídia: escolher o modo", "Desktop, monitores e modos"),
 ("Mídia: a tela", "Desktop, monitores e modos"), ("Modos por tela", "Desktop, monitores e modos"), ("Tela de bloqueio", "Desktop, monitores e modos"),
 ("Tela principal", "Desktop, monitores e modos"), ("Arrastar janela para", "Desktop, monitores e modos"), ("Trocar de modo", "Desktop, monitores e modos"),
 ("A voz da Emma", "Emma"), ("A Emma", "Emma"), ("Painel > Som", "Emma"), ("Painel > Som", "Emma"),
  ("Captura de tela", "Apps novos"),
]
testar_antigos = [
 ("Painel > Som", "Saída (a padrão, volume, mudo e um tom de teste), Entrada (microfones com o nível ao vivo) e Apps (volume por app). Se a voz da Emma falhar, o motivo aparece no terminal, no aviso e na página Voz."),
 ("YouTube TV (a interface de TV)", "As setas movem a seleção entre os vídeos, o OK abre o vídeo, o voltar volta para a lista e o play/pausa e o ±10 s funcionam dentro do vídeo. Me diga o que aparece na tela se algo falhar."),
 ("Nunca dois vídeos juntos", "Abra um vídeo, aperte Home, abra outro serviço e outro vídeo: só um deve tocar por vez. Voltar até a primeira página deve sair do serviço e pausar. (A aba fica viva: para retomar, aperte play.)"),
 ("Netflix e os outros serviços", "Abrir e tocar. É aqui que o Widevine (o decodificador de vídeo protegido) pode dar problema na primeira vez."),
 ("Painel: Por modo", "Telas e modos > Monitores > Como usar as telas > Por modo. Os seletores de cada tela agora abrem; escolha Mídia numa e Desktop noutra."),
 ("Passar janelas entre monitores", "Alt+← e Alt+→ levam a janela para o outro monitor. No \"só a da esquerda ou da direita\", as janelas da tela apagada devem ir para a que ficou, visíveis."),
 ("Topbar por monitor e o botão de tela cheia", "Painel > Aparência > Barra superior > Telas com a barra. O botão ao lado dos ícones sai da tela cheia e volta."),
 ("Seletor de modo: Super+Tab só abre, o seguinte troca", "O primeiro Super+Tab num monitor só abre o seletor (com o modo atual em destaque) e não troca nada; um Super+Tab logo em seguida (até uns 2 s) vai para o próximo modo, Shift+Tab para o anterior. Escolher o modo que o monitor já tem, pelo seletor, pelo dropdown da topbar ou pelo orbit-msg, não troca nada e o seletor mostra \"Este monitor já está no modo X\". Provado em Orbit headless. Sobre o atraso no clique do menu iniciar: aqui ele leva ~25 ms para o primeiro quadro; se continuar com atraso aí, rode LUMNA_TEMPO=1 lumna-lancador no terminal e me mande as linhas."),
 ("Modos: Super+Tab em qualquer tela e a Mídia fora da dock", "(1) O Super+Tab (e o seletor que aparece) agora alcança os três modos em qualquer monitor, inclusive o que outra tela já tem: escolher troca as duas telas de lugar (o modo que estava aqui vai para a outra, e o de lá vem para cá, com as janelas). Antes ele pulava o modo ocupado. (2) As janelas da Mídia e do Gaming não aparecem mais na dock nem no Alt+Tab, mesmo quando a tela delas volta para o Desktop (era o que aparecia como Mídia minimizada na barra). (3) O Super+M abre o seletor de serviços da tela em Mídia mesmo com o cursor no outro monitor. (4) O lançador (o menu do Super ou da dock) ficava sem abrir se um arquivo de controle antigo apontava para um número de processo que passou a ser de outro programa; agora ele confere se o processo é mesmo um lançador. Não consegui reproduzir o seu caso do lançador (aqui ele abre pelo Super e pelo clique, inclusive com a Mídia ligada), então se continuar sem abrir, rode pgrep -a lancador e cat $XDG_RUNTIME_DIR/lumna-lancador.pid e me mande."),
 ("Tela de bloqueio: vídeo tocando, widget de mídia e senha na tela do cursor", "(1) Vídeo tocando não tranca mais: o lumna-ocioso agora atende o D-Bus org.freedesktop.ScreenSaver e PowerManagement (o que Firefox, Chromium, VLC usam para pedir isso), além do idle-inhibit do Wayland que o Orbit já tratava. Enquanto alguém pede, não tranca; quando o último solta a contagem recomeça do zero (não tranca de repente ao fim do vídeo); quem pediu e fechou sem soltar deixa de contar. Para ver quem está segurando: orbit-msg ocioso (Wayland) e o log do lumna-ocioso (D-Bus). Se você roda o LUMNA dentro do KDE, o pedido do Firefox pode ir para o D-Bus do KDE (ele é o dono do nome) e o lumna-ocioso avisa isso no log; fora do KDE, vale. (2) O widget de mídia: o botão agora manda Play ou Pause conforme o estado (e cai para PlayPause se o player recusar), o ícone muda na hora e qualquer falha vai para o log com o nome do player. Não consegui reproduzir o seu caso (só tenho um player de mentira), então se ainda falhar me mande as linhas \"mpris:\" do log. (3) A interface do bloqueio (relógio, player, senha, PIN) agora aparece na tela onde está o cursor e segue o cursor entre os monitores; começa na tela do cursor. Provado em Orbit headless de 2 monitores com mouse virtual e tecla depois da troca; no seu KDE é você quem confirma."),
 ("Mídia: escolher o modo noutra tela troca as duas de lugar", "Com a Mídia no monitor 1, escolha Mídia no seletor da barra do monitor 2: a Mídia (e a janela dela) vai para o 2 e o Desktop do 2, com as janelas, vai para o 1. Vale também para o Gaming e entre Mídia e Gaming. É o mesmo efeito do \"Trocar as telas de lugar\", mas pelo próprio seletor de modo. Provado em Orbit headless de 3 monitores com janelas reais (o teste falha com o comportamento antigo); no seu KDE é você quem confirma."),
 ("Mídia: a tela \"Abrindo\" e o encaixe de janelas", "(1) A tela de carregamento do serviço (a capa) e o seletor de serviços agora abrem na tela que está em Mídia, e não na que o cursor estiver: era por isso que ela aparecia no outro monitor quando você passava o mouse para lá (a regra está no Orbit e também na Mídia). (2) O encaixe agora é por zonas, como no seu desenho: as laterais de cada monitor (56 px) são a zona. A janela atravessa por ela direto para o outro monitor e só entra no arranjo (meia tela) se você SOLTAR com o cursor na zona, ou com a borda da janela encostada na lateral (janela larga agarrada pelo meio chega antes do cursor). A prévia mostra as duas metades, a do alvo cheia. Em cima, uma faixa fina maximiza. Super+setas continuam encaixando. Ainda não tem o texto \"Solte para encaixar\", o ícone e as pontas arredondadas do desenho (o Orbit não desenha texto). Provado em Orbit headless com mouse virtual. Se o Mídia ainda abrir só num monitor, confira se o Orbit foi recompilado com o commit novo (orbit-msg modo mostra o modo de cada saída)."),
 ("Modos por tela: Mídia e Gaming só na tela pedida", "Com 2 ou 3 monitores: coloque a Mídia no monitor 1, 2 ou 3 (pelo seletor da barra daquele monitor): só ele vira Mídia e os outros seguem Desktop, estendidos entre si. Idem Gaming. Sem nenhum dos dois, todos ficam Desktop. Cada barra mostra o modo DA SUA tela (antes todas mostravam o da tela onde o cursor estava). A Mídia e o Gaming existem numa tela só por vez: pedir a Mídia noutro monitor leva a janela da Mídia junto e o monitor que a tinha volta ao Desktop (decisão minha: se preferir poder ter Mídia em duas telas ao mesmo tempo, me diga). Super+Tab pula o modo que outra tela já tem. Comprovado com Orbit headless de 3 monitores e janelas de verdade, e a barra por screenshot; o seu KDE de verdade é o que falta."),
 ("Modos por tela e arrastar janela entre monitores", "(1) A regra nova: o padrão é todas as telas em Desktop, estendido; abrir a Mídia ou o Gaming numa tela só muda aquela e cada tela mantém o seu modo até você trocar; sem nenhuma em modo especial o Desktop estendido é o que fica. A opção \"estender o modo às telas\" saiu do Painel, da barra e do Orbit. Em Painel > Monitores sobram Estender, Espelhar, Só a da esquerda e Só a da direita. (2) Arrastar a janela para o monitor do lado: o encaixe de meia tela só vale em borda que é o fim do desktop; o movimento relativo do KDE deixou de prender o cursor; e ao largar vale o monitor que cobre mais da janela. Reproduzi o seu sintoma (a janela encaixada na metade do monitor de origem) com Alt e mouse virtual, mas o KDE de verdade só você tem."),
 ("A voz da Emma cortando no meio (\"o tocador saiu com erro 1\")", "Reproduzi: numa resposta longa as frases geradas adiantadas disputavam 4 nomes de arquivo e uma apagava o áudio da outra. Agora cada frase tem o seu arquivo e só duas ficam prontas à espera. Peça algo que ela responda com um texto bem comprido e ouça até o fim. Se o erro voltar com uma fala curta, me mande o que aparece."),
 ("A Emma no chat: voz, configurações e setas", "(1) Com \"Ler as respostas em voz alta\" ligado em Painel > Emma > Voz, o chat do app e o assistente também falam (antes só falava quando você falava com ela; a conferência é a cada 10 s). (2) Peça \"abre as suas configurações\": o nome da seção escrito de qualquer jeito (maiúsculas, acento, espaço) agora vale, e a lista que ela conhece inclui Emma, Som e Celular. (3) $\\rightarrow$ e similares viram → na tela e na fala (e o prompt manda não usar LaTeX)."),
 ("Tela principal", "Painel > Telas e modos > Monitores: o interruptor \"Tela principal\" em cada tela. O papel de parede \"Tela principal\" agora segue a função, não o número do monitor (grava como @principal): marque a outra e confira se o papel de parede, a barra e a dock \"só na principal\" mudam sozinhos de tela (os apps reabrem em ~2 s)."),
 ("Arrastar janela para o outro monitor", "Arrastar pela barra de título direto até o monitor do lado, sem soltar no meio. Só reproduzi com um KDE de mentira; no seu KDE de verdade é o que vale."),
 ("Trocar de modo sem piscar", "Mídia <-> Desktop pelo celular, com os dois monitores: a janela deve deslizar só no monitor dela, sem aparecer no do lado."),
 ("Setas nos serviços (Netflix, Prime, Disney...)", "As setas agora movem um contorno violeta entre os itens da página e o OK clica; sem item na direção, a seta rola a página. Dentro de um vídeo em tela cheia as setas voltam a ser do player."),
 ("YouTube em modo TV", "O servidor entrega a interface de TV a quem manda o User-Agent de TV (conferi), então a extensão deveria bastar. Agora, se a aba cair no YouTube de mouse (depois do login, por exemplo), ela volta sozinha para o youtube.com/tv. Se ainda abrir em formato desktop, me diga a URL que aparece."),
]

# ------------------------------------------------------------------------------------------------- O QUE FOI TESTADO
confirmado = [
 "O live da ISO nova rodando num monitor de verdade, de um pendrive num notebook de 2011, com muito boa performance",
 "O instalador numa VM, em BIOS e em UEFI, sem erro",
 "Atualizar pelo GitHub (abre o Pulse e atualiza sem erro)",
 "No notebook: o LUMNA roda liso, o arrastar de um app do Início para a dock funciona, o Atlas \"em geral tudo funcionando muito bem\", o monitor e a resolução são identificados e a Luz noturna funciona",
 "O tema claro (fica bacana)",
 "A voz da Emma: baixar e cancelar o download do Whisper, a chave do Fish, testar a voz e ouvir, o ícone que mostra quando ela fala, o microfone (ela ouve e fala) e o Oi Emma",
 "O LUMNA Connect no Android e no OnePlus: conexão, área de transferência, controle (inclusive na Mídia), arquivos nos dois sentidos, conversa com a Emma por texto e voz e os avisos do celular no PC",
 "O Atlas no lugar do Thunar: arrastar, minimizar, maximizar e fechar a janela, menu de contexto, o Início (um clique abre a pasta), tamanhos, layout, barra de endereço, detalhes e barra lateral",
 "O Pulse: a Emma sugere o comando certo para um pedido em português (Ctrl+E)",
 "A captura de tela: imagem e vídeo, a área de seleção e o menu flutuante (o áudio da gravação não foi testado)",
 "O perfil NSFW com o PIN da barra",
 "O Orbit aninhado com uma janela por monitor, a tela cheia e o mouse no lugar certo",
 "A Mídia no YouTube de desktop: a extensão no Developer Edition, o login do Google e os botões do controle (play, pausa, ±10 s, início, voltar, menu)",
 "A conta ligada no YouTube TV (a interface de TV abriu)",
 "A Mídia do LUMNA antigo, no OnePlus, controlando o PC novo (funciona muito bem)",
 "Mover janelas entre as telas (Alt+setas), um modo por monitor sem as janelas passarem de um para o outro e a troca de modo pelo celular",
]
simulado = [
 "O Orbit com vários monitores de verdade, a gama da Luz noturna, a tela apagando e a bateria (o live rodou, mas você só viu um monitor)",
 "O instalador numa máquina real: só numa VM",
 "O Atlas: Rede e nuvem só com gvfs e rclone de mentira; arrastar arquivos, abas, Cor e Ícone e Propriedades com toques e teclas simulados no Orbit sem tela",
 "O Pulse: a correção de erro, a explicação antes de executar e a memória, com a Emma de mentira",
 "O teclado na tela (um app de mentira com text-input e toques simulados)",
 "Arrastar e soltar entre apps (Início para a dock, dock e Atlas) com o mouse e o dedo simulados; o botão Sair da conta (provado que o Orbit encerra; a volta ao login não)",
 "A barra de título do Orbit nas janelas de fora (foot e mouse virtual)",
 "O Connect: avisos do PC no celular, informações do PC, energia e agendamentos (só com um celular falso)",
 "A topbar por monitor e o botão de tela cheia",
 "Espelhar, só a da esquerda ou da direita e o modo por tela",
 "Os seletores de modo do Painel e passar janelas entre monitores (testei com um mouse e um falso KDE)",
 "Nunca dois vídeos juntos (testei com o Firefox ESR e vídeos locais)",
 "Imagens da Emma, servidores MCP e o Pocket TTS",
 "A gravação de tela com áudio",
]

proximos = [
 ("Instalar numa máquina real, em BIOS e em UEFI", "Você faz hoje. O que falhar, me mande com o log (Painel > Sistema > Diagnóstico junta os relatórios)."),
 ("Ver as correções de hoje no notebook", "Dock (soltar fora), tema claro, pendrives no Atlas e a montagem automática, depois de atualizar pelo Painel. O NetworkManager só na próxima ISO."),
 ("Passar pelos apps novos", "O que sobrou da lista \"Para você testar\": Atlas (abas, Cor e Ícone, Propriedades, arrastar, Rede e nuvem), Pulse, teclado na tela, Sair da conta e o arrastar e soltar. O que falhar eu corrijo antes de começar coisa nova."),
 ("Testar a Mídia, os avisos do PC e a captura de áudio", "O YouTube TV, a Netflix, um aviso do PC chegando no celular e a gravação com áudio."),
 ("O Início do Gaming (a biblioteca)", "É a maior parte visível que falta e o antigo já tem a referência."),
 ("Senhas e confirmações do PC no celular (polkit)", "O último pedaço do Connect completo."),
 ("Os apps que faltam, um por vez", "Navegador primeiro; os que você ainda não descreveu esperam a sua resposta."),
 ("Depois da ISO validada: tirar o shell antigo", "Só com o seu \"pode apagar\". Depois vem a interface do celular (um desenho juntos antes de qualquer código)."),
]

# --------------------------------------------------------------------------------------------------------------- HTML
def painel_html():
    out = []
    for grupo, cats in painel:
        out.append(f'<h3 class="grp">{E(grupo)}</h3><div class="cats">')
        for nome, ok, sk in cats:
            n = len(ok) + len(sk); w = round(100 * len(ok) / n) if n else 0
            chips = "".join(f'<li class="chip ok">{E(x)}</li>' for x in ok) + "".join(f'<li class="chip sk">{E(x)}</li>' for x in sk)
            estado = "completa" if not sk else ("só esqueleto" if not ok else "parcial")
            out.append(f'<article class="cat"><header><h4>{E(nome)}</h4><span class="cnt">{len(ok)}/{n}</span></header><div class="bar" role="img" aria-label="{len(ok)} de {n} páginas prontas"><i style="width:{w}%"></i></div><p class="est">{estado}</p><ul class="chips">{chips}</ul></article>')
        out.append('</div>')
    return "\n".join(out)

cards = "".join(f'<article class="card"><span class="tag {lab[st][1]}">{lab[st][0]}</span><h3>{E(t)}</h3><p>{E(d)}</p><p class="test"><b>Teste:</b> {E(tst)}</p></article>' for t, st, d, tst in areas)
tl = "".join(f'<li class="{k}"><div class="dot"></div><div><h3>{E(t)}</h3><p>{E(d)}</p></div></li>' for t, d, k in timeline)

TAGS = {"feito": ("Feito", "conf"), "testar": ("Falta testar", "sim"), "falta": ("Falta", "sk"), "ref": ("Ver no antigo", "and"), "pergunta": ("Pergunta", "perg")}
def backlog_html():
    out = []
    for titulo, resumo, itens in backlog:
        n_f = sum(1 for t, _, _ in itens if t == "feito"); n_p = sum(1 for t, _, _ in itens if t == "pergunta")
        li = "".join(f'<li><span class="tg {TAGS[t][1]}">{TAGS[t][0]}</span><div><b>{E(a)}</b><span>{E(b)}</span></div></li>' for t, a, b in itens)
        out.append(f'<section class="bk"><header><h3>{E(titulo)}</h3></header><p class="rs">{E(resumo)}</p><ul>{li}</ul></section>')
    return "".join(out)

def _grupo_de(titulo):
    for ini, g in GRUPO_DE:
        if titulo.startswith(ini):
            return g
    raise SystemExit("sem grupo para: " + titulo)
por_grupo = {g: [] for g in GRUPOS}
for g, a, b in testar_novos:
    por_grupo[g].append((a, b))
for a, b in testar_antigos:
    por_grupo[_grupo_de(a)].append((a, b))
tst = "".join(
    f'<h3 class="grp">{E(g)} <span class="cnt">{len(itens)}</span></h3><ol class="nx">'
    + "".join(f'<li><div><b>{E(a)}</b><span>{E(b)}</span></div></li>' for a, b in itens) + '</ol>'
    for g, itens in por_grupo.items() if itens)
cf = "".join(f"<li>{E(x)}</li>" for x in confirmado)
sm = "".join(f"<li>{E(x)}</li>" for x in simulado)
px = "".join(f'<li><div><b>{E(a)}</b><span>{E(b)}</span></div></li>' for a, b in proximos)

CSS = open(os.path.join(AQUI, "estilo.css")).read()
page = f'''<title>LUMNA OS, onde estamos</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com"><link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Sora:wght@500;600;700&family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&display=swap">
<style>
{CSS}
</style>
<div class="wrap">
<header class="top">
<div class="eyebrow">LUMNA OS · {HOJE}</div>
<h1>Onde estamos</h1>
<p class="lead">O mapa do projeto, para ninguém se perder. O que está feito, o que falta, o que espera um teste seu e o que está confirmado na sua máquina.</p>
</header>
<nav class="nav" aria-label="Seções"><a href="#resumo">Resumo</a><a href="#fio">O fio da meada</a><a href="#faltam">O que ainda não temos</a><a href="#testar">Para você testar</a><a href="#painel">Painel</a><a href="#testes">O que foi testado</a><a href="#proximos">Por onde seguir</a></nav>

<section class="sec" id="resumo"><h2>Resumo por área</h2>
<p class="sub">Cada cartão diz o que existe e onde foi testado. “Só simulado” quer dizer que eu testei com programas falsos aqui e que falta você ver na sua máquina.</p>
<div class="grid">{cards}</div>
<div class="legend"><span><span class="tag conf">Confirmado</span> você viu funcionando</span><span><span class="tag sim">Só simulado</span> testei com falsos</span><span><span class="tag and">Em andamento</span> partes prontas e partes por fazer</span><span><span class="tag sk">Não existe</span> ainda não há nada no desktop novo</span></div></section>

<section class="sec" id="fio"><h2>O fio da meada</h2><p class="sub">A ordem em que as coisas aconteceram.</p><ol class="tl">{tl}</ol></section>

<section class="sec" id="faltam"><h2>O que ainda não temos</h2>
<p class="sub">As frentes que você listou, mais o que falta para o desktop virar a sessão do sistema. Cada linha tem uma etiqueta: <b>Feito</b>, <b>Falta testar</b>, <b>Falta</b>, <b>Ver no antigo</b> (o LUMNA antigo, em Vue, está descartado; o que ele tinha serve só de referência) ou <b>Pergunta</b> (preciso de uma decisão sua).</p>
{backlog_html()}</section>

<section class="sec" id="testar"><h2>Para você testar</h2>
<p class="sub">Está pronto, mas só você pode confirmar na sua máquina. Quando algo falhar, o mais útil é me dizer <b>o que aparece na tela</b>. Os passos numerados da ISO estão em <code>docs/TESTES-ISO.md</code>.</p>
{tst}</section>

<section class="sec" id="painel"><h2>Painel de controle, página por página</h2>
<p class="sub">Contado lendo o código do Painel. Uma página sem função de desenho aparece como “em breve”, e é isso que está tracejado.</p>
<div class="big"><div><b>{tot_ok}</b> <span>prontas</span></div><div><b>{tot_sk}</b> <span>esqueleto</span></div><div><b>{pct}%</b> <span>do total de {tot}</span></div></div>
<div class="bar" role="img" aria-label="{tot_ok} de {tot} páginas prontas" style="height:10px"><i style="width:{pct}%"></i></div>
{painel_html()}</section>

<section class="sec" id="testes"><h2>O que foi testado de verdade</h2><p class="sub">Esta é a parte mais importante para confiar no que está escrito acima.</p>
<div class="two"><div class="box ok"><h3>Você confirmou na sua máquina</h3><ul>{cf}</ul></div><div class="box sim"><h3>Testei só em simulação, sem confirmação sua</h3><ul>{sm}</ul></div></div></section>

<section class="sec" id="proximos"><h2>Por onde seguir</h2><p class="sub">Minha sugestão de ordem. Você pode trocar qualquer passo.</p><ol class="nx">{px}</ol></section>
<footer>Gerado por docs/status/gera.py. Painel contado no código de gravity/exemplos/painel. O roteiro está em gravity/ROTEIRO.md e as ideias combinadas em docs/PROXIMOS.md.</footer>
</div>
'''
saida = os.path.join(AQUI, "progresso.html")
open(saida, "w").write(page)
print(saida, len(page), "| Painel:", tot_ok, "prontas /", tot_sk, "esqueleto /", pct, "%")
