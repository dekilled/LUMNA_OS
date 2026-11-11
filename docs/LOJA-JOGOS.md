# Loja do Gaming (aba "Loja" do lumna-jogos)

Esboços: Destaques (hero, "Descubra sua próxima aventura" com gêneros), Buscar (filtros de preço, compatibilidade e jogadores), a página do jogo (Visão geral, Sobre, Requisitos, Avaliações, com Comprar e Lista de desejos), o jogo na biblioteca (Atualizar, Desinstalar) e a janela "Instalar" (local, espaço).

Decisões: catálogo unificado, começando pela **Steam** (depois GOG e Epic). Instalar, atualizar, desinstalar e acompanhar o download são feitos **dentro do Gaming**, sem abrir a janela da Steam; só **Comprar** abre a loja da empresa, na hora.

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

**Falta:** a aba **Avaliações** do esboço, o "Conectado" do topo, a **GOG** e a **Epic**, o catálogo unificado com elas ("Origem: LUMNA"), e as conquistas.

**Não testado com a Steam de verdade** (aqui não há cliente): se `steam://install` abre mesmo um diálogo na janela da Steam, se o `appmanifest` deixado em outra biblioteca faz a Steam baixar lá, e se os flags do manifesto (progresso, atualização) batem. O roteiro 2.316 em diante confere.

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
