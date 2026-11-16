# lumna-navegador (CEF)

Navegador do LUMNA. Hoje existe a **Mídia** (versão simples, para a TV/sala): início com "Continuar assistindo" e os atalhos dos serviços, um serviço de streaming por vez e um menu de troca. Veja `docs/NAVEGADOR.md` para as decisões e o desenho.

## Compilar e abrir
```
# precisa de um CEF extraído (o scripts/teste-cef-codecs.sh baixa um com H.264/AAC em ~/.cache/lumna-cef-teste/godwinmix)
sudo pacman -S --needed base-devel cmake ninja libx11 gtk3
CEF_ROOT=~/.cache/lumna-cef-teste/godwinmix sh navegador/dev.sh          # compila, monta navegador/build/stage e abre a Mídia
CEF_ROOT=... sh navegador/dev.sh compilar                                # só compila e monta
```
O Widevine é baixado pelo próprio CEF na primeira vez (de 1 a 3 minutos, depois **reinicie o app**); fica no perfil `~/.local/share/lumna/navegador/perfil`.

## O controle (gamepad), o Super+M e o celular

**Super+M** (o seletor do Orbit, `seletor_midia`) chama `lumna-navegador --menu`, que fala com a instância que está rodando (socket `$XDG_RUNTIME_DIR/lumna-navegador.sock`): `--menu` abre/fecha o menu de serviços, `--inicio` vai ao início, `--estado` devolve `{"ok":true,"estado":"inicio|servico|menu","ativo":"...","controle":"setas|cursor|"}`. Para usar a nova Mídia, no `orbit.ini`: `seletor_midia = lumna-navegador --menu` (hoje o padrão ainda é o `lumna-midia tela seletor` da Mídia antiga). O celular (LUMNA Connect) já funciona sem mudança: ele manda teclado e ponteiro virtuais pelo Orbit.

O controle vem do serviço **`lumna-gamepad`**, que o navegador assina (`assinar`: botões e analógicos). Só age com a **janela da Mídia em foco** (com um jogo na frente o controle é do jogo). Mapeamento:

| | Início, menu e splash (a interface) | Num serviço aberto, modo **setas** (o padrão) | Num serviço aberto, modo **cursor** |
|---|---|---|---|
| Direcional | navega (setas) | setas no site | setas no site |
| Analógico esquerdo | navega (UM eixo, o dominante) | **setas** (também um eixo só): move o foco entre os itens | **move o ponteiro de verdade** (o do Orbit) |
| Analógico direito | — | **rola** a página (roda do mouse mandada direto ao site) | **rola** (rolagem do Orbit) |
| A | confirma (Enter) | **Enter** (o `nav.js` clica no item em foco; num site de TV age como o OK do controle remoto) | **clique** (segura enquanto apertado: arrasta) |
| B | volta (Esc; fecha o menu) | sai da tela cheia; senão o "voltar" do serviço: a página anterior (`historico`) ou Esc (`esc`, o YouTube TV) | idem |
| X / Y | favorita / mais tarde (no catálogo) | Espaço (play/pausa) / `f` (tela cheia) | idem |
| LB / RB | troca de aba | Page Up / Page Down (ao soltar) | idem |
| **LB + RB juntos** | — | troca **setas ↔ cursor** só neste site (até sair dele), com aviso na tela; não manda Page Up/Down | idem |
| **Start** | abre/fecha o menu de serviços (o mesmo do F2 e do Super+M) | idem | idem |
| **Select** | vai ao início | idem | idem |
| Guia | é do sistema (`guia=` em `gamepad.ini`: use `nada` para ele não abrir o LUMNA Jogos por cima) | idem | idem |

**Como o controle chega em qualquer site.** O navegador injeta o `ui/midia/nav.js` em toda página nova (`Page.addScriptToEvaluateOnNewDocument`, depois de `Page.enable`: sem ele o registro não vale): as setas movem o foco para o item clicável mais perto naquela direção (contorno violeta), o Enter clica, e sem para onde ir a seta volta a rolar a página. Ele sai de fora de vídeo tocando em tela cheia e de qualquer página com o **User-Agent de TV**. Para os serviços marcados `ua_tv` (o YouTube: `youtube.com/tv`) o navegador manda o User-Agent de uma TV (`Emulation.setUserAgentOverride`, que troca o do pedido **e** o de `navigator.userAgent`, os dois que o app de TV confere); ao abrir outro serviço ele volta ao normal. Esse User-Agent vale para a visão inteira (não só para os domínios do serviço, como fazia a extensão do Firefox): serve para o YouTube, que não redireciona para o login do Google. O modo de cada serviço (`modo`: `setas`|`cursor`) e o "voltar" (`voltar`: `historico`|`esc`) ficam em `servicos.json` e se mudam no menu de ações (tecla M / LT) do serviço na tela de início.

**Streamings.** A fila mostra **6 por tela**: ao passar do sexto a fila inteira desliza para fora e entram os seguintes (os recém-adicionados e o **+ Adicionar**), com pontinhos indicando a página; voltar ao sexto desliza de volta. Na tela de início o último cartão é **+ Adicionar**: abre a biblioteca (Netflix, Disney+, Prime, HBO Max, Paramount+, Apple TV+, Globoplay, Crunchyroll, Twitch, Plex...) e o formulário de **Outro site** (nome + endereço; sem teclado físico use `lumna-navegador --adicionar NOME https://endereço`). O menu de ações do serviço (M / LT) tem **Favorito** (o favorito vem primeiro e já nasce em foco), **modo de controle** e **Remover**. Teste: `navegador/testes/servicos.py`.

Detalhes que custaram um bug cada: o ponteiro fica **12 px dentro** da janela (nas bordas fica a moldura invisível de redimensionar, e clicar ali arrasta a janela; a página nem recebe o ponteiro); a rolagem do Orbit (`mouse rolar`) vale ~1 px a cada 15 unidades (calibrado); o analógico segurado parado não gera evento, então o navegador guarda o último valor e continua andando a 60 Hz; no modo cursor o analógico esquerdo **só** move o ponteiro (no modo setas ele vira setas; o serviço marca cada direção com `analogico: true|false`). O direcional segurado repete (400 ms, depois a cada 110 ms). Na interface o **analógico vale por um eixo só, o dominante** (um analógico de verdade nunca é reto: sem isso empurrar para a direita marcava também "cima" e a interface pulava de zona); entra a 55% do curso e sai abaixo de 35%.

## O catálogo (as suas listas)

As abas **Filmes**, **Séries** e **Seus vídeos** mostram o **catálogo pessoal**: o que você salvou para ver depois, os favoritos e o que já assistiu (não é um catálogo de streaming, é o que *você* guarda). **Salvar:** num serviço aberto, **LT** salva em *Assistir mais tarde* e **RT** nos *Favoritos* (ou `lumna-navegador --salvar [depois|favorito|assistido]`, para um atalho do Orbit ou o celular). O navegador lê a página (Open Graph e JSON-LD/schema.org: título, tipo filme/série/vídeo, gênero, duração `PT2H18M`, capa), **baixa a capa** (`og:image`; sem capa, usa o **print** da tela) e guarda em `catalogo.json` + `thumbs/cat-*`. O mesmo link salvo de novo atualiza o **mesmo** item (um item pode ser favorito e "mais tarde" ao mesmo tempo). Um aviso aparece pelo `notify-send` (se houver).

Na tela do catálogo (igual ao esboço): chips **Todos/Filmes/Séries/Vídeos**, **busca**, **gênero**, **ordem**, **grade ou lista** e **Minhas listas** (alterna Todos → Assistir mais tarde → Favoritos → Já assisti); o cartão tem coração (favorito), relógio (mais tarde), progresso, etiqueta *Assistido*, serviço e *Link salvo*/*Arquivo local*. **Teclas:** setas navegam, Enter abre o link no serviço, **F** favorita, **W** mais tarde, **A** assistido, **M** abre o menu de ações (inclui *Remover*), **E** a Emma, **Esc** volta ao início, **Page Up/Down** troca de aba. **No controle:** direcional/analógico navegam, A abre, B volta, **X** favorita, **Y** mais tarde, **LT** o menu de ações, **LB/RB** trocam de aba.

## Teclas (provisórias)
| Tecla | O que faz |
|---|---|
| ← → ↑ ↓ Enter | escolhe e abre (no início e no menu) |
| **F2** | abre/fecha o menu de serviços (o site encolhe e segue tocando) |
| **Esc** (no menu) | volta ao vídeo |
| **Alt+Home** | volta ao início |

## Variáveis de ambiente
`LUMNA_NAV_DADOS` (pasta de dados), `LUMNA_NAV_SERVICOS` (JSON `[{"id","nome","url"}]` com outra lista de serviços), `LUMNA_NAV_TELA_CHEIA=1`, `LUMNA_NAV_OZONE=wayland` (padrão: x11), `LUMNA_NAV_SEM_GPU=1`, `LUMNA_NAV_DEBUG=1` (log `NAV ...` no terminal), `LUMNA_NAV_ZOOM_MENU=0.72` (a escala da página enquanto o menu está aberto; 0.72 é o padrão, escolhido pelo Eric; 1.0 desliga; lido ao abrir o app), `LUMNA_NAV_SEM_ANIMACAO=1` (o site pula direto ao tamanho final, sem animar), `LUMNA_NAV_ANIMACAO=real` (o jeito antigo: redimensiona o site a cada quadro; dá flick em páginas pesadas), `LUMNA_NAV_LARGURA/ALTURA`, `LUMNA_NAV_UI`.

## Testes automáticos
- `sh navegador/testes/midia.sh <pasta do orbit> navegador/build/stage` (Orbit sem tela + XWayland; só pelo teclado).
- `python3 navegador/testes/catalogo.py <pasta do orbit> navegador/build/stage` (salvar de páginas com Open Graph/JSON-LD, a miniatura, a tela do catálogo inteira pelo teclado; `CATALOGO_PRINTS=/pasta` guarda os prints).
- `python3 navegador/testes/controle.py <pasta do orbit> navegador/build/stage gravity` (o serviço `lumna-gamepad` real com um controle de mentira, o Orbit, o navegador e uma página "sonda" que anota no título as teclas, o mouse, os cliques e a rolagem que recebe; precisa do binário `gravity/gamepad`: `make -C gravity gamepad`).

## Como é feito (e por quê)
- **Uma janela, duas camadas** (CEF Views, sem moldura): a interface em HTML (`ui/midia/`) por baixo e o site do serviço por cima. A janela é estilo **Chrome** (precisa dele: é o que baixa o Widevine sozinho) e esse estilo só aceita **um** BrowserView Chrome (o site); a interface é um BrowserView **Alloy**. **A ordem importa:** o site entra primeiro e a interface depois (e desce para o fundo com `ReorderChildView`); ao contrário, o CEF recusa ("Cannot add multiple Chrome style BrowserViews").
- **Ponte JS↔C++** (`window.cefQuery`) só existe na nossa interface (`file://`), nunca nos sites.
- **Um serviço vivo por vez:** trocar de serviço guarda o último endereço, o progresso do vídeo e um print (lidos pelo DevTools: `Runtime.evaluate` e `Page.captureScreenshot`) em `midia.json` e `thumbs/`, e reaproveita a mesma visão.

## Se algo ficar errado nos "Continuar assistindo"
Os dados da Mídia ficam em `~/.local/share/lumna/navegador/`: `midia.json` e `thumbs/` (o que a Mídia guarda) e `perfil/` (o navegador em si: **logins e cookies**, não apague). Para recomeçar só o "Continuar assistindo": `rm -rf ~/.local/share/lumna/navegador/midia.json ~/.local/share/lumna/navegador/thumbs`.

## A animação do site (por que é assim)
Redimensionar a página de um serviço a cada quadro (60 vezes por segundo) faz o renderizador refazer o layout toda vez e dá **flick** em páginas pesadas (Disney+). Redimensionar **uma vez** (instantâneo) é limpo. Por isso, no modo padrão ("imagem"), o site de verdade **não é redimensionado durante a animação**: a interface mostra um **print** dele (ou, ao trocar de serviço, uma **splash** com o nome) e o anima com CSS (leve); o site vai para **baixo** da interface na ordem das camadas e, no fim, é colocado no tamanho final de uma vez e volta para cima. O menu sobe/desce **junto** com a escala (a mesma hora, a mesma duração). A página só começa a carregar depois de o site antigo sair da frente.

**Zoom do menu.** Com o menu aberto o site fica menor e a página se reajusta; para ela ficar condizente, usa-se escala 0,72 (`LUMNA_NAV_ZOOM_MENU`). É a emulação do DevTools `Emulation.setDeviceMetricsOverride` com **três parâmetros juntos**: `width`/`height` (viewport CSS = tamanho ÷ zoom), `deviceScaleFactor` = zoom **e `scale` = zoom**. **Sem o `scale`** a página é diagramada no viewport grande mas **desenhada em 1:1** e estoura a janela (o botão da direita cortado): `dpr` e `innerWidth` medem certo e enganam, só os **pixels** mostram. Não é o `SetZoomLevel` do CEF: este mostra a **bolha "100%"** do Chrome e **fica gravado no perfil por site**.

**Descobertas (cada uma custou um bug):**
1. Com o site **visível por cima**, o Chromium trata a interface de baixo como "em segundo plano" e atrasa os temporizadores dela para 1 s. As flags `--disable-background-timer-throttling`, `--disable-renderer-backgrounding` e `--disable-backgrounding-occluded-windows` resolvem (a flag `--disable-features=...` derruba o app).
2. **Escondido** (`SetVisible(false)`) o CEF **não entrega teclas a nenhuma visão** (o F2/Esc morrem na tela de carregamento); movido para **fora da tela** (`x=-30000`) o CEF o recoloca na janela. O que funciona é **trocar a ordem das camadas** (`ReorderChildView`).
3. **F2 e Alt+Home** são tratados no `OnPreKeyEvent` dos dois navegadores (não só como atalho da janela, que não chega quando a interface tem o foco).
4. Cada transição tem um número, e avisos e alarmes (watchdog) de transições antigas são ignorados; o F2 cancela a espera de uma página que demora.
5. Um evento de layout durante o `LoadURL` chamava `Aplicar()` e o site antigo pulava para a tela cheia: a transição é marcada como ativa **antes** de tudo.

## Login com o Google (e outros popups de login)

Os botões "Entrar com o Google" abrem um **popup** (`window.open`) que devolve o resultado à página por `window.opener`/`postMessage`. Antes, qualquer popup era carregado **na mesma visão** (como os links "nova aba"), o que quebrava essa conversa e deixava a tela preta. Agora um `window.open` abre uma **janela própria e pequena** (app_id `lumna-navegador-popup`) com o `opener` intacto; ela fecha sozinha (`window.close`) e a página de trás continua na tela. Os links `target=_blank` seguem abrindo na mesma visão. Com o popup aberto, o direcional, o analógico (como setas) e o A vão para **ele** (o `nav.js` entra na página do popup), e **B** volta a página dele ou, sem história, **cancela** (fecha). Por que janela à parte: o CEF exige que o popup tenha o mesmo estilo (Chrome) do site, e a janela da Mídia só aceita uma visão Chrome (necessária para o Widevine). Digitar e-mail/senha ainda pede o **teclado físico** (ou o celular): falta o teclado na tela. Teste: `controle.py`, seção 7d (páginas `login.html` e `popup.html`).
