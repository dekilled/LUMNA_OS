# Área de trabalho (ícones e pastas)

`lumna-desktop` mostra o que está na pasta **Área de trabalho** da pessoa como ícones numa grade, **em cada tela**. É um serviço Gravity que roda na camada de
fundo, logo acima do papel de parede (`exemplos/desktop.c`).

- **A pasta é própria e começa vazia: `~/Desktop`.** Se o `xdg-user-dirs` aponta a Área de trabalho para a pasta pessoal inteira (é o que ele grava quando a
  pessoa não tem uma pasta de desktop), isso é ignorado: senão os ícones seriam tudo o que há na casa (cache, programas instalados...). Uma pasta de desktop
  de verdade (diferente da casa) é respeitada.
- **Uma área de trabalho por tela.** A da tela principal abre uma instância para cada outra tela (como o papel de parede) e as fecha quando a tela sai.
  **Cada ícone mora numa tela só**: o lugar é guardado por tela no `~/.config/lumna/desktop.ini` (`[posicoes]` é a principal, `[posicoes NOME]` as outras).
  Arrastar um ícone de uma tela para a outra muda o ícone de tela (o arquivo não sai da pasta). Ícone sem lugar, ou de uma tela que saiu, fica na principal.
  Soltar em cima de uma pasta, de qualquer tela, move o arquivo para dentro dela.

## Decisões (do Eric)

- A fonte é a **pasta de verdade** (não uma lista própria): o Atlas, o terminal e o resto do sistema veem os mesmos arquivos.
- Serviço próprio na camada de fundo, e não dentro do `lumna-papel`.
- v1: abrir/selecionar/arrastar de lugar, menu de contexto e criar pasta, arrastar de e para o Atlas.

## O que faz

- **Selecionar:** clique, Ctrl+clique soma, Shift+clique seleciona o intervalo, arrastar no vazio desenha o retângulo, Ctrl+A, clique no vazio limpa.
- **Abrir:** duplo clique (ou Enter). Pasta abre no Atlas, atalho `.desktop` executa o app, o resto abre no programa padrão (o Signals, para texto).
- **Mudar de lugar:** arrastar o ícone; ele encaixa na grade (o ícone que já estava lá não é atropelado: o arrastado vai para a célula livre mais perto). Os lugares ficam em
  `~/.config/lumna/desktop.ini`. Se a tela encolher, os ícones que não cabem aparecem em outro lugar **sem perder o lugar guardado**.
- **Arrastar e soltar** (`text/uri-list`, o mesmo do Atlas):
  - de um ícone para uma pasta da área de trabalho: **move** para dentro dela;
  - de fora (Atlas, outro app) para o desktop: **copia** para a Área de trabalho e o ícone aparece onde foi solto;
  - do desktop para o Atlas ou outro app: o destino recebe os arquivos.
- **Botão direito** no ícone: Abrir, Copiar, Recortar, Renomear, Lixeira. No vazio: Nova pasta, Novo documento, Colar e os atalhos de sempre do desktop (terminal, Atlas,
  todos os apps, Emma, papel de parede, configurações).
- **Teclado** (depois de clicar no desktop): F2 renomeia, Delete manda para a lixeira, Ctrl+C/X/V copia, recorta e cola (a mesma área de transferência de arquivos do Atlas),
  Ctrl+Z desfaz (cópia, mudança e lixeira), Ctrl+Shift+N cria uma pasta, Esc limpa a seleção.
- Os arquivos que aparecem ou somem na pasta por fora (terminal, download) aparecem ou somem no desktop na hora (inotify).
- Já existe um nome igual no destino: fica os dois ("nome (2)"). O desktop não abre janela de pergunta.
- Os ícones são os mesmos do Atlas: o desenho foi extraído para `servicos/aricone.c` e os dois usam.

## Como se encaixa

- Fica no mesmo nível do papel de parede (o fundo), e **o Orbit o mantém sempre por cima do papel** (`orbit_layers_sync_hidden`), não importa quem nasceu
  primeiro. Antes valia a ordem de criação (com uma espera de 0,9 s ao iniciar): no PC do Eric o papel nascia depois e engolia os cliques, e o menu que
  aparecia era o do papel de parede, sem Nova pasta.
- Nas telas de Gaming e Mídia ele some junto com o resto do desktop (camada de fundo).

## Testes

`sh testes/desktop_telas.sh PASTA_DO_ORBIT`: dois monitores e um `xdg-user-dirs` apontando para a pasta pessoal: a área de trabalho nasce vazia, o ícone mora
numa tela só, arrastar de uma tela para a outra (e de volta) e soltar numa pasta. Voltar a aceitar a casa como Desktop faz o teste falhar.


`sh testes/desktop_app.sh PASTA_DO_ORBIT` (Orbit sem tela, com clientes reais): lista e posições, seleção, retângulo, duplo clique em atalho e em pasta, renomear (e o lugar
acompanha o nome novo), lixeira e desfazer, nova pasta com nome, arrastar para outro lugar, arrastar para dentro de uma pasta, arquivo criado e apagado por fora.
Desligar "soltar em cima da pasta move" faz o teste falhar. O `orbit-msg mouse botao baixo|cima direito` clica com o botão direito.

Dois bugs que o teste achou e já corrigi: a primeira grade (chutada antes do primeiro quadro) gravava lugares errados; e o aviso de "pasta mudou" cancelava o renomear logo
depois de criar uma pasta.

## O que não foi provado / falta

- **Não testei** arrastar **do Atlas para o desktop** nem **do desktop para o Atlas** (só desktop → pasta do próprio desktop). O código usa o mesmo protocolo do Atlas, mas só
  duas janelas de verdade provam.
- Não testei na sessão completa (papel de parede + barra + dock + widgets juntos), nem com **dois monitores**, nem o **toque** (dedo parado abre o menu, arrastar com o dedo).
- Os menus foram vistos num screenshot, não clicados por teste automático.
- Não tem os ícones de sistema (Pasta pessoal, Lixeira, Computador), nem **Propriedades**, nem miniatura de imagem, nem ordenar/organizar por nome/tipo/data, nem
  tamanho de ícone ajustável, nem arrastar para a lixeira. O ícone de um `.desktop` é a folha com "APP", não o ícone do aplicativo.
- Navegar por setas entre ícones.
