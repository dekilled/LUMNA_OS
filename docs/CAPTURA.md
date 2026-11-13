# Captura de tela (lumna-captura)

**Uma captura de cada vez:** a escolha (e a contagem de 3 s da gravação) abre **uma vez só**. Apertar Print duas vezes, ou apertar Print durante a contagem (antes de haver uma gravação para parar), não abre outra escolha por cima: a segunda chamada some sem fazer nada. A trava é `$XDG_RUNTIME_DIR/lumna-captura-escolha.pid` (criada com `O_EXCL`; um pid que já morreu é ignorado; a contagem e a gravação herdam a trava de quem as abriu). Com a gravação rodando, Print continua parando-a.

**Todas as telas:** com mais de um monitor ligado, a barra da escolha ganha o botão **Todas as telas** (só em Foto): captura cada monitor e cola as imagens **lado a lado**, da esquerda para a direita, numa imagem só, e abre a prévia. Telas de alturas diferentes ficam alinhadas pelo topo (o que sobra embaixo é preto). Também existe `lumna-captura --todas` (direto para a prévia, sem a escolha) para quem quiser um atalho.

Atalhos (no Orbit; mudam em `orbit.ini` ou no Painel > Teclado > Atalhos): **Print** escolhe a captura, **Shift+Print** captura a tela inteira
direto para a prévia, **Ctrl+Print** escolhe e grava. Durante uma gravação, **Print** para.

- **Escolher** (`lumna-captura`): congela a tela do monitor do ponteiro e se arrasta a área. Alças nos cantos e meios ajustam o tamanho, arrastar por dentro
  move, as setas movem 1 px (Shift: 10 px). Modos **Área / Tela / Janela** (a janela clicada vira uma área ainda ajustável) e **Foto / Vídeo**. Enter captura, Esc cancela.
- **Prévia** da foto: Copiar (Ctrl+C), Salvar (Ctrl+S, em `Imagens/Capturas`), Salvar como (caminho e nome), Enviar (abrir com outro programa; o celular fica
  reservado para o LUMNA Connect) e Descartar. A captura mora em `$XDG_RUNTIME_DIR/lumna-capturas` até ser salva: **fechar sem salvar a apaga**.
- **Gravar**: contagem de 3 s na área, e o `wf-recorder` grava. A barra com o tempo e o botão Parar só aparece se não cair dentro do que se grava (senão sairia
  no vídeo); numa gravação da tela toda, para-se com Print (ou `lumna-captura --parar`). Depois vem a prévia da gravação (Reproduzir, Copiar caminho, Salvar em
  `Vídeos/Capturas`, Salvar como, Enviar, Descartar), com a mesma regra de descarte.

**Som da gravação:** na barra, em Vídeo, uma segunda linha escolhe **Sem som**, **Do sistema** (o que sai pelas caixas: o monitor da saída padrão, pelo `pactl`) ou
**Microfone** (a fonte padrão); a escolha fica em `~/.config/lumna/captura.ini`. Os dois ao mesmo tempo ainda não (o `wf-recorder` grava um dispositivo por vez).

**Editor (na prévia da foto, e também na confirmação "Mostrar à Emma", para borrar um segredo antes de enviar):** Seta, Retângulo, Texto, Borrar (pixelado), Caneta; seis cores e
três espessuras; **Desfazer** (Ctrl+Z, quantas vezes quiser). Cada edição vale na hora e vai para um arquivo novo: Copiar, Salvar e Enviar usam sempre a versão editada.
Editar depois de salvar cria uma versão nova ainda não salva; o arquivo que você salvou nunca é tocado, e as versões intermediárias somem ao fechar.

Precisa de: `grim` (fotos), `wf-recorder` (vídeo), `wl-clipboard` (Copiar) e, opcionalmente, `ffmpeg` (um quadro do vídeo na prévia). O Orbit já fala `wlr-screencopy`.
O motor é externo de propósito; a interface (área, alças, barra, prévia) é do Gravity. Ainda não grava o áudio (o `wf-recorder` aceita `-a`).
