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
HOJE = "5 de novembro de 2026"

# ---------------------------------------------------------------------------------------------------------- O PAINEL
# (contagem feita lendo gravity/exemplos/painel: página sem função de desenho = esqueleto)
painel = [
 ("Geral", [
  ("Aparência", ["Tema","Cores","Papel de parede","Dock","Barra superior","Tela de bloqueio","Tamanho e textos"], []),
  ("Rede e internet", ["Wi-Fi","Cabo","Adaptadores","Várias conexões","Proxy"], ["VPN","Ponto de acesso"]),
  ("Bluetooth", ["Dispositivos","Nome e visibilidade"], ["Receber arquivos"]),
  ("Aplicativos", ["Instalados","Atualizações"], ["Apps padrão","Permissões"]),
  ("Telas e modos", ["Monitores","Modos","Áreas de trabalho"], ["Luz noturna"]),
  ("Som", [], ["Saída","Entrada","Apps","Sons do sistema"]),
  ("Celular Connect", ["Pareamento","Área de transferência","Avisos do celular","Arquivos"], []),
  ("Emma", ["Oi Emma","Provedores","Roteamento","Personalidade","Recursos","Servidores MCP","Importar do harness"], ["Voz","Privacidade","Automações"]),
 ]),
 ("Inicialização", [("Inicialização", [], ["Ao entrar","Apps ao iniciar","Entrada","Em segundo plano"])]),
 ("Sistema", [
  ("Sobre", ["Este computador","Atualizações do sistema","Avisos legais"], []),
  ("Armazenamento", ["Uso do disco","Limpeza","Discos e pendrives"], []),
  ("Energia", [], ["Perfil de energia","Bateria","Tela e suspensão","Botão de ligar"]),
  ("Teclado e mouse", ["Teclado","Atalhos","Mouse e touchpad"], ["Teclado na tela"]),
  ("Data e hora", ["Data e hora","Fuso horário","Formato"], []),
  ("Idioma e região", [], ["Idioma","Formatos regionais","Métodos de entrada"]),
  ("Usuários", [], ["Sua conta","Senha e PIN","Outros usuários"]),
  ("Privacidade", [], ["Permissões dos apps","Histórico e recentes","Diagnóstico"]),
  ("Desenvolvedor", ["Desenvolvedor"], []),
 ]),
]
tot_ok = sum(len(o) for _, cats in painel for _, o, s in cats)
tot_sk = sum(len(s) for _, cats in painel for _, o, s in cats)
tot = tot_ok + tot_sk
pct = round(100 * tot_ok / tot)

# ---------------------------------------------------------------------------------------------- O QUE SE SABE POR ÁREA
# estado: conf = confirmado por você · sim = só simulado · and = em andamento · falta = não existe
areas = [
 ("Desktop (Orbit + Gravity)", "and", "Funciona numa janela dentro do KDE, com uma janela por monitor. Ainda não rodou como a sua sessão de verdade.",
  "Você confirmou: janelas por monitor, tela cheia e o mouse no lugar."),
 ("Mídia", "and", "Início, seletor, controle pelo celular, nunca dois vídeos juntos e o YouTube em formato de TV.",
  "Você confirmou: extensão, login do Google e os botões do controle no YouTube de desktop. Falta o YouTube TV e a Netflix."),
 ("Painel de controle", "and", f"{tot_ok} de {tot} páginas prontas ({pct}%). As outras {tot_sk} são esqueleto, com a lista do que vão ter.",
  "Rede e Bluetooth foram testados com simulação."),
 ("Emma", "and", "Servidor, assistente, arquivos e comandos, código, perfis SFW e NSFW, imagens e visão, Oi Emma, histórias e MCP.",
  "Você confirmou o PIN do perfil NSFW. Imagens, MCP, Oi Emma e a voz: sem confirmação sua."),
 ("Telas e monitores", "sim", "Estender, por modo, espelhar, só a da esquerda ou da direita, topbar por monitor e o botão de tela cheia.",
  "Num KDE falso. Você confirmou só as janelas, a tela cheia e o mouse."),
 ("Celular Connect (no PC)", "sim", "Pareamento, área de transferência, avisos e arquivos. Faltam no PC: energia, agendamento, senhas e confirmações, Emma e info.",
  "Só com um celular falso. O OnePlus com o PC novo ainda não foi testado."),
 ("Modo Gaming", "and", "O modo e a ilha por tela existem. A biblioteca de jogos e o Início do Gaming não.", "Nada para testar ainda."),
 ("App LUMNA Controle (Android)", "and", "Hoje é só o controle: direcional, texto, teclas, mídia, volume, modo, Home e voz.", "Você confirmou o controle no Samsung."),
 ("Apps do sistema", "falta", "Nenhum ainda no desktop novo (só a captura). No LUMNA antigo existem o Explorador, a Galeria, a Música, a Loja e o Telefone.", "—"),
]
lab = {"conf": ("Confirmado", "conf"), "sim": ("Só simulado", "sim"), "and": ("Em andamento", "and"), "falta": ("Não existe", "sk")}

# ------------------------------------------------------------------------------------------------------ O FIO DA MEADA
timeline = [
 ("A base", "Gravity (a biblioteca de interface), Orbit (o compositor) e as peças do desktop: papel de parede, topbar, dock, lançador, avisos, bloqueio e login.", "feito"),
 ("O Painel de controle", "Aparência, Rede, Bluetooth, Aplicativos, Sobre, Armazenamento, Data e hora, Teclado e mouse e Monitores. Parou com 31 páginas esqueleto.", "parou"),
 ("Modos e áreas de trabalho", "Desktop, Gaming e Mídia, as áreas de trabalho com animação, as ilhas por tela, trocar telas e o Início da Mídia.", "feito"),
 ("A Emma", "O serviço em segundo plano, o chat, o painel dela, arquivos e comandos, código na conversa, as histórias, perfis SFW e NSFW, imagens e visão, Oi Emma, MCP e a sessão contínua.", "feito"),
 ("Captura de tela", "Foto e vídeo com áudio, área, janela ou tela inteira, editor de anotações e enviar para a Emma ou para o celular.", "feito"),
 ("LUMNA Connect", "O dia a dia com o celular: pareamento, área de transferência, avisos e arquivos.", "feito"),
 ("Vários monitores", "Uma janela por monitor, mouse corrigido, topbar por monitor, botão de tela cheia, as disposições e o modo por tela. Distribuir os workspaces foi descartado por você.", "feito"),
 ("A Mídia de verdade", "Firefox Developer Edition escolhido sozinho, perfil por Firefox, extensão validada, voltar que sai do serviço, nunca dois vídeos juntos e o YouTube em formato de TV.", "feito"),
 ("Agora", "Fechar as lacunas: Gaming, a voz da Emma, o Painel, o app LUMNA Controle completo e os apps do sistema.", "agora"),
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
  ("falta", "Página Voz no Painel", "Escolher o motor (Fish Audio ou local), a voz, a velocidade, o microfone e a saída, e um botão de testar a voz."),
  ("falta", "Gerenciar os modelos de voz", "Baixar com progresso e poder cancelar (o download do Vosk travou em 6% na sua máquina) e dizer quanto espaço usa."),
  ("falta", "O ícone da Emma mostra quando ela fala", "Ouvindo, pensando e falando já têm estado; falta ligar na voz."),
  ("falta", "A voz da Emma na tela de bloqueio", "Consta no roteiro."),
  ("testar", "Falar com a Emma, com microfone de verdade", "O servidor e o serviço estão prontos; ninguém confirmou ainda na sua máquina."),
  ("pergunta", "\"Voz da Emma\" é ela falar, ela ouvir, ou os dois?", "Estou assumindo os dois: falar e ouvir (o microfone e a palavra \"Oi Emma\")."),
 ]),
 ("3. Páginas do Painel de controle", f"{tot_sk} páginas ainda são esqueleto. Em ordem de uso no dia a dia:", [
  ("falta", "Energia (4)", "Perfil de energia, bateria, tela e suspensão, botão de ligar. Precisa do serviço de energia (UPower e brilho)."),
  ("falta", "Som (4)", "Saída, entrada, volume por app e sons do sistema. A topbar e o Mixer já controlam o volume; falta o Painel."),
  ("falta", "Inicialização (4)", "Ao entrar, apps ao iniciar, entrada (login automático) e serviços em segundo plano."),
  ("falta", "Emma (3)", "Voz, Privacidade e Automações."),
  ("falta", "Idioma e região (3), Usuários (3), Privacidade (3)", "Idioma, formatos, métodos de entrada; sua conta, senha e PIN, outros usuários; permissões, histórico e diagnóstico."),
  ("falta", "As pontas", "Luz noturna, Apps padrão, Permissões, VPN, Ponto de acesso, Receber arquivos por Bluetooth e Teclado na tela."),
  ("ref", "Páginas que o LUMNA antigo tinha", "Impressoras e Dados móveis (PrintersPage.vue e MobileDataPage.vue)."),
 ]),
 ("4. App LUMNA Controle (Android) com todo o Connect", "Hoje o app é só um controle remoto. O Connect de verdade (arquivos, avisos, energia...) mora no app do OnePlus, e o PC novo ainda não atende parte dele.", [
  ("feito", "O controle no app", "Pareamento por código, direcional, texto, teclas, mídia, volume, modo, Home, falar com a Emma e microfone da TV."),
  ("ref", "A referência completa", "O Connect do LUMNA antigo: ConnectPage, Transfers e Schedules (Vue), e remote.rs, link.rs e connect.rs (Rust)."),
  ("falta", "No app: arquivos nos dois sentidos", "Enviar e receber arquivos e fotos, com progresso."),
  ("falta", "No app: área de transferência nos dois sentidos", "Copiar no celular e colar no PC, e o contrário."),
  ("falta", "No app: avisos nos dois sentidos", "Os do celular no PC (serviço de notificações do Android) e os do PC no celular."),
  ("falta", "No app: controle de energia do PC", "Bloquear, suspender, reiniciar e desligar."),
  ("falta", "No app: agendamento de tarefas", "Programar uma ação do PC para uma hora."),
  ("falta", "No app: senhas e confirmações", "O PC pede a senha ou uma confirmação (polkit) e o celular responde."),
  ("falta", "No app: conversa com a Emma por texto", "Perguntar e aprovar as ações dela."),
  ("falta", "No PC (lumna-controle): info, power, schedule-*, auth-*, emma e emma-decide", "O PC novo hoje não entende essas mensagens; sem isso o OnePlus e o app novo não têm resposta."),
  ("testar", "Connect de arquivos e avisos do PC novo com o OnePlus", "O app antigo já fala o protocolo; a parte nova do PC só foi testada com um celular falso."),
  ("pergunta", "O app novo substitui o LUMNA Controle ou fica ao lado?", "Estou assumindo que o LUMNA Controle cresce e passa a ser o app único (controle e Connect)."),
 ]),
 ("5. Apps do sistema", "No desktop novo só existe a captura de tela. A lista abaixo é o que você pediu; a coluna do meio diz se o LUMNA antigo (Vue) já tem uma versão para portar.", [
  ("ref", "Explorador de arquivos", "No antigo: FilesApp.vue. No novo: não existe."),
  ("ref", "Player de música", "No antigo: MusicApp.vue (fila, letras, Qobuz, saídas). No novo: não existe."),
  ("falta", "Player de vídeo", "O mpv por baixo, com os controles do LUMNA. Não existe em nenhum dos dois."),
  ("ref", "Galeria", "No antigo: GalleryApp.vue. (O \"galeria\" do Gravity é a vitrine dos componentes, não um app de fotos.)"),
  ("falta", "Bloco de notas", "Não existe em nenhum dos dois."),
  ("pergunta", "LUMNA Code", "Não existe. O que ele é? Um editor de código, com a Emma ao lado?"),
  ("pergunta", "Reminders", "Não existe como app. Há lembretes na Emma e o agendamento do Connect como base. Um app de lembretes com avisos?"),
  ("pergunta", "StoryTeller", "Não existe. É o app de escrever e organizar as suas histórias, com a Emma (as pastas por história já existem no servidor)?"),
  ("ref", "Outros que o antigo tinha", "Loja (StoreApp.vue) e Telefone (PhoneApp.vue)."),
  ("falta", "Outros em aberto", "Navegador, visualizador de imagens e PDF, terminal, calculadora, monitor do sistema. Diga quais entram."),
  ("pergunta", "Em Gravity (C) ou aproveitando o shell Vue?", "Estou assumindo que tudo novo é em Gravity, como o resto do desktop novo, e que o antigo serve de referência."),
 ]),
 ("6. Para o desktop ser a sessão do sistema", "Hoje ele roda numa janela dentro do KDE. Isto é o que falta para ser a sessão que você abre no login.", [
  ("falta", "Rodar o Orbit como sessão de verdade", "A entrada \"LUMNA · Orbit\" do greetd existe em session/ e nunca foi testada na sua máquina."),
  ("falta", "A tela principal no Orbit real", "tela_principal só vale no aninhado; no real precisa apontar para o Odyssey."),
  ("falta", "Desligar um monitor de verdade", "No \"só um lado\" o código existe e nunca rodou em hardware."),
  ("falta", "Teclado que abre sozinho nos apps de fora", "Hoje só abre nos campos do próprio LUMNA."),
  ("falta", "Navegador", "Testar Epiphany, Firefox e Chromium no Wayland e avaliar o LUMNA Browser."),
  ("falta", "Instalador e GRUB do LUMNA", "Particionamento, perfis de instalação e boot (docs/PROXIMOS.md)."),
 ]),
]

# ------------------------------------------------------------------------------------- PARA VOCÊ TESTAR (em casa)
testar = [
 ("YouTube TV (a interface de TV)", "As setas movem a seleção entre os vídeos, o OK abre o vídeo, o voltar volta para a lista e o play/pausa e o ±10 s funcionam dentro do vídeo. Me diga o que aparece na tela se algo falhar."),
 ("Nunca dois vídeos juntos", "Abra um vídeo, aperte Home, abra outro serviço e outro vídeo: só um deve tocar por vez. Voltar até a primeira página deve sair do serviço e pausar. (A aba fica viva: para retomar, aperte play.)"),
 ("Netflix e os outros serviços", "Abrir e tocar. É aqui que o Widevine (o decodificador de vídeo protegido) pode dar problema na primeira vez."),
 ("Painel: Por modo", "Telas e modos > Monitores > Como usar as telas > Por modo. Os seletores de cada tela agora abrem; escolha Mídia numa e Desktop noutra."),
 ("Passar janelas entre monitores", "Alt+← e Alt+→ levam a janela para o outro monitor. No \"só a da esquerda ou da direita\", as janelas da tela apagada devem ir para a que ficou, visíveis."),
 ("Topbar por monitor e o botão de tela cheia", "Painel > Aparência > Barra superior > Telas com a barra. O botão ao lado dos ícones sai da tela cheia e volta."),
 ("Connect no OnePlus com o PC novo", "Avisos do PC e do celular, enviar e receber arquivos e a área de transferência. Os recursos de energia, agendamento e senhas ainda não existem no PC novo."),
 ("Captura de tela", "Gravar com áudio e Enviar para o celular. Só foram testados com programas falsos."),
 ("A Emma", "Gerar imagem, um servidor MCP de verdade, o Oi Emma com o microfone e ela falando."),
]

# ------------------------------------------------------------------------------------------------- O QUE FOI TESTADO
confirmado = [
 "O perfil NSFW com o PIN da barra",
 "A captura de tela com a prévia e o escurecimento corrigido",
 "O Orbit aninhado com uma janela por monitor, a tela cheia e o mouse no lugar certo",
 "A Mídia no YouTube de desktop: a extensão no Developer Edition, o login do Google e os botões do controle (play, pausa, ±10 s, início, voltar, menu)",
 "O controle do Samsung (LUMNA Controle) com o PC, depois de instalar o wtype",
 "A conta ligada no YouTube TV (a interface de TV abriu)",
]
simulado = [
 "O Connect de arquivos e avisos (só com um celular falso)",
 "A topbar por monitor e o botão de tela cheia",
 "Espelhar, só a da esquerda ou da direita e o modo por tela",
 "Os seletores de modo do Painel e passar janelas entre monitores (testei com um mouse e um falso KDE)",
 "Nunca dois vídeos juntos (testei com o Firefox ESR e vídeos locais)",
 "Imagens da Emma, servidores MCP, o Oi Emma e a voz",
 "A gravação de tela com áudio",
]

proximos = [
 ("Quando você chegar: testar a lista \"Para você testar\"", "Principalmente o YouTube TV e o Connect do OnePlus; o que falhar eu corrijo antes de começar coisa nova."),
 ("O Início do Gaming (a biblioteca)", "É a maior parte visível que falta e o antigo já tem a referência."),
 ("O PC novo entender o Connect completo (info, energia, agendamento, senhas, Emma)", "Destrava o OnePlus e é a base do app Android novo."),
 ("Painel do dia a dia: Energia, Som e Inicialização", "Sem eles o desktop ainda não serve como sessão única."),
 ("Os apps do sistema, um por vez", "Começando pelo Explorador, o player de vídeo e a Galeria; os que você ainda não descreveu esperam a sua resposta."),
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

tst = "".join(f'<li><b>{E(a)}</b><span>{E(b)}</span></li>' for a, b in testar)
cf = "".join(f"<li>{E(x)}</li>" for x in confirmado)
sm = "".join(f"<li>{E(x)}</li>" for x in simulado)
px = "".join(f'<li><b>{E(a)}</b><span>{E(b)}</span></li>' for a, b in proximos)

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
<p class="sub">As cinco frentes que você listou, mais o que falta para o desktop virar a sessão do sistema. Cada linha tem uma etiqueta: <b>Feito</b>, <b>Falta testar</b>, <b>Falta</b>, <b>Portar do antigo</b> (já existe no LUMNA antigo, em Vue, como referência) ou <b>Pergunta</b> (preciso de uma decisão sua).</p>
{backlog_html()}</section>

<section class="sec" id="testar"><h2>Para você testar</h2>
<p class="sub">Está pronto, mas só você pode confirmar na sua máquina. Quando algo falhar, o mais útil é me dizer <b>o que aparece na tela</b>.</p>
<ol class="nx">{tst}</ol></section>

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
