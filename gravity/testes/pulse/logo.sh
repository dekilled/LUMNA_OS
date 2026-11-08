#!/bin/sh
# O desenho do fastfetch tem de caber numa janela do Pulse de tamanho normal: o logo, o espaço e o texto numa linha só (senão o fastfetch quebra e os blocos de cor vão
# parar no meio do desenho), e o desenho não pode ser alto demais. sh logo.sh PASTA_DA_SESSAO
s=${1:?}
python3 - "$s/fastfetch" <<'PY'
import json, re, sys
pasta = sys.argv[1]
logo = open(pasta + "/logo.txt").read().splitlines()
larg = max(len(re.sub(r"\$\d", "", l)) for l in logo)
cfg = json.loads(re.sub(r"^\s*//.*$", "", open(pasta + "/config.jsonc").read(), flags=re.M))
pad = cfg["logo"]["padding"]
chave = cfg["display"]["key"]["width"]
largura_info = 0
for m in cfg["modules"]:
    if isinstance(m, dict):
        t = re.sub(r"\x1b\[[0-9;]*m|\\u001b\[[0-9;]*m", "", m.get("format", "") or "")
        largura_info = max(largura_info, (chave + len(t)) if m.get("key") else len(t))
largura_info = max(largura_info, chave + 10)  # (o "Sessão" vem de um comando: Wayland)
total = larg + pad["left"] + pad["right"] + largura_info
erros = []
if total > 78:
    erros.append(f"o fastfetch passa de 78 colunas ({larg} do logo + {pad['left']}+{pad['right']} de espaço + {largura_info} do texto = {total}): quebra numa janela de 80")
if len(logo) > 20:
    erros.append(f"o desenho tem {len(logo)} linhas (mais de 20)")
if erros:
    print("FALHOU: " + "; ".join(erros)); sys.exit(1)
print(f"logo do fastfetch: ok ({total} colunas, {len(logo)} linhas)")
PY
