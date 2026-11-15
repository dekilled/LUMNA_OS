# Servidor de teste da Mídia: serve ./paginas e atrasa 1,5 s tudo que começa com /lento/ (para simular um serviço que demora a carregar).
import functools, http.server, sys, time
porta, raiz = int(sys.argv[1]), sys.argv[2]
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/lento/'):
            time.sleep(1.5)
            self.path = self.path[len('/lento'):]
        super().do_GET()
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')  # sem cache: o atraso de /lento/ tem de valer em toda visita
        super().end_headers()
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', porta), functools.partial(H, directory=raiz)).serve_forever()
