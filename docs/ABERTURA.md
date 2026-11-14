# Animação de abertura (depois do login)

A animação que toca entre a senha e o desktop, feita pelo Eric: o logo LUMNA acende, entra em curto-circuito e estabiliza, com o sussurro da Emma; fica em uma "respiração" lenta
até o desktop terminar de abrir; então se distorce e escurece, e o desktop aparece. Quem toca é o `lumna-abertura` (Gravity), com o player próprio do LUMNA (`servicos/video_player.c`,
libmpv por software, o mesmo da Galeria).

## Como funciona

- **Os vídeos do Eric** ficam em `iso/branding/abertura/` (`start.mp4`, `mid-loop.mp4`, `end.mp4`). `sh iso/branding/abertura/gera.sh [fator]` monta **um arquivo só**,
  `abertura.mp4`, com os trechos `[start][F][R][end]`: F é o `mid-loop` esticado (fator 2 por omissão) e R é o mesmo ao contrário; o `abertura.ini` guarda o tempo de cada trecho.
  O `mid-loop` original tem 24 quadros, mas os 2 últimos já são o começo da distorção do `end`; o laço usa só os 22 limpos.
- **A linha do tempo** (`servicos/abertura_tempo.c`, testada em `testes/abertura/testa_tempo.c`) decide, para cada instante, em que ponto do arquivo estar: o start inteiro, o laço F-R-F-R
  até o desktop ficar pronto, e a saída **sem salto**: se está em F, termina F; se está em R, pula para o mesmo quadro de F e termina F; depois o end. Pular (tecla/clique depois de 1 s) vai direto ao end.
- **O desktop está pronto** quando a barra, a dock e o papel de parede estão na tela (`orbit-msg camadas`); passados 20 s sai assim mesmo.
- **Todas as telas**: cada tela é um processo (o da principal abre os outros, como o papel de parede). Todos seguem o mesmo relógio (`$XDG_RUNTIME_DIR/lumna-abertura.ini`), então ficam juntos. Só a principal toca o som.
- **Ajustes** (Painel > Aparência > Animações, em `animacoes.ini [geral]`): `abertura = sim|nao` e `abertura_som = sim|nao`. O nível "Desligada" desliga a abertura; "Simples" a mantém.
- A sessão (`session/lumna-orbit-session`) a chama **primeiro**, antes do papel, da barra e da dock; o começo é preto, o que esconde a troca do greeter para a sessão.

## Testes

- `testes/abertura/testa_tempo.c` (a linha do tempo, com controle negativo), `testes/abertura.sh <orbit>` (o app de verdade num Orbit sem tela: um giro completo, o pulo, pronto em R e em F, desligado),
  `testes/abertura/testa_seek.c` (mede as paradas de imagem nos saltos com o libmpv: nenhuma passou de 70 ms no PC de teste) e `testes/painel_animacoes.sh` (os interruptores).
- **O que não foi provado:** a fluidez em PC fraco, o áudio nos saltos (a saída de áudio nula não revela estalos) e o som real. O laço começa e termina em silêncio no arquivo, o que reduz o risco de estalo.

Ideias: vídeo em resolução menor para máquina fraca; o mesmo arquivo para o desligamento.
