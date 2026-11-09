# Auditoria: o shell antigo (Tauri + Hyprland) × o desktop novo (Orbit + Gravity)

6 de novembro de 2026. Parte da [Fase 0 do plano da ISO](PLANO-ISO.md). Conferido lendo o código dos dois lados.

## Tela por tela

| Função do shell antigo | No desktop novo | Situação |
|---|---|---|
| Topbar, bandeja, central de avisos | `lumna-barra`, `servicos/bandeja`, `lumna-avisos` | ✅ |
| Dock, início/lançador, trocador de janelas | `lumna-dock`, `lumna-lancador`, Alt+Tab no Orbit | ✅ |
| Menu de energia | `lumna-energia` | ✅ |
| Modos (Desktop/Gaming/Mídia), seletor, animações | Orbit + `lumna-modos` | ✅ (Gaming ainda sem biblioteca) |
| Bloqueio e ocioso | `lumna-bloqueio`, `lumna-ocioso` | ✅ |
| Tela de login | `lumna-entrada` (dentro de um Orbit: `lumna-entrada-greeter`) | ✅ |
| Configurações | `lumna-painel` | ✅ (VPN, Ponto de acesso e Receber arquivos nem têm página) |
| Captura e gravação, editor de anotações | `lumna-captura` | ✅ |
| Connect (celular), transferência | `lumna-connect`, `lumna-transferencia` | ✅ |
| Emma: painel, chat, app, voz, Oi Emma | `lumna-emma`, `-chat`, `-app` | ✅ (ver "Ações da Emma") |
| Mídia (Início, serviços, TV) | `lumna-midia` + extensão do Firefox | ✅ (a extensão não está assinada) |
| Galeria | `lumna-galeria` | ✅ |
| Papel de parede | `lumna-papel` | ✅ |
| **Instalador** (`installer.rs`) | `lumna-instalador` (seis passos, em cima do `lumna-install`) | ✅ feito na fase 2 (testado com o `lumna-install` de mentira) |
| **Agente do polkit** (`polkit.rs`, `AuthDialog.vue`) | `lumna-polkit` + `lumna-polkit-janela` | ✅ feito na fase 2 (testado com um polkit de mentira, nos dois modos do helper) |
| Arquivos (`files.rs`, `FilesApp.vue`) | `lumna-atlas` (o Atlas, ver [ATLAS.md](ATLAS.md)) | ✅ no lugar do Thunar (só provado no Orbit sem tela) |
| Loja (`store.rs`) | Painel > Aplicativos instala/atualiza; falta a Loja | ❌ fase 5 |
| Music e Qobuz (`music.rs`, `qobuz.rs`) | — | ❌ fase 5 |
| Biblioteca Gaming (`games.rs`, `vdf.rs`, `pad.rs`) | — | ❌ fase 5 |
| Teclado na tela (`touchkeys.rs`, `keyboard.rs`) | `lumna-teclado` | ✅ (ver [TECLADO.md](TECLADO.md)); falta a vibração (`haptics.rs`) |
| Gestos de borda e rotação | — | ❌ fase 5 |
| Impressoras (`printers.rs`) | — | ❌ (o CUPS roda; falta a página) |
| Celular (`mobile.rs`, `mobilelock.rs`, `phone.rs`) | — | adiado: UI mobile é a fase 6 |
| Barra de título das janelas (`lumna-hyprbars`, `WindowBar.vue`) | `orbit/deco.c`: o Orbit desenha a barra (xdg-decoration), no visual dos apps nativos | ✅ feito (testado com o foot e o mouse virtual; Firefox e Blender só no PC de verdade) |

## Ações da Emma (`actions.rs` no shell antigo × `servicos/emmaa.c`)

O shell antigo expunha ~43 ações para a Emma controlar o PC. O serviço novo cobre: Mídia (estado, abrir, fechar,
início, seletor, controle, navegar, digitar), volume, modo (atual, trocar, trocar telas), apps (listar, abrir),
Painel, arquivos (listar, ler, escrever, criar pasta, mover, apagar), comando, histórias e notificar.

**Faltam no novo** (nenhuma bloqueia a ISO): `apps.close/focus`, `windows.list/move/fullscreen`, `audio.outputs/output`,
`media.now_playing/play_video`, `music.play`, `games.*`, `files.open/search/recent`, `wifi.status/connect`,
`clipboard.get/set`, `screenshot`, `power`, `store.search/install`, `service.list/control`, `macro.*`, `automation.*`
(as automações do Painel são só os lembretes).

## Atalhos: Hyprland (Super) × Orbit (Alt)

O Orbit usa **Alt** como modificador principal; o Hyprland usava **Super**. Já existem no Orbit: Super sozinho (início),
Super+Tab/Shift+Tab (modos), Super+M (Mídia), Super+A / Shift+A (Emma), Print/Shift+Print/Ctrl+Print (captura),
Super+setas (encaixar), Super+H (minimizar), XF86PowerOff, volume e brilho. **Sem atalho no Orbit hoje:** abrir Painel
(Super+I), Arquivos (Super+E), navegador (Super+B), bloquear (Super+Shift+L), menu de energia (Super+Shift+E),
terminal por Super+Enter (o do Orbit é Alt+Enter) e as teclas de mídia (XF86AudioPlay/Next/Prev). Dá para
configurar no `orbit.ini`; decidir quais entram no padrão.

## O que o Orbit ainda não fala (Wayland)

Tem: layer-shell, session-lock, screencopy, gamma, DPMS, idle (notifier e inhibit), input-method e text-input,
virtual keyboard e pointer, foreign-toplevel, output-management, fractional-scale, viewporter, xdg-decoration e
(`orbit/protocolos.c`) primary-selection, wlr-data-control (e ext-data-control no wlroots 0.19+), cursor-shape,
xdg-activation, single-pixel-buffer, alpha-modifier, relative-pointer e pointer-constraints (travar e confinar o ponteiro).
**Falta:** presentation-time, linux-dmabuf explícito (conferir), tearing, keyboard-shortcuts-inhibit, security-context,
ícone do app na barra de título (xdg-toplevel-icon). Nenhuma impede de logar e usar.

## Ambiente de execução (descoberto empacotando)

- O Orbit roda **aninhado no seu Arch com wlroots 0.20** (o `testar-desktop.sh`), então o 0.20 já foi exercitado
  por você, mas **nunca num DRM de verdade**.
- Faltavam na sessão real: subir `lumna-connect`, `lumna-emma` e `lumna-midia servico` (só o `testar-desktop.sh` subia)
  e avisar o D-Bus do `WAYLAND_DISPLAY` (sem isso os portais não acham a tela). Corrigido em `lumna-orbit-session`.
- `lumna-mixer` chamava o `lumna-ctl` (shell antigo) no botão "Ajustes de áudio": agora abre o Painel em Som > Saída.
- O `lumna-emma` (backend Python) dependia do `lumna-shell` sem precisar: tirei.
- O launcher e o app de apps ignoravam `OnlyShowIn`/`NotShowIn`: agora respeitam (os atalhos antigos aparecem só na
  sessão clássica).
- O repositório `[lumna]` e o ISO de teste da UI antiga **não podem** ser tocados pela ISO nova: ela usa canais próprios
  (`repo-orbit`, `teste-orbit`) e o instalador grava o canal certo no sistema instalado.
