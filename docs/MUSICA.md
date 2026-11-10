# Música (`lumna-music`), o player de música

O player do LUMNA OS, em Gravity (C/Clay), feito sobre o seu esboço (Ouvir agora com a fila, Álbuns, Letra sincronizada, Configurações). Biblioteca **local**; o Qobuz fica para a segunda fase.

## O que tem

- **Biblioteca**: lê as pastas de Configurações > Biblioteca (padrão `~/Músicas`; até 8; com subpastas) e as **tags** (título, artista, álbum, número da faixa e do disco, ano, gênero, duração) pelo `ffprobe`, em 4 threads. O resultado fica num cache (`~/.cache/lumna/musica/indice.tsv`, por caminho + data + tamanho): a segunda abertura não lê tudo de novo. Procura músicas novas sozinha a cada minuto e meio. Formatos: MP3, FLAC, Ogg, Opus, M4A/AAC, WAV, WMA, APE, AIFF, WavPack, MKA.
- **Barra lateral**: Ouvir agora, Minha biblioteca, Álbuns, Artistas, Pastas, as **Playlists** (com o `+`) e, embaixo, Adicionar músicas e Configurações.
- **Minha biblioteca**: a lista de todas as faixas (#, capa, título, artista, álbum, duração, "..."); só desenha o que está na tela (milhares de faixas não pesam). Clicar numa faixa toca a lista a partir dela.
- **Álbuns** (grade ou lista, ordenar por título, artista ou ano), **Artistas** (grade redonda) e **Pastas** abrem o **detalhe**: capa grande, nome, "6 faixas · 26 min", **Reproduzir**, **Aleatório**, "..." e a tabela de faixas.
- **Capas**: a imagem da pasta (`cover`, `folder`, `front`, `album`, `capa`; `.jpg` ou `.png`) ou a **embutida** na faixa (extraída com o `ffmpeg` para o cache, numa thread).
- **Ouvir agora**: a capa, a faixa e a **Letra**: sincronizada (`.lrc` ao lado do arquivo, com o mesmo nome, ou a tag em LRC) acende a linha da vez e rola junto ("Seguir reprodução"; tocar numa linha vai até ela); sem tempo aparece inteira ("Não sincronizada"); sem letra, diz como pôr. O fundo ganha o brilho da cor média da capa.
- **Barra do player**: a faixa e o coração (Favoritas), aleatório, anterior, tocar/pausar, próxima, repetir (nada, a fila, a faixa), o tempo (clicar ou arrastar busca), volume, **saída de áudio** ("Neste computador" ou outro dispositivo do mpv), o botão da letra e o da fila.
- **Fila de reprodução** (painel à direita): "Tocando agora" e "A seguir"; tocar numa linha pula para ela; **arrastar a alça** reordena; menu (botão direito) para tocar a seguir ou tirar; "..." para Salvar como playlist e Limpar. A fila é salva e **volta ao abrir** (a mesma faixa, parada, no ponto onde estava).
- **Menu de uma faixa** (o "..." ou botão direito): Tocar agora, Tocar a seguir, Adicionar à fila, Favoritar, **Adicionar a** cada playlist, Nova playlist com esta faixa, Ir para o álbum, Ir para o artista, Mostrar no Atlas e, numa playlist, Tirar desta playlist.
- **Playlists** são arquivos `.m3u8` em `~/.config/lumna/musica/playlists` (outros players também leem). **Favoritas** sempre existe. Criar, renomear, apagar (com confirmação; as músicas não somem) e **reordenar arrastando a alça**.
- **Busca** no alto: título, artista, álbum e pasta, sem acento e sem maiúsculas, várias palavras.
- **Sem intervalos** (gapless): a faixa seguinte entra na lista do mpv e emenda sem corte. **Transição entre faixas** (0 a 12 s): um segundo mpv começa a próxima antes do fim e as duas se misturam. **Normalizar volume** (ReplayGain e `dynaudnorm`).
- **MPRIS** (`org.mpris.MediaPlayer2.lumna_music`): a tela de bloqueio, o controle do celular, o modo Mídia e as teclas de mídia veem e controlam o que toca (tocar, pausar, próxima, anterior, buscar, volume, aleatório, repetir, título, artista, álbum e capa).
- **Configurações** (5 abas do esboço, valem na hora): **Reprodução** (dispositivo de saída, normalizar, sem intervalos, transição, restaurar a fila, Emma), **Biblioteca** (pastas, incluir subpastas, buscar automaticamente, procurar agora, cache de capas), **Letras** (mostrar, seguir ao abrir, tamanho), **Aparência** (capas, brilho da capa, tamanho das capas) e **Emma**.
- Arrastar músicas ou uma pasta para a janela: as músicas entram na fila; a pasta entra na biblioteca. Teclado: Espaço toca/pausa, ←/→ buscam 5 s, Ctrl+←/→ anterior/próxima, Ctrl+↑/↓ volume, Ctrl+F busca.

Escolhas minhas (o esboço não mostrava): o menu de cada faixa, o detalhe de artista e de pasta, a página Pastas, a aba Letras/Aparência/Emma por dentro, o painel de contas do botão de perfil, os atalhos. Diga se quiser outros.
- **App padrão para áudio e instância única**: o LUMNA Music pode ser o app padrão para música (Painel > Aplicativos padrão). Aberto com um arquivo, ele **toca na hora** (a fila salva de antes não volta por cima). Se o app já está aberto, abrir outro arquivo **não cria outra janela nem outro processo**: o pedido vai para o app que existe (MPRIS `OpenUri`), a fila passa a ser o arquivo, ele toca e a janela é trazida para a frente (`Raise`). Vários arquivos de uma seleção entram juntos na fila. Abrir o app de novo sem arquivo só traz a janela que já existe.
- **Nunca sobra mpv invisível**: o mpv é preso ao app (se o app cai ou é morto, o mpv vai junto) e, ao subir, o player derruba os mpv órfãos de um app que já não existe. Tocar a mesma música dezenas de vezes seguidas mantém **um mpv só**.

## O que não faz (ainda)

Qobuz e outros serviços (o botão de perfil só avisa), equalizador, mini-player, editar tags, buscar letra na internet, ReplayGain calculado (usa só a tag que houver), a **Emma controlar a música** (a opção "Emma neste app" só mostra o botão que abre a conversa), tocar numa **caixa de som de outro aparelho** (só os dispositivos de áudio do mpv). Itens de playlist que saíram da biblioteca somem da lista (a contagem diz quantos).

## Arquivos

- `gravity/servicos/musica.[ch]`: a biblioteca (ajustes, varredura e cache, tags, capas, letras, álbuns/artistas/pastas, busca, playlists, fila salva); sem interface.
- `gravity/servicos/musica_player.[ch]`: o motor (mpv pelo socket JSON; gapless e transição); sem interface.
- `gravity/servicos/musica_mpris.[ch]`: o servidor MPRIS (GDBus, numa thread).
- `gravity/exemplos/musica/`: `principal.c` (janela, motor da fila, capas, MPRIS, depuração), `barra.c` (barra lateral, do player, fila, menus, diálogos), `biblioteca.c` (listas e detalhe), `ouvir.c` (a letra), `ajustes.c`.
- Testes: `sh gravity/testes/musica.sh [PASTA_DO_ORBIT]`.

## Como foi testado (e o que não foi)

Testado aqui, com músicas de verdade geradas pelo ffmpeg (MP3 com capa e letra na tag, FLAC com disco 1 e 2, Opus, WAV sem tag, arquivo quebrado) e o mpv de verdade:
- **Biblioteca** (C): tags de cada formato, cache (a 2ª leitura é bem mais rápida), pasta repetida, álbuns (disco e faixa em ordem, "Vários artistas"), busca sem acento, capas (pasta e embutida), letras (`.lrc` com carimbos duplos, offset e marcas de palavra, tag sem tempo, tag com tempo), playlists (nomes ruins, repetidas, renomear, ordem), fila salva; ASAN/UBSAN sem relatório.
- **Motor** (C, mpv com saída nula): tocar, pausar, buscar, volume, fim da faixa, tocar pausado a partir de um ponto, **gapless**, escolher a próxima, **transição de 1 s e 2 s** (a troca acontece, o segundo mpv some, mexer no tempo cancela), trocar a saída, matar o mpv e recuperar, arquivo inexistente; ASAN/UBSAN sem relatório.
- **MPRIS** (C, com um D-Bus de sessão de verdade): todas as propriedades e métodos, o sinal de mudança só com o que mudou, a posição andando em tempo real, `SetPosition` só da faixa de agora; ASAN/UBSAN sem relatório.
- **O app inteiro** num Orbit sem tela, com o mpv e o D-Bus de verdade: biblioteca, vistas, detalhe, tocar, controles, seek, aleatório e repetir, **gapless e repetir a faixa/fila**, fila (arrastar), menus, favoritas, playlists (criar, adicionar, **arrastar para reordenar**, renomear, apagar), busca, **letra sincronizada andando**, configurações (gravam no `config.ini`), adicionar e remover pasta, MPRIS controlando o app, o mpv morrendo e o app se recuperando, e **fechar e abrir de novo voltando de onde parou**; ASAN/UBSAN sem relatório.

Abrir arquivo e instância única: o motor (30 toques seguidos na mesma música, alternar faixas depressa, e o mpv órfão derrubado) e o app inteiro num Orbit sem tela (aberto com um arquivo toca; outro arquivo vai para o app aberto; seis aberturas da mesma música dão um app e um mpv; dois arquivos juntos; abrir sem arquivo; matar o app à força não deixa mpv), com ASAN/UBSAN limpos e controles negativos (sem a instância única e sem o prctl, os testes falham).

**Não testado**: o gerenciador de arquivos de verdade abrindo o arquivo (aqui o pedido vem de um comando igual ao dele), a janela vindo para a frente no compositor de verdade, o seu hardware (**o som de verdade**: nos testes a saída é nula), seu disco e sua biblioteca grande (milhares de faixas, discos lentos), a troca de saída para um dispositivo **real** (fone, HDMI, caixa Bluetooth; aqui só se trocou entre as entradas `alsa` que o mpv lista, com a saída nula), a transição entre faixas **pela interface** (provada no motor, e a configuração no app), o toque (dedo parado abre o menu, arrastar a alça), o que a tela de bloqueio e o celular mostram do MPRIS. O TSAN **não serve** para este código: o GMutex do GLib é invisível para ele e acusa corridas que não existem; a revisão do travamento (um mutex de controle e outro dos campos) foi à mão. Roteiro no `docs/TESTES-ISO.md`, seção 2e.
