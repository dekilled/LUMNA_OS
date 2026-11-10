# Galeria (`lumna-galeria`), fotos e vídeos

A galeria do LUMNA OS, em Gravity (C/Clay), feita sobre o seu esboço (biblioteca, editor de fotos, editor de vídeos, configurações). Entrega em fases:

| Fase | O que | Estado |
|---|---|---|
| 1 | Biblioteca, visualizador com zoom, álbuns, favoritos, lixeira, importar, configurações | **pronta** (testada num Orbit sem tela; falta o seu teste no PC de verdade) |
| 2 | Editor de fotos (Ajustar, Recortar, Girar, Filtros; salva sempre uma cópia) | **pronta** (testada num Orbit sem tela; falta o seu teste no PC de verdade) |
| 3 | Editor de vídeos (recorte, áudio, girar, exportar) | **pronta** (testada num Orbit sem tela; falta o seu teste no PC de verdade) |
| 4 | Player de vídeo dentro da janela (libmpv): tocar, buscar, volume, velocidade, legendas, áudio, tela cheia, MPRIS | **pronta** (testada num Orbit sem tela, com som nulo; falta o seu teste no PC de verdade) |

## Fase 1: o que tem

- **Biblioteca**: lê `~/Imagens`, `~/Vídeos` e as pastas que você pôr em Configurações > Biblioteca (até 8, com "Incluir subpastas" e "Buscar novas mídias automaticamente"). A pasta é vigiada (inotify): o que chega por fora aparece sozinho. Nada é lido na linha principal (disco lento não trava o app).
- **Barra lateral**: Todas as mídias, Recentes, Favoritos, Fotos, Vídeos, Lixeira (pode esconder nas configurações) e os **Álbuns**.
- **Grade por data** (Hoje, Ontem, o mês...) ou lista; ordenar por data da captura (EXIF; sem EXIF vale a data do arquivo), data de modificação, nome ou tamanho; agrupar por dia, mês ou nada; só as miniaturas visíveis são carregadas (milhares de fotos não pesam). Vídeos mostram a duração.
- **Busca** pelo nome, sem precisar do acento.
- **Seleção**: clique no quadradinho da miniatura, Ctrl+clique, Shift+clique (faixa), Ctrl+A, Esc limpa; com o dedo, segurar abre o menu. A barra de seleção tem Favoritar, Adicionar ao álbum, Compartilhar, Mover para a lixeira (ou Restaurar / Apagar para sempre, na Lixeira).
- **Favoritos** e **álbuns** (criar, renomear, apagar, adicionar, tirar do álbum). Apagar um álbum **não apaga as fotos**. Guardados em `~/.config/lumna/galeria/`.
- **Visualizador** (duplo clique ou Enter): zoom com a roda (em volta do ponteiro), `+`/`-` e duplo clique (alterna entre ajustada e 2,5×), `0` volta ao tamanho da tela; arrastar move a foto ampliada; as setas do teclado e os botões dos lados passam para a próxima; `I` mostra os detalhes (data, dimensões, tamanho, local); `F` favorita; Delete manda para a lixeira; Esc volta. **Vídeo** abre no player da própria Galeria (fase 4); em Configurações > Reprodução dá para trocar por o mpv numa janela separada ou o aplicativo padrão do sistema.
- **Lixeira** do freedesktop, a mesma do Atlas: Delete manda para lá com **Desfazer**; Restaurar devolve ao lugar de origem; Apagar para sempre e Esvaziar pedem confirmação (dá para desligar). Nada é sobrescrito sem perguntar (Pular / Manter os dois / Substituir).
- **Importar**: arrastar arquivos ou pastas para a janela, ou o botão Importar (procura numa pasta). Copia ou move (escolha em Configurações > Biblioteca > Ao importar) para a primeira pasta da biblioteca, em "Importadas".
- **Compartilhar**: manda a mídia para um celular conectado pelo LUMNA Connect (em segundo plano). Menu: Abrir, Mostrar no Atlas, Copiar o caminho, Detalhes.
- **Configurações** (as 5 páginas do esboço): Biblioteca (pastas, importar, duplicados), Visualização (modo, tamanho, nomes nas miniaturas, cache em disco e "Limpar cache"), Edição (qualidade e formato da cópia salva pelo editor), Reprodução (player, volume, repetir) e Privacidade (confirmar antes de apagar, mostrar a Lixeira). Aplicar grava em `~/.config/lumna/galeria/config.ini`; Cancelar descarta.

> As páginas Visualização, Edição, Reprodução e Privacidade foram um desenho meu (o seu esboço trazia só os títulos). Diga se quiser outras opções.

## Fase 2: o editor de fotos

Abre pelo botão do lápis do visualizador, pela tecla `E` ou por "Editar" no menu da foto. A tela é a do esboço: em cima Voltar à galeria, o nome, Desfazer/Refazer, Cancelar e **Salvar cópia**; a prévia à esquerda com **Comparar original** (segure) e o zoom; as abas **Ajustar, Recortar, Girar, Filtros** embaixo; o painel à direita.

- **Ajustar**: Luz (Exposição de −3 a +3 passos de luz, Contraste, Realces, Sombras) e Cor (Temperatura, Saturação), de −100 a +100. Tocar no número zera aquele ajuste. Restaurar ajustes zera luz e cor.
- **Recortar**: arraste os cantos e as bordas na foto, ou por dentro para mover; a grade dos terços aparece. Proporções: Livre, Original, 1:1, 4:3, 3:2, 16:9, 3:4, 2:3, 9:16 e **Inverter** (deitada/em pé); com uma proporção escolhida ela se mantém ao arrastar. Mostra o tamanho final em pixels.
- **Girar**: à esquerda, à direita (quartos de volta), espelhar na horizontal e na vertical. Girar ou espelhar **desfaz o recorte** (ele valia na foto de antes).
- **Filtros**: Original, P&B, Sépia, Vívido, Frio, Quente, Desbotado, Dramático e Vintage, cada um numa miniatura da **sua** foto já com os ajustes; a Intensidade mistura o filtro com a foto.
- **Desfazer/Refazer** (botões, Ctrl+Z, Ctrl+Shift+Z ou Ctrl+Y): até 100 passos; um ajuste novo depois de desfazer descarta o que se podia refazer. Um deslizante entra no histórico ao soltar.
- **Zoom** da prévia: botões −/+, a lista (Ajustar à tela, 2×, 4×, 8×) e a roda em volta do ponteiro; arrastar anda quando passa da moldura. (É o zoom da **prévia**; a cópia sai sempre na resolução inteira.)
- **Salvar cópia** (Ctrl+S): grava **ao lado do original** como `foto-editada.jpg` (`-editada-2`... se já existir), no formato de Configurações > Edição (o mesmo do original, JPEG ou PNG) e com a qualidade do JPEG de lá. WebP, GIF e BMP viram JPEG (ou PNG se tiverem transparência). A cópia leva a **data do original**, para ficar ao lado dele na galeria. **O original nunca é alterado.** Sai pela foto inteira (não pela prévia), numa thread.
- Cancelar, Voltar e Esc, com alterações, perguntam "Descartar as alterações?".
- A prévia é refeita numa thread (a mais recente vence); enquanto se arrasta um deslizante ela usa uma foto menor, e ao soltar volta à nítida.

Escolhas minhas (o esboço mostrava só a aba Ajustar): os painéis de Recortar, Girar e Filtros, a lista de filtros, o "Inverter" e o "Restaurar giro/recorte". Diga se quiser outros.

**Não faz** (ainda): girar em ângulo livre (endireitar o horizonte), o EXIF não vai para a cópia (a data sim, pelo arquivo), curvas, nitidez, vinheta, texto/desenho, editar vídeo (fase 3).

Arquivos: `gravity/servicos/edicao.[ch]` (a conta: luz, cor, filtros, geometria, recorte, abrir com a orientação do EXIF e salvar a cópia; sem interface) e `gravity/exemplos/galeria/editor.c` (a tela, o desfazer e as threads).

## Fase 3: o editor de vídeos

Abre com **uma mídia de vídeo selecionada + a tecla `E`**, ou por "Editar" no menu (botão direito) do vídeo. Tela do esboço: Voltar à galeria, o nome, Cancelar e **Exportar cópia** em cima; a prévia com Play e o tempo (`00:14 / 00:42`); a **faixa do tempo** com miniaturas do vídeo, régua, as duas alças do recorte e o ponto da prévia; o painel **Editar vídeo** à direita.

- **Recorte**: arraste as alças da faixa (início e fim), ou use os campos **Início** e **Fim** (▲▼ de 1 em 1 s); **Duração final** mostra o que sobra. O trecho nunca fica menor que 0,5 s. Clicar ou arrastar na faixa leva o ponto da prévia. Teclas: `I` e `O` marcam o início e o fim no ponto de agora; ←/→ andam 1 s; Espaço toca/pausa.
- **Prévia tocando**: Play toca o trecho **na própria janela** (quadros reduzidos, 15 por segundo, em tempo real, **sem som**); para sozinho no fim do trecho.
- **Áudio**: **Silenciar áudio** e **Volume** (0 a 200%; o 100% gruda). Valem na cópia (a prévia é muda).
- **Girar vídeo**: um quarto de volta à direita por clique (a prévia e a faixa giram).
- **Exportar cópia**: grava **ao lado do original** como `video-editado.mp4` (`-editado-2`...), H.264 + AAC (CRF 20), só do trecho escolhido, com o áudio e o giro pedidos, e a **data do original**. O botão mostra o progresso (`Exportando 43%`); **Cancelar exportação** aborta e não deixa arquivo. **O original nunca é alterado.**
- Cancelar, Voltar e Esc, com alterações, perguntam "Descartar as alterações?".

Escolhas minhas (o esboço não mostrava): o atalho `E`, as teclas, os limites do volume, o MP4 como saída. Sai sempre H.264/AAC em MP4, mesmo que o original seja outro formato (recodifica: leva o tempo do trecho).

**Não faz** (ainda): som na prévia (para ouvir com som, abra o vídeo no player da Galeria), cortar o meio do vídeo/juntar trechos, velocidade, filtros de imagem, legendas, escolher a qualidade da saída.

Arquivos: `gravity/servicos/edicao_video.[ch]` (ffprobe/ffmpeg: sondar, quadro, tocar, exportar; sem interface) e `gravity/exemplos/galeria/editor_video.c` (a tela, a faixa e as threads).

## Fase 4: o player de vídeo

Tocar num vídeo (ou Enter) abre o player **dentro da janela da Galeria**: o libmpv desenha o quadro (renderização por software, vira uma imagem do cairo) e os controles são do LUMNA, por cima.

- **Controles**: tocar/pausar (botão do meio, Espaço ou um toque no vídeo), a barra de tempo (clicar ou arrastar pula), anterior/próximo vídeo da lista que está na tela (`P`/`N`), volume e mudo (`M`, setas ↑↓), velocidade (0,5× a 2×), legendas e faixa de áudio (as legendas `.srt` com o mesmo nome do vídeo entram sozinhas), repetir, tela cheia (`F` ou duplo toque; Esc sai) e as setas ←/→ (5 s; com Shift, 30 s).
- Os controles e a barra do alto somem depois de uns segundos sem mexer (com o vídeo tocando) e voltam ao mexer o mouse.
- **Editar** (também `E`) abre o editor de vídeos; a estrela favorita; o menu "…": abrir no mpv (janela separada), mostrar no Atlas e mandar para a lixeira.
- Um arquivo que o mpv não toca mostra o erro com o botão "Tentar no mpv".
- No fim o vídeo para; tocar de novo recomeça do começo. O volume e o repetir ficam salvos nas Configurações.
- **MPRIS** (`org.mpris.MediaPlayer2.lumna_galeria`): a tela de bloqueio, o controle do celular e as teclas de mídia veem o vídeo e o controlam (tocar, pausar, pular, buscar, volume).
- Configurações > Reprodução > "Abrir os vídeos com": **O player da Galeria** (padrão), **O mpv, numa janela separada** ou **O aplicativo padrão do sistema**. O ajuste antigo (mpv ou padrão) migra sozinho.

Arquivos: `gravity/servicos/video_player.[ch]` (o libmpv, sem interface) e `gravity/exemplos/galeria/player.c` (a tela). Depende do pacote `mpv` (que traz o libmpv).

**Não faz** (ainda): aceleração por GPU (o quadro é por software: vídeos 4K podem engasgar; no PC antigo ou muito pesado, use "mpv, numa janela separada"), capítulos, miniatura ao passar na barra, abrir arquivo ou endereço de fora da biblioteca, lista de reprodução própria (a "lista" é a vista que está na tela).

## O que ainda não faz

zoom por pinça e deslizar para trocar de foto no toque (hoje: duplo toque e os botões dos lados), reconhecimento de rostos/lugares, mapa, RAW, HEIC.

## Arquivos

- `gravity/servicos/galeria.[ch]`: o serviço (varredura, EXIF, filtros, grupos, favoritos, álbuns, lixeira, miniaturas, cache), sem interface.
- `gravity/exemplos/galeria/`: `principal.c` (janela, barra lateral, seleção, operações, depuração para teste), `grade.c`, `visualizador.c`, `ajustes.c`, `app.h`.
- `gravity/exemplos/vitrine.c`: a vitrine dos componentes do Gravity (antes chamada `galeria.c`; `make vitrine`).
- Testes: `sh gravity/testes/galeria.sh [PASTA_DO_ORBIT]`: o serviço da galeria e os dos editores e do player (`testa_edicao.c`, `testa_edicao_video.c`, `testa_video_player.c`) em C e, se der a pasta do build do Orbit, o app inteiro num Orbit headless (`testes/galeria/teste_ui.sh` para a biblioteca; `TESTE=teste_editor.py ...` para o editor de fotos e `TESTE=teste_editor_video.py ...` para o de vídeos e `TESTE=teste_player.py ...` para o player).

## Como foi testado (e o que não foi)

Testado aqui: o serviço (EXIF, ordem, filtros, grupos, favoritos, álbuns, lixeira, miniaturas, arquivo quebrado, foto enorme), com ASAN, UBSAN e TSAN; o app inteiro num Orbit sem tela (clicar, selecionar, favoritar, álbuns, apagar e restaurar, visualizador com zoom, configurações, importar), com ASAN e UBSAN sem relatório (o TSAN só acusa falsos positivos de dentro do GLib).
Editor: a conta de cada ajuste (valores conhecidos), giros e espelhos em 300 sequências ao acaso contra a conta direta, o recorte (20 mil arrastos ao acaso: sempre dentro da foto, 16 px no mínimo, proporção mantida), EXIF, salvar a cópia (original intacto, nome, formato, qualidade, data, transparência, arquivo quebrado) e a interface inteira (arrastar deslizantes, desfazer, comparar, zoom, recortar arrastando as alças, girar, filtros, salvar, descartar), com ASAN/UBSAN sem relatório. Uma prévia de 1600×1200 com tudo ligado leva ~50 ms (fora do quadro).
Editor de vídeos: com vídeos de verdade feitos pelo ffmpeg, o serviço (sondar, quadros, tocar em tempo real, exportar recorte/áudio mudo/volume medido em dB/giro, progresso crescente, cancelar sem deixar arquivo, arquivo ruim) e a interface inteira (alças da faixa, campos, clicar na faixa, Play/Pausa/Espaço, áudio, girar, exportar e conferir a cópia com o ffprobe, descartar), com ASAN/UBSAN sem relatório.
**Player de vídeo**: o serviço (libmpv de verdade, saída de áudio nula: abrir, tocar, pausar, buscar, volume, velocidade, faixas, fim, arquivo quebrado, recomeçar) e a interface inteira (tocar de verdade: o quadro tem cor e o tempo anda; Espaço, setas, barra, volume, velocidade, legendas, repetir, tela cheia, MPRIS por D-Bus, anterior/próximo, Editar, erro, lixeira, arquivo apagado de fora), com ASAN/UBSAN sem relatório e controle negativo (sem o pausar, o teste falha).
**Não testado**: o seu hardware (disco NTFS lento, tela de verdade, toque), áudio de verdade saindo, vídeos grandes (4K) por software, tela cheia no compositor real e compartilhar com um celular de verdade (no teste, o mpv e o Connect são de mentira). Roteiro no `docs/TESTES-ISO.md`, seção 2d.
