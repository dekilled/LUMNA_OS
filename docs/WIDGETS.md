# Widgets da área de trabalho (`lumna-widgets`)

Os widgets são cartões numa camada do compositor **atrás das janelas e na frente do papel de parede**, no canto de cima à direita. A área de trabalho ainda não tem ícones; os widgets são o que a deixa útil e bonita sem eles.

**Hoje há um: o do LUMNA Music.** Fechando a janela do Music com uma música tocando, ele vira um cartão: capa, título, artista, o tempo andando e os botões anterior, tocar/pausar e próxima. Tocar no cartão (fora dos botões) abre o Music de novo, e o cartão some. Sem o Music em segundo plano, o widget fica em 1 pixel, invisível e sem pegar cliques.

Como funciona: o `lumna-widgets` é um processo do Gravity (uma camada pequena, que só recebe o ponteiro no cartão) que lê o que toca por MPRIS (`servicos/mpris.c`) e olha, pelo foreign-toplevel (`gv_janelas`), se o Music tem janela. Entra na sessão junto com a barra e a dock (`session/lumna-orbit-session`) e tem de estar no pacote `lumna-gravity`.

**Os outros widgets** (relógio, clima, calendário, notas…) e o jeito de a pessoa escolher, posicionar e arrastar os cartões esperam o seu esboço: por ora o código está pensado para ganhar cartões, mas só o do Music foi entregue.

Arquivos: `gravity/exemplos/widgets.c`. Teste: `gravity/testes/musica/teste_widget.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY` (a janela aberta, fechar, os botões, tocar no cartão), e `testes/musica.sh` o roda junto com os do Music. Roteiro: `docs/TESTES-ISO.md`, seção 2i.
