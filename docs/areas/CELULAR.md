# Área: Celular (a UI mobile e os aparelhos)

Leia antes o [LEIAME.md](LEIAME.md) e `mobile/README.md` (o histórico do porte do OnePlus 6T).

## Escopo

O LUMNA em celular: a **interface** (tela inicial, apps, central, bloqueio, teclado, gestos) e o **porte** para os aparelhos (kernel, boot, rootfs).
A interface é independente do aparelho; o porte de cada aparelho é um projeto de **Hardware** por aparelho (ver abaixo).

## Estado herdado (2026-11-21)

- **OnePlus 6T (`oneplus-fajita`, SDM845, kernel mainline 7.1-rc1)** é o laboratório: Arch Linux ARM nativo, tela, toque, GPU (Adreno 630, freedreno),
  UFS, Wi-Fi, modem registrado, primeira ligação recebida; falta áudio das chamadas, dados móveis, bateria no rootfs, bloqueio e gestos finais.
- **A UI antiga** = Hyprland (config em Lua: `mobile/session/hyprland-mobile.lua`) + `lumna-shell --mobile` (Vue + Rust, `shell/`) + apps GTK/WebKit. O Eric
  diz que **"já não inicia mais, não sei por quê"**. Hipóteses (nenhuma verificada, não há log): (a) o 6T recebeu `pacman -Syu` e o Hyprland novo quebrou a
  config Lua ou o shell; (b) o `lumna-shell` (Vue/WebKit) não sobe mais; (c) o CI do celular **falha desde pelo menos 2026-11-16** (segredo `EMMA_REPO_TOKEN`
  expirado, ver `ATUALIZACAO.md`), então o repositório `[lumna-celular]` ficou parado enquanto o Arch ARM andou; (d) o próprio boot (kernel/initramfs).
- **Desktop atual** = Orbit + Gravity (C), que roda bem. O Vue já foi descartado no desktop por desempenho e espaço (`docs/status/gera.py`).
- O que o Orbit e o Gravity **já têm de celular**: saída `DSI*` tratada como telefone (`output_is_phone`), escala própria (`phone_scale`), mosaico de um
  app por vez e par dividido (`phone_arrange`), gestos de borda (`orbit_gesture_*`, `input.c`), toque (`touch.c`), teclado na tela com input-method
  (`lumna-teclado`), rolagem por dedo com inércia no Gravity (`gv_rolagem.c`), tela de bloqueio (`lumna-bloqueio`), modos e avisos. **Falta**: tela
  inicial/gaveta de apps no formato celular, central (notificações e atalhos), apps recentes, barra de status/navegação, bloqueio com PIN em toque,
  rotação, os apps (Ajustes, Arquivos, Telefone, Contatos) em formato de celular, e **compilar tudo em aarch64 e rodar no 6T** (nunca foi feito).

## Caminho recomendado (precisa da decisão do Eric)

1. **A UI nova do celular é Orbit + Gravity**, a mesma base do desktop, em vez de reviver Hyprland + Vue. Motivos: é o que já roda bem; o Orbit já tem o modo
   telefone; um só código para PC, celular e (um dia) tablet; menos peso. O Vue/Hyprland fica como está (congelado), sem consertar.
2. **Primeiro passo técnico:** compilar Orbit + Gravity para aarch64 (o `pacotes.yml` roda em x86_64; a máquina ARM do `celular.yml` serve) e subir no 6T
   com o `primeira-tela`: provar tela, toque, GPU e o `lumna-barra`/`lumna-lancador` num celular de verdade, antes de desenhar qualquer coisa nova.
3. **Depois, tela por tela, cada uma começando por esboço do Eric** (regra de produto): barra de status + navegação; início e gaveta de apps; central;
   apps recentes; bloqueio; teclado; Ajustes; Telefone.
4. Os apps do desktop ganham um **modo estreito** (já existe para Música e Atlas: `gv_barra_janela`, largura mínima); o celular reaproveita.

## Aparelhos

| Aparelho | SoC | Linux mainline? | Papel |
|---|---|---|---|
| OnePlus 6T | SDM845 | sim (sdm845-mainline), já roda | laboratório |
| **Galaxy Note 10+ do Eric: hardware SM-N975F (Exynos 9825)**, hoje com uma **ROM portada do S22 Ultra** (por isso o Android diz `SM-S908B/DS`; o S22 Ultra tem Exynos 2200, que NÃO é o chip do aparelho) | Exynos 9825 | **praticamente não**: sem o SoC no kernel oficial (clocks, pinctrl, PMIC, UFS), GPU Mali-G76 (a Panfrost cobre o G76, mas sem a base do SoC não há como usá-la), modem Shannon fechado | meta do Eric; projeto grande e incerto, **o Eric quer construir o suporte** preenchendo lacunas e trazendo correções de aparelhos compatíveis |
| (referência) Note 10+ Snapdragon, SM-N975U/U1 | SM8150 (855) | sim | só para comparar |

**Atenção:** para saber o hardware verdadeiro, não confiar no que o Android diz (a ROM portada mente o modelo). Conferir pela etiqueta do aparelho ou no
recovery/bootloader (`fastboot getvar product`, ou o modo Download da Samsung).

## Pendências

1. ~~Decisão do Eric sobre o caminho~~ **decidido (2026-11-21): Orbit + Gravity + todos os apps, um a um**; o Vue/Hyprland antigo é abandonado (o Eric já o refez em C justamente por isso). A UI antiga reiniciava ao carregar o Hyprland (o cursor aparecia, depois reiniciava): não será consertada.
2. Saber por que a versão antiga não inicia **(só se o Eric quiser reviver; se for substituída, não importa)**: do 6T pela USB (`ssh alarm@lumna`):
   `journalctl -b -p warning --no-pager | tail -80`, `systemctl --failed`, `pacman -Q hyprland lumna-shell lumna-mobile`, `ls ~/.local/state/lumna`.
3. Compilar Orbit + Gravity em aarch64 e rodar no 6T. **Compilar: feito (2026-11-21)**, ver "Build aarch64" abaixo. **Rodar no 6T: pendente** (o Eric roda o `primeira-tela-orbit.sh`; roteiro em C-1..C-7).
4. Esboços do Eric da UI do celular.
5. ~~Conserto do CI "Celular"~~ **feito (2026-11-21)**: o `git clone` da Emma com o `EMMA_REPO_TOKEN` expirado derrubava o job em ~10 s; agora só avisa e a Emma fica com o pacote já publicado, e o `[lumna-celular]` voltou a ser publicado. **O token continua expirado** (só o Eric renova: Settings > Secrets > `EMMA_REPO_TOKEN`); enquanto isso a Emma do celular não atualiza.

## Build aarch64 do Orbit e do Gravity (2026-11-21)

- O CI "Celular" (`mobile/scripts/ci-celular.sh`, Arch Linux ARM numa máquina ARM do GitHub, sem emulação) agora compila **`lumna-orbit`** (com o
  `wlroots0.20`, igual ao desktop) e **`lumna-gravity`** (46 binários) em **aarch64** e publica os dois no `[lumna-celular]`, junto com os pacotes antigos.
  Versões pelo git (`mobile/scripts/versao.sh`). Primeira publicação: `lumna-orbit 0.1.r55`, `lumna-gravity 0.1.r283` (run 393 do Celular).
- **Provado só no CI (aarch64, sem tela):** os dois compilam; o Orbit arranca em headless com 2 saídas e responde ao `orbit-msg`; os binários do Gravity
  não têm biblioteca faltando (`ldd`). Se o teste falhar, nada é publicado.
- **NÃO provado (só o 6T prova):** tela, toque, GPU (freedreno/Adreno 630), a barra e o lançador num celular de verdade, escala/`phone_scale` no painel DSI.
- **Para instalar no 6T** (`ssh alarm@lumna`, com o repositório `[lumna-celular]` já configurado): `sudo pacman -Syu lumna-orbit lumna-gravity`.
  Atenção: o Gravity puxa dependências pesadas (firefox, ffmpeg, mpv...).
- **`mobile/session/primeira-tela-orbit.sh`** (novo): `sudo sh primeira-tela-orbit.sh` no 6T liga o `[lumna-celular]`, instala os dois pacotes, põe o login
  automático do alarm no tty1 e sobe o Orbit com papel de parede, barra, lançador, teclado na tela e um terminal (foot); uma tentativa por boot, logs em
  `~/orbit-celular.log`; `desfazer` desfaz. Escala do telefone 2.5 (a do teste do Hyprland; `ORBIT_PHONE_SCALE=2` muda). **Testado só com comandos de mentira**
  (gera os arquivos certos, `desfazer` limpa); sobe de verdade só no 6T. Quais peças do Gravity funcionam em formato de celular é justamente o que o teste mostra.
- O registro de testes agora **tem a área Celular** (ids `C-`): C-1 a C-7 são o roteiro do primeiro teste no 6T.

## Visão do Eric para o celular (2026-11-21)

- O objetivo da área é **trazer a UI do PC para o celular**: tudo o que existe lá (apps, configurações, tela de bloqueio, animação de boot e o
  navegador em construção) vem para cá.
- O celular tem **dois modos: celular e mídia** (correção do Eric: "desktop" era um engano, ele queria dizer "celular"). Um **modo gaming** talvez
  entre depois, mas o Eric acha difícil um jogo de Linux rodar num smartphone (não é pendência).
- O **modo mídia no celular muda**: vira **controle total do PC** (além do modo mídia simples). Detalhes a definir com esboço do Eric.
- Foco de aparelhos: **OnePlus 6T** e **Galaxy Note 10+**.

## Esboço: tela inicial (Eric, 2026-11-21)

Imagem: [celular/esboco-inicio.webp](celular/esboco-inicio.webp). De cima para baixo: barra de status (hora, Wi-Fi, sinal, bateria); cabeçalho com
logo LUMNA e seletor de modo (no esboço aparece "Desktop ▾", mas o certo é o nome do modo atual: **Celular** ou **Mídia**); relógio grande e data; barra "Pergunte à Emma" (microfone + seta); barra de navegação (Início, Arquivos,
Apps, Ajustes, Emma) e a pílula de gesto. Fundo: planeta roxo.

Decisões do Eric:
- **Cabeçalho (LUMNA + modo) é opcional no celular**: módulo que pode ser desativado, e os textos podem até sair. No PC não há essa opção, nem precisa.
- **O seletor do cabeçalho não abre lista de modos.** A troca de modo é por **gesto de pinça**, com um **toggle On/Off** nas configurações (o Eric pensou em pôr o toggle dentro do próprio mode switcher; ver a viabilidade depois):
  - **On:** a pinça troca o modo na hora, com o mode switcher igual ao do desktop: Zoom In, desliza para o próximo, Zoom Out.
  - **Off:** a pinça abre o mode switcher (Zoom In) e **para**, para o usuário escolher; depois de escolhido, Zoom Out.
- **A barra de navegação só aparece no Início.** Dentro dos apps ela vira uma **barra de opções** (do app).
- **Emma:** está ligada ao núcleo do sistema e presente em todas as telas (no LUMNA antigo: deslizar da borda de baixo e "Oi Emma"). Por ora a barra
  "Pergunte à Emma" e a aba Emma ficam **só como botões, sem função**, até entendermos por que a Emma não entrou em nenhuma área.

## Esboço: tela de bloqueio (Eric, 2026-11-21)

Imagens: [bloqueio](celular/esboco-bloqueio.webp), [com música](celular/esboco-bloqueio-midia.webp), [com Emma](celular/esboco-bloqueio-emma.webp),
[senha + teclado](celular/esboco-bloqueio-senha.webp). O Eric avisou que vai **atualizar** o bloqueio e os apps depois: tratar como base, não como final.

- **Bloqueio simples:** barra de status (com % da bateria), logo LUMNA no canto, cadeado, relógio grande e data; embaixo, o leitor de digital
  ("Deslize para desbloquear"), **lanterna** no canto esquerdo e **câmera** no direito.
- **Com música tocando:** o logo vai para o centro e aparece um cartão de mídia (capa, título, artista/álbum, estado, barra de progresso, aleatório,
  anterior, pausa/play, próxima, repetir, e a saída de áudio "Neste dispositivo >").
- **Com a Emma:** cartão no rodapé ("Emma / Como posso ajudar?", onda de voz e botão de microfone), acima do leitor de digital.
- **Senha:** "Desbloquear LUMNA", campo com pontos, olho (mostrar a senha), seta de confirmar, digital, link **Emergência** e o teclado completo
  (linha de números, QWERTY pt-BR com ç, ?123, espaço "Português (BR)", enter roxo). É o `lumna-teclado`.

## Esboço: tela de aplicativos (Eric, 2026-11-21)

Imagem: [celular/esboco-apps.webp](celular/esboco-apps.webp).

- Título "Aplicativos", botão **Loja** no canto, busca ("Buscar aplicativos") e abas **Todos / Trabalho / Mídia** (a aba Jogos saiu: era resquício do lançador do desktop, decisão do Eric).
- Grade de 3 colunas: Arquivos, Navegador, Terminal, Notas, Música, Vídeos, Loja, Ajustes, Emma. Abaixo, **Recentes** (Notas, Arquivos).
- Rodapé: só a aba **Apps** (selecionada) e o botão de **desligar** à direita; sem a barra de 5 itens do Início.
- **Apps próprios do LUMNA (lista do Eric):** Loja de apps, Calculadora, Atlas (explorador de arquivos), Galeria, Signals, Captura de tela, Música,
  Painel de configurações, Emma (AI harness) e Processos (mais o **Pulse**, o terminal). O celular leva estes, em modo estreito.
- **Nomes no esboço x apps reais (Eric):** "Terminal" é o **Pulse**; "Notas" são os **Signals**; "Vídeos" **não existe como app**: hoje o vídeo só toca
  a partir da Galeria (não dá para abrir de fora e não há player próprio ainda). Então "Vídeos" sai da grade (ou vira player no futuro, decisão do Eric).
- **Para conferir quando o Eric atualizar:** a grade ainda não tem Calculadora, Galeria, Captura e Processos (Signals e Pulse entram com os nomes certos).
  O Navegador é o que está em construção. A Loja e a Emma seguem "fora das áreas".

## Possibilidades (NÃO são pendência; sem promessa de sucesso)

Ideias que o Eric quer explorar **se der**. Dependem de hardware e drivers próprios de cada fabricante, então a chance de não funcionar é real.
Não entram no planejamento nem bloqueiam nada.

- **Modo "projetor"/estilo DEX no modo mídia:** ligar o celular num projetor ou TV e usá-lo como uma smart TV, parecido com o Samsung DEX.
  O Note 10+ tem suporte a saída de vídeo desse tipo no Android, mas a implementação em Linux dependeria de drivers próprios da Samsung (Exynos),
  o que é o ponto crítico. No OnePlus 6T não se sabe se a saída de vídeo (DisplayPort sobre USB-C) funciona no kernel mainline.
- **Trocar o Android do projetor do Eric pelo LUMNA:** o projetor dele roda Android; rodar o LUMNA direto nele fica para bem depois, só se
  houver base de hardware. Aparelho e SoC ainda não levantados.

## Recebido de outras áreas

(vazio)
