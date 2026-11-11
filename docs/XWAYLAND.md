# XWayland no Orbit

A Steam (e os jogos do Proton, e muitos apps antigos) são apps X11. O Orbit só falava Wayland; agora ele roda o XWayland.

## Como funciona
- O Orbit cria o servidor X **sob demanda** (`wlr_xwayland_create(..., lazy = true)`): ele só sobe quando o primeiro app X11 se conecta. Sem app X11 não custa nada.
- O `DISPLAY` vai no ambiente do Orbit **antes** de abrir os apps da sessão (o `-S`), então tudo o que a sessão abre (lumna-jogos, a Steam, terminais) já o encontra.
- Cada janela X11 vira uma `orbit_view` normal (o `xsurface` no lugar do `toplevel` do xdg). O que muda entre os dois mundos está nas funções `orbit_view_*` de `orbit/view.c` (app_id, título, fechar, tamanho, estado, foco); o resto do Orbit não sabe a diferença: modos, monitores, tela cheia, dock, Alt+Tab, `orbit-msg janelas`.
- O app_id de uma janela X11 é a **classe** dela (`WM_CLASS`): a Steam é `steam`, os jogos do Proton `steam_app_<id>`. O Orbit já manda essas classes para o modo Gaming (prefixo `steam`).
- Menus, dicas e janelas sem moldura (override-redirect) ficam onde o app mandou, por cima de tudo, sem foco nem arranjo (`orbit/xwl.c`).
- Sem XWayland na wlroots (`WLR_HAS_XWAYLAND = 0`) o Orbit compila e roda igual, só com apps Wayland, e avisa no log.

## O pacote
O `lumna-orbit` passou a depender de `xorg-xwayland` e `libxcb`.

## O que ainda não tem
- A barra de título do Orbit nas janelas X11 (hoje elas não têm barra do servidor; arrastar com Alt funciona).
- Teste com a Steam e jogos reais: foi provado só com clientes X11 de teste.

## Como testar sem a Steam
Com o Orbit rodando, `DISPLAY` já está no ambiente: abra `xterm` ou `xeyes` e confira `orbit-msg janelas`.
