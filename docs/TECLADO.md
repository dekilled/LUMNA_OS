# Teclado na tela (`lumna-teclado`)

Um teclado só, que serve ao celular e ao PC. Sobe de baixo como uma camada (layer-shell) com zona exclusiva: **empurra as janelas** em vez de ficar por cima do campo. Escrito em Gravity (C/Clay), sem biblioteca de fora.

## Quando aparece

Por padrão ("Sozinho"), só quando um campo de texto ganha o foco **e** a pessoa está sem teclado físico ou acabou de tocar na tela (a última entrada foi toque). Com teclado e mouse, não sobe. Em **Painel > Teclado e mouse > Teclado na tela**:

| Modo | |
|---|---|
| Sozinho | sem teclado físico, ou ao tocar na tela |
| Sempre | sempre que um campo de texto abrir |
| Só quando eu pedir | `lumna-teclado alternar` (um botão, um atalho) abre e fecha; `lumna-teclado esconder` fecha |

E o tamanho: pequeno, médio, grande. O `⌄` esconde até o próximo campo. O que o Painel grava está em `~/.config/lumna/teclado.ini`.

## O que tem

- **Layout da língua de quem usa**: o do Orbit (`orbit-msg entrada`: `br` = pt-BR ABNT2, o resto cai no US); a tecla de idioma troca.
- **Letras com acentos por toque longo** (a → á à â ã ä å, e, i, o, u, c, n, s, `.`, `$` → €...). Segurou, escorregou, soltou escolhe; soltou sem escolher escreve a própria tecla.
- **Shift**: uma vez vale uma letra, duas trava (traço embaixo), a terceira solta. A primeira letra de uma frase vem maiúscula sozinha (campo vazio, depois de `. `, `! `, `? `, linha nova), a menos que o campo diga que é senha/dado sensível.
- **Números e telefone** abrem direto nos símbolos (o campo diz o "propósito").
- **Símbolos** em duas páginas, **emojis** em 9 categorias + os recentes (`~/.local/share/lumna/teclado/recentes`).
- **Tela larga (PC)**: entram Esc, Tab e as setas; o teclado fica com no máximo 1100 px, centralizado.
- **Vários dedos ao mesmo tempo** (dois polegares): `gv_pontos()` no Gravity.
- Apagar e setas repetem se o dedo ficar.

## Como funciona

```
campo de texto (Firefox, Painel, GTK...) ──text-input-v3──> Orbit (im.c) ──input-method-v2──> lumna-teclado
                                                                                   │ commit_string (texto pronto)
                                                                                   │ delete_surrounding_text (apagar)
                                                                                   └ teclado virtual: Enter, Tab, Esc, setas;
                                                                                     letras quando não há campo (um terminal)
```

- `gravity/servicos/teclado_layout.{c,h}`: as páginas, a geometria, o que cada toque faz (sem Wayland: testável).
- `gravity/servicos/teclado_im.{c,h}`: a ligação com o compositor (input-method-v2 + virtual-keyboard), com fd próprio.
- `gravity/servicos/teclado_cfg.{c,h}`: ajustes e recentes.
- `gravity/exemplos/teclado.c`: a camada, o toque, o desenho.
- Orbit: `orbit-msg entrada` traz `teclado_fisico` e `ultima_entrada` (`toque`/`mouse`/`teclado`; o teclado virtual não conta); a faixa de gestos da borda do celular não vale sobre o teclado.

Só um método de entrada por vez: se outro estiver ligado, o `lumna-teclado` avisa e só abre pelo atalho.

## Testes

```
sh gravity/testes/teclado.sh                    # layout e ajustes (sem tela)
make -C gravity teclado
sh gravity/testes/teclado.sh PASTA_DO_BUILD_DO_ORBIT   # + o app inteiro num Orbit headless com toques de verdade
```

O e2e usa um app de mentira com text-input (`cliente_texto.c`) e `orbit-msg toque` (vários dedos): maiúscula automática, dois dedos juntos, apagar, toque longo e variante, espaço, Enter, símbolos, emojis, fechar, e os modos do Painel.

## O que ainda não foi provado

Só rodou no Orbit headless (wlroots 0.18) com toques simulados. Falta ver numa tela de toque de verdade, no Firefox/GTK (que usam text-input-v3 de formas próprias), na VM e no celular; a vibração ao tocar; sugestões/correção de palavras; o botão para abrir o teclado na barra.
