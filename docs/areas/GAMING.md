# Área: Gaming

Leia antes o [LEIAME.md](LEIAME.md) (regras de todas as áreas).

## Escopo

O modo Gaming inteiro: a biblioteca `lumna-jogos` (Início, Biblioteca, Detalhes, Configurações), a **Loja** do Gaming (Steam, GOG, Epic), o
**controle** (gamepad) no Gaming, a Steam/Proton/Heroic/Lutris como fontes, e as regras do Orbit para jogos (quem fica na frente, o que é um jogo).

## Onde fica

- App: `gravity/exemplos/jogos.c`, `gravity/exemplos/jogos_loja.h` (a Loja); serviço de jogos `gravity/servicos/jogos.[ch]`.
- Lojas: `gravity/servicos/loja_steam.[ch]`, `loja_gog.[ch]`, `loja_epic.[ch]`.
- Controle: `gravity/exemplos/gamepad.c` (serviço `lumna-gamepad`), `gravity/servicos/gamepad.[ch]`, `gamepadc.[ch]` (cliente do socket).
- Orbit: `orbit/jogo.c` (o que é jogo), `orbit/mode.c` (modos, ilha, quem fica na frente: `orbit_kiosk_pick`).
- Docs: `docs/JOGOS.md`, `docs/LOJA-JOGOS.md`, `docs/MODOS.md` (Gaming), `docs/XWAYLAND.md`.
- Testes: `gravity/testes/jogos.sh`, `gamepad.sh`, `gaming_frente.sh`, `loja_steam.sh`, `loja_gog.sh`, `loja_epic.sh`,
  `gravity/testes/jogos/teste_loja.sh PASTA_DO_ORBIT PASTA_DO_GRAVITY` (a Loja inteira com servidor da Steam de mentira; caminhos absolutos).

## Estado

- Funciona no PC (Eric, 2026-11-17): abre os jogos e eles rodam; a Loja mostra a maioria das capas; a biblioteca mostra capas e jogos comprados.
- Feito em 2026-11-17, **falta o Eric confirmar**: capas da Steam pelo endereço que a API informa (jogos novos só têm a capa no caminho com hash;
  o antigo dá 404) e nova tentativa nas falhas passageiras de imagem (3, 8 e 20 s), também nas capas grandes da GOG. Commit `b65a9b8`.

## Pendências (na ordem combinada)

1. **O controle não faz nada no Gaming** ("o joystick não tem efeito nenhum em nenhum lugar"). O caminho controle → `lumna-gamepad` → `wtype` →
   Orbit → lumna-jogos passa no teste com controle de mentira, então o problema está no ambiente do PC. **Pedir ao Eric:** qual controle (Xbox,
   PS, 8BitDo; USB ou Bluetooth), se funciona na Mídia (lá funciona), `which wtype`, `pgrep -a gamepad`, e `evtest` mexendo o analógico com o
   lumna-jogos aberto. Ideia de solução: o lumna-jogos assinar o socket do `lumna-gamepad` (como a Mídia faz) em vez de depender do `wtype` e da
   detecção de foco a cada 400 ms.
2. **A janela da Steam "Abrindo <jogo>…" (e a de compilar shaders Vulkan) aparece por cima do Gaming**, e o LUMNA não mostra **nenhum estado**:
   se o jogo está abrindo, se falhou, se está compilando shaders. Proposta: um aviso/estado do lumna-jogos ("Abrindo X…", "Preparando shaders",
   "Não abriu") e a regra do Orbit para essas janelas da Steam (hoje: diálogo da Steam fica por trás até o jogo fechar; ver `docs/MODOS.md`).
3. **Os discos com jogos não montam no boot.** Decisão do Eric: **uma permissão no Painel que se mantém depois do boot**. O trabalho é da área
   **Hardware** (fstab/`lumna-discos`, com a página no Painel); aqui só conferir que a biblioteca acha os jogos depois.

## Para o Eric conferir

No registro de testes: `python3 docs/testes/teste.py lista Gaming` (pendentes) e `lista Gaming falhou`. Os itens recentes:

- [ ] Capas da Loja que não carregavam ("just volleyball", "Morrowind", "Upgrade para Estatuto Prime"): carregam depois do update?
- [ ] Os dados do controle (item 1 das pendências).

## Confirmado no PC

- Abrir e jogar (Steam/Proton); biblioteca com capas e jogos comprados; a maioria das capas da Loja (2026-11-17).

## Decisões já tomadas

- O serviço do controle **não captura** o controle (sem `EVIOCGRAB`): o jogo continua lendo o dele. Com um jogo em foco, o serviço não manda tecla.
- Lista de desejos da Steam é **local**; a chave de API da Steam é opcional (`~/.config/lumna/jogos/steam.ini`, 600).
- Papel de parede em vídeo no Gaming: depende de decisão do Eric (pede o mpv por baixo).

## Recebido de outras áreas

(vazio)
