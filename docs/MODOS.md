# Telas, modos e espaços de trabalho (Orbit)

Como o Orbit decide **onde** cada janela fica e **quem aparece** em cada monitor. Este doc reúne as regras que estavam espalhadas em `mode.c`,
`arrange.c`, `view.c`, `layer.c` e `xwl.c`.

## O modelo

- **Cada tela (monitor) tem um modo:** Desktop, Gaming ou Mídia. Trocar o modo de uma tela não mexe nas outras.
- **Cada modo tem espaços de trabalho.** O Desktop tem de 1 a 3 espaços numerados, e o número sai do Painel > Áreas de trabalho. Gaming e Mídia têm **um espaço cada**,
  e esse espaço existe uma vez só no sistema.
- **O modo de uma tela é o espaço que ela mostra** (`output->tag`): espaços 1-3 são Desktop, o 4 é o Gaming e o 5 é a Mídia. Trocar de modo nunca fecha nem move
  janela. Cada janela pertence a um espaço (`view->tag`) e fica nele.
- **Pedir um modo que outra tela já tem move o modo para a tela pedida**, com as janelas dele. A outra tela volta ao Desktop dela, ou fica com o modo especial
  que a pedida tinha (o Gaming e a Mídia trocam de monitor).
- **Trocar as telas** (`orbit-msg trocar_telas`) é a única ponte entre uma tela em Gaming/Mídia e o resto. O conteúdo das duas telas troca de lugar.

## Ilha: uma tela em Gaming ou Mídia

Uma tela em Gaming ou Mídia é uma **ilha** (`orbit_output_isolated`):

- As janelas dela não aparecem na dock nem no Alt+Tab e não saem dela. Janela de outra tela também não entra.
- **Só uma janela aparece por vez**, em tela cheia (modo quiosque).
- **O Desktop não aparece nela.** Papel de parede, widgets, barra e dock ficam escondidos e o fundo é preto. Continuam aparecendo as camadas *overlay*
  (avisos, teclado na tela, gerenciador de processos, capa e seletor da Mídia) e uma camada que pegou o teclado só para ela (o lançador que a pessoa
  abriu ali). Para conferir: `orbit-msg camadas`.

## As mesmas regras nos três modos

Desktop, Gaming e Mídia são um componente só (`orbit/mode.c`). O que vale para os três:

- **Uma janela pertence a um espaço e o espaço diz o modo dela.** Ela nunca muda de modo sozinha:
  - **monitor que sai** (desligado, desconectado, "só esquerdo/direito", espelhar): a janela vai para a tela que fica, **no mesmo modo** (`orbit_view_adopt`).
    A do Desktop entra no Desktop dessa tela; a do Gaming ou da Mídia continua no espaço do modo dela, escondida até alguma tela abrir esse modo.
    **O Gaming pula:** se a tela que fica está no Desktop, ela vira Gaming e o jogo aparece com o foco (se ela está na Mídia, o filme não é interrompido
    e o jogo espera no Gaming). A Mídia não pula.
    O foco do teclado não fica numa janela que sumiu (`orbit_focus_visible`);
  - **um app pedindo atenção** (xdg-activation no Wayland, `_NET_ACTIVE_WINDOW` no X11) troca o espaço do Desktop numa tela no Desktop,
    **nunca o modo de uma tela** (`orbit_activation_may_switch`);
  - **o Alt+número e o `orbit-msg espaco N`** só valem numa tela no Desktop. Sair do Gaming ou da Mídia é pelo modo, que guarda o espaço do Desktop.
- **Quem aparece numa ilha** (Gaming ou Mídia), e quem ganha o foco ao entrar nela, seguem o mesmo critério: a janela com o foco, o jogo (Gaming), a
  biblioteca (Gaming), a última usada, a última aberta (`orbit_kiosk_pick`).
- **Uma ilha nunca fica vazia:** fechou a última janela, a tela do modo volta (a biblioteca no Gaming, o Início na Mídia; no máximo uma vez a cada 5 s).
  Quem abre a biblioteca do Gaming é só o Orbit (`orbit_mode_ensure_shell`): ao entrar no Gaming, quando ele fica vazio e quando ele pula de tela.

## O papel de parede segue o modo de cada tela

O `lumna-papel` de cada tela pergunta ao Orbit o modo **daquela** tela e ouve as trocas (`servicos/modos.c`). O arquivo `~/.config/lumna/modo` guarda só
a última troca feita em qualquer tela (é do tempo em que o modo era um só): usá-lo fazia o papel de todas as telas seguir a última troca, e as imagens
"por tela" do Desktop deixavam de valer quando uma tela ia para a Mídia ou o Gaming. Teste: `gravity/testes/papel_telas.sh`.

## Onde abre uma janela nova

1. **Jogo** vai para a tela do Gaming (veja "O que é um jogo"), e o mesmo vale para a biblioteca, a Steam, o Lutris e o Heroic.
2. **Mídia** (`lumna-midia*`) vai para a tela da Mídia.
3. **O resto** vai para a tela do cursor. Se essa tela for uma ilha, a janela vai para a tela de Desktop mais à mão.
4. Sem nenhuma tela no modo da janela, ela abre na tela do cursor.

## O que é um jogo

A janela é de um jogo quando o processo dela (X11 ou Wayland) tem a marca de quem o abriu (`jogo.c`). A classe da janela não basta: o CS:GO nativo
é `csgo_linux64` e o CS2 é `cs2`. As marcas são:

- **Steam:** `SteamGameId` ou `SteamAppId` (diferente de 0) no ambiente, tanto no jogo nativo quanto no do Proton. Janela X11 com classe `steam_app_*` também conta.
- **LUMNA Jogos:** `LUMNA_JOGO` no ambiente dos jogos que ele abre por conta própria (avulsos, GOG, Epic).
- **Lutris / Heroic:** o processo foi aberto por eles (o pai é o `lutris`, o `heroic`, o `legendary` ou o `gogdl`). A janela do próprio lançador não conta.
- Se o jogo apagou a marca do próprio ambiente, a marca herdada de um pai também vale.

`orbit-msg janelas` mostra `"jogo":true` nessas janelas, e o log (`journalctl -b | grep 'xwl:'`) mostra `jogo=1` nas X11.

## Quem fica na frente no Gaming

A ordem é esta (`orbit_kiosk_pick`):

1. A janela **com o foco**, se ela for dessa tela. Quando a pessoa pede uma janela, ela vale: o Guia trazendo a biblioteca ou o `orbit-msg foco`.
2. Senão, o **jogo** (o último que teve o foco).
3. Senão, a **biblioteca** (`lumna-jogos`).
4. Senão, a última janela usada.

Com isso:

- **O jogo abre na frente, em tela cheia e com o foco.** Se o cursor estava no outro monitor, ele vai para o centro do jogo, porque senão o mouse do jogo mexeria no Desktop.
- **Janela da Steam que abre depois, ou que pede o foco (`_NET_ACTIVE_WINDOW`, xdg-activation), não tira o jogo da frente.** Ela fica por trás e aparece quando
  o jogo fecha ou quando a pessoa a pede.
- **Clicar no Desktop do outro monitor não troca o que o Gaming mostra:** o jogo continua lá.
- **O jogo fecha e volta a biblioteca,** e não a Steam.
- **Se a tela do Gaming fica vazia** (biblioteca e Steam fechadas), a biblioteca volta sozinha, no máximo uma vez a cada 5 s (o mesmo vale para a Mídia, com o Início).

A Mídia usa o mesmo critério, sem o jogo e a biblioteca: a janela com o foco, a última usada, a última que abriu.

## Diagnóstico

```
orbit-msg janelas  | tr '{' '\n' | grep -E 'jogo|steam'   # quem é jogo, onde está, se aparece
orbit-msg camadas  | tr '{' '\n'                          # barra, dock, papel de parede: em que tela e se aparecem
orbit-msg saidas   | tr '{' '\n' | grep -E 'nome|espaco|ponteiro'
journalctl -b | grep -E 'xwl:|por trás do jogo|ficou vazia'
```

## Testes

- `gravity/testes/gaming_frente.sh`: as regras do Gaming, com clientes X11 de mentira (jogo com `SteamGameId`, janelas da Steam que pedem
  o foco, app comum no Desktop), um app Wayland marcado como jogo e o papel de parede. Desligar as regras faz o teste falhar.
- `gravity/testes/trocas_x11.sh`: troca de modo (Gaming → Mídia → Desktop → Gaming), de espaço e de tela com janelas X11 abertas, um jogo
  e um app comum. Antes, a animação da troca recortava a janela X11 e o wlroots abortava, derrubando a sessão para o login. Com o Orbit antigo o teste
  reproduz o crash. Aceita um binário com ASAN: `sh trocas_x11.sh orbit orbit-asan`.
- `gravity/testes/modos_telas.sh`: modos em dois monitores (mover, trocar).
- `gravity/testes/modos_componente.sh`: o componente como um só, em dois monitores: monitor que sai com a outra tela na Mídia (o app comum não vira janela
  da Mídia) e com a Mídia nele (a janela da Mídia continua no modo, sem o foco), o `espaco` recusado numa tela em Mídia, a Mídia vazia que traz o Início
  de volta, e o Gaming que pula para a tela que ficou (e não interrompe a Mídia). Com o Orbit de antes, 11 das 26 verificações falham. Aceita um binário com ASAN: `sh modos_componente.sh <pasta> orbit-asan`.
- `gravity/testes/xwayland.sh`: as janelas X11 e o tamanho do jogo.

## O que não está coberto

- Só testado num Orbit sem tela, com clientes de mentira. A Steam e um jogo de verdade (CS2, Proton) podem ter janelas e pedidos que não simulei.
- Uma janela de diálogo que um jogo precise mostrar **por cima** dele (aceitar termos, escolher opção de lançamento): se ela for do próprio jogo,
  ela é jogo e vem para a frente. Se for da Steam, fica por trás até o jogo fechar. Ainda não há um "diálogo flutuando sobre o jogo".
- O gamescope (resolução e escala independentes do monitor) não é usado.


## Troca em carrossel (Super + Tab)

Painel > Aparência > **Animações** > **Carrossel de modos (Super + Tab)**: desligado (o padrão, a troca *Rápida*: o seletor de sempre, o segundo Super+Tab já troca) ou ligado (*Carrossel*). A opção mora na página Animações (no `modos.ini`, `[geral] troca`).

No carrossel: **Super + Tab** abre o `lumna-carrossel` na tela do cursor, com o modo de agora no centro e **sem trocar nada**; **Tab** (ou a seta direita) gira para o próximo, **Shift+Tab** (ou a seta esquerda) para o anterior, dando a volta; **soltar o Super** traz o modo do centro para a tela inteira; **Esc** cancela; **Enter** ou um clique no cartão confirma na hora; um clique fora dos cartões cancela. Soltar só o Tab não faz nada. O Super sozinho continua abrindo o Início.

Como funciona: o Orbit guarda o estado (`server->carousel_*` em `mode.c`) e lê `[geral] troca` do `modos.ini` a cada Super+Tab (então mudar no Painel vale na hora). A troca em si é a `orbit_mode_set` de sempre (a mesma do modo Rápido). O `lumna-carrossel` só desenha: ouve o evento `carrossel` do Orbit, pergunta `orbit-msg carrossel` e anima; se ele morrer, soltar o Super ainda confirma. Trocar o modo por qualquer outro caminho (barra, Painel) fecha o carrossel aberto.

**A animação é um zoom contínuo** (`lumna-carrossel`): ao **entrar**, o cartão do centro vem da tela cheia encolhendo até ser cartão, e os dois vizinhos chegam de fora da tela; ao **confirmar**, o contrário: os vizinhos se afastam para fora da tela e o do centro dá zoom até a tela cheia, esmaecendo no fim para mostrar o modo novo. Cancelar só esmaece. Uma variável (`e`, 0 a 1) comanda tudo, então outros estilos de animação cabem como outra função dessa variável.

Por trás, ao confirmar o Orbit troca o modo **na hora e sem deslizar** (o `orbit_mode_set` com `mode_instant`), e o carrossel, opaco por cima (a camada pede zona exclusiva -1: cobre a barra e a dock), faz o efeito. O seletor antigo (`lumna-modos`) **não abre** depois dessa troca.

Ilusão a saber: ao entrar, o cartão de tela cheia é a **ilustração** do modo, não a tela de verdade; ela entra esmaecendo por cima da tela real, e o fundo é o do carrossel.

Limites: os cartões são ilustrações de cada modo, **não miniaturas da tela de verdade** (decisão do Eric: mais leve); sem toque nem gamepad (só teclado e mouse).

Testes: `gravity/testes/carrossel_orbit.sh` (o Orbit: abre/gira/confirma/cancela, o Início não abre, sem deslizar e sem o seletor antigo, e o Rápido continua como antes) e `gravity/testes/carrossel_app.sh` (o programa de verdade: cobre a tela com a barra e a dock rodando, o cartão fica na tela enquanto cresce, clique no cartão, clique fora, saída do processo).


## Espaços do Desktop em círculo

Os 3 espaços do Desktop dão a volta: 1 → 2 → 3 → 1. Avançar desliza o conteúdo para a esquerda (inclusive o 3 → 1), voltar desliza para a direita; vai-se pelo caminho mais curto (1 → 3 volta pela direita). Os modos (Gaming, Mídia) seguem a ordem simples. Código: `slide_direction` em `orbit/arrange.c`. Teste: `gravity/testes/workspaces_deslize.sh`.

## Painel > Aparência > Animações

Uma página própria com o **Nível** (Completa, Simples, Desligada; Personalizada quando os ajustes finos não batem com nenhum) e o ajuste fino: animações dos apps (tema.ini), deslize ao trocar de espaço/modo (`~/.config/lumna/animacoes.ini`, `[geral] deslize`, que o Orbit lê na hora) e o carrossel. Simples = tudo menos o carrossel; Desligada = nada se move. As animações novas (janelas, lançador) entram nessa página. Teste: `gravity/testes/painel_animacoes.sh`.
