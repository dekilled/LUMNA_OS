#!/usr/bin/env python3
"""Um servidor local que responde como a loja da Steam, o ProtonDB e a API da Steam, com respostas reais gravadas (steam_api/). Anota cada pedido em LOG.
servidor_steam_falso.py PORTA LOG"""
import http.server, json, os, sys, urllib.parse

aqui = os.path.join(os.path.dirname(os.path.abspath(__file__)), "steam_api")
porta, log = int(sys.argv[1]), sys.argv[2]
CHAVE = "ABCDEF0123456789ABCDEF0123456789"


class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def resp(self, corpo, codigo=200, tipo="application/json"):
        if isinstance(corpo, str):
            corpo = corpo.encode()
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.end_headers()
        self.wfile.write(corpo)

    def do_GET(self):
        open(log, "a").write(self.path + "\n")
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        arq = lambda n: open(os.path.join(aqui, n), "rb").read()
        if u.path == "/api/featuredcategories":
            return self.resp(arq("featured.json"))
        if u.path == "/api/appdetails":
            ids = q.get("appids", [""])[0]
            if ids == "620":
                corpo = arq("appdetails_620.json")
                # o teste não pode depender do espaço livre da máquina: o Portal 2 pede 8 GB de verdade; aqui, 100 MB (a janela de instalação trata o "não cabe" à parte)
                return self.resp(corpo.replace("Requer 8 GB de espaço livre".encode(), "Requer 100 MB de espaço livre".encode()))
            return self.resp(json.dumps({ids: {"success": False}}))
        if u.path == "/search/results/":
            if q.get("term", [""])[0] == "nada-existe":
                return self.resp(json.dumps({"success": 1, "results_html": "", "total_count": 0, "start": 0}))
            return self.resp(arq("search.json"))
        if u.path.startswith("/api/v1/reports/summaries/"):
            if u.path.endswith("/620.json"):
                return self.resp(arq("proton_620.json"))
            return self.resp("", 404)
        if u.path == "/IPlayerService/GetOwnedGames/v1/":
            if q.get("key", [""])[0] != CHAVE:
                return self.resp("Forbidden", 403, "text/plain")
            return self.resp(json.dumps({"response": {"game_count": 2, "games": [{"appid": 620}, {"appid": 730}]}}))
        if u.path.endswith(".jpg"):
            # uma imagem de verdade (um degradê na cor do jogo), para o app poder desenhar
            import io, re
            from PIL import Image
            m = re.search(r"/apps/(\d+)/", u.path)
            n = int(m.group(1)) if m else 1
            cor = ((n * 53) % 200 + 30, (n * 97) % 200 + 30, (n * 31) % 200 + 30)
            img = Image.new("RGB", (160, 90), cor)
            for y in range(90):
                for x in range(160):
                    img.putpixel((x, y), (min(255, cor[0] + x // 4), cor[1], max(0, cor[2] - y)))
            b = io.BytesIO()
            img.save(b, "PNG")
            return self.resp(b.getvalue(), 200, "image/jpeg")
        return self.resp("nao", 404, "text/plain")


http.server.ThreadingHTTPServer(("127.0.0.1", porta), H).serve_forever()
