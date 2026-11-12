# Auditoria (novembro de 2026)

O que foi verificado, o que estava errado, o que foi corrigido (com a prova) e o que fica para decidir. Os modos de tela (**Desktop, Gaming e Mídia**) foram
auditados como **um componente só**: as mesmas regras base para os três, com as diferenças só onde o modo pede (o Gaming tem jogo; a Mídia não).

Tudo aqui foi verificado num Orbit sem tela, com clientes de verdade (apps do Gravity, X11 de teste). Nada foi visto num PC com monitores, Steam ou TV de verdade.

## Como foi feito

1. **Todas as baterias de teste** (`gravity/testes/*.sh`), uma a uma, com o Orbit sem tela quando pedem. Resultado na tabela do fim.
2. **Análise estática**: o Orbit inteiro e o Gravity (`src/` e `servicos/`) no `clang-tidy` (analisador do clang: ponteiro nulo, uso depois de liberar, vazamento,
   valor não inicializado; e um conjunto do `bugprone`). Cada aviso foi conferido no código: a maioria é falso positivo (anotados abaixo).
3. **Leitura do componente de modos** inteiro: `orbit/mode.c`, `arrange.c`, `view.c`, `output.c`, `telas.c`, `keybinds.c`, `ipc.c`, `protocolos.c`, `xwl.c`,
   e do lado do Gravity `servicos/modos.c`, a barra, o seletor (`lumna-modos`), o Painel > Modos, a Mídia (`midiad.c`) e a biblioteca do Gaming (`jogos.c`).
4. Para cada falha do componente de modos: um teste novo (`gravity/testes/modos_componente.sh`) que **falha no Orbit de antes** e passa no de agora.

## Componente de modos: o que estava errado e foi corrigido

| # | Gravidade | O que acontecia | O que mudou |
|---|---|---|---|
| M1 | **Alta** | **Monitor que sai** (desligado, desconectado, "só esquerdo/direito", espelhar): as janelas dele iam para o espaço que a outra tela *mostrava agora*. Se a outra tela estava na Mídia ou no Gaming, um app comum do Desktop **virava janela do quiosque: aparecia em tela cheia na TV**, fora da dock e do Alt+Tab. E ao contrário: a janela da Mídia/jogo virava uma janela comum do Desktop. | `orbit_view_adopt` (mode.c), usado pelos três caminhos (monitor destruído, desligado pelo Painel, espelhar): a janela do Desktop vai para o **Desktop** da tela que fica; a do Gaming/Mídia **continua no espaço do modo dela**, escondida até alguma tela abrir esse modo (como "trocar de modo nunca fecha nem move janela"). |
| M2 | Alta | Depois de um monitor sair, **o foco do teclado ficava na janela que sumiu** (o teclado digitava numa janela invisível). | `orbit_focus_visible` depois do arranjo, no monitor destruído e no desligado pelo Painel. |
| M3 | Média | `orbit-msg espaco N` numa tela em Gaming/Mídia **tirava a tela do modo por uma porta dos fundos** (o Alt+N recusa a mesma coisa). | O comando recusa, com a mesma regra do teclado: sair do modo é pelo `modo desktop`. |
| M4 | Média | **Pedido de ativação** de um app Wayland (xdg-activation) trocava o espaço da tela para o da janela: um app do Desktop pedindo atenção **tirava a TV da Mídia** (ou o Gaming sem jogo) sozinho. O X11 já não fazia isso: os dois caminhos tinham regras diferentes. | Uma regra só (`orbit_activation_may_switch`): a ativação troca o espaço do Desktop numa tela no Desktop, **nunca o modo de uma tela**. |
| M5 | Média | A **tela da Mídia que ficava vazia** (a última janela fechou) ficava preta. O Gaming já trazia a biblioteca de volta; a Mídia não. | `orbit_mode_ensure_shell` vale para os dois: Gaming traz a biblioteca, Mídia traz o Início (`lumna-midia tela inicio`, que não abre segunda cópia). Um intervalo de 5 s por modo evita laço. |
| M6 | Baixa | Ao entrar na Mídia o foco ia para a janela **mais antiga** da Mídia (o arranjo dizia "a última"): o critério do quiosque só valia no Gaming. | O mesmo critério nos dois: a com foco, o jogo (Gaming), a biblioteca (Gaming), a **última usada**, a última aberta. |
| M7 | Baixa | Alt+Shift+N movia a janela com foco **mesmo se ela estava no outro monitor** (ela mudava de espaço lá e sumia). | Só move a janela da tela onde está o cursor. |
| M8 | Baixa | O Painel > Atalhos mostrava "Ir para a área 4" e "Mandar para a área 4", que não existem (são 3 áreas). | Linhas tiradas. |
| M9 | Baixa | O Painel > Modos tinha o interruptor **"Esconder a dock"** no Gaming e na Mídia que **não fazia nada** (ninguém lia a opção; o Orbit sempre esconde a dock nessas telas). | No lugar, o texto do que acontece de fato. |
| M10 | Baixa | O "Não perturbe" dos modos valia pela tela **do cursor**, mas só era reavaliado quando um modo trocava, nunca quando o cursor passava de tela: jogando na TV com o mouse no outro monitor, os avisos apareciam ou não conforme onde o mouse estava. | Vale se **alguma** tela está num modo com "Não perturbe" (o que o Gaming promete: "enquanto a tela está em Gaming"). |
| M11 | Baixa | Os atalhos abriam programas sem `setsid` (o `mode.c` já fazia): o programa ficava no grupo de processos do Orbit. | `setsid` nos dois. |

**Prova:** `gravity/testes/modos_componente.sh` (dois monitores, a Calculadora como app comum, a janela de verdade da Mídia e o Signals marcado como jogo):
26 verificações. No Orbit de antes: **11 falhas** (a Calculadora aparecendo em tela cheia no espaço da Mídia, a janela da Mídia virando janela do Desktop,
`espaco` tirando a TV da Mídia, a Mídia vazia que não volta, o Gaming que não passa para a tela que ficou). No de agora: 0, também com ASAN.

**Não coberto por teste (só leitura):** M4 (precisa de um cliente que peça xdg-activation), M6, M7, M10, M11.

## Segurança: o que roda como root (pkexec) e o que escuta na rede

Lidos todos: `lumna-pkg`, `lumna-rede-mesclar`, `lumna-usuarios`, `lumna-entrada-ajustar`, `lumna-processos-ajudante`, e quem os chama no Gravity; e o que fica aberto na rede (o LUMNA Connect).

| # | Gravidade | Onde | O que acontecia | O que mudou |
|---|---|---|---|---|
| S1 | **Alta** | `lumna-pkg pacman ...` | Repassava **qualquer** argumento ao pacman como root: `--config`, `--root`, `--hookdir`, `--logfile` (escrever em qualquer arquivo)... Com a política `auth_admin_keep`, depois da pessoa digitar a senha uma vez (numa instalação pela Loja), **qualquer programa dela tinha uns minutos para virar root sem senha**. | Só as operações que o `makepkg` usa (`-U -S -D -R -T -Q`) e nenhuma opção que mude onde o pacman lê ou escreve. Testado com um pacman de mentira (as do makepkg passam; as outras são recusadas). |
| S2 | **Alta** | `lumna-pkg voltar CAMINHO` | Aceitava qualquer caminho: um subvolume Btrfs criado pelo usuário na pasta dele viraria **o sistema do próximo boot**. | Só um snapshot `antes-*` da pasta dos snapshots. A bateria do snapshot segue passando. |
| S3 | Média | `lumna-rede-mesclar` | Gravava os nomes de interface e os pesos crus no `/etc/lumna/mesclar.conf` e depois **executava esse arquivo como root** (`. "$CONF"`) a cada vez que uma rede sobe ou desce (o gancho do NetworkManager). Um peso `1;comando` virava um comando de root rodando sempre. (Gravar já pedia a senha de admin: é persistência, não escalada.) | Interface e peso validados; o arquivo é lido como texto, nunca executado. Testado: os valores com comando são recusados e o arquivo adulterado não executa nada. |
| S4 | Baixa | `lumna-entrada-ajustar papel` | Copiava como root qualquer arquivo com nome de imagem para `/etc/lumna`, legível por todos: um link `foto.png -> /etc/shadow` copiaria as senhas. (A política pede a senha toda vez.) | O arquivo é lido com os direitos de quem pediu (`PKEXEC_UID`); o papel antigo só sai depois do novo ser lido. A bateria do ajudante segue passando. |
| S5 | **Alta** | LUMNA Connect (rede local, porta 47800) | O código de pareamento tem 6 dígitos, vale 5 minutos e **não tinha limite de tentativas**: alguém na mesma rede Wi-Fi testava todos enquanto a pessoa pareava o celular e **ganhava o controle do PC** (teclado, mouse, comandos). O resto do Connect está bem feito (token aleatório de 32 bytes, comparação em tempo constante, código de uso único). | **5 códigos errados e o código é gasto**: a pessoa abre o pareamento de novo. Testado em `testes/connect.sh`: 5 erros e depois o código certo, que é recusado. Sem a correção, o teste mostra o "Intruso" pareando. |
| — | ok | `lumna-usuarios` | Nome de usuário validado; a senha é lida com `read -r` (uma linha só: não dá para enfiar outra linha no `chpasswd`). | Nada a mudar. |

## Outras falhas encontradas

| # | Onde | O que | Gravidade |
|---|---|---|---|
| F1 | Lançador > app > **Desinstalar** | **Nunca funcionou**: chamava `lumna-pkg desinstalar`, que não existe (e sem pkexec). Agora: o pacote dono do `.desktop` (`pacman -Qo`) sai pelo `lumna-pkg remove` com a senha; se for Flatpak, `flatpak uninstall`; senão, um aviso. Testado com pacman/pkexec/flatpak de mentira, inclusive nome com aspa. | Média |
| F2 | `testes/jogos/testa_loja_steam.c` | O teste pedia a cápsula do jogo 620, e o servidor falso (desde um commit antigo) dá 404 nas cápsulas de número par. O teste falhava; a Loja estava certa. Agora pede a do 621. | Teste |

## Outras correções

| # | Onde | O que | Gravidade |
|---|---|---|---|
| O1 | `orbit/orbit.h` | `_POSIX_C_SOURCE 200112L` deixava o `strdup` do `clip.c` **sem declaração** (em 64 bits, uma função sem declaração é tratada como devolvendo `int`: o ponteiro podia ser cortado; hoje o GCC salva por ser função embutida). Agora `200809L`. | Média (bomba-relógio) |
| O2 | `orbit/copiados.c`, `gravity/servicos/terminal.c` | `x = realloc(x, ...)`: sem memória, perdia o buffer e escrevia em NULL. | Baixa |
| O3 | `gravity/exemplos/painel/rede.c` | O proxy chamava o `gsettings` pelo shell com o texto digitado entre aspas: um host ou endereço PAC com aspa quebrava o comando. Agora sem shell, com o valor montado pelo GLib. (Não é falha de segurança: é o próprio usuário.) | Baixa |
| O4 | comentário do `mode.c` dizia "Desktop usa 1-4" (são 1-3). | Doc |
| O5 | dock, lançador | `mkdir -p '%s'` pelo shell quebrava com aspa no nome da pasta pessoal: agora `g_mkdir_with_parents`. | Baixa |

## Análise estática: o que o analisador apontou e não é falha

- `jogos.c:166` "uso depois de liberar": compara o ponteiro que **não** foi liberado. Falso.
- `segundoplano.c:72` "ponteiro nulo": `g_strsplit_set` nunca devolve NULL. Falso.
- `mdns.c:157, 293` "não inicializado": só se lê depois do `recvfrom` dar certo; `enderecos` preenche o que conta. Falso.
- `pacotes.c:562`, `sdservicos.c:217` "nulo": `no_de` é um `container_of`, nunca nulo; idem. Falso.
- `bandeja.c`, `bluetooth.c` "sizeof de ponteiro": vem das macros atômicas do GLib. Falso.
- `calc.c:529` "valor lixo": o `primario` sempre preenche quando dá certo; inicializado mesmo assim.
- `loja.c:573`: o `lj_buscar` devolve o **total** achado, não quanto coube; os dois chamadores se protegem (um aloca o tamanho do catálogo, o outro limita).
  Não é falha, mas é uma API fácil de usar errado (anotado abaixo).
- Orbit: nenhum aviso sério do analisador (só os `realloc` de O2).

## O que o Eric decidiu (e foi feito)

1. **O Gaming pula para a tela que ficou.** O monitor que tinha o Gaming sai: a tela que fica vira Gaming, com o jogo na frente e o foco nele
   (`orbit_mode_output_leaving`). Só se ela estava no **Desktop**: um filme na Mídia da outra tela não é interrompido (o jogo espera no Gaming até a
   pessoa pedir). As janelas do Desktop dessa tela continuam no Desktop dela. A Mídia de um monitor que sai **não** pula (fica guardada, como antes).
2. **A senha da Loja continua lembrada por alguns minutos; o AUR pede toda vez.** O AUR do shell antigo (o `PACMAN_AUTH` do makepkg) passou para um ajudante
   próprio, `/usr/lib/lumna/lumna-pkg-aur`, com a ação `os.lumna.pkg.aur` (`auth_admin`, sem lembrar). O `lumna-pkg pacman` direto agora recusa: só aceita
   vindo do `lumna-pkg-aur` (que, já como root, põe uma marca no ambiente; o pkexec limpa o ambiente de quem chama, então ninguém a forja). A Loja nova
   já instalava do AUR com `paru/yay --sudo pkexec` (o `pkexec pacman` direto, que pede a senha toda vez).
3. **A biblioteca do Gaming tem um dono só: o Orbit.** Entrar no Gaming, a tela do Gaming ficar vazia e o Gaming pular de tela passam pela mesma função
   (`orbit_mode_ensure_shell`), que só abre a biblioteca se o Gaming não tem nada para mostrar. O vigia do `lumna-jogos` ficou só com a energia.

## Ainda em aberto

- **`lj_buscar`** poderia devolver só quanto coube, para não depender de cada chamador se proteger.
- **O passo dos discos no teste do Atlas** falha quando roda logo depois do teste de menu (antigo; ver a tabela).

## As baterias de teste

Todas as de `gravity/testes/` (os ajudantes `*-falso.sh` não são testes), uma de cada vez, num Orbit sem tela.

| Bateria | Resultado | Observação |
|---|---|---|
| atlas | ok, menos 1 passo | `teste_discos.py` falha **quando roda logo depois do `teste_menu.py`**, e falha igual com o Atlas de **antes** desta auditoria; sozinho, passa. É ordem entre testes (o menu do teste anterior), antiga. Não corrigi. |
| calculadora, calculadora_app | ok | |
| connect | ok | com o teste novo da força bruta |
| desktop_app | ok | |
| emma, galeria, gamepad, jogos, musica, painel, portal, setup, signals, signals_app, teclado, servicos_sd | ok | |
| loja, loja_epic, loja_gog | ok | |
| loja_steam | ok depois de F2 | o teste estava errado (F2) |
| midia, modos_telas, gaming_frente | ok | o `gaming_frente` falhou uma vez **porque eu rodei outro Orbit com Xwayland ao mesmo tempo**; sozinho passa, no Orbit de antes e no de agora |
| processos, processos_ajudante, processos_atalho, processos_servicos | ok | |
| processos_app | ok | falhou uma vez com a máquina carregada (eu compilava o Orbit ao mesmo tempo: o menu de prioridade demorou a abrir); sozinho passa |
| trocas_x11 (normal e **ASAN**) | ok | no Orbit de agora |
| xwayland | ok em 10 de 11 rodadas no Orbit de agora; 5 de 5 no de antes | a uma falha: um app X11 comum não apareceu em 4 s (a lista de janelas veio vazia). O caminho que abre janela X11 não mudou nesta auditoria; **não provei a causa**. Se aparecer em casa, anote. |
| **modos_componente** (novo; normal e **ASAN**) | ok, 26 verificações | no Orbit de antes: 11 falhas |
| pkg/snapshot, painel/testa_entrada_ajustar | ok | depois de S2 e S4 |

ASAN no Orbit corrigido (`trocas_x11` e `modos_componente`): nenhum erro de memória.
