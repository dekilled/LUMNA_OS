# Ideias e otimizações

Onde o Eric e a Claudia anotam o que lembrar para depois. Quando o Eric tiver uma ideia, é só falar: a Claudia põe aqui (com a data) e, quando virar trabalho, passa para "Feito" com o commit.

Regras de sempre: perguntar antes de decisão estrutural; app novo começa por esboço do Eric; só o que foi pedido.

## Em andamento

- **Menu de contexto de arquivos** (Atlas e área de trabalho): "Abrir com…", "Abrir local do arquivo", "Enviar para" com sublista (Área de trabalho, Documentos, E-mail, provedores de nuvem já conectados, pastas compartilhadas da rede) e, no Connect, um botão "Enviar para dispositivo…" com os aparelhos conectados.
- **Cliente de e-mail do LUMNA** (algo como o Thunderbird: várias contas, receber e enviar). Primeiro para Linux; depois Windows; o Eric também quer usá-lo fora do LUMNA. Precisa de esboço antes de codar. Até lá, o "Enviar para > E-mail" usa o cliente que estiver instalado (mailto).
- **Fone Bluetooth** que conectava sozinho ao ligar o PC e parou: o `bluetooth.service` não era ligado pela ISO nem pela sessão (corrigido em `iso/build.sh` e `lumna-session.install`); falta o Eric confirmar no PC.

## Para fazer

- Papel de parede em vídeo.
- Extras da Loja.
- Gerenciar aparelhos no Connect (parear, esquecer, ver o que cada um pode).
- Navegador para o celular (mobile).
- Teclado na tela abrindo sozinho em apps de fora do LUMNA.
- API de `lj_buscar` devolvendo a contagem.
- Teste do Atlas: "discos" depois do menu depende da ordem dos testes.
- Animações do sistema em geral (o Eric tem ideias; a troca de workspace em carrossel já foi feita).

## Decidido

- **Carrossel de workspaces: sem miniaturas reais da tela** (mais leve e mais simples). Só se o Eric sentir falta depois de usar. (2026)

## Feito

- Carrossel de modos (Super + Tab), com a troca Rápida como alternativa no Painel.
- NumLock ligado sozinho no login e na sessão.
- Área de trabalho com `~/Desktop` próprio, uma por tela.
- Calculadora-fita, editor Signals, auditoria dos modos.
