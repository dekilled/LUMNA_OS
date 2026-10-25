# Gravity

A interface do LUMNA OS em C. O LUMNA roda em cima do **Orbit** (o compositor) e é desenhado pelo
**Gravity**: componentes com a cara do LUMNA, leves (o mixer inteiro usa ~2,5 MB só dele, o resto
são bibliotecas partilhadas) e mudáveis por tema — cor, tamanho, cantos, texto e movimento.

## Como funciona

- **Modo imediato.** A app tem uma função `desenhar` que declara a tela inteira a cada quadro, com o
  layout do [Clay](https://github.com/nicbarker/clay) (flexbox em C: `CLAY(id, {...}) { filhos }`) e
  os componentes `gv_*`. Não há árvore de objectos para manter: o estado é o da app.
- **Só desenha quando precisa:** entrada, tamanho novo ou uma animação a correr, ao ritmo do ecrã
  (frame callback do Wayland). Parado, não gasta nada.
- **Wayland direto:** uma janela (xdg-shell) ou uma camada do LUMNA (layer-shell: barra, painel,
  teclado, bloqueio), com rato, **toque** (o primeiro dedo) e teclado, na escala do ecrã.
- **Desenho:** cairo; **texto:** pango (acentos, emoji, ligaduras, a fonte do tema).
- **Ícones:** os mesmos 130 do shell (`shell/src/components/Icon.vue`), lidos do traço SVG.
  `sh gravity/ferramentas/icones.sh` regenera `src/gv_icones.c` quando o shell ganha ícones.

## Componentes

| | |
|---|---|
| `gv_texto`, `gv_rotulo` | texto com tamanho, cor e peso |
| `gv_icone` | um ícone do LUMNA |
| `gv_botao`, `gv_botao_ext` | botão cheio (acento), elevado, de contorno, simples ou de perigo, com ícone e/ou texto; o `_ext` cresce e desativa |
| `gv_botao_redondo` | botão redondo (ligado / perigo), como os do mixer e dos ajustes rápidos |
| `gv_deslizante` | volume, brilho... (arrastar com o dedo ou o rato) |
| `gv_interruptor` | liga/desliga animado |
| `gv_campo` | campo de texto: normal, busca (lupa e ✕), senha (pontos e o olho), número, e-mail, URL |
| `gv_campo_area` | campo de várias linhas: cresce até um máximo e rola por dentro; Enter quebra, Ctrl+Enter envia |
| `gv_linha` | linha de ajuste: ícone, título, subtítulo e › ou interruptor (como no Painel de controle) |
| `gv_abas` | abas com o sublinhado do acento |
| `gv_barra_janela` | a barra de título: ícone, título, minimizar, maximizar, fechar; arrastar move |
| `GV_POPUP`, `gv_popup_*`, `gv_menu_item` | popup preso a um elemento, numa superfície própria (sai da janela ou da barra), com sombra; fecha ao tocar fora ou com Esc |
| `gv_lista_suspensa` | dropdown: a opção escolhida e as outras num popup da largura dela |
| `gv_janelas`, `gv_janela_ativar/fechar` | as janelas abertas no compositor (wlr-foreign-toplevel), para a dock |
| `gv_app_entrada_em` | só um elemento recebe o toque (a dock numa camada larga) |
| `gv_menu_contexto` | pediram o menu deste elemento: botão direito, segurar o dedo, tecla Menu |
| `gv_imagem` | PNG, JPEG, WebP, SVG: cobrir, conter ou esticar, nos cantos do elemento, esmaecendo ao chegar |
| `gv_icone_app` | o ícone de um app pelo tema de ícones (o `Icon=` do .desktop) |
| `gv_rolagem` | área que rola: dedo com inércia e elástico, roda do rato, barra que aparece e some |
| `gv_separador` | linha de 1 px na cor da borda |
| `gv_cartao()` | a declaração de um cartão: superfície, borda, cantos e sombra |
| `gv_sombra()` | sombra macia em qualquer elemento |
| `gv_anim`, `gv_anim_cor` | qualquer número ou cor animado até ao alvo |
| `gv_tocado`, `gv_premido`, `gv_por_cima`, `gv_tecla`, `gv_tecla_carregada`, `gv_modificadores` | interação |
| `gv_copiar`, `gv_colar` | área de transferência (entre apps) |
| `gv_focar`, `gv_focado` | o campo com o foco |

## Teclado

Um foco para a app inteira. **Tab** e **Shift+Tab** andam pelos controles na ordem em que
aparecem, **Enter** ou **Espaço** ativam, e o anel violeta só aparece ao navegar pelo teclado
(o rato e o dedo focam sem anel). Nas abas **← →** trocam; no deslizante as setas andam de 5%
(Shift: 1%) e Home/End vão às pontas; numa lista suspensa fechada **↑ ↓** trocam a escolha.
Um popup aberto pelo teclado puxa o foco para dentro (para a opção marcada), as setas andam
pelos itens, **Esc** fecha e o foco volta para onde estava. Um elemento feito à mão entra na
fila com `gv_focavel(id, raio)`.

## Escrever

O campo de texto faz o que se espera de um campo:

- **teclado físico:** letras com os acentos do ABNT (as teclas mortas ´ ~ ^ pelo *compose* do
  xkb), a tecla segura repete, ← → (Ctrl: palavra a palavra), Home/End, Shift para selecionar,
  Backspace/Delete (Ctrl: a palavra), Ctrl+A, Ctrl+C/X/V, Enter, Esc;
- **rato e dedo:** tocar põe o cursor, arrastar seleciona, dois toques selecionam a palavra;
- **teclado no ecrã do LUMNA** (text-input-v3): o campo diz que é um campo (e se é senha, número,
  e-mail...), manda o texto à volta e onde está o cursor; o que o teclado escreve entra, e a
  palavra a compor aparece sublinhada;
- **senha:** pontos, um olho para mostrar, e não deixa copiar para fora;
- o cursor pisca sem gastar: o Gravity só acorda para cada piscada.

## Tema

`gv_t` é o tema activo; os componentes lêem dele a cada quadro, então mudar um campo muda a tela.
De fábrica é a paleta nova do LUMNA, token a token:

| Token | Campo | Cor | Onde |
|---|---|---|---|
| bg | `fundo` | `#141518` | fundo geral |
| surface | `superficie` | `#1D1E22` | janelas e conteúdo |
| raised | `elevada` | `#25262A` | barras, menus, cartões, teclas, campos |
| hover | `sob_cursor` | `#2D2E33` | superfície sob o cursor |
| selected | `selecionado` | `#2B2340` | item selecionado |
| border | `borda` | `#303238` | divisórias e bordas discretas |
| border-control | `borda_controle` | `#4A4C53` | contorno de controles |
| text | `texto` | `#F4F1FA` | texto principal e ícones |
| text-secondary | `texto_secundario` | `#BCB5CE` | texto secundário |
| text-muted | `texto_suave` | `#8E869F` | texto auxiliar, dicas |
| accent | `acento` | `#A78BFA` | ações principais, deslizantes, janela ativa |
| accent-hover | `acento_cursor` | `#B79FFF` | ação principal sob o cursor |
| accent-pressed | `acento_premido` | `#8F73DC` | ação principal premida |
| on-accent | `sobre_acento` | `#160F26` | texto sobre o violeta |
| focus | `foco` | `#C4B5FD` | anel de foco |
| success / warning / danger / info | `sucesso` / `aviso` / `perigo` / `info` | `#78C6A3` / `#E4BB77` / `#E58B98` / `#86B8E8` | estados |
| disabled | `desativado` | `#625B70` | controles desativados |
| grip-idle | `divisor` | `#8E869F` | divisor em repouso (ativo: acento) |

As superfícies (bg, surface, raised, hover, border, border-control) são cinzas neutros, tirados
das telas novas: o violeta fica só no acento, na seleção e no papel de parede.

Mais `veu` (scrim, o escuro por trás de um diálogo) e `icone`. Cantos: 6 nos controles, 8 nas
linhas, 12 nas janelas e cartões. Um `tema.ini` muda o que quiser
sem recompilar, pelo nome do campo ou do token (`accent = #f472b6`) — `~/.config/gravity/tema.ini` ou `GRAVITY_TEMA=caminho`. Exemplo em
[`exemplos/tema.ini`](exemplos/tema.ini): cores, raios, espaçamento, grossura dos ícones, fonte,
tamanhos de texto, durações, curva (`suave`, `mola`, `linear`) e animações ligadas ou não.

## Compilar e testar

Precisa de `wayland`, `wayland-protocols`, `libxkbcommon`, `cairo`, `pango` e `gdk-pixbuf2` (SVG pelo
`librsvg`), e o mixer do
`libpulse` (no Arch: `sudo pacman -S --needed wayland wayland-protocols libxkbcommon cairo pango
gdk-pixbuf2 librsvg libpulse`). O som responde pelo `pipewire-pulse`.

## Serviços

Fora da interface, em `servicos/`, o que as peças do LUMNA usam do sistema:

- `som.c` — saídas, volume e mudo de cada saída e de cada app tocando, a saída padrão; assina as
  mudanças (as teclas, a topbar, outra app) e avisa a app por um descritor que o Gravity vigia
  (`gv_app_vigiar`).

```
cd gravity
make
./mixer              # o mixer de áudio do LUMNA (som de verdade), numa janela
./mixer --camada     # como no celular: uma camada por cima de tudo
./galeria            # os componentes, com o tema mudando ao vivo
./wifi               # conectar ao Wi-Fi: busca, lista e senha (os campos de texto)
./painel             # o Painel de controle das telas novas: abas, busca, atalhos em duas colunas
./apps               # os apps instalados (.desktop) com os ícones do tema, rolagem e busca; tocar abre
./papel [imagem]     # o papel de parede: uma camada no fundo da tela
./barra              # a topbar do desktop novo; o Bluetooth abre o menu da tela nova
./rede               # a janela de Rede da tela nova: cartões que abrem e listas suspensas
./transferencia      # a área de transferência do Celular Connect: texto de várias linhas
./dock               # a dock: apps fixos (~/.config/lumna/dock), abertos e o ativo
```

Roda em qualquer compositor Wayland (Hyprland, Sway, Orbit...). Dentro do Orbit, o toque de teste
dele também serve: `orbit-msg toque baixo 0 X Y`.

## Uma app mínima

A lanterna: um botão redondo que acende (e o ícone acende junto).

```c
#include "gravity.h"

static bool acesa;

static void desenhar(gv_app *app, void *dados) {
    CLAY(CLAY_ID("raiz"), { .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) }, .childGap = 16,
        .layoutDirection = CLAY_TOP_TO_BOTTOM, .childAlignment = { CLAY_ALIGN_X_CENTER, CLAY_ALIGN_Y_CENTER } } }) {
        if (gv_botao_redondo(CLAY_ID("lanterna"), "flashlight", 72, acesa, false)) {
            acesa = !acesa;
        }
        gv_texto(acesa ? CLAY_STRING("Lanterna acesa") : CLAY_STRING("Lanterna apagada"),
            gv_t.tamanho_corpo, gv_t.texto_suave, GV_PESO_NORMAL);
    }
}

int main(void) {
    gv_app *app = gv_app_criar(&(gv_opcoes){ .titulo = "Lanterna", .largura = 240, .altura = 240 });
    return app ? gv_app_correr(app, desenhar, NULL) : 1;
}
```

`cc lanterna.c -Isrc -Ivendor/clay libgravity.a $(pkg-config --cflags --libs wayland-client wayland-cursor xkbcommon cairo pangocairo glib-2.0) -lm`

## O que vem a seguir

- listas que rolam com física de dedo e vários dedos (pinça, deslizar);
- imagens (ícones das apps, capas de álbum);
- o Wi-Fi ligado ao NetworkManager, e outras peças do LUMNA em Gravity (a barra de volume das
  teclas, a central);
- as janelinhas de sugestão do teclado no ecrã e campos de várias linhas;
- desenho na GPU quando o cairo não chegar.

O Clay vem em `vendor/clay` (licença zlib), no commit `e6cc369` do repositório dele.
