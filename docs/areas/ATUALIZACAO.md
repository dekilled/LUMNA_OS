# Área: Atualização (integração e release)

Leia antes o [LEIAME.md](LEIAME.md).

## Para que serve

Este chat **não é de uma funcionalidade**: ele junta o que os outros chats fizeram, procura **inconsistências entre as áreas** (um chat muda uma
coisa, outro depende dela) e confere que o que o Eric vai baixar está **completo, coerente e testado** antes de ele atualizar. Quem conserta
uma inconsistência é o chat da área dona; aqui se acha, se registra e se confere de novo.

## Como o update chega ao PC do Eric

1. Push em `iso-orbit` → CI **"Pacotes da UI nova"** (`.github/workflows/pacotes.yml`, Arch Linux com wlroots 0.20) roda `scripts/publica-repo.sh`:
   compila os pacotes de `packages/` (`lumna-orbit`, `lumna-gravity`, `lumna-session`, `lumna-boot`, `lumna-classico`, `lumna-emma`...), confere com
   `scripts/confere-pacotes.sh` (arquivos que a sessão chama existem, nenhum binário com biblioteca faltando, políticas polkit válidas) e publica no
   release **`repo-orbit`** (o repositório `[lumna]` do pacman). A **versão é um carimbo** `0.1.0.AAAAMMDDhhmm`: todo push gera um update novo.
2. No PC: Painel > Sistema > Sobre > Atualizações (**só o LUMNA**, escolher pacotes ou horário marcado; ver `docs/ATUALIZACOES.md`) ou `pacman -Syu`.
3. **Orbit novo não troca o compositor em uso: sair e entrar na sessão.** Vale também para o `lumna-session` (script de sessão).
4. Push em `lumna-mobile` → CI **"Celular"** (pacotes aarch64 do celular, `[lumna-celular]`).

## Lista de conferência antes de dizer "pode atualizar"

- [ ] Os dois branches (`lumna-mobile`, `iso-orbit`) estão no mesmo commit e nenhum chat de área tem trabalho sem push.
- [ ] CI "Pacotes da UI nova" verde **no último commit** (`mcp__github__actions_list`, workflow `pacotes.yml`) e o release `repo-orbit` com os pacotes
  do mesmo carimbo (`lumna-orbit`, `-gravity`, `-session`, `-boot`, `-emma`, `-classico`).
- [ ] O que cada área colocou no "Para o Eric conferir" / no registro de testes (`python3 docs/testes/teste.py lista AREA`) está coerente com o que
  entrou no update (nada prometido que não foi empurrado).
- [ ] Mudança em arquivo que o pacote precisa **instalar** (unit, timer, drop-in, política polkit, script): está no `PKGBUILD` e, se for essencial,
  na lista do `confere-pacotes.sh`.
- [ ] Mudança que exige **sair e entrar** (Orbit, sessão) ou **reiniciar** (kernel, boot, drop-in de rede): dita ao Eric junto com o update.
- [ ] Orbit compila no 0.18 (testes locais) **e** no 0.20 (o CI): o CI verde cobre o 0.20.
- [ ] Testes de regressão das áreas tocadas passam (Orbit: `orbit/testes-encaixe/*`, `orbit/testes-foco/*`; Gravity: `gravity/testes/*.sh`).

## Estado (2026-11-17, 15:30)

- Último commit `44c39f8` nos dois branches. CI "Pacotes da UI nova" **verde** em todos os pushes do dia; release `repo-orbit` com os pacotes na versão
  `0.1.0.202611171510`. **Não há código novo esperando para ser publicado**: o que o Eric baixar agora já tem a Loja do Gaming (capas), o encaixe
  em grade, o menu de clique direito do X11 e o foco.

## Inconsistências achadas (2026-11-17)

1. **CI "Celular" falha em todo push desde pelo menos 2026-11-16 (12+ execuções)**: o passo "Baixa a Emma" recebe `Authentication failed` do GitHub.
   O segredo `EMMA_REPO_TOKEN` do repositório está **inválido ou expirado**. Só afeta os pacotes do celular (aarch64) e a Emma desse lado; o desktop
   não depende. **Só o Eric resolve:** gerar um token novo (acesso de leitura ao `dekilled/NewHarnessV2`) e trocar em Settings > Secrets > Actions >
   `EMMA_REPO_TOKEN`. Enquanto isso o workflow tem de cair no ramo "sem token" (a Emma fica com o pacote já publicado), mas hoje ele **falha** em vez
   de seguir: o `if [ -n "$EMMA_REPO_TOKEN" ]` entra porque o segredo existe. Ajuste possível no `celular.yml`: se o `git clone` falhar, avisar e seguir sem a Emma.
2. **O navegador CEF da Mídia nova não está em nenhum pacote**: o Eric o compila à mão (`CEF_ROOT=... sh navegador/dev.sh`, CEF extraído de
   `scripts/teste-cef-codecs.sh`). O update **não leva** a Mídia nova nem as correções dela (popup de login, páginas de 6, controle); e o `seletor_midia`
   do `orbit.ini` ainda aponta para a Mídia antiga (`lumna-midia tela seletor`). Decisão estrutural do Eric (o CEF pesa centenas de MB; empacotar
   no CI ou baixar na primeira vez, como o Widevine?). Pertence à área **Mídia**; registrado lá.
3. **A página "Onde estamos" está desatualizada** (`docs/status/gera.py`, `HOJE = "9 de novembro de 2026"`, dados de 14/11). Pertence à área **Testes**.
4. `lumna-shell` e `lumna-hyprbars` continuam no release com versão de 2026-11-07 (restos do LUMNA antigo em Vue e da sessão Hyprland de emergência):
   inofensivos, mas **não saem do pacman** sozinhos; decidir se continuam (a sessão "clássico · emergência" ainda os usa).

## Recebido de outras áreas

(vazio: cada chat de área anota aqui o que mudou em código compartilhado, para a conferência do release)
