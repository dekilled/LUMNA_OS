# Painel de controle — o plano

O Painel do LUMNA em Gravity (`exemplos/painel.c`, `lumna-painel`). Os esboços estão em
`docs/visual/04-painel-de-controle.jpg` (o início) e nas telas de Tema, Cores e Papel de parede.

## Tudo num lugar só

O Painel é o único lugar das configurações. As telas que nasceram separadas viram seções dele, e
os exemplos soltos saem quando a seção estiver pronta:

| Tela solta | Passa a ser |
| --- | --- |
| `rede` e `wifi` | Rede e internet → Wi-Fi, Cabo, VPN... |
| o menu do Bluetooth da `barra` (a parte completa) | Bluetooth → Dispositivos |
| `mixer` (na janela) | Som → Saída, Entrada, Apps |
| `transferencia` | Celular Connect → Área de transferência |

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
- ✅ **Barra superior:** relógio (segundos, data), os ícones que aparecem e o indicador das áreas.
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
- 🔨 Apps padrão (navegador, música, vídeo, imagens, e-mail);
- ⏳ Permissões (câmera, microfone, localização, arquivos);
- ✅ Atualizações (os apps com versão nova; atualizar roda `yay -Syu`/`pacman -Syu` num terminal: o Arch atualiza o sistema inteiro junto).

**Telas e modos**
- ✅ Monitores: o desenho das telas, e por tela a resolução, a taxa, a escala, a rotação, ligar/desligar e a
  posição (à direita, à esquerda, acima ou abaixo de outra). Grava `~/.config/lumna/telas.ini`, que o Orbit
  vigia e aplica na hora (`servicos/telas.c`); depois de mudar algo que pode apagar a imagem o Painel pergunta
  "Manter?" e volta sozinho em 15 s. O Orbit nunca desliga a última tela. Falta: "qual é a principal" (o Orbit
  ainda não tem esse conceito) e identificar cada tela piscando um número nela.
- ⏳ Luz noturna;
- ✅ Modos (Desktop, Gaming, Mídia): o modo ao ligar, estender o modo às telas (ou um por tela) e, por modo,
  esconder a dock e o "não perturbe" (`modos.ini`). A base está no Orbit (um espaço por modo e por saída:
  trocar nunca fecha nem move janela; `orbit-msg modo`, o evento `modo`, Super+Tab e Super+Shift+Tab);
  a topbar tem o seletor (o nome do modo), o `lumna-modos` mostra o seletor no meio da tela a cada troca, a
  dock some e os avisos só deixam passar os urgentes. As telas de Gaming (biblioteca) e Mídia vêm depois;
- ⏳ Áreas de trabalho.

**Som**
- ✅ Saída, entrada e volume por app (o `mixer` já fala com o PipeWire): passa para este padrão.
- 🔨 Sons do sistema.

**Celular Connect**
- ⏳ Pareamento;
- ⏳ Área de transferência (a tela `transferencia` existe);
- ⏳ Avisos do celular;
- ⏳ Arquivos.

**Emma**
- ⏳ Voz;
- ⏳ "Oi Emma";
- ⏳ Provedores e modelos;
- ⏳ Privacidade;
- ⏳ Automações.

### Inicialização (a aba nova)

- 🔨 **Ao entrar:** o modo inicial (Desktop, Gaming ou Mídia) e reabrir as janelas da última sessão.
- 🔨 **Apps ao iniciar:** a lista de apps que abrem sozinhos (`~/.config/autostart`), com adicionar e remover.
- ⏳ **Entrada:** login automático, PIN ou senha, e a tela de boas-vindas.
- ⏳ **Em segundo plano:** os serviços do usuário (systemd --user), ligar e desligar.

### Sistema

- **Sobre:**
  - ✅ este computador (modelo, processador, memória, versão do LUMNA);
  - ✅ atualizações do sistema (procurar e histórico do pacman.log);
  - avisos legais.
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
  - 🔨 atalhos;
  - ✅ mouse e touchpad: botão primário (trocar para canhotos), velocidade e aceleração do ponteiro, rolagem
    natural, tocar para clicar e ignorar o touchpad ao digitar. Valem em aparelhos de verdade (libinput); o
    desenho do ponteiro e os ícones ficam para Temas;
  - ⏳ teclado na tela.
- **Data e hora:**
  - ⏳ automática;
  - ⏳ fuso;
  - 🔨 formato (24 h, data na barra).
- **Idioma e região:**
  - ⏳ idioma;
  - ⏳ formatos;
  - ⏳ métodos de entrada.
- **Usuários:**
  - ⏳ sua conta (foto, nome);
  - ⏳ senha e PIN;
  - ⏳ outros usuários.
- **Privacidade:**
  - ⏳ permissões dos apps;
  - 🔨 histórico e recentes (limpar os recentes do lançador);
  - ⏳ diagnóstico.

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
3. Inicialização.
4. As seções 🔨 do Sistema.
5. Trazer para dentro as telas soltas: Som (o mixer), Rede, Bluetooth e a área de transferência
   do Celular Connect, com o "Mais configurações" nos menus da topbar.
6. As ⏳, à medida que os serviços (NetworkManager, BlueZ, UPower...) entram.
