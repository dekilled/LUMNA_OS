"""O registro de testes (registro.tsv): um teste por linha, colunas separadas por TAB. Ler e gravar."""
COLUNAS = ["id", "area", "grupo", "fazer", "esperado", "estado", "data", "nota", "origem"]
ESTADOS = ["pendente", "passou", "falhou", "parcial", "obsoleto", "pulado"]
AREAS = ["Gaming", "Mídia", "Painel", "Hardware", "Desktop", "A classificar"]


def limpa(v):
    return str(v).replace("\t", " ").replace("\n", " ").strip()


def le(caminho):
    linhas = open(caminho, encoding="utf-8").read().split("\n")
    cab = linhas[0].split("\t")
    out = []
    for l in linhas[1:]:
        if not l.strip():
            continue
        p = l.split("\t")
        out.append({c: (p[i] if i < len(p) else "") for i, c in enumerate(cab)})
    return out


def grava(caminho, testes):
    with open(caminho, "w", encoding="utf-8") as f:
        f.write("\t".join(COLUNAS) + "\n")
        for t in testes:
            f.write("\t".join(limpa(t.get(c, "")) for c in COLUNAS) + "\n")
