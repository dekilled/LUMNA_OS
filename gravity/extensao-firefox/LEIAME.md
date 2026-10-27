# Extensão do Firefox da Mídia

A extensão que o `lumna-midia servico` usa para controlar o Firefox da Mídia: abre, ativa e fecha as abas dos
serviços, avisa o endereço, o título e se a página está carregando, e tira a miniatura. Fala com o serviço por um
WebSocket só em `127.0.0.1:47822`, e o serviço só aceita quem vem dela (o `Origin` é o `moz-extension://UUID` que ele
mesmo fixa no perfil; uma página da web não consegue forjá-lo).

Ela existe no lugar do WebDriver (`--remote-debugging-port`): com o WebDriver ligado o Firefox se anuncia aos sites
como automatizado (`navigator.webdriver`) e o Google recusa o login. Uma extensão não liga essa marca.

## Como o serviço a instala

A cada vez que abre o Firefox, o serviço escreve o `user.js` do perfil (`~/.local/share/lumna/midia/perfil`) e põe a
extensão em `perfil/extensions/`. Ele a procura, nesta ordem, em `$LUMNA_EXTENSAO` (arquivo `.xpi` ou pasta),
`/usr/share/lumna/midia/lumna-midia.xpi` e `extensao-firefox/` ao lado do programa (o `.xpi`, e depois a pasta).

## Assinatura (só no Firefox comum)

O Firefox comum (o do Arch, por exemplo) só liga extensões **assinadas pela Mozilla**. Duas saídas:

1. **Developer Edition, ESR ou Nightly** aceitam a pasta sem assinar. No Arch: `firefox-developer-edition`, e
   `LUMNA_FIREFOX=firefox-developer-edition lumna-midia servico`. Nada mais a fazer.
2. **Assinar a extensão** (de graça, automático, sem publicar nada): crie uma conta em addons.mozilla.org, gere as chaves
   de API (Tools > Manage API Keys) e rode, na pasta `gravity`:

   ```
   make extensao                                    # gera extensao-firefox/lumna-midia.xpi (sem assinatura)
   npx web-ext sign --channel=unlisted --source-dir extensao-firefox \
       --api-key=SEU_USUARIO --api-secret=SEU_SEGREDO
   cp web-ext-artifacts/*.xpi extensao-firefox/lumna-midia.xpi   # o assinado toma o lugar do outro
   ```

   O `.xpi` assinado pode ficar no repositório e ir para `/usr/share/lumna/midia/` no pacote do sistema. O id
   (`lumna-midia@lumna.os`) tem de ser o mesmo de sempre.

## Mensagens

Ver o comentário no começo de `fundo.js`.
