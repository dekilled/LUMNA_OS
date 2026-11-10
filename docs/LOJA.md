# Loja LUMNA (`lumna-loja`)

A loja de aplicativos do LUMNA OS, em Gravity (C/Clay), feita sobre o seu esboço (Explorar, Pacotes com busca e filtros, Drivers e kernels). Entrega em duas fases:

| Fase | O que | Estado |
|---|---|---|
| 1 | Explorar, Pacotes (busca com filtros), Instalados, Atualizações, Configurações; repositórios do Arch (core, extra, multilib) e AUR (paru/yay) | **pronta** (testada num Orbit sem tela, com pacman, curl e paru de mentira; falta o seu teste no PC de verdade) |
| 2 | Drivers e kernels, com detecção de hardware e checagem de compatibilidade | **não começada** (a página existe e diz isso) |

## Fase 1: o que tem

- **Explorar**: os aplicativos em destaque (uma tabela de apps conhecidos: Firefox, Blender, Krita, VLC, GIMP, OBS Studio, Steam, Code…) em cartões, com abas por categoria (Todos, Desenvolvimento, Gráficos, Internet, Multimídia, Jogos, Sistema) e ordenação (nome, tamanho, melhor avaliados). Tocar no cartão abre o detalhe.
- **Busca** (no alto, `Ctrl+K`): digitar em qualquer tela leva a **Pacotes**: "Resultados para “mesa”", com o nome exato primeiro. Procura no nome, na descrição ou nos dois.
- **Pacotes**: o painel de **Filtros** (Buscar por, Tipo: Aplicativo/Utilitário/Biblioteca/Driver/Kernel, Categoria, Repositório core/extra/multilib/AUR, Avaliação, "Incluir sem avaliações", Limpar) e a tabela (nome e descrição, tipo, repositório, avaliação, Instalar). 100 linhas por vez, "Mostrar mais". A coluna de avaliação some em janela estreita.
- **Detalhe**: versão, tipo, tamanho do download e instalado, licença, site; do AUR também o mantenedor e os votos, com um aviso de que é não oficial. Instalar, Atualizar ou Remover.
- **Instalar/Remover**: pergunta antes (dá para desligar em Configurações; **do AUR pergunta sempre**), pede a senha por uma janela do LUMNA (pkexec) e mostra o andamento com a saída do pacman, linha por linha, num painel no canto. Uma ação por vez. Ao terminar, o que está instalado é relido.
- **Instalados**: o que foi pedido pela pessoa (`pacman -Qe`), mais o que veio de fora dos repositórios (AUR, `pacman -Qm`); uma chave mostra também bibliotecas e dependências. Remover roda `pacman -Rns`.
- **Atualizações**: a lista com a versão de agora e a nova (`checkupdates`, e o AUR pelo paru/yay), com um selo na barra lateral e **Atualizar tudo** (`pacman -Syu`, depois o AUR).
- **Configurações**: buscar também no AUR, perguntar antes de instalar ou remover, e qual programa do AUR usar (Automático, paru ou yay). Gravam em `~/.config/lumna/loja/config.ini`.
- **AUR**: a busca consulta a RPC do aur.archlinux.org (pelo `curl`, depois de meio segundo sem digitar). Instalar usa o paru (ou o yay) com `--sudo pkexec`: compila como a pessoa e só usa a senha para instalar. Sem nenhum dos dois instalados, a Loja diz isso em vez de falhar calada.

Como o Arch não tem categoria nem tipo, eles são **deduzidos**: a tabela de apps conhecidos manda; senão o jeito do nome (`lib*`, `*-utils`, `linux*`, `nvidia*`, `*-driver`…) e palavras da descrição. Pode errar: a categoria "Sistema" é o balaio.

**As avaliações da comunidade LUMNA ainda não têm servidor**: toda a interface está lá (estrelas, filtro, ordenar por avaliação), mas hoje tudo mostra "Sem avaliações". Quando houver um servidor, só falta preencher `nota` e `avaliacoes` de cada pacote.

## Não faz (ainda)

Drivers e kernels (fase 2), Flatpak, ícones dos apps fora do tema do sistema (usa o ícone do tema; sem ele, o desenho genérico), capturas de tela dos apps, avaliações, instalar um `.pkg.tar` solto, fila de instalações (é uma de cada vez), o botão Cancelar durante uma instalação.

## Arquivos

- `gravity/servicos/loja.[ch]`: o serviço (catálogo pelo `pacman -Si`, busca, filtros, categorias, AUR, atualizações, ações), sem interface.
- `gravity/exemplos/loja/`: `principal.c` (janela, threads, ações, depuração), `barra.c` (lateral, busca, painel de andamento, diálogos), `telas.c` (as telas e o detalhe).
- `packages/lumna-gravity`: `desktop/lumna-loja.desktop`, o ícone `lumna-loja`, `curl` nas dependências e paru/yay como opcionais.

## Testes

`sh gravity/testes/loja.sh [PASTA_DO_ORBIT]`: o serviço em C (com `pacman`, `checkupdates`, `curl` e `paru` falsos, que guardam o que o app instalou e removeu) e, se der a pasta do build do Orbit, o app inteiro num Orbit sem tela: catálogo, cartões, abas, busca, filtros, detalhe, instalar com confirmação e com o log, falha do pacman, AUR (busca, aviso, instalação pelo paru), Instalados, Remover, Atualizações e Atualizar tudo, Configurações, `Ctrl+K`. Sem relatório do ASAN/UBSAN, e quebrar o comando do pacman faz os testes falharem.

**Não testado**: um pacman, um AUR e um paru de verdade (nos testes são falsos), a janela de senha do pkexec, instalar de fato, o desempenho com o catálogo inteiro (uns 15 mil pacotes), ícones de apps reais, o toque. Roteiro: `docs/TESTES-ISO.md`, seção 2f.
