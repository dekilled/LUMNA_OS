# Apps do sistema: o plano (10 de novembro de 2026)

Combinado com o Eric: tudo em Gravity (C), **um app por vez, completo e testado** (código, testes, pacote, documentação e roteiro no ar), e **o esboço de cada app vem dele antes de eu começar o app** (como foi com o Atlas). A Loja já tem o esboço `docs/visual/02-loja.jpg`.

Ordem pedida: **1. Galeria, 2. Player de música, 3. Player de vídeo, 4. Loja de apps, 5. Instalador de drivers, 6. Menu de pré-configuração** (o que aparece assim que o sistema acaba de ser instalado).

Cada app segue o molde do Atlas: serviço em `servicos/` (sem interface, com teste em C), app em `exemplos/`, teste com o app inteiro num Orbit headless (`testes/<app>/`), ASAN/TSAN antes de empurrar, pacote em `packages/lumna-gravity`, `.desktop` e ícone, docs e linha no PRD. Nada que demore roda na linha principal (o Orbit derruba o app que para de ler os eventos): leitura de disco, miniaturas e rede vão em threads.

## 1. Galeria (fotos e vídeos)

Esboço recebido (biblioteca, editor de fotos, editor de vídeos, configurações). Em fases: **1. biblioteca + visualizador + configurações (pronta, aguardando seu teste)**, 2. editor de fotos, 3. editor de vídeos. Tudo está em [GALERIA.md](GALERIA.md). O `vitrine.c` do Gravity (antes `galeria.c`) é a vitrine dos componentes, não este app.

## 2. Player de música

Decidido: **biblioteca local, fila, letras e saída de áudio primeiro; o Qobuz entra numa segunda fase** (como no antigo).
- Referência: `shell/src/components/music/MusicApp.vue` (fila, letras, Qobuz, saídas).
- Motor: o mpv (o Eric confirmou que abre; ele exigia um arquivo para abrir), controlado pelo socket JSON dele, e MPRIS para a tela de bloqueio, o controle do celular e a Mídia.
- Escolher a saída usa o que o Painel > Som já faz (PipeWire/Pulse).

## 3. Player de vídeo

O mpv por baixo, com os controles do LUMNA por cima. O bloqueio antigo ("o mpv não abre na minha máquina") era o mpv sem arquivo: resolvido pelo Eric. Em aberto para o esboço: o mpv numa janela própria com os controles numa camada, ou o vídeo dentro da nossa janela (libmpv com o render API).

## 4. Loja de apps

Esboço: `docs/visual/02-loja.jpg` (Descobrir, Instalados, Atualizações; categorias Todos, Internet, Trabalho, Multimídia, Jogos, Desenvolvimento; "Essenciais para começar"; busca). Base: o serviço de pacotes do Painel (`servicos/pacotes.c`, pacman, `checkupdates`). Em aberto: só pacman/`extra`, ou também Flatpak e o AUR (o VSCodium do esboço é AUR ou Flatpak)?

## 5. Instalador de drivers

Detecta a placa de vídeo (e o que mais precisar de driver) e instala o pacote certo (NVIDIA, AMD, Intel) pelo pacman, com a senha pelo polkit do LUMNA. Sem esboço ainda. Em aberto: onde ele mora (app próprio, uma página do Painel, ou os dois) e se roda sozinho no pré-configuração.

## 6. Menu de pré-configuração (pós-instalação)

O que aparece assim que o sistema acaba de ser instalado, na primeira entrada. Sem esboço ainda. O instalador do sistema (`exemplos/instalador.c`) já cuida do disco e da conta; este vem depois, já dentro da sessão do usuário. Candidatos de etapas (a confirmar com o esboço): idioma e teclado, rede, tema, drivers, Emma (provedor), apps essenciais pela Loja, Connect com o celular.
