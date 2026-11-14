# Navegador do LUMNA (CEF)

Ideia e decisões do Eric (2026-11-14). **Nada foi codado ainda:** este arquivo é a lista de funcionalidades para o Eric marcar o que fica e o que vai para o lixo, mais as perguntas técnicas que decidem o caminho.

## O que já está decidido

- **Base:** Chromium com o **CEF** (Chromium Embedded Framework). Permite mexer em tudo e roda os plug-ins do streaming.
- **Duas versões, dois apps** (para não haver confusão de quem abre o quê):
  - **Completa**: o navegador do dia a dia do Desktop. O esboço do Eric está em [navegador/esboco-completa.png](navegador/esboco-completa.png): abas com ícone e fechar, voltar/avançar/recarregar, barra de endereço, favoritar, downloads, botão da Emma e menu; **nova aba** com o logo, a busca (com microfone), atalhos (YouTube, GitHub, Netflix, Arch Wiki, Projetos, Adicionar) e **"Continuar de onde parou"** (cartões com miniatura); **painel da Emma** à direita (Resumir a página, Encontrar nesta página, Comparar abas, e uma caixa para perguntar sobre a página, com voz).
  - **Simples**: para o modo **Mídia**, só o básico do streaming. (Esboço ainda não existe.) Substitui o Firefox que o `lumna-midia` abre hoje.
- **Princípio:** jogar fora o que for inútil, manter o que for útil, e deixar o mais **leve** possível.

## A lista de funcionalidades (marcar com ✅ fica, ❌ lixo, 🟡 depois)

"Como" diz de onde vem: **CEF** (já vem no Chromium), **fazer** (nós construímos) ou **sistema** (usa o que o LUMNA já tem). A coluna **Sugestão** é minha, só para começar a conversa.

| # | Funcionalidade | Completa | Simples (Mídia) | Como | Sugestão |
|---|---|---|---|---|---|
| 1 | Abas (abrir, fechar, reordenar, favicon) | | 🟡 (poucas ou nenhuma) | CEF + fazer a barra | ✅ completa; simples: 1 a 3 abas |
| 2 | Barra de endereço e busca (URL ou pesquisa) | | ✅ (com voz) | fazer | ✅ ambas |
| 3 | Voltar, avançar, recarregar | | ✅ | CEF | ✅ ambas |
| 4 | Nova aba do LUMNA (atalhos, "Continuar de onde parou") | | só os atalhos de streaming | fazer (HTML) | ✅ completa; simples: grade de serviços |
| 5 | **Painel da Emma** (resumir, achar na página, comparar abas, perguntar) | | só voz | fazer + Emma | ✅ completa; simples: só o microfone |
| 6 | Histórico | | ❌ | CEF + fazer a tela | ✅ completa |
| 7 | Favoritos | | ❌ (atalhos já servem) | fazer | ✅ completa (simples) |
| 8 | Downloads | | ❌ | CEF + Atlas | ✅ completa, entregando ao Atlas/Connect |
| 9 | Buscar na página (Ctrl+F) | | ❌ | CEF | ✅ completa |
| 10 | Zoom, tela cheia | | tela cheia ✅ | CEF | ✅ ambas |
| 11 | **Widevine** (DRM do Netflix, Disney+, Spotify) | | ✅ | CDM fornecido à parte | ✅ ambas (essencial) |
| 12 | Codecs H.264/AAC (YouTube, Netflix) | | ✅ | **precisa de build próprio do CEF** (ver abaixo) | ✅ ambas (essencial) |
| 13 | Teclas de mídia, MPRIS (barra e Mídia controlam o player) | | ✅ | CEF + sistema | ✅ ambas |
| 14 | Picture-in-picture | | ❌ | CEF | ✅ completa |
| 15 | Controle por **gamepad/controle remoto** (navegar, clicar, teclado na tela) | ❌ | ✅ essencial | fazer + `lumna-gamepad`/`lumna-teclado` | ✅ simples |
| 16 | Janela anônima | | ❌ | CEF | ✅ completa |
| 17 | Permissões de site (câmera, microfone, notificações) | | ✅ mínimo | CEF + fazer o aviso | ✅ completa; simples: só áudio/tela cheia |
| 18 | Apagar dados de navegação | | ❌ | CEF | ✅ completa |
| 19 | **Extensões** (Chrome Web Store) | | ❌ | CEF só em parte | 🟡 discutir: bloqueador de anúncios **embutido e leve** no lugar |
| 20 | Gerenciador de senhas | | ❌ | fazer sobre o chaveiro do sistema | 🟡 depois |
| 21 | Preenchimento automático (endereços, cartões) | | ❌ | CEF | ❌ lixo |
| 22 | Sincronização (conta Google) | | ❌ | — | ❌ lixo (o Connect cuida) |
| 23 | Tradução de páginas | | ❌ | depende do Google | ❌ lixo (a Emma traduz) |
| 24 | Navegação segura (Safe Browsing) | | ❌ | envia URLs ao Google | ❌ lixo (privacidade) |
| 25 | Leitor de PDF | | ❌ | CEF (pesado) | 🟡 ou abrir no app de PDF do sistema |
| 26 | Impressão | | ❌ | CEF + CUPS | ✅ completa, depois |
| 27 | Ferramentas de desenvolvedor | | ❌ | CEF | 🟡 só na completa, como opção |
| 28 | Cast / compartilhar tela do Chrome | | ❌ | — | ❌ lixo |
| 29 | Modo leitura, "Lens", assistentes do Google | | ❌ | — | ❌ lixo |
| 30 | Perfis múltiplos | | ❌ | CEF | 🟡 um perfil só no começo |
| 31 | Grupos de abas, abas verticais, busca em abas | | ❌ | fazer | 🟡 depois |
| 32 | WebRTC (Meet, Discord) e WebGL/WebGPU | | WebGL ✅ | CEF | ✅ ambas |
| 33 | Corretor ortográfico | | ❌ | CEF (pt-BR) | ✅ completa |
| 34 | Página de configurações | | ❌ | fazer (ou no **Painel** do LUMNA) | ✅ completa, no Painel |
| 35 | Telemetria, relatórios de falha, atualização automática do Google | | ❌ | desligar | ❌ lixo |

## Perguntas técnicas que decidem o caminho (honestamente, as difíceis)

1. **Codecs H.264/AAC.** Os binários prontos do CEF **não** trazem os codecs proprietários (o Chromium livre os desliga); sem eles muita coisa de streaming e vídeo não toca. É preciso um **build próprio do CEF** com `proprietary_codecs=true ffmpeg_branding=Chrome`, que leva muitas horas e dezenas de GB, ou usar um pacote que já faça isso. **Isso é o maior risco do projeto.**
2. **Widevine.** O CEF não traz o CDM. Ele precisa vir do Chrome instalado ou ser baixado, e a licença de redistribuição é da Google. No Linux, sem hardware certificado, costuma ficar em **L3 (qualidade limitada, ~720p a 1080p nos serviços)**. Quanto isso pesa para você?
3. **Como desenhar a interface.** Três jeitos: (a) a interface do navegador **em HTML/CSS** dentro de um CEF e as abas como outro CEF (o jeito de várias alternativas; o esboço sai quase pronto, mas é mais um processo e mais memória); (b) o **CEF Views** (C++), mais leve, mas o visual é mais travado; (c) a interface em **Gravity (C)** e o CEF renderizando fora da tela para dentro dela (o jeito do nosso `libmpv`; o vídeo e o DRM ficam mais difíceis, sem a GPU direta). Meu palpite: **(a) para a completa e uma interface mínima para a simples**, mas isso quer teste.
4. **A moldura da janela.** O esboço desenha os botões de minimizar, maximizar e fechar **dentro da faixa das abas** (como o Chrome). Então o navegador desenharia a própria moldura (não pediria a barra do Orbit). É isso mesmo?
5. **Wayland.** O CEF no Linux roda melhor em X11; o Wayland nativo (Ozone) existe, mas é mais cru. Vale começar em X11/XWayland (onde o Orbit já dá tudo) e migrar depois?
6. **"O mais leve possível" em números.** O Chromium não é leve por natureza (processo por aba, tamanho do pacote de ~300 MB a 1 GB). O que dá para fazer: um processo por **site** em vez de por aba, sem serviços do Google, sem componentes que não usamos, e sem a segunda interface de HTML se escolhermos (b) ou (c). Uma meta em MB de memória com 3 abas ajudaria a medir.

## O que eu sugiro como primeiro passo (sem escrever o navegador ainda)

Um **teste de viabilidade pequeno**, de um dia: pegar um CEF pronto, abrir **uma** janela com o YouTube e o Netflix, e ver (1) se os codecs tocam, (2) se o Widevine carrega, (3) quanta memória gasta, (4) como o Orbit trata a janela. Se isso passar, o resto é construir a interface em cima. Se não passar (codecs ou DRM), melhor descobrir antes de desenhar tudo.
