# LUMNA OS

> Seu espaço. Seus modos. Sua Emma.

Distro Linux com três modos (**Desktop**, **Gaming** e **Mídia**) que trocam com `Super + Tab`.
O shell é feito em **Tauri + Vue** e roda sobre o **Hyprland**, com base no **Arch Linux**.
Suporta só GPUs **AMD e Intel**.

- Por que é assim: [docs/VIABILIDADE.md](docs/VIABILIDADE.md)
- Onde estamos: **Fase 1**. O ISO dá boot direto no LUMNA, com topbar, dock, launcher de apps e avisos.

![LUMNA OS: topbar, dock e um aviso da Emma](docs/fase1-desktop.png)

![Launcher de apps](docs/fase1-launcher.png)

## Estrutura

```
crates/lumna-core     tipos e o contrato Compositor (não sabe qual compositor é)
crates/lumna-hypr     Compositor implementado para o Hyprland (IPC por socket)
crates/lumna-ctl      comando que os atalhos usam para falar com o shell
shell/                lumna-shell: app Tauri (src-tauri/) + interface Vue (src/)
session/              config do Hyprland em Lua, script da sessão, entrada do login, wallpaper
packages/             PKGBUILDs dos pacotes lumna-*
iso/                  perfil do ISO (releng do archiso + overlay do LUMNA) e ambiente de build
scripts/              build-iso.sh, run-vm.sh, make-art.py
```

## Montar o ISO

Em qualquer Linux com Docker:

```
./scripts/build-iso.sh
```

Compila os pacotes `lumna-*`, valida a config do Hyprland e gera `out/lumna-os-AAAA.MM.DD-x86_64.iso`.
O GitHub Actions faz o mesmo a cada push na `main`, e publica o ISO na Release quando sai uma tag `v*`.

## Testar na VM

Num Linux com KVM, QEMU e OVMF:

```
./scripts/run-vm.sh                # 1 monitor
./scripts/run-vm.sh --telas 3      # 3 monitores virtuais
./scripts/run-vm.sh --disco        # com disco virtual (testes de instalação, Fase 7)
```

A VM usa virtio-gpu com OpenGL (virgl). VirtualBox e VMware costumam ir mal com o Hyprland.

## Testar no PC (pendrive)

Grave o ISO com Ventoy, balenaEtcher ou `dd`, desligue o **Secure Boot** e dê boot pelo pendrive.
Ele roda direto do pendrive (modo live) e não mexe no disco.

## Atalhos

| Atalho | Ação |
|---|---|
| `Super + Espaço` | Launcher de apps (busca sem acento; setas e Enter; Esc fecha) |
| `Super + Enter` | Terminal |
| `Super + E` | Arquivos (Thunar) |
| `Super + B` | Firefox |
| `Super + Q` | Fechar janela |
| `Super + 1..9` | Ir para a área de trabalho |
| `Super + Shift + 1..9` | Mover janela para a área de trabalho |
| `Super + F` | Tela cheia |
| `Super + Shift + E` | Sair da sessão |

Na topbar: clique no cristal abre o launcher, clique no volume silencia e a roda do mouse ajusta.
Os avisos dos apps (`notify-send`, Firefox...) aparecem no canto superior direito.

## Problemas

O log do shell fica no journal: `journalctl -t lumna-shell`.
