# Atlas (`lumna-atlas`), o explorador de arquivos

O explorador de arquivos do LUMNA OS, em Gravity (C/Clay). Fatias prontas: **a navegação** (a tela do mockup) e **as operações** (copiar, recortar, colar, renomear, lixeira com desfazer, apagar para sempre, compactar e extrair), o **menu de contexto**, o **caminho digitado com sugestões** e a **Lixeira**. Também: o **Início**, a **Lista grande** e as **páginas de configurações** (engrenagem). As **abas** e a **Rede e nuvem**. Faltam: busca em subpastas.

## O que tem

- **Barra de ferramentas**: voltar, avançar, subir, caminho clicável (Home / Documentos / Projetos), busca, **+** (nova pasta, novo arquivo de texto), ordenar (nome, data, tamanho, tipo, crescente/decrescente), grade/lista e ajustes (mostrar ocultos, painel de detalhes).
- **Barra lateral**: Espaços (Início, Recentes, Favoritos), Coleções (Home, Documentos, Imagens, Músicas, Downloads, Vídeos, Templates e a pasta da Emma, só as que existem) e Fixados (pastas favoritas).
- **Nomes longos**: na grade o nome ganha até duas linhas (quebra nos espaços, nos `_` e `-`, ou no meio da palavra comprida) e, se ainda passar, termina em **…**; na lista, termina em **…** quando não cabe na coluna. O nome inteiro aparece no painel de detalhes ao selecionar. (Antes o nome era cortado sem aviso, e dava a impressão de um nome com letras faltando.)
- **Grade e lista**: só desenha o que está à vista (pasta com milhares de itens não pesa); miniatura nas imagens (decodificada numa thread); a quantidade de itens nas pastas; os ícones de pasta e arquivo (PDF, MD, ODS, ODP, ...) são desenhados à mão.
- **Filtros** (Tudo, Pastas, Documentos, Imagens, Mídia, Outros): só aparecem os que separam alguma coisa.
- **Painel de detalhes**: prévia (imagem ou ícone), tipo, itens, tamanho (a pasta é somada, até 20 mil entradas), local, modificado, Favoritar e as permissões. Vários selecionados mostram o total.
- **Seleção**: clique; Ctrl+clique soma; Shift+clique faixa; clique no vazio limpa; duplo clique (ou Enter) abre. **Seleção por área**: aperte no vazio da grade ou da lista e ande com o mouse: um retângulo seleciona o que ele toca (com Ctrl, soma ao que já estava selecionado). Só pega o que está à vista (a rolagem durante a área não seleciona o que saiu da tela). Com o dedo, andar continua rolando a lista.
- **Mouse: clique, duplo clique e arrasto**: o arrasto de arquivos só começa quando o mouse anda 10 px **depois de apertar** e o candidato morre ao soltar (antes o segundo clique de um duplo clique já nascia arraste se o mouse tivesse andado um pouco entre os dois); o clique pede um novo quadro na hora, para a seleção aparecer sem esperar.
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
- Também no menu: Abrir em nova janela, Abrir no terminal (Pulse na pasta), Copiar caminho, Enviar para (Área de trabalho, Documentos, E-mail, contas na nuvem e redes conectadas; `servicos/enviar.c`, o mesmo da área de trabalho) e Enviar para dispositivo… > celulares conectados (LUMNA Connect), Propriedades (Ctrl+I), Fixar na barra lateral (Espaços, Coleções, Favoritos; o submenu abre no lugar, não ao lado).

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

## Robustez (o que já derrubou o Atlas, e como se testa)

Vistos com o ASAN e com um notebook de 2011 (nov. 2026):
- **Nome de arquivo que não é UTF-8** (pendrives antigos, zips, NTFS: `café.txt` em Latin-1): `g_utf8_normalize` devolve NULL e o Atlas caía ao ordenar a pasta. Montar um pendrive antigo abre a pasta dele, por isso parecia "cair ao montar". Agora o nome vira UTF-8 válido só para ordenar, buscar e mostrar (o arquivo mantém o nome verdadeiro).
- **Recarregar a lista no meio do desenho** (Enter no renome, clique que abre uma pasta, "mostrar ocultos"): a grade e os menus liam memória já liberada. Agora a recarga pedida durante o desenho roda no quadro seguinte.
- **Ordenar uma pasta grande** calculava a chave do nome a cada comparação (milhões de normalizações para 20 mil arquivos); agora é uma por entrada.
- **A pasta mudando por fora** (download, cópia) relia tudo a cada aviso do inotify; agora os avisos se juntam (0,3 s depois do último, no máximo 1,5 s depois do primeiro).
- **Miniaturas**: só as que estão à vista entram na fila das threads (no máximo 2 por thread, na ordem da tela), e a proporção não pede mais uma segunda descodificação. Rolar depressa uma pasta de fotos não enfileira centenas de miniaturas que já saíram da tela.
- **O Orbit mandava o título da janela a todos os apps a cada commit**, e cada app redesenhava e fazia commit: 60 quadros por segundo, parado, em todos os apps Gravity (dock, barra, Atlas...). Era o "flicker" e o CPU alto em repouso.
- **Os pontinhos coloridos nas pastas** (a prévia do que há dentro) trocavam de cor sozinhos: a lista de nomes era ordenada com `strcmp` sobre os ponteiros que o `g_ptr_array_sort` entrega, uma ordem que mudava a cada leitura (e cada recarga relia). Agora a ordem é a do nome. Além disso a leitura era feita na linha principal, uma pasta por vez, com um `stat` por arquivo: abrir uma pasta com dezenas de subpastas congelava o app por segundos num HD ou pendrive. Agora uma thread lê (usando o tipo que o próprio diretório já informa), a pasta aparece na hora, as miniaturas entram quando chegam, e o resultado fica guardado pela data da pasta (uma recarga que não mudou nada não recalcula e não pisca).
- **Imagens enormes**: só o JPEG é descodificado já reduzido; PNG, WebP, TIFF abrem inteiros (largura × altura × 4 bytes) antes de reduzir, e quatro threads fazendo isso numa pasta de fotos grandes podiam comer a memória toda. Acima de 60 megapixels (400 no JPEG) não há miniatura.
- **Relatório de queda**: todo app Gravity que morrer por SIGSEGV/SIGBUS/SIGABRT/SIGFPE/SIGILL deixa `~/.cache/lumna/crash/<app>.log` com o sinal, a pilha e o mapa de memória; `addr2line -f -C -e /usr/bin/lumna-atlas <deslocamento>` dá a função e a linha (o pacote não tira os símbolos).
- **Recarga em loop em TODA pasta**: o `vigiar_pasta` tirava e punha o watch do inotify a cada recarga, e o kernel manda um `IN_IGNORED` quando o watch antigo sai. O Atlas lia isso como "a pasta mudou" e recarregava, de novo e de novo, uma vez a cada 0,3 s, para sempre: o "carregando" eterno, as prévias recalculadas (e piscando) e o CPU gasto à toa, tanto mais caro quanto maior a pasta. Agora o watch só troca quando a pasta é outra, `IN_IGNORED` não conta como mudança, uma recarga que não mudou nada (mesmos nomes, tamanhos, datas e permissões) não mexe na tela, e se quatro avisos seguidos não mudam nada as releituras se espaçam (até 8 s) e o motivo vai para o log (`atlas: ... avisa que mudou mas nada mudou (evento ...)`); mudanças de verdade nunca são atrasadas.
- **Nada que demore roda na linha principal.** O compositor derruba o cliente que fica parado sem ler os eventos (com o mouse mexendo o buffer dele enche em poucos segundos: `Data too big for buffer` no log do Orbit; provado com o Atlas parado por SIGSTOP). Era o "travou e fechou" ao clicar em pastas de um disco grande: o painel de detalhes (aberto por padrão) somava o tamanho da pasta selecionada (até 20 mil `lstat`) e recontava os itens a cada quadro, a grade contava os itens de cada pasta à vista, e a listagem da própria pasta (um `lstat` por entrada) rodava no meio do desenho. Agora a contagem e o tamanho (que desiste se a seleção já mudou) vêm de threads e o painel mostra "…" / "calculando…"; a listagem roda numa thread: `carregar()` espera até 0,12 s (uma pasta normal chega antes e tudo se comporta como antes) e, se passar disso, o Atlas segue vivo mostrando "Abrindo…" e a pasta entra quando chegar. `ar_ordenar_com` ordena sem variável global, por isso pode rodar na thread.

Testes: `testes/atlas/teste_lento.py` (cada leitura de pasta demorando 9 s, com `~/.atlas_lento_ms` e `LUMNA_ATLAS_DEBUG`, e o mouse mexendo sem parar: o app tem de abrir a pasta, selecionar pastas e continuar vivo), `testes/atlas/teste_cheias.py` (dezenas de pastas com centenas de arquivos, fotos grandes e quebradas, links em laço, FIFO, 81 MP, duplo clique e Enter em cada uma) (nomes não UTF-8, pasta que muda, CPU parado e o relatório de queda) e, para pegar memória liberada ou ponteiro nulo, **`sh testes/atlas/asan.sh PASTA_DO_ORBIT [testes...]`** roda a bateria inteira com o ASAN e o UBSAN numa cópia.

## O Atlas como o gerenciador de arquivos do sistema (`lumna-portal`)

O `lumna-portal` (um serviço sem tela, sobe sozinho pelo D-Bus e sai depois de um minuto parado) responde por dois serviços do sistema:

- **`org.freedesktop.FileManager1`** (o "Mostrar na pasta" dos downloads do Firefox e do Chromium, do Steam e de outros): abre o Atlas na pasta e, num arquivo, na pasta dele **com o arquivo selecionado** (`atlas /caminho/do/arquivo` faz o mesmo). A sessão copia o arquivo de serviço para `~/.local/share/dbus-1/services`, que vale mais que o de um Thunar que sobrou no sistema.
- **`org.freedesktop.impl.portal.FileChooser`**: o seletor de arquivos de **qualquer app** (Firefox, Chromium, Steam, apps GTK e Qt): abrir, enviar (vários arquivos), escolher pasta, salvar (com nome e confirmação de substituir) e salvar vários. O `lumna-portals.conf` aponta o FileChooser para o backend `lumna`.

O seletor é o próprio Atlas num modo à parte: `atlas --escolher=abrir|abrir-varios|pasta|salvar [--titulo=T] [--pasta=P] [--nome=N] [--rotulo=R] [--filtro=NOME::g=*.png,m=image/png]...`. O cabeçalho diz "Atlas · Salvar arquivo" (ou Selecionar arquivo/arquivos/pasta) e, embaixo, "Solicitado por Firefox · Upload" (`--solicitante=` e `--titulo=`). Ao abrir: a grade com a prévia do arquivo marcado ("PNG · 3840 × 2160 · 4,2 MB") e o rodapé com o nome, o filtro do app e o botão com o rótulo do app ("Selecionar para upload"); ao salvar: a lista com colunas, "Nova pasta", o nome, o "Tipo", "Salvar em <pasta>" e o aviso. O que a pessoa escolhe sai em stdout, um caminho por linha, com código 0; cancelar (Esc, o botão ou fechar a janela) sai com 1. Duplo clique num arquivo o escolhe; Enter também.

Ainda é só o Atlas de sempre por dentro: a navegação, a busca, as abas e as nuvens funcionam no seletor. O **Abrir com…** do menu de contexto de um arquivo abre uma janelinha com os aplicativos do tipo (o padrão primeiro), "Mostrar todos os aplicativos" e "Sempre usar este aplicativo para este tipo de arquivo".

Para o Atlas ser o padrão das pastas também fora do LUMNA (um sistema com Thunar): `xdg-mime default lumna-atlas.desktop inode/directory`.
