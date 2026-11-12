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

## O que falta

1. **Componente de edição** (o Gravity só tem campo de uma linha): desenho das linhas com Pango, cursor, seleção, rolagem vertical e horizontal, número de linhas, quebra de linha opcional, clique, duplo e triplo clique, arrastar para selecionar, copiar/colar/recortar, teclado completo (setas, Home/End, PgUp/PgDn, Ctrl+setas, Tab com recuo), entrada por método de entrada e acentos.
2. **O app:** abas (e restaurar as abertas), barra de título e menu, buscar e substituir (Ctrl+F, Ctrl+H), ir para a linha, salvar/salvar como/abrir pelo seletor do Atlas, aviso de "mudou no disco" e de "tem alterações" ao fechar, barra de estado (linha, coluna, codificação, fim de linha), arquivo passado na linha de comando e por `%U`.
3. **Integração:** `.desktop` com os tipos de texto, padrão em `mimeapps.list`, pacote, `Abrir com…` no Atlas.
4. **Testes** num Orbit sem tela.

Fica para depois: cor de sintaxe, recuo automático, várias seleções, UTF-16.
