# Ideias e otimizações

Onde o Eric e a Claudia anotam o que lembrar para depois. Quando o Eric tiver uma ideia, é só falar: a Claudia põe aqui (com a data) e, quando virar trabalho, passa para "Feito" com o commit.

Regras de sempre: perguntar antes de decisão estrutural; app novo começa por esboço do Eric; só o que foi pedido.

## Em andamento

- **Menu de contexto de arquivos**: feito no Atlas e na área de trabalho (Abrir com…, Abrir local do arquivo, Enviar para com Área de trabalho, Documentos, E-mail, nuvens e redes conectadas, e Enviar para dispositivo…). Falta o Eric testar no PC (itens 2.469). Pendente: Enviar para na Galeria e no menu de outros apps; o Atlas ainda tem a própria cópia da lista de celulares (pode usar `env_celulares`).
- **Cliente de e-mail do LUMNA** (algo como o Thunderbird: várias contas, receber e enviar). Primeiro para Linux; depois Windows; o Eric também quer usá-lo fora do LUMNA. Precisa de esboço antes de codar. Até lá, o "Enviar para > E-mail" usa o cliente que estiver instalado (mailto).
- **Fone Bluetooth** que conectava sozinho ao ligar o PC e parou: o `bluetooth.service` não era ligado pela ISO nem pela sessão (corrigido em `iso/build.sh` e `lumna-session.install`); falta o Eric confirmar no PC.

## Animações (página própria no Painel: Animações)

Decisões do Eric (2026): o ajuste fica numa **página própria** do Painel, com a versão simples como alternativa para máquina fraca.

- **Workspaces do Desktop:** feito. Os 3 espaços dão a volta em círculo (1→2→3→1); avançar desliza para a esquerda, voltar para a direita (3→1 também desliza para a esquerda). A página **Animações** do Painel também está feita (Nível + ajuste fino).
- **Janelas:** feito (Orbit, `winanim.c`): nascem 6% maiores e transparentes e assentam no tamanho final (180 ms); ao fechar, o contrário, com um fantasma da última imagem. Jogos, telas de Gaming/Mídia e tela cheia não animam; tudo mais anima. Falta o Eric ver no PC (2.473). Ideias futuras: curva com leve "quicar", animar minimizar/restaurar, popups.
- **Lançador de apps:** feito (Gravity): o fundo vai de transparente até a opacidade dele e o conteúdo nasce menor e cresce; ao fechar, o contrário. O Gravity ganhou `gv_grupo_transformar` (escala e opacidade de um trecho), que outras telas podem usar (menus, popups, diálogos). Falta o Eric ver no PC (2.474).
- **Carrossel dos modos:** feito (zoom contínuo).
- Depois: boot, pós-login (abaixo).

## Polimento (anotado em 2026-11-14, depois da abertura)

Bugs (o Eric viu no PC):
- **Updater:** feito (2026-11-14): o "Atualizar tudo" abre um terminal e, quando ele termina, o Painel procura de novo sozinho e os botões voltam a "sem atualização" (Aplicativos > Atualizações, a página do aplicativo e Sistema > Atualizações). Falta o Eric ver no PC (2.478).
- **Atlas ao redimensionar:** feito (2026-11-14): a barra de título e a de ferramentas acompanham a largura da janela (as barras têm largura fixa = a da janela; o que passa é cortado), a busca encolhe, e o app pede o tamanho mínimo 560×360 (`gv_opcoes.largura_min/altura_min`, e o Orbit agora respeita o mínimo do app ao arrastar a borda). Outros apps Gravity podem pedir o mínimo e usar o mesmo invólucro se mostrarem o mesmo defeito. Falta o Eric ver no PC (2.479).
- **Montar disco no boot:** (a) o pendrive: corrigido um motivo provável (2026-11-14): volumes que apareciam nos primeiros segundos da sessão eram tratados como "ao conectar" e não "ao entrar"; agora os primeiros 60 s valem como entrada, e o `lumna-discos` escreve no `orbit.log` por que não montou cada volume (`grep 'monta sozinho\|montou' ~/.local/state/lumna/orbit.log`). Falta confirmar no PC com o log. (b) o **HDD interno** nunca monta sozinho **de propósito** (só removíveis; montar disco do sistema pede senha de administrador pelo udisks): para montar no boot o caminho é uma linha no `/etc/fstab` (`nofail`), que pede o Painel pedir a senha de administrador; decisão do Eric pendente.
- **Música:** (a) crashou logo depois de tocar uma faixa com o HDD recém-montado (pedir o log); (b) a letra aparece em algumas faixas, mas sem sincronia com a música.

Recursos pedidos:
- **Captura de tela: "última área usada"** para repetir o mesmo recorte sem refazer (o Eric usa muito).
- **Janelas lado a lado (Orbit):** o encaixe pega cedo demais ao mover a janela. Ideias do Eric: zona menor e mais perto da borda, ou um quadradinho fixo que ativa; a janela encaixada ir até a borda de baixo (dock se esconde) para dar mais espaço; depois empilhar 2 janelas por lado (grade) e arrastar para mudar largura/altura no empilhado. Perguntar antes: como ativar (zona fina ou quadrado), e a dock.
- **Barra de título em apps de fora** (Firefox, Spotify): sem barra, ou só com o fechar; falta minimizar e maximizar no estilo do LUMNA.

## Para fazer (anotado em 2026-11)

- **Animação pós-login:** feita (`lumna-abertura`, ver docs/ABERTURA.md): o Eric mandou os três vídeos (start, mid-loop, end); toca depois do login em todas as telas, com laço até o desktop ficar pronto, pulo por tecla e o interruptor (e o do som) no Painel > Animações. Falta o Eric ver no PC (2.477) e dizer se roda bem no notebook fraco.
- **Boot e GRUB:** feitos (pacote `lumna-boot`, ver docs/BOOT.md); falta o Eric aplicar no PC (2.476) e decidir o tema na ISO live.
- **Barra de título do Firefox:** hoje tem a própria dele, com um único ícone de fechar; falta uma barra de título completa (minimizar, maximizar, fechar) no estilo do LUMNA.
- **Modo Mídia:** mudar um pouco o estilo (o Eric vai dizer o quê; pedir esboço).
- **Modo Mídia vinculado a um monitor/projetor** (escolher a tela da Mídia e mantê-la lá).
- **Wi-Fi:** placa interna do notebook conecta mas não transmite (talvez driver ou economia de energia); o adaptador USB funciona como alternativa. Ver `nmcli`/`dmesg` se voltar a incomodar.
- **Fone USB (dongle) que às vezes não toca:** pedir `wpctl status`, `pactl list cards` e o `dmesg` quando falhar; suspeitas: perfil da placa não escolhido, timing do dongle, suspensão.

## Para fazer

- Papel de parede em vídeo.
- Extras da Loja.
- Gerenciar aparelhos no Connect (parear, esquecer, ver o que cada um pode).
- Navegador para o celular (mobile).
- Teclado na tela abrindo sozinho em apps de fora do LUMNA.
- API de `lj_buscar` devolvendo a contagem.
- Teste do Atlas: "discos" depois do menu depende da ordem dos testes.
- Animações do sistema em geral (o Eric tem ideias; "o LUMNA merece um pouco de extravagância" na fase final). Já feito: o zoom do carrossel (vizinhos saem/entram de fora da tela, o do centro dá zoom). Ideia do Eric: **estilos de animação escolhíveis** (um ajuste no Painel, com a Rápida/simples como alternativa para máquina fraca); outras animações entram como novos estilos. Perguntar antes: quais partes do sistema, e se vira um ajuste único de "nível de animação".

## Decidido

- **Carrossel de workspaces: sem miniaturas reais da tela** (mais leve e mais simples). Só se o Eric sentir falta depois de usar. (2026)

## Feito

- Carrossel de modos (Super + Tab), com a troca Rápida como alternativa no Painel.
- NumLock ligado sozinho no login e na sessão.
- Área de trabalho com `~/Desktop` próprio, uma por tela.
- Calculadora-fita, editor Signals, auditoria dos modos.
