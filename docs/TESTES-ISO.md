# Roteiro de testes da ISO nova (Orbit + Gravity)

Para você ir marcando **passou / falhou** e me mandar o que falhou. Tudo aqui foi testado antes só com programas de
mentira e com o Orbit sem tela (headless): **na sua máquina é a primeira vez**, então falhar é esperado em algumas coisas.

## 0. Baixar e preparar

- A ISO nova sai pelo GitHub Actions no canal próprio, **sem mexer na ISO da UI antiga**: release **`teste-orbit`**
  (`lumna-os-teste.iso` + `.sha256`). A antiga continua em `teste`.
- Confira o SHA256 (`sha256sum -c lumna-os-teste.iso.sha256`).
- **VM (comece por aqui):** `scripts/run-vm.sh --disco --telas 2 lumna-os-teste.iso` (QEMU/KVM, UEFI, 64 GB de disco virtual).
  Precisa de `qemu`, `edk2-ovmf` e KVM. `--sem-gl` se a VM ficar preta.
- **Pendrive:** `sudo dd if=lumna-os-teste.iso of=/dev/sdX bs=4M status=progress oflag=sync` (confira o `sdX`!). Boot em **UEFI**.

## 1. Boot do live (a sessão do Orbit)

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 1.1 | Dar boot na ISO | Aparece o menu de boot "LUMNA OS" e o sistema sobe | ☐ |
| 1.2 | Esperar | Entra sozinho no **Orbit**: papel de parede, barra em cima, dock embaixo | ☐ |
| 1.3 | Olhar a barra | Relógio, modo, volume, rede, bateria (se for notebook) | ☐ |
| 1.4 | `Super` | Abre o menu Iniciar | ☐ |
| 1.5 | `Super+Tab` | Abre o seletor de modo (um 2º `Super+Tab` troca) | ☐ |
| 1.6 | `Alt+Enter` | Abre o terminal do LUMNA (`lumna-pulse`) | ☐ |

**Se a tela ficar preta ou voltar para o login:** é o risco que a gente já esperava (o Orbit nunca rodou num DRM de verdade).
No login escolha **"LUMNA (clássico · emergência)"** (a senha do live é `lumna`), ou `Ctrl+Alt+F2` para um terminal
(usuário `lumna`, senha `lumna`), e rode `journalctl -b | grep -i -E "orbit|lumna|greetd" > ~/log.txt`.

## 2. Funções no live

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 2.1 | Abrir o **Painel de controle** (dock) | Abre; as páginas carregam | ☐ |
| 2.2 | Painel > Rede > Wi-Fi (ou Cabo). Num terminal: `systemctl is-active NetworkManager` | Lista redes; conecta (a VM já vem com rede). O NetworkManager vem instalado e ativo (`active`), no live e no sistema instalado; o systemd-networkd e o iwd não sobem | ☐ |
| 2.3 | Painel > Som > Saída | Lista a saída; o tom de teste toca | ☐ |
| 2.4 | Mexer no volume na barra | O volume muda | ☐ |
| 2.5 | Painel > Sistema > Idioma, depois **Aplicar** | **Aparece a janela de senha do administrador** (`lumna-polkit`); senha `lumna` aplica | ☐ |
| 2.6 | Digitar a senha errada | "Senha incorreta"; na 3ª errada o pedido é recusado | ☐ |
| 2.7 | Abrir a janela de senha e apertar `Esc` | Cancela, nada é aplicado | ☐ |
| 2.8 | Abrir o **Firefox** (dock) | Abre; abre uma página (precisa de internet) | ☐ |
| 2.9 | Abrir **Arquivos** (dock, é o Atlas) | Abre a sua pasta | ☐ |
| 2.10 | `Print` | Abre a captura de tela | ☐ |
| 2.11 | Abrir a **Emma** (`Super+A`) | Abre o chat; (precisa de um provedor de IA configurado em Painel > Emma > Provedores) | ☐ |
| 2.12 | Notificação: `notify-send Oi "teste"` no terminal | Aparece o aviso no canto | ☐ |
| 2.13 | Bloquear a tela (Painel > Tela de bloqueio ou o menu de energia) e desbloquear | Bloqueia e desbloqueia | ☐ |
| 2.14 | Menu de energia (botão da barra) | Mostra desligar, reiniciar, suspender | ☐ |
| 2.15 | Abrir uma janela de fora que usa a barra do Orbit: no terminal do LUMNA, `foot &` | A janela tem a **barra de título do Orbit** (título, minimizar, maximizar, fechar), igual à dos apps do LUMNA | ☐ |
| 2.16 | Arrastar o `foot` pela barra; duplo clique na barra | Move a janela; o duplo clique maximiza e restaura | ☐ |
| 2.17 | Botões da barra: minimizar (volta pela dock), maximizar, fechar | Cada um faz o que diz | ☐ |
| 2.18 | Abrir o **Firefox**, o **Blender** (se tiver) | Cada um tem barra: a do Orbit (Blender) ou a deles com botões (Firefox). Se o Firefox não tiver barra: botão direito na barra de abas > "Barra de título" | ☐ |
| 2.19 | Abrir o **Terminal** do LUMNA (`Alt+Enter`, ou na lista de apps) e digitar `ls`, `echo çãé`, `htop` (se tiver) | Texto, cores e acentos certos; o cursor é um bloco lilás; `q` sai do htop | ☐ |
| 2.20 | No terminal: `seq 1 300` e rolar com a roda do mouse e com `Shift+PgUp` / `Shift+PgDn` | O histórico sobe e desce; aparece uma barrinha à direita; digitar volta ao fim | ☐ |
| 2.21 | Selecionar com o mouse (arrastar), duplo clique numa palavra, triplo clique numa linha; `Ctrl+Shift+C` e `Ctrl+Shift+V` (também botão direito > Copiar/Colar) | Seleciona em lilás; copia e cola no mesmo terminal e em outro app | ☐ |
| 2.22 | `Ctrl+Shift+T` (aba nova na mesma pasta), `Ctrl+PgUp/PgDn` (trocar de aba), `Ctrl+Shift+W` ou o × da aba (fechar), `exit` na última | Abas funcionam; `exit` na última fecha a janela | ☐ |
| 2.23 | `Ctrl+=`, `Ctrl+-`, `Ctrl+0` | A letra aumenta, diminui e volta; o terminal reajusta as colunas (`tput cols`) | ☐ |
| 2.24 | `Ctrl+C` num `sleep 100`; `vim` (setas, `Esc`, `:q`) e a roda dentro do vim | Ctrl+C interrompe; o vim responde às teclas | ☐ |
| 2.25 | Abrir o terminal (a conta nova, do `/etc/skel`): o `fastfetch` aparece sozinho na primeira aba (nas abas seguintes não); depois, `fastfetch` à mão | Aparece o desenho do LUMNA (o losango com o **LL** e a órbita) ao lado de Sistema, Base, Compositor, Interface e Sessão, e os blocos lilás embaixo | ☐ |
| 2.26 | Abrir o terminal e olhar o prompt | `usuário@máquina` em lilás; `ls` com cores | ☐ |
| 2.27 | Logo ao entrar na sessão, abrir um app (Apps ao iniciar, ou rápido na dock) enquanto a barra de cima ainda sobe | A janela não fica com o título escondido atrás da barra: ela se ajusta para a área útil assim que a barra aparece | ☐ |
| 2.28 | No Pulse, rodar `echo oi`, `ls /nao-existe` e ` echo escondido` (com espaço na frente); depois, num terminal qualquer: `sqlite3 ~/.local/share/lumna/pulse/pulse.db "select command, exit_code from commands"` | Os dois primeiros aparecem (códigos 0 e 2); o terceiro não | ☐ |
| 2.29 | `curl --password=abc123 http://localhost` no Pulse e olhar o banco | O comando guardado tem `--password=***` | ☐ |
| 2.30 | `lumna-pulse --migrar-banco`, `lumna-pulse --esquema`, `lumna-pulse --limpar-historico` | Mostra a versão, o SQL, e apaga tudo (o banco fica vazio) | ☐ |
| 2.31 | No Pulse (com uma IA configurada em Painel > Emma > Provedores), rodar um comando que erra: `gti status` | Em 1 a 3 s aparece um cartão lilás abaixo do prompt: "Emma sugere", o comando corrigido (`git status`), a explicação e as teclas | ☐ |
| 2.32 | Com o cartão aberto: **Enter** (espere meio segundo) | O comando corrigido roda | ☐ |
| 2.33 | Outro erro, e **Tab** | O comando entra no prompt, sem rodar; dá para editar e dar Enter | ☐ |
| 2.34 | Outro erro, e **Esc** | O cartão some e nada roda | ☐ |
| 2.35 | Dar Enter logo (por hábito) depois de um comando que erra | Não roda a sugestão: o Enter só pula a linha | ☐ |
| 2.36 | `grep xyz /etc/hostname` (sem resultado) e Ctrl+C num `sleep 100` | Nenhum cartão: não são erros | ☐ |
| 2.37 | Sem internet ou sem IA configurada: errar um comando | Nada aparece e o terminal segue normal (sem travar) | ☐ |
| 2.38 | No Pulse, num prompt vazio: `Ctrl+E`, escrever "listar os arquivos maiores que 10 MB aqui" e Enter | Aparece o campo da Emma e depois o cartão com um comando (`find . -size +10M ...`); nada roda ainda | ☐ |
| 2.39 | No cartão: Enter / Tab / Esc (em três pedidos) | Enter executa; Tab coloca no prompt sem rodar; Esc descarta | ☐ |
| 2.40 | Pedir "apagar a pasta temp" (um pedido destrutivo) e dar Enter no cartão | Só entra no prompt; roda quando você dá Enter de novo | ☐ |
| 2.41 | Escrever `echo par` e apertar `Ctrl+E` | O cursor vai ao fim da linha (bash): o campo da Emma não abre | ☐ |
| 2.42 | `Ctrl+E` e depois `Esc`; `Ctrl+E` com a internet desligada | Esc devolve o teclado ao shell; sem a Emma aparece um aviso de 3 s e some | ☐ |
| 2.43 | No Pulse, criar `touch /tmp/teste-pulse` e rodar `rm /tmp/teste-pulse` | O Enter não roda: aparece o cartão "Cuidado" com a frase do rm e, em 1 a 3 s, a explicação da Emma e o risco; o arquivo segue existindo | ☐ |
| 2.44 | No cartão: Enter (espere meio segundo) | O comando roda e o arquivo some | ☐ |
| 2.45 | Repetir o `rm` e apertar Esc | O cartão some, nada roda e o comando continua no prompt para editar | ☐ |
| 2.46 | Rodar `ls -la`, `echo oi` e `grep x /etc/hostname` | Rodam direto, sem cartão | ☐ |
| 2.47 | Em `~/.config/lumna/pulse.ini`: `[pulse]` e `explicar_comandos=touch` (abrir um Pulse novo) | Só `touch` pede explicação; `rm` passa a rodar direto | ☐ |
| 2.48 | Com a internet desligada, repetir o `rm` | O cartão aparece com a frase fixa e "A Emma não respondeu a tempo": a decisão é sua | ☐ |

### 2b. Teclado na tela (`lumna-teclado`)

| # | O que fazer | O que deve acontecer | OK? |
|---|---|---|---|
| 2.49 | Na VM sem teclado físico "de verdade" (ou tocando na tela): clicar numa caixa de texto do Firefox ou do Painel | O teclado sobe de baixo e a janela encolhe/sobe, sem ficar por cima do campo | ☐ |
| 2.50 | Digitar `olá, mundo` clicando nas teclas | A primeira letra sai maiúscula sozinha; o texto aparece no campo | ☐ |
| 2.51 | Segurar o `a` meio segundo | Abre á à â ã ä; escorregar até o `ã` e soltar escreve `ã`; soltar sem escolher escreve `a` | ☐ |
| 2.52 | `?123` e `=\<` e `ABC`; o `😀`, escolher uma categoria e um emoji | Trocam de página; o emoji entra no campo e fica nos "recentes" | ☐ |
| 2.53 | `⇧` uma vez, duas vezes, três | Uma maiúscula; trava (traço embaixo); solta | ☐ |
| 2.54 | Tecla `⌫` segurada; `↵` num campo de uma linha | Apaga repetindo; o Enter envia/confirma | ☐ |
| 2.55 | `⌄` (a setinha) | O teclado desce; ao clicar noutro campo ele volta | ☐ |
| 2.56 | Num monitor de PC: ver as teclas Esc, Tab e as setas | Aparecem na linha de baixo (só em tela larga) | ☐ |
| 2.57 | Painel > Teclado e mouse > Teclado na tela: "Só quando eu pedir", Aplicar, abrir um campo | Não sobe sozinho; num terminal: `lumna-teclado alternar` abre e fecha | ☐ |
| 2.58 | No mesmo painel: "Sempre que um campo abrir" com teclado físico ligado | Sobe mesmo digitando no teclado de verdade | ☐ |
| 2.59 | No Pulse (terminal), abrir o teclado por `lumna-teclado alternar` e digitar `ls` + Enter | As letras chegam (o terminal não fala text-input; vão como teclas) | ☐ |
| 2.60 | Com teclado físico e mouse (modo Sozinho): clicar num campo | NÃO sobe (só sobe ao tocar na tela, se tiver) | ☐ |

### 2c. Arquivos (`lumna-atlas`, só a navegação)

| # | O que fazer | O que deve acontecer | OK? |
|---|---|---|---|
| 2.61 | Abrir "Arquivos" no lançador | Abre em Home, com a barra lateral, a grade e o painel de detalhes | ☐ |
| 2.62 | Duplo clique numa pasta; voltar/avançar/subir; clicar num pedaço do caminho | Navega; o botão de voltar acende só quando há para onde voltar | ☐ |
| 2.63 | Clicar numa imagem | Miniatura na grade e prévia grande no painel | ☐ |
| 2.64 | Duplo clique num PDF e num `.txt` | Abre no aplicativo padrão; aparece em Recentes | ☐ |
| 2.65 | Grade ↔ lista; ordenar por tamanho e por data; o controle de zoom na grade | Muda; a lista mostra as colunas | ☐ |
| 2.66 | Buscar um nome com acento sem digitar o acento | Acha | ☐ |
| 2.67 | Favoritar uma pasta (painel ou botão direito) | Entra em Favoritos e em Fixados | ☐ |
| 2.68 | `+` > Nova pasta, digitar o nome, Enter | A pasta é criada e fica selecionada | ☐ |
| 2.69 | Num terminal criar um arquivo na pasta aberta | Aparece sozinho, sem atualizar | ☐ |
| 2.70 | Uma pasta com milhares de arquivos (ex.: `/usr/share/icons`) | Abre sem travar e rola suave | ☐ |
| 2.71 | Entrar em `/root` | Mostra "Não foi possível abrir esta pasta" e dá para voltar | ☐ |

| 2.72 | Selecionar um arquivo, Ctrl+C, abrir outra pasta, Ctrl+V | Copia (o original continua); Ctrl+Z tira a cópia | ☐ |
| 2.73 | Ctrl+X num arquivo, colar em outra pasta | Move; o recortado ficava esmaecido até colar | ☐ |
| 2.74 | Colar um arquivo que já existe no destino | Pergunta (Pular / Manter os dois / Substituir); nada some sem você dizer | ☐ |
| 2.75 | F2 num arquivo, digitar outro nome, Enter | Renomeia; um nome que já existe é recusado | ☐ |
| 2.76 | Delete num arquivo | Vai para a lixeira, a barra mostra "Desfazer"; os outros programas também o veem na lixeira | ☐ |
| 2.77 | Barra lateral > Lixeira: selecionar e Restaurar | Volta ao lugar de origem | ☐ |
| 2.78 | Shift+Delete | Pede confirmação; confirmado, some para sempre | ☐ |
| 2.79 | Botão direito num item (ou dedo parado) | O menu com Abrir, Recortar, Copiar, Renomear, Fixar, Compactar, Copiar caminho, Terminal, Propriedades, Lixeira | ☐ |
| 2.80 | Menu > Compactar, depois Extrair aqui no `.zip` | Cria o `.zip` e devolve o conteúdo numa pasta | ☐ |
| 2.81 | Menu > Abrir no terminal | O Pulse abre dentro dessa pasta | ☐ |
| 2.82 | Menu > Compartilhar (com o celular conectado) | Lista o celular; escolher oferece o arquivo a ele | ☐ |
| 2.83 | Ctrl+L, digitar parte de um caminho, Tab, Enter | Sugere, completa e abre | ☐ |
| 2.84 | Copiar uma pasta grande (vários GB) e Cancelar no meio | A barra anda; cancelar para e não deixa lixo pela metade além do já copiado | ☐ |
| 2.85 | Ctrl+I | As propriedades (inclusive o tamanho de uma pasta) | ☐ |
| 2.86 | Botão direito na área vazia | Novo arquivo/pasta, Abrir no terminal, Git clone, Ordenar por, Colar | ☐ |
| 2.87 | Git clone de um endereço https (rede ligada) | A barra mostra o progresso e a pasta clonada aparece; endereço inválido dá erro sem travar | ☐ |
| 2.88 | Botão direito numa pasta > Cor/ícone | A pasta muda de cor ou ganha o emblema, e continua assim depois de fechar e abrir | ☐ |
| 2.89 | Barra lateral > Início | Cartões de pastas e unidades; "Recentes" lista o que você abriu | ☐ |
| 2.90 | Engrenagem > Exibição: Lista grande, ocultos, extensões, "lembrar a visualização de cada pasta" | A prévia ao vivo acompanha; Aplicar muda a janela; cada pasta guarda a sua vista | ☐ |
| 2.91 | Engrenagem > Página inicial e Barra lateral: mudar a ordem, esconder, renomear, adicionar atalho | Reflete na barra; sobrevive a fechar e abrir | ☐ |
| 2.92 | Engrenagem > Avançado: restaurar o padrão da página | Volta só aquela página ao padrão | ☐ |
| 2.93 | Grade com "Exibir prévias nas pastas" ligado | As pastas mostram miniaturas do que há dentro; desligado, a pasta fica lisa | ☐ |
| 2.94 | Ctrl+T no Atlas; navegar numa aba e clicar na outra | Surge a faixa de abas; cada aba guarda a sua pasta e o seu voltar/avançar | ☐ |
| 2.95 | Botão direito numa pasta > Abrir em nova aba; X numa aba; Ctrl+Tab; Ctrl+W até a última | Abre depois da de agora; fecha; troca; a última aba fecha a janela | ☐ |
| 2.96 | Dock: o ícone **Atlas**; duplo clique numa pasta no Firefox (Mostrar na pasta) ou `xdg-open ~` | Abre o Atlas (não há mais Thunar) | ☐ |
| 2.97 | Botão direito numa pasta > Cor da pasta; depois Ícone da pasta | O submenu abre ao lado, com as cores (visto na atual) e a grade de emblemas; escolher vale na hora e fecha o menu | ☐ |
| 2.98 | Selecionar uma pasta e Ctrl+I (ou menu > Propriedades) | A janela de propriedades aparece **na tela** (com o véu escuro atrás); Geral / Permissões / Detalhes; Cancelar e Esc fecham; nada trava | ☐ |
| 2.99 | Propriedades > Geral: mudar o nome e Aplicar | Renomeia; "Abrir com" troca o aplicativo padrão do tipo | ☐ |
| 2.100 | Propriedades > Permissões: mudar "Outros" e Aplicar (também com "aplicar aos itens da pasta" numa pasta pequena) | `ls -l` mostra o novo modo | ☐ |
| 2.101 | Início: duplo clique e clique num cartão de pasta e de unidade; botão direito num cartão | Entra; o menu tem "Remover da página inicial", que tira o cartão (volta em Configurações) | ☐ |
| 2.102 | Barra lateral: apertar num item e arrastar para cima ou para baixo (mouse); soltar | Uma linha mostra onde cai; solta, o item muda de lugar e fica assim ao reabrir; Esc no meio cancela; um clique simples ainda navega | ☐ |
| 2.103 | Barra lateral: arrastar um item de Espaços para Coleções e vice-versa; no celular, segurar meio segundo e arrastar | Muda de seção; no toque a lista não rola enquanto se arrasta | ☐ |
| 2.104 | Atlas > engrenagem > Rede e nuvem; "Conectar a um servidor" com `smb://IP-DO-NAS/pasta` (ou um servidor do PC/celular) | Pede usuário e senha num diálogo do Atlas; conectado, aparece "Conectado" e dá para abrir a pasta; "Lembrar a senha" não pede de novo ao reconectar | ☐ |
| 2.105 | Menu "..." do compartilhamento: Desconectar, Conectar, Esquecer; fechar e abrir o Atlas | Desconectar tira a pasta; com "Reconectar ao abrir" ligado, o que estava conectado volta sozinho | ☐ |
| 2.106 | Barra lateral > Rede > Compartilhadas e Nuvem | Cartões dos compartilhamentos conectados (os desconectados só com "Mostrar locais indisponíveis") e das contas; clicar entra; botão direito abre o menu | ☐ |
| 2.107 | Adicionar conta Nextcloud (endereço, usuário, senha de aplicativo) | Sincroniza `~/Nuvem/NOME` sozinho; "Sincronizar agora" e o horário da última sincronização atualizam; erros aparecem em vermelho | ☐ |
| 2.108 | Adicionar conta Google Drive/Dropbox/OneDrive | O navegador abre para entrar; ao voltar a conta aparece e sincroniza | ☐ |
| 2.109 | "Lembrar a senha" de um servidor SMB/SFTP | **Atenção:** a sessão ainda não traz um provedor de segredos (gnome-keyring ou similar); sem ele o gvfs não guarda a senha. Anotar o que acontece | ☐ |
| 2.110 | Passar o mouse num arquivo ou pasta, ou selecioná-lo (grade e lista) | Aparece o "..." no canto/fim; clicar abre o mesmo menu do botão direito; na grade o ícone dos três pontinhos não cobre o nome | ☐ |
| 2.111 | Botão direito num item da barra lateral (Início, uma pasta, Lixeira, Compartilhadas) e, no celular, segurar e soltar sem andar | Abre o menu (Abrir, nova aba, nova janela; Esconder/Remover nas pastas; Esvaziar na Lixeira); "Abrir em nova janela" abre outra janela do Atlas no lugar certo | ☐ |
| 2.112 | Dock: apertar num ícone fixo e arrastar de lado (mouse); soltar | Uma linha mostra onde cai; o ícone muda de lugar e fica assim ao reabrir a sessão; um clique simples ainda abre o app | ☐ |
| 2.113 | Dock no celular: segurar meio segundo e arrastar; segurar e soltar sem andar | Reordena; sem andar abre o menu do app | ☐ |
| 2.114 | Abrir um app que não está na dock e arrastar o ícone dele (na dock) para entre os fixos | Ele fica fixo ali | ☐ |
| 2.115 | Menu de aplicativos (Início) > botão **Sair da conta** (ao lado do Bloquear) | Abre uma confirmação (Cancelar / Sair); "Sair" encerra a sessão e volta para a tela de login (greetd) | ☐ |
| 2.116 | No menu de aplicativos, apertar num app e arrastar (mouse) até a dock; soltar | O menu fica transparente enquanto arrasta, o app fica fixo na dock onde caiu e o menu fecha. Soltar fora da dock: nada fixa e o menu volta como estava | ☐ |
| 2.117 | O mesmo com o dedo: segurar meio segundo num app do menu e arrastar | Idem | ☐ |
| 2.118 | Atlas: arrastar um arquivo (mouse) para uma pasta da grade, para um atalho da barra lateral e para o fundo da lista | Copia para lá; o original continua onde estava; o destino acende | ☐ |
| 2.119 | Atlas: arrastar vários selecionados; soltar na pasta de onde vieram | Copia todos; na própria pasta não faz nada (nem cria "(2)") | ☐ |
| 2.120 | Atlas: deixar a janela baixa (arraste a borda de baixo) e abrir o menu de contexto de um arquivo perto do fim da janela | O menu aparece **inteiro, passando da borda da janela**, sem barra de rolagem; só rola se for mais alto que a tela. Vale também para os menus de outros apps (Firefox...) | ☐ |
| 2.121 | Ligar um pendrive. Atlas > Início > Unidades e dispositivos | Aparece um cartão com o nome do pendrive, "não montado" e o botão **Montar**. Tocar monta e abre a pasta dele (a lista se refaz em até uns 3 s) | ☐ |
| 2.122 | No pendrive montado: botão direito no cartão > **Ejetar** | Desmonta e desliga; o aviso diz que pode retirar. Se você estava dentro dele, o Atlas volta para a Home | ☐ |
| 2.123 | Painel > Armazenamento > Discos e pendrives > ligar **Ao conectar**; tirar e ligar o pendrive | Ele é montado sozinho (aparece no Atlas como montado). `pgrep lumna-discos` mostra o serviço | ☐ |
| 2.124 | O mesmo com **Ao entrar na sessão**: deixar o pendrive ligado e sair e entrar na sessão | Já está montado ao chegar | ☐ |
| 2.125 | Dock: apertar num ícone fixo e arrastar para **fora** da dock (para cima), soltar | O arraste acaba, o ícone fica onde estava e nada fica preso ao mouse | ☐ |
| 2.126 | Tema claro (Painel > Aparência): passar o mouse nos itens do Atlas, do Painel e dos menus | O texto e os ícones continuam legíveis (o lilás do acento ficou mais escuro no claro) | ☐ |
| 2.127 | Num computador antigo (sem SSE4.2): `systemctl --user status lumna-emma` e Painel > Emma > Provedores | O servidor fica `active (running)` e o Painel conecta (antes caía com `core-dump` em loop). `curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8011/api/auth/me` dá `401` | ☐ |
| 2.128 | Nesse computador: Painel > Emma > Voz > instalar o Whisper ou o Pocket TTS | Recusa na hora: "Este processador é antigo demais para a voz local" (use o Fish Audio). Num computador novo instala normal | ☐ |
| 2.129 | Atlas: criar uma pasta com nome bem comprido (ex.: `Projeto_de_Historias_Fantasticas_Volume_Um`) e uma palavra longa sem espaço | Na grade o nome quebra em até duas linhas e termina em **…** se ainda passar; o ícone ficou um pouco mais largo. Na lista termina em **…**. Selecionando, o painel de detalhes mostra o nome inteiro | ☐ |
| 2.130 | Atlas: abrir uma pasta com milhares de arquivos e fotos grandes; rolar depressa (Fim, Início, PageDown) | Não cai, não pisca e as miniaturas da tela aparecem logo (só as visíveis são pedidas) | ☐ |
| 2.131 | Atlas: montar um pendrive velho (nomes com acento de outro sistema, de FAT/NTFS antigo) | Abre a pasta; os nomes ruins aparecem com um "�" no lugar do byte inválido, e nada cai | ☐ |
| 2.133 | Atlas: abrir uma pasta lotada de subpastas (cada uma com muitos arquivos) e ficar olhando as miniaturas coloridas dentro das pastas | As pastas aparecem na hora (sem congelar) e os pontinhos entram depois, sempre das mesmas cores e na mesma ordem (a dos nomes); não mudam sozinhos | ☐ |
| 2.134 | Atlas: se ele cair, abrir `~/.cache/lumna/crash/atlas.log` | O arquivo existe com o sinal, a pilha e o mapa; mande-o para mim (ou `coredumpctl info lumna-atlas`) | ☐ |
| 2.135 | Atlas, num disco grande (o NTFS de 1 TB): abrir uma pasta lotada e, em seguida, clicar, clicar, clicar em várias pastas | O Atlas não congela nem fecha: a pasta nova mostra "Abrindo…" se demorar, o painel de detalhes mostra "calculando…" e depois o tamanho | ☐ |
| 2.136 | Atlas: deixar uma pasta aberta por um minuto sem mexer; procurar `nada mudou` em `journalctl --user -b` | Nenhuma linha (se aparecer, a pasta está mesmo mudando por fora e o log diz qual evento) | ☐ |
| 2.137 | Dock: abra 3 janelas do mesmo app, passe o mouse no ícone (aparece a lista), feche uma pelo X e passe o mouse sobre as outras linhas | O hover e o clique acompanham a lista que encolheu (antes ficavam ~48 px deslocados até o mouse sair da lista e voltar) | ☐ |
| 2.138 | Abra um app (o Firefox), vá para o modo Mídia (Super+Tab), volte ao Desktop (mais de uma vez, e também depressa) | O app volta inteiro na tela, no lugar onde estava (antes, com a troca interrompida por outro arranjo, ele constava como ativo mas sumia da tela, parecia minimizado, e clicar nele na dock não fazia nada). Se algo ainda sumir: `orbit-msg janelas` diz `visivel` e `recortada` de cada janela, e o journal tem `dock: toque em` e `a dock pediu ativar` | ☐ |
| 2.132 | Deixar o Atlas, a dock e a barra parados por uns 30 s e olhar o uso de CPU (`top`) | Perto de 0 % (antes, cada app Gravity gastava 15 a 50 % de um núcleo redesenhando à toa) | ☐ |

### 2d. Galeria (`lumna-galeria`, fases 1 a 3)

| # | O que fazer | O que deve acontecer | OK? |
|---|---|---|---|
| 2.139 | Abrir "Galeria" no lançador, com fotos em `~/Imagens` e vídeos em `~/Vídeos` | Mostra a grade por data (Hoje, Ontem...), a barra lateral (Todas, Recentes, Favoritos, Fotos, Vídeos, Lixeira, Álbuns) e a duração nos vídeos | ☐ |
| 2.140 | Uma pasta com milhares de fotos (ou o disco NTFS): rolar depressa | Não congela nem fecha; as miniaturas da tela aparecem logo | ☐ |
| 2.141 | Passar o mouse numa miniatura, clicar no quadradinho; Shift+clique em outra; Ctrl+A; Esc | Seleciona, faixa, tudo, limpa; a barra de seleção aparece com a contagem | ☐ |
| 2.142 | Duplo clique numa foto; roda do mouse; `+`, `-`, `0`; duplo clique de novo; arrastar; setas; `I`; Esc | Abre grande, amplia até 12× (duplo clique alterna 2,5×), move, passa para a próxima, mostra os detalhes e volta | ☐ |
| 2.143 | No toque: duplo toque na foto aberta, os botões ◀ ▶ dos lados, segurar numa miniatura | Amplia 2,5×, passa de foto, abre o menu (pinça e deslizar ainda não existem) | ☐ |
| 2.144 | Duplo clique num vídeo | Abre no mpv com o volume das configurações; o vídeo toca | ☐ |
| 2.145 | Favoritar (botão ou `F`), abrir "Favoritos"; desfavoritar | Entra e sai da lista | ☐ |
| 2.146 | Novo álbum, adicionar 2 mídias, renomear, tirar uma do álbum, apagar o álbum | Tudo funciona e **as fotos continuam nas pastas** | ☐ |
| 2.147 | Selecionar, Delete; Lixeira; Restaurar; depois Apagar para sempre | Vai para a lixeira com Desfazer; restaurar devolve; apagar pede confirmação; o Atlas vê a mesma lixeira | ☐ |
| 2.148 | Soltar uma pasta de fotos de um pendrive na janela (e o botão Importar) | Copia (ou move, conforme a configuração) para "Importadas"; arquivo que já existe pergunta | ☐ |
| 2.149 | Num terminal, `cp` uma foto para `~/Imagens` com a Galeria aberta | Aparece sozinha | ☐ |
| 2.150 | Foto muito grande (ex.: 8000×6000) e um `.jpg` quebrado | A grande abre sem travar; a quebrada mostra o ícone de falha e nada cai | ☐ |
| 2.151 | Menu "..." > Compartilhar, com o celular conectado pelo Connect | A mídia chega no celular; sem celular diz "Nenhum celular conectado" | ☐ |
| 2.152 | Engrenagem: adicionar uma pasta, Aplicar; Cancelar numa outra mudança | A pasta entra na biblioteca e vai para `~/.config/lumna/galeria/config.ini`; Cancelar não muda nada | ☐ |
| 2.153 | Configurações > Visualização > Limpar cache | O "Cache em uso" volta a zero e as miniaturas refazem | ☐ |
| 2.154 | Se ela cair: `ls ~/.cache/lumna/crash/` | Há um `lumna-galeria-<número>.log` com a pilha; mande-o para mim | ☐ |
| 2.155 | Visualizador de uma foto: botão do lápis (ou tecla `E`); no menu "..." da foto, Editar | Abre o editor com a foto, as 4 abas embaixo e o painel Ajustar | ☐ |
| 2.156 | Ajustar: arrastar Exposição, Contraste, Realces, Sombras, Temperatura, Saturação | A prévia acompanha (com a foto grande, de 20+ MP, sem travar o arrastar); tocar no número zera | ☐ |
| 2.157 | Segurar "Comparar original" | Mostra a foto como abriu; soltar volta à editada | ☐ |
| 2.158 | Desfazer/Refazer pelos botões e por Ctrl+Z / Ctrl+Shift+Z | Anda pelos passos; um ajuste novo depois de desfazer corta o refazer | ☐ |
| 2.159 | Recortar: escolher 16:9, arrastar um canto, arrastar por dentro, Inverter, Livre | O recorte acompanha o dedo/mouse, mantém a proporção, fica dentro da foto; o "Tamanho final" confere | ☐ |
| 2.160 | Girar à direita/esquerda e espelhar; depois Recortar | A foto gira/espelha; o recorte recomeça inteiro | ☐ |
| 2.161 | Filtros: cada um, e a Intensidade | A miniatura é da sua foto; Original tira o filtro; a intensidade mistura | ☐ |
| 2.162 | Zoom (−, +, lista, roda do mouse) e arrastar a foto ampliada | Dá zoom até 8×; arrastar anda; no Recortar o zoom fica desligado | ☐ |
| 2.163 | Salvar cópia | Aparece `nome-editada.jpg` ao lado do original, **igual ao que se viu**, na resolução inteira; o original não muda; a Galeria volta e a cópia está na biblioteca | ☐ |
| 2.164 | Salvar outra vez, sem mexer na foto | O botão não deixa (sem alterações); depois de mexer, a cópia vem como `-editada-2` | ☐ |
| 2.165 | Configurações > Edição: PNG e Qualidade 60; salvar de novo | A cópia sai PNG; com JPEG e 60 o arquivo é bem menor | ☐ |
| 2.166 | Mexer e apertar Cancelar, Voltar ou Esc | Pergunta "Descartar as alterações?"; Continuar mantém tudo; Descartar sai sem salvar nada | ☐ |
| 2.167 | Uma foto de celular "deitada" (com orientação no EXIF) | O editor mostra em pé e a cópia sai em pé | ☐ |
| 2.168 | Editar um PNG com transparência | A cópia PNG mantém a transparência | ☐ |
| 2.169 | Foto enorme (ex.: 9000×9000) | Abre ("Abrindo a foto…"), edita, salva sem a Galeria cair (a cópia pode levar alguns segundos) | ☐ |
| 2.170 | Biblioteca: selecionar um vídeo e apertar `E` (ou botão direito > Editar) | Abre o editor de vídeo com o quadro, a faixa com miniaturas e o painel Editar vídeo | ☐ |
| 2.171 | Arrastar as alças da faixa; usar os ▲▼ de Início e Fim; clicar na faixa | O trecho muda (mín. 0,5 s), a Duração final acompanha, o ponto da prévia vai junto | ☐ |
| 2.172 | Play, Pausa, Espaço, tocar até o fim do trecho | Toca na janela em tempo real, **sem som**; para sozinho no fim | ☐ |
| 2.173 | Silenciar áudio, Volume, Girar vídeo | Os controles respondem; Girar vira a prévia e a faixa | ☐ |
| 2.174 | Exportar cópia (trecho, mudo e girado) | O botão mostra o progresso; sai `nome-editado.mp4` ao lado do original, **só o trecho**, mudo e girado; o original não muda; a Galeria volta e a cópia está na biblioteca | ☐ |
| 2.175 | Exportar com volume 50% e abrir a cópia num player | Soa mais baixo (cerca de 6 dB) | ☐ |
| 2.176 | Durante a exportação, apertar "Cancelar exportação" (num vídeo longo) | Cancela, avisa e não deixa arquivo (nem `.lumna-tmp`) na pasta | ☐ |
| 2.177 | Mexer e apertar Cancelar, Voltar ou Esc | Pergunta "Descartar as alterações?"; Descartar sai sem exportar nada | ☐ |
| 2.178 | Um vídeo grande (1080p, vários minutos) e um de celular na vertical | Abre sem travar (as miniaturas chegam em segundos), a prévia sai na orientação certa e a cópia também | ☐ |
| 2.179 | Um vídeo sem áudio e um que não é MP4 (MKV, WebM) | O painel diz que não tem áudio; o MKV/WebM exporta como MP4 | ☐ |

### 2e. Música (`lumna-music`)

| # | O que fazer | O que deve acontecer | OK? |
|---|---|---|---|
| 2.180 | Pôr músicas em `~/Músicas` (MP3, FLAC...) e abrir "Música" no lançador | A biblioteca aparece (Minha biblioteca, Álbuns, Artistas, Pastas) com as capas e as tags certas | ☐ |
| 2.181 | Uma biblioteca grande (milhares de faixas, ou o disco lento): abrir e rolar a lista | Abre dizendo "Procurando…" sem travar; a 2ª abertura é bem mais rápida; rola suave | ☐ |
| 2.182 | Abrir um álbum e tocar em Reproduzir; depois em uma faixa do meio | **Sai som** pela caixa/fone; a lista toca a partir de onde clicou; o tempo anda | ☐ |
| 2.183 | Pausar, Próxima, Anterior (nos primeiros 3 s e depois), clicar e arrastar na barra de tempo, volume | Tudo responde; arrastar na barra não pula o som até soltar | ☐ |
| 2.184 | Aleatório e Repetir (nada, fila, faixa) | A ordem muda; no fim da fila com "repetir a fila" volta à primeira; com "repetir a faixa" ela recomeça | ☐ |
| 2.185 | Deixar um álbum tocar inteiro com "Reprodução sem intervalos" ligado | De uma faixa para a outra **não há corte** (álbum ao vivo ou de música contínua é o melhor teste) | ☐ |
| 2.186 | Configurações > Reprodução > Transição entre faixas em 4 s | As faixas se misturam no fim (uma some, a outra entra); mexer na barra durante a mistura a desfaz | ☐ |
| 2.187 | Abrir o painel da fila (ícone no canto); tocar numa linha; arrastar a alça; botão direito numa linha | Pula para a linha; a ordem muda; tira da fila | ☐ |
| 2.188 | "..." de uma faixa: Tocar a seguir, Adicionar à fila, Favoritar, Adicionar a uma playlist | Cada um faz o que diz; o coração da barra acende na favorita | ☐ |
| 2.189 | `+` ao lado de Playlists: criar; adicionar faixas; arrastar a alça para reordenar; renomear e apagar pelo "..." | Funciona; apagar pergunta e **não apaga as músicas**; o arquivo `.m3u8` abre em outro player | ☐ |
| 2.190 | Buscar `cancao` (sem acento) e depois duas palavras (artista e álbum) | Acha a "Canção"; várias palavras afinam; apagar a busca volta à tela de antes | ☐ |
| 2.191 | Uma música com `.lrc` ao lado: abrir a letra (ícone da barra) | Aparece "Sincronizada"; a linha da vez acende e rola junto; tocar numa linha vai até ela; "Seguir reprodução" desliga a rolagem | ☐ |
| 2.192 | Uma música com a letra só na tag; uma sem letra | A 1ª aparece inteira ("Não sincronizada"); a 2ª diz como pôr o `.lrc` | ☐ |
| 2.193 | Configurações: cada aba (Reprodução, Biblioteca, Letras, Aparência, Emma) | Mudar vale na hora e fica gravado; adicionar uma pasta traz as músicas; remover tira | ☐ |
| 2.194 | Barra do player > "Neste computador" | Lista as saídas de áudio; escolher outra muda o som na hora (fone, HDMI, caixa Bluetooth) | ☐ |
| 2.195 | Fechar com uma faixa a meio e abrir de novo | A fila volta com a mesma faixa, **parada**, e Tocar continua de onde estava | ☐ |
| 2.196 | Com música tocando, bloquear a tela e olhar o celular (Connect) / o modo Mídia | Mostram título, artista e capa; pausar, próxima e anterior de lá funcionam | ☐ |
| 2.197 | Teclas de mídia do teclado (play/pause, próxima) | Controlam o LUMNA Music | ☐ |
| 2.198 | Arrastar uma pasta de músicas e alguns arquivos do Atlas para a janela | A pasta entra na biblioteca; os arquivos vão para a fila | ☐ |
| 2.199 | Se o app cair: `ls ~/.cache/lumna/crash/` | Há um `lumna-music-<número>.log` com a pilha; mande-o para mim | ☐ |

### 2e. Galeria: o player de vídeo (fase 4)

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.200 | Abra a Galeria e toque num vídeo | Abre o player dentro da janela, o vídeo toca com som | ☐ |
| 2.201 | Toque no vídeo, aperte Espaço | Pausa e retoma (o botão grande do meio aparece pausado) | ☐ |
| 2.202 | Clique no meio da barra de tempo; use as setas ← e → | Pula para o ponto; as setas andam 5 s (Shift, 30 s) | ☐ |
| 2.203 | Mexa no volume, no mudo e nas setas ↑↓ | O volume muda e fica salvo na próxima vez | ☐ |
| 2.204 | Velocidade (0,5× a 2×) | O vídeo acelera e desacelera, o som acompanha | ☐ |
| 2.205 | Legendas e áudio: ponha um `.srt` com o nome do vídeo e abra o menu | A legenda aparece na lista e liga e desliga; um vídeo com dois áudios deixa trocar | ☐ |
| 2.206 | Tela cheia (`F` ou duplo toque); fique parado uns segundos | Fica em tela cheia e os controles somem; mexer o mouse os traz de volta; Esc sai | ☐ |
| 2.207 | `N` e `P`; os botões de anterior e próximo | Passam para os outros vídeos da lista que estava na tela | ☐ |
| 2.208 | Deixe o vídeo chegar ao fim; toque no botão do meio | Para no fim; tocar recomeça do começo | ☐ |
| 2.209 | Em Editar (ou `E`) | Abre o editor de vídeos com este vídeo | ☐ |
| 2.210 | Menu "…" > Abrir no mpv | O mpv abre numa janela separada | ☐ |
| 2.211 | Configurações > Reprodução > "Abrir os vídeos com" > mpv; toque num vídeo | Abre no mpv, não no player da Galeria; volte para "O player da Galeria" | ☐ |
| 2.212 | Toque num vídeo pesado (4K ou 60 fps) | Toca; se engasgar, diga o vídeo e o PC (é a renderização por software) | ☐ |
| 2.213 | Com o vídeo tocando, tela de bloqueio ou celular (controle de mídia) | Mostra o vídeo e os botões funcionam | ☐ |
| 2.214 | Se o app cair: `ls ~/.cache/lumna/crash/` | Há um `lumna-galeria-<número>.log` com a pilha; mande-o para mim | ☐ |

### 2f. Loja: apps (fase 1)

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.215 | Abra a Loja | Lê os repositórios (alguns segundos) e abre em Explorar com os cartões (Firefox, Blender…) e os ícones dos apps | ☐ |
| 2.216 | Toque numa aba (Multimídia, Jogos…) | Só os apps dessa categoria | ☐ |
| 2.217 | Digite `mesa` na busca (ou `Ctrl+K`) | Vai para Pacotes: "Resultados para “mesa”", o nome exato primeiro | ☐ |
| 2.218 | Mexa nos filtros (Tipo, Repositório, Categoria) e em Limpar | A lista acompanha; Limpar volta ao começo | ☐ |
| 2.219 | Toque numa linha | Abre o detalhe (versão, tamanhos, licença, site) | ☐ |
| 2.220 | Instale um app pequeno (ex.: `htop` se não tiver) | Pergunta; a janela da senha do LUMNA aparece; o painel mostra o andamento; no fim, "Instalado" | ☐ |
| 2.221 | Remova o que você instalou (Instalados > Remover) | Pergunta, remove, some da lista | ☐ |
| 2.222 | Busque algo do AUR (ex.: `visual-studio-code-bin`) | Aparece "aur"; Instalar avisa que não é oficial; com o paru/yay instalados compila e instala | ☐ |
| 2.223 | Sem paru nem yay: tente instalar do AUR | A Loja diz que precisa de um deles (não falha calada) | ☐ |
| 2.224 | Atualizações | Lista o que tem versão nova com a versão de agora e a nova; o selo na lateral confere | ☐ |
| 2.225 | Atualizar tudo | Atualiza o sistema (e o AUR, se houver paru/yay) | ☐ |
| 2.226 | Configurações: desligue o AUR; escolha o yay | A busca deixa de trazer do AUR; o "Em uso" muda | ☐ |
| 2.227 | Com o catálogo carregado, digite uma letra | Não trava (vários milhares de pacotes) | ☐ |
| 2.228 | Se o app cair: `ls ~/.cache/lumna/crash/` | Há um `lumna-loja-<número>.log`; mande-o para mim | ☐ |

### 2g. Loja: drivers e kernels (fase 2)

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.229 | Loja > Drivers e kernels | Mostra o hardware detectado (a sua placa de vídeo e x86_64); "Ver hardware" lista processador, memória, kernel em uso, UEFI e o gerenciador de boot | ☐ |
| 2.230 | Confira a lista de Drivers de vídeo | Só o que serve à sua placa aparece; o `mesa` aparece como Instalado | ☐ |
| 2.231 | Desligue "Mostrar só compatíveis" e abra "Ver detalhes" de um driver de outra marca | Aparece Incompatível com o motivo ("não tem placa NVIDIA") e o Instalar bloqueado | ☐ |
| 2.232 | Se a sua placa é NVIDIA: abra o `nvidia-open` e o `nvidia-dkms` | Dizem se servem à geração da sua placa; o dkms avisa que leva os headers | ☐ |
| 2.233 | Aba Kernels: abra o `linux` (em uso) | Diz que é o kernel em uso e não oferece Remover | ☐ |
| 2.234 | Instale o `linux-lts` | Pergunta; pede a senha; instala o kernel e os headers; o painel mostra o que foi conferido em `/boot` e no menu do boot | ☐ |
| 2.235 | Se o painel disser que o GRUB (ou systemd-boot) não tem o kernel: rode o comando que ele mostrou | O kernel aparece no menu do boot ao reiniciar; escolha o linux-lts, entre e volte ao linux | ☐ |
| 2.236 | Remova o `linux-lts` (estando no `linux`) | Remove o kernel e os headers; o `linux` em uso nunca tem Remover | ☐ |
| 2.237 | Configurações > Modo avançado | Liga (e grava); um item Incompatível passa a ter "Instalar mesmo assim", com a confirmação em vermelho | ☐ |
| 2.238 | **Não instale** nada incompatível de verdade. Só confira os motivos | Os motivos fazem sentido para o seu computador (se algo estiver errado, me diga o modelo da placa) | ☐ |

### 2h. Music: app padrão e instância única

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.239 | Painel > Aplicativos padrão: ponha o LUMNA Music para áudio; no Atlas, dê duplo clique numa música | O Music abre **e a música toca na hora** | ☐ |
| 2.240 | Com o Music aberto e tocando, dê duplo clique em outra música no Atlas | Não abre outra janela: a mesma janela passa a tocar a nova música e vem para a frente | ☐ |
| 2.241 | Dê duplo clique na mesma música várias vezes seguidas | Continua uma janela só e a música toca uma vez só (sem eco, sem duas por cima) | ☐ |
| 2.242 | Selecione 3 músicas no Atlas e abra todas | Entram as 3 na fila e a primeira toca | ☐ |
| 2.243 | Abra o Music pelo menu/lançador estando com ele aberto | Só traz a janela que já existe | ☐ |
| 2.244 | `ps aux | grep mpv` depois de fechar o Music | Nenhum mpv sobra | ☐ |

### 2i. Music em segundo plano, topbar e widget

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.245 | Toque uma música no Music e feche a janela (o X) | A janela some, a música **continua**; na topbar, à direita, aparece o ícone de música aceso | ☐ |
| 2.246 | Toque no ícone da topbar | Abre um painel com o título, o tempo e anterior, tocar/pausar e próxima; eles mandam na música | ☐ |
| 2.247 | Olhe a área de trabalho | O cartão do Music (capa, título, botões) aparece no canto de cima, **atrás** das janelas que você abrir | ☐ |
| 2.248 | Toque no cartão (fora dos botões) | O Music abre de volta e o cartão some | ☐ |
| 2.249 | No painel da topbar, "Fechar o LUMNA Music" | Encerra o app e a música; o ícone e o cartão somem | ☐ |
| 2.250 | Pause e feche a janela | Encerra o app (não fica um app invisível parado) | ☐ |
| 2.251 | Music > Configurações > Reprodução: desligue "Continuar tocando ao fechar"; toque e feche | Fechar encerra o app | ☐ |
| 2.252 | Clique no desktop onde fica o cartão (com ele escondido) | Nada é bloqueado (o widget não pega cliques fora do cartão) | ☐ |

### 2j. Pré-setup (primeiro login)

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.253 | Entre pela primeira vez num usuário novo | Depois de uns segundos abre "Bem-vindo ao LUMNA", etapa 1 de 5, com Uso diário marcado | ☐ |
| 2.254 | Escolha Gaming, 12 horas, outro idioma e **Continuar** | Vai para Rede e som; pode pedir a senha de administrador (idioma e fuso) sem travar a tela | ☐ |
| 2.255 | Ao terminar o assistente, reinicie a sessão | O sistema começa no modo do perfil (Gaming); a barra mostra a hora em 12 h | ☐ |
| 2.256 | Etapa 2: ligue o Wi-Fi, escolha sua rede, digite a senha | Conecta e aparece "Conectado, com internet" | ☐ |
| 2.257 | Etapa 2: troque a saída de som e toque "Testar som" | O som sai na saída escolhida | ☐ |
| 2.258 | Etapa 3: e-mail inválido, depois um certo; "Configuração manual" | O inválido não vale; o certo fica "guardado"; aparecem IMAP e SMTP | ☐ |
| 2.259 | Etapa 3: serviço de nuvem (com o rclone instalado), "Conectar conta" | Abre o navegador para entrar; depois "Conta conectada" e a pasta `~/Nuvem` existe | ☐ |
| 2.260 | Etapa 3: ligue "Sincronizar em segundo plano" e termine; reinicie a sessão | A nuvem aparece montada em `~/Nuvem` | ☐ |
| 2.261 | Etapa 4: desligue "Ativar Emma no sistema" e continue | O ícone da Emma some da barra; o chat (Super+A) e o app não abrem; `pgrep lumna-emma` não acha nada | ☐ |
| 2.262 | Reinicie a sessão com a Emma desligada | A Emma não sobe sozinha | ☐ |
| 2.263 | `lumna-setup --passo 4`, ligue a Emma de novo | O serviço sobe e o ícone volta à barra | ☐ |
| 2.264 | Etapa 4: provedor, URL, chave, modelo e "Testar conexão" | Diz "Conexão funcionando" (ou o erro do provedor); o provedor aparece em Painel > Emma > Provedores | ☐ |
| 2.265 | Etapa 5: "Usar código de pareamento" e digite o código no LUMNA Connect do celular | O código aparece com o endereço; ao parear vira "1 dispositivo pareado" e os recursos escolhidos ficam ligados no celular (Painel > Connect) | ☐ |
| 2.266 | "Começar a usar o LUMNA" | A janela fecha; no próximo login não abre mais | ☐ |
| 2.267 | Feche o assistente no meio (**Fechar**) e entre de novo | Não abre a janela: aparece um aviso "Termine de configurar o seu LUMNA" | ☐ |
| 2.268 | Ligue "Não mostrar de novo" e feche | Nem janela nem aviso nos próximos logins | ☐ |
| 2.269 | Lançador: "Configuração inicial" | Abre o assistente de novo, quando quiser | ☐ |

### 2k. Gaming: biblioteca de jogos

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.270 | Com jogos instalados na Steam, Super+Tab até o Gaming (ou `lumna-modo gaming`) | A biblioteca abre sozinha na tela do Gaming, no Início, com o último jogo em destaque | ☐ |
| 2.271 | Olhe "Seus jogos" | Até 6 jogos, os mais recentes primeiro, com a capa/fundo que a Steam guarda | ☐ |
| 2.272 | "Continuar" no destaque | Abre o jogo pela Steam (`steam://rungameid`); a "Última sessão" passa a ser "agora há pouco" | ☐ |
| 2.273 | "Ver biblioteca" | Todos os jogos da Steam (inclusive de outras pastas/discos), Lutris e Heroic, em ordem de nome | ☐ |
| 2.274 | Filtros Steam, Lutris, Heroic, Avulsos, Favoritos | Cada um mostra só os jogos dele | ☐ |
| 2.275 | Toque num jogo | Detalhes: "Sua jornada" (tempo de jogo, última sessão, tamanho) e o painel Instalado (versão, origem, disco, local) | ☐ |
| 2.276 | Compare o tempo de jogo e a última sessão com a Steam | Batem | ☐ |
| 2.277 | O coração nos Detalhes | Favorita; o jogo aparece no filtro Favoritos | ☐ |
| 2.278 | "Abrir pasta" | Abre a pasta do jogo no Atlas | ☐ |
| 2.279 | Jogo da Steam: "Verificar arquivos" | A Steam abre e verifica o jogo | ☐ |
| 2.280 | Biblioteca > "Adicionar jogo": nome e o caminho de um executável | O jogo aparece em Avulsos | ☐ |
| 2.281 | Detalhes do avulso > Propriedades: opções `gamemoderun %command%`, uma variável (`MANGOHUD=1`), Aplicar | Salva; "Continuar" abre com o gamemode e o MangoHud | ☐ |
| 2.282 | Propriedades > "Restaurar padrão" | Limpa opções, variáveis e GPU | ☐ |
| 2.283 | Propriedades de um jogo da Steam | Só leitura, com o aviso de que a Steam guarda as dela | ☐ |
| 2.284 | Desinstalar um jogo da Steam > confirmar | A Steam abre pedindo para desinstalar; o jogo continua na lista até ela terminar | ☐ |
| 2.285 | Desinstalar um avulso > confirmar (sem marcar nada) | Sai da biblioteca; os arquivos continuam | ☐ |
| 2.286 | Desinstalar um avulso marcando "Apagar também os arquivos" | Sai da biblioteca e a pasta dele some | ☐ |
| 2.287 | Desinstalar > Cancelar (ou Esc) | Nada muda | ☐ |
| 2.288 | Jogo do Lutris/Heroic: Desinstalar | Abre o Lutris/Heroic para você desinstalar lá | ☐ |
| 2.289 | Jogo do Lutris e do Heroic: "Continuar" | Abre o jogo (Heroic: se não abrir, me mande o `heroic --no-gui heroic://launch/...` do terminal) | ☐ |
| 2.290 | Teclado: setas, Enter e Esc na Biblioteca e nos Detalhes | Navega, abre e volta | ☐ |
| 2.291 | Abra um jogo com a Steam fechada, e feche-o | O jogo abre na tela do Gaming; ao fechar, volta a biblioteca | ☐ |

### 2l. Controle (gamepad) e Configurações do Gaming

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.292 | Ligue um controle por USB (Xbox, PlayStation ou genérico) e abra o LUMNA Jogos | O direcional e o analógico esquerdo movem o card; A abre os Detalhes; B volta | ☐ |
| 2.293 | Segure o direcional | Repete (anda sem parar), e para ao soltar | ☐ |
| 2.294 | LB e RB | Trocam de aba (Início ↔ Biblioteca ↔ Loja; na Loja, as subabas; nos Detalhes, Principal ↔ Propriedades) | ☐ |
| 2.295 | Abra um jogo e mexa no controle dentro dele | O jogo recebe o controle normalmente; o Gaming não reage | ☐ |
| 2.296 | Com o jogo aberto, aperte o botão Guia | Traz a biblioteca por cima (guia=biblioteca); o jogo continua rodando | ☐ |
| 2.297 | Configurações > Controles | O controle aparece com o nome, USB/Bluetooth e a bateria (se ele informa) | ☐ |
| 2.298 | "Testar vibração" | O controle vibra | ☐ |
| 2.299 | "Renomear": dê um apelido | O apelido aparece na lista e continua depois de reiniciar o serviço | ☐ |
| 2.300 | "Ignorar" | O controle para de mexer na biblioteca (o jogo ainda o lê); "Voltar a usar" desfaz | ☐ |
| 2.301 | Desconecte o controle e conecte de novo | Some e volta da lista em até 2 s, sem reiniciar nada | ☐ |
| 2.302 | Controles > "Procurar controles" com um controle Bluetooth em modo de pareamento | Ele aparece; "Parear" e depois "Conectar" | ☐ |
| 2.303 | Controle pareado > "Remover" (duas vezes) | O controle é esquecido pelo Bluetooth | ☐ |
| 2.304 | Ajustes: mexa na zona morta com um analógico gasto | O analógico deixa de "andar sozinho" | ☐ |
| 2.305 | Ajustes: "Trocar A e B" | O A passa a voltar e o B a confirmar | ☐ |
| 2.306 | Ajustes: desligue a vibração e "Testar vibração" | Recusa (a vibração está desligada) | ☐ |
| 2.307 | Botão Guia: escolha "Chamar a Emma" e aperte o Guia | O chat da Emma abre e fecha por cima | ☐ |
| 2.308 | Gaming: perfil "Desempenho"; entre e saia do Gaming (`powerprofilesctl get`) | Entra em performance e volta ao que estava ao sair | ☐ |
| 2.309 | Gaming: "Mostrar o FPS" com o MangoHud instalado; abra um jogo avulso | O FPS aparece por cima do jogo | ☐ |
| 2.310 | Gaming: "Não perturbar" | Em Gaming, só os avisos urgentes passam | ☐ |

### 2m. Fundo das telas do Gaming

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.311 | Início: ande pelos cards de "Seus jogos" | O nome, a origem e o fundo trocam para o do jogo selecionado | ☐ |
| 2.312 | Olhe a barra de cima | Escura em cima e transparente a partir do meio, deixando a arte aparecer | ☐ |
| 2.313 | Olhe a barra de baixo (dicas dos botões) | De uma cor só, sem arte por trás | ☐ |
| 2.314 | Biblioteca | Um papel de parede (a arte do jogo mais recente) escurecido atrás da grade | ☐ |
| 2.315 | Configurações > Gaming > Papel de parede: "Uma imagem minha" com um caminho; depois "Nenhum" | A Biblioteca usa a sua imagem (em até 5 s); com "Nenhum" fica só o fundo escuro | ☐ |

### 2n. Loja do Gaming (Steam)

| # | Faça | Esperado | OK |
|---|---|---|---|
| 2.316 | Gaming > Loja (com internet) | Destaques com os mais vendidos da Steam, em reais, com capas; o jogo selecionado dá o nome e o fundo | ☐ |
| 2.317 | Chips Ação, Aventura, RPG, Corrida, Estratégia, Indie | A fila de jogos muda para o gênero | ☐ |
| 2.318 | Subabas Categorias, Ofertas e Lista de desejos | Ofertas mostra o desconto e o preço; Categorias leva à busca do gênero | ☐ |
| 2.319 | Buscar: um nome, depois "Linux nativo", "Só grátis", "Cooperativo" e a ordem | Os resultados acompanham; "Linux nativo" só traz jogos com versão para Linux | ☐ |
| 2.320 | Abra um jogo | Visão geral (prints e trailer), Sobre e Requisitos; "Via Proton" mostra o nível do ProtonDB | ☐ |
| 2.321 | "Ver o trailer" | O mpv abre e toca | ☐ |
| 2.322 | Em um jogo que você não tem: "Lista de desejos" | Entra na lista local (subaba Lista de desejos); tocar de novo tira | ☐ |
| 2.323 | "Comprar" | A loja da Steam abre naquele jogo (a única janela da Steam que aparece) | ☐ |
| 2.324 | Configurações > Steam: cole a chave de API (e o id, se pedir) | "Chave guardada"; os jogos da conta passam a mostrar "Na biblioteca" | ☐ |
| 2.325 | Num jogo da sua conta: "Instalar" | A janela mostra as bibliotecas da Steam com o espaço livre e o espaço necessário | ☐ |
| 2.326 | Confirme "Instalar" **com a Steam fechada** | A Steam sobe **sem abrir janela** e o download começa; a barra de progresso anda na página do jogo | ☐ |
| 2.327 | Troque o local por outra biblioteca (outro disco) e instale | O jogo baixa na pasta escolhida (se não, me diga o que a Steam fez) | ☐ |
| 2.328 | O ícone de download da barra | Mostra o número de downloads em curso | ☐ |
| 2.329 | Quando termina | "Instalado", com "Ver na biblioteca"; o jogo aparece na Biblioteca | ☐ |
| 2.330 | "Desinstalar" | A Steam desinstala (e o jogo some da Biblioteca) | ☐ |
| 2.331 | Um jogo grátis (ex. um free-to-play) | Mostra "Grátis" e "Instalar" (sem Comprar) | ☐ |
| 2.332 | Desligue a internet e abra a Loja | Mostra a mensagem e "Tentar de novo"; o que já foi visto continua (cache) | ☐ |

## 3. Instalar (na VM, com `--disco`)

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 3.1 | Clicar em **Instalar LUMNA OS** (dock) | Abre o instalador | ☐ |
| 3.2 | Boas-vindas > Instalar > Região > Continuar | Passa os passos | ☐ |
| 3.3 | Disco | Lista o disco virtual de 64 GB (e **não** o pendrive/ISO); escolher mostra o aviso vermelho | ☐ |
| 3.4 | Sem marcar "Entendi" | O Continuar fica desligado | ☐ |
| 3.5 | Marcar "Entendi" > Continuar | Vai para "Você" | ☐ |
| 3.6 | Seu nome, usuário (sugerido), senha, repetir a senha | Valida; o Continuar liga só quando está certo | ☐ |
| 3.7 | Resumo > **Instalar agora** | A instalação começa (no live a senha de administrador não é pedida para instalar) | ☐ |
| 3.8 | Esperar | A barra anda com texto ("Preparando o disco", "Copiando o sistema"...) até 100% | ☐ |
| 3.9 | **Reiniciar agora** | Reinicia (tire o ISO da VM: `Ctrl+Alt+G`, menu da janela do QEMU) | ☐ |

Se a instalação **parar com erro**, a mensagem aparece na tela: me mande o texto. O instalador só instala **em UEFI** e no
**disco inteiro** (não faz partição manual ainda).

## 3b. Instalar em BIOS (modo legado), como num notebook antigo

O instalador segue o modo em que o computador iniciou o pendrive e usa o **GRUB nos dois** (o `/boot` fica sempre dentro do Btrfs, então o
"Desfazer a última atualização" leva o kernel junto). **UEFI:** partição EFI de 1 GiB em `/boot/efi`. **BIOS:** partição "BIOS boot" de
1 MiB. Na VM: `scripts/run-vm.sh --bios --disco lumna-os-teste.iso` (BIOS) ou sem o `--bios` (UEFI).

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 3b.1 | Dar boot na ISO em modo BIOS (VM com `--bios`, ou o notebook em "Legacy"/CSM) | Aparece o menu do syslinux e o live sobe | ☐ |
| 3b.2 | Instalar (3.1 a 3.8) | No Resumo aparece "Boot: BIOS (modo legado), com o GRUB"; a instalação chega a 100% | ☐ |
| 3b.3 | Reiniciar sem o pendrive | Aparece o menu do GRUB ("LUMNA OS") e o sistema sobe até o login | ☐ |
| 3b.3b | Repita a instalação em **UEFI** (VM sem `--bios`) | Resumo: "Boot: UEFI, com o GRUB"; depois do reboot aparece o menu do GRUB e o sistema sobe | ☐ |
| 3b.3c | No sistema instalado: `ls /boot` | Mostra `vmlinuz-linux`, `initramfs-linux.img` e `grub/` (o kernel está no Btrfs, não na partição EFI) | ☐ |
| 3b.4 | Em notebooks: se não der boot, tente no menu de boot da BIOS escolher o disco (e não "UEFI: ...") | Entra no GRUB | ☐ |

A instalação em BIOS **nunca rodou num disco de verdade** (só os testes de conferência): é a primeira vez.

## 4. O sistema instalado

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 4.1 | Boot do disco | Aparece o systemd-boot e sobe | ☐ |
| 4.2 | Tela de login | A tela de login do LUMNA (a Gravity); seu usuário já vem escolhido | ☐ |
| 4.3 | Entrar com a sua senha | Entra no Orbit (sessão "LUMNA · Orbit", a primeira da lista) | ☐ |
| 4.4 | Olhar a dock | **Sem** o "Instalar LUMNA OS"; com Início, Arquivos, Navegador, Painel, Emma | ☐ |
| 4.5 | Painel > Sistema > Usuários | Mostra a sua conta | ☐ |
| 4.6 | Painel > Idioma > Aplicar | Janela de senha do administrador; a **sua** senha aplica | ☐ |
| 4.7 | `sudo pacman -Sy` num terminal | Funciona (chaves do pacman prontas) | ☐ |
| 4.8 | Painel > Aplicativos > Atualizações | Lista atualizações sem erro de assinatura | ☐ |
| 4.9 | Reiniciar pelo menu de energia | Reinicia e volta ao login | ☐ |

## 4b. Atualizar o sistema instalado (pelo GitHub)

O repositório de atualizações é a release `repo-orbit`: **todo push na branch `iso-orbit` compila e publica os pacotes novos
lá** (uns minutos). O sistema instalado já aponta para ela.

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 4b.1 | `grep -A3 '\[lumna\]' /etc/pacman.conf` | Mostra `Server = https://github.com/dekilled/LUMNA_OS/releases/download/repo-orbit` (e não um `file://`) | ☐ |
| 4b.2 | Painel > Sobre > Atualizações do sistema > **Procurar** | Procura sem erro; se eu publiquei algo depois da ISO, aparecem os `lumna-*` | ☐ |
| 4b.3 | **Atualizar tudo** | Abre um terminal, pede a senha de administrador (a janela do LUMNA), mostra "Snapshot antes-… guardado" e atualiza | ☐ |
| 4b.4 | Sair da sessão e entrar de novo | Entra na versão nova | ☐ |
| 4b.5 | Painel > Sobre > Atualizações: **Desfazer a última atualização** (aparece depois do 4b.3) | Confirma, pede a senha e diz para reiniciar; depois do reboot o sistema está como antes da atualização | ☐ |
| 4b.6 | `lumna-pkg snapshots` no terminal | Lista os snapshots (ficam os 5 últimos) | ☐ |

O snapshot e o "voltar" usam o Btrfs de verdade; eu só testei com um `btrfs` de mentira, então o 4b.5 é o mais arriscado.

## 5. Só no PC de verdade (a VM não mostra)

| # | O que fazer | Deve acontecer | |
|---|---|---|---|
| 5.1 | Vários monitores: Painel > Telas e modos > Monitores | Lista todos; ordem e escala funcionam | ☐ |
| 5.2 | `Super+Tab` com 2+ monitores (Desktop → Mídia) | Só a tela pedida vira Mídia; a outra mantém o Desktop | ☐ |
| 5.3 | Arrastar uma janela entre monitores | Passa de um para o outro | ☐ |
| 5.4 | Painel > Luz noturna: ligar "Sempre ligada" | **A tela esquenta** (é a gama de verdade; no headless nunca deu pra ver) | ☐ |
| 5.5 | Painel > Energia > Tela e suspensão: apagar a tela em 1 minuto | A tela apaga (DPMS) e acende ao mexer | ☐ |
| 5.6 | Suspender pelo menu de energia | Suspende e volta | ☐ |
| 5.7 | Botão físico de ligar | Abre a pergunta de energia (padrão) | ☐ |
| 5.8 | Notebook: bateria na barra; Painel > Energia > Bateria | Mostra a carga; o perfil automático troca ao tirar da tomada | ☐ |
| 5.9 | Reproduzir um vídeo no Firefox e esperar o bloqueio | Não bloqueia com o vídeo tocando | ☐ |
| 5.10 | Bluetooth: Painel > Bluetooth, parear um fone | Pareia e o som sai nele | ☐ |
| 5.11 | Teclas de volume e brilho (notebook) | Mexem o volume e o brilho | ☐ |
| 5.12 | Placa AMD ou Intel: usar o desktop 10 minutos | Sem travar, sem piscar | ☐ |

## 6. O que me mandar quando falhar

1. **O número do item** e o que aconteceu (e o que você esperava).
2. Uma foto da tela se for visual.
3. Os logs (num terminal, `Alt+Enter`, ou `Ctrl+Alt+F2`):
   ```
   journalctl -b --no-pager > ~/log-boot.txt
   ls ~/.local/state/lumna/
   cat ~/.local/state/lumna/emma.log
   ```
   No desktop, **Painel > Sistema > Diagnóstico** junta os relatórios que existirem.
4. Para a janela de senha: `journalctl -b | grep lumna-polkit`.
5. Para o instalador: a mensagem de erro que aparece na tela.

## O que **não** está pronto (não é bug)

Loja própria (por ora o Painel > Aplicativos); o Atlas sem mover ao arrastar, sem arrastar com o dedo e sem discos e USB, Music, Biblioteca Gaming,
particionamento manual, VPN, Ponto de acesso, Receber arquivos por Bluetooth, a UI do celular. Lista completa em [AUDITORIA-UI.md](AUDITORIA-UI.md).
