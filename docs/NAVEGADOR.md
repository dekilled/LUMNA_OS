# Navegador do LUMNA (CEF)

Ideia e decisões do Eric (2026-11-14). **O navegador em si ainda não foi codado:** este arquivo é a lista de funcionalidades para o Eric marcar o que fica e o que vai para o lixo, mais as perguntas técnicas que decidem o caminho. Só existe o **teste de viabilidade** (seção no fim), em [navegador/teste-cef/](navegador/teste-cef/).

## O que já está decidido

- **Base:** Chromium com o **CEF** (Chromium Embedded Framework). Permite mexer em tudo e roda os plug-ins do streaming.
- **Duas versões, dois apps** (para não haver confusão de quem abre o quê):
  - **Completa**: o navegador do dia a dia do Desktop. O esboço do Eric está em [navegador/esboco-completa.png](navegador/esboco-completa.png): abas com ícone e fechar, voltar/avançar/recarregar, barra de endereço, favoritar, downloads, botão da Emma e menu; **nova aba** com o logo, a busca (com microfone), atalhos (YouTube, GitHub, Netflix, Arch Wiki, Projetos, Adicionar) e **"Continuar de onde parou"** (cartões com miniatura); **painel da Emma** à direita (Resumir a página, Encontrar nesta página, Comparar abas, e uma caixa para perguntar sobre a página, com voz).
  - **Simples**: para o modo **Mídia**, só o básico do streaming. (Esboço ainda não existe.) Substitui o Firefox que o `lumna-midia` abre hoje.
- **Respostas do Eric às perguntas técnicas (2026-11-14):**
  1. **Codecs:** o build próprio do CEF com H.264/AAC **entra no projeto** (é parte do trabalho).
  2. **Widevine:** o Eric entende que o Chromium já vem com DRM e só falta licenciar. Nuance: o Chromium traz o *suporte* (a interface do CDM); o **binário do CDM** vem da Google/Chrome e a licença de redistribuição é dela. No Linux costuma ser L3.
  3. **Moldura:** o navegador **desenha a própria moldura**, com os 3 botões dentro da faixa das abas, sem pedir barra ao Orbit. Haverá uma **opção nas configurações** do navegador para pedir a barra de título do Orbit (modo padrão), caso o desenho próprio não dê certo.
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

## Resultado do teste de viabilidade (2026-11-14, feito no container, sem GPU e sem tela real)

App de teste mínimo (CEF **Views**, C++, ~100 linhas, em [navegador/teste-cef/](navegador/teste-cef/)) com o CEF **156.0.30** (Chromium 156.0.8078.96, `linux64_minimal`, o binário pronto oficial). Abriu **uma janela** no Orbit (via XWayland) numa página local que consulta o que o motor suporta.

| Teste | Resultado |
|---|---|
| Compila e abre no Orbit (X11/XWayland) | ✅ janela normal, flutuante, 1000x650 |
| **H.264** (`avc1`) e **AAC** (`mp4a`) | ❌ **não** suportados, como previsto: o binário oficial exclui os codecs proprietários |
| HEVC | ❌ não |
| VP9 e AV1 | ✅ sim (o YouTube serve esses, mas Netflix/Disney+/muitos sites exigem H.264) |
| **Widevine** | ❌ `NotSupportedError` (sem CDM, como previsto) |
| Memória, 1 janela e 1 página local | **~190 MB** de PSS (memória real), 8 processos (browser, 2 zygote, gpu, 2 utility, 2 renderer); a soma de RSS dá ~810 MB, mas conta a memória compartilhada várias vezes |
| Tamanho do pacote | **1,5 GB** em disco (a `libcef.so` sozinha tem 1,4 GB, com símbolos; dá para reduzir bastante com strip) |
| Wayland nativo (`--ozone-platform=wayland`) | ⚠️ **inconclusivo**: o app caiu (SIGSEGV) no container porque aqui não existe nó DRM (`drmGetDevices2() has not found any devices`). Precisa ser testado numa máquina com GPU |
| Barra do Orbit | a janela do CEF Views em X11 **pediu a moldura e o Orbit pôs a barra** (`barra:true`): hoje, por padrão, ele usaria o modo padrão. Para a moldura própria será preciso pedir ao CEF uma janela sem moldura (`CefWindowDelegate::IsFrameless`) |

**Conclusões honestas:**
- O motor roda no Orbit e a memória base é razoável. **Os dois riscos previstos (codecs e Widevine) estão confirmados:** com o binário oficial não toca H.264/AAC e não há DRM.
- **Próximo passo duro:** o build próprio do CEF/Chromium com `proprietary_codecs=true ffmpeg_branding=Chrome` (muitas horas e ~100 GB; não cabe neste container). Alternativas: procurar um CEF já compilado com codecs, ou testar com o **Chromium de uma distribuição** (que costuma ter os codecs ligados) só para validar o resto.
- **Widevine:** testar com o CDM do Chrome instalado, apontando `--widevine-cdm-path` (o app de teste já aceita `TESTE_WIDEVINE`). Tanto o CDM quanto o teste real do Netflix pedem uma máquina com internet e GPU.
- Teste de vídeo real (YouTube/Netflix) e de Wayland nativo: **não foi possível aqui**, ficam para o PC do Eric.

## Segundo teste: CEF já compilado com H.264/AAC (2026-11-15)

Procurei um CEF pronto com os codecs. O único que achei foi o [`psmux/godwinmix-cef`](https://github.com/psmux/godwinmix-cef): scripts + releases no mesmo formato do oficial, `proprietary_codecs=true ffmpeg_branding=Chrome`, release `cef-154.0.34` (Chromium 154.0.8037.98, `linux64_minimal`, 155 MB, `libcef.so` de 470 MB). **É de um mantenedor desconhecido e vem sem licença de patentes** (H.264/AAC pedem licenciamento para distribuir). Hash SHA-256 conferido contra o publicado por ele (isso só prova que o arquivo não corrompeu, não a origem).

- **Verificação estática** (sem executar): a `libcef.so` dele contém `ff_h264_decoder` e `ff_aac_decoder`; a do CEF oficial 156 não contém nenhum dos dois.
- **Não foi executado no container** (o auto-classificador bloqueou rodar um binário de origem desconhecida). Quem roda é o Eric, no PC dele: `bash scripts/teste-cef-codecs.sh` (opções `--wayland`, `--gpu`, `--manter`, `--oficial`).
- O teste agora **toca de verdade** um mp4 H.264+AAC de 3 s (`docs/navegador/teste-cef/teste.mp4`) e tenta o Widevine (com o CDM do Chrome do sistema, se existir), servindo a página por `http://127.0.0.1` (o EME **não funciona em `file://`**: "EME use is not allowed on unique origins").
- **Validação do script** (no container): CEF oficial 156 → `toca_h264_aac = false` (`DEMUXER_ERROR_NO_SUPPORTED_STREAMS`), H.264/AAC falsos, VP9/AV1/Opus verdadeiros, ~199 MB de PSS. **Controle positivo** (Chrome for Testing 157, que tem codecs) → `toca_h264_aac = true`, áudio decodificado, então o mp4 e a página estão certos.

### Resultado do Eric no PC (2026-11-15), CEF 154 do godwinmix, X11/XWayland, sem GPU

| Teste | Resultado |
|---|---|
| H.264 (Baseline e High) e AAC | ✅ suportados |
| **Tocar o mp4 H.264+AAC de verdade** | ✅ **toca** (320x180, t=1,76 s, 16771 bytes de áudio decodificados), idêntico ao controle positivo do Chrome 157 |
| HEVC | ❌ (esperado: o Chromium só habilita com decodificação por hardware) |
| VP9, AV1, Opus | ✅ |
| Widevine | ❌ `NotSupportedError: Unsupported keySystem or supportedConfigurations` |

**Conclusão:** o risco dos **codecs está resolvido na prática**: um CEF com H.264/AAC existe e roda no LUMNA. Falta decidir de onde ele vem (build próprio, para não depender de terceiro desconhecido, e a licença de patentes para distribuir).

**Widevine ainda em aberto.** Esse erro específico é o de "sistema de chaves não registrado", ou seja, o CEF **não achou o CDM** (esperado sem o Chrome instalado: o script só tenta o CDM do sistema). Isso não diz nada ainda sobre o L3 funcionar. Próximo teste: instalar o Chrome (AUR `google-chrome`, que traz `/opt/google/chrome/WidevineCdm`) e rodar o script de novo, que o acha sozinho. Depois, um serviço real (Netflix/Disney+) é o teste final: o CDM carregar não garante que o serviço aceite um navegador que não é o Chrome.

### Widevine: por que o primeiro teste não carregou o CDM (2026-11-15)

O Eric instalou o Chrome (`/opt/google/chrome/WidevineCdm` existe: `LICENSE manifest.json _platform_specific`) e o resultado continuou `NotSupportedError`. A causa era do **script**, não do CEF: ele passava `--widevine-cdm-path`/`--widevine-cdm-version`, e essas strings **não existem** na `libcef.so` (conferido nos dois CEFs; o suporte a Widevine em si está compilado: `com.widevine.alpha` e o componente do CDM estão lá). O CEF/Chromium procura uma **pasta `WidevineCdm/`** (manifest.json + `_platform_specific/linux_x64/libwidevinecdm.so`) ao lado da `libcef.so`, ou como componente dentro do diretório de dados, com o arquivo `latest-component-updated-widevine-cdm`.

O script agora copia a pasta nos dois lugares e liga o log do CDM. **Validado no container com um CDM falso** (uma `.so` vazia no lugar do Chrome): o log do Chromium mostra `Registering hinted Widevine 4.10.9999.0` (`cdm_registration.cc`) e `Updating hint file with Widevine CDM` (o componente), ou seja, o local certo é achado. Com o CDM falso o resultado continua `NAO`, como deve. **Ainda não se sabe** se o CDM *real* carrega e se um serviço (Netflix etc.) o aceita neste navegador.

### Widevine funciona: resultado do Eric (2026-11-15)

Com a pasta `WidevineCdm/` do Chrome (`/opt/google/chrome/WidevineCdm`) copiada para o lado da `libcef.so`: **`widevine = SIM`** (`requestMediaKeySystemAccess` + `createMediaKeys` passaram) com robustez `SW_SECURE_DECODE` (o nível de software, L3). Junto com H.264/AAC tocando, **a base técnica do navegador está provada: CEF + codecs + Widevine rodam no LUMNA.**

**O que ainda não está provado:** um serviço real aceitar o navegador. `SIM` quer dizer que o CDM carrega e cria chaves; Netflix/Disney+/Spotify podem ainda exigir outras coisas (user-agent, resolução máxima em L3, verificações próprias). Teste: `bash scripts/teste-cef-codecs.sh --url https://shaka-player-demo.appspot.com` (abre o site depois do teste), depois o serviço de verdade.

**Como entregar o CDM (a parte jurídica, que decide o projeto).** O CDM é binário da Google, com licença própria que **não permite redistribuir** sem acordo. Caminhos, do mais simples ao mais formal:
1. **Só no PC do Eric, apontando para o Chrome instalado** (o que o teste faz). Funciona já; serve para desenvolver.
2. **Não distribuir o CDM; baixar na máquina do usuário** (o modelo do AUR `chromium-widevine` e de scripts de Debian): um auxiliar do LUMNA, no primeiro uso, baixa o pacote oficial do Chrome da Google na máquina do usuário e extrai só a pasta `WidevineCdm/`. A ISO não carrega o binário. É uma prática comum, mas é uma zona cinzenta: o ideal é o usuário aceitar os termos da Google no auxiliar, e isso vale uma checagem jurídica antes de publicar a ISO.
3. **Acordo formal**: ser licenciado pela Google (o programa do Widevine; é como Chrome/Brave/Edge/Firefox fazem) ou usar um intermediário. A **castlabs** vende um **Electron** com Widevine licenciado e assinado (ECS); é uma base **Electron, não CEF**, então trocaria o plano (a interface sairia em HTML/JS dentro do Electron). Fica como plano B, não é um atalho.
4. **Colocar o CDM dentro da ISO** funcionaria tecnicamente, mas é redistribuir, então só serve para uso pessoal, nunca para a ISO pública.

Recomendação: **2 para o LUMNA público** (com a checagem jurídica), 1 enquanto se desenvolve, 3 só se o projeto crescer a ponto de valer o acordo.

### O CEF baixa o Widevine sozinho? (2026-11-15, testado no container)

A ideia: compilar com `enable_widevine=true` e deixar o navegador baixar o CDM da Google ao iniciar (o "componente" do Chromium). **Achado no container, com o CEF oficial 156 e perfil vazio, sem Chrome:** o CEF **baixou o CDM sozinho** (`Widevine CDM 4.10.3112.0`, 21 MB, com `_metadata/verified_contents.json`, ou seja, assinado pela Google) **em ~65 s**, de `update.googleapis.com`, sem nenhuma flag de compilação a mais (a build oficial não tem `enable_widevine` explícito no nosso controle; o suporte já está compilado). Depois de reiniciar, o CDM é registrado (`Registering hinted Widevine`).

**O que NÃO ficou provado:** neste container o CDM, baixado ou copiado, **não passa** de `NotSupportedError` (mesmo CDM, mesmo método que funcionou no PC do Eric). Isso aponta para o container sem GPU/DRM ou para o CEF 156, e não para o download, mas é só inferência. No PC do Eric o caminho com o CDM do Chrome deu `SIM`.

**Pendente:** o Eric rodar `bash scripts/teste-cef-codecs.sh --baixar` (sem copiar nada do Chrome) para ver se o CEF 154 também baixa e carrega sozinho. Mesmo que funcione, **a licença continua sendo o ponto**: a Google serve o componente a quem pede, mas redistribuir ou apoiar o produto no download automático sem acordo pode ser desligado por ela a qualquer momento (é um serviço dela, sem contrato nosso).

### Netflix toca no nosso CEF (2026-11-15, print do Eric)

**Netflix reproduzindo** ("O Mentalista", E1, 44:39 de duração) numa janela do nosso app de teste: CEF 154 do godwinmix (H.264/AAC) + CDM Widevine copiado do Chrome instalado (`/opt/google/chrome/WidevineCdm`), no Orbit, com a barra de título do Orbit (a moldura própria ainda não existe). **Isso fecha a prova de viabilidade técnica do navegador:** codecs, DRM e um serviço comercial real funcionam juntos no LUMNA.

**Ainda não medido (não dá para tirar do print):** a resolução que a Netflix entrega em L3 (costuma ficar em torno de 720p; ela mostra em "Estatísticas para nerds", Ctrl+Alt+Shift+D), o consumo de CPU/memória durante o vídeo, se a decodificação é por software (sem GPU no teste) e como fica com teclas de mídia/MPRIS. O resultado do `--baixar` (CDM baixado pelo próprio CEF, sem Chrome instalado) também está pendente.

### Netflix, HBO Max e YouTube tocam depois de desinstalar o Chrome (2026-11-15, prints do Eric)

Com o Google Chrome **desinstalado**, o app de teste (CEF 154 do godwinmix) tocou **Netflix** ("O Mentalista" E1, janela grande no Desktop do LUMNA), **HBO Max** ("Chernobyl" T1 E5) e **YouTube**. Prova que o navegador não depende do Chrome instalado.

**Ressalva de método (a confirmar com o Eric):** o script, nas rodadas anteriores, **copiava** o `WidevineCdm` do Chrome para `~/.cache/lumna-cef-teste/` (pasta do teste e perfil). Desinstalar o Chrome **não apaga essas cópias**. Então há dois cenários: (a) o Eric rodou `--baixar`, que apaga as cópias e deixa o CEF baixar o CDM da Google sozinho (a prova forte); (b) ele rodou o modo normal e o CDM veio das cópias. Os dois funcionam, mas só (a) mostra que o LUMNA não precisa de nenhum Chrome em nenhum momento. Para conferir: `ls ~/.cache/lumna-cef-teste/build-*/cache/WidevineCdm` e ver se a versão bate com a que o `--baixar` anunciou (`baixou: Widevine CDM ...`).

### Confirmado: o CEF baixou o Widevine sozinho no PC do Eric (2026-11-15)

O Eric rodou `bash scripts/teste-cef-codecs.sh --baixar` (o script apaga qualquer cópia do CDM antes) com o Chrome desinstalado, e o **CEF baixou o CDM da Google por conta própria**; depois Netflix, HBO Max (print do episódio com a vinheta da HBO) e YouTube tocaram. Então **o navegador do LUMNA não precisa de Chrome em nenhum momento e a ISO não carrega o binário do CDM** (o modelo de entrega do Widevine é o download automático pelo próprio CEF, no primeiro uso).

**Riscos que ficam (decisão do Eric, não técnica):** (1) a Google serve o componente a quem pede, mas **sem contrato com o LUMNA** ela pode parar de servir a builds que não são dela; (2) para o LUMNA público, checar a parte jurídica antes (Widevine e patentes H.264/AAC); (3) o build de CEF com codecs ainda é de terceiros: para distribuir, fazer o **build próprio**.

**Próximos passos técnicos do navegador:** moldura própria (3 botões na faixa das abas, janela sem moldura no CEF Views), abas, barra de endereço; depende da lista de funcionalidades marcada e do esboço da versão Mídia.

## Lista final de funcionalidades, marcada pelo Eric (2026-11-15)

Imagem original: [navegador/lista-marcada.png](navegador/lista-marcada.png). ✅ fica, ❌ lixo, 🟡 depois.

| # | Funcionalidade | Completa | Simples (Mídia) |
|---|---|---|---|
| 1 | Abas | ✅ | 🟡 poucas ou nenhuma |
| 2 | Barra de endereço e busca | ✅ com voz | ✅ com voz |
| 3 | Voltar, avançar, recarregar | ✅ | ✅ |
| 4 | Nova aba do LUMNA | ✅ | só os atalhos de streaming |
| 5 | Painel da Emma | ✅ | ✅ painel enxuto |
| 6 | Histórico | ✅ | ❌ |
| 7 | Favoritos | ✅ | ❌ (atalhos já servem) |
| 8 | Downloads | ✅ | ❌ |
| 9 | Buscar na página | ✅ | ❌ |
| 10 | Zoom, tela cheia | ✅ | só tela cheia |
| 11 | Widevine | ✅ | ✅ |
| 12 | Codecs H.264/AAC | ✅ | ✅ |
| 13 | Teclas de mídia, MPRIS | **✅** (corrigido pelo Eric: era ❌) | ✅ |
| 14 | Picture-in-picture | ✅ | ❌ |
| 15 | Gamepad/controle remoto | ❌ | ✅ essencial |
| 16 | Janela anônima | ✅ | ❌ |
| 17 | Permissões de site | ✅ completa | ✅ mínimo |
| 18 | Apagar dados de navegação | ✅ | ❌ |
| 19 | **Extensões** (Chrome Web Store) | **❌** (corrigido pelo Eric: era ✅) | ❌ |
| 20 | Gerenciador de senhas | ✅ | ❌ |
| 21 | Preenchimento automático | ✅ | ❌ |
| 22 | Sincronização | ❌ | ❌ |
| 23 | Tradução de páginas | ❌ | ❌ |
| 24 | Navegação segura (Safe Browsing) | ❌ | ❌ |
| 25 | Leitor de PDF | ❌ | ❌ |
| 26 | Impressão | ✅ | ❌ |
| 27 | Ferramentas de desenvolvedor | ✅ | ❌ |
| 28 | Cast | ❌ | ❌ |
| 29 | Modo leitura, Lens, assistentes do Google | ❌ | ❌ |
| 30 | Perfis múltiplos | 🟡 um perfil só no começo | ❌ |
| 31 | Grupos de abas, abas verticais, busca em abas | ✅ | ❌ |
| 32 | WebRTC e WebGL/WebGPU | WebGL ✅ | WebGL ✅ |
| 33 | Corretor ortográfico | ❌ | ❌ |
| 34 | Página de configurações | ✅ | ❌ |
| 35 | Telemetria / relatórios de falha / atualização automática do Google | só relatórios de falha **nossos** (confirmado); **nada vai para a Google** | ❌ |

**Pontos a confirmar com o Eric (pareceram inconsistentes com o resto):**
- **#13 Completa = ❌:** sem MPRIS/teclas de mídia, o Música e a barra do LUMNA **não controlam** o vídeo/áudio do navegador do Desktop. Foi de propósito?
- **#19 Extensões Completa = ✅:** é o item mais caro da lista. O CEF não traz a loja nem o instalador de extensões do Chrome. Precisa de pesquisa (o que o CEF 154 suporta de extensões Manifest V3) antes de prometer; o plano anterior era um bloqueador de anúncios embutido.
- **#25 PDF ❌:** sem leitor, o PDF vira download (ou abre no app de PDF do sistema).
- **#35:** só relatórios de falha, e para o LUMNA (nunca para a Google)? E o CEF nunca deve falar com os servidores de telemetria da Google.

## Desenho da versão Mídia (esboços do Eric, 2026-11-15)

Esboços: [navegador/midia-inicio.webp](navegador/midia-inicio.webp) e [navegador/midia-player.webp](navegador/midia-player.webp).

**Início:** fundo cinematográfico (capa grande, ex. "A TRAVESSIA"), barra superior com LUMNA e abas **Início, Filmes, Séries, Música, Seus vídeos**, e à direita busca, notificações, configurações, tela, Wi-Fi, data e hora; a **Emma** como orbe no canto. Em cima, **"Continuar assistindo"**: cartões com **print da última tela** do serviço, barra de progresso e "tempo restante", com botão de retomar. Perto da borda de baixo, os **atalhos dos serviços**: Netflix, YouTube, Disney+, Prime Video, Música, Vídeos locais.

**Player e troca de serviço:** ao abrir o menu de serviços, o navegador faz um **shrink** (o vídeo continua tocando, menor) e o **menu sobe** de baixo ("Reproduzindo no YouTube"; itens Início, YouTube *(Em uso)*, Netflix *(Retomar última página)*, Disney+, Paramount+, Prime Video, Mais; teclas ←→ escolher, Enter trocar, Esc fechar; microfone da Emma). Ao escolher um serviço: **pausa o player, tira um print da janela, salva o último endereço no banco**, o navegador **expande** de novo já abrindo o serviço novo, enquanto o menu **desliza para baixo** e some.

**Efeito de memória (bom):** como o serviço anterior é salvo (print + último endereço) e liberado, só um serviço fica vivo por vez; "Retomar" reabre o último endereço.

## Posição do Eric sobre licenças (2026-11-15)

O Eric entende que, pelos termos, o uso do Widevine fora do Chrome não é estritamente correto, mas a Google tolera e várias distribuições grandes fazem o mesmo há anos; não cobra nem modifica nada; o CEF baixa o CDM direto da Google na máquina do usuário; o LUMNA mostra **um termo que o usuário aceita antes de usar**; e o usuário usa o Widevine para serviços que ele mesmo paga. **Decisão dele; fica registrada.** Duas ressalvas técnicas para não esquecer: (1) isso cobre o **Widevine**, não os **codecs H.264/AAC**, cuja licença de patentes é outro assunto (o build de terceiros diz expressamente que vem sem licença de patentes); (2) o termo deve dizer que o CDM é da Google e baixado dela, e que o LUMNA não o redistribui.

## Decisões do Eric, rodada 3 (2026-11-15)

- **#13 e #19 trocados** (ver tabela acima): a Completa **tem** MPRIS/teclas de mídia, e **não tem** extensões (some o problema da loja do Chrome; o bloqueador de anúncios embutido fica como ideia à parte).
- **#35:** relatórios de falha são **nossos**; o navegador **não manda nada para a Google**. O CEF deve sair com a telemetria, relatórios de falha, Safe Browsing e atualização do Google desligados.
- **"Filmes" e "Séries" da Mídia não são catálogo**, e sim as **listas pessoais do usuário**: *assistir mais tarde*, *favoritos*, *já assisti*, e mais. Ninguém precisa de TMDB: tudo vem do que o usuário salva.
- **Função "salvar na lista"** (nova, própria do nosso navegador): ao salvar uma página, o navegador **extrai o título e a miniatura** automaticamente (lê do HTML da página, ou detecta a posição exata do elemento e recorta um print) e grava um registro: *filme/série tal, serviço tal, link tal, estado (assistir mais tarde / favorito / assistido)*.

**Notas técnicas dessa função (para o desenho):**
- **Ordem de tentativa da miniatura:** (1) `<meta property="og:image">` / `twitter:image` / dados `schema.org` (JSON-LD) da página, que dão título e capa sem depender de vídeo; (2) o atributo `poster` do `<video>`; (3) por último, **recortar um print** da região do elemento (posição via JS `getBoundingClientRect()`, captura via DevTools `Page.captureScreenshot` com `clip`).
- **Cuidado com DRM:** um print de um quadro de vídeo protegido pode sair preto em alguns caminhos de decodificação; em L3 por software costuma sair, mas **precisa de teste** (por isso a miniatura prefere as imagens da página, não o quadro do vídeo).
- **Serviços diferentes escondem coisas diferentes:** a Netflix só mostra `og:image` completo com a página do título aberta e conta logada; o YouTube tem `og:image` sempre. Haverá extratores por serviço (pequenos) e um genérico.
- **Armazenamento:** banco local (SQLite) no perfil do usuário: `itens(id, titulo, servico, url, miniatura, tipo [filme|série|vídeo|outro], estado [assistir_depois|favorito|assistido], salvo_em, ultimo_ponto)`. As abas **Filmes** e **Séries** do esboço são filtros por `tipo`; "Continuar assistindo" vem do último endereço salvo ao trocar de serviço.

## Primeira versão da Mídia (2026-11-15): funcionando no Orbit

Código em [`navegador/`](../navegador/README.md) (C++ + HTML/CSS/JS, `navegador/testes/midia.sh` automatizado). **O que já funciona** (testado no Orbit sem tela, com dois serviços locais de teste; ainda **não** com os serviços reais): janela sem moldura; início em HTML com os atalhos e **"Continuar assistindo"** (print, barra de progresso, tempo restante); abrir um serviço; **menu de serviços** (F2) com o site **encolhendo enquanto continua tocando** e o menu subindo; trocar de serviço guardando **endereço, progresso e print**; Esc volta ao vídeo; Alt+Home volta ao início; navegação por teclado (setas, Enter, Esc).

**Descobertas técnicas que valem registrar:**
1. **Chrome ou Alloy.** O CEF 154/156 tem dois estilos. A janela Chrome (a que baixa o Widevine sozinho) aceita **só 1 BrowserView Chrome**; a interface HTML entra como **Alloy**, ao lado. **Só funciona se o site (Chrome) for adicionado primeiro** e a interface depois, descendo-a para o fundo.
2. **Os dois estilos existem igualmente no CEF 154 do PC do Eric** (as assinaturas usadas são idênticas nas duas versões).
3. A janela Views em estilo Chrome sem moldura (`IsFrameless`) abre **sem a barra do Orbit** (`barra:false`): é o caminho da moldura própria para a versão Completa.

**Desvios do esboço, para o Eric aprovar:** (a) o **print é tirado quando o menu abre** (o site ainda em tamanho cheio e tocando), não no instante da escolha: assim a miniatura sai com o enquadramento certo; a pausa e o gravar continuam na escolha; (b) as abas **Filmes/Séries/Música/Seus vídeos** aparecem apagadas ("em breve"), e o fundo cinematográfico é por enquanto um degradê (falta a arte); (c) as teclas F2 (menu) e Alt+Home (início) são provisórias, até o mapeamento do controle (gamepad).

**Ainda não feito:** o botão do gamepad/controle para o menu; o Widevine no perfil do app (o CEF baixa sozinho; falta testar no app e não só no teste); os serviços reais (YouTube/Netflix/HBO Max) neste app; o sandbox ligado; a telemetria do Google totalmente desligada; o "Salvar na lista" e as listas pessoais; a versão Completa.

### Bug: "Continuar assistindo" com o print de outro serviço (2026-11-15, relato do Eric)

**Sintoma:** trocando de serviço pelo menu, o "Retomar última página" abria a página de outro serviço (ex.: clicar em Netflix mostrava HBO/Disney+). **Causa:** o print, o título e o progresso vêm da captura feita **ao abrir o menu**; se o usuário troca de serviço **antes de a página nova terminar de carregar**, a captura é da página *antiga*, e era gravada com o nome do serviço *novo*. (A hipótese inicial, de que o endereço saía errado, estava incompleta: o `GetURL()` do CEF já devolve o endereço da navegação nova.) **Correção:** a captura traz o `location.href` da própria página; só é gravada se o endereço for do domínio do serviço (cada serviço tem a sua lista de domínios); ao carregar, entradas antigas gravadas errado são descartadas. **Teste:** `navegador/testes/midia.sh` agora troca de serviço antes de a página lenta (1,5 s, sem cache) carregar e confere que nada errado é gravado; **controle negativo feito** (sem a correção, o teste falha reproduzindo o bug).

### Pendente: "flicks" na animação do shrink (relato do Eric)

No PC do Eric (com GPU), no meio da animação o site aparece no canto superior esquerdo com o tamanho antigo e o resto escuro, e só depois vai para a tela cheia. **Não reproduzido no container** (software, sem GPU): a hipótese (não confirmada) é que redimensionar uma página pesada (Disney+) a cada 16 ms faz o renderizador não acompanhar. Teste de A/B para o Eric: `LUMNA_NAV_SEM_ANIMACAO=1` (se os flicks somem, é a animação). Se for isso, a solução provável é animar uma **imagem** (o print) na interface e só redimensionar o site de verdade no começo e no fim.

### Flick da animação: resolvido com animação por imagem (2026-11-15)

O Eric confirmou no PC (com GPU): **sem animação o site cresce e diminui sem flick**; então o culpado era redimensionar a página a cada quadro. **Solução** (padrão agora): a interface anima um **print** do site e o site de verdade só é redimensionado **uma vez**, no fim. Valida no container (sem GPU): a transição leva ~0,5 s e os quadros intermediários mostram a imagem encolhendo enquanto o menu sobe; o vídeo continua tocando depois do esconde-e-mostra. **Não verificado em GPU:** se fica fluida no PC do Eric (é o que ele vai dizer). Saídas: `LUMNA_NAV_SEM_ANIMACAO=1` (instantâneo, sabidamente limpo) e `LUMNA_NAV_ANIMACAO=real` (o antigo). Teste automático: `navegador/testes/midia.sh` passa nos modos imagem e instantâneo.

### Segunda rodada de ajustes da animação (2026-11-15, relato do Eric)

O Eric testou a animação por imagem no PC: mais fluida; o menu sobe, o site encolhe e restaura bem. Pediu duas mudanças: (1) **zoom da página** enquanto o menu está aberto (80% a 90%), porque a página se reajusta ao encolher; (2) ao abrir um serviço **ainda não aberto**, a **splash devia aparecer antes**, o menu descer e o site escalar de forma suave (ele via o menu descer de uma vez, a página atual pular, e só depois a splash).

**Feito e verificado no container:** o zoom (escala 0,8; confirmado pela página: `dpr 0,8` e viewport CSS 1138 px no menu, `dpr 1` e 1264 px de volta); a splash aparece **já no tamanho encolhido, no lugar da página**, e cresce enquanto o menu desce, **junto** (10 quadros em sequência). **Causas do relato:** a interface por baixo do site era atrasada em ~1 s pelo navegador (flags de throttling), a página antiga pulava para a tela cheia por um `Aplicar()` disparado por evento de layout durante o carregamento, e carregar a página nova com o site antigo ainda visível já o fazia pular. **Efeitos colaterais corrigidos:** o `SetZoomLevel` do CEF mostra a bolha "100%" do Chrome e grava o zoom por site no perfil (por isso a escala é emulada pelo DevTools); F2 não fechava o menu nem abria durante a splash (as teclas não chegavam à interface); um alarme de 12 s da primeira abertura finalizava por engano a transição do menu. **Não verificado em GPU:** a fluidez no PC do Eric.

### Bug: a página estourava a janela com o menu aberto (2026-11-15, print do Eric)

**Sintoma (print do Paramount+):** com o menu aberto o site encolhia, mas a página aparecia **maior** que a janela (botão "ASSINAR" cortado à direita, texto cortado embaixo): o contrário de um zoom-out. **Causa:** a emulação do DevTools sem o parâmetro **`scale`** diagrama a página no viewport grande (`width`/`height` = tamanho ÷ 0,8) mas a **desenha em 1:1**. O meu teste só conferia `devicePixelRatio` e `innerWidth` (que mediam certo e enganavam) e usava páginas em `vw`/`vh`, então nunca viu. **Reproduzido no container** com uma página de pixels fixos (`px.html`: caixa 1000×500 com um canto amarelo): a caixa saía cortada, como no print. **Correção:** `width`, `height`, `deviceScaleFactor` **e `scale`** juntos (testei 4 combinações; `scale` sozinho com dpr 1 também funciona; `setPageScaleFactor` não). **Teste:** agora por **pixels**: com o menu aberto o canto amarelo tem de aparecer (2209 px a 80%: 60×60×0,64 ≈ 2304); **controle negativo** sem o `scale`: 0 px, o teste falha só nessa asserção.

### Mídia aprovada pelo Eric (2026-11-15)

O Eric testou no PC com **zoom 0,72** (`LUMNA_NAV_ZOOM_MENU=0.72`) em **Netflix, YouTube, Prime Video, Paramount+ e HBO Max**: o zoom do menu, a animação de entrada e saída e a splash ficaram "perfeitos", como ele buscava. **0,72 virou o padrão** (antes 0,8). Mídia funcionando no PC: início, serviços reais, menu com escala, splash, "Continuar assistindo".

**Falta na Mídia:** o botão do gamepad/controle para o menu (hoje F2 e Alt+Home), a arte do fundo cinematográfico, as abas Filmes/Séries/Música/Seus vídeos (listas pessoais), a opção de zoom nas configurações (hoje é variável de ambiente), o sandbox ligado, a telemetria da Google totalmente desligada, a entrega do CDM (primeiro uso), o empacotamento (PKGBUILD) e o modo da sessão (abrir a Mídia no modo Mídia do Orbit). Depois vem a versão **Completa**.

### Controle (gamepad) na Mídia e o Super+M (2026-11-15)

Pedido do Eric: no modo Mídia o PC fica no monitor/TV e a mão **não fica no mouse**; o menu é o **Super+M** (hoje F2 só em teste), o **LUMNA Connect** (celular) já existe e **falta o controle**, que também precisa **controlar o site** (clicar, rolar, tela cheia...).

**Feito e testado de ponta a ponta** (`navegador/testes/controle.py`: serviço `lumna-gamepad` real + controle falso + Orbit + navegador + página sonda; controles negativos feitos): (1) o serviço `lumna-gamepad` agora transmite os **analógicos** (esquerdo e direito, normalizados, ~60/s e sempre o último valor) e marca as direções com `analogico`; (2) o Orbit ganhou o pedido `cursor` (posição do ponteiro); (3) o navegador assina o serviço e mapeia: na interface o direcional/analógico/A/B navegam; no site o analógico esquerdo é o **ponteiro de verdade** (pelo IPC do Orbit, o mesmo caminho do mouse e do celular), o direito rola, A clica, B volta/sai da tela cheia, X Espaço, Y `f`, LB/RB Page Up/Down; **Start** abre/fecha o menu e **Select** vai ao início; (4) `lumna-navegador --menu|--inicio|--estado` (o Super+M do Orbit) e o `app_id` `lumna-navegador` da janela (antes o Orbit não a reconhecia). Tabela completa no `navegador/README.md`.

**Descobertas:** o ponteiro virtual não pode ir até a borda (a moldura invisível de redimensionar de uma janela sem moldura: a página não recebe o ponteiro ali e clicar arrasta a janela); `mouse rolar` do Orbit vale ~1 px a cada 15 unidades; o evdev não repete o analógico parado (o cliente guarda o último valor e continua andando); `SendKeyEvent` do CEF entrega `key/code/keyCode` certos ao site; a página sonda mostrou que clicar sobre a barra de rolagem não gera `click`.

**Pendente (decisões e passos):** (a) trocar o padrão do `seletor_midia` do `orbit.ini` por `lumna-navegador --menu` quando a nova Mídia substituir a antiga na sessão; (b) **testar num controle de verdade** (os eixos reais variam: Xbox/DualSense usam ABS_RX/ABS_RY, mas há controles que não); (c) sensibilidade do ponteiro e da rolagem (hoje 1700 px/s e 1800 px/s no fim do curso): ajustar ao gosto no PC; (d) **teclado na tela** para digitar em buscas (hoje o controle não digita: o `lumna-teclado` existe, falta ligar); (e) o botão Guia abre o LUMNA Jogos por padrão (`guia=nada` para a Mídia); (f) **fundo cinematográfico** na tela de início e o **catálogo** (abas Filmes/Séries com a arte `docs/navegador/midia-catalogo.webp`).
