# Trabalho por áreas (a partir de 2026-11-17)

O LUMNA ficou grande demais para um chat só: coisa para conferir passava, coisa para mudar ficava para trás. Agora cada **área** tem o **seu chat**
e o **seu arquivo aqui**. O arquivo é a memória da área: o chat novo começa lendo ele, e termina atualizando ele.

| Área | Arquivo | O que entra |
|---|---|---|
| Gaming | [GAMING.md](GAMING.md) | lumna-jogos (biblioteca, Loja Steam/GOG/Epic), controle no Gaming, Steam/Proton, regras do Orbit para jogos |
| Mídia | [MIDIA.md](MIDIA.md) | o navegador CEF (versão Mídia), streamings, catálogo, controle nos sites, Super+M |
| Painel de controle | [PAINEL.md](PAINEL.md) | todas as páginas do Painel, Atualizações (lumna-pkg), Animações, Aparência, Inicialização |
| Hardware, drivers e kernel | [HARDWARE.md](HARDWARE.md) | discos e montagem, rede, Wi-Fi, áudio, Bluetooth, GPU, drivers e kernels, boot/GRUB, ISO e instalador |
| Desktop e Orbit | [DESKTOP.md](DESKTOP.md) | o compositor (janelas, encaixe, foco, multi-monitor, XWayland), dock, barra, lançador e os apps do dia a dia (Atlas, Música, Galeria, Signals, Captura) |

As quatro primeiras são as que o Eric pediu. **Desktop e Orbit** é sugestão da Claudia: o multitarefa (o resize das janelas encaixadas) e os apps
do dia a dia não cabem bem em nenhuma das quatro. Se o Eric preferir, ela some e os itens vão para a área mais perto.

## Como abrir o chat de uma área

Mensagem de abertura (trocando a área):

> Claudia, este chat é só da área **Gaming**. Antes de tudo, leia `docs/areas/LEIAME.md` e `docs/areas/GAMING.md` no repositório
> do LUMNA (`dekilled/LUMNA_OS`, o antigo `dekilled/indoors`; branch `lumna-mobile`), me diga em poucas linhas o estado e qual pendência você sugere atacar primeiro.

## Regras de todas as áreas

- **Cada chat mexe na sua área.** Se aparecer algo de outra área, anota no arquivo dela (seção "Recebido de outras áreas") e não resolve ali.
  Mudança em código compartilhado (Orbit, `gravity/src`, `servicos/`, `session/`) é permitida quando a área precisa, mas fica dito no commit e no
  arquivo da área.
- **Fim de cada rodada de trabalho:** atualizar o arquivo da área: o "Estado", as "Pendências" e, principalmente, o **"Para o Eric conferir"**
  (o que ele precisa testar no PC, com o comando de diagnóstico quando houver). Item confirmado pelo Eric sai da lista e vai para "Confirmado no PC".
- **Git:** os chats rodam em paralelo e empurram para os mesmos branches. Antes de empurrar: `git fetch origin lumna-mobile && git merge --no-edit FETCH_HEAD`
  (nunca rebase nem force). Empurrar para **os dois**: `git push origin HEAD:lumna-mobile` e `git push origin HEAD:iso-orbit` (o push no
  `iso-orbit` faz o CI republicar os pacotes do repositório `[lumna]`). Commits pequenos, com a área no começo da mensagem.
- **Honestidade:** dizer sempre o que foi testado só aqui (headless, com servidor ou controle de mentira) e o que só o PC do Eric prova.
  Teste novo vem com **controle negativo** (desfazer a correção e ver o teste falhar).
- **Regras de produto (do Eric):** perguntar antes de decisão estrutural; app ou tela nova começa por **esboço** do Eric; fazer só o que foi pedido.
- **Orbit atualizado não troca o compositor em uso:** depois de atualizar o `lumna-orbit`, o Eric precisa **sair e entrar na sessão**.
- **Logs no PC do Eric:** Orbit em `~/.local/state/lumna/orbit.log`; o resto em `journalctl --user -b` e `journalctl -b`.

## O ambiente de trabalho da Claudia (container novo a cada chat)

- O Orbit compila contra o **wlroots 0.18** (o Eric usa o 0.20 no Arch: o código tem que valer para os dois). O container novo **não traz** o
  wlroots 0.18 compilado: compilar do fonte (meson) num prefixo em `/tmp` e usar `PKG_CONFIG_PATH`/`LD_LIBRARY_PATH` dele. O build local do 0.20 foi
  barrado antes pelo classificador de permissões; não insistir sem o Eric pedir.
- Testes de tela: Orbit com `WLR_BACKENDS=headless WLR_HEADLESS_OUTPUTS=2`, `orbit-msg` (janelas, saidas, acao, mouse mover/botao), `grim` para
  capturas, `xterm`/`xdotool` para X11, `wtype` para teclado.
- Gravity: `make` dentro de `gravity/`; testes em `gravity/testes/`.

## O roteiro de testes antigo (docs/TESTES-ISO.md)

O `TESTES-ISO.md` tem ~580 itens e muitos estão **desatualizados**. A partir de agora, a lista viva de cada área é o **"Para o Eric conferir"**
do arquivo dela. Quando o Eric fizer a bateria de testes, cada chat revisa as seções antigas da sua área (abaixo) e: apaga o que não vale mais,
reescreve o que mudou, e marca o que passou.

| Seções do TESTES-ISO.md | Área |
|---|---|
| 2k, 2l, 2m, 2n, 2o, 2p | Gaming |
| (a Mídia nova não tem seção; criar) | Mídia |
| 2, 2b (teclado na tela), 2j (pré-setup), 4b (atualizar) | Painel |
| 0, 1, 2g (drivers e kernels), 2t, 3, 3b, 4, 5 | Hardware |
| 2c, 2d, 2e, 2f, 2h, 2i, 2q, 2r, 2s | Desktop |

## Fora das áreas (por enquanto)

Emma, Connect (celular), Loja de apps, mobile, navegador completo do Desktop. Seguem anotados no `docs/IDEIAS.md` e `docs/PROXIMOS.md`. Se o Eric
quiser trabalhar neles, vira uma área nova com o mesmo formato.
