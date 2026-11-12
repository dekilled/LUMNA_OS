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
- **Se a tela do Gaming fica vazia** (biblioteca e Steam fechadas), a biblioteca volta sozinha, no máximo uma vez a cada 5 s.

A Mídia segue como era: a janela com o foco, ou a última que abriu.

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
- `gravity/testes/modos_telas.sh`: modos em dois monitores (mover, trocar).
- `gravity/testes/xwayland.sh`: as janelas X11 e o tamanho do jogo.

## O que não está coberto

- Só testado num Orbit sem tela, com clientes de mentira. A Steam e um jogo de verdade (CS2, Proton) podem ter janelas e pedidos que não simulei.
- Uma janela de diálogo que um jogo precise mostrar **por cima** dele (aceitar termos, escolher opção de lançamento): se ela for do próprio jogo,
  ela é jogo e vem para a frente. Se for da Steam, fica por trás até o jogo fechar. Ainda não há um "diálogo flutuando sobre o jogo".
- O gamescope (resolução e escala independentes do monitor) não é usado.
