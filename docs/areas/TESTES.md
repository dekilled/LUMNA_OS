# Área: Testes (a filtragem)

Leia antes o [LEIAME.md](LEIAME.md).

## Para que serve

O Eric tem centenas de testes para fazer e estava deixando coisa para trás. Este chat **não programa**: ele organiza os testes, conduz as
sessões de teste com o Eric (um por um ou em blocos curtos), registra o resultado de cada um e, no fim de cada rodada, atualiza a página
**"LUMNA OS, onde estamos"**. Falha encontrada aqui vira pendência no arquivo da área dona (seção "Recebido de outras áreas"), e quem conserta
é o chat daquela área.

## As peças

- **O registro:** `docs/testes/registro.tsv`, um teste por linha: `id`, `area`, `grupo`, `fazer` (o que fazer), `esperado` (o que deve
  acontecer), `estado`, `data`, `nota`, `origem`. **Ninguém edita na mão**: tudo pelo `docs/testes/teste.py`.
- **Estados:** `pendente` (falta testar), `passou`, `falhou`, `parcial` (funciona com ressalva: a nota diz qual), `obsoleto` (o teste não vale
  mais: a função mudou ou saiu), `pulado` (não dá para testar agora: a nota diz por quê).
- **Comandos:**
  - `python3 docs/testes/teste.py resumo`: quantos por área e estado.
  - `python3 docs/testes/teste.py lista Gaming` (só os pendentes) ou `lista Gaming falhou` / `lista Gaming todos`.
  - `python3 docs/testes/teste.py ver T2.270`.
  - `python3 docs/testes/teste.py marcar T2.270 passou "nota opcional"` (a data é a de hoje).
  - `python3 docs/testes/teste.py novo Desktop "Grupo" "O que fazer" "O que deve acontecer"` (ids novos: G-, M-, P-, H-, D-).
  - `python3 docs/testes/teste.py area T2.12 Desktop` (mudar de área).
  - `python3 docs/testes/teste.py pagina`: gera `docs/testes/pagina/` (index.html + testes.json).
- **A página de acompanhamento:** artefato **"LUMNA OS, testes"** (slug `bf953a49-8f5a-4784-90d1-2ab965c8799e`): um cartão por área com a barra
  do que falta, passou, falhou etc.; filtros por área e estado; busca. Republicar com o `publish_files` (os dois arquivos da pasta `pagina/`,
  mesmo slug, `base_version` do último publish).
- **A página "Onde estamos":** `docs/status/gera.py` → `progresso.html`, artefato **"LUMNA OS, onde estamos"** (slug
  `34c85add-aaac-4ae2-b82f-4213a485ed5c`). Regras em `docs/status/LEIAME.md` (confirmado = o Eric viu no PC; testado só aqui = simulado).

## De onde veio

Os **584** itens do roteiro antigo (`docs/TESTES-ISO.md`, ids `T<seção>.<n>`) foram importados como `pendente`, com a área adivinhada pela
seção e por palavras-chave (`docs/testes/importar.py`; não rodar de novo, apaga os resultados). **44 ficaram "A classificar"** e as áreas
adivinhadas podem estar erradas. Mais 26 testes novos (ids G-, M-, P-, H-, D-) do trabalho de 2026-11-16/17, já com o que o Eric confirmou.

## O roteiro de trabalho deste chat

1. **Filtrar (primeiro, sem o Eric testar nada):** área por área, ler os pendentes e: classificar os "A classificar"; corrigir a área errada;
   marcar `obsoleto` o que não vale mais (função mudou: ex. Hyprland → Orbit, Mídia antiga → nova, encaixe novo); juntar repetidos (marcar o
   repetido `obsoleto` com a nota "igual a T…"); reescrever o que mudou (novo teste + o velho `obsoleto`). Perguntar ao Eric só o que o código
   e os docs não respondem.
2. **Marcar o que o Eric já confirmou** em conversas e docs (o "Confirmado no PC" de cada área, o `docs/status/gera.py`, o `docs/IDEIAS.md`),
   com a nota "confirmado em <data> (fonte)".
3. **Sessões de teste com o Eric:** um bloco por vez (5 a 10 testes, da mesma área e do mesmo lugar do sistema), na ordem em que ele usa o PC;
   cada teste com o comando de diagnóstico quando falhar. Registrar na hora.
4. **Fechar a rodada:** `teste.py pagina` e republicar "LUMNA OS, testes"; levar para o `gera.py` o que mudou (confirmados, o que falta testar,
   as falhas) e republicar "Onde estamos"; passar as falhas para o arquivo da área dona; commit e push (regras do LEIAME).

## Estado

- Registro criado em 2026-11-17: 610 testes (18 já com resultado). Página "LUMNA OS, testes" publicada.
- "Onde estamos" está desatualizada desde 2026-11-14 (o `HOJE` do `gera.py` ainda diz 9 de novembro).

## Pendências

1. A filtragem inteira (passo 1), começando pelos 44 "A classificar".
2. Pôr na "Onde estamos" um bloco com a contagem do registro por área (ler o `registro.tsv` no `gera.py`) e o link da página de testes.

## Recebido de outras áreas

(vazio)
