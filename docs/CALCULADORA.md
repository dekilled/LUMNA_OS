# Calculadora (a calculadora-fita)

A conta se **escreve como texto, em português**, e o resultado aparece enquanto se digita. Cada conta confirmada (Enter) vai para a fita. Não é o visor com teclado de sempre:
o teclado do desenho original (dígitos e operações) é um painel que se mostra ou esconde (botão ⠿ na barra), para o toque.

## Decisões (do Eric)

- Direção: **fita + teclado opcional** (e não o desenho clássico com visor).
- v1: **contas e porcentagens em português** e **conversão de unidades**. Ficaram de fora, por ora: variáveis (`total = 129,80`), modo científico/programador, moeda.

## O que entende (`servicos/calc.c`, sem tela)

```
128 × 3      250 ÷ 5      2^10      (2 + 3) × 4      2(3 + 4)      7 mod 3      raiz(81)   √81   pi
15% de 89,90         200 + 10%  (=220)      200 - 10%  (=180)      200 × 10%  (=20)      50% + 25%  (=75%)
89,90 + 39,90 (=129,80)       1.234,56 × 2       vírgula decimal, ponto de milhar (e "1.5" também vale como decimal)
5 km em milhas       1 galão em litros       2 GB em MB       90 km/h em m/s       100 °C em °F       -40 °C em °F
3h20 × 4 (= 13 h 20 min)      3 horas e 20 min      5 km e 300 m      90 min em h
```

- Porcentagem: `N% de X` é N/100 de X; `X + N%` e `X − N%` são relativos a X; `X × N%` é N/100 de X; `N%` sozinho continua `N%`.
- Unidades: comprimento, massa, tempo, volume, temperatura, velocidade, dados (GB = 1000 MB, GiB = 1024 MiB). Soma e subtração pedem a mesma grandeza; `em`/`para`/`→` converte.
  Temperatura só converte (não soma). Não multiplica duas grandezas com unidade.
- Casas: até 10 casas decimais, sem zeros sobrando, **mas com duas ou mais casas digitadas os zeros ficam** (`89,90 + 39,90 = 129,80`, como no desenho).
- Conta incompleta (`128 ×`, `(3 + 4`) **não é erro**: só espera. Erro de verdade mostra o motivo ("não dá para somar km e kg", "divisão por zero", "não converte km em kg").

## O app (`exemplos/calculadora.c`)

- A entrada é grande, o resultado vai embaixo à direita em lilás, ao vivo.
- **Enter** confirma. Uma conta que começa por operador **continua do resultado de cima**: depois de `384`, digitar `+ 16` é `384 + 16`. (`-5 + 2` é um número negativo, não uma continuação; `- 5` com espaço continua.)
- A **fita**: clicar na conta traz de volta para editar; clicar no resultado insere no que se escreve; o botão de copiar (aparece ao passar o mouse) copia o resultado.
- **Esc** limpa a entrada. A lixeira da barra limpa a fita.
- O teclado: `C`, `(`, `)`, apagar, `%`, `de`, `^`, `÷ × − +`, dígitos, vírgula, `em`, `=`. Escreve no mesmo campo, então as duas formas se misturam.
- Guardado: a fita em `~/.local/state/lumna/calculadora.txt` (até 300 linhas) e a escolha do teclado em `~/.config/lumna/calculadora.ini`.

## Testes

- `sh testes/calculadora.sh`: o motor com ASAN/UBSAN (136 casos escritos à mão, com a conta e o resultado esperado e os erros) e **12 000 contas sorteadas contra o Python**
  (frações exatas, calculadas a partir da árvore da conta, independentes do meu parser). Trocar uma conta no motor faz o diferencial falhar.
- `sh testes/calculadora_app.sh PASTA_DO_ORBIT`: o app num Orbit sem tela (digitar, resultado ao vivo, incompleto x erro, fita, continuar do resultado, cliques na fita,
  teclado por cliques, guardar e reabrir, limpar). Desligar a "continuação" faz o teste falhar.
- Bugs que os testes acharam e já corrigi: `-40 °C` era recusado; `50% + 25%` dava 0,625; um resultado como 3e-13 virava 0; depois do Esc o campo perdia o foco e não dava
  mais para digitar; texto inserido por fora (clicar num resultado, teclas) deixava o cursor no começo (o Gravity ganhou `gv_campo_cursor_no_fim`).

## O que não foi provado / falta

- Só testado num Orbit sem tela. **Não testei** o toque (as teclas são grandes, mas ninguém tocou), nem o tamanho da janela em telas pequenas/celular.
- Sem variáveis, sem modo científico/programador, sem moeda (precisaria de cotação), sem memória (MC/MR/M+/M−), sem unidades compostas (m², km/h só como unidade pronta).
- O cursor do campo vai sempre para o fim quando o app mexe no texto (o teclado de cliques escreve no fim, mesmo com o cursor no meio).
- "por" e "dividido por" não são operadores (ambíguos); use `÷` ou `/`.
