# Atlas (`lumna-atlas`), o explorador de arquivos

O explorador de arquivos do LUMNA OS, em Gravity (C/Clay). Fatias prontas: **a navegação** (a tela do mockup) e **as operações** (copiar, recortar, colar, renomear, lixeira com desfazer, apagar para sempre, compactar e extrair), o **menu de contexto**, o **caminho digitado com sugestões** e a **Lixeira**. Também: o **Início**, a **Lista grande** e as **páginas de configurações** (engrenagem). As **abas** e a **Rede e nuvem**. Faltam: busca em subpastas.

## O que tem

- **Barra de ferramentas**: voltar, avançar, subir, caminho clicável (Home / Documentos / Projetos), busca, **+** (nova pasta, novo arquivo de texto), ordenar (nome, data, tamanho, tipo, crescente/decrescente), grade/lista e ajustes (mostrar ocultos, painel de detalhes).
- **Barra lateral**: Espaços (Início, Recentes, Favoritos), Coleções (Home, Documentos, Imagens, Músicas, Downloads, Vídeos, Templates e a pasta da Emma, só as que existem) e Fixados (pastas favoritas).
- **Grade e lista**: só desenha o que está à vista (pasta com milhares de itens não pesa); miniatura nas imagens (decodificada numa thread); a quantidade de itens nas pastas; os ícones de pasta e arquivo (PDF, MD, ODS, ODP, ...) são desenhados à mão.
- **Filtros** (Tudo, Pastas, Documentos, Imagens, Mídia, Outros): só aparecem os que separam alguma coisa.
- **Painel de detalhes**: prévia (imagem ou ícone), tipo, itens, tamanho (a pasta é somada, até 20 mil entradas), local, modificado, Favoritar e as permissões. Vários selecionados mostram o total.
- **Seleção**: clique; Ctrl+clique soma; Shift+clique faixa; clique no vazio limpa; duplo clique (ou Enter) abre.
- **Submenus** (menu de contexto): "Cor da pasta" e "Ícone da pasta" (e "Fixar na barra lateral", "Compartilhar", "Ordenar por") abrem num popup ao lado do menu, como no mockup; a cor de agora fica com o visto.
- **Propriedades** (Ctrl+I ou no menu): diálogo com abas **Geral** (nome editável, local com copiar e "Abrir pasta", tipo, tamanho somado numa thread, criado e modificado em, "Abrir com" que troca o aplicativo padrão do tipo), **Permissões** (grupo e os três níveis, "Modo atual" ao vivo, "aplicar também aos itens da pasta"), **Detalhes** (MIME, inode, links...) e, num atalho `.desktop`, **Aplicativo** (versão, pacote, origem, executável). **Aplicar** renomeia e muda o que mudou; **Cancelar** não toca em nada.
- **Discos e pendrives no Início**: os montados (o do sistema, os de /media, /run/media e /mnt) aparecem com a barra de uso; um pendrive ou disco externo que está ligado e **ainda não montado** aparece como cartão com "Montar": tocar monta (udisks, sem pedir nada) e abre a pasta. A lista se refaz sozinha a cada poucos segundos (o lsblk roda numa thread). No botão direito de um pendrive montado há "Ejetar" (desmonta e desliga). As outras partições do mesmo disco do sistema (o Windows de um dual boot) não entram: o Painel trata como "do sistema". Para montar sozinho ao conectar ou ao entrar, **Painel > Armazenamento > Discos e pendrives** (quem faz é o `lumna-discos`, que sobe com a sessão; só monta removíveis).
- **Início**: o clique num cartão entra na pasta; o botão direito (ou o dedo parado) abre o menu com Abrir, Abrir em nova aba, Abrir em nova janela e **Remover da página inicial**.
- **Botões "..."**: cada arquivo ou pasta (na grade e na lista) ganha um "..." quando o ponteiro está em cima ou o item está selecionado; ele abre o mesmo menu do botão direito.
- **Menu de contexto da barra lateral**: botão direito (ou segurar e soltar sem andar, com o dedo) num atalho: Abrir, Abrir em nova aba, Abrir em nova janela, Esconder (e, nas pastas fixadas, Remover da barra lateral), Esvaziar a lixeira (na Lixeira) e Personalizar barra lateral. A nova janela também abre em `@recentes`, `@favoritos`, `@rede`, `@nuvem` e `@inicio`.
- **Reordenar a barra lateral arrastando**: com o mouse, aperte num item e ande; com o dedo, segure meio segundo e então ande (andar logo rola a lista). Uma linha mostra onde o item cai, inclusive em outra seção (Espaços ↔ Coleções), um fantasma acompanha o ponteiro, soltar grava e Esc cancela. As setas de Configurações > Barra lateral continuam.
- **Arrastar arquivos**: aperte num arquivo (ou em vários selecionados) com o mouse e ande: um fantasma do ícone acompanha o ponteiro. Soltar numa pasta da grade ou da lista, num atalho de pasta da barra lateral ou no fundo da lista **copia** para lá (o original fica; soltar na pasta de onde veio não faz nada). O contorno da pasta de destino acende. Vem do Início, da Rede ou da Lixeira não: ali o arraste não começa. Com o dedo o toque longo continua abrindo o menu (arrastar arquivos com o dedo não existe ainda). Fora do Atlas, o que cair é um `text/uri-list`: outros apps que aceitam arquivos recebem os mesmos caminhos.
- **Rede e nuvem** (Configurações > Rede e nuvem, e "Compartilhadas" e "Nuvem" na barra lateral, na seção Rede):
  - **Compartilhamentos**: "Conectar a um servidor" aceita `servidor/pasta`, `smb://...`, `sftp://usuario@servidor/...`, `\\servidor\pasta` do Windows, ftp, WebDAV (`https://` vira `davs://`) e nfs; outros esquemas (`file://`...) são recusados. Conectar e desconectar é o **gvfs**, numa thread; o gvfs pede usuário/senha/domínio (ou a escolha, como a chave de um servidor SFTP novo) num diálogo do Atlas; "Lembrar a senha" deixa o gvfs guardar no **chaveiro do sistema** (o Atlas nunca grava senha). O "..." de cada um: Abrir, Abrir em nova aba, Conectar/Desconectar, Copiar endereço, Esquecer. A lista fica em `~/.config/lumna/atlas/rede.ini`.
  - **Preferências**: "Reconectar ao abrir o Atlas" (conecta de novo, sem pedir senha, os que estavam conectados) e "Mostrar locais indisponíveis" (os desconectados aparecem em Compartilhadas).
  - **Contas na nuvem** (precisa do **rclone**): Nextcloud e WebDAV (endereço, usuário e senha; no Nextcloud basta o endereço do servidor) e Google Drive, Dropbox e OneDrive (o rclone abre o navegador para você entrar; o Atlas nunca vê a senha). A conta vira uma **pasta local** (`~/Nuvem/NOME`, editável) mantida igual à da nuvem por `rclone bisync` (a primeira vez com `--resync`, que junta os dois lados sem apagar nada). "Sincronizar em segundo plano" repete a cada 10 minutos **enquanto o Atlas está aberto**; "Sincronizar agora" faz na hora. As contas ficam num `rclone.conf` só do Atlas (0600, senha ofuscada pelo `rclone obscure`, nunca na linha de comando).
- **Abas**: Ctrl+T abre uma aba no Início; "Abrir em nova aba" no menu de uma pasta abre a pasta numa aba logo depois da de agora; o **+** da faixa e o **X** de cada aba; Ctrl+W fecha a aba (a última fecha a janela); Ctrl+Tab / Ctrl+Shift+Tab (ou Ctrl+PgDn/PgUp) trocam; Ctrl+1…8 vão direto e Ctrl+9 vai para a última. Cada aba tem o seu histórico (voltar/avançar) e onde a lista estava rolada. A faixa só aparece com duas ou mais abas; no máximo 12. (O Alt+número é das áreas de trabalho do Orbit.)
- **Teclado**: setas, Home/End, Enter, Backspace (sobe), Ctrl+A, Ctrl+F (busca), Ctrl+H (ocultos), Ctrl+R, Esc (limpa busca/seleção), Alt+←/→/↑ (o Alt é a tecla do Orbit: ele toma essas teclas; use os botões).
- **Menu de contexto** (botão direito, ou dedo parado meio segundo): Abrir, Favoritar, Copiar caminho; no vazio: Nova pasta, Atualizar. Ele abre **fora dos limites da janela**: o Orbit o vira para cima ou o desliza para caber na tela, e só rola se o menu for mais alto que a tela.
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

Do mockup: as opções "Pausar transferências ao fechar", "Transferências simultâneas", "Verificar cópias" e o cache de miniaturas da página Avançado, Etiquetas. Também: mover (em vez de copiar) ao arrastar, arrastar com o dedo, discos e USB, sincronização da nuvem com o Atlas fechado, busca dentro das subpastas, selecionar o nome sem a extensão ao renomear. 

**Só provado** no Orbit headless (wlroots 0.18), com toques e teclas simuladas: o menu de contexto foi aberto e tem as entradas, mas clicar nos submenus (Fixar, Compartilhar) não está no teste automático (a janela de teste é baixa e o menu rola); o enviar ao celular não foi testado (não há celular). Nunca com mouse, teclado e tela de verdade.
