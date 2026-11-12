# Signals, o editor de texto do LUMNA

O editor padrão do sistema, e também o "bloco de notas" (decidido: um app só). App em Gravity: `lumna-signals`, `app_id = lumna-signals`.

## Decisões (do Eric)

- **Um app só:** o Signals é o editor e o bloco de notas. A calculadora segue separada.
- **Primeira versão:** texto, abas, buscar e substituir. Sem cor de sintaxe por enquanto (fica para uma segunda fase).
- **Padrão do sistema:** vira o aplicativo padrão para texto e código (duplo clique no Atlas, "Abrir com…", seletor).
- **Interface:** espera o esboço do Eric. O núcleo, que não depende dele, já está pronto e testado.

## O que já existe: o núcleo (`servicos/sgbuffer.c`)

O documento, sem tela nenhuma (UTF-8; posição = linha + byte na linha, sempre na fronteira de um caractere):

- **Texto:** lista de linhas (um texto que termina em `\n` tem a última linha vazia, então o que se abre é exatamente o que se salva).
- **Desfazer e refazer:** grupos (digitar uma palavra desfaz de uma vez; Enter separa; Backspace repetido junta), cursor certo depois de cada um, limite de 50 mil operações, e "modificado" que volta a falso ao desfazer até o ponto salvo.
- **Busca:** literal, com ou sem diferença de maiúsculas (inclusive acentuadas), palavra inteira, para frente e para trás, voltando ao outro lado do documento (e avisando), contar, substituir tudo desfeito de uma vez (a troca pode ter quebra de linha).
- **Movimento:** caractere, palavra (Ctrl+setas), palavra sob o ponto (duplo clique).
- **Abrir:** UTF-8, com ou sem BOM; Latin-1 quando não é UTF-8 válido (e volta a Latin-1 ao salvar, ou recusa se o texto tem algo que não cabe); CRLF ou LF (lembra e devolve; avisa se o arquivo era misto); recusa binário, UTF-16, pasta e arquivo com mais de 64 MB, sempre com o motivo.
- **Salvar:** num temporário na mesma pasta e `rename` (um corte no meio não estraga o original), mantém as permissões (um script continua executável), segue link simbólico sem trocá-lo por arquivo, grava direto quando a pasta não deixa criar arquivos, e detecta que o arquivo mudou no disco (outro programa, outra janela).
- **Teste:** `sh gravity/testes/signals.sh` (ASAN/UBSAN): 60 sequências aleatórias de 400 operações comparadas com um modelo simples, os casos de arquivo, o desfazer agrupado, as posições e a busca.

## O app (`exemplos/signals.c`, pacote `lumna-signals`)

- **Janela** como no esboço: barra de título, menus Arquivo/Editar/Exibir (com os atalhos), busca, salvar e ajustes à direita, abas (ponto de "modificado" e ×), barra de busca (Aa, palavra inteira, "N de M", ↑ ↓) com a linha de substituir no Ctrl+H, e a barra de estado (linha/coluna, codificação, LF/CRLF, tipo, zoom).
- **Editor:** linhas desenhadas com Pango, cursor, seleção, clique/duplo/triplo, arrastar, rolagem nos dois eixos, números de linha, destaque da linha atual, recuo automático, Tab/Shift+Tab, Ctrl+A/C/X/V/Z/Y, palavra a palavra, Home inteligente, zoom (Ctrl +/−/0 e Ctrl+roda).
- **Arquivos:** abrir/salvar pelo seletor do portal (que é o Atlas), salvar atômico, aviso ao fechar aba/janela com alterações, aviso (ou recarga calada) quando o arquivo muda no disco, arquivos pela linha de comando e `%U`.
- **Integração:** `lumna-signals.desktop` com os tipos de texto e código, padrão para eles em `session/mimeapps.list`.
- **Preferências:** `~/.config/lumna/signals.ini` (zoom, fonte monoespaçada, números de linha, destaque da linha).

## Testes

- `sh testes/signals.sh`: o núcleo com ASAN/UBSAN (sequências aleatórias contra um modelo, arquivos, desfazer, busca).
- `sh testes/signals_app.sh PASTA_DO_ORBIT`: o app num Orbit sem tela (digitar, desfazer/refazer, selecionar/copiar/colar, salvar, abas, diálogos de fechar, salvar sem nome pelo seletor de mentira, buscar, substituir tudo, ir para a linha, mudança no disco, saída, zoom). Quebrar o desfazer faz o teste falhar.
- Os erros que o teste achou e foram corrigidos: Ctrl+N não ia para a aba nova; fechar a aba derrubava o app (desenho com o documento já liberado); recarregar do disco corrompia o nome da aba (caminho liberado em uso).

## O que ainda falta (e o que não foi provado)

- Só testado num Orbit sem tela, com o seletor de mentira. **Não testei** o seletor de verdade (portal + Atlas), a digitação de acentos/dead keys e método de entrada, o toque, nem ver o Atlas abrindo texto com duplo clique no Signals.
- Sem quebra de linha automática (rola na horizontal), sem cor de sintaxe, sem restaurar abas da sessão anterior, sem uma instância só (cada abertura é uma janela), sem seleção por toque, sem UTF-16, sem várias seleções.
- O zoom com Ctrl+roda também rola o texto.
