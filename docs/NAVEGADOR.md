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
