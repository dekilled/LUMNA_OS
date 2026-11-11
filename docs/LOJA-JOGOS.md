# Loja do Gaming (aba "Loja" do lumna-jogos)

Esboços: Destaques (hero, "Descubra sua próxima aventura" com gêneros), Buscar (filtros de preço, compatibilidade e jogadores), a página do jogo (Visão geral, Sobre, Requisitos, Avaliações, com Comprar e Lista de desejos), o jogo na biblioteca (Atualizar, Desinstalar) e a janela "Instalar" (local, espaço).

Decisões: catálogo unificado, começando pela **Steam** (depois GOG e Epic). Instalar, atualizar, desinstalar e acompanhar o download são feitos **dentro do Gaming**, sem abrir a janela da Steam; só **Comprar** abre a loja da empresa, na hora.

## Estado

- **Pronto e testado:** o catálogo da Steam (`gravity/servicos/loja_steam.[ch]`): destaques (ofertas, mais vendidos, lançamentos), busca com filtros (gênero, grátis, Linux nativo, um jogador/cooperativo, ordem), detalhes (descrição, prints, trailer, requisitos, preço, plataformas), o nível do ProtonDB ("Via Proton") e a lista de jogos da conta (com a chave de API). Tudo por `curl`, com cache em `~/.cache/lumna/jogos/loja`.
  Testado (`sh gravity/testes/loja_steam.sh`, ASAN/UBSAN) contra um servidor local com **respostas reais gravadas** da loja da Steam e do ProtonDB.
- **Falta:** a interface (as telas do esboço), o serviço que instala pela Steam escondida e a janela de instalação.

## A chave de API da Steam (opcional)

Sem ela a Loja funciona, mas só marca "Na biblioteca" o que já está instalado. Com ela, mostra "Jogo adquirido" no que a conta tem.

1. Entre em <https://steamcommunity.com/dev/apikey> com a sua conta. A Steam só libera a chave para contas que já gastaram ao menos US$ 5 (contas "limitadas" não têm).
2. No campo "Domain Name" ponha qualquer coisa (`localhost`) e aceite os termos. A página mostra a **chave** (32 letras e números). Ela é só sua: não a envie a ninguém.
3. O seu **id da Steam (64 bits)**: 17 dígitos que começam com 7656. Aparecem na URL do seu perfil (`steamcommunity.com/profiles/7656...`), ou em <https://steamid.io>. Se o perfil usa um apelido na URL, o steamid.io converte. O LUMNA tenta achar sozinho o do usuário logado no cliente da Steam.
4. No perfil da Steam, **Detalhes do jogo** precisa estar **Público** (senão a lista vem vazia).
5. Quando a interface existir: Configurações > Steam > cole a chave (e o id, se ele não achar sozinho). Fica em `~/.config/lumna/jogos/steam.ini` (permissão 600), nunca aparece na tela nem na linha de comando.

## Limites que já sabemos

- A **lista de desejos** da Steam só se altera logado no site: a nossa será **local**.
- **Conquistas** ("12 de 48") dependem da chave e de o perfil ser público.
- O **tamanho do download** a Steam não informa antes de instalar: a janela mostrará o espaço dos requisitos.
- **Escolher a pasta de instalação** sem a janela da Steam: precisa ser testado com o cliente de verdade (registrar uma biblioteca da Steam na pasta escolhida).
- Os trailers vêm como lista de reprodução HLS/DASH; o mpv toca.
