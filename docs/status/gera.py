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
 ("ISO nova (Orbit + Gravity)", "and", "Pacotes lumna-orbit e lumna-gravity, sessão do Orbit no login, instalador em Gravity (disco inteiro, UEFI e BIOS, GRUB), agente da senha de administrador, sessão live e atualizações pelo GitHub com snapshot e \"Desfazer\". A ISO de teste é a do release teste-orbit (7 de novembro); o que entrou depois chega pelo Painel > Sobre > Atualizações.",
  "Montada pelo CI e provada com programas falsos e Orbit sem tela. O boot de verdade (VM e PC) é seu: o roteiro com passou/falhou está em docs/TESTES-ISO.md."),
 ("Desktop (Orbit + Gravity)", "and", "Janelas livres e encaixe, um modo por tela, barra de título do lado do Orbit nos apps de fora, teclado na tela, copiar e colar, arrastar e soltar entre apps (Início para a dock) e o botão Sair da conta no menu de aplicativos.",
  "Você confirmou, aninhado no KDE: janelas por monitor, tela cheia e o mouse no lugar. Nunca rodou num monitor de verdade (DRM): é o maior risco da ISO."),
 ("Atlas (explorador de arquivos)", "sim", "Substitui o Thunar. Abas, grade e lista, operações com desfazer, lixeira, menu de contexto e \"...\" em cada item, Propriedades em abas, Início com pastas e partições, barra lateral que reordena arrastando, Rede e nuvem (servidores pelo gvfs e contas pelo rclone) e arrastar arquivos entre pastas.",
  "Provado no Orbit sem tela. Você já abriu e me mandou os travamentos, que foram corrigidos. Nenhum servidor nem conta de nuvem de verdade foi usado (só gvfs e rclone de mentira)."),
 ("Terminal (Pulse) com a Emma", "sim", "Terminal em Gravity com abas, histórico e memória em SQLite. A Emma sugere a correção de um comando que falhou, traduz o que você quer fazer (Ctrl+E) e explica antes de executar um comando perigoso. Nada roda sem a sua confirmação.",
  "Provado aqui com a Emma de mentira. Com a sua Emma e um provedor de verdade ainda não."),
 ("Teclado na tela", "sim", "Sobe de baixo e empurra as janelas, com acentos por toque longo, símbolos, emojis, vários dedos e a tela larga do PC. Abre sozinho nos apps de fora (input-method). O Painel escolhe quando aparece e o tamanho.",
  "Provado com um app de mentira e toques simulados. Falta ver no Firefox e no celular de verdade."),
 ("Painel de controle", "and", f"{tot_ok} de {tot} páginas prontas ({pct}%). Faltam {FALTAM_PAINEL}: estão no plano e ainda nem têm página.",
  "Tudo foi testado com simulação e com o Orbit sem tela (headless); em máquina de verdade ainda não: gama da Luz noturna, perfil automático da bateria, a senha de administrador (polkit) e o idioma do sistema."),
 ("Mídia", "and", "Início, seletor, controle pelo celular, nunca dois vídeos juntos e o YouTube em formato de TV.",
  "Você confirmou: extensão, login do Google e os botões do controle no YouTube de desktop. Falta o YouTube TV e a Netflix."),
 ("Emma", "and", "Servidor, assistente, arquivos e comandos, código, perfis SFW e NSFW, imagens e visão, Oi Emma, histórias, MCP e a página Voz do Painel.",
  "Você confirmou o PIN do perfil NSFW. Imagens, MCP, Oi Emma e a voz: sem confirmação sua."),
 ("Telas e monitores", "sim", "Estender, por modo, espelhar, só a da esquerda ou da direita, topbar por monitor e o botão de tela cheia.",
  "Num KDE falso. Você confirmou só as janelas, a tela cheia e o mouse."),
 ("LUMNA Connect (no PC)", "sim", "Pareamento, área de transferência, avisos, arquivos, informações do PC, energia (bloquear, suspender, reiniciar, desligar), agendamentos e a Emma por texto. Falta no PC: senhas e confirmações (polkit).",
  "Só com um celular falso. O OnePlus com o PC novo ainda não foi testado."),
 ("App LUMNA Connect (Android)", "sim", "Um app só, em cinco telas: Controle, Compartilhar (arquivos e área de transferência), Emma, PC (informações, energia e agendamentos) e Ajustes. Avisos nos dois sentidos e um serviço que mantém a conexão.", "Escrito e compilado pela CI, mas nunca rodou num celular."),
 ("Modo Gaming", "and", "O modo e a ilha por tela existem. A biblioteca de jogos e o Início do Gaming não.", "Nada para testar ainda."),
 ("Apps que ainda não existem", "falta", "Música, player de vídeo, Galeria de fotos, bloco de notas, Code, Reminders, StoryTeller, Loja, Telefone e o navegador. No LUMNA antigo (Vue) existem Música, Galeria, Loja e Telefone, como referência.", "—"),
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
 ("A ISO nova", "Auditoria do shell antigo, pacotes do Orbit e do Gravity, agente da senha de administrador, instalador em Gravity, sessão live, GRUB em UEFI e BIOS, atualizações pelo GitHub com snapshot e a ISO de teste no canal teste-orbit.", "feito"),
 ("Os apps do sistema começam", "Teclado na tela, o terminal Pulse com a Emma por dentro, a barra de título do lado do Orbit e o Atlas no lugar do Thunar, com abas, Rede e nuvem e arrastar e soltar entre apps.", "feito"),
 ("Agora", "Você dar o primeiro boot da ISO e passar pelos apps novos. Depois: Gaming, a voz da Emma, o app LUMNA Connect completo e os apps que faltam (Música, player de vídeo, Navegador).", "agora"),
]

# ---------------------------------------------------------------------------------------- O QUE AINDA NÃO TEMOS (backlog)
# tag: feito · testar (pronto, falta você ver) · falta · ref (existe no LUMNA antigo: é portar) · pergunta (preciso de uma decisão sua)
backlog = [
 ("1. Modo Gaming", "A ilha por tela, a dock escondida e as janelas de jogo na tela certa já existem. Falta o que se vê e se usa.", [
  ("feito", "O modo, a ilha por tela, dock escondida e não perturbe", "Os jogos (Steam, gamescope, Heroic, Lutris) abrem na tela do Gaming."),
  ("ref", "Biblioteca de jogos em carrossel", "No LUMNA antigo: GameLibrary.vue, com capas, filtros (Todos, Recentes, Steam, Epic, GOG, Amazon, Lutris), controle, teclado e mouse."),
  ("falta", "Início do Gaming no desktop novo (como o da Mídia)", "Uma tela de Gravity com a biblioteca, navegável pelo teclado, pelo gamepad e pelo celular."),
  ("falta", "Ler as bibliotecas e abrir o jogo", "Steam (appmanifest, steam://rungameid), Heroic e Lutris; capas pela Steam ou SteamGridDB."),
  ("falta", "Gamepad como controle da interface", "Direcional, A e B no lugar das setas, OK e voltar."),
  ("falta", "Desempenho enquanto joga (ideia minha)", "Perfil de energia de desempenho e GameMode ao abrir um jogo; voltar ao normal ao sair."),
  ("pergunta", "O que o Gaming deve ter além da biblioteca?", "Por exemplo: ver o FPS, conversar com a Emma por cima do jogo, gravar um clipe com um atalho."),
 ]),
 ("2. A voz da Emma", "A Emma já ouve, entende e fala pelo serviço. Falta a tela para escolher como, e testar de verdade.", [
  ("feito", "O serviço fala e ouve", "Microfone, transcrição (/api/stt), resposta falada frase a frase (/api/tts), Oi Emma offline. O servidor tem voz Fish Audio ou local (faster-whisper e voz clonada)."),
  ("testar", "Página Voz no Painel", "Escolher o motor (Whisper e Pocket TTS no próprio PC, ou Fish Audio com chave), o botão \"Instalar no PC\" e o \"Testar a voz da Emma\". Falta a velocidade, o microfone e a saída."),
  ("falta", "Gerenciar os modelos de voz", "Baixar com progresso e poder cancelar (o download do Vosk travou em 6% na sua máquina) e dizer quanto espaço usa."),
  ("falta", "O ícone da Emma mostra quando ela fala", "Ouvindo, pensando e falando já têm estado; falta ligar na voz."),
  ("falta", "A voz da Emma na tela de bloqueio", "Consta no roteiro."),
  ("testar", "Falar com a Emma, com microfone de verdade", "O servidor e o serviço estão prontos; ninguém confirmou ainda na sua máquina."),
  ("pergunta", "\"Voz da Emma\" é ela falar, ela ouvir, ou os dois?", "Estou assumindo os dois: falar e ouvir (o microfone e a palavra \"Oi Emma\")."),
 ]),
 ("3. Páginas do Painel de controle", "Quase tudo existe. Faltam só as pontas e duas páginas que o LUMNA antigo tinha.", [
  ("feito", "Teclado na tela", "A página existe: quando o teclado sobe (sozinho, sempre ou só quando você pedir) e o tamanho. O teclado em si é o lumna-teclado."),
 ("falta", "As pontas", "VPN, Ponto de acesso e Receber arquivos por Bluetooth: estão no plano, mas ainda nem têm página."),
 ("ref", "Páginas que o LUMNA antigo tinha", "Impressoras e Dados móveis (PrintersPage.vue e MobileDataPage.vue)."),
 ]),
 ("4. App LUMNA Connect (Android) com todo o Connect", "Hoje o app é só um controle remoto. O Connect de verdade (arquivos, avisos, energia...) mora no app do OnePlus, e o PC novo ainda não atende parte dele.", [
  ("feito", "O controle no app", "Pareamento por código, direcional, texto, teclas, mídia, volume, modo, Home, falar com a Emma e microfone da TV."),
  ("ref", "A referência completa", "O Connect do LUMNA antigo: ConnectPage, Transfers e Schedules (Vue), e remote.rs, link.rs e connect.rs (Rust)."),
  ("testar", "No app: arquivos nos dois sentidos", "Tela Compartilhar: enviar vários arquivos (ou Compartilhar > LUMNA Connect, de qualquer app) com progresso; o que o PC oferecer chega sozinho em Downloads/LUMNA."),
  ("testar", "No app: área de transferência nos dois sentidos", "O que o PC copia chega no celular sozinho. Do celular: botão, ou o envio automático ao abrir o app (o Android não deixa ler a cópia em segundo plano)."),
  ("testar", "No app: avisos nos dois sentidos", "Os do celular no PC (precisa dar acesso às notificações em Ajustes) e os do PC no celular (precisa permitir avisos)."),
  ("testar", "No app: controle de energia do PC", "Tela PC: bloquear, suspender, reiniciar e desligar (os três últimos pedem confirmação)."),
  ("testar", "No app: agendamento de tarefas", "Tela PC: lembrete, pedido para a Emma, bloquear, suspender, reiniciar ou desligar, a uma hora marcada e com repetição diária."),
  ("falta", "No app: senhas e confirmações", "O PC pede a senha ou uma confirmação (polkit) e o celular responde."),
  ("testar", "No app: conversa com a Emma", "Tela Emma, por texto ou pela voz do celular. Falta aprovar as ações que ela pede (o PC ainda não avisa o celular desse pedido)."),
  ("testar", "No app: manter conectado", "Um aviso fixo mantém a conexão com o app fechado, para avisos e arquivos chegarem (Ajustes)."),
  ("feito", "No PC (lumna-connect): info, power, schedule-*, emma e emma-decide", "Portados do LUMNA antigo, no mesmo formato de mensagens (e o schedule.json igual), com testes usando um celular falso. O agendador confere a cada 15 s e roda na volta o que venceu há menos de 1 h."),
  ("falta", "No PC (lumna-connect): senhas e confirmações (auth-respond)", "Pede um agente de autenticação do polkit escrito em C. É um passo grande e separado."),
  ("testar", "Connect de arquivos e avisos do PC novo com o OnePlus", "O app antigo já fala o protocolo; a parte nova do PC só foi testada com um celular falso."),
  ("feito", "Um app só: LUMNA Connect", "Decidido por você: o LUMNA Controle virou LUMNA Connect (no PC, no Android e nos textos). O app cresce em telas fáceis e intuitivas: controle, arquivos, avisos, energia, agendamento, senhas e Emma."),
 ]),
 ("5. Apps do sistema", "A lista abaixo é o que você pediu. A etiqueta \"Portar do antigo\" diz que o LUMNA antigo (Vue) já tem uma versão para olhar.", [
  ("testar", "Atlas, o explorador de arquivos", "No lugar do Thunar. Ainda não tem: mover (em vez de copiar) ao arrastar, arrastar com o dedo, discos e USB, sincronização da nuvem com o Atlas fechado, busca dentro das subpastas e as opções avançadas do mockup (pausar transferências, transferências simultâneas, verificar cópias, cache de miniaturas, etiquetas)."),
  ("testar", "Terminal (Pulse)", "Abas, memória, correção de erro, comando por linguagem natural e explicação antes de executar. Detalhes em docs/PULSE.md."),
  ("testar", "Captura de tela", "Foto, vídeo com áudio e o editor de anotações."),
  ("ref", "Player de música", "No antigo: MusicApp.vue (fila, letras, Qobuz, saídas). No novo: não existe."),
  ("falta", "Player de vídeo", "O mpv por baixo, com os controles do LUMNA. Não existe em nenhum dos dois."),
  ("ref", "Galeria de fotos", "No antigo: GalleryApp.vue. (O \"galeria\" do Gravity é a vitrine dos componentes, não um app de fotos.)"),
  ("falta", "Bloco de notas", "Não existe em nenhum dos dois."),
  ("falta", "LUMNA Code", "Definido por você: a sua versão do VS Code, com a Emma integrada. Em C com o Gravity. Ainda não começou."),
  ("falta", "Reminders", "Definido por você: calendário e lembretes centralizados, que você e a Emma gerenciam. Em C com o Gravity. O agendamento do Connect já é a base dos avisos."),
  ("falta", "StoryTeller", "Definido por você: a sua engine de histórias, com a Emma integrada, em cima do repositório dekilled/world-forge-app. Ainda não comecei; vou ler o repositório antes de propor a estrutura."),
  ("falta", "O Gravity vira o \"Gravity framework\"", "Decidido por você: tudo novo é em C com o Gravity, no estilo do sistema, para construir qualquer interface dentro do LUMNA. Depois do sistema pronto, a interface do celular também vai para C, para todos os apps."),
  ("ref", "Outros que o antigo tinha", "Loja (StoreApp.vue) e Telefone (PhoneApp.vue)."),
  ("falta", "Outros em aberto", "Navegador, visualizador de imagens e PDF, calculadora, monitor do sistema. Diga quais entram."),
  ("pergunta", "Em Gravity (C) ou aproveitando o shell Vue?", "Estou assumindo que tudo novo é em Gravity, como o resto do desktop novo, e que o antigo serve de referência."),
 ]),
 ("6. Para o desktop ser a sessão do sistema", "O desktop já tem sessão no login, instalador e ISO de teste. O que falta é rodar tudo isso numa máquina de verdade.", [
  ("feito", "Sessão, instalador e ISO de teste", "A entrada \"LUMNA · Orbit\" do greetd, o instalador em Gravity (disco inteiro, UEFI e BIOS, GRUB com o /boot no Btrfs) e a ISO do release teste-orbit. A sessão do Hyprland antigo vai junto só como emergência."),
  ("testar", "O primeiro boot de verdade", "O Orbit nunca rodou num monitor de verdade (DRM). Primeiro numa VM com scripts/run-vm.sh, depois no PC. O roteiro com passou/falhou está em docs/TESTES-ISO.md."),
  ("testar", "Atualizar pelo GitHub", "Painel > Sobre > Atualizações: Procurar, Atualizar tudo (com snapshot antes) e Desfazer a última atualização. Os pacotes saem a cada push na iso-orbit."),
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
 ("A ISO nova e o instalador", "Dar boot na ISO, numa VM", "Baixe a ISO do release teste-orbit e rode scripts/run-vm.sh --disco --telas 2 lumna-os-teste.iso. Deve entrar sozinha no Orbit, com papel de parede, barra e dock. Se a tela ficar preta, escolha \"LUMNA (clássico · emergência)\" no login e me mande o log (o comando está no roteiro, seção 1). Todos os passos, com número, estão em docs/TESTES-ISO.md."),
 ("A ISO nova e o instalador", "Instalar e entrar no sistema instalado", "Instalar pelo atalho \"Instalar LUMNA OS\" (disco inteiro), em UEFI e em BIOS, reiniciar e entrar. Conferir o GRUB e o Painel > Sobre."),
 ("A ISO nova e o instalador", "A janela de senha do administrador", "Painel > Sistema > Idioma, depois Aplicar: deve aparecer a janela do lumna-polkit. Senha errada, três vezes seguidas, recusa o pedido; Esc cancela."),
 ("A ISO nova e o instalador", "Atualizar o sistema pelo GitHub", "Painel > Sobre > Atualizações: Procurar, Atualizar tudo e Desfazer a última atualização (tira um snapshot do Btrfs antes). O Atlas, o Pulse e o arrastar e soltar, que entraram depois da ISO de 7 de novembro, chegam por aqui."),
 ("A ISO nova e o instalador", "No PC de verdade", "O que a VM não mostra: a gama da Luz noturna, a tela apagando (DPMS), suspensão, botão de ligar, bateria, vários monitores e a placa AMD ou Intel (roteiro, seção 5)."),
 ("Apps novos", "Atlas: o dia a dia", "Abas, grade e lista, copiar e colar, renomear, lixeira com desfazer, compactar e extrair. O \"...\" aparece em cada item; no botão direito de uma pasta, os submenus Cor e Ícone abrem ao lado e Propriedades abre a janela em abas. No Início, duplo clique entra na pasta e o botão direito oferece \"Remover da página inicial\". Foi onde você achou os travamentos que corrigi: confirme que sumiram."),
 ("Apps novos", "Atlas: arrastar", "Na barra lateral, arraste um atalho para reordenar. Arraste um arquivo (ou vários selecionados) para uma pasta, para um atalho da barra lateral ou para o fundo da lista: copia, e o original fica. Com o dedo, arrastar arquivos ainda não existe."),
 ("Apps novos", "Atlas: Rede e nuvem", "Configurações > Rede e nuvem: conectar a um servidor (smb://, sftp, ftp, WebDAV, nfs) e criar contas de nuvem pelo rclone (Nextcloud, WebDAV, Google Drive, Dropbox, OneDrive). Só foi provado com gvfs e rclone de mentira, então é aqui que mais pode falhar."),
 ("Apps novos", "Terminal Pulse", "Alt+Enter abre o terminal. Rode um comando que erra (gti status) e veja o cartão da Emma; Ctrl+E em um prompt vazio pede um comando em português; um rm ou dd mostra a explicação antes de executar. Precisa de um provedor em Painel > Emma > Provedores."),
 ("Apps novos", "Teclado na tela", "Toque num campo de texto no Firefox ou no Painel: o teclado sobe e as janelas se ajustam. Teste o toque longo nas letras, Shift duplo, símbolos, emojis, e Painel > Teclado e mouse > Teclado na tela (Sozinho, Sempre, Só quando eu pedir)."),
 ("Desktop, monitores e modos", "Sair da conta", "Menu de aplicativos, botão Sair da conta (ao lado do Bloquear): pede confirmação e, ao confirmar, volta para a tela de login. Só provei que o Orbit encerra a sessão; a volta pelo greetd é sua."),
 ("Desktop, monitores e modos", "Arrastar um app do Início para a dock", "Com o mouse, aperte num app do menu e arraste até a dock; com o dedo, segure meio segundo. O menu fica transparente enquanto arrasta, o app fica fixo onde você soltar e o menu fecha. Soltar fora não fixa nada. Na dock, arrastar um ícone fixo reordena."),
 ("Desktop, monitores e modos", "Barra de título das janelas de fora", "Firefox, Blender e outros apps de fora ganham a barra de título que o Orbit desenha, no estilo dos apps nativos. Foi testada só com o foot e um mouse virtual."),
]
GRUPO_DE = [  # (começo do título antigo, grupo)
 ("YouTube TV", "Mídia"), ("Nunca dois vídeos", "Mídia"), ("Netflix", "Mídia"), ("Setas nos serviços", "Mídia"), ("YouTube em modo TV", "Mídia"),
 ("Painel: Por modo", "Desktop, monitores e modos"), ("Passar janelas", "Desktop, monitores e modos"), ("Topbar por monitor", "Desktop, monitores e modos"),
 ("Seletor de modo", "Desktop, monitores e modos"), ("Modos: Super+Tab", "Desktop, monitores e modos"), ("Mídia: escolher o modo", "Desktop, monitores e modos"),
 ("Mídia: a tela", "Desktop, monitores e modos"), ("Modos por tela", "Desktop, monitores e modos"), ("Tela de bloqueio", "Desktop, monitores e modos"),
 ("Tela principal", "Desktop, monitores e modos"), ("Arrastar janela para", "Desktop, monitores e modos"), ("Trocar de modo", "Desktop, monitores e modos"),
 ("A voz da Emma", "Emma"), ("A Emma", "Emma"), ("Painel > Som", "Emma"), ("Painel > Emma", "Emma"),
 ("Connect no OnePlus", "Connect e celular"), ("App LUMNA Connect", "Connect e celular"),
 ("Captura de tela", "Apps novos"),
]
testar_antigos = [
 ("YouTube TV (a interface de TV)", "As setas movem a seleção entre os vídeos, o OK abre o vídeo, o voltar volta para a lista e o play/pausa e o ±10 s funcionam dentro do vídeo. Me diga o que aparece na tela se algo falhar."),
 ("Nunca dois vídeos juntos", "Abra um vídeo, aperte Home, abra outro serviço e outro vídeo: só um deve tocar por vez. Voltar até a primeira página deve sair do serviço e pausar. (A aba fica viva: para retomar, aperte play.)"),
 ("Netflix e os outros serviços", "Abrir e tocar. É aqui que o Widevine (o decodificador de vídeo protegido) pode dar problema na primeira vez."),
 ("Painel: Por modo", "Telas e modos > Monitores > Como usar as telas > Por modo. Os seletores de cada tela agora abrem; escolha Mídia numa e Desktop noutra."),
 ("Passar janelas entre monitores", "Alt+← e Alt+→ levam a janela para o outro monitor. No \"só a da esquerda ou da direita\", as janelas da tela apagada devem ir para a que ficou, visíveis."),
 ("Topbar por monitor e o botão de tela cheia", "Painel > Aparência > Barra superior > Telas com a barra. O botão ao lado dos ícones sai da tela cheia e volta."),
 ("Connect no OnePlus com o PC novo", "Avisos do PC e do celular, enviar e receber arquivos e a área de transferência. Energia, agendamentos, informações e a Emma por texto já existem no PC, mas o app do celular ainda não tem telas para eles; senhas ainda não existem no PC novo."),
 ("App LUMNA Connect novo (APK)", "Instalar o APK novo (pacote os.lumna.connect, instala ao lado do antigo) e passar pelas cinco telas. O erro de \"conflito\" era a chave de assinatura mudando a cada build; agora o pacote é outro e o número de versão sempre sobe."),
 ("Seletor de modo: Super+Tab só abre, o seguinte troca", "O primeiro Super+Tab num monitor só abre o seletor (com o modo atual em destaque) e não troca nada; um Super+Tab logo em seguida (até uns 2 s) vai para o próximo modo, Shift+Tab para o anterior. Escolher o modo que o monitor já tem, pelo seletor, pelo dropdown da topbar ou pelo orbit-msg, não troca nada e o seletor mostra \"Este monitor já está no modo X\". Provado em Orbit headless. Sobre o atraso no clique do menu iniciar: aqui ele leva ~25 ms para o primeiro quadro; se continuar com atraso aí, rode LUMNA_TEMPO=1 lumna-lancador no terminal e me mande as linhas."),
 ("Modos: Super+Tab em qualquer tela e a Mídia fora da dock", "(1) O Super+Tab (e o seletor que aparece) agora alcança os três modos em qualquer monitor, inclusive o que outra tela já tem: escolher troca as duas telas de lugar (o modo que estava aqui vai para a outra, e o de lá vem para cá, com as janelas). Antes ele pulava o modo ocupado. (2) As janelas da Mídia e do Gaming não aparecem mais na dock nem no Alt+Tab, mesmo quando a tela delas volta para o Desktop (era o que aparecia como Mídia minimizada na barra). (3) O Super+M abre o seletor de serviços da tela em Mídia mesmo com o cursor no outro monitor. (4) O lançador (o menu do Super ou da dock) ficava sem abrir se um arquivo de controle antigo apontava para um número de processo que passou a ser de outro programa; agora ele confere se o processo é mesmo um lançador. Não consegui reproduzir o seu caso do lançador (aqui ele abre pelo Super e pelo clique, inclusive com a Mídia ligada), então se continuar sem abrir, rode pgrep -a lancador e cat $XDG_RUNTIME_DIR/lumna-lancador.pid e me mande."),
 ("Tela de bloqueio: vídeo tocando, widget de mídia e senha na tela do cursor", "(1) Vídeo tocando não tranca mais: o lumna-ocioso agora atende o D-Bus org.freedesktop.ScreenSaver e PowerManagement (o que Firefox, Chromium, VLC usam para pedir isso), além do idle-inhibit do Wayland que o Orbit já tratava. Enquanto alguém pede, não tranca; quando o último solta a contagem recomeça do zero (não tranca de repente ao fim do vídeo); quem pediu e fechou sem soltar deixa de contar. Para ver quem está segurando: orbit-msg ocioso (Wayland) e o log do lumna-ocioso (D-Bus). Se você roda o LUMNA dentro do KDE, o pedido do Firefox pode ir para o D-Bus do KDE (ele é o dono do nome) e o lumna-ocioso avisa isso no log; fora do KDE, vale. (2) O widget de mídia: o botão agora manda Play ou Pause conforme o estado (e cai para PlayPause se o player recusar), o ícone muda na hora e qualquer falha vai para o log com o nome do player. Não consegui reproduzir o seu caso (só tenho um player de mentira), então se ainda falhar me mande as linhas \"mpris:\" do log. (3) A interface do bloqueio (relógio, player, senha, PIN) agora aparece na tela onde está o cursor e segue o cursor entre os monitores; começa na tela do cursor. Provado em Orbit headless de 2 monitores com mouse virtual e tecla depois da troca; no seu KDE é você quem confirma."),
 ("Mídia: escolher o modo noutra tela troca as duas de lugar", "Com a Mídia no monitor 1, escolha Mídia no seletor da barra do monitor 2: a Mídia (e a janela dela) vai para o 2 e o Desktop do 2, com as janelas, vai para o 1. Vale também para o Gaming e entre Mídia e Gaming. É o mesmo efeito do \"Trocar as telas de lugar\", mas pelo próprio seletor de modo. Provado em Orbit headless de 3 monitores com janelas reais (o teste falha com o comportamento antigo); no seu KDE é você quem confirma."),
 ("Mídia: a tela \"Abrindo\" e o encaixe de janelas", "(1) A tela de carregamento do serviço (a capa) e o seletor de serviços agora abrem na tela que está em Mídia, e não na que o cursor estiver: era por isso que ela aparecia no outro monitor quando você passava o mouse para lá (a regra está no Orbit e também na Mídia). (2) O encaixe agora é por zonas, como no seu desenho: as laterais de cada monitor (56 px) são a zona. A janela atravessa por ela direto para o outro monitor e só entra no arranjo (meia tela) se você SOLTAR com o cursor na zona, ou com a borda da janela encostada na lateral (janela larga agarrada pelo meio chega antes do cursor). A prévia mostra as duas metades, a do alvo cheia. Em cima, uma faixa fina maximiza. Super+setas continuam encaixando. Ainda não tem o texto \"Solte para encaixar\", o ícone e as pontas arredondadas do desenho (o Orbit não desenha texto). Provado em Orbit headless com mouse virtual. Se o Mídia ainda abrir só num monitor, confira se o Orbit foi recompilado com o commit novo (orbit-msg modo mostra o modo de cada saída)."),
 ("Modos por tela: Mídia e Gaming só na tela pedida", "Com 2 ou 3 monitores: coloque a Mídia no monitor 1, 2 ou 3 (pelo seletor da barra daquele monitor): só ele vira Mídia e os outros seguem Desktop, estendidos entre si. Idem Gaming. Sem nenhum dos dois, todos ficam Desktop. Cada barra mostra o modo DA SUA tela (antes todas mostravam o da tela onde o cursor estava). A Mídia e o Gaming existem numa tela só por vez: pedir a Mídia noutro monitor leva a janela da Mídia junto e o monitor que a tinha volta ao Desktop (decisão minha: se preferir poder ter Mídia em duas telas ao mesmo tempo, me diga). Super+Tab pula o modo que outra tela já tem. Comprovado com Orbit headless de 3 monitores e janelas de verdade, e a barra por screenshot; o seu KDE de verdade é o que falta."),
 ("Modos por tela e arrastar janela entre monitores", "(1) A regra nova: o padrão é todas as telas em Desktop, estendido; abrir a Mídia ou o Gaming numa tela só muda aquela e cada tela mantém o seu modo até você trocar; sem nenhuma em modo especial o Desktop estendido é o que fica. A opção \"estender o modo às telas\" saiu do Painel, da barra e do Orbit. Em Painel > Monitores sobram Estender, Espelhar, Só a da esquerda e Só a da direita. (2) Arrastar a janela para o monitor do lado: o encaixe de meia tela só vale em borda que é o fim do desktop; o movimento relativo do KDE deixou de prender o cursor; e ao largar vale o monitor que cobre mais da janela. Reproduzi o seu sintoma (a janela encaixada na metade do monitor de origem) com Alt e mouse virtual, mas o KDE de verdade só você tem."),
 ("A voz da Emma cortando no meio (\"o tocador saiu com erro 1\")", "Reproduzi: numa resposta longa as frases geradas adiantadas disputavam 4 nomes de arquivo e uma apagava o áudio da outra. Agora cada frase tem o seu arquivo e só duas ficam prontas à espera. Peça algo que ela responda com um texto bem comprido e ouça até o fim. Se o erro voltar com uma fala curta, me mande o que aparece."),
 ("A Emma no chat: voz, configurações e setas", "(1) Com \"Ler as respostas em voz alta\" ligado em Painel > Emma > Voz, o chat do app e o assistente também falam (antes só falava quando você falava com ela; a conferência é a cada 10 s). (2) Peça \"abre as suas configurações\": o nome da seção escrito de qualquer jeito (maiúsculas, acento, espaço) agora vale, e a lista que ela conhece inclui Emma, Som e Celular. (3) $\\rightarrow$ e similares viram → na tela e na fala (e o prompt manda não usar LaTeX)."),
 ("Painel > Som e Painel > Emma > Voz (não ouço a Emma)", "Três páginas novas em Som: Saída (qual é a padrão, volume, mudo e um tom de teste), Entrada (microfones com o nível ao vivo) e Apps (volume por app). Em Emma > Voz há o botão \"Testar a voz da Emma\", que percorre o caminho todo. Se a voz falhar agora o motivo aparece (terminal, aviso na tela e a página Voz): antes o serviço engolia o erro e ficava mudo."),
 ("Painel > Emma > Voz", "A página que estava em branco: escolher se a Emma ouve (Whisper) e fala (Pocket TTS) no próprio PC, de graça e sem internet, ou pelo Fish Audio com chave. O botão \"Instalar no PC\" pede ao servidor da Emma para baixar o que falta (é grande, uma vez só). Depois diga \"Oi Emma\" e veja se ela responde sem nenhuma API."),
 ("Tela principal", "Painel > Telas e modos > Monitores: o interruptor \"Tela principal\" em cada tela. O papel de parede \"Tela principal\" agora segue a função, não o número do monitor (grava como @principal): marque a outra e confira se o papel de parede, a barra e a dock \"só na principal\" mudam sozinhos de tela (os apps reabrem em ~2 s)."),
 ("Arrastar janela para o outro monitor", "Arrastar pela barra de título direto até o monitor do lado, sem soltar no meio. Só reproduzi com um KDE de mentira; no seu KDE de verdade é o que vale."),
 ("Trocar de modo sem piscar", "Mídia <-> Desktop pelo celular, com os dois monitores: a janela deve deslizar só no monitor dela, sem aparecer no do lado."),
 ("Setas nos serviços (Netflix, Prime, Disney...)", "As setas agora movem um contorno violeta entre os itens da página e o OK clica; sem item na direção, a seta rola a página. Dentro de um vídeo em tela cheia as setas voltam a ser do player."),
 ("YouTube em modo TV", "O servidor entrega a interface de TV a quem manda o User-Agent de TV (conferi), então a extensão deveria bastar. Agora, se a aba cair no YouTube de mouse (depois do login, por exemplo), ela volta sozinha para o youtube.com/tv. Se ainda abrir em formato desktop, me diga a URL que aparece."),
 ("Captura de tela", "Gravar com áudio e Enviar para o celular. Só foram testados com programas falsos."),
 ("A Emma", "Gerar imagem, um servidor MCP de verdade, o Oi Emma com o microfone e ela falando."),
]

# ------------------------------------------------------------------------------------------------- O QUE FOI TESTADO
confirmado = [
 "O perfil NSFW com o PIN da barra",
 "A captura de tela com a prévia e o escurecimento corrigido",
 "O Orbit aninhado com uma janela por monitor, a tela cheia e o mouse no lugar certo",
 "A Mídia no YouTube de desktop: a extensão no Developer Edition, o login do Google e os botões do controle (play, pausa, ±10 s, início, voltar, menu)",
 "O controle do Samsung (LUMNA Connect) com o PC, depois de instalar o wtype",
 "A conta ligada no YouTube TV (a interface de TV abriu)",
 "A Mídia do LUMNA antigo, no OnePlus, controlando o PC novo (funciona muito bem)",
 "Mover janelas entre as telas (Alt+setas), um modo por monitor sem as janelas passarem de um para o outro e a troca de modo pelo celular",
]
simulado = [
 "A ISO nova: o CI monta e confere os pacotes, e o instalador e o agente de senha foram provados com o lumna-install e o polkit de mentira. Ninguém deu boot ainda no registro",
 "O Orbit num monitor de verdade (DRM): nunca; só sem tela (headless) e aninhado no seu KDE",
 "O Atlas inteiro, com toques e teclas simulados no Orbit sem tela; Rede e nuvem só com gvfs e rclone de mentira",
 "O terminal Pulse e a Emma dentro dele (a Emma de mentira)",
 "O teclado na tela (um app de mentira com text-input e toques simulados)",
 "Arrastar e soltar entre apps (Início para a dock, dock e Atlas) com o mouse e o dedo simulados; o botão Sair da conta (provado que o Orbit encerra; a volta ao login não)",
 "A barra de título do Orbit nas janelas de fora (foot e mouse virtual)",
 "O Connect de arquivos, avisos, informações do PC, energia, agendamentos e a Emma por texto (só com um celular falso)",
 "O app Android novo inteiro (compila na CI, nunca rodou num celular)",
 "A topbar por monitor e o botão de tela cheia",
 "Espelhar, só a da esquerda ou da direita e o modo por tela",
 "Os seletores de modo do Painel e passar janelas entre monitores (testei com um mouse e um falso KDE)",
 "Nunca dois vídeos juntos (testei com o Firefox ESR e vídeos locais)",
 "Imagens da Emma, servidores MCP, o Oi Emma e a voz",
 "A gravação de tela com áudio",
]

proximos = [
 ("Dar o primeiro boot da ISO numa VM", "Siga docs/TESTES-ISO.md, marque passou ou falhou e me mande o que falhar (o Painel > Sistema > Diagnóstico junta os logs). É o maior risco que sobrou: o Orbit nunca rodou num monitor de verdade."),
 ("Passar pelos apps novos", "Atlas, Pulse, teclado na tela, Sair da conta e o arrastar e soltar (a lista \"Para você testar\"). O que falhar eu corrijo antes de começar coisa nova."),
 ("Testar a Mídia e o Connect do OnePlus", "O YouTube TV, a Netflix e os avisos, arquivos e área de transferência com o PC novo."),
 ("O Início do Gaming (a biblioteca)", "É a maior parte visível que falta e o antigo já tem a referência."),
 ("Instalar o APK novo e caçar bugs", "O APK sai da CI (aba Actions, \"LUMNA Connect (APK)\"). Todas as telas são novas; o que falhar eu corrijo."),
 ("Senhas e confirmações do PC no celular (polkit)", "O último pedaço do Connect completo."),
 ("Os apps que faltam, um por vez", "Player de vídeo, Música e Navegador primeiro; os que você ainda não descreveu esperam a sua resposta."),
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

TAGS = {"feito": ("Feito", "conf"), "testar": ("Falta testar", "sim"), "falta": ("Falta", "sk"), "ref": ("Portar do antigo", "and"), "pergunta": ("Pergunta", "perg")}
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
<p class="sub">As frentes que você listou, mais o que falta para o desktop virar a sessão do sistema. Cada linha tem uma etiqueta: <b>Feito</b>, <b>Falta testar</b>, <b>Falta</b>, <b>Portar do antigo</b> (já existe no LUMNA antigo, em Vue, como referência) ou <b>Pergunta</b> (preciso de uma decisão sua).</p>
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
