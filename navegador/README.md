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

## Teclas (provisórias)
| Tecla | O que faz |
|---|---|
| ← → ↑ ↓ Enter | escolhe e abre (no início e no menu) |
| **F2** | abre/fecha o menu de serviços (o site encolhe e segue tocando) |
| **Esc** (no menu) | volta ao vídeo |
| **Alt+Home** | volta ao início |

## Variáveis de ambiente
`LUMNA_NAV_DADOS` (pasta de dados), `LUMNA_NAV_SERVICOS` (JSON `[{"id","nome","url"}]` com outra lista de serviços), `LUMNA_NAV_TELA_CHEIA=1`, `LUMNA_NAV_OZONE=wayland` (padrão: x11), `LUMNA_NAV_SEM_GPU=1`, `LUMNA_NAV_DEBUG=1` (log `NAV ...` no terminal), `LUMNA_NAV_ZOOM_MENU=0.72` (a escala da página enquanto o menu está aberto; 0.72 é o padrão, escolhido pelo Eric; 1.0 desliga; lido ao abrir o app), `LUMNA_NAV_SEM_ANIMACAO=1` (o site pula direto ao tamanho final, sem animar), `LUMNA_NAV_ANIMACAO=real` (o jeito antigo: redimensiona o site a cada quadro; dá flick em páginas pesadas), `LUMNA_NAV_LARGURA/ALTURA`, `LUMNA_NAV_UI`.

## Teste automático
`sh navegador/testes/midia.sh <pasta do orbit> navegador/build/stage` (Orbit sem tela + XWayland; só pelo teclado).

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
