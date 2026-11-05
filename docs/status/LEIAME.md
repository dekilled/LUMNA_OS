# A página de status ("LUMNA OS, onde estamos")

Gerada por `gera.py` a partir dos dados que estão no próprio arquivo. O visual fica em `estilo.css`, e o resultado em `progresso.html`.

## Como atualizar (a cada mudança que importa)

1. Edite os dados no `gera.py`: `areas` (o resumo), `timeline`, `backlog` (o que falta, com uma etiqueta em cada linha), `testar`
   (o que está pronto e espera o Eric), `confirmado` e `simulado`, e `proximos`.
2. `python3 docs/status/gera.py` (gera o `progresso.html`).
3. Publique o `progresso.html` no artefato "LUMNA OS, onde estamos" (o mesmo endereço, por cima).
4. Faça o commit do `gera.py` e do `progresso.html`.

## Regras

- **Confirmado** só quando o Eric viu funcionando na máquina dele. Teste meu, com programa falso, é **simulado**.
- Tudo o que está pronto e ainda não foi confirmado entra na lista **Para você testar**. Quando ele confirmar, passa para
  **confirmado** e sai dessa lista.
- As etiquetas do `backlog`: `feito`, `testar`, `falta`, `ref` (já existe no LUMNA antigo, em Vue: é portar) e `pergunta`
  (depende de uma decisão do Eric).
- O Painel de controle é contado no código (`gravity/exemplos/painel`): página sem função de desenho é esqueleto. Nada de
  percentual inventado.
- Quando mudar o que está pronto, atualize também `gravity/ROTEIRO.md` e `gravity/PAINEL.md`.
