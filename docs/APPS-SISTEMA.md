# Apps do sistema: o plano (10 de novembro de 2026)

Combinado com o Eric: tudo em Gravity (C), **um app por vez, completo e testado** (código, testes, pacote, documentação e roteiro no ar), e **o esboço de cada app vem dele antes de eu começar o app** (como foi com o Atlas). A Loja já tem o esboço `docs/visual/02-loja.jpg`.

Ordem pedida: **1. Galeria, 2. Player de música, 3. Player de vídeo, 4. Loja de apps, 5. Instalador de drivers, 6. Menu de pré-configuração** (o que aparece assim que o sistema acaba de ser instalado).

Cada app segue o molde do Atlas: serviço em `servicos/` (sem interface, com teste em C), app em `exemplos/`, teste com o app inteiro num Orbit headless (`testes/<app>/`), ASAN/TSAN antes de empurrar, pacote em `packages/lumna-gravity`, `.desktop` e ícone, docs e linha no PRD. Nada que demore roda na linha principal (o Orbit derruba o app que para de ler os eventos): leitura de disco, miniaturas e rede vão em threads.

## 1. Galeria (fotos e vídeos)

Esboço recebido (biblioteca, editor de fotos, editor de vídeos, configurações). Em fases: **1. biblioteca + visualizador + configurações (pronta, aguardando seu teste)**, **2. editor de fotos (pronto, aguardando seu teste)**, **3. editor de vídeos (pronto, aguardando seu teste)**. Tudo está em [GALERIA.md](GALERIA.md). O `vitrine.c` do Gravity (antes `galeria.c`) é a vitrine dos componentes, não este app.

## 2. Player de música

Esboço recebido (Ouvir agora com a fila, Álbuns, Letra sincronizada, Configurações). **Pronto, aguardando seu teste** (biblioteca local, fila, letras, saída de áudio, gapless, transição, MPRIS); o Qobuz fica para a segunda fase. Tudo em [MUSICA.md](MUSICA.md).

## 3. Player de vídeo

Virou a **fase 4 da Galeria** (combinado: a Galeria já edita vídeo e mostra as miniaturas, então também classifica e toca). O libmpv toca dentro da janela com os controles do LUMNA; o mpv numa janela separada continua como opção em Configurações > Reprodução. **Pronto, aguardando seu teste.** Tudo em [GALERIA.md](GALERIA.md#fase-4-o-player-de-vídeo).

## 4. Loja de apps

Esboço recebido (Explorar, Pacotes com busca e filtros, Drivers e kernels), e a Loja ficou **expandida**: apps dos repositórios do Arch e do **AUR (paru/yay)**, mais drivers e kernels com checagem de compatibilidade. Em duas fases: **1. apps (pronta, aguardando seu teste)** e **2. Drivers e kernels (não começada)**. Tudo em [LOJA.md](LOJA.md). Decidido com você: o AUR usa o paru ou o yay que estiver instalado (escolhido em Configurações); as avaliações da comunidade LUMNA ficam com a interface pronta e sem dados até existir um servidor.

## 5. Instalador de drivers

**Virou a fase 2 da Loja** (a página Drivers e kernels do esboço). O texto abaixo é o plano antigo.

Detecta a placa de vídeo (e o que mais precisar de driver) e instala o pacote certo (NVIDIA, AMD, Intel) pelo pacman, com a senha pelo polkit do LUMNA. Sem esboço ainda. Em aberto: onde ele mora (app próprio, uma página do Painel, ou os dois) e se roda sozinho no pré-configuração.

## 6. Menu de pré-configuração (pós-instalação)

O que aparece assim que o sistema acaba de ser instalado, na primeira entrada. Sem esboço ainda. O instalador do sistema (`exemplos/instalador.c`) já cuida do disco e da conta; este vem depois, já dentro da sessão do usuário. Candidatos de etapas (a confirmar com o esboço): idioma e teclado, rede, tema, drivers, Emma (provedor), apps essenciais pela Loja, Connect com o celular.
