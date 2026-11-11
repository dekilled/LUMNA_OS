# Loja do Gaming (aba "Loja" do lumna-jogos)

Esboços: Destaques (hero, "Descubra sua próxima aventura" com gêneros), Buscar (filtros de preço, compatibilidade e jogadores), a página do jogo (Visão geral, Sobre, Requisitos, Avaliações, com Comprar e Lista de desejos), o jogo na biblioteca (Atualizar, Desinstalar) e a janela "Instalar" (local, espaço).

Decisões: catálogo unificado (Steam e GOG misturadas, cada jogo com o selo da loja, e um filtro "Todas as lojas / Só Steam / Só GOG"), e a Epic em uma subaba própria (sem busca pública). Instalar, atualizar, desinstalar e acompanhar o download são feitos **dentro do Gaming**, sem abrir a janela da Steam; só **Comprar** abre a loja da empresa, na hora.

## Estado

**Pronto e testado** (sobre respostas reais gravadas da loja da Steam e do ProtonDB, com a Steam de mentira):

- O catálogo (`gravity/servicos/loja_steam.[ch]`): destaques, busca com filtros, detalhes, "Via Proton", jogos da conta (com a chave), tudo por `curl` com cache em `~/.cache/lumna/jogos/loja`.
- As telas (`gravity/exemplos/jogos_loja.h`, na aba **Loja** do lumna-jogos):
  - **Destaques:** o jogo selecionado manda no centro e no fundo; chips de gênero (Ação, Aventura, RPG, Corrida, Estratégia, Indie) refazem a fila.
  - **Categorias, Ofertas, Lista de desejos** (esta é **local**; a da Steam só se altera pelo site).
  - **Buscar:** texto, gênero, só grátis, Linux nativo, um jogador, cooperativo e ordem (relevância, mais vendidos, lançamento, menor preço).
  - **Página do jogo:** Visão geral (resumo, prints, trailer pelo mpv), Sobre, Requisitos; no painel: preço, Comprar, Lista de desejos, "Um jogador", "Controle compatível", "Linux nativo" ou o nível do ProtonDB.
  - **Instalar:** a janela com a biblioteca da Steam (espaço livre de cada uma), o espaço necessário (dos requisitos) e o que sobra; não deixa instalar onde não cabe.
  - **Download:** barra de progresso na página do jogo (lida do appmanifest da Steam), contador no ícone de downloads da barra, e depois Instalado, Atualizar e Desinstalar.
  - **Configurações > Steam:** a chave de API e o id (a chave fica num arquivo 600 e nunca aparece).
- O instalador (`gravity/servicos/jogos.[ch]`): sobe a Steam **sem janela** (`steam -silent -nochatui -nofriendsui`) e pede `steam://install/ID`; em outra biblioteca deixa antes um `appmanifest` lá. **Comprar** abre a loja da Steam (`steam://store/ID`), a única exceção.

**Falta:** a aba **Avaliações** do esboço, o "Conectado" do topo e as conquistas.

**Não testado com a Steam de verdade** (aqui não há cliente): se `steam://install` abre mesmo um diálogo na janela da Steam, se o `appmanifest` deixado em outra biblioteca faz a Steam baixar lá, e se os flags do manifesto (progresso, atualização) batem. O roteiro 2.316 em diante confere.

## GOG

Mesmas telas da Steam, com o selo **GOG** nos cards e o filtro de loja ao lado da busca.

- **Catálogo** (`gravity/servicos/loja_gog.[ch]`): `catalog.gog.com` (destaques, ofertas, busca com gênero, grátis, Linux, um jogador/cooperativo, ordem; "Indie" é uma tag, a GOG não tem esse gênero), detalhes e preço em reais pela `api.gog.com`. Testado (`sh gravity/testes/loja_gog.sh`) contra **respostas reais gravadas**.
- **Entrar:** Configurações > GOG: abre a página de login da GOG no navegador e você cola a URL para onde ela leva depois (ou só o código); o `gogdl` troca por um token (`~/.config/lumna/jogos/gog/auth.json`, 600). Só se faz uma vez.
- **Os jogos da conta:** com o login, a Loja marca "Na biblioteca" o que você já comprou na GOG.
- **Instalar:** pede a pasta (padrão `~/Jogos`) e roda o `gogdl download` num processo à parte (`lumna-jogos gog-instalar`), que sobrevive à janela; o progresso aparece na página do jogo e no contador de downloads. Versão Linux nativa quando existe; senão a do Windows, por Wine/Proton.
- **Depois de instalado:** o jogo entra na Biblioteca (filtro GOG), abre pelo `start.sh` (Linux) ou pelo programa principal do `goggame-ID.info` (Windows, por `umu-run`/`wine`) e tem as mesmas Propriedades dos avulsos. **Desinstalar apaga a pasta do jogo** (na Loja, em dois toques).
- **Comprar:** abre a página do jogo na GOG, no navegador (a exceção, como na Steam).

**Não testado com a GOG de verdade** (sem conta nem `gogdl` aqui): o formato do `auth.json` do gogdl, a linha de progresso dele, o login pela URL colada e se o token que ele guarda serve para a lista de jogos da conta. O roteiro 2.333 em diante confere. O `gogdl` precisa estar instalado (AUR).

## Epic

A Epic **não tem catálogo público pesquisável** (a busca da loja fica atrás de um desafio do Cloudflare e é uma API sem documentação), então a Epic **não entra na busca unificada**. Ela tem uma subaba própria na Loja, **Epic**:

- **Sua biblioteca** (com login): os jogos da conta, pelo `legendary list`; Instalar, Instalado, Ver na biblioteca e Desinstalar, como nas outras lojas. O `legendary` (o motor de linha de comando do Heroic) cuida da instalação e de abrir o jogo, sem janela, num processo à parte, com o progresso na página do jogo.
- **Grátis e ofertas:** o que a própria Epic publica (`freeGamesPromotions`): os grátis agora, os que ficam grátis em breve e as ofertas, em reais. **Resgatar grátis** e **Comprar** abrem a página do jogo na loja da Epic, no navegador (resgatar pede a conta da Epic no site; depois o jogo aparece na biblioteca). Testado contra **respostas reais gravadas**.
- **Entrar:** Configurações > Epic: abre `legendary.gl/epiclogin`, e você cola o texto que a Epic mostra depois do login (`{"authorizationCode":"..."}`). O login e a lista de jogos instalados ficam em `~/.config/lumna/jogos/epic` (o `LEGENDARY_CONFIG_PATH` do legendary), separados do Heroic.
- **Na Biblioteca:** os jogos da Epic instalados entram com o filtro Epic; abrem pelo `legendary launch`. As Propriedades são do legendary (só leitura). Desinstalar pede dois toques e o legendary apaga a pasta.

**Não testado com a Epic de verdade** (sem conta nem legendary aqui): `legendary auth --code`, o formato do `list --json` e do `installed.json`, a linha de progresso ("Progress: NN%") e o `launch` (que pede o login do jogo na primeira vez). O roteiro 2.346 em diante confere. Precisa do `legendary`, que **não está nos repositórios oficiais do Arch**: é do AUR (`yay -S legendary`, ou `paru -S legendary`). O `gogdl` também é do AUR.

## A chave de API da Steam (opcional)

Sem ela a Loja funciona, mas só marca "Na biblioteca" o que já está instalado. Com ela, mostra "Jogo adquirido" no que a conta tem.

1. Entre em <https://steamcommunity.com/dev/apikey> com a sua conta. A Steam só libera a chave para contas que já gastaram ao menos US$ 5 (contas "limitadas" não têm).
2. No campo "Domain Name" ponha qualquer coisa (`localhost`) e aceite os termos. A página mostra a **chave** (32 letras e números). Ela é só sua: não a envie a ninguém.
3. O seu **id da Steam (64 bits)**: 17 dígitos que começam com 7656. Aparecem na URL do seu perfil (`steamcommunity.com/profiles/7656...`), ou em <https://steamid.io>. Se o perfil usa um apelido na URL, o steamid.io converte. O LUMNA tenta achar sozinho o do usuário logado no cliente da Steam.
4. No perfil da Steam, **Detalhes do jogo** precisa estar **Público** (senão a lista vem vazia).
5. Configurações > Steam > cole a chave (e o id, se ele não achar sozinho). Fica em `~/.config/lumna/jogos/steam.ini` (permissão 600), nunca aparece na tela nem na linha de comando.

## Limites que já sabemos

- A **lista de desejos** da Steam só se altera logado no site: a nossa será **local**.
- **Conquistas** ("12 de 48") dependem da chave e de o perfil ser público.
- O **tamanho do download** a Steam não informa antes de instalar: a janela mostrará o espaço dos requisitos.
- **Escolher a pasta de instalação** sem a janela da Steam: precisa ser testado com o cliente de verdade (registrar uma biblioteca da Steam na pasta escolhida).
- Os trailers vêm como lista de reprodução HLS/DASH; o mpv toca.
