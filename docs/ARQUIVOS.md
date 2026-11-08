# Arquivos (`lumna-arquivos`, "LUMNA Explorer")

O explorador de arquivos do LUMNA OS, em Gravity (C/Clay). Fatias prontas: **a navegação** (a tela do mockup "LUMNA Explorer") e **as operações** (copiar, recortar, colar, renomear, lixeira com desfazer, apagar para sempre, compactar e extrair), o **menu de contexto**, o **caminho digitado com sugestões** e a **Lixeira**. Faltam: cartões de pasta com prévias, as páginas de configurações (Exibição, Barra lateral...), abas, discos e USB, busca em subpastas.

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

## Operações (`servicos/arquivos_op.c`)

- **Copiar, recortar e colar** (Ctrl+C/X/V, menu): numa thread, com a barra de progresso e **Cancelar** na barra de status. Colar na própria pasta faz "nome (2)". Entre janelas funciona (um arquivo em `$XDG_RUNTIME_DIR` guarda "copiar/recortar" e os caminhos; o texto da área de transferência leva os mesmos caminhos, para colar num terminal). Os recortados ficam esmaecidos.
- **Nada é sobrescrito sem perguntar**: já existe um item com esse nome → *Pular*, *Manter os dois* ou *Substituir* (e "fazer o mesmo para os próximos"). O que é substituído vai antes para a lixeira. Pasta com pasta junta.
- **Lixeira do freedesktop** (`~/.local/share/Trash/files` + `info`): o que apagamos aqui o Thunar e os outros também veem, e vice-versa. Item na lixeira de outro disco usa a do GIO (sem desfazer). Na tela da Lixeira: Restaurar (volta ao lugar de origem; se já está ocupado, volta como "nome (2)"), Excluir permanentemente, Esvaziar.
- **Desfazer** (Ctrl+Z, botão na barra de status por 10 s): mover volta, lixeira restaura, cópia/compactar/extrair/criar vão para a lixeira, renomear volta o nome. Apagar para sempre (Shift+Del, com confirmação) não tem volta.
- **Renomear** (F2, menu): o nome vira um campo; Enter confirma, Esc cancela. Recusa nome vazio, com `/`, ou que já existe.
- **Compactar** (`.zip`, pelo `bsdtar`) e **Extrair aqui** (qualquer pacote que o `bsdtar` abra; se o pacote já traz uma pasta só, não faz "Projetos/Projetos").
- Também no menu: Abrir em nova janela, Abrir no terminal (Pulse na pasta), Copiar caminho, Compartilhar > celulares conectados (LUMNA Connect), Propriedades (Ctrl+I), Fixar na barra lateral (Espaços, Coleções, Favoritos; o submenu abre no lugar, não ao lado).

## O caminho digitado (Ctrl+L, ou clicar no vazio do caminho)

O caminho vira um campo. Sugere as pastas que começam com o que foi digitado (sem caixa nem acento) e os recentes que combinam; ↑/↓ escolhem, **Tab** completa e entra na pasta, **Enter** abre, **Esc** cancela. `~` é a casa e um nome solto vale a partir da pasta de agora.

## Onde está

- `gravity/servicos/arquivos_op.{c,h}`: as operações acima, sem desenho.
- `gravity/servicos/arquivos.{c,h}`: o que sabe do disco (listar, ordenar "natural" sem caixa nem acento, filtrar, tipos, tamanhos e datas em português, histórico, caminhos, favoritos e recentes). Sem desenho, testado sem tela.
- `gravity/exemplos/arquivos.c`: o app.
- Favoritos: `~/.config/lumna/arquivos/favoritos`; fixados na barra lateral: `~/.config/lumna/arquivos/fixados`; recentes: `~/.local/share/lumna/arquivos/recentes`.

## Testes

```
sh gravity/testes/arquivos.sh                 # o serviço, numa árvore de teste
make -C gravity arquivos
sh gravity/testes/arquivos.sh PASTA_DO_BUILD_DO_ORBIT   # + o app inteiro num Orbit headless (toques e teclas de verdade)
```

## Ainda não

Do mockup: cartões de pasta com prévias do conteúdo e a visão "Lista grande", os botões "..." em cada item, as páginas de configurações (Exibição, Página inicial, Barra lateral, Rede e nuvem, Avançado), Etiquetas, as seções Rede (Compartilhadas, Nuvem), "Abrir em nova aba" (não há abas ainda). Também: arrastar e soltar, discos e USB, "Abrir com...", busca dentro das subpastas, selecionar o nome sem a extensão ao renomear. A dock e a pasta padrão (`inode/directory`) **ainda apontam para o Thunar**; trocam com o seu OK.

**Só provado** no Orbit headless (wlroots 0.18), com toques e teclas simuladas: o menu de contexto foi aberto e tem as entradas, mas clicar nos submenus (Fixar, Compartilhar) não está no teste automático (a janela de teste é baixa e o menu rola); o enviar ao celular não foi testado (não há celular). Nunca com mouse, teclado e tela de verdade.
