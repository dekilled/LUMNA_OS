# Gaming: a biblioteca de jogos (lumna-jogos)

Esboços: Início, Detalhes (Principal) e Detalhes (Propriedades). Código: `gravity/exemplos/jogos.c` (a janela), `gravity/servicos/jogos.[ch]` (a biblioteca).
O app_id `lumna-jogos` faz o Orbit abrir a janela na tela que está em Gaming. `lumna-jogos vigia` (iniciado pela sessão) abre a biblioteca quando uma tela entra em Gaming.

## De onde vêm os jogos

| Origem | O que se lê | Abrir | Desinstalar |
|---|---|---|---|
| **Steam** | `libraryfolders.vdf` (todas as bibliotecas) e `appmanifest_*.acf` (só instalados por inteiro; Proton e runtimes ficam de fora); horas e última sessão do `localconfig.vdf`; capas e fundos do `appcache/librarycache` | `steam steam://rungameid/ID` | `steam steam://uninstall/ID` (a Steam cuida dos arquivos) |
| **Lutris** | `~/.local/share/lutris/pga.db` (instalados) | `lutris lutris:rungame/SLUG` | abre o Lutris |
| **Heroic** | Epic (`legendaryConfig/legendary/installed.json`) e GOG (`gog_store/installed.json`) | `heroic --no-gui heroic://launch/RUNNER/APP` | abre o Heroic |
| **Avulsos** | `~/.config/lumna/jogos/avulsos.json`, adicionados na Biblioteca | o próprio executável (com Wine/Proton se for `.exe`) | tira da lista; com a caixa marcada, apaga a pasta (nunca a casa) |

O que é nosso fica em `~/.config/lumna/jogos/props.json`: favorito, última sessão e as propriedades de inicialização.

## Telas

- **Início**: o último jogo que você abriu em destaque (Continuar, Sobre o jogo) e "Seus jogos".
- **Biblioteca**: capas em grade, filtros por origem e Favoritos, "Adicionar jogo".
- **Detalhes › Principal**: "Sua jornada" (tempo de jogo, última sessão, tamanho em disco), versão, origem, compatibilidade, armazenamento do disco, local, Abrir pasta, Gerenciar, Verificar arquivos (Steam), Desinstalar.
- **Detalhes › Propriedades**: executável, opções de inicialização (`%command%`), diretório de trabalho, variáveis de ambiente, camada de compatibilidade, modo de janela, GPU dedicada. Aplicar, Cancelar e Restaurar padrão.
  Valem para os **avulsos e o Lutris**. A **Steam e o Heroic guardam as próprias** propriedades: a tela mostra o aviso e fica só para leitura.
- **Desinstalar**: sempre pede confirmação e diz o que vai acontecer em cada origem.

Teclado: setas, Enter (abre), Esc (volta), Page Up e Page Down (abas), Home (Início). O controle faz as mesmas teclas (veja abaixo).

## O que ainda não existe

- O indicador **"Conectado"** do topo do esboço (a bolinha do controle) e os atalhos **Guia + botão** (captura etc.).
- Um **mapa de botões livre** (qualquer botão para qualquer função): hoje só se troca A e B.
- A **Loja** (a aba existe, apagada), as abas **Sobre** e **Instalação**, **Conquistas** e a busca do topo.
- Gêneros e descrições dos jogos (não há fonte local; vão com a Loja).
- O **Heroic** e o **Lutris** foram escritos pela documentação deles, sem os programas de verdade aqui: se algo não abrir, o roteiro 2.289 pede o comando.

## Testes

`sh gravity/testes/jogos.sh [PASTA_DO_ORBIT]`: o serviço (ASAN/UBSAN) com uma Steam, um Lutris e um Heroic de mentira (duas bibliotecas da Steam, capas, horas, nomes com aspas), e a janela inteira num Orbit sem tela, até o vigia abrir a biblioteca ao entrar no Gaming.

## O fundo das telas

- **Início:** o jogo selecionado em "Seus jogos" manda na tela: o nome, a origem e a última sessão no centro e a **arte dele como fundo** (troca ao andar pelos cards). A esquerda e a parte de baixo escurecem para o texto e a fila se lerem. A barra de cima começa escura e, **a partir do meio, fica transparente**; a barra de baixo (dicas dos botões) é de **uma cor só**.
- **Biblioteca:** um **papel de parede** por trás da grade, escurecido. Em Configurações > Gaming: *Automático* (a arte do jogo em destaque), *Uma imagem minha* (um arquivo) ou *Nenhum*. Guardado em `~/.config/lumna/jogos/ui.ini`.
- **Ainda não:** papel de parede em **vídeo** (fica para uma decisão sua: ele pede o mpv por baixo, como o player de vídeo).

## O controle (gamepad)

`lumna-gamepad` (`gravity/exemplos/gamepad.c`, `servicos/gamepad.[ch]`) é um serviço que sobe com a sessão e lê os controles direto do kernel (`/dev/input/event*`, USB e Bluetooth; o acesso vem do `uaccess` do systemd).
Ele **não captura** o controle (sem `EVIOCGRAB`): o jogo continua lendo o dele.

- **Navegação:** com o LUMNA Jogos em foco, o direcional e o analógico esquerdo viram as setas (e repetem enquanto seguram), A é Enter, B é Esc, LB e RB são Page Up e Page Down (trocam de aba) e Home leva ao Início. Isso é feito pelo `wtype`. **Com um jogo em foco o serviço não manda tecla nenhuma.**
- **Botão Guia:** em qualquer janela. Abre a biblioteca, vai ao Início, chama a Emma (se estiver ligada) ou não faz nada, conforme a configuração.
- **Configurações** (a engrenagem do topo, ou RB a partir da Biblioteca):
  - *Controles:* a lista (nome, USB/Bluetooth, bateria quando o controle informa), testar vibração, renomear (apelido), ignorar e voltar a usar; e **Adicionar um controle (Bluetooth)**: procurar, parear, conectar e remover (pelo BlueZ).
  - *Ajustes do controle:* zona morta dos analógicos, vibração, trocar A e B (estilo Nintendo) e o mapa dos botões na navegação.
  - *Botão Guia:* o que ele faz.
  - *Gaming:* perfil de energia ao entrar no modo (volta ao que estava ao sair; precisa do power-profiles-daemon), não perturbar do modo Gaming e o FPS (MangoHud; vale para os jogos **avulsos** que o Gaming abre, na Steam use `mangohud %command%`).
- Guardado em `~/.config/lumna/gamepad.ini` (`[geral]`, `[gaming]`, `[apelidos]`, `[ignorados]`).

### Testes do controle

`sh gravity/testes/gamepad.sh [PASTA_DO_ORBIT]`: a leitura (ASAN/UBSAN) e o serviço de ponta a ponta com controles de mentira (FIFOs com `struct input_event`), e, com o Orbit, o caminho inteiro: controle de mentira → `lumna-gamepad` → o `wtype` de verdade → Orbit → LUMNA Jogos, navegando só pelo controle e mexendo nas Configurações.
**Não foi testado com controle de verdade** (aqui não há `/dev/input`): os códigos dos botões e dos eixos seguem a API de joystick do Linux; a vibração, a bateria e o Bluetooth precisam do seu teste.
