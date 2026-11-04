# Captura de tela (lumna-captura)

Atalhos (no Orbit; mudam em `orbit.ini` ou no Painel > Teclado > Atalhos): **Print** escolhe a captura, **Shift+Print** captura a tela inteira
direto para a prévia, **Ctrl+Print** escolhe e grava. Durante uma gravação, **Print** para.

- **Escolher** (`lumna-captura`): congela a tela do monitor do ponteiro e se arrasta a área. Alças nos cantos e meios ajustam o tamanho, arrastar por dentro
  move, as setas movem 1 px (Shift: 10 px). Modos **Área / Tela / Janela** (a janela clicada vira uma área ainda ajustável) e **Foto / Vídeo**. Enter captura, Esc cancela.
- **Prévia** da foto: Copiar (Ctrl+C), Salvar (Ctrl+S, em `Imagens/Capturas`), Salvar como (caminho e nome), Enviar (abrir com outro programa; o celular fica
  reservado para o LUMNA Connect) e Descartar. A captura mora em `$XDG_RUNTIME_DIR/lumna-capturas` até ser salva: **fechar sem salvar a apaga**.
- **Gravar**: contagem de 3 s na área, e o `wf-recorder` grava. A barra com o tempo e o botão Parar só aparece se não cair dentro do que se grava (senão sairia
  no vídeo); numa gravação da tela toda, para-se com Print (ou `lumna-captura --parar`). Depois vem a prévia da gravação (Reproduzir, Copiar caminho, Salvar em
  `Vídeos/Capturas`, Salvar como, Enviar, Descartar), com a mesma regra de descarte.

Precisa de: `grim` (fotos), `wf-recorder` (vídeo), `wl-clipboard` (Copiar) e, opcionalmente, `ffmpeg` (um quadro do vídeo na prévia). O Orbit já fala `wlr-screencopy`.
O motor é externo de propósito; a interface (área, alças, barra, prévia) é do Gravity. Ainda não grava o áudio (o `wf-recorder` aceita `-a`).
