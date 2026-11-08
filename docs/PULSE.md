# Pulse Terminal

O terminal do LUMNA (`lumna-pulse`), em Gravity, com a Emma por dentro. Aqui fica o desenho e o que já existe; o plano de entregas vem por último.

## As peças

| Peça | Onde | O que faz |
|---|---|---|
| Terminal | `gravity/servicos/terminal.{c,h}` | libvterm + pty: grade, cores, histórico (5000 linhas), teclas, colar, mouse, **eventos do shell** |
| Janela | `gravity/exemplos/pulse.c` | abas, seleção, rolagem, zoom, desenho da grade; liga o resto |
| Integração com o bash | `gravity/pulse-shell/integracao.bash` | marcas do prompt (OSC 133) e o aviso de cada comando (OSC 7770), carregada pelo Pulse no primeiro prompt |
| Memória | `gravity/servicos/pulsedb.{c,h}` | SQLite só da pessoa (`~/.local/share/lumna/pulse/pulse.db`, 600), migrações versionadas, segredos trocados por `***` |
| Emma no terminal | `gravity/servicos/emma_terminal.{c,h}` | monta o contexto, pergunta à Emma, devolve a resposta; isolado do resto |
| Pergunta avulsa | `lumna-emma`, comando `pergunta` | uma resposta só texto, **sem ferramentas** (a Emma não age), com prazo para começar e para acabar |

## Como o Pulse sabe o que rodou

Um terminal só vê bytes. Para saber o comando, o código de saída e o que ele escreveu, o bash manda marcas junto da saída:
`OSC 133;A` (o prompt vai aparecer), `B` (aqui se digita), `C` (o comando começou), `D;N` (acabou com o código N), e `OSC 7770` com o comando e a pasta
(base64). O Pulse liga isso sozinho: põe um `PROMPT_COMMAND` no ambiente do shell que carrega `integracao.bash` no primeiro prompt, sem mexer no `~/.bashrc`.

- **Saída, não stderr:** o pty mistura stdout e stderr. O que vai à Emma e ao banco é o fim da saída do comando (40 linhas), onde estão os erros.
- **Comando com espaço na frente** não entra no histórico do shell, então o Pulse nem o vê (não registra, não manda à Emma).
- **Pré-execução** (explicar antes de rodar): o Pulse lê a linha digitada pela grade (da marca `B` ao cursor) e segura o Enter. (Entrega d.)

## O banco

`sessions(id, started_at, ended_at, cwd_inicial)`, `commands(id, session_id, timestamp, cwd, command, exit_code, saida_snippet)`,
`emma_interactions(id, session_id, command_id, tipo, prompt_enviado, resposta, timestamp)`.
Migrações em `pulsedb.c` (`PRAGMA user_version`); `lumna-pulse --esquema` mostra o SQL, `--migrar-banco` cria/atualiza, `--limpar-historico` apaga tudo.
Dados com mais de 365 dias saem sozinhos.

## Privacidade e segurança

- `***` no lugar de `--password=`, `Authorization: Bearer`, `VAR_TOKEN=`, `https://usuario:senha@`, chaves conhecidas, **antes** de gravar e de enviar.
- A saída dos comandos vai à Emma como **dado** (marcas `<saida_do_comando>`), com aviso de que não é instrução; a marca de fechamento dentro da saída é neutralizada.
- A listagem da pasta só vai se ela está na home, sem nomes de segredos (`.ssh`, `.env`, `id_rsa`, `*.pem`...). Fora da home, só o caminho.
- A Emma **só sugere**: o `pergunta` não tem ferramentas, e nada executa sem a confirmação da pessoa.
- Nada trava: o pedido é assíncrono, com prazo (3 s para começar a responder, 15 s no total, em `~/.config/lumna/pulse.ini`); estourou, volta vazio e o Pulse não mostra nada.

## Configuração (`~/.config/lumna/pulse.ini`)

```ini
[pulse]
sessoes_contexto=5   ; quantas sessões anteriores a Emma recebe por padrão
espera_inicio=3      ; segundos para a resposta começar
teto=15              ; segundos para a resposta acabar
emma=sim             ; nao desliga a Emma no terminal
```

## Entregas

- (a) **feita:** o renome, a integração com o bash, o banco com migração, o comando `pergunta` e o módulo `emma_terminal`; cada comando que acaba vai para a memória.
- (b) **feita:** correção de erro com a Emma. Todo comando que acaba com código diferente de 0 (menos Ctrl+C/Ctrl+Z e o código 1 de `grep`, `diff`, `test`...) vai à Emma com o
  comando, o código e o fim da saída; a sugestão aparece num cartão lilás logo abaixo do prompt. **Enter** executa (só depois de 0,6 s: um Enter por hábito logo depois do erro
  não roda nada, só segue para o shell), **Tab** põe no prompt para editar, **Esc** descarta, qualquer outra tecla some com o cartão. Um comando destrutivo (a lista do (d))
  nunca roda com o Enter do cartão: só entra no prompt. A decisão fica em `emma_interactions.decisao` (aplicada, inserida, descartada, ignorada).
- (c) comando por linguagem natural (`Ctrl+E`)
- (d) explicação antes de executar (lista configurável)
