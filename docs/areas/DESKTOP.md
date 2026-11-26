# Área: Desktop e Orbit

Leia antes o [LEIAME.md](LEIAME.md). Área sugerida pela Claudia e aprovada pelo Eric (2026-11-17): o compositor e o dia a dia do Desktop.

## Escopo

O **Orbit** (o compositor: janelas livres, encaixe e multitarefa, foco, multi-monitor, XWayland, animações de janela, carrossel de modos,
espaços), a **dock**, a **barra** de cima, o **lançador**, a área de trabalho (ícones) e os apps do dia a dia: **Atlas** (arquivos), **Música**,
**Galeria** (e o player de vídeo), **Signals** (editor), **Captura**, **Calculadora**, **Processos** (Ctrl+Shift+Esc).

## Onde fica

- Orbit: `orbit/` (C, wlroots 0.18/0.20). Encaixe: `float.c`; arranjo: `arrange.c`; foco: `view.c`; X11: `xwl.c`; animação: `anim.c`, `winanim.c`;
  dock/foreign-toplevel: `foreign.c`; comandos: `ipc.c` + `orbit-msg`. Guia: `orbit/leiame.txt`.
- Testes do Orbit: `orbit/testes-encaixe/` (encaixe, divisas, dock, menu X11; ver o LEIAME de lá), `orbit/testes-foco/` (`foco.py`, `saida.py`),
  `orbit/testes-arrasto/` (roteiros antigos), `orbit/regressao.sh`.
- Apps: `gravity/exemplos/{dock,barra,lancador,atlas,captura,signals,processos}.c`, `gravity/exemplos/musica/`, `gravity/exemplos/galeria/`.
  Docs: `docs/DESKTOP.md`, `docs/ATLAS.md`, `docs/MUSICA.md`, `docs/GALERIA.md`, `docs/CAPTURA.md`, `docs/SIGNALS.md`, `docs/MODOS.md`.

## Estado

- **Encaixe novo** (2026-11-17, `8597b85`): só pelo cursor (faixa de 24 px); meio da lateral = metade, terço de cima/baixo = canto (grade de 4);
  janela encaixada esconde a dock e vai até a borda de baixo; arrastar a divisa redimensiona as janelas encaixadas juntas. O Eric testou:
  **funciona, com uma pequena inconsistência no resize** (ainda não descrita).
- **Menu de clique direito no outro monitor:** corrigido (`e73bed0`, a janela X11 não sabia a própria posição) e **confirmado pelo Eric**.
- **xdg-foreign v1 e v2** (2026-11-26, `orbit/protocolos.c`): o Orbit anuncia exporter/importer nas duas versões (registro único do wlroots). Testado só aqui,
  headless, com `orbit/testes-protocolos/teste_foreign.sh` (app + portal de mentira; controle negativo feito). **Não faz** ainda o diálogo ficar por cima da
  janela-pai: o Orbit não trata janela-filha (`set_parent`) em lugar nenhum; isso é decisão de produto (ver pendência 7).
- Foco (a janela usada mais recentemente recebe o foco) e o Orbit não abortar mais ao sair: corrigidos; foco confirmado no PC.

## Pendências

1. **A inconsistência no resize das janelas encaixadas:** pedir ao Eric o que acontece (qual divisa, com quantas janelas, o que esperava). Suspeitos:
   a faixa de pegar a divisa é só de 6 px dentro da janela (o espaço entre as janelas não pega); a prévia do encaixe aparece com a dock ainda à
   vista (menor) e a janela cresce ao soltar; app com tamanho mínimo próprio.
2. Cantos pelo teclado (Super+setas só faz as metades).
3. Menu de contexto do **Chromium/CEF** (Wayland, `xdg_popup`): não reproduzido; se errar, o Orbit grava `menu da janela APP: ...` no log.
4. Barra de título do Firefox completa no estilo do LUMNA; `app_id` do Spotify para a barra do Orbit.
5. Música: crash logo depois de tocar com HDD recém-montado (pedir o log); letra sem sincronia.
6. Itens antigos sem confirmação no PC (TESTES-ISO 2.469 a 2.483): menu de contexto de arquivos, animações de janela e do lançador, Atlas e Música
   estreitos, captura "última área".
7. **Janela-filha (xdg_toplevel `parent`, inclusive a importada pelo xdg-foreign):** hoje o diálogo não fica sobre a janela-pai nem acompanha ela. Perguntar ao Eric
   o comportamento que quer (sempre por cima, centralizar sobre a pai, modal) antes de mexer.

## Para o Eric conferir

No registro de testes: `python3 docs/testes/teste.py lista Desktop` (pendentes) e `lista Desktop falhou`. Os itens recentes:

- [ ] Descrever a inconsistência do resize (pendência 1).
- [ ] A faixa de 24 px do encaixe está boa de acertar?
- [ ] xdg-foreign: depois de atualizar o `lumna-orbit` e sair/entrar na sessão, `wayland-info | grep xdg_` mostra os 4 globais, e o app que precisava dele funciona (D-10).

## Confirmado no PC

- Foco (2026-11-16); clique direito no cursor em janela encaixada/arrastada (2026-11-17); encaixe lado a lado e em grade (2026-11-17, com a ressalva do resize).

## Decisões já tomadas

- Encaixe: o cursor manda, não a borda da janela; metade no meio da lateral, canto nos terços; a dock se esconde com janela encaixada.
- Carrossel de espaços sem miniaturas reais da tela.

## Recebido de outras áreas

(vazio)
