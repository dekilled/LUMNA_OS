# O servidor da Emma

O backend Python da Emma (conversas, skills, memória, RAG), copiado do NewHarnessV2 (o commit está em `ORIGEM`). Faz parte do LUMNA: o
serviço `lumna-emma` liga `iniciar` sozinho quando o servidor não está no ar, e o religa se cair. Ninguém precisa abrir o harness.

- **Sem login:** ao ligar, o servidor cria um token único em `~/.config/lumna/emma/chave` (só o usuário lê) e o LUMNA entra com ele.
- **Primeira vez:** `iniciar` cria o ambiente Python em `~/.local/share/lumna/emma-servidor/venv` (precisa de internet só nessa vez) e os
  segredos em `~/.config/lumna/emma/servidor.env`. Os dados (conversas, provedores) ficam em `~/.local/share/lumna/emma-servidor`.
- **Atualizar:** copie de novo os `.py` e o `requirements.txt` de `NewHarnessV2/backend` para `backend/` e anote o commit em `ORIGEM`.
