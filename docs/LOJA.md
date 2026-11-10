# Loja LUMNA (`lumna-loja`)

A loja de aplicativos do LUMNA OS, em Gravity (C/Clay), feita sobre o seu esboço (Explorar, Pacotes com busca e filtros, Drivers e kernels). Entrega em duas fases:

| Fase | O que | Estado |
|---|---|---|
| 1 | Explorar, Pacotes (busca com filtros), Instalados, Atualizações, Configurações; repositórios do Arch (core, extra, multilib) e AUR (paru/yay) | **pronta** (testada num Orbit sem tela, com pacman, curl e paru de mentira; falta o seu teste no PC de verdade) |
| 2 | Drivers e kernels, com detecção de hardware e checagem de compatibilidade | **pronta** (testada num Orbit sem tela, com `lspci`, `pacman` e `/boot` de mentira; falta o seu teste no PC de verdade) |

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

## Fase 2: Drivers e kernels

A página **Drivers e kernels** do esboço: o cartão **Hardware detectado** (as placas de vídeo e a arquitetura; "Ver hardware" mostra também processador, memória, kernel em uso, UEFI/BIOS, gerenciador de boot e espaço livre em `/boot`), as abas **Drivers** e **Kernels** e a chave **Mostrar só compatíveis** (ligada). Cada linha mostra o nome, a descrição, a fonte (repositório), as avaliações (ainda sem dados) e o estado: **Em uso** ou **Instalado** (verde), **Compatível**, **Verifique** (amarelo) ou **Incompatível** (vermelho), com **Ver detalhes**.

- **O que a checagem olha** (`servicos/loja_hw.c`, com o motivo em português em cada resultado):
  - *Vídeo*: a marca da placa (AMD, Intel, NVIDIA) pelo `lspci`; para a NVIDIA, a **geração** (pelo código do chip e pelo nome): Turing ou mais nova (RTX 20 em diante, GTX 16) serve ao driver atual; Maxwell, Pascal e Volta (GTX 9xx e 10xx) ficam **incompatíveis** com o driver atual (a Loja manda usar o nouveau, ou o nvidia-580xx do AUR); mais antigas, só o nouveau; geração desconhecida dá **Verifique**.
  - *Par kernel e driver*: o `nvidia` e o `nvidia-open` são pré-compilados para o kernel `linux` e o `nvidia-lts` para o `linux-lts`; usando outro kernel, são incompatíveis e a Loja sugere o `nvidia-dkms`, que compila para o kernel em uso e leva os headers junto.
  - *Pacotes de 32 bits* (jogos e Wine): sem o repositório multilib ligado, dá **Verifique** com o motivo.
  - *Processador*: o `amd-ucode` ou o `intel-ucode`, conforme o seu processador.
  - *Kernels* (`linux`, `linux-lts`, `linux-zen`, `linux-hardened`, só os oficiais): arquitetura, **espaço em `/boot`** (menos de ~200 MB livres é incompatível), driver NVIDIA pré-compilado que não carregaria no kernel novo, e se o gerenciador de boot (GRUB ou systemd-boot) foi reconhecido.
  - Em dúvida o resultado é **Verifique**, nunca "Compatível". Pacote que não está nos repositórios ligados é incompatível.
- **Incompatível bloqueia** (o botão Instalar fica desativado, com o motivo). Em **Configurações > Modo avançado** dá para liberar, e aí a confirmação aparece em vermelho avisando que pode deixar o sistema sem vídeo ou sem boot.
- **Kernel**: instala o kernel e os **headers** dele numa transação só (`pacman -S --needed linux-lts linux-lts-headers`). **A Loja não mexe no gerenciador de boot.** Depois de instalar, confere (e mostra no painel do andamento) se o kernel e a imagem de boot estão em `/boot` e se o GRUB ou o systemd-boot já listam o kernel novo; se não, diz o comando que falta (por exemplo `sudo grub-mkconfig -o /boot/grub/grub.cfg`). O kernel em uso e o único kernel instalado **nunca** saem; o `mesa` também não, com placa de vídeo.

## Não faz (ainda)

Drivers antigos e legados do AUR (nvidia-580xx, nvidia-470xx), kernels de terceiros (cachyos, xanmod), mexer no gerenciador de boot, driver de Wi-Fi e firmware, Flatpak, ícones dos apps fora do tema do sistema (usa o ícone do tema; sem ele, o desenho genérico), capturas de tela dos apps, avaliações, instalar um `.pkg.tar` solto, fila de instalações (é uma de cada vez), o botão Cancelar durante uma instalação.

## Arquivos

- `gravity/servicos/loja.[ch]`: o serviço (catálogo pelo `pacman -Si`, busca, filtros, categorias, AUR, atualizações, ações), sem interface.
- `gravity/servicos/loja_hw.[ch]`: o hardware, as regras de compatibilidade e a conferência do boot, sem interface.
- `gravity/exemplos/loja/`: `principal.c` (janela, threads, ações, depuração), `barra.c` (lateral, busca, painel de andamento, diálogos), `telas.c` (as telas e o detalhe) e `drivers.c` (Drivers e kernels).
- `packages/lumna-gravity`: `desktop/lumna-loja.desktop`, o ícone `lumna-loja`, `curl` nas dependências e paru/yay como opcionais.

## Testes

`sh gravity/testes/loja.sh [PASTA_DO_ORBIT]`: o serviço em C (com `pacman`, `checkupdates`, `curl` e `paru` falsos, que guardam o que o app instalou e removeu) e, se der a pasta do build do Orbit, o app inteiro num Orbit sem tela: catálogo, cartões, abas, busca, filtros, detalhe, instalar com confirmação e com o log, falha do pacman, AUR (busca, aviso, instalação pelo paru), Instalados, Remover, Atualizações e Atualizar tudo, Configurações, `Ctrl+K`. Sem relatório do ASAN/UBSAN, e quebrar o comando do pacman faz os testes falharem.

`testa_loja_hw.c` cobre o hardware (AMD, NVIDIA nova, NVIDIA Pascal, notebook híbrido, máquina virtual, geração desconhecida), cada regra, o que pode instalar e remover e a conferência do boot (GRUB e systemd-boot); `teste_drivers.sh` roda a página no Orbit sem tela (hardware detectado, abas, filtro, detalhe, instalar o kernel com os headers e conferir o boot, remover, o incompatível bloqueado e o modo avançado). ASAN/UBSAN limpos, e afrouxar uma regra ou o bloqueio faz os testes falharem.

**Não testado**: um `lspci`, um `/boot`, um GRUB e um systemd-boot de verdade, instalar de fato um kernel, o driver NVIDIA numa placa de verdade (as regras de geração seguem o suporte da NVIDIA de agora e podem envelhecer), um pacman, um AUR e um paru de verdade (nos testes são falsos), a janela de senha do pkexec, instalar de fato, o desempenho com o catálogo inteiro (uns 15 mil pacotes), ícones de apps reais, o toque. Roteiro: `docs/TESTES-ISO.md`, seções 2f e 2g.
