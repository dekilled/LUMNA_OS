# Arquivos (`lumna-arquivos`, "LUMNA Explorer")

O explorador de arquivos do LUMNA OS, em Gravity (C/Clay). Esta é a **primeira fatia: a navegação**, a tela do mockup "LUMNA Explorer". As operações (copiar, mover, renomear, lixeira com desfazer), discos e USB, abas e a busca em subpastas vêm nas próximas fatias.

## O que tem

- **Barra de ferramentas**: voltar, avançar, subir, caminho clicável (Home / Documentos / Projetos), busca, **+** (nova pasta, novo arquivo de texto), ordenar (nome, data, tamanho, tipo, crescente/decrescente), grade/lista e ajustes (mostrar ocultos, painel de detalhes).
- **Barra lateral**: Espaços (Início, Recentes, Favoritos), Coleções (Home, Documentos, Imagens, Músicas, Downloads, Vídeos, Templates e a pasta da Emma, só as que existem) e Fixados (pastas favoritas).
- **Grade e lista**: só desenha o que está à vista (pasta com milhares de itens não pesa); miniatura nas imagens (decodificada numa thread); a quantidade de itens nas pastas; os ícones de pasta e arquivo (PDF, MD, ODS, ODP, ...) são desenhados à mão.
- **Filtros** (Tudo, Pastas, Documentos, Imagens, Mídia, Outros): só aparecem os que separam alguma coisa.
- **Painel de detalhes**: prévia (imagem ou ícone), tipo, itens, tamanho (a pasta é somada, até 20 mil entradas), local, modificado, Favoritar e as permissões. Vários selecionados mostram o total.
- **Seleção**: clique; Ctrl+clique soma; Shift+clique faixa; clique no vazio limpa; duplo clique (ou Enter) abre.
- **Teclado**: setas, Home/End, Enter, Backspace (sobe), Ctrl+A, Ctrl+F (busca), Ctrl+H (ocultos), Ctrl+R, Esc (limpa busca/seleção), Alt+←/→/↑ (o Alt é a tecla do Orbit: ele toma essas teclas; use os botões).
- **Menu de contexto** (botão direito, ou dedo parado meio segundo): Abrir, Favoritar, Copiar caminho; no vazio: Nova pasta, Atualizar.
- A pasta é vigiada (inotify): o que muda por fora aparece sozinho.
- Abrir um arquivo usa o aplicativo padrão do tipo (Painel > Aplicativos padrão); abertos entram nos **Recentes**.

## Onde está

- `gravity/servicos/arquivos.{c,h}`: o que sabe do disco (listar, ordenar "natural" sem caixa nem acento, filtrar, tipos, tamanhos e datas em português, histórico, caminhos, favoritos e recentes). Sem desenho, testado sem tela.
- `gravity/exemplos/arquivos.c`: o app.
- Favoritos: `~/.config/lumna/arquivos/favoritos`; recentes: `~/.local/share/lumna/arquivos/recentes`.

## Testes

```
sh gravity/testes/arquivos.sh                 # o serviço, numa árvore de teste
make -C gravity arquivos
sh gravity/testes/arquivos.sh PASTA_DO_BUILD_DO_ORBIT   # + o app inteiro num Orbit headless (toques e teclas de verdade)
```

## Ainda não

Copiar/colar/mover/renomear/apagar (lixeira), arrastar e soltar, abas, discos e USB, "Abrir com...", Compartilhar (pelo LUMNA Connect), Etiquetas, as seções Rede (Compartilhadas, Nuvem) e "Personalizar barra lateral" do mockup, o caminho editável (Ctrl+L), busca dentro das subpastas. A dock e a pasta padrão (`inode/directory`) **ainda apontam para o Thunar**: trocam quando as operações chegarem.

Só rodou no Orbit headless (wlroots 0.18), sem teclado/mouse/tela de verdade.
