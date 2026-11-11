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

Teclado: setas, Enter (abre), Esc (volta), Tab (abas).

## O que ainda não existe

- **Controle (gamepad)**: o Gravity ainda não lê o controle; os atalhos A/B/LB/RB do esboço e o "Conectado" do topo ficam para quando o Orbit tiver entrada de controle. Hoje: teclado, mouse e toque.
- A **Loja** (a aba existe, apagada), as abas **Sobre** e **Instalação**, **Conquistas** e a busca do topo.
- Gêneros e descrições dos jogos (não há fonte local; vão com a Loja).
- O **Heroic** e o **Lutris** foram escritos pela documentação deles, sem os programas de verdade aqui: se algo não abrir, o roteiro 2.289 pede o comando.

## Testes

`sh gravity/testes/jogos.sh [PASTA_DO_ORBIT]`: o serviço (ASAN/UBSAN) com uma Steam, um Lutris e um Heroic de mentira (duas bibliotecas da Steam, capas, horas, nomes com aspas), e a janela inteira num Orbit sem tela, até o vigia abrir a biblioteca ao entrar no Gaming.
