# Área: Mídia

Leia antes o [LEIAME.md](LEIAME.md).

## Escopo

O modo Mídia: o navegador **CEF** na versão Mídia (início com a fila de streamings, menu de serviços com Super+M, catálogo Filmes/Séries/Seus
vídeos, splash e animações), os streamings (Netflix, YouTube TV, Prime, HBO, Disney+...), o **controle** nos sites, e o seletor de serviços do Orbit.

## Onde fica

- `navegador/` (CEF, C++): `src/`, `ui/midia/` (HTML/JS/CSS da Mídia, `nav.js` = navegação por setas nos sites), `CMakeLists.txt`, `dev.sh`.
- Antiga: `gravity/exemplos/midia.c`, `gravity/servicos/midia*.c` (a Mídia antiga, ainda é o `seletor_midia` padrão no `orbit.ini`).
- Controle: o `lumna-gamepad` transmite botões e analógicos num socket; o navegador assina (ver `docs/NAVEGADOR.md`, "Controle (gamepad) na Mídia").
- Docs: `docs/NAVEGADOR.md` (todo o histórico, decisões e testes), esboços em `docs/navegador/`.
- Testes: `navegador/testes/midia.sh`, `controle.py` (gamepad real + controle falso + Orbit + navegador + páginas sonda), `catalogo.py`, `servicos.py`.

## Estado

- **Aprovada pelo Eric** (2026-11-15, zoom 0,72) e em uso: Netflix, YouTube, Prime, Paramount+, HBO Max tocam (Widevine baixado pelo próprio CEF).
- Confirmado pelo Eric (2026-11-17): login do Google pelo popup; controle nos sites "facilita muito"; YouTube abre a versão TV ("no geral está muito bom").

## Pendências

1. **Destaque (highlight) do controle se perde em partes de algumas páginas** (relato 2026-11-17): pedir ao Eric quais sites e telas. O `nav.js` é
   heurístico (o item clicável mais perto na direção); carrosséis virtualizados são o suspeito.
2. Trocar o `seletor_midia` do `orbit.ini` para `lumna-navegador --menu` quando a nova substituir a antiga na sessão (decisão do Eric).
3. **Teclado na tela** para a busca (o controle não digita).
4. **Arquivos locais** no catálogo ("Arquivo local" existe no modelo; falta adicionar vídeos e abrir num player).
5. Miniaturas em sites que bloqueiam o `og:image` (DRM) caem no print, que sai preto.
6. Mudança de estilo da Mídia (o Eric vai dizer o quê, com esboço) e Mídia vinculada a um monitor/projetor.
7. Navegador próprio em duas versões (completa para o Desktop, simples para a Mídia): ideia registrada, não começada.
8. Vídeos lentos para carregar (YouTube) **comparado com o BigLinux na mesma máquina**: é da área **Hardware** (rede); deixado de lado pelo Eric.

## Para o Eric conferir

No registro de testes: `python3 docs/testes/teste.py lista Mídia` (pendentes) e `lista Mídia falhou`. Os itens recentes:

- [ ] Em quais sites/telas o destaque do controle se perde (print ajuda).
- [ ] Netflix e Disney+ com o controle (o YouTube era o ruim antes do modo TV).

## Confirmado no PC

- Mídia aprovada (zoom 0,72, animação, splash); streamings tocam; login Google por popup; controle nos sites; YouTube TV (2026-11-15 a 17).

## Decisões já tomadas

- Zoom do menu 0,72. Fila e menu de serviços em páginas de 6. Popups (`window.open`) abrem em janela própria.
- User-Agent de TV no YouTube; LB+RB troca para o modo cursor; o site ideal é o que se identifica como TV.

## Recebido de outras áreas

(vazio)
