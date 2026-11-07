# Plano: o LUMNA novo (Orbit + Gravity) numa ISO de verdade

Escrito em 6 de novembro de 2026. Objetivo: você instalar o LUMNA numa máquina (primeiro VM, depois o PC),
com **Orbit + Gravity no lugar do Hyprland + shell Tauri**, e testar o sistema inteiro de verdade.
Ordem que você pediu: **(1) ISO com a UI nova → (2) os programas combinados → (3) UI mobile.**

## Decisões já tomadas (6 de novembro)

1. **A primeira ISO leva uma sessão de emergência**: o Hyprland antigo, escolhível no login, até o Orbit provar que sobe
   numa tela de verdade. Depois que você validar, o antigo sai (a tag guarda tudo).
2. **Instalador e agente do polkit novos, em Gravity**, no mesmo visual do resto, em cima do `lumna-install`.
   Só disco inteiro por enquanto.

## O que existe hoje (conferido no código)

- A ISO (`iso/`, `scripts/build-iso.sh`, GitHub Actions) monta **Hyprland + `lumna-shell` (Tauri/Vue)**.
  Pacotes: `lumna-shell`, `lumna-session`, `lumna-hyprbars`, `lumna-emma`, `lumna-mobile`.
- O Orbit morava noutro repositório (`LUMNA_COMPOSITOR`); **agora é o `orbit/` deste repositório**, com o histórico dele (58 commits).
- O Gravity (`gravity/`, ~30 apps `lumna-*` em C) não tem pacote nem entra na ISO.
- Já existem peças da sessão nova: `lumna-orbit-session`, `lumna-orbit.desktop`, `lumna-entrada-greeter`,
  `lumna-entrada` (a tela de login), `lumna-ocioso`, `lumna-inicio`, ajudantes root + políticas polkit.
- **O Orbit nunca rodou num DRM real**: só headless e aninhado (você já rodou aninhado no seu Arch, com wlroots 0.20). É o maior risco do plano (tela preta no boot).
- Neste ambiente não tenho KVM nem QEMU x86: **eu monto a ISO (pelo GitHub Actions), mas quem dá o boot é você.**

## O que o shell antigo faz e o novo ainda não (lacunas)

Bloqueiam a ISO instalável (têm que existir antes do primeiro teste):

| Lacuna | Hoje | Plano |
|---|---|---|
| **Instalador** (`InstallerApp.vue` + `installer.rs`) | só no shell antigo | app novo em Gravity (`lumna-instalador`) em cima do `session/lumna-install`, que já funciona |
| **Agente do polkit** (a janela de senha de admin, `polkit.rs`) | só no shell antigo | `lumna-polkit` em Gravity. **Sem ele, o `pkexec` do Painel (Entrada, Idioma, Usuários) falha** |
| **Sessão e login** | Hyprland + greeter Tauri | `lumna-orbit-session` + `lumna-entrada-greeter` (já existem) no greetd |
| **Portais** (escolher arquivo, compartilhar tela) | `xdg-desktop-portal-hyprland` | trocar por `xdg-desktop-portal-wlr` + `-gtk` e conferir no Firefox |
| **Arquivos** (gerenciador, lixeira, pendrives) | `FilesApp.vue` | temporário: um gerenciador pronto (ex.: Thunar) até o nosso existir |
| **Loja** (pacman/Flatpak/AUR) | `StoreApp` | temporário: Painel > Aplicativos já instala/atualiza; a Loja vem na fase 2 |
| **Ações da Emma** (`actions.rs`, `automation.rs`) | só no shell antigo | **auditar** o que o serviço `lumna-emma` já cobre e portar o resto |

Podem esperar (viram a fase 2): Music, Galeria nova, Biblioteca Gaming, Navegador, Player de vídeo,
teclado na tela, Connect gerenciando o aparelho, páginas VPN/Ponto de acesso/Receber arquivos.

## Fase 0 — Auditoria ✅ feita: ver [AUDITORIA-UI.md](AUDITORIA-UI.md)

1. Inventário completo: cada tela/função do shell antigo → "Gravity já tem / falta / descartada".
2. Conferir o que o `lumna-emma` cobre das ações da Emma.
3. Conferir dependências de runtime de cada binário (`ldd`) para fechar o `depends` do pacote.
4. Entregável: tabela no fim deste arquivo, com o que bloqueia a ISO e o que não.

## Fase 1 — Empacotar a UI nova ✅ escrita (falta o CI compilar de verdade)

1. **`lumna-orbit`**: PKGBUILD que compila o `orbit/` do próprio repositório. O Arch de hoje só tem **wlroots 0.20** (pacote `wlroots0.20`); o Orbit já roda aninhado nele na sua máquina.
2. **`lumna-gravity`**: compila a biblioteca e todos os `lumna-*`, instala binários, `.desktop`, ícones, assets,
   extensão do Firefox e as políticas polkit.
3. **`lumna-session`** enxuto: tira Hyprland, hyprlock, hypridle e hyprbars; entra a sessão do Orbit como padrão,
   `/etc/greetd/config.toml` apontando para `lumna-entrada-greeter`, configurações padrão em `/etc/skel`.
4. Portais e dependências novas no `depends` (itens acima).
5. Teste de pacote no CI: `pacman -Qkk`, `ldd` sem "not found", `desktop-file-validate`, `xmllint` nas políticas.

## Fase 2 — Instalador e polkit (Gravity) ✅ escrita e testada com fakes

1. `lumna-polkit` (agente de autenticação): senha do usuário, mesmo visual do bloqueio, sobe com a sessão.
2. `lumna-instalador`: idioma e teclado → disco (com aviso de apagar) → usuário → resumo → progresso (as linhas
   `@@ %` do `lumna-install`) → reiniciar. **Só disco inteiro**, como hoje; particionamento manual fica no backlog.
3. Sessão **live** do pendrive: entra sozinha, com o atalho "Instalar LUMNA OS" na área de trabalho/dock.
4. Ajustar o `lumna-install` para gravar a sessão nova no sistema instalado (greetd, skel, defaults).

## Fase 3 — A ISO e a remoção da UI antiga (em duas etapas, para não ficar sem volta)

1. **ISO de teste (tag `orbit-*`, canal próprio `repo-orbit`/`teste-orbit`)**: a sessão padrão é a nova (Orbit); o Hyprland antigo vai junto só como
   sessão de emergência escolhível no login. O código antigo **continua no repositório**. Marco o estado atual com a tag `ui-antiga-hyprland`.
2. Você testa. **Só depois do seu "pode apagar"** entra o PR que remove: `shell/`, `crates/lumna-hypr`, `lumna-ctl`,
   `hyprland.lua`, `greeter.lua`, `lumna-hyprbars`, scripts e docs do Hyprland. A tag guarda tudo.
3. Pacotes: o CI compila só Orbit e Gravity a cada push na branch `iso-orbit` (`pacotes.yml`, minutos). A ISO sai ao empurrar
   uma tag `orbit-*` (`iso.yml`). A `main` (UI antiga) e o repositório `[lumna]` dela **não são tocados**.

## Fase 4 — Testar de verdade (em degraus)

1. **CI (eu faço):** a ISO monta, os pacotes passam nos testes da Fase 1, as configs são válidas.
2. **VM com KVM (você):** `scripts/run-vm.sh`. Já é DRM de verdade, greetd, logind, polkit e D-Bus reais: troca quase
   todos os meus "fakes" por coisa real. Roteiro: boot → login → sessão → instalar → reiniciar → entrar no instalado.
3. **PC de verdade (você):** o que VM não mostra: **gama (luz noturna), DPMS, bateria/UPower, suspensão, botão de
   ligar, vários monitores, GPU AMD/Intel**, e o desempenho.
4. Eu escrevo `docs/TESTES-ISO.md`: um roteiro por página do Painel e por função (o que fazer, o que deve acontecer),
   para você só responder passou/falhou. O Painel > Sistema > Diagnóstico junta os logs para você me mandar.

## Riscos e como seguro cada um

- **Tela preta no boot (Orbit em DRM real, nunca testado):** primeira ISO leva o Hyprland antigo como sessão de **emergência**
  até o Orbit provar que sobe. *(decisão 1)*
- **wlroots 0.20 em DRM real:** se der problema, fixo o pacote em outra versão do repositório.
- **Sem agente polkit:** é bloqueante, entra na Fase 2 antes da ISO.
- **Emma sem as ações do shell antigo:** auditoria na Fase 0; o que faltar entra antes da ISO se for essencial.
- **Perder o antigo:** tag `ui-antiga-hyprland` + remoção só com o seu OK.

## Fase 5 — Programas (depois da ISO validada)

Ordem sugerida (a confirmar com você): Arquivos → Loja → Teclado na tela (+ input-method nos apps de fora) →
Player de vídeo → Music → Navegador → Biblioteca Gaming → Connect gerenciando o aparelho → páginas
VPN / Ponto de acesso / Receber arquivos. StoryTeller segue adiado.

## Fase 6 — UI mobile (OnePlus 6T)

Por último. Antes, uma conversa e um desenho juntos (como a regra da casa): Orbit + Gravity em modo celular
(gestos, teclado, tela dividida) ou outra abordagem. Nenhuma linha de código antes disso.

## Resumo da ordem

`0 Auditoria → 1 Pacotes → 2 Polkit + Instalador → 3 ISO de teste → 4 Testes (CI → VM → PC) → [seu OK] remover o antigo → 5 Programas → 6 Mobile`
