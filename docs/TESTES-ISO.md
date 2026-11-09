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
