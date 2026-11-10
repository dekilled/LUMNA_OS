# Galeria (`lumna-galeria`), fotos e vídeos

A galeria do LUMNA OS, em Gravity (C/Clay), feita sobre o seu esboço (biblioteca, editor de fotos, editor de vídeos, configurações). Entrega em fases:

| Fase | O que | Estado |
|---|---|---|
| 1 | Biblioteca, visualizador com zoom, álbuns, favoritos, lixeira, importar, configurações | **pronta** (testada num Orbit sem tela; falta o seu teste no PC de verdade) |
| 2 | Editor de fotos (Ajustar, Recortar, Girar, Filtros; salva sempre uma cópia) | falta |
| 3 | Editor de vídeos (recorte, áudio, girar, exportar) | falta |

## Fase 1: o que tem

- **Biblioteca**: lê `~/Imagens`, `~/Vídeos` e as pastas que você pôr em Configurações > Biblioteca (até 8, com "Incluir subpastas" e "Buscar novas mídias automaticamente"). A pasta é vigiada (inotify): o que chega por fora aparece sozinho. Nada é lido na linha principal (disco lento não trava o app).
- **Barra lateral**: Todas as mídias, Recentes, Favoritos, Fotos, Vídeos, Lixeira (pode esconder nas configurações) e os **Álbuns**.
- **Grade por data** (Hoje, Ontem, o mês...) ou lista; ordenar por data da captura (EXIF; sem EXIF vale a data do arquivo), data de modificação, nome ou tamanho; agrupar por dia, mês ou nada; só as miniaturas visíveis são carregadas (milhares de fotos não pesam). Vídeos mostram a duração.
- **Busca** pelo nome, sem precisar do acento.
- **Seleção**: clique no quadradinho da miniatura, Ctrl+clique, Shift+clique (faixa), Ctrl+A, Esc limpa; com o dedo, segurar abre o menu. A barra de seleção tem Favoritar, Adicionar ao álbum, Compartilhar, Mover para a lixeira (ou Restaurar / Apagar para sempre, na Lixeira).
- **Favoritos** e **álbuns** (criar, renomear, apagar, adicionar, tirar do álbum). Apagar um álbum **não apaga as fotos**. Guardados em `~/.config/lumna/galeria/`.
- **Visualizador** (duplo clique ou Enter): zoom com a roda (em volta do ponteiro), `+`/`-` e duplo clique (alterna entre ajustada e 2,5×), `0` volta ao tamanho da tela; arrastar move a foto ampliada; as setas do teclado e os botões dos lados passam para a próxima; `I` mostra os detalhes (data, dimensões, tamanho, local); `F` favorita; Delete manda para a lixeira; Esc volta. **Vídeo** abre no player do LUMNA (mpv, com o volume e o repetir das configurações) ou no aplicativo padrão do sistema.
- **Lixeira** do freedesktop, a mesma do Atlas: Delete manda para lá com **Desfazer**; Restaurar devolve ao lugar de origem; Apagar para sempre e Esvaziar pedem confirmação (dá para desligar). Nada é sobrescrito sem perguntar (Pular / Manter os dois / Substituir).
- **Importar**: arrastar arquivos ou pastas para a janela, ou o botão Importar (procura numa pasta). Copia ou move (escolha em Configurações > Biblioteca > Ao importar) para a primeira pasta da biblioteca, em "Importadas".
- **Compartilhar**: manda a mídia para um celular conectado pelo LUMNA Connect (em segundo plano). Menu: Abrir, Mostrar no Atlas, Copiar o caminho, Detalhes.
- **Configurações** (as 5 páginas do esboço): Biblioteca (pastas, importar, duplicados), Visualização (modo, tamanho, nomes nas miniaturas, cache em disco e "Limpar cache"), Edição (qualidade e formato da cópia; **só vale quando o editor da fase 2 chegar**), Reprodução (player, volume, repetir) e Privacidade (confirmar antes de apagar, mostrar a Lixeira). Aplicar grava em `~/.config/lumna/galeria/config.ini`; Cancelar descarta.

> As páginas Visualização, Edição, Reprodução e Privacidade foram um desenho meu (o seu esboço trazia só os títulos). Diga se quiser outras opções.

## O que ainda não faz

Editor de fotos e de vídeos (fases 2 e 3), zoom por pinça e deslizar para trocar de foto no toque (hoje: duplo toque e os botões dos lados), reconhecimento de rostos/lugares, mapa, vídeo tocando **dentro** da janela (hoje abre o mpv), RAW, HEIC.

## Arquivos

- `gravity/servicos/galeria.[ch]`: o serviço (varredura, EXIF, filtros, grupos, favoritos, álbuns, lixeira, miniaturas, cache), sem interface.
- `gravity/exemplos/galeria/`: `principal.c` (janela, barra lateral, seleção, operações, depuração para teste), `grade.c`, `visualizador.c`, `ajustes.c`, `app.h`.
- `gravity/exemplos/vitrine.c`: a vitrine dos componentes do Gravity (antes chamada `galeria.c`; `make vitrine`).
- Testes: `sh gravity/testes/galeria.sh [PASTA_DO_ORBIT]` (serviço em C numa biblioteca de mentira e, se der a pasta do build do Orbit, o app inteiro num Orbit headless: `testes/galeria/teste_ui.sh`).

## Como foi testado (e o que não foi)

Testado aqui: o serviço (EXIF, ordem, filtros, grupos, favoritos, álbuns, lixeira, miniaturas, arquivo quebrado, foto enorme), com ASAN, UBSAN e TSAN; o app inteiro num Orbit sem tela (clicar, selecionar, favoritar, álbuns, apagar e restaurar, visualizador com zoom, configurações, importar), com ASAN e UBSAN sem relatório (o TSAN só acusa falsos positivos de dentro do GLib).
**Não testado**: o seu hardware (disco NTFS lento, tela de verdade, toque), vídeo tocando no mpv e compartilhar com um celular de verdade (no teste, o mpv e o Connect são de mentira). Roteiro no `docs/TESTES-ISO.md`, seção 2d.
