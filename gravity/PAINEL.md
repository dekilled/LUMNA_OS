# Painel de controle — o plano

O Painel do LUMNA em Gravity (`exemplos/painel/`, `lumna-painel`). Os esboços estão em
`docs/visual/04-painel-de-controle.jpg` (o início) e nas telas de Tema, Cores e Papel de parede.

**Em números (4 de novembro de 2026, contado no código):** 52 de 74 páginas prontas e 22 ainda só o esqueleto
(a página aparece como "em breve", com a lista do que vai ter). Os ✅ abaixo valem para uma página que existe e
funciona; ✅ não quer dizer que já foi testado na máquina de verdade.

## Tudo num lugar só

O Painel é o único lugar das configurações. As telas que nasceram separadas viram seções dele, e
os exemplos soltos saem quando a seção estiver pronta:

| Tela solta | Passa a ser |
| --- | --- |
| `rede` e `wifi` | Rede e internet → Wi-Fi, Cabo, VPN... |
| o menu do Bluetooth da `barra` (a parte completa) | Bluetooth → Dispositivos |
| `mixer` (na janela) | Som → Saída, Entrada, Apps |
| `transferencia` | LUMNA Connect → Área de transferência |

Situação de hoje: Rede e internet, Bluetooth e LUMNA Connect já têm as seções no Painel. Som ainda é esqueleto,
então o `mixer` segue sozinho.

A topbar fica com o **rápido**:
- ligar e desligar;
- escolher a rede ou o fone;
- o volume.

No pé de cada menu dela, um **"Mais configurações"** abre o Painel direto na seção certa
(`lumna-painel --secao rede/wifi`). O mixer continua a existir como camada de volume no celular
(as teclas de volume), mas sem ajustes próprios.

O código das telas fica em funções por seção dentro do Painel (`exemplos/painel/*.c`, uma por categoria),
para o `painel.c` não virar um arquivo gigante. Os serviços (`servicos/som.c`, e depois a rede e o
Bluetooth) são os mesmos para o Painel e para a topbar.

## O início

Abas no topo, com a busca à direita:

| Aba | O que tem |
| --- | --- |
| **Geral** | "Usados recentemente" (4 atalhos para os últimos painéis abertos) e a grade das categorias do dia a dia |
| **Inicialização** | o que acontece ao ligar e ao entrar (ver abaixo) |
| **Sistema** | a grade das categorias do sistema |

A busca procura em todas as categorias, seções e ajustes. Tocar num resultado abre o painel certo
e realça o ajuste. No rodapé ficam as Atualizações, como no esboço.

## Dentro de uma categoria

O mesmo padrão em todas, como nas telas de Tema, Cores e Papel de parede:

- **Barra lateral:**
  - "‹ Todas as categorias";
  - "Filtrar painéis";
  - o nome da categoria e as seções dela (o ícone e o nome; a escolhida com a faixa do acento à esquerda);
  - "Perguntar à Emma" no pé.
- **Conteúdo:**
  - a categoria em cinza;
  - o título e a descrição;
  - uma linha;
  - os ajustes;
  - no rodapé, **Restaurar padrão | Aplicar**.
- A aba de onde a categoria veio fica marcada.

**Aplicar:** a mudança aparece na hora como prévia sempre que der (cor, tema, papel de parede),
mas só fica gravada no Aplicar. Ao sair com algo pendente, o painel pergunta: Aplicar, Descartar
ou Continuar a editar. O Restaurar padrão volta a seção aos valores de fábrica (também pendente
até o Aplicar).

**Abrir direto numa seção:** `lumna-painel --secao aparencia/papel` (o menu do desktop, a dock,
a busca do lançador).

Os ajustes ficam em `~/.config/lumna/*.ini`. Cada peça vigia o seu arquivo e aplica na hora
(como a dock já faz com o `dock.ini`).

## As categorias

Legenda: ✅ feito · 🔨 a seguir · ⏳ depois (depende de um serviço)

### Geral

**Aparência**
- ✅ **Tema:**
  - estilo Escuro, Claro ou Automático (pelo horário);
  - densidade Compacta ou Confortável;
  - cantos das janelas Suaves ou Retos;
  - animações;
  - o tema atual.
- ✅ **Cores:**
  - a cor de destaque (6 do LUMNA, um #hex e o conta-gotas);
  - prévia ao vivo;
  - usar as cores do papel de parede (a cor dominante da imagem vira o acento);
  - destaque na janela ativa;
  - contraste (Padrão ou Alto).
- ✅ **Papel de parede:**
  - *O mesmo em todas as telas* ou *um por tela*: com "um por tela", o seletor de Monitor escolhe qual se edita.
  - Modo: Desktop, Gaming ou Mídia, e "Todos os modos".
  - Prévia grande, o nome, a coleção e "Escolher arquivo".
  - Ajuste: Preencher, Ajustar, Esticar ou Centralizar.
  - A Coleção LUMNA em miniaturas, mais "Ver todos".
  - Trocar automaticamente, com o intervalo.
  - ⏳ Vídeo e página web como papel de parede.
- ✅ **Dock:** formato, monitores e o que fazer com uma janela maximizada.
- ✅ **Barra superior:** relógio (segundos, data), o indicador das áreas e em quais telas ela aparece: só na
  principal, em todas, em nenhuma ou só nas que você escolher (`barra.ini`; a barra da tela principal abre e fecha
  as das outras, como a dock). Se nenhuma das telas listadas existe, volta para a principal.
- ✅ **Tela de bloqueio:** layout Centralizado ou Lateral, papel próprio ou o do desktop, o que está
  tocando, a Emma, o PIN (criar, trocar, tirar) e Bloquear agora. Antes era:
  - papel de parede próprio ou o mesmo;
  - mostrar avisos;
  - relógio grande;
  - bloquear depois de X minutos.
- ✅ **Tamanho e textos:**
  - a escala da interface;
  - o tamanho do texto;
  - a fonte;
  - texto em negrito.

**Rede e internet**
- ✅ Wi-Fi: liga/desliga o rádio, procura, conecta (com senha), desconecta, as redes guardadas com o
  histórico (última vez), DNS por rede, conectar sozinho, esquecer, adicionar manualmente (aberta, WPA2,
  WPA3, oculta). Com duas antenas, escolhe qual usar;
- ✅ Cabo: placas, estado, IP, gateway, DNS, velocidade, conectar/desconectar, conexões guardadas;
- ✅ Adaptadores: lista as placas e antenas com o modelo, o driver, o MAC e de onde vêm (Onboard/PCI ou
  Antena USB), e liga ou desativa cada uma;
- ✅ Proxy: nenhum, manual (HTTP/HTTPS/SOCKS, não usar para) ou automático (PAC); vale para a sessão
  (environment.d) e para o GNOME (gsettings);
- ✅ Várias conexões: Dividir (rota multipath pelo `lumna-rede-mesclar`, na proporção escolhida),
  Redundância (principal e reserva, pelas métricas do NetworkManager) e Somar (⏳ precisa de um servidor,
  MPTCP);
- ⏳ VPN;
- ⏳ Ponto de acesso;
- ⏳ Dados móveis (no celular).

O NetworkManager por D-Bus (`servicos/rede.c`: uma linha de fundo guarda o estado, o Painel copia). Sem
placa ou sem o NetworkManager, a seção avisa e diz como instalar. Para testar sem hardware:
`testes/rede-falsa.sh` (python-dbusmock).

**Bluetooth**
- ✅ Dispositivos: ligar/desligar, procurar, parear (agente sem PIN: Just Works), conectar, desconectar,
  esquecer, bateria e sinal;
- ✅ Nome e visibilidade: o nome, visível ou não, e o adaptador (Onboard ou USB);
- ⏳ Receber arquivos.

Pelo BlueZ (`servicos/bluetooth.c`); teste sem hardware em `testes/bluez-falso.sh`.

**Aplicativos**
- ✅ Instalados (pacman, AUR e AppImages; a página do app: origem, versão, tamanho, pasta, datas, o que depende dele, atualização, desativar e desinstalar mostrando antes o que sai junto e o que quebraria; `servicos/pacotes.c`);
- ✅ Apps padrão (navegador, e-mail, música, vídeo, imagens, PDF, texto e pastas, pelo mimeapps.list);
- ✅ Permissões (câmera, microfone, localização, captura de tela, avisos e segundo plano, pelo PermissionStore dos portais: vale para Flatpak e apps que usam os portais; app sem sandbox o sistema não limita);
- ✅ Atualizações (os apps com versão nova; atualizar roda `yay -Syu`/`pacman -Syu` num terminal: o Arch atualiza o sistema inteiro junto).

**Telas e modos**
- ✅ Monitores: o desenho das telas, e por tela a resolução, a taxa, a escala, a rotação, ligar/desligar e a
  posição (à direita, à esquerda, acima ou abaixo de outra). Grava `~/.config/lumna/telas.ini`, que o Orbit
  vigia e aplica na hora (`servicos/telas.c`); depois de mudar algo que pode apagar a imagem o Painel pergunta
  "Manter?" e volta sozinho em 15 s. O Orbit nunca desliga a última tela.
  Em cima disso, **Como usar as telas** (`disposicao.ini`, que o Orbit vigia): Estender (o modo troca em todas
  as telas), Por modo (cada tela com o seu modo, com um seletor por tela), Espelhar, Só a da esquerda e Só a da
  direita. No Orbit aninhado a tela "desligada" fica preta, porque uma janela de outro sistema não se desliga.
  Falta: identificar cada tela piscando um número nela, e a tela principal no Orbit real (a chave
  `tela_principal` do `orbit.ini` hoje só vale no Orbit aninhado).
- ⏳ Luz noturna;
- ✅ Modos (Desktop, Gaming, Mídia): o modo ao ligar, cada tela com o seu modo (a Mídia ou o Gaming numa só mudam aquela; sem nenhuma em modo especial, o Desktop estendido; cada barra mostra e troca o modo da PRÓPRIA tela, e a Mídia/Gaming existem numa tela só por vez: pedida noutra, as duas telas trocam de lugar) e, por modo,
  esconder a dock e o "não perturbe" (`modos.ini`). A base está no Orbit (um espaço por modo e por saída:
  trocar nunca fecha nem move janela; `orbit-msg modo`, o evento `modo`, Super+Tab e Super+Shift+Tab);
  a topbar tem o seletor (o nome do modo), o `lumna-modos` mostra o seletor no meio da tela a cada troca, a
  dock some e os avisos só deixam passar os urgentes. O Início da Mídia já existe (`lumna-midia`); a biblioteca
  de jogos do Gaming vem depois;
- ✅ Áreas de trabalho: quantas o Desktop usa (de 1 a 3) e o papel de parede de cada uma; a troca entre elas desliza.

**Som**
- 🔨 Saída, Entrada e Apps: o `mixer` e a topbar já controlam o volume pelo PipeWire (`servicos/som.c`); as três
  páginas do Painel ainda são esqueleto, e é para elas que o `mixer` vai passar.
- 🔨 Sons do sistema.

**LUMNA Connect**
- ✅ Pareamento: a chave geral do Connect, o código (vale uma vez e tem validade) e os endereços para digitar no app, e os
  celulares pareados, que se esquece com confirmação;
- ✅ Área de transferência: copiar num e colar no outro, nos dois sentidos, e "Mandar o que está copiado agora";
- ✅ Avisos do celular: os do celular aparecem aqui e os daqui vão para o celular, cada sentido liga e desliga;
- ✅ Arquivos: a pasta de recebimento, mandar um arquivo para um celular e as transferências com progresso.

Os recursos de cada celular ligam e desligam um a um. Quem fala com o celular é o `lumna-connect` (o protocolo
antigo, porta 47800, o mesmo do app LUMNA Connect do Android). Testado só com um celular falso
(`testes/connect.sh`); o celular de verdade ainda não.

**Emma**
- 🔨 Voz (o microfone e a fala já funcionam no serviço `lumna-emma`; falta a tela de escolher voz e modelo);
- ✅ "Oi Emma": ouvir sempre, offline, e abrir o assistente em qualquer tela;
- ✅ Provedores: as telas do harness (Configurações > Providers) no Painel: Ollama Cloud, OpenAI, Gemini, Claude ou um endereço seu, com a URL, a chave de API, os modelos (`/models`) e o teste de capacidades. A lista fica sempre na tela e guardada no PC; o serviço manda para o servidor da Emma (o motor) e traz de lá o que já existir, sem tela de login nem token. Se o servidor estiver desligado, a Emma conversa direto com o provedor (reserva);
- ✅ Roteamento: qual provedor e modelo atende Chat, Planner, RAG, Coder e Visão (o primeiro provedor já vira o padrão);
- ✅ Personalidade: a personalidade e as diretrizes da Emma (caixas de texto de várias linhas); gravam nas Configurações do servidor e numa cópia automática (persona.json) para a conversa direta;
- ✅ Recursos: liga e desliga cada capacidade (imagens, visão, servidores MCP, Oi Emma);
- ✅ Servidores MCP: as ferramentas de fora que a Emma pode usar (arquivos, web, GitHub...), sempre perguntando antes;
- ✅ Importar do harness: traz conversas, memórias e a personalidade de um backup;
- ✅ Perfis SFW e NSFW: um prompt para cada perfil na Personalidade, e o PIN e o botão na topbar para entrar no NSFW;
- ⏳ Voz com Fish Audio e voz local, Embeddings e Permissões das ações: as outras telas do harness, na sequência;
- ⏳ Privacidade;
- ⏳ Automações.

### Inicialização (a aba nova)

- ✅ **Ao entrar:** o modo inicial (Desktop, Gaming ou Mídia) e reabrir as janelas da última sessão (`inicio.ini`; quem faz é o `lumna-inicio`, que também guarda a sessão de 15 em 15 s).
- ✅ **Apps ao iniciar:** a lista de apps que abrem sozinhos (`~/.config/autostart`), ligar, desligar, adicionar (um app instalado ou um comando) e tirar. Os do sistema só se desligam.
- ✅ **Entrada:** login automático (`session/lumna-entrada-ajustar`, com pkexec e a política `os.lumna.entrada`), o desenho e o papel de parede da tela de login, e o atalho para Senha e PIN.
- ✅ **Em segundo plano:** os serviços do usuário (systemd --user): ligar ao entrar, iniciar e parar.

### Sistema

- **Sobre:**
  - ✅ este computador (modelo, processador, memória, versão do LUMNA);
  - ✅ atualizações do sistema (procurar e histórico do pacman.log);
  - ✅ avisos legais.
- **Armazenamento:**
  - ✅ uso do disco como no Android (barra: apps, cache, temporários, lixeira; os apps que mais ocupam levam à página do app);
  - ✅ limpeza (lixeira, temporários, cache do pacman, com confirmação);
  - ✅ discos e pendrives (montar, abrir, ejetar pelo udisks).
- **Energia:**
  - ⏳ perfil de energia (power-profiles-daemon);
  - ⏳ bateria (UPower);
  - 🔨 tela e suspensão (desligar a tela e suspender depois de X min);
  - ⏳ o botão de ligar.
- **Teclado e mouse:**
  - ✅ teclado: vários layouts do XKB com a tecla que troca de um para o outro, Caps Lock (Ctrl ou Esc) e a
    repetição das teclas (`entrada.ini`, o Orbit refaz o mapa na hora);
  - ✅ atalhos: Alt+Tab, Super+H, Super+Tab (modos), Super sozinho abre o lançador, e as outras teclas do Orbit;
  - ✅ mouse e touchpad: botão primário (trocar para canhotos), velocidade e aceleração do ponteiro, rolagem
    natural, tocar para clicar e ignorar o touchpad ao digitar. Valem em aparelhos de verdade (libinput); o
    desenho do ponteiro e os ícones ficam para Temas;
  - ⏳ teclado na tela.
- **Data e hora:**
  - ✅ acertar o relógio: automático pela internet ou à mão (o `timedatectl`, com a senha pelo polkit);
  - ✅ fuso horário;
  - ✅ formato: 24 ou 12 horas, que a barra e a tela de bloqueio leem de `hora.ini`.
- **Idioma e região:**
  - ⏳ idioma;
  - ⏳ formatos;
  - ⏳ métodos de entrada.
- **Usuários:**
  - ⏳ sua conta (foto, nome);
  - ⏳ senha e PIN;
  - ⏳ outros usuários.
- **Privacidade:**
  - ✅ permissões dos apps (as mesmas, por app);
  - ✅ histórico e recentes (lembrar ou não, limpar os recentes e a área de transferência);
  - ✅ diagnóstico (os logs deste computador: ver e apagar; nada é enviado).
- **Desenvolvedor:**
  - ✅ o modo desenvolvedor (`desenvolvedor.ini`): tela de toque no desktop (com o teclado de números no PIN) e aplicar o
    Painel em tempo real.

## Como a Aparência aplica

- **Tema, Cores e Tamanho:** geram `~/.config/gravity/tema.ini`, com `estilo` (escuro, claro ou
  automático), o acento e as cores derivadas dele, `contraste`, os raios, o espaço, a fonte e os
  tamanhos. Todas as apps Gravity vigiam a pasta e se repintam na hora. O que se escolheu fica em
  `~/.config/lumna/aparencia.ini`.
- **Cor do papel de parede:** a matiz que mais aparece no papel da tela principal, clareada para o
  tom de um acento. É calculada no Aplicar.
- **O que ficou para depois:**
  - destaque na janela ativa (é do Orbit);
  - a escala da interface (é do Orbit, por tela);
  - texto em negrito;
  - o conta-gotas;
  - centralizar como ajuste do papel.

## O papel de parede: o arquivo

`~/.config/lumna/papel.ini`, que o `papel` vigia e aplica na hora:

```ini
[geral]
por_tela = nao          ; sim: cada tela com o seu
trocar = nao            ; trocar sozinho entre os da coleção
intervalo = 30          ; minutos

[desktop]               ; um grupo por modo (desktop, gaming, midia); [todos] vale para os três
imagem = /usr/share/lumna/wallpapers/eclipse.jpg
ajuste = preencher      ; preencher, ajustar, esticar, centralizar

[desktop HDMI-A-1]      ; com por_tela = sim: o desta tela (sem o grupo, vale o do modo)
imagem = ~/Imagens/praia.jpg
```

O `papel` passa a abrir uma camada por tela (como a dock em "todos os monitores"). Cada uma lê
o seu grupo. O antigo `~/.config/lumna/papel` (um caminho só) continua a valer quando o `.ini`
não existe.

## Os outros arquivos

Cada peça vigia o seu arquivo em `~/.config/lumna/` e aplica na hora:

| Arquivo | Para quê | Quem lê |
| --- | --- | --- |
| `telas.ini` | resolução, escala, rotação, posição e ligar ou desligar, uma seção por tela | Orbit |
| `disposicao.ini` | `disposicao = estender \| espelhar \| esquerda \| direita` | Orbit |
| `modos.ini` | o modo ao ligar, as áreas e o que cada modo faz (um `layout = ...` de antes é ignorado) | Orbit, dock e avisos |
| `barra.ini` | segundos, data, áreas e `telas = principal \| todas \| nenhuma \| NOME,NOME` | barra |
| `hora.ini` | o formato da hora (24 ou 12 horas) | barra e bloqueio |

O `disposicao.ini` fica à parte do `telas.ini` porque o Painel regrava o `telas.ini` inteiro e trata toda seção
como se fosse uma tela.

## A ordem

1. ✅ A estrutura (em `exemplos/painel/`, um arquivo por categoria): as abas novas, a grade com "Usados recentemente", a vista da categoria com a
   barra lateral, o rodapé Restaurar | Aplicar, o `--secao` e a busca.
2. ✅ Aparência:
   - Papel de parede (com uma camada por tela no `papel`);
   - Cores (com as cores do papel de parede);
   - Tema;
   - Dock, que já existe, passa para o padrão novo;
   - Tamanho e textos;
   - Barra superior;
   - Tela de bloqueio.
3. ✅ Inicialização (as 4 páginas: Ao entrar, Apps ao iniciar, Entrada e Em segundo plano; falta testar na máquina de verdade).
4. As seções 🔨 do Sistema: já estão Atalhos e Data e hora; faltam Energia, Histórico e recentes e as outras.
5. Trazer para dentro as telas soltas: ✅ Rede, ✅ Bluetooth e ✅ a área de transferência do LUMNA Connect; falta o
   Som (o mixer), e o "Mais configurações" nos menus da topbar (a conferir).
6. As ⏳, à medida que os serviços entram: o NetworkManager e o BlueZ já entraram; falta o UPower (Energia).
