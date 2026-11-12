# clientes X11 de teste: "jan" cria uma janela normal colorida (classe dada); "or" cria uma override-redirect (menu) numa posição
import sys, time, os
from Xlib import X, display, Xatom
d = display.Display(); s = d.screen()
modo = sys.argv[1]
cor = int(sys.argv[2], 16)
if modo == "jan":
    classe = sys.argv[3]
    w = s.root.create_window(0, 0, 400, 300, 0, s.root_depth, X.InputOutput, X.CopyFromParent, background_pixel=cor, event_mask=X.KeyPressMask | X.ExposureMask)
    w.set_wm_class(classe.lower(), classe); w.set_wm_name("X " + classe)
    w.change_property(d.intern_atom("_NET_WM_PID"), Xatom.CARDINAL, 32, [os.getpid()])
    w.map(); d.flush()
    # "redim LxA": como um jogo que troca a resolução por conta própria, pede esse tamanho logo ao abrir e de novo um segundo depois
    redim = [a for a in sys.argv[5:] if "x" in a]
    if redim:
        rw, rh = [int(v) for v in redim[0].split("x")]
        import threading
        def pedir():
            time.sleep(0.15); w.configure(width=rw, height=rh); d.flush()
            time.sleep(1.2); w.configure(width=rw, height=rh); d.flush()
        threading.Thread(target=pedir, daemon=True).start()
    # "ativar=S": S segundos depois de abrir, pede o foco pelo _NET_ACTIVE_WINDOW, como a Steam faz ao abrir um jogo
    ativar = [a for a in sys.argv[5:] if a.startswith("ativar=")]
    if ativar:
        import threading
        from Xlib.protocol import event as xev
        def pedir_foco():
            time.sleep(float(ativar[0].split("=")[1]))
            ev = xev.ClientMessage(window=w, client_type=d.intern_atom("_NET_ACTIVE_WINDOW"), data=(32, [1, X.CurrentTime, 0, 0, 0]))
            s.root.send_event(ev, event_mask=X.SubstructureRedirectMask | X.SubstructureNotifyMask); d.flush()
        threading.Thread(target=pedir_foco, daemon=True).start()
    while True:
        e = d.next_event()
        if e.type == X.KeyPress:
            open(sys.argv[4], "a").write("tecla %d\n" % e.detail)
else:
    x, y = int(sys.argv[3]), int(sys.argv[4])
    w = s.root.create_window(x, y, 160, 90, 0, s.root_depth, X.InputOutput, X.CopyFromParent, background_pixel=cor, override_redirect=True)
    w.map(); d.flush()
    while True:
        time.sleep(1)
