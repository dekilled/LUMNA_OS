# Servidor de teste da Mídia: serve ./paginas e atrasa 1,5 s tudo que começa com /lento/ (para simular um serviço que demora a carregar).
import functools, http.server, sys, time
porta, raiz = int(sys.argv[1]), sys.argv[2]
class H(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.startswith('/eco-ua'):  # devolve o User-Agent que CHEGOU no pedido e o que a própria página lê (a Mídia troca os dois para os serviços de TV)
            ua = (self.headers.get('User-Agent') or '').replace('"', "'").replace('|', '/')
            html = ('<!doctype html><meta charset=utf-8><title>UA</title><body style="background:#223;color:#fff">eco do User-Agent<script>'
                    'window.__k="-";const t=()=>{document.title="UA|http=" + HDR + "|js=" + navigator.userAgent.replace(/[|]/g,"/") + "|nav=" + (window.__lumnaNav ? 1 : 0) + "|k=" + window.__k;};'
                    'addEventListener("keydown",e=>{window.__k=e.key;t();});t();</script>').replace('HDR', '"' + ua + '"').encode()
            self.send_response(200)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.send_header('Content-Length', str(len(html)))
            self.end_headers()
            self.wfile.write(html)
            return
        if self.path.startswith('/lento/'):
            time.sleep(1.5)
            self.path = self.path[len('/lento'):]
        super().do_GET()
    def end_headers(self):
        self.send_header('Cache-Control', 'no-store')  # sem cache: o atraso de /lento/ tem de valer em toda visita
        super().end_headers()
    def log_message(self, *a): pass
http.server.ThreadingHTTPServer(('127.0.0.1', porta), functools.partial(H, directory=raiz)).serve_forever()
