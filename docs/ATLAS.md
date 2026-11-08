# Atlas (`lumna-atlas`), o explorador de arquivos

O explorador de arquivos do LUMNA OS, em Gravity (C/Clay). Fatias prontas: **a navegação** (a tela do mockup) e **as operações** (copiar, recortar, colar, renomear, lixeira com desfazer, apagar para sempre, compactar e extrair), o **menu de contexto**, o **caminho digitado com sugestões** e a **Lixeira**. Também: o **Início**, a **Lista grande** e as **páginas de configurações** (engrenagem). As **abas**. Faltam: a página Rede e nuvem, busca em subpastas.

## O que tem

- **Barra de ferramentas**: voltar, avançar, subir, caminho clicável (Home / Documentos / Projetos), busca, **+** (nova pasta, novo arquivo de texto), ordenar (nome, data, tamanho, tipo, crescente/decrescente), grade/lista e ajustes (mostrar ocultos, painel de detalhes).
- **Barra lateral**: Espaços (Início, Recentes, Favoritos), Coleções (Home, Documentos, Imagens, Músicas, Downloads, Vídeos, Templates e a pasta da Emma, só as que existem) e Fixados (pastas favoritas).
- **Grade e lista**: só desenha o que está à vista (pasta com milhares de itens não pesa); miniatura nas imagens (decodificada numa thread); a quantidade de itens nas pastas; os ícones de pasta e arquivo (PDF, MD, ODS, ODP, ...) são desenhados à mão.
- **Filtros** (Tudo, Pastas, Documentos, Imagens, Mídia, Outros): só aparecem os que separam alguma coisa.
- **Painel de detalhes**: prévia (imagem ou ícone), tipo, itens, tamanho (a pasta é somada, até 20 mil entradas), local, modificado, Favoritar e as permissões. Vários selecionados mostram o total.
- **Seleção**: clique; Ctrl+clique soma; Shift+clique faixa; clique no vazio limpa; duplo clique (ou Enter) abre.
- **Submenus** (menu de contexto): "Cor da pasta" e "Ícone da pasta" (e "Fixar na barra lateral", "Compartilhar", "Ordenar por") abrem num popup ao lado do menu, como no mockup; a cor de agora fica com o visto.
- **Propriedades** (Ctrl+I ou no menu): diálogo com abas **Geral** (nome editável, local com copiar e "Abrir pasta", tipo, tamanho somado numa thread, criado e modificado em, "Abrir com" que troca o aplicativo padrão do tipo), **Permissões** (grupo e os três níveis, "Modo atual" ao vivo, "aplicar também aos itens da pasta"), **Detalhes** (MIME, inode, links...) e, num atalho `.desktop`, **Aplicativo** (versão, pacote, origem, executável). **Aplicar** renomeia e muda o que mudou; **Cancelar** não toca em nada.
- **Início**: o clique num cartão entra na pasta; o botão direito (ou o dedo parado) abre o menu com Abrir, Abrir em nova aba, Abrir em nova janela e **Remover da página inicial**.
- **Abas**: Ctrl+T abre uma aba no Início; "Abrir em nova aba" no menu de uma pasta abre a pasta numa aba logo depois da de agora; o **+** da faixa e o **X** de cada aba; Ctrl+W fecha a aba (a última fecha a janela); Ctrl+Tab / Ctrl+Shift+Tab (ou Ctrl+PgDn/PgUp) trocam; Ctrl+1…8 vão direto e Ctrl+9 vai para a última. Cada aba tem o seu histórico (voltar/avançar) e onde a lista estava rolada. A faixa só aparece com duas ou mais abas; no máximo 12. (O Alt+número é das áreas de trabalho do Orbit.)
- **Teclado**: setas, Home/End, Enter, Backspace (sobe), Ctrl+A, Ctrl+F (busca), Ctrl+H (ocultos), Ctrl+R, Esc (limpa busca/seleção), Alt+←/→/↑ (o Alt é a tecla do Orbit: ele toma essas teclas; use os botões).
- **Menu de contexto** (botão direito, ou dedo parado meio segundo): Abrir, Favoritar, Copiar caminho; no vazio: Nova pasta, Atualizar.
- A pasta é vigiada (inotify): o que muda por fora aparece sozinho.
- Abrir um arquivo usa o aplicativo padrão do tipo (Painel > Aplicativos padrão); abertos entram nos **Recentes**.

## Operações (`servicos/arquivos_op.c`)

- **Copiar, recortar e colar** (Ctrl+C/X/V, menu): numa thread, com a barra de progresso e **Cancelar** na barra de status. Colar na própria pasta faz "nome (2)". Entre janelas funciona (um arquivo em `$XDG_RUNTIME_DIR` guarda "copiar/recortar" e os caminhos; o texto da área de transferência leva os mesmos caminhos, para colar num terminal). Os recortados ficam esmaecidos.
- **Nada é sobrescrito sem perguntar**: já existe um item com esse nome → *Pular*, *Manter os dois* ou *Substituir* (e "fazer o mesmo para os próximos"). O que é substituído vai antes para a lixeira. Pasta com pasta junta.
- **Lixeira do freedesktop** (`~/.local/share/Trash/files` + `info`): o que apagamos aqui o resto do sistema também vê, e vice-versa. Item na lixeira de outro disco usa a do GIO (sem desfazer). Na tela da Lixeira: Restaurar (volta ao lugar de origem; se já está ocupado, volta como "nome (2)"), Excluir permanentemente, Esvaziar.
- **Desfazer** (Ctrl+Z, botão na barra de status por 10 s): mover volta, lixeira restaura, cópia/compactar/extrair/criar vão para a lixeira, renomear volta o nome. Apagar para sempre (Shift+Del, com confirmação) não tem volta.
- **Renomear** (F2, menu): o nome vira um campo; Enter confirma, Esc cancela. Recusa nome vazio, com `/`, ou que já existe.
- **Compactar** (`.zip`, pelo `bsdtar`) e **Extrair aqui** (qualquer pacote que o `bsdtar` abra; se o pacote já traz uma pasta só, não faz "Projetos/Projetos").
- Também no menu: Abrir em nova janela, Abrir no terminal (Pulse na pasta), Copiar caminho, Compartilhar > celulares conectados (LUMNA Connect), Propriedades (Ctrl+I), Fixar na barra lateral (Espaços, Coleções, Favoritos; o submenu abre no lugar, não ao lado).

## O menu do espaço vazio

Botão direito (ou o dedo parado) fora dos itens: Nova pasta, Novo arquivo de texto, Colar, **Ordenar por** (nome, data, tamanho, tipo, crescente/decrescente), Selecionar tudo, Atualizar, **Abrir no terminal aqui**, **Git clone...**, **Cor e ícone desta pasta**, Copiar caminho, Propriedades. (Na primeira fatia o pedido estava ligado a um elemento que não existia e o menu nunca abria: corrigido e agora no teste automático.)

- **Git clone**: pede o endereço (https, ssh, um caminho local), clona numa pasta nova com o nome do repositório (se existir, "nome (2)"), com o progresso do git na barra de status e Cancelar. Endereço que começa com `-` é recusado. Repositório que pede senha **ainda não** (falha em vez de travar num prompt escondido).
- **Cor e ícone da pasta** (também no menu de uma pasta): 9 cores e 16 emblemas, valem na hora, guardados em `~/.config/lumna/atlas/pastas.ini`.

## O Início

A página inicial do Atlas (Barra lateral > Início): **Pastas principais** (cartões das coleções, com o emblema de cada uma) e **Unidades e dispositivos** (o disco do sistema e o que estiver montado em `/mnt`, `/media` ou `/run/media`, com a barra de uso). É onde o Atlas abre, a menos que você escolha outra coisa.

## Configurações (a engrenagem)

A janela troca para as páginas de configurações, com **Aplicar**, **Cancelar** (descarta) e **Restaurar padrão** (da página). Tudo vale na hora ao aplicar. Esc volta aos arquivos. Ficam em `~/.config/lumna/atlas/` (`config.ini`, `barra.ini`, `visoes.ini`).

- **Prévia nas pastas**: na grade, cada pasta mostra até quatro miniaturas do que há dentro (com "Exibir prévias nas pastas" ligado; pastas com emblema mostram só o emblema).
- **Exibição**: visualização padrão (**Grade**, **Lista** ou **Lista grande**), tamanho das pastas (64-192 px, o mesmo do controle da barra de status), tamanho do texto, mostrar ocultos, mostrar extensões, pastas antes dos arquivos, ordenar por, e "usar como padrão nas novas pastas" ou "lembrar a visualização de cada pasta"; com a **prévia ao vivo** ao lado.
- **Página inicial**: o que abre (a página inicial, a última pasta, ou uma pasta que você escolher), as seções do Início e quais pastas principais aparecem.
- **Barra lateral**: o que aparece (o olho), a ordem (setas), renomear, mudar o caminho e a categoria (Espaços/Coleções), adicionar e remover atalhos, com a prévia. "Fixar na barra lateral" do menu de contexto escreve aqui.
- **Avançado**: o que fazer quando já existe um arquivo com o nome (perguntar, substituir, manter os dois, pular), confirmar a exclusão permanente e o esvaziar da lixeira, e o registro de erros (`~/.local/state/lumna/atlas.log`).

## O caminho digitado (Ctrl+L, ou clicar no vazio do caminho)

O caminho vira um campo. Sugere as pastas que começam com o que foi digitado (sem caixa nem acento) e os recentes que combinam; ↑/↓ escolhem, **Tab** completa e entra na pasta, **Enter** abre, **Esc** cancela. `~` é a casa e um nome solto vale a partir da pasta de agora.

## Onde está

- `gravity/servicos/arquivos_op.{c,h}`: as operações acima, sem desenho.
- `gravity/servicos/arquivos.{c,h}`: o que sabe do disco (listar, ordenar "natural" sem caixa nem acento, filtrar, tipos, tamanhos e datas em português, histórico, caminhos, favoritos e recentes). Sem desenho, testado sem tela.
- `gravity/exemplos/atlas.c`: o app.
- Favoritos: `~/.config/lumna/atlas/favoritos`; fixados na barra lateral: `~/.config/lumna/atlas/fixados`; recentes: `~/.local/share/lumna/atlas/recentes`.

## Testes

```
sh gravity/testes/atlas.sh                 # o serviço, numa árvore de teste
make -C gravity arquivos
sh gravity/testes/atlas.sh PASTA_DO_BUILD_DO_ORBIT   # + o app inteiro num Orbit headless (toques e teclas de verdade)
```

## Ainda não

Do mockup: a página **Rede e nuvem** (conectar a SMB/SFTP, contas na nuvem) e a seção Rede da barra lateral, os botões "..." em cada item, as opções "Pausar transferências ao fechar", "Transferências simultâneas", "Verificar cópias" e o cache de miniaturas da página Avançado, arrastar para reordenar a barra lateral (por ora, setas), Etiquetas. Também: arrastar e soltar, discos e USB, "Abrir com...", busca dentro das subpastas, selecionar o nome sem a extensão ao renomear. 

**Só provado** no Orbit headless (wlroots 0.18), com toques e teclas simuladas: o menu de contexto foi aberto e tem as entradas, mas clicar nos submenus (Fixar, Compartilhar) não está no teste automático (a janela de teste é baixa e o menu rola); o enviar ao celular não foi testado (não há celular). Nunca com mouse, teclado e tela de verdade.
